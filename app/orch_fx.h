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

/* Settings are percentages, 0..100. Mix is dry/wet; the two parameters mean
 * something different per effect (see orch_fx_param_a_name()). */
void orch_fx_init(void);
/* Ask the render context to flush the delay and reverb tails. */
void orch_fx_reset(void);
/* Audio interrupt: applies the enabled effects in place to Q23 stereo. */
void orch_fx_process(int32_t *stereo_q23, unsigned frames);

int orch_fx_set_enabled(unsigned slot, unsigned on);
int orch_fx_set_mix(unsigned slot, unsigned percent);
int orch_fx_set_param_a(unsigned slot, unsigned percent);
int orch_fx_set_param_b(unsigned slot, unsigned percent);

unsigned orch_fx_enabled(unsigned slot);
unsigned orch_fx_mix(unsigned slot);
unsigned orch_fx_param_a(unsigned slot);
unsigned orch_fx_param_b(unsigned slot);
unsigned orch_fx_any_enabled(void);

const char *orch_fx_name(unsigned slot);       /* CHORUS, ENSEMBLE, ... */
const char *orch_fx_param_a_name(unsigned slot);
const char *orch_fx_param_b_name(unsigned slot);

#endif
