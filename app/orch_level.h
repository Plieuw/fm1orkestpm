/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ORCH_LEVEL_H
#define ORCH_LEVEL_H

#include <stdint.h>
#include "fm1.h"

/* The output, as the panel hears it: 0..255, the peak of the last rendered
 * block through a level that falls back slowly.
 *
 * The audio callback hands every block it has finished — after the effects and
 * the master gain, so this is what leaves the instrument — to orch_level_take();
 * the control task reads it with orch_output_level(). It is the one value on
 * the screen that follows the sound instead of which notes are held: a chord
 * let ring takes the light down while the keys are still down. */
FM1_AUDIO_CODE void orch_level_take(const int32_t *pcm, unsigned samples);
unsigned orch_output_level(void);

#endif
