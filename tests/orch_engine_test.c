/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host tests for the engine: routing, divisi realization, output range. */
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

static void midi(unsigned status, unsigned d1, unsigned d2)
{
    const uint8_t m[3] = { (uint8_t)status, (uint8_t)d1, (uint8_t)d2 };
    orch_midi_short_message(m, 3U);
}

static void test_routing(void)
{
    orch_init();
    /* part 0 listens on channels 1-4, part 1 on 5-8 */
    midi(0x90U, 60U, 100U);      /* channel 1 -> strings */
    render(2U);
    CHECK(orch_voices_of_part(0U) >= 1U);
    CHECK(orch_voices_of_part(1U) == 0U);

    orch_panic();
    render(1U);
    CHECK(orch_active_voices() == 0U);

    midi(0x94U, 64U, 100U);      /* channel 5 -> woodwinds */
    render(2U);
    CHECK(orch_voices_of_part(1U) >= 1U);
    CHECK(orch_voices_of_part(0U) == 0U);
    CHECK(orch_note_count(1U) == 1U);
}

static void test_divisi_voices(void)
{
    orch_init();
    CHECK(orch_engine_set_player_cap(0U, 2U) == 0);
    midi(0x90U, 60U, 100U);
    render(2U);
    /* the first note of a phrase opens with two players */
    CHECK(orch_voices_of_part(0U) == 2U);

    orch_panic();
    render(1U);
    CHECK(orch_engine_set_player_cap(0U, 1U) == 0);
    midi(0x90U, 62U, 100U);
    render(2U);
    CHECK(orch_voices_of_part(0U) == 1U);
    orch_panic();
    render(1U);
    CHECK(orch_engine_set_player_cap(0U, 4U) == 0);
}

static void test_output_range_and_sound(void)
{
    orch_init();
    midi(0x90U, 57U, 110U);
    int64_t peak = 0;
    int nonzero = 0;
    for (unsigned b = 0; b < 60U; ++b) {
        memset(block, 0, sizeof block);
        orch_render_q23(block, 128U);
        for (unsigned i = 0; i < 256U; ++i) {
            const int64_t v = block[i];
            if (v > 8388607 || v < -8388608) { CHECK(0 && "sample out of Q23"); return; }
            if (v > peak) peak = v;
            if (v < -peak) peak = -v;
            if (v) ++nonzero;
        }
    }
    CHECK(nonzero > 1000);
    CHECK(peak > 0);
    CHECK(peak <= 8388607);
}

static void test_note_off_and_sustain(void)
{
    orch_init();
    midi(0x90U, 60U, 100U);
    render(4U);
    CHECK(orch_active_voices() > 0U);
    midi(0x80U, 60U, 0U);
    render(240U); /* longer than the release */
    CHECK(orch_active_voices() == 0U);
    CHECK(orch_note_count(0U) == 0U);

    /* sustain holds the note until CC64 = 0 */
    orch_init();
    midi(0xB0U, 64U, 127U);
    midi(0x90U, 60U, 100U);
    render(4U);
    midi(0x80U, 60U, 0U);
    render(20U);
    CHECK(orch_active_voices() > 0U);
    midi(0xB0U, 64U, 0U);
    render(240U);
    CHECK(orch_active_voices() == 0U);
}

static void test_sequencer(void)
{
    struct orch_step_note note;
    orch_init();
    orch_seq_clear();
    note.note = 60U; note.velocity = 100U; note.gate_percent = 50U;
    CHECK(orch_seq_set_step(0U, 0U, &note) == 0);
    CHECK(orch_seq_set_step(16U, 0U, &note) == -1);
    orch_seq_set_tempo(240U);
    orch_seq_set_enabled(1U);
    render(20U);
    CHECK(orch_active_voices() > 0U);
    orch_seq_set_enabled(0U);
    render(240U);
    CHECK(orch_active_voices() == 0U);
}

static void test_articulation_and_family(void)
{
    orch_init();
    CHECK(orch_engine_set_articulation(0U, ORCH_ART_PIZZICATO) == 0);
    midi(0x90U, 60U, 100U);
    render(3U);
    const struct orch_intent *in = orch_last_intent(0U);
    CHECK(in && in->articulation == ORCH_ART_PIZZICATO);
    CHECK(orch_active_voices() > 0U);
    /* a pluck rings on with the gate already closed, then decays */
    midi(0x80U, 60U, 0U);
    render(600U);
    CHECK(orch_active_voices() == 0U);

    CHECK(orch_engine_configure_part(2U, ORCH_FAM_BRASS, 0x0F00U) == 0);
    CHECK(orch_engine_configure_part(9U, ORCH_FAM_BRASS, 0U) == -1);
    orch_init();
}

int main(void)
{
    test_routing();
    test_divisi_voices();
    test_output_range_and_sound();
    test_note_off_and_sustain();
    test_sequencer();
    test_articulation_and_family();
    if (failures) { printf("%u engine test(s) failed\n", failures); return 1; }
    printf("orch_engine: all checks passed\n");
    return 0;
}
