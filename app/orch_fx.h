/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Master effect chain for the orchestral engine: chorus, ensemble, echo and
 * shimmer reverb.
 *
 * The chain is a master insert after the voice mix, shared by all parts. It
 * runs in the audio interrupt; the panel writes one byte per setting and reads
 * the same bytes back, so the two contexts never share a structure that could
 * be read half-written.
 */
#ifndef ORCH_FX_H
#define ORCH_FX_H

#include <stdint.h>

enum orch_fx_slot {
    ORCH_FX_CHORUS = 0,
    ORCH_FX_ENSEMBLE,
    ORCH_FX_DELAY,
    ORCH_FX_REVERB,
    ORCH_FX_SLOT_COUNT
};

/* Four parameters an effect, all percentages: what each one means is the
 * effect's own business, and orch_fx_param_name() says which is which —
 * RATE, DEPTH, SPREAD and TONE on the chorus; RATE, DEPTH, WIDTH and TAPS on
 * the ensemble; TIME, FEEDBACK, SYNC and DAMP on the echo; DECAY, SHIM, EARLY
 * and DAMP on the shimmer. Mix is dry/wet and sits outside them. */
#define ORCH_FX_PARAMS 4U

void orch_fx_init(void);
/* Ask the render context to flush the delay and reverb tails. */
void orch_fx_reset(void);
/* The tempo the delay's SYNC follows, in BPM; 120 until told otherwise. */
void orch_fx_set_tempo(unsigned bpm);
/* Audio interrupt: applies the enabled effects in place to Q23 stereo. */
void orch_fx_process(int32_t *stereo_q23, unsigned frames);

int orch_fx_set_enabled(unsigned slot, unsigned on);
int orch_fx_set_mix(unsigned slot, unsigned percent);
int orch_fx_set_param(unsigned slot, unsigned index, unsigned percent);

unsigned orch_fx_enabled(unsigned slot);
unsigned orch_fx_mix(unsigned slot);
unsigned orch_fx_param(unsigned slot, unsigned index);
unsigned orch_fx_any_enabled(void);

const char *orch_fx_name(unsigned slot);              /* CHORUS, ENSEMBLE, ... */
const char *orch_fx_param_name(unsigned slot, unsigned index);

#endif
