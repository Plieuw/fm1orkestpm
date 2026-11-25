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

static void instrument_adsr(unsigned instrument, struct orch_adsr *a)
{
    /* Instrument defaults in milliseconds and percent; scenes may override. */
    a->attack_ms = 32U; a->decay_ms = 180U;
    a->sustain_percent = 82U; a->release_ms = 420U;
    switch (instrument) {
    case ORCH_INST_VIOLA: a->attack_ms=42U; a->decay_ms=210U; a->release_ms=480U; break;
    case ORCH_INST_CELLO: a->attack_ms=58U; a->decay_ms=240U; a->sustain_percent=86U; a->release_ms=560U; break;
    case ORCH_INST_DOUBLE_BASS: a->attack_ms=72U; a->decay_ms=280U; a->sustain_percent=88U; a->release_ms=680U; break;
    case ORCH_INST_HARP: a->attack_ms=5U; a->decay_ms=110U; a->sustain_percent=62U; a->release_ms=900U; break;
    case ORCH_INST_FLUTE: a->attack_ms=38U; a->decay_ms=170U; a->sustain_percent=84U; a->release_ms=360U; break;
    case ORCH_INST_PICCOLO: a->attack_ms=18U; a->decay_ms=130U; a->sustain_percent=82U; a->release_ms=300U; break;
    case ORCH_INST_CLARINET: a->attack_ms=28U; a->decay_ms=190U; a->sustain_percent=86U; a->release_ms=420U; break;
    case ORCH_INST_OBOE: a->attack_ms=32U; a->decay_ms=210U; a->sustain_percent=84U; a->release_ms=440U; break;
    case ORCH_INST_BASSOON: a->attack_ms=62U; a->decay_ms=250U; a->sustain_percent=86U; a->release_ms=580U; break;
    case ORCH_INST_TRUMPET: a->attack_ms=12U; a->decay_ms=120U; a->sustain_percent=80U; a->release_ms=260U; break;
    case ORCH_INST_FRENCH_HORN: a->attack_ms=42U; a->decay_ms=200U; a->sustain_percent=86U; a->release_ms=460U; break;
    case ORCH_INST_TROMBONE: a->attack_ms=24U; a->decay_ms=160U; a->sustain_percent=84U; a->release_ms=360U; break;
    case ORCH_INST_TUBA: a->attack_ms=55U; a->decay_ms=230U; a->sustain_percent=88U; a->release_ms=520U; break;
    case ORCH_INST_MARIMBA: a->attack_ms=3U; a->decay_ms=90U; a->sustain_percent=48U; a->release_ms=720U; break;
    case ORCH_INST_XYLOPHONE: a->attack_ms=2U; a->decay_ms=70U; a->sustain_percent=42U; a->release_ms=520U; break;
    case ORCH_INST_TIMPANI: a->attack_ms=4U; a->decay_ms=240U; a->sustain_percent=58U; a->release_ms=1100U; break;
    case ORCH_INST_CHOIR: a->attack_ms=115U; a->decay_ms=280U; a->sustain_percent=90U; a->release_ms=760U; break;
    default: break;
    }
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

int orch_part_set_adsr(unsigned part, unsigned attack_ms, unsigned decay_ms,
                       unsigned sustain_percent, unsigned release_ms)
{
    if (part >= ORCH_PARTS || !attack_ms || attack_ms > 4000U ||
        !decay_ms || decay_ms > 4000U || sustain_percent > 100U ||
        !release_ms || release_ms > 6000U) return -1;
    parts[part].adsr.attack_ms = (uint16_t)attack_ms;
    parts[part].adsr.decay_ms = (uint16_t)decay_ms;
    parts[part].adsr.sustain_percent = (uint8_t)sustain_percent;
    parts[part].adsr.release_ms = (uint16_t)release_ms;
    return 0;
}

int orch_part_set_lfo(unsigned part, unsigned wave, unsigned destinations,
                      unsigned depth, unsigned rate_millihz,
                      unsigned sync_division, unsigned delay_ms, unsigned fade_ms)
{
    if (part >= ORCH_PARTS || wave > ORCH_LFO_SAMPLE_HOLD ||
        (destinations & ~(unsigned)(ORCH_LFO_PITCH | ORCH_LFO_TREMOLO |
                                    ORCH_LFO_BRIGHTNESS)) ||
        depth > 100U || rate_millihz < 100U || rate_millihz > 20000U ||
        sync_division > 4U || delay_ms > 5000U || fade_ms > 5000U)
        return -1;
    parts[part].lfo.wave = (uint8_t)wave;
    parts[part].lfo.destinations = (uint8_t)destinations;
    parts[part].lfo.depth = (uint8_t)depth;
    parts[part].lfo.rate_millihz = (uint16_t)rate_millihz;
    parts[part].lfo.sync_division = (uint8_t)sync_division;
    parts[part].lfo.delay_ms = (uint16_t)delay_ms;
    parts[part].lfo.fade_ms = (uint16_t)fade_ms;
    return 0;
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
        0x0001U, 0x0002U, 0x0004U, 0x0008U,
        0x0010U, 0x0020U, 0x0040U, 0x0080U
    };
    static const uint8_t family_map[ORCH_PARTS] = {
        ORCH_FAM_STRINGS, ORCH_FAM_STRINGS,
        ORCH_FAM_WOODWINDS, ORCH_FAM_WOODWINDS,
        ORCH_FAM_BRASS, ORCH_FAM_BRASS,
        ORCH_FAM_MALLETS, ORCH_FAM_CHOIR
    };
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        struct orch_part *part = &parts[p];
        orch_adaptive_init(&part->adaptive);
        orch_divisi_init(&part->divisi);
        part->channels = channel_map[p];
        part->family = family_map[p];
        part->instrument = (uint8_t)default_instrument(family_map[p]);
        orch_divisi_set_instrument(&part->divisi, part->instrument);
        part->enabled = 1U;
        part->articulation = ORCH_ART_AUTO;
        part->player_cap = 2U;
        instrument_adsr(part->instrument, &part->adsr);
        part->lfo.wave = ORCH_LFO_SINE;
        part->lfo.destinations = 0U;
        part->lfo.depth = 0U;
        part->lfo.rate_millihz = 5000U;
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
    instrument_adsr(parts[part].instrument, &parts[part].adsr);
    parts[part].channels = channels;
    parts[part].enabled = channels ? 1U : 0U;
    orch_adaptive_reset_phrase(&parts[part].adaptive);
    orch_divisi_init(&parts[part].divisi);
    orch_divisi_set_instrument(&parts[part].divisi, parts[part].instrument);
    return 0;
}

int orch_part_set_instrument(unsigned part, unsigned instrument)
{
    if (part >= ORCH_PARTS || instrument >= ORCH_INST_COUNT) return -1;
    parts[part].instrument = (uint8_t)instrument;
    parts[part].family = (uint8_t)instrument_family(instrument);
    instrument_adsr(instrument, &parts[part].adsr);
    orch_adaptive_reset_phrase(&parts[part].adaptive);
    orch_divisi_init(&parts[part].divisi);
    orch_divisi_set_instrument(&parts[part].divisi, parts[part].instrument);
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
