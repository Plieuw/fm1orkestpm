/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "orch_divisi.h"
#include "orch_part.h"

static unsigned failures;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static void test_empty(void)
{
    struct orch_divisi_state d;
    struct orch_divisi_plan p;
    orch_divisi_init(&d);
    orch_divisi_plan(&d, 0, 0, 0U, 3U, 4U, &p);
    CHECK(p.players == 0U);
    CHECK(p.active_mask == 0U);
}

static void test_single_note_players(void)
{
    struct orch_divisi_state d;
    struct orch_divisi_plan p;
    const uint8_t notes[1] = { 60U };
    const uint32_t ids[1] = { 1U };
    orch_divisi_init(&d);

    orch_divisi_plan(&d, notes, ids, 1U, 1U, 4U, &p);
    CHECK(p.players == 1U);
    CHECK(p.active_mask == 0x01U);
    CHECK(p.assignment[0] == 0U);

    orch_divisi_plan(&d, notes, ids, 1U, 3U, 4U, &p);
    CHECK(p.players == 3U);
    CHECK(p.active_mask == 0x07U);
    /* more players than notes: everyone doubles the note */
    CHECK(p.assignment[0] == 0U && p.assignment[1] == 0U && p.assignment[2] == 0U);
    CHECK(p.detune_cents[0] != p.detune_cents[1]);
    CHECK(p.pan[0] != p.pan[1]);
}

static void test_cap(void)
{
    struct orch_divisi_state d;
    struct orch_divisi_plan p;
    const uint8_t notes[2] = { 60U, 64U };
    const uint32_t ids[2] = { 1U, 2U };
    orch_divisi_init(&d);
    orch_divisi_plan(&d, notes, ids, 2U, 6U, 2U, &p);
    CHECK(p.players == 2U);
    CHECK(p.active_mask == 0x03U);
}

static void test_chord_distribution(void)
{
    struct orch_divisi_state d;
    struct orch_divisi_plan p;
    const uint8_t notes[3] = { 55U, 60U, 64U };
    const uint32_t ids[3] = { 1U, 2U, 3U };
    orch_divisi_init(&d);
    orch_divisi_plan(&d, notes, ids, 3U, 3U, 4U, &p);
    CHECK(p.players == 3U);
    CHECK(p.assignment[0] != p.assignment[1]);
    CHECK(p.assignment[1] != p.assignment[2]);
    CHECK(p.assignment[0] != p.assignment[2]);
}

/* A held note keeps its player between plans (hysteresis). */
static void test_stability(void)
{
    struct orch_divisi_state d;
    struct orch_divisi_plan p1, p2;
    const uint8_t notes_a[2] = { 60U, 64U };
    const uint32_t ids_a[2] = { 11U, 12U };
    orch_divisi_init(&d);
    orch_divisi_plan(&d, notes_a, ids_a, 2U, 3U, 4U, &p1);

    /* a third note arrives: the two held notes must not move */
    const uint8_t notes_b[3] = { 60U, 64U, 67U };
    const uint32_t ids_b[3] = { 11U, 12U, 13U };
    orch_divisi_plan(&d, notes_b, ids_b, 3U, 3U, 4U, &p2);
    CHECK(p2.assignment[0] == p1.assignment[0]);
    CHECK(p2.assignment[1] == p1.assignment[1]);

    /* and the plan is deterministic */
    struct orch_divisi_plan p3;
    orch_divisi_plan(&d, notes_b, ids_b, 3U, 3U, 4U, &p3);
    CHECK(memcmp(&p2, &p3, sizeof p2) == 0);
}

/* Once a note is released its player is free for a new note. */
static void test_release_frees_player(void)
{
    struct orch_divisi_state d;
    struct orch_divisi_plan p;
    const uint8_t notes_a[1] = { 60U };
    const uint32_t ids_a[1] = { 1U };
    orch_divisi_init(&d);
    orch_divisi_plan(&d, notes_a, ids_a, 1U, 2U, 4U, &p);

    const uint8_t notes_b[1] = { 72U };
    const uint32_t ids_b[1] = { 2U };
    orch_divisi_plan(&d, notes_b, ids_b, 1U, 2U, 4U, &p);
    CHECK(p.players == 2U);
    CHECK(p.assignment[0] == 0U && p.assignment[1] == 0U);
}

/* The plan's pan is the player's place on the stage: one player stands on the
 * section's centre, more players fan out evenly around it. */
static void test_section_placement(void)
{
    struct orch_divisi_state d;
    struct orch_divisi_plan p;
    const uint8_t notes[4] = { 55U, 60U, 64U, 67U };
    const uint32_t ids[4] = { 1U, 2U, 3U, 4U };
    /* the choir sits in the middle of the stage: one player is the centre */
    orch_divisi_init(&d);
    orch_divisi_set_instrument(&d, ORCH_INST_CHOIR);

    orch_divisi_plan(&d, notes, ids, 1U, 1U, 4U, &p);
    CHECK(p.pan[0] == 128U);

    orch_divisi_init(&d);
    orch_divisi_set_instrument(&d, ORCH_INST_CHOIR);
    orch_divisi_plan(&d, notes, ids, 2U, 2U, 4U, &p);
    CHECK(p.pan[0] < 128U && p.pan[1] > 128U);
    CHECK((unsigned)(128U - p.pan[0]) == (unsigned)(p.pan[1] - 128U));

    orch_divisi_init(&d);
    orch_divisi_set_instrument(&d, ORCH_INST_CHOIR);
    orch_divisi_plan(&d, notes, ids, 4U, 4U, 4U, &p);
    CHECK(p.pan[0] < p.pan[1] && p.pan[1] < p.pan[2] && p.pan[2] < p.pan[3]);
    CHECK(p.pan[0] < 128U && p.pan[3] > 128U);
}

int main(void)
{
    test_empty();
    test_single_note_players();
    test_cap();
    test_chord_distribution();
    test_stability();
    test_release_frees_player();
    test_section_placement();
    if (failures) { printf("%u divisi test(s) failed\n", failures); return 1; }
    printf("orch_divisi: all checks passed\n");
    return 0;
}
