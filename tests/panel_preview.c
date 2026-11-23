/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Renders the panel to a PPM file with the board services stubbed. This is the
 * only way to look at the screen without an FM-1 attached; it is a preview of
 * the layout, not evidence that the ST7789 shows it correctly. */
#include <stdio.h>
#include <string.h>

#include "fm1.h"
#include "orch_engine.h"
#include "orch_ui.h"

static uint16_t fb[240 * 240];
static struct fm1_controls feed;
static uint32_t clock_ms;

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

static void press(unsigned button)
{
    feed.buttons |= 1UL << button;
    orch_ui_handle(&feed);
    feed.buttons &= ~(1UL << button);
    orch_ui_handle(&feed);
}

int main(int argc, char **argv)
{
    orch_init();
    memset(&feed, 0, sizeof feed);
    feed.knob = 820U;
    orch_ui_init();

    press(2);                       /* part 3 */
    press(4U);                      /* strings for that part */
    press(13);                      /* C major chord */
    int32_t block[256];
    for (int i = 0; i < 60; ++i) orch_render_q23(block, 128U);
    /* the control task only reads every 25 ms, so move its clock forward */
    clock_ms += 40U; orch_ui_control();
    clock_ms += 40U; orch_ui_control();

    const char *path = argc > 1 ? argv[1] : "panel.ppm";
    FILE *out = fopen(path, "wb");
    if (!out) { perror(path); return 1; }
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
    printf("wrote %s (voices: %u)\n", path, orch_active_voices());
    return 0;
}
