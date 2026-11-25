/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host tests for the engine: routing, families, instruments, release, Q23. */
#include <stdio.h>
#include <string.h>
#include "orch_engine.h"
#include "orch_stage.h"
#include "orch_resonance.h"
#include "orch_cc.h"

static unsigned failures;
static int32_t block[256];

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static void render(unsigned blocks)
{
    for (unsigned b = 0; b < blocks; ++b) {
        memset(block, 0, sizeof block);
        orch_render_q23(block, 128U);
    }
}

static int64_t render_energy(unsigned blocks)
{
    int64_t energy = 0;
    for (unsigned b = 0; b < blocks; ++b) {
        memset(block, 0, sizeof block);
        orch_render_q23(block, 128U);
        for (unsigned i = 0; i < 256U; ++i) energy += block[i] < 0 ? -block[i] : block[i];
    }
    return energy;
}

/* one side of the stereo block, summed over a few blocks */
static int64_t side_energy(unsigned blocks, unsigned right)
{
    int64_t energy = 0;
    for (unsigned b = 0; b < blocks; ++b) {
        memset(block, 0, sizeof block);
        orch_render_q23(block, 128U);
        for (unsigned i = 0; i < 256U; i += 2U) {
            const int64_t v = block[i + right];
            energy += v < 0 ? -v : v;
        }
    }
    return energy;
}

static void midi(unsigned status, unsigned d1, unsigned d2)
{
    const uint8_t m[3] = { (uint8_t)status, (uint8_t)d1, (uint8_t)d2 };
    orch_midi_short_message(m, 3U);
}

/* CC7 fully open on every channel (XG powers on at 100): the gain bounds
 * are held at the loudest a controller can make it */
static void full_volume(void)
{
    for (unsigned ch = 0; ch < 16U; ++ch) midi(0xB0U | ch, 7U, 127U);
}

/* Only part 0 listens, on MIDI channel 1, with the given family. */
static void solo_part(unsigned family)
{
    orch_init();
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        CHECK(orch_engine_configure_part(p, family, p == 0U ? 0x000FU : 0U) == 0);
}

static uint32_t q23_power(uint32_t base, unsigned exponent)
{
    uint64_t value = 8388608U;
    for (unsigned i = 0; i < exponent; ++i)
        value = (value * base) >> 23;
    return (uint32_t)value;
}

extern uint16_t orch_test_level_compensation_gain(unsigned velocity, uint32_t level);
extern uint16_t orch_test_register_target_scale_q15(unsigned note);

static void test_instrument_level_compensation_bounds(void)
{
    const uint16_t quiet = orch_test_level_compensation_gain(127U, 1000U);
    const uint16_t loud = orch_test_level_compensation_gain(127U, 30000U);
    const uint16_t full_velocity = orch_test_level_compensation_gain(127U, 10000U);
    const uint16_t half_velocity = orch_test_level_compensation_gain(64U, 10000U);
    CHECK(quiet == 65535U);       /* no more than +6 dB */
    CHECK(loud == 16384U);        /* no more than -6 dB */
    CHECK(full_velocity > half_velocity);
    CHECK(half_velocity > 0U);
    CHECK(orch_test_level_compensation_gain(127U, 32U) == 32767U);
    CHECK(orch_test_register_target_scale_q15(36U) >
          orch_test_register_target_scale_q15(60U));
    CHECK(orch_test_register_target_scale_q15(60U) >
          orch_test_register_target_scale_q15(108U));
    CHECK(orch_test_register_target_scale_q15(60U) == 32767U);
}

static void test_round_trip_loss(void)
{
    const uint32_t low_note = orch_test_loop_gain_per_sample_q23(32400U, 400U);
    const uint32_t high_note = orch_test_loop_gain_per_sample_q23(32400U, 67U);
    const uint32_t low_trip = q23_power(low_note, 400U);
    const uint32_t high_trip = q23_power(high_note, 67U);
    const uint32_t expected = (uint32_t)(((uint64_t)32400U * 8388608U) / 32768U);
    const uint32_t tolerance = expected / 1000U;
    const uint32_t low_error = low_trip > expected ? low_trip - expected : expected - low_trip;
    const uint32_t high_error = high_trip > expected ? high_trip - expected : expected - high_trip;
    CHECK(low_note > high_note);
    CHECK(low_error < tolerance);
    CHECK(high_error < tolerance);
    CHECK(low_note < 8388608U && high_note < 8388608U);
}

/* Measure the bowed-string fundamental with autocorrelation rather than
 * choosing the strongest spectral partial (which may be the second harmonic). */
