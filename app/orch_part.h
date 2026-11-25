/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Instrument parts: the MIDI channel a part listens to, its family, and its
 * own adaptive + divisi state. One MIDI note belongs to one part; the part
 * decides with how many modeled players it is realized.
 */
#ifndef ORCH_PART_H
#define ORCH_PART_H

#include <stdint.h>
#include "orch_adaptive.h"
#include "orch_divisi.h"

#define ORCH_PARTS 8U

enum orch_family {
    ORCH_FAM_STRINGS = 0,
    ORCH_FAM_WOODWINDS,
    ORCH_FAM_BRASS,
    ORCH_FAM_MALLETS,
    ORCH_FAM_CHOIR,
    ORCH_FAM_COUNT
};

enum orch_instrument {
    ORCH_INST_VIOLIN = 0, ORCH_INST_VIOLA, ORCH_INST_CELLO,
    ORCH_INST_DOUBLE_BASS, ORCH_INST_HARP,
    ORCH_INST_FLUTE, ORCH_INST_PICCOLO, ORCH_INST_CLARINET,
    ORCH_INST_OBOE, ORCH_INST_BASSOON,
    ORCH_INST_TRUMPET, ORCH_INST_FRENCH_HORN,
    ORCH_INST_TROMBONE, ORCH_INST_TUBA,
    ORCH_INST_MARIMBA, ORCH_INST_XYLOPHONE, ORCH_INST_TIMPANI,
    ORCH_INST_CHOIR,
    /* 0.7.1, appended so saved parts and scenes keep their numbers; the
     * family and its order come from orch_instrument_family and
     * orch_family_member, not from the numbers */
    ORCH_INST_ENGLISH_HORN, ORCH_INST_BASS_CLARINET, ORCH_INST_CONTRABASSOON,
    ORCH_INST_ALTO_FLUTE, ORCH_INST_BASS_TROMBONE,
    ORCH_INST_GLOCKENSPIEL, ORCH_INST_VIBRAPHONE, ORCH_INST_TUBULAR_BELLS,
    ORCH_INST_CELESTA, ORCH_INST_HARPSICHORD,
    ORCH_INST_WOMENS_CHOIR, ORCH_INST_MENS_CHOIR,
    ORCH_INST_COUNT
};

enum orch_lfo_wave { ORCH_LFO_SINE=0, ORCH_LFO_TRIANGLE, ORCH_LFO_SAW,
                     ORCH_LFO_SQUARE, ORCH_LFO_SAMPLE_HOLD };
enum orch_lfo_dest { ORCH_LFO_PITCH=1U, ORCH_LFO_TREMOLO=2U, ORCH_LFO_BRIGHTNESS=4U };
struct orch_lfo {
    uint16_t rate_millihz; /* free-running rate; ignored when sync_division != 0 */
    uint16_t delay_ms, fade_ms;
    uint8_t wave, destinations, depth, sync_division;
};

struct orch_adsr {
    uint16_t attack_ms, decay_ms, release_ms;
    uint8_t sustain_percent;
};

struct orch_part {
    struct orch_adaptive adaptive;
    struct orch_divisi_state divisi;
    struct orch_adsr adsr;
    struct orch_lfo lfo;
    uint16_t channels;     /* bit 0 = MIDI channel 1 */
    uint8_t family;
    uint8_t instrument;
    uint8_t enabled;
    uint8_t articulation;  /* ORCH_ART_AUTO or a pinned articulation */
    uint8_t player_cap;    /* divisi ceiling for this part */
};

void orch_parts_init(void);
struct orch_part *orch_part_get(unsigned part);
unsigned orch_part_for_channel(unsigned channel); /* 0xFF when unassigned */
unsigned orch_parts_enabled(void);

int orch_part_configure(unsigned part, unsigned family, uint16_t channels);
int orch_part_set_instrument(unsigned part, unsigned instrument);
int orch_part_set_articulation(unsigned part, unsigned articulation);
int orch_part_set_player_cap(unsigned part, unsigned cap);
int orch_part_set_adsr(unsigned part, unsigned attack_ms, unsigned decay_ms,
                       unsigned sustain_percent, unsigned release_ms);
int orch_part_set_lfo(unsigned part, unsigned wave, unsigned destinations,
                      unsigned depth, unsigned rate_millihz,
                      unsigned sync_division, unsigned delay_ms, unsigned fade_ms);

const char *orch_family_name(unsigned family);
unsigned orch_instrument_family(unsigned instrument);
void orch_instrument_adsr(unsigned instrument, struct orch_adsr *a); /* its defaults */
/* A family's instruments in score order (high to low, as a score lists
 * them): how many, the index-th, and where an instrument stands in its own */
unsigned orch_family_size(unsigned family);
unsigned orch_family_member(unsigned family, unsigned index);
unsigned orch_family_index(unsigned instrument);

#endif
