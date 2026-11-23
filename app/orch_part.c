/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "orch_part.h"

static struct orch_part parts[ORCH_PARTS];

static unsigned instrument_family(unsigned instrument)
{
    if (instrument <= ORCH_INST_HARP) return ORCH_FAM_STRINGS;
    if (instrument <= ORCH_INST_BASSOON) return ORCH_FAM_WOODWINDS;
    if (instrument <= ORCH_INST_TUBA) return ORCH_FAM_BRASS;
    if (instrument <= ORCH_INST_TIMPANI) return ORCH_FAM_MALLETS;
    return ORCH_FAM_CHOIR;
}

static unsigned default_instrument(unsigned family)
{
    switch (family) {
    case ORCH_FAM_STRINGS: return ORCH_INST_VIOLIN;
    case ORCH_FAM_WOODWINDS: return ORCH_INST_FLUTE;
    case ORCH_FAM_BRASS: return ORCH_INST_TRUMPET;
    case ORCH_FAM_MALLETS: return ORCH_INST_MARIMBA;
    case ORCH_FAM_CHOIR: return ORCH_INST_CHOIR;
    default: return ORCH_INST_VIOLIN;
    }
}

const char *orch_family_name(unsigned family)
{
    switch (family) {
    case ORCH_FAM_STRINGS: return "STRINGS";
    case ORCH_FAM_WOODWINDS: return "WOODWINDS";
    case ORCH_FAM_BRASS: return "BRASS";
    case ORCH_FAM_MALLETS: return "MALLETS";
    case ORCH_FAM_CHOIR: return "CHOIR";
    default: return "OFF";
    }
}

void orch_parts_init(void)
{
    static const uint16_t channel_map[ORCH_PARTS] = {
        0x000FU, 0x00F0U, 0x0F00U, 0xF000U
    };
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        struct orch_part *part = &parts[p];
        orch_adaptive_init(&part->adaptive);
        orch_divisi_init(&part->divisi);
        part->channels = channel_map[p];
        part->family = (uint8_t)p;
        part->instrument = (uint8_t)default_instrument(p);
        part->enabled = 1U;
        part->articulation = ORCH_ART_AUTO;
        part->player_cap = 2U;
        part->voice_ceiling = ORCH_PART_VOICE_CEIL;
    }
}

struct orch_part *orch_part_get(unsigned part)
{
    return (part < ORCH_PARTS) ? &parts[part] : 0;
}

unsigned orch_part_for_channel(unsigned channel)
{
    if (channel > 15U) return 0xFFU;
    const uint16_t bit = (uint16_t)(1U << channel);
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        if (parts[p].enabled && (parts[p].channels & bit)) return p;
    return 0xFFU;
}

unsigned orch_parts_enabled(void)
{
    unsigned n = 0U;
    for (unsigned p = 0; p < ORCH_PARTS; ++p) n += parts[p].enabled ? 1U : 0U;
    return n;
}

int orch_part_configure(unsigned part, unsigned family, uint16_t channels)
{
    if (part >= ORCH_PARTS || family >= ORCH_FAM_COUNT) return -1;
    parts[part].family = (uint8_t)family;
    parts[part].instrument = (uint8_t)default_instrument(family);
    parts[part].channels = channels;
    parts[part].enabled = channels ? 1U : 0U;
    orch_adaptive_reset_phrase(&parts[part].adaptive);
    orch_divisi_init(&parts[part].divisi);
    return 0;
}

int orch_part_set_instrument(unsigned part, unsigned instrument)
{
    if (part >= ORCH_PARTS || instrument >= ORCH_INST_COUNT) return -1;
    parts[part].instrument = (uint8_t)instrument;
    parts[part].family = (uint8_t)instrument_family(instrument);
    orch_adaptive_reset_phrase(&parts[part].adaptive);
    orch_divisi_init(&parts[part].divisi);
    return 0;
}

int orch_part_set_articulation(unsigned part, unsigned articulation)
{
    if (part >= ORCH_PARTS || articulation >= ORCH_ART_COUNT) return -1;
    parts[part].articulation = (uint8_t)articulation;
    return 0;
}

int orch_part_set_player_cap(unsigned part, unsigned cap)
{
    if (part >= ORCH_PARTS) return -1;
    if (cap < 1U) cap = 1U;
    if (cap > ORCH_DIVISI_MAX) cap = ORCH_DIVISI_MAX;
    parts[part].player_cap = (uint8_t)cap;
    return 0;
}
