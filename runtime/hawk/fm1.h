/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The part of the FM1 Core application API (rgrvmusic/fm1-core, include/
 * fm1.h) that the orchestral panel and engine use, for the HAWK/Felucca
 * firmware build. It is copied next to the orchestral sources by
 * tools/prepare_hawk_runtime.py; the functions are implemented on HAWK's own
 * screen, input and storage by orch_hawk_panel.c and orch_hawk_glue.c.
 * Written for this project, not copied from FM1 Core. */
#ifndef ORCH_HAWK_FM1_COMPAT_H
#define ORCH_HAWK_FM1_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#define FM1_AUDIO_CODE
#define FM1_AUDIO_DATA __attribute__((section(".pool")))
#define FM1_BLOCK_FRAMES 32U
/* the panel's text line and path buffers: in the pool, the RAM section is
 * nearly full beside HAWK's runtime (0.3.11: 65 856 of 73 728 B) */
#define ORCH_UI_DATA __attribute__((section(".pool")))

struct fm1_controls {
    uint32_t keys;      /* bits 0..26: F3..G5 */
    uint32_t buttons;   /* physical panel buttons 0..13 */
    int16_t encoders[7];/* signed detents since the previous read */
    uint16_t knob, battery;
};

void fm1_controls_init(void);
void fm1_controls_read(struct fm1_controls *state);
void fm1_led_set(unsigned key, int on);  /* 0..13 buttons, 14..40 note keys */
uint32_t fm1_millis(void);

void fm1_screen_init(void);
void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t rgb565);
int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h,
                    const uint16_t *pixels, size_t count);

enum { FM1_FLASH_OK = 0 };
uint32_t fm1_flash_size(void);
int fm1_flash_read(uint32_t offset, void *data, size_t bytes);
int fm1_flash_write(uint32_t offset, const void *data, size_t bytes);
int fm1_flash_erase(uint32_t offset);

#endif
