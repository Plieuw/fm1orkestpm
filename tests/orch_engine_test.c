/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host tests for the engine: routing, families, instruments, release, Q23. */
#include <stdio.h>
#include <string.h>
#include "orch_engine.h"

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

static void midi(unsigned status, unsigned d1, unsigned d2)
{
    const uint8_t m[3] = { (uint8_t)status, (uint8_t)d1, (uint8_t)d2 };
    orch_midi_short_message(m, 3U);
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
    printf("LEGATO DEBUG voices=%u active=%u connected=%u artic=%u density=%u\\n", orch_voices_of_part(0U), orch_active_voices(), orch_last_intent(0U)->connected, orch_last_intent(0U)->articulation, orch_last_intent(0U)->density);
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
    note.note = 60U; note.velocity = 100U; note.gate_percent = 50U;
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

int main(void)
{
    test_round_trip_loss();
    test_string_pitch_autocorrelation();
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
    if (failures) { printf("%u engine test(s) failed\n", failures); return 1; }
    printf("orch_engine: all checks passed\n");
    return 0;
}