static void test_string_pitch_autocorrelation(void)
{
    static int32_t captured[8192];
    solo_part(ORCH_FAM_STRINGS);
    midi(0x90U, 45U, 100U); /* A2 = 110 Hz */
    for (unsigned b = 0; b < 64U; ++b) {
        memset(block, 0, sizeof block);
        orch_render_q23(block, 128U);
        for (unsigned i = 0; i < 128U; ++i)
            captured[b * 128U + i] = block[i * 2U] >> 8;
    }

    int64_t best_score = INT64_MIN;
    unsigned best_lag = 0U;
    for (unsigned lag = 360U; lag <= 440U; ++lag) {
        int64_t score = 0;
        for (unsigned n = 2048U; n < 8192U - lag; ++n)
            score += (int64_t)captured[n] * captured[n - lag];
        if (score > best_score) { best_score = score; best_lag = lag; }
    }
    CHECK(best_lag >= 390U && best_lag <= 410U);
    orch_init();
}

/* The period of a held note, refined between lags (parabolic), in samples. */
static double held_period(unsigned family, unsigned note, unsigned lo, unsigned hi)
{
    static int32_t cap[12288];
    solo_part(family);
    midi(0x90U, note, 100U);
    for (unsigned b = 0; b < 96U; ++b) {
        memset(block, 0, sizeof block);
        orch_render_q23(block, 128U);
        for (unsigned i = 0; i < 128U; ++i) cap[b * 128U + i] = block[i * 2U] >> 8;
    }
    double best = -1e300, sc[3] = { 0, 0, 0 };
    unsigned bl = lo;
    for (unsigned lag = lo; lag <= hi; ++lag) {
        double score = 0;
        for (unsigned n = 4096U; n < 12288U - hi - 1U; ++n)
            score += (double)cap[n] * cap[n + lag];
        if (score > best) { best = score; bl = lag; }
    }
    for (int k = -1; k <= 1; ++k) {
        double score = 0;
        for (unsigned n = 4096U; n < 12288U - hi - 1U; ++n)
            score += (double)cap[n] * cap[n + (unsigned)((int)bl + k)];
        sc[k + 1] = score;
    }
    const double den = sc[0] - 2.0 * sc[1] + sc[2];
    orch_init();
    return (double)bl + (den != 0.0 ? 0.5 * (sc[0] - sc[2]) / den : 0.0);
}

/* E5 is 66.89 samples a period. 0.5.0 played every note at SR / whole
 * (66.0, +23 c) because the fractional read went the wrong way and the
 * wind/brass/voice oscillators divided by whole samples. 0.3 % is 5 cents. */
static void test_high_register_tuning(void)
{
    static const unsigned fam[4] = {
        ORCH_FAM_STRINGS, ORCH_FAM_WOODWINDS, ORCH_FAM_BRASS, ORCH_FAM_CHOIR
    };
    for (unsigned f = 0; f < 4U; ++f) {
        const double p = held_period(fam[f], 76U, 62U, 72U);
        CHECK(p > 66.89 * 0.997 && p < 66.89 * 1.003);
    }
    /* and the middle of the range, C4 = 168.56 samples */
    const double c4 = held_period(ORCH_FAM_WOODWINDS, 60U, 160U, 176U);
    CHECK(c4 > 168.56 * 0.997 && c4 < 168.56 * 1.003);
}

static int32_t peak_of(unsigned blocks)
{
    int32_t top = 0;
    for (unsigned b = 0; b < blocks; ++b) {
        memset(block, 0, sizeof block);
        orch_render_q23(block, 128U);
        for (unsigned i = 0; i < 256U; ++i) {
            const int32_t a = block[i] < 0 ? -block[i] : block[i];
            if (a > top) top = a;
        }
    }
    return top;
}

/* Even mix: every voice an equal share of full scale (1 / n), with no limiter.
 * Every instrument alone at velocity 127 across C2..C7, and eight parts
 * together, stay under full scale; the share does not step the level. */
static void test_gain_staging(void)
{
    int32_t loudest = 0;
    for (unsigned inst = 0; inst < ORCH_INST_COUNT; ++inst) {
        for (unsigned note = 36U; note <= 96U; note += 12U) {
            orch_init();
            full_volume();
            orch_engine_set_instrument(0U, inst);
            orch_engine_set_player_cap(0U, 1U);
            midi(0x90U, note, 127U);
            const int32_t p = peak_of(500U);
            if (p > loudest) loudest = p;
        }
    }
    CHECK(loudest < 8388607);
    orch_init();
    full_volume();
    for (unsigned p = 0; p < 8U; ++p) {
        orch_engine_set_player_cap(p, 1U);
        midi(0x90U | p, 55U + 3U * p, 127U);
    }
    const int32_t all = peak_of(700U);
    CHECK(all > 0 && all < 8000000);
    /* the worst there is for the 1.36 / n share: all eight parts double
     * basses on the same C2 at 127, fully open */
    orch_init();
    full_volume();
    for (unsigned p = 0; p < 8U; ++p) {
        orch_engine_set_instrument(p, ORCH_INST_DOUBLE_BASS);
        orch_engine_set_player_cap(p, 1U);
        midi(0x90U | p, 36U, 127U);
    }
    const int32_t unison = peak_of(700U);
    CHECK(unison > 0 && unison < 8388607);
    orch_init();
}

