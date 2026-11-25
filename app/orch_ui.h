/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FM-1 panel: screen, buttons, encoders and LEDs for the orchestral engine.
 *
 * Control task only. The audio interrupt reads orch_ui_master_q23 and the FX
 * bytes the panel writes through orch_fx.h, and nothing else.
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
void orch_ui_repaint(void);    /* after another owner had the screen */

/* Octave transpose and scale quantisation, applied by the note paths in
 * fm1_app.c. Returns -1 when the result leaves the MIDI range. */
int orch_ui_map_note(int note);

unsigned orch_ui_part(void);
unsigned orch_ui_tempo(void);
unsigned orch_ui_page(void);
unsigned orch_ui_transpose(void);
unsigned orch_ui_scale(void);
unsigned orch_ui_recording(void);
unsigned orch_ui_seq_step(void);
unsigned orch_ui_saved(void);   /* 1 after a successful save this session */
unsigned orch_ui_fx_selection(void); /* effect row the FX page has selected */

#endif
