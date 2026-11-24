/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ORCH_LEVEL_H
#define ORCH_LEVEL_H

#include <stdint.h>
#include "fm1.h"

/* The output, as the panel hears it: 0..ORCH_LEVEL_MAX (24). The scale is the
 * orchestra's — eight voices, the four parts at two each, are the top and every
 * voice is three steps — and the peak of the block moves it inside that size,
 * falling back slowly through a tail. A full orchestra reads 24, a single held
 * note (two players in this engine) six.
 *
 * The audio callback hands every block it has finished — after the effects and
 * the master gain, so this is what leaves the instrument — to orch_level_take();
 * the control task reads it with orch_output_level(). It is the one value on
 * the screen that follows the performance instead of which notes are held: a
 * chord let ring takes the light down while the keys are still down. */
#define ORCH_LEVEL_MAX 24U

FM1_AUDIO_CODE void orch_level_take(const int32_t *pcm, unsigned samples);
unsigned orch_output_level(void);

#endif
