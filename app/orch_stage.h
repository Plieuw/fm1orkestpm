/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Orchestral seating and the listener.
 *
 * The four parts can each hold any instrument, so where a voice stands comes
 * from the section the instrument belongs to and not from the part number:
 * violins on the left, violas inwards, cellos and basses on the right, the
 * woodwinds in the middle, the brass to their right and the percussion at the
 * back right. A part's divisi players share the width of their own section
 * (orch_divisi.c hands over a section-relative pan, 128 = its middle).
 *
 * The listener is the control the player has over that image: `x` moves the
 * seat left and right, `depth` moves it back, which narrows the stage because
 * a section heard from further away subtends a smaller angle. Depth changes no
 * level: this is a stereo image, not a room simulation.
 */
#ifndef ORCH_STAGE_H
#define ORCH_STAGE_H

#include <stdint.h>

/* 0 hard left, 128 centre, 255 hard right, seen from the audience. */
unsigned orch_stage_seat(unsigned instrument);

/* A player's pan: section_pan is relative to the middle of the section. */
unsigned orch_stage_place(unsigned instrument, uint8_t section_pan);

/* x -100..100 (left..right), depth 0..100 (front..back); both are clamped. */
void orch_stage_set_listener(int x, int depth);
int orch_stage_listener_x(void);
unsigned orch_stage_listener_depth(void);
void orch_stage_reset(void);

#endif
