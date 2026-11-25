/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "orch_cc.h"
#include "orch_engine.h"
#include "orch_fx.h"
#include "orch_resonance.h"
#include "orch_performance.h"

#include "orch_cc_tables.inc"

int32_t orch_cc_gain_q15[ORCH_PARTS];
int32_t orch_cc_excite_q15[ORCH_PARTS];
int32_t orch_cc_bright_q8[ORCH_PARTS];
uint32_t orch_cc_bend_q16[ORCH_PARTS];
uint8_t orch_cc_mod[ORCH_PARTS];
int32_t orch_cc_pan_l_q15[ORCH_PARTS], orch_cc_pan_r_q15[ORCH_PARTS];

/* what each part was sent */
struct cc_part {
    uint8_t volume, expression, breath, modulation, pan, bright, soft;
    uint8_t bank, rpn_msb, rpn_lsb, range;   /* range: semitones */
    uint16_t bend;
};
static struct cc_part cc[ORCH_PARTS];
/* where the gliding values are going */
static int32_t gain_to[ORCH_PARTS], excite_to[ORCH_PARTS], bright_to[ORCH_PARTS];
static int32_t pan_l_to[ORCH_PARTS], pan_r_to[ORCH_PARTS];
static uint32_t bend_to[ORCH_PARTS];
static volatile uint8_t gliding;

/* 2^(-cents / 1200) in Q16, for -2400..+2400 cents: a longer loop is a lower
 * note, so a bend up shortens it */
static uint32_t bend_ratio_q16(int32_t cents)
{
    int32_t t = -cents, octaves = 0;
    while (t < 0) { t += 1200; --octaves; }
    while (t >= 1200) { t -= 1200; ++octaves; }
    const uint32_t pos = (uint32_t)t * 64U;
    const uint32_t i = pos / 1200U, fr = pos - i * 1200U;
    uint32_t r = EXP2_Q16[i] + ((EXP2_Q16[i + 1U] - EXP2_Q16[i]) * fr) / 1200U;
    return octaves >= 0 ? r << octaves : r >> -octaves;
}

static void retarget(unsigned p)
{
    const struct cc_part *c = &cc[p];
    /* volume x expression on the GM2/XG curve, x breath on an even 30 dB */
    uint32_t g = ((uint32_t)LAW_GM2_Q15[c->volume] * LAW_GM2_Q15[c->expression]) >> 15;
    g = (g * LAW30_Q15[c->breath]) >> 15;
    if (c->soft) g = (g * 24576U) >> 15;              /* soft pedal: -2.5 dB */
    gain_to[p] = (int32_t)g;
    /* breath is also how hard the string is bowed, the pipe blown: half at
     * 0, full at 127, so the timbre darkens as the level falls */
    excite_to[p] = 16384 + (int32_t)(((uint32_t)c->breath * 16383U) / 127U);
    /* CC74, as XG: 64 is the instrument; +-1.5 damping steps at the ends;
     * the soft pedal takes half a step off as well */
    bright_to[p] = ((int32_t)c->bright - 64) * 6 - (c->soft ? 128 : 0);
    /* CC10 as a balance on the part's own seat: 64 leaves it as it sits */
    pan_l_to[p] = c->pan <= 64U ? 32767 : (int32_t)((127U - c->pan) * 32767U / 63U);
    pan_r_to[p] = c->pan >= 64U ? 32767 : (int32_t)((uint32_t)c->pan * 32767U / 64U);
    /* pitch bend over the RPN 0 range */
    const int32_t cents = ((int32_t)c->bend - 8192) * (int32_t)c->range * 100 / 8192;
    bend_to[p] = bend_ratio_q16(cents);
    orch_cc_mod[p] = (uint8_t)(((uint32_t)c->modulation * 100U + 63U) / 127U);
    gliding = 1U;
}

/* the reset values of a Yamaha XG part, CC121 (volume, pan and the bank
 * are not reset; nor are the settings) */
void orch_cc_reset_part(unsigned p)
{
    if (p >= ORCH_PARTS) return;
    struct cc_part *c = &cc[p];
    c->expression = 127U;
    c->breath = 127U;
    c->modulation = 0U;
    c->bright = 64U;
    c->soft = 0U;
    c->bend = 8192U;
    c->rpn_msb = c->rpn_lsb = 127U;
    retarget(p);
}

void orch_cc_init(void)
{
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        cc[p].volume = 100U;      /* XG's power-on volume */
        cc[p].pan = 64U;
        cc[p].bank = 0U;
        cc[p].range = 2U;         /* XG's pitch bend range */
        orch_cc_reset_part(p);
        orch_cc_gain_q15[p] = gain_to[p];
        orch_cc_excite_q15[p] = excite_to[p];
        orch_cc_bright_q8[p] = bright_to[p];
        orch_cc_bend_q16[p] = bend_to[p];
        orch_cc_pan_l_q15[p] = pan_l_to[p];
        orch_cc_pan_r_q15[p] = pan_r_to[p];
    }
    gliding = 0U;
}