static int64_t note_energy(unsigned velocity)
{
    orch_init();
    orch_engine_set_player_cap(0U, 1U);
    midi(0x90U, 60U, velocity);
    return render_energy(200U);
}

/* The controllers, as a Yamaha XG part answers them (docs/MIDI.md). */
static void test_midi_controllers(void)
{
    /* velocity: every step counts. 1 is soft but there (about -33 dB of
     * 127, not -63 as in 0.6.3), 64 sits in between, and it only rises */
    const int64_t e1 = note_energy(1U), e64 = note_energy(64U), e127 = note_energy(127U);
    CHECK(e1 > 0 && e1 * 10 > e64 / 10 && e1 * 20 < e127);
    CHECK(e64 * 3 < e127 && e64 * 8 > e127);
    int64_t last = 0;
    for (unsigned v = 8U; v <= 127U; v += 17U) {
        const int64_t e = note_energy(v);
        CHECK(e > last);
        last = e;
    }

    /* CC11 expression on the GM2 curve: 64 is about -12 dB, 0 silent (the
     * shared strings off, or they would ring on) */
    orch_init();
    orch_resonance_set_amount(0U);
    midi(0x90U, 60U, 100U);
    render(100U);
    const int64_t open = render_energy(40U);
    midi(0xB0U, 11U, 64U);
    render(4U);
    const int64_t half = render_energy(40U);
    CHECK(half * 3 < open && half * 6 > open);
    midi(0xB0U, 11U, 0U);
    render(12U);
    CHECK(render_energy(10U) == 0);
    /* CC121 brings the expression back */
    midi(0xB0U, 121U, 0U);
    render(1U);
    CHECK(orch_cc_value(0U, 11U) == 127U);

    /* CC2 breath: quieter and darker at 0, never silent */
    orch_init();
    orch_resonance_set_amount(0U);
    midi(0xB0U, 2U, 0U);
    midi(0x90U, 60U, 100U);
    render(100U);
    const int64_t soft = render_energy(40U);
    CHECK(soft > 0 && soft * 8 < open);

    /* CC10: 0 is hard left, 127 hard right */
    orch_init();
    orch_resonance_set_amount(0U);
    midi(0xB0U, 10U, 0U);
    midi(0x90U, 60U, 100U);
    render(60U);
    CHECK(side_energy(20U, 1U) == 0 && side_energy(20U, 0U) > 0);

    /* pitch bend: +2 semitones by default, RPN 0 sets 12 */
    orch_init();
    midi(0xE0U, 127U, 127U);
    render(10U);
    CHECK(orch_cc_bend_q16[0] > 58300U && orch_cc_bend_q16[0] < 58480U);
    midi(0xB0U, 101U, 0U);
    midi(0xB0U, 100U, 0U);
    midi(0xB0U, 6U, 12U);
    render(10U);
    CHECK(orch_cc_bend_range(0U) == 12U);
    CHECK(orch_cc_bend_q16[0] > 32700U && orch_cc_bend_q16[0] < 32830U);
    midi(0xE0U, 0U, 64U);                      /* centre */
    render(10U);
    CHECK(orch_cc_bend_q16[0] == 65536U);

    /* CC1 is vibrato: the wheel alone runs the LFO */
    orch_init();
    midi(0xB0U, 1U, 127U);
    render(2U);
    CHECK(orch_cc_mod[0] == 100U);

    /* bank select + program change: bank 3 is brass, program 1 its second */
    orch_init();
    midi(0xB0U, 0U, 3U);
    {
        const uint8_t pc[2] = { 0xC0U, 1U };
        orch_midi_short_message(pc, 2U);
    }
    render(1U);
    CHECK(orch_part_get(0U)->instrument == ORCH_INST_FRENCH_HORN);

    /* the sound controllers are relative, as on XG: 64 is the instrument's
     * own attack, 0 the shortest, 127 the longest */
    {
        struct orch_adsr d;
        orch_instrument_adsr(ORCH_INST_FRENCH_HORN, &d);
        midi(0xB0U, 73U, 64U);
        render(1U);
        CHECK(orch_part_get(0U)->adsr.attack_ms == d.attack_ms);
        midi(0xB0U, 73U, 0U);
        render(1U);
        CHECK(orch_part_get(0U)->adsr.attack_ms == 1U);
        midi(0xB0U, 73U, 127U);
        render(1U);
        CHECK(orch_part_get(0U)->adsr.attack_ms == 4000U);
    }
    /* The adaptive layer hears the played dynamic: the same note under CC2
     * breath 0 reads as a lighter line than at breath 127 */
    {
        uint32_t energy[2];
        for (unsigned k = 0; k < 2U; ++k) {
            orch_init();
            midi(0xB0U, 2U, k ? 0U : 127U);
            render(2U);
            midi(0x90U, 60U, 100U);
            render(2U);
            energy[k] = orch_last_intent(0U)->energy;
        }
        CHECK(energy[1] < energy[0]);
    }
    /* the ARP and the sequencer by controller, full range */
    orch_init();
    midi(0xB0U, 80U, 127U);                    /* ARP on */
    midi(0xB0U, 16U, 127U);                    /* the last type */
    midi(0xB0U, 102U, 77U);                    /* density, 1:1 */
    midi(0xB0U, 106U, 127U);                   /* ratchet 4 */
    midi(0xB0U, 86U, 0U);                      /* sequence length 1 */
    midi(0xB0U, 87U, 127U);                    /* this part's lane muted */
    render(1U);
    {
        struct orch_perf_config pc;
        orch_perf_get_config(0U, &pc);
        CHECK(pc.arp_enabled == 1U);
        CHECK(pc.arp_type == ORCH_PERF_ARP_TYPES - 1U);
        CHECK(pc.density == 77U);
        CHECK(pc.ratchet == 4U);
        CHECK(orch_seq_length() == 1U);
        CHECK(orch_seq_lane_muted(0U) == 1U);
    }
    orch_init();
    orch_seq_set_lane_muted(0U, 0U);
    orch_seq_set_length(16U);

    /* CC71 is the shared resonance, 64 its default */
    midi(0xB0U, 71U, 64U);
    render(1U);
    CHECK(orch_resonance_amount() == 25U);
    midi(0xB0U, 71U, 127U);
    render(1U);
    CHECK(orch_resonance_amount() == 100U);
    orch_resonance_set_amount(25U);
    orch_init();
}

