/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ORCH_ENGINE_H
#define ORCH_ENGINE_H

#include <stdint.h>
#include "orch_adaptive.h"
#include "orch_part.h"
#include "orch_performance.h"

#define ORCH_MAX_VOICES 8U
#define ORCH_MIDI_CHANNELS 16U
#define ORCH_SEQ_STEPS 16U
#define ORCH_STEP_NOTES 4U
#define ORCH_SAMPLE_RATE 44100U
#define ORCH_DEFAULT_BPM 128U   /* until a MIDI clock or the playing says otherwise */

/* chance: percent the step plays (0 means always, so older data plays). */
struct orch_step_note { uint8_t note, velocity, gate_percent, chance; };

void orch_init(void);

/* Audio ISR: FM1_BLOCK_FRAMES stereo frames of signed Q23. */
void orch_render_q23(int32_t *stereo, unsigned frames);

/* A second core, if the platform has one (the FM-1's AC79 has two). run
 * hands it fn(arg) and returns 1 if it took the job, 0 if not (then this
 * core does the work); wait returns 0 when the job is done, non-zero when
 * the core stopped answering (then this core does it again). With none set
 * every voice renders here. The output is the same bit for bit either way. */
typedef int (*orch_core_run_fn)(void (*fn)(uint32_t), uint32_t arg);
typedef int (*orch_core_wait_fn)(void);
void orch_set_second_core(orch_core_run_fn run, orch_core_wait_fn wait);
uint32_t orch_second_core_chunks(void);   /* chunks it has shared so far */

/* Event input: MIDI 1.0 channel voice, channel 0..15. */
void orch_midi_short_message(const uint8_t *message, unsigned bytes);
void orch_midi_realtime(unsigned status);
void orch_midi_clock_tempo(unsigned bpm);
void orch_note_event(unsigned channel, unsigned note, unsigned velocity);
void orch_reset_notes(void);

/* Parts: channel routing, family, articulation and divisi ceiling. */
int orch_engine_configure_part(unsigned part, unsigned family, uint16_t channels);
int orch_engine_set_instrument(unsigned part, unsigned instrument);
int orch_engine_set_articulation(unsigned part, unsigned articulation);
int orch_engine_set_player_cap(unsigned part, unsigned cap);
int orch_engine_set_adsr(unsigned part, unsigned attack_ms, unsigned decay_ms,
                         unsigned sustain_percent, unsigned release_ms);
int orch_engine_set_lfo(unsigned part, unsigned wave, unsigned destinations,
                        unsigned depth, unsigned rate_millihz,
                        unsigned sync_division, unsigned delay_ms, unsigned fade_ms);
unsigned orch_voices_of_part(unsigned part);
unsigned orch_note_count(unsigned part);

/* Sequencer (16 steps, four notes per step, per-note gate percentage). */
void orch_seq_set_tempo(uint16_t bpm);
unsigned orch_seq_tempo(void);       /* the orchestra's tempo, wherever it came from */
unsigned orch_tempo_from_clock(void); /* 1 while a MIDI clock sets it */
unsigned orch_seq_enabled(void);
void orch_seq_set_enabled(unsigned enabled);
/* Since 0.6.0 every part has its own lane of 16 steps, played on that part.
 * orch_seq_set_part() chooses the lane orch_seq_set_step() and
 * orch_seq_clear() address; orch_seq_clear_all() empties every lane. The
 * transport (enabled, tempo, length) is shared. */
int orch_seq_set_step(unsigned step, unsigned slot, const struct orch_step_note *note);
void orch_seq_clear(void);
void orch_seq_clear_all(void);
void orch_seq_set_part(unsigned part);
void orch_seq_set_length(unsigned steps);          /* 1..16 */
unsigned orch_seq_length(void);
void orch_seq_set_lane_muted(unsigned part, unsigned muted);
unsigned orch_seq_lane_muted(unsigned part);
/* Optional orchestral arranger; disabled by default. */
void orch_seq_set_adaptive(unsigned enabled);
/* Optional cross-part call/response; disabled by default. Interval is semitones. */
void orch_call_response_configure(unsigned enabled, unsigned leader,
                                  unsigned follower, int interval);

/* The step the sequencer is on, or 0xFF when it is stopped. */
unsigned orch_seq_position(void);

unsigned orch_active_voices(void);

/* Introspection for the panel and the host tests. */
#ifdef FM1_HOST_TEST
uint32_t orch_test_loop_gain_per_sample_q23(unsigned trip_gain_q15, unsigned samples);
int32_t orch_test_expression_q15(void);
unsigned orch_test_seq_adapt_note(unsigned part, unsigned note);
unsigned orch_test_seq_phrase_arc_velocity(unsigned velocity, unsigned position);
#endif
const struct orch_intent *orch_last_intent(unsigned part);

#endif
