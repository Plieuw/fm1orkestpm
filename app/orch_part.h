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

#define ORCH_PARTS 4U
#define ORCH_PART_VOICE_CEIL 4U

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
    ORCH_INST_CHOIR, ORCH_INST_COUNT
};

struct orch_part {
    struct orch_adaptive adaptive;
    struct orch_divisi_state divisi;
    uint16_t channels;     /* bit 0 = MIDI channel 1 */
    uint8_t family;
    uint8_t instrument;
    uint8_t enabled;
    uint8_t articulation;  /* ORCH_ART_AUTO or a pinned articulation */
    uint8_t player_cap;    /* divisi ceiling for this part */
    uint8_t voice_ceiling; /* voices this part may hold */
};

void orch_parts_init(void);
struct orch_part *orch_part_get(unsigned part);
unsigned orch_part_for_channel(unsigned channel); /* 0xFF when unassigned */
unsigned orch_parts_enabled(void);

int orch_part_configure(unsigned part, unsigned family, uint16_t channels);
int orch_part_set_instrument(unsigned part, unsigned instrument);
int orch_part_set_articulation(unsigned part, unsigned articulation);
int orch_part_set_player_cap(unsigned part, unsigned cap);

const char *orch_family_name(unsigned family);

#endif