unsigned orch_cc_dynamic(unsigned part, unsigned velocity)
{
    if (velocity > 127U) velocity = 127U;
    if (!velocity || part >= ORCH_PARTS) return velocity;
    const struct cc_part *c = &cc[part];
    int v = (int)velocity + EXPR_STEPS[c->expression] + BREATH_STEPS[c->breath] -
            (c->soft ? 10 : 0);                      /* -2.5 dB */
    return v < 1 ? 1U : (unsigned)v;
}

uint16_t orch_cc_velocity_q12(unsigned velocity)
{
    return VEL_OUT_Q12[velocity & 127U];
}

/* 1/128 of the gap a sample, at least one step: about 3 ms */
static int glide(int32_t *now, int32_t to)
{
    const int32_t d = to - *now;
    if (!d) return 0;
    *now += d > 0 ? (d + 127) >> 7 : -((-d + 127) >> 7);
    return 1;
}

/* Stops when everything has arrived, so still controllers cost one test a
 * sample. */
void orch_cc_step(void)
{
    if (!gliding) return;
    int moving = 0;
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        moving |= glide(&orch_cc_gain_q15[p], gain_to[p]);
        moving |= glide(&orch_cc_excite_q15[p], excite_to[p]);
        moving |= glide(&orch_cc_pan_l_q15[p], pan_l_to[p]);
        moving |= glide(&orch_cc_pan_r_q15[p], pan_r_to[p]);
        {
            int32_t b = (int32_t)orch_cc_bend_q16[p];
            moving |= glide(&b, (int32_t)bend_to[p]);
            orch_cc_bend_q16[p] = (uint32_t)b;
        }
        const int32_t d = bright_to[p] - orch_cc_bright_q8[p];
        if (d) { orch_cc_bright_q8[p] += d > 0 ? 1 : -1; moving = 1; }
    }
    gliding = (uint8_t)moving;
}

unsigned orch_cc_value(unsigned part, unsigned n)
{
    if (part >= ORCH_PARTS) return 0U;
    const struct cc_part *c = &cc[part];
    switch (n) {
    case 1U: return c->modulation;
    case 2U: return c->breath;
    case 7U: return c->volume;
    case 10U: return c->pan;
    case 11U: return c->expression;
    case 67U: return c->soft ? 127U : 0U;
    case 74U: return c->bright;
    case ORCH_CC_BEND: return c->bend;
    default: return 0U;
    }
}

unsigned orch_cc_bend_range(unsigned part)
{
    return part < ORCH_PARTS ? cc[part].range : 0U;
}

/* ------------------------------------------------- the sound controllers */

/* XG-style relative value: 64 gives `mid`, 0 `lo`, 127 `hi`, straight
 * lines either side */
static unsigned around(unsigned value, unsigned lo, unsigned mid, unsigned hi)
{
    if (value <= 64U) return lo + ((mid - lo) * value + 32U) / 64U;
    return mid + ((hi - mid) * (value - 64U) + 31U) / 63U;
}

/* the same on a log scale: `table` is the 128-step log law of the whole
 * range; 64 gives `mid` exactly, the ends the table's ends */
static unsigned around_log(unsigned value, const uint16_t *table, unsigned mid)
{
    if (value == 64U) return mid;
    unsigned d = 0U;
    while (d < 127U && table[d] < mid) ++d;          /* mid's place */
    return table[around(value, 0U, d, 127U)];
}

static void set_adsr(unsigned p, int a, int d, int s, int r)
{
    const struct orch_part *part = orch_part_get(p);
    if (!part) return;
    orch_engine_set_adsr(p,
        a >= 0 ? (unsigned)a : part->adsr.attack_ms,
        d >= 0 ? (unsigned)d : part->adsr.decay_ms,
        s >= 0 ? (unsigned)s : part->adsr.sustain_percent,
        r >= 0 ? (unsigned)r : part->adsr.release_ms);
}

static void set_lfo(unsigned p, int depth, int rate, int delay)
{
    const struct orch_part *part = orch_part_get(p);
    if (!part) return;
    const struct orch_lfo *l = &part->lfo;
    unsigned dest = l->destinations;
    /* XG's CC76-78 are the vibrato's: a depth sent turns the pitch on */
    if (depth > 0) dest |= ORCH_LFO_PITCH;
    orch_engine_set_lfo(p, l->wave, dest,
        depth >= 0 ? (unsigned)depth : l->depth,
        rate >= 0 ? (unsigned)rate : l->rate_millihz,
        rate >= 0 ? 0U : l->sync_division,     /* a rate sent frees the sync */
        delay >= 0 ? (unsigned)delay : l->delay_ms, l->fade_ms);
}

