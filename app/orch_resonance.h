/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The orchestra's shared resonance (0.6.3): a bank of eight sympathetic
 * "open strings" — G2 C3 D3 G3 A3 D4 A4 E5, the open strings of the string
 * section — that every part excites at once, as the strings, the soundboards
 * and the hall of a real orchestra ring with what is played around them. A
 * note on (or a fifth or octave against) one of those pitches makes it ring
 * on after the note; others pass by it.
 *
 * Bounded by construction, no limiter: each resonator's input is scaled by
 * (1 - feedback), so its output never exceeds its input's peak, and the
 * output is a crossfade, dry x (1 - m/2) + resonance x m/2, which cannot
 * exceed the larger of the two. With the engine's even 1/n mix that keeps the
 * whole within full scale.
 *
 * Audio interrupt: orch_resonance_process() runs on the finished mix inside
 * orch_render_q23(). The amount is one byte the control task writes. */
#ifndef ORCH_RESONANCE_H
#define ORCH_RESONANCE_H

#include <stdint.h>

#define ORCH_RESONATORS 8U

void orch_resonance_init(void);              /* clears the strings, amount 25 */
void orch_resonance_clear(void);             /* clears the strings only */
void orch_resonance_set_amount(unsigned percent);   /* 0..100 */
unsigned orch_resonance_amount(void);
/* in place, Q23 stereo, interleaved */
void orch_resonance_process(int32_t *stereo, unsigned frames);
/* how much a resonator is ringing, 0..255, for the panel */
unsigned orch_resonance_ring(unsigned k);

#endif
