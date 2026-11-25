/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The orchestral panel (app/orch_ui.c) inside the HAWK/Felucca firmware.
 *
 * orch_ui.c is written against the FM1 Core application API: controls,
 * screen, LEDs, a millisecond clock, a flash area and an edit transaction.
 * This file is that API on top of the few functions orch_hawk_glue.c exports
 * from HAWK's own static hardware layer (orch_hawk_hw.h), so the same panel
 * that runs on the FM1 Core SDK and in the browser runs on the firmware that
 * CI builds. It is compiled as its own unit, next to the orchestral sources.
 *
 * Main loop only, except orch_hawk_panel_key(), which the audio interrupt
 * calls for a note key (as HAWK's keys.c played its synth there: lowest
 * latency).
 */
#include <stdint.h>

#include "fm1.h"
#include "fm1_engine_control.h"
#include "orch_engine.h"
#include "orch_hawk_bridge.h"
#include "orch_hawk_hw.h"
#include "orch_ui.h"

#define STORE_BYTES 3840U       /* HAWK's storage payload (storage.c ST_PAYLOAD_MAX) */
#define NOTE_KEYS 27U
#define FIRST_KEY_NOTE 53U      /* F3, as HAWK's KEY_BASE_NOTE and FM1 Core */
#define NO_NOTE 0xFFU

/* --- FM1 Core API --------------------------------------------------------- */

void fm1_controls_init(void) {}

void fm1_controls_read(struct fm1_controls *state)
{
    if (!state) return;
    state->keys = orch_hw_keys();
    state->buttons = orch_hw_buttons();
    for (unsigned e = 0; e < 7U; ++e) {
        int32_t d = orch_hw_encoder(e);
        if (d > 32767) d = 32767;
        if (d < -32767) d = -32767;
        state->encoders[e] = (int16_t)d;
    }
    /* HAWK's MASTER pot already scales the output in the bridge (the square
     * law of its main.c), so the panel's own master stays at unity */
    state->knob = 1023U;
    state->battery = 0U;
}

static uint8_t led_lit[41];
static uint8_t led_dirty = 1U;

void fm1_led_set(unsigned key, int on)
{
    if (key >= sizeof led_lit) return;
    const uint8_t v = on ? 1U : 0U;
    if (led_lit[key] != v) { led_lit[key] = v; led_dirty = 1U; }
}

uint32_t fm1_millis(void) { return orch_hw_ms(); }

void fm1_screen_init(void) {}

void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t rgb565)
{
    orch_hw_fill(x, y, w, h, rgb565);
}

int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h,
                    const uint16_t *pixels, size_t count)
{
    if (!pixels || !w || !h || count < (size_t)w * h || x + w > 240U || y + h > 240U)
        return -1;
    orch_hw_blit(x, y, w, h, pixels);
    return 0;
}

/* The panel keeps one record at offset 0 and writes it whole after an erase.
 * HAWK's storage keeps an object as an A/B pair with a CRC and a commit
 * record, which is safer than a bare sector: the erase is the save's own, and
 * a read hands back the last record that was committed whole. */
uint32_t fm1_flash_size(void) { return STORE_BYTES; }

int fm1_flash_erase(uint32_t offset) { return offset ? -1 : FM1_FLASH_OK; }

int fm1_flash_write(uint32_t offset, const void *data, size_t bytes)
{
    if (offset || !data || bytes > STORE_BYTES) return -1;
    return orch_hw_store_save(data, (uint32_t)bytes) == 0 ? FM1_FLASH_OK : -1;
}

int fm1_flash_read(uint32_t offset, void *data, size_t bytes)
{
    if (offset || !data || bytes > STORE_BYTES) return -1;
    return orch_hw_store_load(data, (uint32_t)bytes) == (int)bytes ? FM1_FLASH_OK : -1;
}

static unsigned pause_depth;

void fm1_engine_pause(void)
{
    if (pause_depth++ == 0U) orch_hw_irq_off();
}

void fm1_engine_resume(void)
{
    if (pause_depth && --pause_depth == 0U) orch_hw_irq_on();
}

/* --- the panel in the main loop ------------------------------------------- */

static uint8_t panel_up;

void orch_hawk_panel_start(void)
{
    orch_ui_init();
    panel_up = 1U;
}

void orch_hawk_panel_frame(int screen_is_ours)
{
    if (!panel_up) return;
    if (!screen_is_ours) return;   /* the UBOOT countdown has the screen */
    orch_ui_control();
    if (led_dirty) {
        led_dirty = 0U;
        orch_hw_leds(led_lit);
    }
}

void orch_hawk_panel_repaint(void)
{
    if (panel_up) orch_ui_repaint();
    led_dirty = 1U;
}

/* --- the note keys, from the audio interrupt ------------------------------ */

/* A key remembers the note and the part it started, so a transpose, a scale
 * or a part change while it is down never leaves a note hanging. */
static uint8_t key_note[NOTE_KEYS] = {
    NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE,
    NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE,
    NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE, NO_NOTE
};
static uint8_t key_channel[NOTE_KEYS];

void orch_hawk_panel_key(unsigned key, unsigned down, unsigned velocity)
{
    if (key >= NOTE_KEYS) return;
    if (down) {
        if (key_note[key] != NO_NOTE) return;
        /* the SEQUENCE page records keys while REC is on; they still sound */
        const int note = orch_ui_map_note((int)(FIRST_KEY_NOTE + key));
        if (note < 0) return;
        key_note[key] = (uint8_t)note;
        key_channel[key] = (uint8_t)(orch_ui_part() & 15U);
        orch_hawk_note_event(key_channel[key], (uint8_t)note, (uint8_t)velocity);
    } else if (key_note[key] != NO_NOTE) {
        orch_hawk_note_event(key_channel[key], key_note[key], 0U);
        key_note[key] = NO_NOTE;
    }
}

void orch_hawk_panel_keys_off(void)
{
    for (unsigned k = 0; k < NOTE_KEYS; ++k) {
        if (key_note[k] == NO_NOTE) continue;
        orch_hawk_note_event(key_channel[k], key_note[k], 0U);
        key_note[k] = NO_NOTE;
    }
}
