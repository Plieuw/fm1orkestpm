/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Factory orchestra scenes (see orch_scenes.c). */
#ifndef ORCH_SCENES_H
#define ORCH_SCENES_H

#include "orch_engine.h"

#define ORCH_SCENE_COUNT 8U

const char *orch_scene_name(unsigned index);
unsigned orch_scene_bpm(unsigned index);
/* Whether the scene's motif is meant to run, and on which part; the recall
 * itself leaves the sequencer stopped until PLAY. */
unsigned orch_scene_seq_enabled(unsigned index);
unsigned orch_scene_seq_part(unsigned index);
/* The scene's 16-step motif, one note a step (velocity 0 is a rest). */
void orch_scene_step(unsigned index, unsigned step, struct orch_step_note *out);
/* Recall a whole scene into the engine, the performance engine and the FX.
 * Returns -1 for an index out of range. */
int orch_scene_apply(unsigned index);

#endif