/* Every part has its own lane: two lanes play at once, each on its part; a
 * muted lane is silent; the length wraps the steps; a 0 % step never plays
 * and a 50 % one some of the time. */
static void test_sequencer_lanes(void)
{
    struct orch_step_note n = { 60U, 100U, 80U, 100U };
    orch_init();
    orch_seq_set_tempo(240U);
    orch_seq_set_part(0U);
    CHECK(orch_seq_set_step(0U, 0U, &n) == 0);
    n.note = 48U;
    orch_seq_set_part(3U);
    CHECK(orch_seq_set_step(0U, 0U, &n) == 0);
    orch_seq_set_enabled(1U);
    render(10U);
    CHECK(orch_voices_of_part(0U) > 0U);
    CHECK(orch_voices_of_part(3U) > 0U);
    orch_seq_set_enabled(0U);
    render(400U);

    orch_seq_set_lane_muted(3U, 1U);
    orch_seq_set_enabled(1U);
    render(10U);
    CHECK(orch_voices_of_part(0U) > 0U);
    CHECK(orch_voices_of_part(3U) == 0U);
    orch_seq_set_enabled(0U);
    render(400U);

    /* length 2 at 240 BPM: a sixteenth is 2756 samples, so step 0 comes back
     * after 5512 and the position never reaches step 2 */
    orch_seq_set_length(2U);
    orch_seq_set_enabled(1U);
    unsigned top = 0U;
    for (unsigned b = 0; b < 200U; ++b) {
        render(1U);
        if (orch_seq_position() != 0xFFU && orch_seq_position() > top) top = orch_seq_position();
    }
    CHECK(top == 1U);
    orch_seq_set_enabled(0U);
    CHECK(orch_seq_set_step(0U, 0U, &(struct orch_step_note){ 60U, 100U, 80U, 101U }) == 0);

    /* chance: 50 % over 64 passes plays somewhere between a quarter and
     * three quarters of them */
    orch_init();
    orch_seq_set_tempo(240U);
    orch_seq_set_length(1U);
    orch_seq_set_step(0U, 0U, &(struct orch_step_note){ 60U, 100U, 20U, 50U });
    orch_seq_set_enabled(1U);
    unsigned played = 0U, was = 0U;
    for (unsigned b = 0; b < 64U * 22U; ++b) {
        render(1U);
        const unsigned now = orch_note_count(0U);
        if (now && !was) ++played;
        was = now;
    }
    CHECK(played > 12U && played < 52U);
    orch_seq_set_enabled(0U);
    orch_init();
}

/* Energy of the tail after a held note: 0.3..0.9 s after its release. */
static int64_t tail_after(unsigned note, unsigned amount)
{
    orch_init();
    orch_resonance_set_amount(amount);
    midi(0x90U, note, 110U);
    render(350U);                             /* 1 s */
    midi(0x80U, note, 0U);
    render(100U);
    int64_t e = 0;
    for (unsigned b = 0; b < 200U; ++b) {
        memset(block, 0, sizeof block);
        orch_render_q23(block, 128U);
        for (unsigned i = 0; i < 256U; ++i) e += block[i] < 0 ? -block[i] : block[i];
    }
    return e;
}

/* The shared resonance: a note on one of its strings (G3) leaves them ringing
 * after it, a note between them (G#3) much less; at 0 it is bypassed; and the
 * full orchestra at velocity 127 with the most resonance stays under full
 * scale, with no limiter. */
