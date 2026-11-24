/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ORCH_HAWK_BRIDGE_H
#define ORCH_HAWK_BRIDGE_H

#include <stdint.h>

void orch_hawk_init(void);
void orch_hawk_render_q15(int32_t *output, uint32_t frames);
void orch_hawk_note_event(uint8_t channel, uint8_t note, uint8_t velocity);
void orch_hawk_midi_realtime(uint8_t status);
void orch_hawk_panic(void);

#endif
