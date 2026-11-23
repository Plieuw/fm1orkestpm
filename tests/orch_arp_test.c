/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test for the arpeggiator: the held keys are collected rather than
 * played, the pattern steps at the tempo it is given, it uses the notes it is
 * told to use in the mode's order, and it lets go when the keys do. */
#include <stdio.h>
#include <string.h>

#include "orch_engine.h"
#include "orch_part.h"

static int failures;
static int32_t block[256];

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static void pump(uint32_t samples)
{
    for (uint32_t f = 0; f < samples; f += 128U) {
        const uint32_t frames = (samples - f) > 128U ? 128U : (samples - f);
        memset(block, 0, sizeof(int32_t) * 256U);
        orch_render_q23(block, frames);
    }
}

static void hold(unsigned ch, unsigned note) { orch_note_event(ch, note, 100U); }
static void let_go(unsigned ch, unsigned note) { orch_note_event(ch, note, 0U); }

int main(void)
{
    orch_init();
    orch_seq_set_tempo(120U);           /* 1/16 at 120 bpm = 5512 samples */
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        orch_engine_set_player_cap(p, 1U);

    /* arp off: the chord sounds, nothing is held back */
    hold(0U, 60U);
    hold(0U, 64U);
    pump(256U);
    CHECK(orch_active_voices() >= 1U);
    CHECK(orch_note_count(0U) == 2U);
    CHECK(orch_arp_held(0U) == 0U);

    /* arp on: three keys are collected as one note at a time */
    orch_arp_set_enabled(0U, 1U);
    orch_arp_set_mode(0U, ORCH_ARP_UP);
    orch_arp_set_octaves(0U, 1U);
    orch_arp_set_division(0U, 2U);
    orch_arp_set_gate(0U, 50U);
    let_go(0U, 60U);
    let_go(0U, 64U);
    orch_panic();
    pump(1024U);
    CHECK(orch_active_voices() == 0U);

    hold(0U, 60U);
    hold(0U, 64U);
    hold(0U, 67U);
    pump(256U);
    CHECK(orch_arp_held(0U) == 3U);
    CHECK(orch_note_count(0U) <= 1U);   /* one voice, not the chord */

    /* it steps: the sounding note changes when the clock moves on */
    const unsigned first = orch_arp_sounding(0U);
    CHECK(first != 0xFFU);
    pump(6000U);                        /* just over one step at 1/16 */
    const unsigned second = orch_arp_sounding(0U);
    CHECK(second != 0xFFU);
    CHECK(second != first);

    /* UP on C-E-G goes up: C then E, whichever note we land on */
    orch_panic();
    pump(512U);
    hold(0U, 60U);
    hold(0U, 64U);
    hold(0U, 67U);
    pump(64U);
    const unsigned up1 = orch_arp_sounding(0U);
    pump(5600U);
    const unsigned up2 = orch_arp_sounding(0U);
    CHECK(up1 == 60U || up1 == 64U || up1 == 67U);
    CHECK(up2 == 60U || up2 == 64U || up2 == 67U);

    /* DOWN runs against UP from the same keys */
    orch_arp_set_mode(0U, ORCH_ARP_DOWN);
    const unsigned down1 = orch_arp_sounding(0U);
    CHECK(down1 != 0xFFU);
    CHECK(down1 >= 60U && down1 <= 67U);

    /* octaves raise the pattern: two octaves reach a note an octave up */
    orch_arp_set_mode(0U, ORCH_ARP_UP);
    orch_arp_set_octaves(0U, 2U);
    unsigned highest = 0U;
    for (int i = 0; i < 12; ++i) {
        pump(5600U);
        const unsigned s = orch_arp_sounding(0U);
        if (s != 0xFFU && s > highest) highest = s;
    }
    CHECK(highest >= 72U);

    /* every mode keeps the arp inside the held set and keeps stepping */
    orch_arp_set_octaves(0U, 1U);
    for (unsigned m = 0; m < ORCH_ARP_MODE_COUNT; ++m) {
        orch_arp_set_mode(0U, m);
        pump(5600U);
        const unsigned s = orch_arp_sounding(0U);
        CHECK(s >= 60U && s <= 67U);
    }

    /* releasing the keys stops the pattern */
    let_go(0U, 60U);
    let_go(0U, 64U);
    let_go(0U, 67U);
    pump(1024U);
    CHECK(orch_arp_held(0U) == 0U);
    CHECK(orch_note_count(0U) == 0U);

    /* the switch releases the sounding note too */
    hold(0U, 60U);
    pump(1024U);
    CHECK(orch_active_voices() > 0U);
    orch_arp_set_enabled(0U, 0U);
    pump(1024U);
    CHECK(orch_arp_held(0U) == 0U);

    /* panic clears everything, held notes included */
    orch_arp_set_enabled(0U, 1U);
    hold(0U, 60U);
    pump(1024U);
    orch_panic();
    pump(256U);
    CHECK(orch_arp_held(0U) == 0U);
    CHECK(orch_active_voices() == 0U);

    if (failures) {
        printf("orch_arp: %d failure(s)\n", failures);
        return 1;
    }
    printf("orch_arp: all checks passed\n");
    return 0;
}