static void test_shared_resonance(void)
{
    const int64_t g3_off = tail_after(55U, 0U);
    const int64_t g3_on = tail_after(55U, 100U);
    const int64_t gs3_off = tail_after(56U, 0U);
    const int64_t gs3_on = tail_after(56U, 100U);
    CHECK(g3_on > g3_off);
    CHECK(g3_on - g3_off > 2 * (gs3_on > gs3_off ? gs3_on - gs3_off : 0));

    orch_init();
    full_volume();
    orch_resonance_set_amount(100U);
    for (unsigned p = 0; p < 8U; ++p) {
        orch_engine_set_player_cap(p, 1U);
        midi(0x90U | p, 43U + 7U * (p & 3U), 127U);
    }
    const int32_t all = peak_of(700U);
    CHECK(all > 0 && all < 8388607);
    orch_resonance_set_amount(25U);
    orch_init();
}

static void test_routing(void)
{
    orch_init();
    midi(0x90U, 60U, 100U);      /* channel 1 -> part 0 */
    render(2U);
    CHECK(orch_voices_of_part(0U) >= 1U);
    CHECK(orch_voices_of_part(1U) == 0U);
    CHECK(orch_note_count(0U) == 1U);

    orch_reset_notes();
    render(1U);
    CHECK(orch_active_voices() == 0U);
    CHECK(orch_note_count(0U) == 0U);

    midi(0x91U, 64U, 100U);      /* channel 2 -> part 1 */
    render(2U);
    CHECK(orch_voices_of_part(1U) >= 1U);
    CHECK(orch_voices_of_part(0U) == 0U);
    orch_init();
}


static void test_eight_part_routing(void)
{
    orch_init();
    for (unsigned p = 0; p < 8U; ++p) {
        CHECK(orch_part_for_channel(p) == p);
        midi(0x90U | p, 60U + p, 100U);
    }
    render(8U);
    for (unsigned p = 0; p < 8U; ++p)
        CHECK(orch_note_count(p) == 1U);
    CHECK(orch_active_voices() <= ORCH_MAX_VOICES);
    for (unsigned p = 0; p < 8U; ++p)
        midi(0x80U | p, 60U + p, 0U);
    render(500U);
    CHECK(orch_active_voices() == 0U);
    orch_init();
}

static void test_repeated_same_pitch_note_off_fifo(void)
{
    orch_init();
    CHECK(orch_engine_set_player_cap(0U, 1U) == 0);
    midi(0x90U, 60U, 96U);
    render(2U);
    midi(0x90U, 60U, 112U);
    render(2U);
    CHECK(orch_note_count(0U) == 2U);

    /* The first Note Off consumes only the oldest note instance. */
    midi(0x80U, 60U, 0U);
    render(2U);
    CHECK(orch_note_count(0U) == 1U);
    CHECK(orch_voices_of_part(0U) >= 1U);

    /* The second Note Off releases the newer instance. */
    midi(0x80U, 60U, 0U);
    render(500U);
    CHECK(orch_note_count(0U) == 0U);
    CHECK(orch_active_voices() == 0U);
    orch_init();
}

static void test_families(void)
{
    static const unsigned families[ORCH_FAM_COUNT] = {
        ORCH_FAM_STRINGS, ORCH_FAM_WOODWINDS, ORCH_FAM_BRASS,
        ORCH_FAM_MALLETS, ORCH_FAM_CHOIR
    };
    for (unsigned f = 0; f < ORCH_FAM_COUNT; ++f) {
        solo_part(families[f]);
        midi(0x90U, 60U, 100U);
        CHECK(render_energy(30U) > 0);
        midi(0x80U, 60U, 0U);
        render(600U);
        CHECK(orch_active_voices() == 0U);
    }
    CHECK(orch_engine_configure_part(0U, ORCH_FAM_COUNT, 0x000FU) == -1);
    orch_init();
}

static void test_instruments(void)
{
    /* every instrument is selectable and stays inside the Q23 range */
    for (unsigned inst = 0; inst < ORCH_INST_COUNT; ++inst) {
        solo_part(ORCH_FAM_STRINGS);
        CHECK(orch_engine_set_instrument(0U, inst) == 0);
        midi(0x90U, 57U, 100U);
        int64_t peak = 0;
        for (unsigned b = 0; b < 30U; ++b) {
            memset(block, 0, sizeof block);
            orch_render_q23(block, 128U);
            for (unsigned i = 0; i < 256U; ++i) {
                const int64_t v = block[i];
                if (v > 8388607 || v < -8388608) { CHECK(0 && "out of Q23"); return; }
                if (v < 0 ? -v > peak : v > peak) peak = v < 0 ? -v : v;
            }
        }
        CHECK(peak > 0);
    }
    CHECK(orch_engine_set_instrument(0U, ORCH_INST_COUNT) == -1);
    CHECK(orch_engine_set_instrument(9U, 0U) == -1);
    orch_init();
}

