/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ORCH_ENGINE_H
#define ORCH_ENGINE_H
#include <stdint.h>
#define ORCH_MAX_VOICES 12U
#define ORCH_MIDI_CHANNELS 16U
#define ORCH_SEQ_STEPS 16U
#define ORCH_STEP_NOTES 4U
#define ORCH_SAMPLE_RATE 44100U
#define ORCH_DIVISI_PLAYERS 6U

enum orch_model { ORCH_MODEL_WAVEGUIDE_STRING = 0, ORCH_MODEL_PLUCKED_STRING = 1 };
enum orch_family {
    ORCH_FAMILY_VIOLIN = 0, ORCH_FAMILY_VIOLA, ORCH_FAMILY_CELLO,
    ORCH_FAMILY_BASS, ORCH_FAMILY_WOODWIND, ORCH_FAMILY_BRASS,
    ORCH_FAMILY_CHOIR, ORCH_FAMILY_PERCUSSION
};
struct orch_step_note { uint8_t note, velocity, gate_percent; };
struct orch_divisi_plan {
    uint8_t player_count;
    uint8_t active_mask;
    int8_t semitone_offset[ORCH_DIVISI_PLAYERS];
    uint8_t part_assignment[ORCH_DIVISI_PLAYERS];
    uint8_t density;
};
void orch_init(void);
void orch_render_q23(int32_t *stereo, unsigned frames);
void orch_midi_short_message(const uint8_t *message, unsigned bytes);
void orch_note_event(unsigned channel, unsigned note, unsigned velocity);
void orch_panic(void);
void orch_seq_set_tempo(uint16_t bpm);
void orch_seq_set_enabled(unsigned enabled);
int orch_seq_set_step(unsigned step, unsigned slot, const struct orch_step_note *note);
void orch_seq_clear(void);
void orch_set_model(enum orch_model model);
void orch_set_family(enum orch_family family);
void orch_set_divisi(unsigned enabled);
void orch_plan_divisi(const uint8_t *notes, unsigned note_count,
                      unsigned phrase_density, struct orch_divisi_plan *plan);
unsigned orch_active_voices(void);
#endif
