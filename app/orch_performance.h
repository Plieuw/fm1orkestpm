/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Orchestral ARP / phrase sequencer interface.
 * Musical feature reference: Plieuw/hawk-chua main, commit
 * e750df6a2e13490a4f01b84d3f9c553f4612163f. This module is a new,
 * FM1-orchestra-specific implementation; it does not include HAWK CHUA code.
 */
#ifndef ORCH_PERFORMANCE_H
#define ORCH_PERFORMANCE_H

#include <stdint.h>

#define ORCH_PERF_PARTS 4U
#define ORCH_PERF_SAMPLE_RATE 44100U
#define ORCH_PERF_GE_COUNT 46U
#define ORCH_PERF_ARP_TYPES 31U
#define ORCH_PERF_DIVISIONS 10U

enum orch_perf_logic {
    ORCH_PERF_OR = 0, ORCH_PERF_AND, ORCH_PERF_XOR,
    ORCH_PERF_NOR, ORCH_PERF_ALT, ORCH_PERF_SR,
    ORCH_PERF_LOGIC_COUNT
};

struct orch_perf_config {
    uint16_t bpm;
    uint8_t arp_enabled;
    uint8_t phrase_enabled;
    uint8_t external_clock;
    uint8_t latch;
    uint8_t arp_type;
    uint8_t phrase_ge;
    uint8_t division;
    uint8_t gate;
    uint8_t swing;
    uint8_t logic;
    uint8_t inverse;
    uint8_t quantize;   /* 0 off, 1 chord, 2 inferred major/minor, 3..17 scales */
    uint8_t root;       /* 0 auto, 1..12 C..B */
    uint8_t density;    /* phrase density, 0..127 */
    uint8_t range;      /* phrase octave range, 1..4 */
    uint8_t accent;     /* phrase accent amount, 0..127 */
    uint8_t cluster;    /* phrase chord size, 1..4 */
    uint8_t repeat;     /* phrase echoes, 0..3 */
    uint8_t velocity_groove; /* 0..7 */
    uint8_t gate_groove;     /* 0..7 */
};

struct orch_perf_event {
    uint8_t part;
    uint8_t note;
    uint8_t velocity;
    uint8_t note_on;
};

void orch_perf_init(void);
void orch_perf_default_config(struct orch_perf_config *config);
int orch_perf_set_config(unsigned part, const struct orch_perf_config *config);
int orch_perf_get_config(unsigned part, struct orch_perf_config *config);
/* Returns non-zero when this note was captured by an enabled performance
 * engine and should not be sent directly to the instrument voices. */
int orch_perf_note_event(unsigned part, unsigned note, unsigned velocity);
void orch_perf_midi_realtime(unsigned status, uint32_t sample_now);
void orch_perf_set_external_tempo(unsigned bpm);
void orch_perf_advance_sample(uint32_t sample_now);
int orch_perf_pop_event(struct orch_perf_event *event);
void orch_perf_reset_notes(void);
unsigned orch_perf_current_step(unsigned part);

#ifdef FM1_HOST_TEST
unsigned orch_test_phrase_arc_velocity(unsigned velocity, unsigned position);
#endif

#endif