static void test_divisi_players(void)
{
    solo_part(ORCH_FAM_STRINGS);
    CHECK(orch_engine_set_player_cap(0U, 2U) == 0);
    midi(0x90U, 60U, 100U);
    render(2U);
    CHECK(orch_voices_of_part(0U) == 2U);   /* the phrase opens with two players */

    orch_reset_notes();
    render(1U);
    CHECK(orch_engine_set_player_cap(0U, 1U) == 0);
    midi(0x90U, 62U, 100U);
    render(2U);
    CHECK(orch_voices_of_part(0U) == 1U);
    orch_init();
}

static int64_t measure_adsr_attack_energy(unsigned attack_ms)
{
    int64_t energy = 0;
    solo_part(ORCH_FAM_STRINGS);
    CHECK(orch_engine_set_adsr(0U, attack_ms, 180U, 82U, 350U) == 0);
    midi(0x90U, 60U, 100U);
    /* First ~26 ms: the ADSR must shape both the output and the excitation. */
    energy = render_energy(9U);
    orch_init();
    return energy;
}

static void test_adsr_shapes_transient_energy(void)
{
    const int64_t fast = measure_adsr_attack_energy(5U);
    const int64_t slow = measure_adsr_attack_energy(260U);
    CHECK(fast > 0);
    CHECK(slow > 0);
    CHECK(fast > slow * 2);
}

static void test_overlapping_notes_use_legato_handover(void)
{
    solo_part(ORCH_FAM_STRINGS);
    CHECK(orch_engine_set_player_cap(0U, 1U) == 0);
    midi(0x90U, 60U, 96U);
    render(24U);
    CHECK(orch_voices_of_part(0U) == 1U);

    /* A second pitch arrives before the first Note Off: this is a true
     * overlapping legato handover, not a fresh attack from silence. */
    midi(0x90U, 64U, 104U);
    render(8U);
    CHECK(orch_last_intent(0U) && orch_last_intent(0U)->connected);
    CHECK(orch_note_count(0U) == 2U);
    CHECK(orch_voices_of_part(0U) == 1U);
    CHECK(orch_active_voices() == 1U);

    /* Releasing the older note must not release the reassigned voice. */
    midi(0x80U, 60U, 0U);
    render(2U);
    CHECK(orch_voices_of_part(0U) == 1U);
    midi(0x80U, 64U, 0U);
    render(500U);
    CHECK(orch_active_voices() == 0U);
    orch_init();
}

static void test_connected_legato_preserves_waveguide(void)
{
    int64_t before = 0;
    solo_part(ORCH_FAM_STRINGS);
    /* Isolate one resonator so this test measures legato reuse rather than
     * the default two-player divisi arrangement. */
    CHECK(orch_engine_set_player_cap(0U, 1U) == 0);
    CHECK(orch_engine_set_adsr(0U, 180U, 220U, 82U, 420U) == 0);
    midi(0x90U, 60U, 100U);
    for (unsigned i = 0; i < 18U; ++i)
        before = render_energy(1U);
    CHECK(before > 0);

    /* A short, intentional gap should be interpreted as connected. The
     * released resonator is reused and retuned, rather than restarted silent. */
    midi(0x80U, 60U, 0U);
    render(1U);
    midi(0x90U, 62U, 104U);
    const int64_t after = render_energy(1U);
    CHECK(orch_last_intent(0U) && orch_last_intent(0U)->connected);
    CHECK(after > before / 3);
    CHECK(orch_voices_of_part(0U) == 1U);
    CHECK(orch_active_voices() == 1U);
    midi(0x80U, 62U, 0U);
    render(500U);
    CHECK(orch_active_voices() == 0U);
    orch_init();
}

static void test_adaptive_sustain_expression(void)
{
    int32_t low = 32767, high = -32768;
    solo_part(ORCH_FAM_STRINGS);
    CHECK(orch_engine_set_adsr(0U, 2U, 2U, 100U, 900U) == 0);
    midi(0x90U, 60U, 100U);
    for (unsigned b = 0; b < 1400U; ++b) {
        render(1U);
        {
            const int32_t value = orch_test_expression_q15();
            if (value < low) low = value;
            if (value > high) high = value;
        }
    }
    CHECK(high > low + 500);
    orch_init();

    /* Percussive mallets should not acquire an artificial held-note swell. */
    solo_part(ORCH_FAM_MALLETS);
    midi(0x90U, 60U, 100U);
    render(20U);
    CHECK(orch_test_expression_q15() == 0);
    orch_init();
}

static void test_release_and_sustain(void)
{
    solo_part(ORCH_FAM_STRINGS);
    midi(0x90U, 60U, 100U);
    render(4U);
    CHECK(orch_active_voices() > 0U);
    midi(0x80U, 60U, 0U);
    render(400U);
    CHECK(orch_active_voices() == 0U);

    orch_init();
    midi(0xB0U, 64U, 127U);
    midi(0x90U, 60U, 100U);
    render(4U);
    midi(0x80U, 60U, 0U);
    render(20U);
    CHECK(orch_active_voices() > 0U);
    midi(0xB0U, 64U, 0U);
    render(400U);
    CHECK(orch_active_voices() == 0U);
    orch_init();
}