/* an effect send: 0 switches the effect off, 1..127 on at 1..100 % */
static void set_send(unsigned slot, unsigned value)
{
    orch_fx_set_mix(slot, value ? 1U + (value - 1U) * 99U / 126U : 0U);
    orch_fx_set_enabled(slot, value ? 1U : 0U);
}

/* --------------------------------------------- the ARP and the sequencer */

/* value 0..127 into n equal zones, every value used */
static unsigned zone(unsigned value, unsigned n)
{
    const unsigned z = (value * n) >> 7;
    return z < n ? z : n - 1U;
}

/* The ARP's settings, per part, as the ARP page sets them (SAVE keeps
 * them). Returns 1 when the controller was one of them. */
static int arp_cc(unsigned p, unsigned n, unsigned value)
{
    struct orch_perf_config pc;
    if (orch_perf_get_config(p, &pc) != 0) return 0;
    const unsigned on = value >= 64U;
    switch (n) {
    case 80U: pc.arp_enabled = (uint8_t)on; break;          /* GP5 */
    case 81U: pc.latch = (uint8_t)on; break;                /* GP6: hold */
    case 82U: pc.phrase_enabled = (uint8_t)on; break;       /* GP7 */
    case 83U: pc.arp_order = (uint8_t)on; break;            /* GP8: as played */
    case 16U: pc.arp_type = (uint8_t)zone(value, ORCH_PERF_ARP_TYPES); break;
    case 17U: pc.division = (uint8_t)zone(value, ORCH_PERF_DIVISIONS); break;
    case 18U: pc.gate = (uint8_t)(5U + (value * 95U + 63U) / 127U); break;
    case 19U: pc.swing = (uint8_t)value; break;
    case 102U: pc.density = (uint8_t)value; break;
    case 103U: pc.accent = (uint8_t)value; break;
    case 104U: pc.range = (uint8_t)(1U + zone(value, 4U)); break;
    case 105U: pc.cluster = (uint8_t)(1U + zone(value, 4U)); break;
    case 106U: pc.ratchet = (uint8_t)(1U + zone(value, 4U)); break;
    case 107U: pc.strum = (uint8_t)((value * 40U + 63U) / 127U); break;
    case 108U: pc.velocity_groove = (uint8_t)zone(value, 8U); break;
    case 109U: pc.gate_groove = (uint8_t)zone(value, 8U); break;
    case 110U: pc.repeat = (uint8_t)zone(value, 4U); break;
    case 111U: pc.quantize = (uint8_t)zone(value, 18U); break;
    case 112U: pc.root = (uint8_t)zone(value, 13U); break;
    case 113U: pc.phrase_ge = (uint8_t)zone(value, ORCH_PERF_GE_COUNT); break;
    case 114U: pc.logic = (uint8_t)zone(value, ORCH_PERF_LOGIC_COUNT); break;
    case 115U: pc.arp_adaptive = (uint8_t)on; break;
    case 116U: pc.external_clock = (uint8_t)on; break;
    default: return 0;
    }
    orch_perf_set_config(p, &pc);
    return 1;
}

/* the tempo is the whole orchestra's: the sequencer, every ARP, the delay */
static void set_tempo(unsigned value)
{
    const unsigned bpm = 40U + (value * 210U + 63U) / 127U;   /* 40..250 */
    orch_seq_set_tempo((uint16_t)bpm);
    orch_fx_set_tempo(bpm);
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        struct orch_perf_config pc;
        if (orch_perf_get_config(p, &pc) == 0 && pc.bpm != bpm) {
            pc.bpm = (uint16_t)bpm;
            orch_perf_set_config(p, &pc);
        }
    }
}

/* the instruments of a family, in order */
static unsigned family_instrument(unsigned family, unsigned index, unsigned *count)
{
    unsigned n = 0U, found = ORCH_INST_COUNT;
    for (unsigned i = 0; i < ORCH_INST_COUNT; ++i) {
        if (orch_instrument_family(i) != family) continue;
        if (n == index) found = i;
        ++n;
    }
    if (count) *count = n;
    return found;
}

/* Program change with the bank select before it, as on XG: bank MSB 0
 * keeps the part's family, 1..5 choose strings, woodwinds, brass, mallets,
 * choir; the program picks the instrument in it, wrapping. */
static void program(unsigned p, unsigned value)
{
    const struct orch_part *part = orch_part_get(p);
    if (!part) return;
    unsigned family = part->family;
    if (cc[p].bank >= 1U && cc[p].bank <= ORCH_FAM_COUNT) family = cc[p].bank - 1U;
    unsigned n = 0U;
    (void)family_instrument(family, 0U, &n);
    if (!n) return;
    while (value >= n) value -= n;
    const unsigned inst = family_instrument(family, value, 0);
    if (inst < ORCH_INST_COUNT && inst != part->instrument)
        orch_engine_set_instrument(p, inst);
}

