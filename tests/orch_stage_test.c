/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test for the seating and the listener: pans are numbers, so the order
 * of the sections and the direction of every move can be checked exactly. */
#include <stdio.h>

#include "orch_engine.h"
#include "orch_part.h"
#include "orch_stage.h"

static unsigned failures;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

/* The audience's left to right: strings, woodwinds, brass, percussion. */
static void test_seats(void)
{
    orch_stage_reset();
    CHECK(orch_stage_seat(ORCH_INST_VIOLIN) < orch_stage_seat(ORCH_INST_VIOLA));
    CHECK(orch_stage_seat(ORCH_INST_VIOLA) < orch_stage_seat(ORCH_INST_CELLO));
    CHECK(orch_stage_seat(ORCH_INST_CELLO) < orch_stage_seat(ORCH_INST_DOUBLE_BASS));
    CHECK(orch_stage_seat(ORCH_INST_HARP) < orch_stage_seat(ORCH_INST_VIOLA));
    CHECK(orch_stage_seat(ORCH_INST_VIOLIN) < orch_stage_seat(ORCH_INST_CLARINET));
    CHECK(orch_stage_seat(ORCH_INST_BASSOON) < orch_stage_seat(ORCH_INST_TRUMPET));
    CHECK(orch_stage_seat(ORCH_INST_TRUMPET) < orch_stage_seat(ORCH_INST_TIMPANI));
    CHECK(orch_stage_seat(ORCH_INST_CHOIR) == 128U);
    /* the middle of a section is the seat itself */
    CHECK(orch_stage_place(ORCH_INST_VIOLIN, 128U) == orch_stage_seat(ORCH_INST_VIOLIN));
    /* an unknown instrument still lands in the middle instead of off-stage */
    CHECK(orch_stage_seat(ORCH_INST_COUNT + 5U) == 128U);

    /* four parts seated as an orchestra: violins, violas, cellos, basses */
    orch_init();
    orch_engine_set_instrument(0U, ORCH_INST_VIOLIN);
    orch_engine_set_instrument(1U, ORCH_INST_VIOLA);
    orch_engine_set_instrument(2U, ORCH_INST_CELLO);
    orch_engine_set_instrument(3U, ORCH_INST_DOUBLE_BASS);
    CHECK(orch_stage_place(orch_part_get(0)->instrument, 128U) <
          orch_stage_place(orch_part_get(1)->instrument, 128U));
    CHECK(orch_stage_place(orch_part_get(1)->instrument, 128U) <
          orch_stage_place(orch_part_get(2)->instrument, 128U));
    CHECK(orch_stage_place(orch_part_get(2)->instrument, 128U) <
          orch_stage_place(orch_part_get(3)->instrument, 128U));
}

/* Players of one part stand side by side, evenly around the section middle. */
static void test_section_spread(void)
{
    orch_stage_reset();
    const unsigned l = orch_stage_place(ORCH_INST_CELLO, 80U);
    const unsigned c = orch_stage_place(ORCH_INST_CELLO, 128U);
    const unsigned r = orch_stage_place(ORCH_INST_CELLO, 176U);
    CHECK(l < c && c < r);
    CHECK(c - l == r - c);
}

static void test_listener(void)
{
    orch_stage_reset();
    const unsigned base = orch_stage_place(ORCH_INST_VIOLA, 128U);

    orch_stage_set_listener(50, 0);
    CHECK(orch_stage_listener_x() == 50);
    CHECK(orch_stage_place(ORCH_INST_VIOLA, 128U) == base + 32U);

    orch_stage_set_listener(-100, 0);
    CHECK(orch_stage_place(ORCH_INST_VIOLA, 128U) == base - 64U);

    /* depth narrows the section but keeps it on its axis */
    orch_stage_set_listener(0, 0);
    const unsigned wide = orch_stage_place(ORCH_INST_VIOLA, 176U) - base;
    orch_stage_set_listener(0, 100);
    const unsigned back = orch_stage_place(ORCH_INST_VIOLA, 176U) - base;
    CHECK(back < wide && back > 0U);
    CHECK(orch_stage_place(ORCH_INST_VIOLA, 128U) == base);

    /* both ends stay inside the field */
    orch_stage_set_listener(100, 0);
    CHECK(orch_stage_place(ORCH_INST_TIMPANI, 255U) == 255U);
    orch_stage_set_listener(-100, 0);
    CHECK(orch_stage_place(ORCH_INST_VIOLIN, 0U) == 0U);

    /* out of range input is clamped, not wrapped around */
    orch_stage_set_listener(-300, 500);
    CHECK(orch_stage_listener_x() == -100);
    CHECK(orch_stage_listener_depth() == 100U);
    orch_stage_reset();
    CHECK(orch_stage_listener_x() == 0);
    CHECK(orch_stage_listener_depth() == 0U);
}

int main(void)
{
    test_seats();
    test_section_spread();
    test_listener();
    if (failures) { printf("%u stage test(s) failed\n", failures); return 1; }
    printf("orch_stage: all checks passed\n");
    return 0;
}
