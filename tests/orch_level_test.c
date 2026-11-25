/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test for the panel's level meter (app/orch_level.inc, compiled into the
 * panel): the light has to follow the sound, not the gate.
 *
 * Two properties, both at the board's block size:
 *
 * 1. the scale is the orchestra's — eight voices, the four parts at two each,
 *    reach ORCH_LEVEL_MAX, and one held string is six;
 * 2. the level falls while the keys are still down — a timpani note is struck
 *    and never released, its sound dies on its own, and the level goes with it
 *    to nought. The part's note count is read before and after, so the test
 *    cannot pass by a note-off: the gate never moves.
 */
#include <stdio.h>
#include <string.h>

#include "fm1.h"
#include "orch_engine.h"
#include "orch_fx.h"
#include "orch_level.h"
#include "orch_scenes.h"
#include "orch_resonance.h"

static uint32_t clock_ms;
static int32_t block[256];
static unsigned failures;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

/* The board's services, as the audio callback and the panel see them. */
void fm1_controls_init(void) {}
void fm1_controls_read(struct fm1_controls *state) { memset(state, 0, sizeof *state); }
void fm1_screen_init(void) {}
void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
}
int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h,
                    const uint16_t *pixels, size_t count)
{
    (void)x; (void)y; (void)w; (void)h; (void)pixels; (void)count;
    return 0;
}
void fm1_led_set(unsigned key, int on) { (void)key; (void)on; }
uint32_t fm1_millis(void) { return clock_ms; }
void fm1_engine_pause(void) {}
void fm1_engine_resume(void) {}
uint32_t fm1_flash_size(void) { return 8192U; }
int fm1_flash_erase(uint32_t offset) { (void)offset; return 0; }
int fm1_flash_write(uint32_t offset, const void *data, size_t bytes)
{
    (void)offset; (void)data; (void)bytes;
    return 0;
}
int fm1_flash_read(uint32_t offset, void *data, size_t bytes)
{
    (void)offset; (void)data; (void)bytes;
    return -1;
}

/* One block as the audio callback makes it — renderer, effects, then the meter —
 * for as many blocks as the test needs. */
static void render(unsigned blocks)
{
    for (unsigned i = 0; i < blocks; ++i) {
        memset(block, 0, sizeof block);
        orch_render_q23(block, 128U);
        orch_fx_process(block, 128U);
        orch_level_take(block, 2U * 128U);
        clock_ms += 3U;
    }
}

/* The highest level of the next n blocks: a strike lights the cone at once and
 * it falls again within a few blocks, so the peak has to be taken as it comes. */
static unsigned render_peak(unsigned blocks)
{
    unsigned top = 0U;
    for (unsigned i = 0; i < blocks; ++i) {
        render(1U);
        if (orch_output_level() > top) top = orch_output_level();
    }
    return top;
}

/* Eight voices are the top of the scale; one held string is two players, six. */
static void test_scale_is_the_orchestra(void)
{
    orch_init();
    orch_fx_init();
    orch_engine_set_instrument(0U, ORCH_INST_VIOLIN);
    orch_note_event(0U, 60U, 100U);
    render(700);                               /* the bow has swelled by now */
    CHECK(orch_active_voices() == 2U);         /* one note is two players */
    /* the 0.5.0 engine breathes (adaptive sustain, the bow's expression), so
     * the light is read at the top of the next bar, not at one block */
    CHECK(render_peak(100) == 6U);
    orch_note_event(0U, 60U, 0U);
    render(600);

    orch_init();
    orch_fx_init();
    /* eight parts, one note each and one player each: the pool of eight */
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        orch_engine_set_instrument(p, ORCH_INST_VIOLIN + p);
        orch_engine_set_player_cap(p, 1U);
        orch_note_event(p, 60U + p, 100U);
    }
    render(400);                               /* the bows swell over a second */
    CHECK(orch_active_voices() == 8U);
    CHECK(render_peak(300) == ORCH_LEVEL_MAX);
}

/* The gate is not what it follows: the key stays down, the sound dies, and the
 * level goes with the sound. */
static void test_level_falls_while_the_keys_are_down(void)
{
    orch_init();
    orch_fx_init();
    orch_engine_set_instrument(0U, ORCH_INST_TIMPANI);
    orch_note_event(0U, 60U, 127U);

    const unsigned loud = render_peak(12);
    const unsigned held = orch_note_count(0U);
    CHECK(loud > 0U);                          /* the strike lights the cone */
    CHECK(held == 1U);                         /* one key, still down */

    render(700);                               /* the drum rings out on its own */
    const unsigned quiet = orch_output_level();

    CHECK(orch_note_count(0U) == held);        /* no note-off was ever sent */
    CHECK(quiet < loud);
    CHECK(quiet == 0U);

    /* and it comes back when the stick falls again */
    orch_note_event(0U, 60U, 0U);
    render(60);
    orch_note_event(0U, 60U, 127U);
    CHECK(render_peak(12) > 0U);
}

/* Every factory scene, the whole orchestra at velocity 127 with CC7 fully
 * open, through the scene's own effects: under full scale, no limiter. A
 * scene leaves none of the last scene's effect settings behind. */
static void test_scenes_stay_in_range(void)
{
    for (unsigned sc = 0; sc < ORCH_SCENE_COUNT; ++sc) {
        orch_init();
        orch_fx_init();
        CHECK(orch_scene_apply(sc) == 0);
        for (unsigned p = 0; p < ORCH_PARTS; ++p) {
            const uint8_t vol[3] = { (uint8_t)(0xB0U | p), 7U, 127U };
            orch_midi_short_message(vol, 3U);
            orch_note_event(p, 48U + 5U * p, 127U);
        }
        int32_t top = 0;
        for (unsigned b = 0; b < 600U; ++b) {
            memset(block, 0, sizeof block);
            orch_render_q23(block, 128U);
            orch_fx_process(block, 128U);
            for (unsigned i = 0; i < 256U; ++i) {
                const int32_t a = block[i] < 0 ? -block[i] : block[i];
                if (a > top) top = a;
            }
        }
        CHECK(top > 0 && top < 8388607);
    }
    orch_scene_apply(5U);                      /* AMBIENT: shimmer open */
    orch_scene_apply(1U);                      /* BAROQUE: none, a drier room */
    CHECK(orch_fx_param(ORCH_FX_REVERB, 1U) == 0U);
    CHECK(orch_fx_enabled(ORCH_FX_CHORUS) == 0U);
    CHECK(orch_resonance_amount() == 20U);
    orch_resonance_set_amount(25U);
    orch_init();
    orch_fx_init();
}

int main(void)
{
    test_scenes_stay_in_range();
    test_scale_is_the_orchestra();
    test_level_falls_while_the_keys_are_down();
    if (failures) { printf("%u level test(s) failed\n", failures); return 1; }
    printf("orch_level: all checks passed\n");
    return 0;
}
