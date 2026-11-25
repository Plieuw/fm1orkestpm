/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The FM-1 orchestral engine in a browser.
 *
 * Modelled on HAWK CHUA's browser emulator (Plieuw/hawk-chua, web/emu, main
 * 649102d): the firmware's own sources compile to WebAssembly and the board is
 * replaced by a handful of buffers. Here the sources are this repository's
 * app/fm1_app.c and the orch_* engine, and the buffer set is the same idea:
 *
 *   the LCD      a 240 x 240 RGB565 framebuffer (emu_fb)
 *   the panel    14 buttons, 27 keys, 7 encoders and the knob (emu_button,
 *                emu_key, emu_turn, emu_knob)
 *   the MIDI in  emu_note, which goes through the same path as the panel keys
 *   the audio    blocks of FM1_BLOCK_FRAMES stereo frames, Q23 in int32
 *                (emu_render, emu_audio)
 *   the flash    4 KiB of settings, so SAVE works and survives a reload
 *
 * Nothing in the panel is reimplemented here: app/fm1_app.c is the same adapter
 * the firmware uses, so the browser shows what the board would show.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fm1.h"
#include "orch_engine.h"
#include "orch_ui.h"

/* What app/fm1_app.c provides; the SDK's main loop would call the first two. */
void groove_screen_init(void);
void groove_screen_control(void);
void groove_midi_note(unsigned track, unsigned note, unsigned velocity);
int fm1_app_render(int32_t *output, unsigned frames);

#define FB_W 240U
#define FB_H 240U
#define FLASH_BYTES 4096U

static uint16_t fb[FB_W * FB_H];
static uint8_t flash[FLASH_BYTES];
static uint8_t leds[41];
static int32_t block[2U * FM1_BLOCK_FRAMES];
static uint32_t now_ms;
static struct fm1_controls in;

/* ------------------------------------------------------------- the SDK --- */

void fm1_controls_init(void) { memset(&in, 0, sizeof in); }

void fm1_controls_read(struct fm1_controls *state)
{
    if (!state) return;
    *state = in;
    for (unsigned e = 0; e < 7U; ++e) in.encoders[e] = 0;   /* detents are taken once */
}

void fm1_led_set(unsigned key, int on)
{
    if (key < sizeof leds) leds[key] = (uint8_t)(on != 0);
}

uint32_t fm1_millis(void) { return now_ms; }
void fm1_engine_pause(void) {}
void fm1_engine_resume(void) {}
void fm1_control_request(void) {}

void fm1_screen_init(void) { memset(fb, 0, sizeof fb); }

void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t rgb565)
{
    for (unsigned j = y; j < y + h && j < FB_H; ++j)
        for (unsigned i = x; i < x + w && i < FB_W; ++i)
            fb[j * FB_W + i] = rgb565;
}

int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h,
                    const uint16_t *pixels, size_t count)
{
    if (!pixels || count < (size_t)w * h) return -1;
    for (unsigned j = 0; j < h; ++j)
        for (unsigned i = 0; i < w; ++i)
            if (x + i < FB_W && y + j < FB_H)
                fb[(y + j) * FB_W + x + i] = pixels[j * w + i];
    return 0;
}

/* The panel has no DMA: a submitted band is copied at once and is never busy. */
int fm1_screen_submit(unsigned x, unsigned y, unsigned w, unsigned h,
                      uint16_t *pixels, size_t count)
{
    return fm1_screen_blit(x, y, w, h, pixels, count);
}

int fm1_screen_pending(void) { return 0; }

uint32_t fm1_flash_size(void) { return FLASH_BYTES; }

int fm1_flash_read(uint32_t offset, void *data, size_t bytes)
{
    if (!data || offset > FLASH_BYTES || bytes > FLASH_BYTES - offset) return -1;
    memcpy(data, flash + offset, bytes);
    return 0;
}

int fm1_flash_write(uint32_t offset, const void *data, size_t bytes)
{
    if (!data || offset > FLASH_BYTES || bytes > FLASH_BYTES - offset) return -1;
    memcpy(flash + offset, data, bytes);
    return 0;
}

int fm1_flash_erase(uint32_t offset)
{
    if (offset >= FLASH_BYTES) return -1;
    memset(flash + offset, 0xFF, FLASH_BYTES - offset);
    return 0;
}

/* --------------------------------------------------- what the page calls --- */

void emu_init(void)
{
    memset(fb, 0, sizeof fb);
    memset(flash, 0xFF, sizeof flash);      /* erased, as a new board */
    memset(leds, 0, sizeof leds);
    memset(&in, 0, sizeof in);
    now_ms = 0U;
    groove_screen_init();                   /* engine, FX and the panel */
    /* The panel's own init clears the controls, as the SDK's does, so the pot
     * is set after it: all the way up, unity, or the browser starts silent. */
    in.knob = 1023U;
}

void emu_tick(uint32_t millis)
{
    now_ms = millis;
    groove_screen_control();                /* the SDK's control task */
}

void emu_render(void)
{
    memset(block, 0, sizeof block);
    fm1_app_render(block, FM1_BLOCK_FRAMES);  /* engine, FX, master gain */
}

void emu_button(uint32_t mask) { in.buttons = mask & 0x3FFFU; }
void emu_key(uint32_t mask) { in.keys = mask & 0x07FFFFFFUL; }
void emu_turn(uint32_t encoder, int32_t detents)
{
    if (encoder < 7U) in.encoders[encoder] = (int16_t)detents;
}
void emu_knob(uint32_t value) { in.knob = (uint16_t)(value > 1023U ? 1023U : value); }
void emu_battery(uint32_t millivolts) { in.battery = (uint16_t)millivolts; }
void emu_note(uint32_t track, uint32_t note, uint32_t velocity)
{
    groove_midi_note(track, note, velocity);   /* transpose and scale applied */
}

uint32_t emu_fb(void) { return (uint32_t)(uintptr_t)fb; }
uint32_t emu_audio(void) { return (uint32_t)(uintptr_t)block; }
uint32_t emu_flash(void) { return (uint32_t)(uintptr_t)flash; }
uint32_t emu_leds(void) { return (uint32_t)(uintptr_t)leds; }
uint32_t emu_frames(void) { return (uint32_t)FM1_BLOCK_FRAMES; }
uint32_t emu_fb_width(void) { return FB_W; }
uint32_t emu_fb_height(void) { return FB_H; }

/* What the page shows beside the panel: a part's sounding voices and the master
 * gain, so a silent browser can be told apart from a silent engine. */
uint32_t emu_voices(uint32_t part) { return orch_voices_of_part(part); }
uint32_t emu_gain(void) { return orch_ui_master_q23; }
