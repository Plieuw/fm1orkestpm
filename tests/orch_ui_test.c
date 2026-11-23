/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test for the panel logic. The FM-1 board services are stubbed, so this
 * checks what a knob or button does to the engine, not what the display shows.
 */
#include <stdio.h>
#include <string.h>

#include "fm1.h"
#include "orch_engine.h"
#include "orch_part.h"
#include "orch_ui.h"

static int failures;
static unsigned fills, blits, leds;
static uint32_t clock_ms;
static struct fm1_controls feed;
static int32_t block[256];

/* The engine allocates voices while rendering, so an event needs one block
 * before orch_active_voices() means anything. */
static void render_block(void) { orch_render_q23(block, 128U); }

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

/* --- board stubs ---------------------------------------------------------- */

void fm1_controls_init(void) {}
void fm1_controls_read(struct fm1_controls *state) { *state = feed; }
void fm1_screen_init(void) {}
void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t c)
{
    (void)x; (void)y; (void)c;
    if (w > 240U || h > 240U) ++failures;
    ++fills;
}
int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h,
                    const uint16_t *pixels, size_t count)
{
    (void)x; (void)y; (void)pixels;
    if (w > 240U || h > 240U || count < (size_t)w * h) ++failures;
    ++blits;
    return 0;
}
void fm1_led_set(unsigned key, int on) { (void)key; (void)on; ++leds; }
uint32_t fm1_millis(void) { return clock_ms; }

/* --- helpers -------------------------------------------------------------- */

static void press(unsigned button)
{
    feed.buttons |= 1UL << button;
    orch_ui_handle(&feed);
    feed.buttons &= ~(1UL << button);
    orch_ui_handle(&feed);
}

static void turn(unsigned encoder, int detents)
{
    feed.encoders[encoder] = (int16_t)detents;
    orch_ui_handle(&feed);
    feed.encoders[encoder] = 0;
}

int main(void)
{
    orch_init();
    memset(&feed, 0, sizeof feed);
    orch_ui_init();

    CHECK(fills > 0U);
    CHECK(blits > 0U);
    CHECK(orch_ui_master_q23 == 0U);   /* silent until the knob is read */

    /* a button selects a part */
    press(2);
    CHECK(orch_ui_part() == 2U);
    CHECK(orch_part_get(2)->family == ORCH_FAM_BRASS);

    /* the family buttons replace family and pick that family's default */
    press(4U + ORCH_FAM_WOODWINDS);
    CHECK(orch_part_get(2)->family == ORCH_FAM_WOODWINDS);
    CHECK(orch_part_get(2)->instrument == ORCH_INST_FLUTE);

    /* encoder 1 walks the instrument list and stays inside the family */
    turn(0, 2);
    CHECK(orch_part_get(2)->instrument == ORCH_INST_CLARINET);
    turn(0, 3);   /* past the end of the family: wraps to the first */
    CHECK(orch_part_get(2)->instrument == ORCH_INST_FLUTE);
    turn(0, 1);
    CHECK(orch_part_get(2)->instrument == ORCH_INST_PICCOLO);

    /* players, voices and tempo are clamped, not wrapped */
    turn(1, 10);
    CHECK(orch_part_get(2)->player_cap == 4U);
    turn(1, -20);
    CHECK(orch_part_get(2)->player_cap == 1U);
    turn(2, 0);
    turn(4, 3);
    CHECK(orch_ui_tempo() == 132U);
    turn(4, 200);
    CHECK(orch_ui_tempo() == 300U);

    /* articulation steps forwards and wraps back to AUTO */
    CHECK(orch_part_get(2)->articulation == ORCH_ART_AUTO);
    press(10);
    CHECK(orch_part_get(2)->articulation == ORCH_ART_PORTAMENTO);
    press(9);
    CHECK(orch_part_get(2)->articulation == ORCH_ART_AUTO);
    press(9);
    CHECK(orch_part_get(2)->articulation == ORCH_ART_COL_LEGNO);

    /* the part encoder wraps, the family encoder changes the family */
    turn(5, 3);
    CHECK(orch_ui_part() == 1U);
    turn(6, 1);
    CHECK(orch_part_get(1)->family == ORCH_FAM_BRASS);

    /* the knob is the master gain, and it is not unity at zero */
    feed.knob = 1023U;
    orch_ui_handle(&feed);
    CHECK(orch_ui_master_q23 == (1UL << 23));
    feed.knob = 512U;
    orch_ui_handle(&feed);
    CHECK(orch_ui_master_q23 > 0U);
    CHECK(orch_ui_master_q23 < (1UL << 23));
    feed.knob = 0U;
    orch_ui_handle(&feed);
    CHECK(orch_ui_master_q23 == 0U);

    /* panic silences a sounding part */
    orch_note_event(8U, 60U, 100U);
    render_block();
    CHECK(orch_active_voices() > 0U);
    press(12);
    render_block();
    CHECK(orch_active_voices() == 0U);

    /* the chord button starts the parts and stops them again */
    press(13);
    render_block();
    CHECK(orch_active_voices() > 0U);
    CHECK(orch_note_count(0U) > 0U);
    clock_ms += 1000U;
    orch_ui_control();
    render_block();
    /* the release is sent; the voices keep decaying, so the held-note count is
     * what proves the button did its job */
    CHECK(orch_note_count(0U) == 0U);
    CHECK(orch_note_count(1U) == 0U);

    /* the scan is throttled: a second call inside 25 ms reads nothing */
    const unsigned before = leds;
    clock_ms += 5U;
    orch_ui_control();
    CHECK(leds == before);

    if (failures) {
        printf("orch_ui: %d failure(s)\n", failures);
        return 1;
    }
    printf("orch_ui: all checks passed (%u fills, %u blits, %u leds)\n",
           fills, blits, leds);
    return 0;
}
