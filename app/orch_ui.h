/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FM-1 panel: screen, buttons, encoders and LEDs for the orchestral engine.
 *
 * Control task only. orch_ui_master_q23 is the one word the audio interrupt
 * reads, and the only one it may read.
 */
#ifndef ORCH_UI_H
#define ORCH_UI_H

#include <stdint.h>

struct fm1_controls;

/* Master output gain in Q23 (1 << 23 is unity), written by the control task
 * and read by the renderer. */
extern volatile uint32_t orch_ui_master_q23;

void orch_ui_init(void);       /* engine must already be initialised */
void orch_ui_control(void);    /* call from groove_screen_control() */
void orch_ui_handle(const struct fm1_controls *controls);

unsigned orch_ui_part(void);
unsigned orch_ui_tempo(void);

#endif
