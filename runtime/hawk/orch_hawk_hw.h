/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What the HAWK/Felucca runtime gives the orchestral panel. Felucca is one
 * amalgamated translation unit whose hardware layer is all static, so
 * orch_hawk_glue.c (included into felucca.c) exports these few functions and
 * the orchestral sources, compiled as their own units, call them. */
#ifndef ORCH_HAWK_HW_H
#define ORCH_HAWK_HW_H

#include <stdint.h>

uint32_t orch_hw_ms(void);
/* debounced buttons 0..13 and note keys 0..26 */
uint32_t orch_hw_buttons(void);
uint32_t orch_hw_keys(void);
/* detents since the last call, by matrix encoder 0..6, clockwise positive */
int32_t orch_hw_encoder(unsigned matrix_id);
/* the screen: a rectangle of one colour, and native-endian RGB565 rows */
void orch_hw_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t rgb565);
void orch_hw_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px);
/* the LEDs: bit i of the 41 ids (0..13 buttons, 14..40 keys) */
void orch_hw_leds(const uint8_t lit[41]);
/* the settings object: <0 on failure, else the bytes read */
int orch_hw_store_load(void *data, uint32_t max);
int orch_hw_store_save(const void *data, uint32_t len);
/* interrupts off / on, for edit transactions (not nested here) */
void orch_hw_irq_off(void);
void orch_hw_irq_on(void);

#endif
