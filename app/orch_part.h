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
    ORCH_FAM_COUNT
};

struct orch_part {
    struct orch_adaptive adaptive;
    struct orch_divisi_state divisi;
    uint16_t channels;     /* bit 0 = MIDI channel 1 */
    uint8_t family;
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
int orch_part_set_articulation(unsigned part, unsigned articulation);
int orch_part_set_player_cap(unsigned part, unsigned cap);

const char *orch_family_name(unsigned family);

#endif
