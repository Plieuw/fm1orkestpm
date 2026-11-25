/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test for the panel: the board services are stubbed, so this checks what
 * a printed button or an encoder role does to the engine, not what the display
 * looks like. */
#include <stdio.h>
#include <string.h>

#include "fm1.h"
#include "orch_engine.h"
#include "orch_fx.h"
#include "orch_part.h"
#include "orch_performance.h"
#include "orch_scenes.h"
#include "orch_stage.h"
#include "orch_ui.h"

static int failures;
static unsigned fills, blits, leds, flash_writes, flash_erases;
static uint32_t clock_ms;
static struct fm1_controls feed;
static int32_t block[256];
static int saved_record_ok;
static uint8_t flash_mem[4096];

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

/* the printed panel labels, as the board numbers the matrix */
enum { B_OCT_DOWN = 0, B_OCT_UP, B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO,
       B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC };
enum { E_SELECT = 0, E_ALGO, E_KNOB1, E_KNOB2, E_KNOB3, E_KNOB4, E_PRESET };

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
static int paused;
void fm1_engine_pause(void) { ++paused; }
void fm1_engine_resume(void) { --paused; }

uint32_t fm1_flash_size(void) { return sizeof flash_mem; }
int fm1_flash_erase(uint32_t offset)
{
    if (offset + 4096U > sizeof flash_mem) return -1;
    memset(flash_mem + offset, 0xFF, 4096U);
    ++flash_erases;
    return 0;
}
int fm1_flash_write(uint32_t offset, const void *data, size_t bytes)
{
    if (offset + bytes > sizeof flash_mem) return -1;
    memcpy(flash_mem + offset, data, bytes);
    ++flash_writes;
    saved_record_ok = 1;
    return 0;
}
int fm1_flash_read(uint32_t offset, void *data, size_t bytes)
{
    if (offset + bytes > sizeof flash_mem) return -1;
    memcpy(data, flash_mem + offset, bytes);
    return 0;
}

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

static void key(unsigned note, int down)
{
    const unsigned bit = note - 53U;
    if (down) feed.keys |= 1UL << bit;
    else feed.keys &= ~(1UL << bit);
    orch_ui_handle(&feed);
}

