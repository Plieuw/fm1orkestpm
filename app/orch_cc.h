/* SPDX-License-Identifier: GPL-3.0-or-later */
/* MIDI controllers, laid out as a Yamaha XG tone generator lays them out.
 *
 * Two kinds of controller:
 *  - playing controllers (velocity, pitch bend, CC1 modulation, CC2 breath,
 *    CC7 volume, CC10 pan, CC11 expression, CC67 soft pedal, CC74
 *    brightness): per part, live, gliding so a fast sweep does not zipper,
 *    not saved;
 *  - setting controllers (CC71..78 the sound controllers, program change
 *    with bank select, the effect sends): they turn the settings the panel
 *    turns, so the panel shows them and SAVE keeps them.
 * As on XG, the sound controllers are relative: 64 is the instrument as it
 * is, 0 and 127 the ends. Every law uses all 128 values. The map is in
 * docs/MIDI.md.
 */
#ifndef ORCH_CC_H
#define ORCH_CC_H

#include <stdint.h>
#include "orch_part.h"

void orch_cc_init(void);                /* every part to its reset values */
void orch_cc_reset_part(unsigned part); /* CC121, as XG resets */

/* A controller (0..127), a program change (cc = ORCH_CC_PROGRAM) or a pitch
 * bend (cc = ORCH_CC_BEND, value 0..16383) for a part. Call from the audio
 * side, between samples (the engine's event queue does). */
#define ORCH_CC_PROGRAM 0x100U
#define ORCH_CC_BEND 0x101U
void orch_cc_apply(unsigned part, unsigned cc, unsigned value);

/* The velocity's share of a note's output, Q12, 127 -> 4064. */
uint16_t orch_cc_velocity_q12(unsigned velocity);

/* Once per sample: the playing controllers glide to where they were sent. */
void orch_cc_step(void);

/* Per part, after the glide:
 *  gain     output Q15: volume x expression x breath x soft pedal
 *  excite   Q15: how hard it is bowed or blown (breath)
 *  bright   damping offset in 1/256 steps (CC74, soft pedal)
 *  bend     loop-length ratio Q16 (pitch bend; 65536 = none)
 *  mod      vibrato depth from the modulation wheel, in LFO depth units
 *           (100 = +-50 cents), on top of the part's own LFO
 *  pan      left and right balance Q15 (CC10) */
extern int32_t orch_cc_gain_q15[ORCH_PARTS];
extern int32_t orch_cc_excite_q15[ORCH_PARTS];
extern int32_t orch_cc_bright_q8[ORCH_PARTS];
extern uint32_t orch_cc_bend_q16[ORCH_PARTS];
extern uint8_t orch_cc_mod[ORCH_PARTS];
extern int32_t orch_cc_pan_l_q15[ORCH_PARTS], orch_cc_pan_r_q15[ORCH_PARTS];

/* The last value a playing controller received (bend: 0..16383). */
unsigned orch_cc_value(unsigned part, unsigned cc);
unsigned orch_cc_bend_range(unsigned part);   /* semitones, RPN 0 */

#endif
