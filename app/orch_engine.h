/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ORCH_ENGINE_H
#define ORCH_ENGINE_H

#include <stdint.h>
#include "orch_adaptive.h"
#include "orch_part.h"

#define ORCH_MAX_VOICES 12U
#define ORCH_MIDI_CHANNELS 16U
#define ORCH_SEQ_STEPS 16U
#define ORCH_STEP_NOTES 4U
#define ORCH_SAMPLE_RATE 44100U

struct orch_step_note { uint8_t note, velocity, gate_percent; };

void orch_init(void);

/* Audio ISR: FM1_BLOCK_FRAMES stereo frames of signed Q23. */
void orch_render_q23(int32_t *stereo, unsigned frames);

/* Event input: MIDI 1.0 channel voice, channel 0..15. */
void orch_midi_short_message(const uint8_t *message, unsigned bytes);
void orch_note_event(unsigned channel, unsigned note, unsigned velocity);
void orch_panic(void);

/* Parts: channel routing, family, articulation and divisi ceiling. */
int orch_engine_configure_part(unsigned part, unsigned family, uint16_t channels);
int orch_engine_set_instrument(unsigned part, unsigned instrument);
int orch_engine_set_articulation(unsigned part, unsigned articulation);
int orch_engine_set_player_cap(unsigned part, unsigned cap);
int orch_engine_set_voice_ceiling(unsigned part, unsigned voices);
unsigned orch_voices_of_part(unsigned part);
unsigned orch_note_count(unsigned part);

/* Sequencer (16 steps, four notes per step, per-note gate percentage). */
void orch_seq_set_tempo(uint16_t bpm);
void orch_seq_set_enabled(unsigned enabled);
int orch_seq_set_step(unsigned step, unsigned slot, const struct orch_step_note *note);
void orch_seq_clear(void);
void orch_seq_set_part(unsigned part);

/* Arpeggiator. A held note on a part with the arp on is collected instead of
 * played; the render loop plays the pattern at the shared tempo. */
enum orch_arp_mode {
    ORCH_ARP_UP = 0, ORCH_ARP_DOWN, ORCH_ARP_UPDOWN, ORCH_ARP_RANDOM,
    ORCH_ARP_AS_PLAYED, ORCH_ARP_MODE_COUNT
};

int orch_arp_set_enabled(unsigned part, unsigned on);
int orch_arp_set_mode(unsigned part, unsigned mode);
int orch_arp_set_octaves(unsigned part, unsigned octaves); /* 1..3 */
int orch_arp_set_division(unsigned part, unsigned div);    /* 0:1/8 1:1/8T 2:1/16 3:1/16T 4:1/32 */
int orch_arp_set_gate(unsigned part, unsigned percent);   /* 5..100 */
unsigned orch_arp_enabled(unsigned part);
unsigned orch_arp_held(unsigned part);
unsigned orch_arp_sounding(unsigned part); /* note or 0xFF when silent */
unsigned orch_arp_mode_of(unsigned part);
unsigned orch_arp_octaves_of(unsigned part);
unsigned orch_arp_div_of(unsigned part);
unsigned orch_arp_gate_of(unsigned part);

/* The step the sequencer is on, or 0xFF when it is stopped. */
unsigned orch_seq_position(void);

unsigned orch_active_voices(void);

/* Introspection for the panel and the host tests. */
const struct orch_intent *orch_last_intent(unsigned part);

#endif
