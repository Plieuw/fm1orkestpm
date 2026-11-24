/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host tests for the engine: routing, families, instruments, release, Q23. */
#include <stdio.h>
#include <string.h>
#include "orch_engine.h"
#include "orch_stage.h"

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

/* Only part 0 listens, on channels 1-4, with the given family. */
static void solo_part(unsigned family)
{
    orch_init();
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        CHECK(orch_engine_configure_part(p, family, p == 0U ? 0x000FU : 0U) == 0);
}

static void test_routing(void)
{
    orch_init();
    midi(0x90U, 60U, 100U);      /* channel 1 -> part 0 */
    render(2U);
    CHECK(orch_voices_of_part(0U) >= 1U);
    CHECK(orch_voices_of_part(1U) == 0U);
    CHECK(orch_note_count(0U) == 1U);

    orch_panic();
    render(1U);
    CHECK(orch_active_voices() == 0U);
    CHECK(orch_note_count(0U) == 0U);

    midi(0x94U, 64U, 100U);      /* channel 5 -> part 1 */
    render(2U);
    CHECK(orch_voices_of_part(1U) >= 1U);
    CHECK(orch_voices_of_part(0U) == 0U);
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

    orch_panic();
    render(1U);
    CHECK(orch_engine_set_player_cap(0U, 1U) == 0);
    midi(0x90U, 62U, 100U);
    render(2U);
    CHECK(orch_voices_of_part(0U) == 1U);
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

static void test_sequencer(void)
{
    struct orch_step_note note;
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
    orch_panic();
    render(1U);
    orch_stage_set_listener(100, 0);
    midi(0x90U, 60U, 100U);
    CHECK(side_energy(20U, 0U) < left_seat);
    orch_stage_reset();
    orch_init();
}

int main(void)
{
    test_routing();
    test_families();
    test_instruments();
    test_divisi_players();
    test_release_and_sustain();
    test_sequencer();
    test_strings_are_not_dc();
    test_articulation();
    test_seating_stereo();
    if (failures) { printf("%u engine test(s) failed\n", failures); return 1; }
    printf("orch_engine: all checks passed\n");
    return 0;
}