static void test_arp_phrase_integration(void)
{
    struct orch_perf_config config;
    orch_init();
    orch_perf_default_config(&config);
    config.arp_enabled=1U;
    config.arp_type=0U;
    config.bpm=120U;
    CHECK(orch_perf_set_config(0U,&config)==0);
    orch_note_event(0U,60U,100U);
    render(8U);
    CHECK(orch_active_voices()>0U);
    orch_note_event(0U,60U,0U);
    render(600U);
    CHECK(orch_active_voices()==0U);

    orch_init();
    orch_perf_default_config(&config);
    config.phrase_enabled=1U;
    config.phrase_ge=0U;
    config.density=127U;
    CHECK(orch_perf_set_config(0U,&config)==0);
    orch_note_event(0U,64U,100U);
    render(8U);
    CHECK(orch_active_voices()>0U);
    orch_note_event(0U,64U,0U);
    render(600U);
    CHECK(orch_active_voices()==0U);
    orch_init();
}

static void test_external_midi_clock(void)
{
    struct orch_perf_config config;
    orch_init();
    orch_perf_default_config(&config);
    config.arp_enabled=1U;
    config.external_clock=1U;
    CHECK(orch_perf_set_config(0U,&config)==0);
    orch_note_event(0U,60U,100U);
    render(2U);
    CHECK(orch_active_voices()==0U);
    midi(0xFAU,0U,0U);
    midi(0xF8U,0U,0U);
    render(4U);
    CHECK(orch_active_voices()>0U);
    orch_note_event(0U,60U,0U);
    midi(0xFCU,0U,0U);
    render(600U);
    CHECK(orch_active_voices()==0U);
    orch_init();
}

static void test_sequencer_phrase_arc(void)
{
    CHECK(orch_test_seq_phrase_arc_velocity(100U,0U) <
          orch_test_seq_phrase_arc_velocity(100U,8U));
    CHECK(orch_test_seq_phrase_arc_velocity(100U,15U) <
          orch_test_seq_phrase_arc_velocity(100U,8U));
}

static void test_sequencer(void)
{
    struct orch_step_note note;
    orch_init();
    orch_seq_clear();
    /* The arranger snaps to a held chord only when explicitly enabled. */
    orch_note_event(0U, 60U, 100U);
    render(1U);
    orch_note_event(0U, 64U, 100U);
    render(1U);
    orch_note_event(0U, 67U, 100U);
    render(1U);
    CHECK(orch_test_seq_adapt_note(0U, 62U) == 62U);
    orch_seq_set_adaptive(1U);
    CHECK(orch_test_seq_adapt_note(0U, 62U) == 64U);
    /* The same stored step is gently pulled toward the player's register. */
    orch_note_event(0U, 79U, 100U);
    render(1U);
    orch_note_event(0U, 79U, 0U);
    render(1U);
    CHECK(orch_test_seq_adapt_note(0U, 62U) == 67U);
    orch_seq_set_adaptive(0U);
    orch_init();
    orch_seq_clear();
    note.note = 60U; note.velocity = 100U; note.gate_percent = 50U; note.chance = 100U;
    CHECK(orch_seq_set_step(0U, 0U, &note) == 0);
    CHECK(orch_seq_set_step(16U, 0U, &note) == -1);
    CHECK(orch_seq_set_step(0U, 9U, &note) == -1);
    orch_seq_set_tempo(240U);
    orch_seq_set_enabled(1U);
    render(20U);
    CHECK(orch_active_voices() > 0U);
    orch_seq_set_enabled(0U);
    render(400U);
    CHECK(orch_active_voices() == 0U);
    orch_init();
}

/* A bowed string must radiate a pitch, not a constant offset. This caught a
 * real bug: the friction feedback pumped DC until the peak was 2.7 M of a
 * 3.0 M signal and the "note" had no fundamental at all. */
static void test_strings_are_not_dc(void)
{
    solo_part(ORCH_FAM_STRINGS);
    midi(0x90U, 57U, 100U);
    int64_t peak = 0, sum = 0;
    long n = 0;
    for (unsigned b = 0; b < 80U; ++b) {
        memset(block, 0, sizeof block);
        orch_render_q23(block, 128U);
        for (unsigned i = 0; i < 256U; i += 2U) {
            const int64_t v = block[i];
            const int64_t a = v < 0 ? -v : v;
            if (a > peak) peak = a;
            sum += v;
            ++n;
        }
    }
    const int64_t mean = n ? sum / n : 0;
    CHECK(peak > 100000);
    CHECK(mean < peak / 4 && mean > -(peak / 4));
    orch_init();
}