int main(void)
{
    orch_init();
    orch_fx_init();
    memset(&feed, 0, sizeof feed);
    orch_ui_init();

    CHECK(fills > 0U);
    CHECK(blits > 0U);
    CHECK(orch_ui_master_q23 == 0U);
    CHECK(orch_ui_page() == 0U);          /* PARTS */

    /* SELECT picks a part, ALGORITHM the family on the parts page. A choice
     * moves one place a detent however many detents arrive at once, so a quick
     * turn cannot skip a part. */
    turn(E_SELECT, 2);
    CHECK(orch_ui_part() == 1U);
    turn(E_SELECT, 1);
    CHECK(orch_ui_part() == 2U);
    CHECK(orch_part_get(2)->family == ORCH_FAM_WOODWINDS);
    turn(E_ALGO, 1);                      /* WOODWINDS -> BRASS */
    CHECK(orch_part_get(2)->family == ORCH_FAM_BRASS);
    turn(E_ALGO, -1);                     /* and back */
    CHECK(orch_part_get(2)->family == ORCH_FAM_WOODWINDS);

    /* PRESETS walks the instruments inside the family and wraps */
    turn(E_PRESET, 1);
    CHECK(orch_part_get(2)->instrument == ORCH_INST_PICCOLO);
    turn(E_PRESET, 4);                    /* four detents: still one entry */
    CHECK(orch_part_get(2)->instrument == ORCH_INST_CLARINET);
    turn(E_PRESET, -9);
    CHECK(orch_part_get(2)->instrument == ORCH_INST_PICCOLO);

    /* eight parts: SELECT wraps after the eighth */
    turn(E_SELECT, 1); turn(E_SELECT, 1); turn(E_SELECT, 1);
    turn(E_SELECT, 1); turn(E_SELECT, 1);
    CHECK(orch_ui_part() == 7U);
    turn(E_SELECT, 1);
    CHECK(orch_ui_part() == 0U);
    turn(E_SELECT, -1);
    CHECK(orch_ui_part() == 7U);
    for (int i = 0; i < 5; ++i) turn(E_SELECT, -1);
    CHECK(orch_ui_part() == 2U);
    CHECK(paused == 0);                   /* every edit pause is resumed */

    /* OCT+ / OCT- transpose in octaves, and the note map follows */
    press(B_OCT_UP);
    CHECK(orch_ui_transpose() == 12U);
    CHECK(orch_ui_map_note(60) == 72);
    press(B_OCT_DOWN);
    CHECK(orch_ui_map_note(60) == 60);
    press(B_OCT_DOWN);
    CHECK(orch_ui_map_note(60) == 48);
    press(B_OCT_DOWN);
    press(B_OCT_DOWN);                    /* clamps at two octaves down */
    CHECK(orch_ui_transpose() == (unsigned)-24);
    press(B_OCT_UP);
    press(B_OCT_UP);                      /* and back to concert pitch */
    CHECK(orch_ui_transpose() == 0U);

    /* SCL quantises down to the next scale note: C# and D# become C and D in
     * major, and D# survives in minor because it is the minor third */
    press(B_SCL);
    CHECK(orch_ui_scale() == 1U);         /* MAJOR */
    CHECK(orch_ui_map_note(61) == 60);
    CHECK(orch_ui_map_note(63) == 62);
    press(B_SCL);
    CHECK(orch_ui_scale() == 2U);         /* MINOR */
    CHECK(orch_ui_map_note(63) == 63);
    press(B_SCL); press(B_SCL);
    CHECK(orch_ui_scale() == 0U);         /* back to OFF */

    /* the stage page's KNOB2 is the articulation, one place a detent */
    CHECK(orch_part_get(2)->articulation == ORCH_ART_AUTO);
    turn(E_KNOB2, 1);
    CHECK(orch_part_get(2)->articulation == ORCH_ART_PORTAMENTO);
    turn(E_KNOB2, -1);
    CHECK(orch_part_get(2)->articulation == ORCH_ART_AUTO);
    turn(E_KNOB2, -1);
    CHECK(orch_part_get(2)->articulation == ORCH_ART_COL_LEGNO);
    turn(E_KNOB2, 1);

    /* ENV opens the part's envelope: four knobs, A D S R */
    press(B_ENV);
    CHECK(orch_ui_page() == 5U);
    {
        const unsigned a0 = orch_part_get(2)->adsr.attack_ms;
        const unsigned s0 = orch_part_get(2)->adsr.sustain_percent;
        turn(E_KNOB1, 1);
        CHECK(orch_part_get(2)->adsr.attack_ms > a0);
        turn(E_KNOB3, -2);                /* sustain, 5 % a detent */
        CHECK(orch_part_get(2)->adsr.sustain_percent == s0 - 10U);
        turn(E_KNOB4, 1);
        turn(E_KNOB2, 1);
        CHECK(orch_part_get(2)->adsr.release_ms > 1U);
    }

    /* LFO: depth, rate, wave and where it goes */
    press(B_LFO);
    CHECK(orch_ui_page() == 6U);
    CHECK(orch_part_get(2)->lfo.destinations == 0U);
    turn(E_KNOB1, 2);
    CHECK(orch_part_get(2)->lfo.depth == 10U);
    turn(E_KNOB4, 1);
    CHECK(orch_part_get(2)->lfo.destinations == ORCH_LFO_PITCH);
    turn(E_KNOB3, 1);
    CHECK(orch_part_get(2)->lfo.wave == ORCH_LFO_TRIANGLE);
    turn(E_KNOB2, 3);
    CHECK(orch_part_get(2)->lfo.rate_millihz == 5300U);

    /* ARP opens its page; a press on the page switches the part's arp on */
    press(B_ARP);
    CHECK(orch_ui_page() == 1U);
    {
        struct orch_perf_config pc;
        orch_perf_get_config(2U, &pc);
        CHECK(pc.arp_enabled == 0U);
        press(B_ARP);
        orch_perf_get_config(2U, &pc);
        CHECK(pc.arp_enabled == 1U);
        turn(E_KNOB1, 1);                 /* type */
        turn(E_KNOB2, 1);                 /* rate: 1/8 -> 1/16. */
        turn(E_KNOB3, 2);                 /* gate, 5 % a detent: 80 -> 90 */
        turn(E_KNOB4, 1);                 /* the phrase comes on, GE 1 */
        turn(E_ALGO, 1);                  /* OR -> AND */
        orch_perf_get_config(2U, &pc);
        CHECK(pc.arp_type == 1U);
        CHECK(pc.division == 5U);
        CHECK(pc.gate == 90U);
        CHECK(pc.phrase_enabled == 1U && pc.phrase_ge == 0U);
        CHECK(pc.logic == ORCH_PERF_AND);
        /* PRESETS turns the knobs over to the other banks: FEEL, PHRASE,
         * ORDER, GROOVE, and back to PLAY */
        turn(E_PRESET, 1);                /* FEEL */
        turn(E_KNOB2, 1);                 /* ratchet 1 -> 2 */
        turn(E_KNOB4, 1);                 /* latch on */
        turn(E_PRESET, 1); turn(E_PRESET, 1);   /* ORDER */
        turn(E_KNOB1, 1);                 /* as played */
        turn(E_KNOB3, 1);                 /* MIDI clock */
        turn(E_KNOB3, -1);                /* and back to the internal one */
        orch_perf_get_config(2U, &pc);
        CHECK(pc.ratchet == 2U);
        CHECK(pc.latch == 1U);
        CHECK(pc.arp_order == 1U);
        CHECK(pc.external_clock == 0U);
        CHECK(orch_part_get(2)->instrument == ORCH_INST_PICCOLO);  /* not stepped */
        turn(E_PRESET, 1); turn(E_PRESET, 1);   /* GROOVE, then PLAY */
        turn(E_KNOB4, -1);                /* and goes off again */
        press(B_ARP);
        orch_perf_get_config(2U, &pc);
        CHECK(pc.arp_enabled == 0U);
        CHECK(pc.phrase_enabled == 0U);
    }

    /* SEQ: encode a step with the keys while REC is armed */
    press(B_SEQ);
    CHECK(orch_ui_page() == 2U);
    press(B_REC);
    CHECK(orch_ui_recording() == 1U);
    key(60, 1);
    key(60, 0);
    CHECK(orch_ui_seq_step() == 1U);      /* the recorder moves on */
    CHECK(orch_part_get(2)->family == ORCH_FAM_WOODWINDS);

    /* PLAY runs the sequencer: the step recorded above must sound */
    press(B_PLAY);
    unsigned peak = 0U;
    for (int i = 0; i < 200; ++i) {
        memset(block, 0, sizeof block);
        orch_render_q23(block, 128U);
        if (orch_active_voices() > peak) peak = orch_active_voices();
    }
    CHECK(peak > 0U);
    press(B_PLAY);                        /* and stop it again */

    /* GLOBAL: tempo, transpose and volume; K4 is the scene (at the end) */
    press(B_GLO);
    CHECK(orch_ui_page() == 3U);
    turn(E_KNOB3, 3);                     /* 2 BPM a detent, and the accel adds */
    CHECK(orch_ui_tempo() == 126U);
    turn(E_KNOB2, -5);                    /* semitone transpose from the global page */
    CHECK(orch_ui_transpose() == (unsigned)-5);
    CHECK(orch_ui_map_note(60) == 55);
    for (int i = 0; i < 5; ++i) turn(E_KNOB2, 1);
    CHECK(orch_ui_transpose() == 0U);
    feed.knob = 1023U;
    orch_ui_handle(&feed);
    CHECK(orch_ui_master_q23 == (1UL << 23));
    feed.knob = 0U;
    orch_ui_handle(&feed);
    CHECK(orch_ui_master_q23 == 0U);

    /* FX: the button opens the FX page; the encoders pick an effect and edit
     * its mix and its four parameters two at a time, K4 switches it on and off */
    press(B_FX);
    CHECK(orch_ui_page() == 4U);
    CHECK(orch_ui_fx_selection() == 0U);
    turn(E_ALGO, 2);
    CHECK(orch_ui_fx_selection() == 1U);      /* one detent, one effect */
    turn(E_ALGO, 1);
    CHECK(orch_ui_fx_selection() == 2U);      /* DELAY */
    turn(E_KNOB1, 2);                         /* mix 25 -> 35, 5 % a detent */
    CHECK(orch_fx_mix(2U) == 35U);
    turn(E_KNOB2, -1);                        /* time 50 -> 45 */
    CHECK(orch_fx_param(2U, 0U) == 45U);
    turn(E_KNOB3, 2);                         /* feedback 35 -> 45 */
    CHECK(orch_fx_param(2U, 1U) == 45U);
    turn(E_PRESET, 1);                        /* the second pair of parameters */
    turn(E_KNOB2, 2);                         /* sync: free -> a sixteenth */
    CHECK(orch_fx_param(2U, 2U) == 10U);
    CHECK(orch_fx_param(2U, 0U) == 45U);
    turn(E_KNOB3, 1);                         /* damp stays at the top */
    CHECK(orch_fx_param(2U, 3U) == 100U);
    turn(E_PRESET, 1);                        /* and back to the first pair */
    turn(E_KNOB4, 1);
    CHECK(orch_fx_enabled(2U) == 1U);
    turn(E_KNOB4, -1);
    CHECK(orch_fx_enabled(2U) == 0U);
    turn(E_KNOB4, 1);                         /* leave it on, SAVE keeps it */
    CHECK(orch_fx_any_enabled() == 1U);

    /* on the FX page the same button is the eight-part sound check */
    press(B_FX);
    CHECK(orch_ui_page() == 4U);
    memset(block, 0, sizeof block);
    orch_render_q23(block, 128U);
    CHECK(orch_active_voices() > 0U);
    clock_ms += 1000U;
    orch_ui_control();
    memset(block, 0, sizeof block);
    orch_render_q23(block, 128U);
    CHECK(orch_note_count(0U) == 0U);

    /* PARTS is the stage: K3 moves the listener across, K4 moves it back, and
     * neither of them touches the tempo the global page set */
    press(B_HOME);
    CHECK(orch_ui_page() == 0U);
    clock_ms += 2000U;
    turn(E_KNOB3, 1);
    CHECK(orch_stage_listener_x() == 5);
    turn(E_KNOB4, 1);
    CHECK(orch_stage_listener_depth() == 5U);
    CHECK(orch_ui_tempo() == 126U);
    for (int i = 0; i < 25; ++i) {          /* clamps at the left edge */
        clock_ms += 2000U;
        turn(E_KNOB3, -1);
    }
    CHECK(orch_stage_listener_x() == -100);

    /* SAVE erases a sector, writes the record and reports it */
    press(B_SAVE);
    CHECK(flash_erases == 1U);
    CHECK(flash_writes == 1U);
    CHECK(saved_record_ok == 1);
    CHECK(orch_ui_saved() == 1U);

    /* the FX block travels in the record: a fresh panel load brings it back */
    orch_fx_set_enabled(ORCH_FX_DELAY, 0U);
    orch_fx_set_mix(ORCH_FX_DELAY, 10U);
    orch_ui_init();
    orch_ui_control();       /* prime the scan timer after the re-init */
    CHECK(orch_fx_enabled(ORCH_FX_DELAY) == 1U);
    CHECK(orch_fx_mix(ORCH_FX_DELAY) == 35U);
    CHECK(orch_fx_param(ORCH_FX_DELAY, 0U) == 45U);
    CHECK(orch_fx_param(ORCH_FX_DELAY, 1U) == 45U);
    CHECK(orch_fx_param(ORCH_FX_DELAY, 2U) == 10U);
    /* and the listener's seat travels in its own block of the same record */
    CHECK(orch_stage_listener_x() == -100);
    CHECK(orch_stage_listener_depth() == 5U);

    /* HOME returns to the parts page, and the scan is throttled */
    press(B_HOME);
    CHECK(orch_ui_page() == 0U);
    const unsigned before = leds;
    clock_ms += 5U;
    orch_ui_control();
    CHECK(leds == before);

    press(B_GLO);
    /* HAWK CHUA's knob speed: alone it is one step; the same way at 20 a
     * second x2, at 40 x8; slowing comes back by halves, and turning back is
     * one step again. The tempo is 2 BPM a detent. */
    clock_ms += 2000U;
    turn(E_KNOB3, 1);
    CHECK(orch_ui_tempo() == 128U);       /* 126 + 2, alone */
    clock_ms += 50U;
    turn(E_KNOB3, 1);
    CHECK(orch_ui_tempo() == 132U);       /* 20 a second: x2 */
    clock_ms += 25U;
    turn(E_KNOB3, 1);
    CHECK(orch_ui_tempo() == 148U);       /* 40 a second: x8 */
    clock_ms += 60U;
    turn(E_KNOB3, 1);
    CHECK(orch_ui_tempo() == 156U);       /* slowing: x4, not a jump */
    clock_ms += 25U;
    turn(E_KNOB3, -1);
    CHECK(orch_ui_tempo() == 154U);       /* turning back: one detent */

    /* A percentage keeps its 5 % a detent, so slow turning is as fine as it
     * ever was, and a fast turn only doubles that step: the delay mix goes up
     * 5, then 10, not the 40 the full x8 would give it. */
    press(B_FX);                          /* a fresh load selected the chorus */
    turn(E_ALGO, 1);
    turn(E_ALGO, 1);                      /* -> DELAY, whose mix the save kept */
    CHECK(orch_ui_fx_selection() == 2U);
    CHECK(orch_fx_mix(ORCH_FX_DELAY) == 35U);
    clock_ms += 2000U;
    turn(E_KNOB1, 1);
    CHECK(orch_fx_mix(ORCH_FX_DELAY) == 40U);   /* alone: 5 */
    clock_ms += 25U;
    turn(E_KNOB1, 1);
    CHECK(orch_fx_mix(ORCH_FX_DELAY) == 50U);   /* 40 a second: x8 asked, x2 given */
    clock_ms += 25U;
    turn(E_KNOB1, 1);
    CHECK(orch_fx_mix(ORCH_FX_DELAY) == 60U);
    clock_ms += 25U;
    turn(E_KNOB1, -4);
    CHECK(orch_fx_mix(ORCH_FX_DELAY) == 50U);   /* back, still one 10 % step */

    /* the CONSOLE's KNOB4 recalls a factory scene whole: the parts, the tempo,
     * the motif on the SEQUENCE page, and nothing left sounding */
    press(B_FX);
    press(B_FX);                          /* a chord, to be cut by the recall */
    press(B_GLO);
    turn(E_KNOB4, 1);
    memset(block, 0, sizeof block);
    orch_render_q23(block, 128U);
    CHECK(orch_active_voices() == 0U);
    CHECK(orch_part_get(2)->instrument == ORCH_INST_HARP);    /* BAROQUE */
    CHECK(orch_ui_tempo() == orch_scene_bpm(1U));
    {
        struct orch_perf_config pc;
        orch_perf_get_config(0U, &pc);
        CHECK(pc.arp_enabled == 1U);
        CHECK(pc.bpm == orch_ui_tempo());
    }
    CHECK(paused == 0);

    if (failures) {
        printf("orch_ui: %d failure(s)\n", failures);
        return 1;
    }
    printf("orch_ui: all checks passed (%u fills, %u blits, %u leds)\n",
           fills, blits, leds);
    return 0;
}