/* data entry on the selected RPN: 0 is the pitch bend range */
static void data_entry(unsigned p, unsigned msb)
{
    struct cc_part *c = &cc[p];
    if (c->rpn_msb == 0U && c->rpn_lsb == 0U) {
        c->range = (uint8_t)(msb > 24U ? 24U : msb);
        retarget(p);
    }
}

void orch_cc_apply(unsigned p, unsigned n, unsigned value)
{
    if (n == ORCH_CC_BEND) {
        if (p < ORCH_PARTS) {
            cc[p].bend = (uint16_t)(value > 16383U ? 16383U : value);
            retarget(p);
        }
        return;
    }
    if (value > 127U) value = 127U;
    /* the effects are the whole hall's: every channel's sends reach them */
    switch (n) {
    case 91U: set_send(ORCH_FX_REVERB, value); return;
    case 93U: set_send(ORCH_FX_CHORUS, value); return;
    case 94U: set_send(ORCH_FX_DELAY, value); return;     /* XG variation */
    case 95U: set_send(ORCH_FX_ENSEMBLE, value); return;
    case 71U: orch_resonance_set_amount(around(value, 0U, 25U, 100U)); return;
    /* the sequencer's transport is shared, so every channel reaches it */
    case 85U: orch_seq_set_enabled(value >= 64U); return;
    case 86U: orch_seq_set_length(1U + zone(value, 16U)); return;
    case 88U: orch_seq_set_adaptive(value >= 64U); return;
    case 89U: set_tempo(value); return;
    default: break;
    }
    if (p >= ORCH_PARTS) return;
    if (n == 87U) { orch_seq_set_lane_muted(p, value >= 64U); return; }
    if (arp_cc(p, n, value)) return;
    struct cc_part *c = &cc[p];
    const struct orch_part *part = orch_part_get(p);
    struct orch_adsr d;
    orch_instrument_adsr(part ? part->instrument : 0U, &d);
    switch (n) {
    /* playing controllers */
    case 1U: c->modulation = (uint8_t)value; retarget(p); break;
    case 2U: c->breath = (uint8_t)value; retarget(p); break;
    case 7U: c->volume = (uint8_t)value; retarget(p); break;
    case 10U: c->pan = (uint8_t)value; retarget(p); break;
    case 11U: c->expression = (uint8_t)value; retarget(p); break;
    case 67U: c->soft = value >= 64U; retarget(p); break;
    case 74U: c->bright = (uint8_t)value; retarget(p); break;
    case 121U: orch_cc_reset_part(p); break;
    /* bank, RPN, data entry */
    case 0U: c->bank = (uint8_t)value; break;
    case 101U: c->rpn_msb = (uint8_t)value; break;
    case 100U: c->rpn_lsb = (uint8_t)value; break;
    case 6U: data_entry(p, value); break;
    case 96U: if (c->rpn_msb == 0U && c->rpn_lsb == 0U) data_entry(p, c->range + 1U); break;
    case 97U: if (c->rpn_msb == 0U && c->rpn_lsb == 0U && c->range) data_entry(p, c->range - 1U); break;
    case ORCH_CC_PROGRAM: program(p, value); break;
    /* the sound controllers, relative to the instrument, as on XG */
    case 72U: set_adsr(p, -1, -1, -1, (int)around_log(value, RELEASE_MS, d.release_ms)); break;
    case 73U: set_adsr(p, (int)around_log(value, ATTACK_MS, d.attack_ms), -1, -1, -1); break;
    case 75U: set_adsr(p, -1, (int)around_log(value, ATTACK_MS, d.decay_ms), -1, -1); break;
    case 79U: set_adsr(p, -1, -1, (int)around(value, 0U, d.sustain_percent, 100U), -1); break;
    case 76U: set_lfo(p, -1, (int)around_log(value, RATE_MHZ, 5000U), -1); break;
    case 77U: set_lfo(p, (int)around(value, 0U, 0U, 100U), -1, -1); break;
    case 78U: set_lfo(p, -1, -1, (int)around_log(value, DELAY_MS, 0U)); break;
    /* the orchestra's own (no XG meaning): articulation and players */
    case 14U: {
        const unsigned z = (value * ORCH_ART_COUNT) >> 7;
        orch_engine_set_articulation(p, z < ORCH_ART_COUNT ? z : ORCH_ART_COUNT - 1U);
        break;
    }
    case 15U: orch_engine_set_player_cap(p, 1U + ((value * ORCH_DIVISI_MAX) >> 7)); break;
    default: break;
    }
}