static void test_assignable_lfo(void)
{
    solo_part(ORCH_FAM_STRINGS);
    CHECK(orch_engine_set_lfo(0U, ORCH_LFO_SINE,
          ORCH_LFO_PITCH | ORCH_LFO_TREMOLO | ORCH_LFO_BRIGHTNESS,
          50U, 5000U, 0U, 120U, 300U) == 0);
    midi(0x90U, 60U, 100U);
    CHECK(render_energy(40U) > 0);
    midi(0x80U, 60U, 0U);
    render(500U);
    CHECK(orch_active_voices() == 0U);

    /* Tempo-synchronised dotted-eighth and all waveform selectors validate. */
    CHECK(orch_engine_set_lfo(0U, ORCH_LFO_SAMPLE_HOLD,
          ORCH_LFO_TREMOLO, 35U, 5000U, 4U, 0U, 150U) == 0);
    CHECK(orch_engine_set_lfo(0U, ORCH_LFO_SAMPLE_HOLD,
          ORCH_LFO_TREMOLO, 35U, 5000U, 5U, 0U, 150U) == -1);
    CHECK(orch_engine_set_lfo(0U, 9U, ORCH_LFO_PITCH,
          20U, 5000U, 0U, 0U, 0U) == -1);
    CHECK(orch_engine_set_lfo(ORCH_PARTS, ORCH_LFO_SINE,
          ORCH_LFO_PITCH, 20U, 5000U, 0U, 0U, 0U) == -1);
    orch_init();
}

static void test_articulation(void)
{
    solo_part(ORCH_FAM_STRINGS);
    CHECK(orch_engine_set_articulation(0U, ORCH_ART_PIZZICATO) == 0);
    midi(0x90U, 60U, 100U);
    render(3U);
    const struct orch_intent *in = orch_last_intent(0U);
    CHECK(in && in->articulation == ORCH_ART_PIZZICATO);
    CHECK(orch_active_voices() > 0U);
    midi(0x80U, 60U, 0U);
    render(600U);
    CHECK(orch_active_voices() == 0U);
    CHECK(orch_engine_set_articulation(0U, ORCH_ART_COUNT) == -1);
    orch_init();
}

/* The pan comes from the seat of the instrument, not from a fixed pattern:
 * a violin stands on the left, the timpani on the right, and moving the
 * listener moves the whole image. */
static void test_seating_stereo(void)
{
    solo_part(ORCH_FAM_STRINGS);
    orch_stage_reset();
    CHECK(orch_engine_set_instrument(0U, ORCH_INST_VIOLIN) == 0);
    midi(0x90U, 60U, 100U);
    const int64_t vl = side_energy(20U, 0U);
    const int64_t vr = side_energy(20U, 1U);
    CHECK(vl > 0 && vr > 0);
    CHECK(vl > vr + vr / 4);            /* a violin is clearly on the left */

    solo_part(ORCH_FAM_MALLETS);
    orch_stage_reset();
    CHECK(orch_engine_set_instrument(0U, ORCH_INST_TIMPANI) == 0);
    midi(0x90U, 45U, 100U);
    const int64_t tl = side_energy(20U, 0U);
    const int64_t tr = side_energy(20U, 1U);
    CHECK(tl > 0 && tr > 0);
    CHECK(tr > tl + tl / 4);            /* the timpani is on the right */

    /* the listener crossing to the right pulls the violins towards the right
     * channel: less left than from the conductor's seat */
    solo_part(ORCH_FAM_STRINGS);
    orch_stage_reset();
    CHECK(orch_engine_set_instrument(0U, ORCH_INST_VIOLIN) == 0);
    midi(0x90U, 60U, 100U);
    const int64_t left_seat = side_energy(20U, 0U);
    orch_reset_notes();
    render(1U);
    orch_stage_set_listener(100, 0);
    midi(0x90U, 60U, 100U);
    CHECK(side_energy(20U, 0U) < left_seat);
    orch_stage_reset();
    orch_init();
}

int main(void)
{
    test_instrument_level_compensation_bounds();
    test_round_trip_loss();
    test_string_pitch_autocorrelation();
    test_high_register_tuning();
    test_gain_staging();
    test_sequencer_lanes();
    test_shared_resonance();
    test_midi_controllers();
    test_routing();
    test_eight_part_routing();
    test_repeated_same_pitch_note_off_fifo();
    test_families();
    test_instruments();
    test_divisi_players();
    test_adsr_shapes_transient_energy();
    test_connected_legato_preserves_waveguide();
    test_overlapping_notes_use_legato_handover();
    test_adaptive_sustain_expression();
    test_release_and_sustain();
    test_sequencer();
    test_sequencer_phrase_arc();
    test_arp_phrase_integration();
    test_external_midi_clock();
    test_strings_are_not_dc();
    test_articulation();
    test_assignable_lfo();
    test_seating_stereo();
    if (failures) { printf("%u engine test(s) failed\n", failures); return 1; }
    printf("orch_engine: all checks passed\n");
    return 0;
}
