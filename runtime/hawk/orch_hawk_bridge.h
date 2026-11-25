/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ORCH_HAWK_BRIDGE_H
#define ORCH_HAWK_BRIDGE_H

#include <stdint.h>

void orch_hawk_init(void);
void orch_hawk_render_q15(int32_t *output, uint32_t frames, uint32_t master_q12);
/* the second core for the engine (orch_set_second_core): run hands it
 * fn(arg), 1 = taken; wait: 0 = done, else this core does the job */
void orch_hawk_set_second_core(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void));

/* The panel (orch_hawk_panel.c): started once the splash is over, a frame
 * per main-loop pass, a full repaint after the update countdown, and the
 * note keys from the audio interrupt. */
void orch_hawk_panel_start(void);
void orch_hawk_panel_frame(int screen_is_ours);
void orch_hawk_panel_repaint(void);
void orch_hawk_panel_key(unsigned key, unsigned down, unsigned velocity);
void orch_hawk_panel_keys_off(void);
void orch_hawk_note_event(uint8_t channel, uint8_t note, uint8_t velocity);
void orch_hawk_midi_realtime(uint8_t status);
void orch_hawk_midi_message(uint8_t status, uint8_t d1, uint8_t d2);
void orch_hawk_control_change(uint8_t channel, uint8_t controller, uint8_t value);
void orch_hawk_reset_notes(void);

#endif
