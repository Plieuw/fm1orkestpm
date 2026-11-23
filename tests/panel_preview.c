/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Renders the panel pages to PPM files with the board services stubbed. This is
 * the only way to look at the screen without an FM-1 attached; it is a preview
 * of the layout, not evidence that the ST7789 shows it.
 *
 *   ./panel_preview /tmp/panel      -> /tmp/panel-parts.ppm, -arp.ppm, -seq.ppm
 */
#include <stdio.h>
#include <string.h>

#include "fm1.h"
#include "orch_engine.h"
#include "orch_ui.h"

static uint16_t fb[240 * 240];
static struct fm1_controls feed;
static uint32_t clock_ms;
static int32_t block[256];

void fm1_controls_init(void) {}
void fm1_controls_read(struct fm1_controls *state) { *state = feed; }
void fm1_screen_init(void) {}
void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t color)
{
    for (unsigned row = 0; row < h; ++row)
        for (unsigned col = 0; col < w; ++col)
            if (x + col < 240U && y + row < 240U)
                fb[(y + row) * 240U + x + col] = color;
}
int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h,
                    const uint16_t *pixels, size_t count)
{
    if (count < (size_t)w * h) return -1;
    for (unsigned row = 0; row < h; ++row)
        for (unsigned col = 0; col < w; ++col)
            if (x + col < 240U && y + row < 240U)
                fb[(y + row) * 240U + x + col] = pixels[row * w + col];
    return 0;
}
void fm1_led_set(unsigned key, int on) { (void)key; (void)on; }
uint32_t fm1_millis(void) { return clock_ms; }

uint32_t fm1_flash_size(void) { return 4096U; }
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

static void hold(unsigned note, int down)
{
    const unsigned bit = note - 53U;
    if (down) feed.keys |= 1UL << bit;
    else feed.keys &= ~(1UL << bit);
    orch_ui_handle(&feed);
}

static void render(unsigned blocks)
{
    for (unsigned i = 0; i < blocks; ++i) {
        memset(block, 0, sizeof block);
        orch_render_q23(block, 128U);
    }
}

static void shot(const char *base, const char *suffix)
{
    char path[256];
    snprintf(path, sizeof path, "%s-%s.ppm", base, suffix);
    FILE *out = fopen(path, "wb");
    if (!out) { perror(path); return; }
    fprintf(out, "P6\n240 240\n255\n");
    for (unsigned i = 0; i < 240U * 240U; ++i) {
        const unsigned c = fb[i];
        const unsigned char rgb[3] = {
            (unsigned char)(((c >> 11) & 31U) * 255U / 31U),
            (unsigned char)(((c >> 5) & 63U) * 255U / 63U),
            (unsigned char)((c & 31U) * 255U / 31U)
        };
        fwrite(rgb, 1, 3, out);
    }
    fclose(out);
    printf("wrote %s\n", path);
}

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : "/tmp/panel";

    orch_init();
    memset(&feed, 0, sizeof feed);
    feed.knob = 820U;
    orch_ui_init();

    /* PARTS: part 3 on strings, a chord sounding */
    turn(0, 2);            /* SELECT -> part 3 */
    turn(1, 1);            /* ALGORITHM -> strings? brass is 2, so this is choir */
    turn(1, 1);            /* -> strings (wraps to family 0) */
    press(2);              /* FX: the four-part chord */
    render(400);
    clock_ms += 40U;
    orch_ui_control();
    shot(base, "parts");

    /* ARP: three keys held, arp on. The keys of the real board arrive as note
     * events through groove_midi_note(), which this preview does not have, so
     * they are sent to the engine directly. */
    press(10);             /* ARP */
    hold(60, 1);
    hold(64, 1);
    hold(67, 1);
    orch_note_event(8U, 60U, 100U);
    orch_note_event(8U, 64U, 100U);
    orch_note_event(8U, 67U, 100U);
    render(200);
    clock_ms += 40U;
    orch_ui_control();
    shot(base, "arp");

    /* SEQUENCER: three steps recorded, running */
    hold(60, 0);
    hold(64, 0);
    hold(67, 0);
    orch_note_event(8U, 60U, 0U);
    orch_note_event(8U, 64U, 0U);
    orch_note_event(8U, 67U, 0U);
    render(64);
    press(11);             /* SEQ */
    press(13);             /* REC */
    hold(60, 1); hold(60, 0);
    hold(64, 1); hold(64, 0);
    hold(67, 1); hold(67, 0);
    press(13);             /* REC off */
    press(12);             /* PLAY */
    turn(1, -3);           /* ALGORITHM: back to step 1 */
    render(200);
    clock_ms += 40U;
    orch_ui_control();
    shot(base, "seq");

    return 0;
}
