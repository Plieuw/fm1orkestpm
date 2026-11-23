/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FM1 Orchestral Engine: polyphonic physical modeling.
 *
 * A MIDI note belongs to an instrument part (orch_part.c). The part's adaptive
 * interpreter (orch_adaptive.c, ported from Himalia's StrBrain) decides how
 * the note is played; its divisi planner (orch_divisi.c) decides how many
 * modeled players realize it. This file is the physical model they drive: one
 * digital waveguide per player, excited as a bowed string, a breath column, a
 * lip-driven bore or a struck bar, depending on the part's family.
 *
 * No samples, no wavetables: every voice is computed. Fixed point throughout,
 * no allocation, no locks, no libc in the render path.
 */
#include "fm1.h"
#include "orch_engine.h"

#define ORCH_AUDIO FM1_AUDIO_CODE

#define DELAY_SIZE 2048U
#define DELAY_MASK (DELAY_SIZE - 1U)
#define EVENT_CAP 64U
#define ENV_MAX 32767
#define Q23_MAX 8388607
#define Q23_MIN (-8388608)
#define PART_NOTES 8U
#define SEQ_CHANNEL 16U

enum orch_excite { EXC_BOW = 0, EXC_BREATH, EXC_LIP, EXC_STRIKE, EXC_VOICE };

typedef struct {
    int16_t delay[DELAY_SIZE];
    int16_t body[512];
    uint32_t write;
    uint16_t body_len, body_write;
    int32_t body_lp;
    uint32_t excite_phase, excite_inc;
    uint32_t len_q16, len_target_q16, len_step;
    uint32_t glide_left;
    uint32_t age;
    int32_t lp;      /* previous loop sample (the damping filter's state) */
    int32_t dc;      /* output DC blocker state */
    int32_t exc_dc;  /* running mean of the friction force */
    int32_t out;     /* last radiated sample */
    int32_t env, env_inc, env_dec;
    int32_t accent_q16;
    int32_t energy_q16;
    uint16_t vib_phase, vib_inc;
    uint32_t vib_depth, vib_depth_max, vib_ramp;
    uint32_t vib_delay;
    uint32_t stroke_left;
    uint32_t attack_left;
    uint16_t strike_left;
    uint16_t bow_gain;   /* Q8 force scale, proportional to the string length */
    int16_t pan_l, pan_r;
    uint8_t note, channel, part, family, instrument, artic, player, excite, damp;
    uint8_t velocity, gate, active, releasing, struck;
} voice_t;

typedef struct {
    uint8_t note;
    uint32_t id;
    uint8_t used;
} part_note_t;

typedef struct {
    part_note_t notes[PART_NOTES];
    struct orch_divisi_plan plan;
    uint32_t next_id;
    uint32_t last_len_q16;
    struct orch_intent intent;
} part_runtime_t;

typedef struct { uint8_t channel, note, velocity, type; } event_t;

static voice_t voices[ORCH_MAX_VOICES];
static event_t events[EVENT_CAP];
static part_runtime_t runtime[ORCH_PARTS];
static volatile uint8_t event_write, event_read;
static uint8_t sustain[ORCH_MIDI_CHANNELS];
static struct orch_step_note pattern[ORCH_SEQ_STEPS][ORCH_STEP_NOTES];
static uint8_t pattern_count[ORCH_SEQ_STEPS];
static uint8_t seq_note[ORCH_STEP_NOTES], seq_active[ORCH_STEP_NOTES];
static uint32_t seq_off_at[ORCH_STEP_NOTES];
static uint32_t age_counter, sample_counter, step_start;

/* ------------------------------------------------------------ arpeggiator
 *
 * A held key (or MIDI note) on a part with the arp on is collected instead of
 * played; the render loop then plays the pattern one note at a time. It lives
 * here, and not in the panel, because the note list and the clock must be
 * handled in the same context as the voices: the panel runs in the control
 * task, which can interrupt this code half way through a note. */
#define ARP_MAX_HELD 8U
#define ARP_NONE 0xFFU

typedef struct {
    uint8_t held[ARP_MAX_HELD];   /* in the order the keys were pressed */
    uint8_t vel[ARP_MAX_HELD];
    uint8_t order[ARP_MAX_HELD];  /* ascending pitch, for the sorted modes */
    uint8_t count, enabled, mode, octaves, div, gate;
    uint8_t index;
    uint8_t sounding;             /* the note it is playing, or ARP_NONE */
    uint8_t sounding_vel;
    uint32_t next_at, off_at;
    uint32_t rng;
} arp_t;

static arp_t arps[ORCH_PARTS];
static void arp_hold(unsigned part, unsigned note, unsigned velocity);
static void arp_unhold(unsigned part, unsigned note);
static void arp_reset_all(void);
static void arp_tick(void);
static void part_note_start(unsigned part, unsigned note, unsigned velocity);
static void part_note_stop(unsigned part, unsigned note);
static volatile uint16_t tempo = 120U;
static volatile uint8_t seq_enabled, seq_stop_pending, seq_part;
static uint8_t seq_started, current_step;
static uint32_t rng = 0x6d2b79f5U;

/* ---------------------------------------------------------------- helpers */

static int32_t clamp16(int32_t x)
{
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return x;
}
static int32_t noise16(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (int16_t)(rng >> 16);
}
/* Semitone ratio table from A, Q16. */
static const uint32_t RATIO_A[12] = {
    65536U, 69433U, 73562U, 77936U, 82570U, 87480U,
    92682U, 98209U, 104048U, 110245U, 116802U, 123743U
};

/* Loop length in Q16 samples for a MIDI note. */
static uint32_t note_len_q16(unsigned note)
{
    const unsigned d = (note + 3U) % 12U;
    const int shift = (int)((note + 3U) / 12U) - 6; /* A4 = note 69 */
    uint32_t f = (440U << 16);
    f = (uint32_t)(((uint64_t)f * RATIO_A[d]) >> 16);
    if (shift > 0) f <<= (unsigned)shift;
    else if (shift < 0) f >>= (unsigned)(-shift);
    if (f < 16U) f = 16U;
    uint64_t len = ((uint64_t)ORCH_SAMPLE_RATE << 32) / f;
    if (len < (16ULL << 16)) len = 16ULL << 16;
    if (len > ((uint64_t)(DELAY_SIZE - 8U) << 16)) len = (uint64_t)(DELAY_SIZE - 8U) << 16;
    return (uint32_t)len;
}

static unsigned family_excite(unsigned family)
{
    switch (family) {
    case ORCH_FAM_WOODWINDS: return EXC_BREATH;
    case ORCH_FAM_BRASS: return EXC_LIP;
    case ORCH_FAM_MALLETS: return EXC_STRIKE;
    case ORCH_FAM_CHOIR: return EXC_VOICE;
    case ORCH_FAM_STRINGS:
    default: return EXC_BOW;
    }
}

/* Articulation -> excitation, damping, attack, release, stroke, ring.
 * These are the FM1 equivalents of Himalia's STR_ARTIC table, in fixed point. */
struct artic_profile {
    uint8_t excite;      /* EXC_* unless overridden, 0xFF = family */
    uint8_t damp;        /* damping profile: 1 dark, 4 bright */
    uint8_t attack_ms;
    uint16_t release_ms;
    uint16_t stroke_ms;  /* 0 = as long as the gate */
    uint8_t accent;      /* extra strike/noise, Q8 */
    uint8_t semis;       /* pitch offset */
    uint8_t pluck;       /* excites once and rings */
};

static void artic_profile(unsigned artic, unsigned family, struct artic_profile *p)
{
    p->excite = 0xFFU;
    p->damp = 2U;
    p->attack_ms = 30U;
    p->release_ms = 180U;
    p->stroke_ms = 0U;
    p->accent = 0U;
    p->semis = 0U;
    p->pluck = 0U;
    switch (artic) {
    case ORCH_ART_PORTAMENTO: p->attack_ms = 45U; break;
    case ORCH_ART_LEGATO: p->attack_ms = 60U; break;
    case ORCH_ART_DETACHE: p->accent = 90U; break;
    case ORCH_ART_MARTELE:
        p->accent = 200U; p->attack_ms = 12U; p->release_ms = 220U; break;
    case ORCH_ART_STACCATO:
        p->stroke_ms = 120U; p->release_ms = 120U; p->accent = 120U; break;
    case ORCH_ART_SPICCATO:
        p->stroke_ms = 60U; p->release_ms = 260U; p->accent = 150U; p->damp = 3U; break;
    case ORCH_ART_PIZZICATO:
        p->excite = EXC_STRIKE; p->pluck = 1U; p->release_ms = 900U;
        p->accent = 210U; p->damp = 2U; break;
    case ORCH_ART_FLAGEOLET:
        p->semis = 12U; p->damp = 3U; p->attack_ms = 45U; p->release_ms = 400U; break;
    case ORCH_ART_TASTO:      /* over the fingerboard: darkest */
        p->damp = 1U; p->attack_ms = 55U; break;
    case ORCH_ART_PONTICELLO: /* at the bridge: brightest */
        p->damp = 4U; p->attack_ms = 20U; break;
    case ORCH_ART_COL_LEGNO:
        p->excite = EXC_STRIKE; p->pluck = 1U; p->release_ms = 140U;
        p->accent = 255U; p->damp = 2U; break;
    case ORCH_ART_AUTO:
    default: break;
    }
    /* the family shifts the default damping and release */
    if (family == ORCH_FAM_WOODWINDS) { p->damp = (uint8_t)(p->damp + 1U); p->release_ms = 90U; }
    else if (family == ORCH_FAM_BRASS) { p->release_ms = 130U; }
    else if (family == ORCH_FAM_MALLETS) { p->release_ms = (uint16_t)(p->pluck ? 700U : 500U); }
}

struct instrument_profile {
    uint16_t loop_gain, body_gain, body_hz;
    uint8_t damping, attack_ms;
    uint16_t release_ms;
    uint8_t body_mix, half_period, pluck, vibrato_cents;
    uint16_t bow_pressure;
    uint8_t air_noise;
};
static const struct instrument_profile INST_PROFILE[ORCH_INST_COUNT] = {
    {32630U,31600U,500U,2U,42U,260U,52U,0U,0U,7U,22000U,5U}, /* violin */
    {32610U,31650U,350U,2U,48U,320U,58U,0U,0U,6U,22500U,5U}, /* viola */
    {32590U,31700U,220U,3U,62U,460U,68U,0U,0U,5U,23000U,4U}, /* cello */
    {32550U,31750U,110U,3U,78U,620U,76U,0U,0U,4U,23500U,4U}, /* bass */
    {32400U,31000U,700U,2U,12U,1100U,64U,0U,1U,2U,0U,4U}, /* harp */
    {32000U,30900U,1000U,3U,24U,180U,34U,1U,0U,3U,0U,42U}, /* flute */
    {31900U,30700U,1800U,3U,18U,140U,28U,1U,0U,2U,0U,48U}, /* piccolo */
    {32100U,31100U,800U,3U,28U,220U,42U,1U,0U,3U,0U,34U}, /* clarinet */
    {32000U,31200U,1200U,3U,30U,220U,46U,1U,0U,4U,0U,38U}, /* oboe */
    {32200U,31300U,400U,3U,38U,300U,54U,1U,0U,2U,0U,32U}, /* bassoon */
    {31900U,30700U,1500U,3U,26U,220U,44U,1U,0U,4U,0U,28U}, /* trumpet */
    {32000U,31000U,700U,3U,36U,280U,52U,1U,0U,3U,0U,26U}, /* horn */
    {32100U,31100U,500U,3U,34U,260U,58U,1U,0U,3U,0U,24U}, /* trombone */
    {32200U,31300U,250U,3U,48U,340U,64U,1U,0U,2U,0U,22U}, /* tuba */
    {31300U,29200U,1200U,2U,3U,700U,84U,0U,1U,0U,0U,72U}, /* marimba */
    {31100U,29000U,1800U,2U,2U,520U,82U,0U,1U,0U,0U,78U}, /* xylophone */
    {31400U,29400U,150U,2U,4U,900U,88U,0U,1U,0U,0U,64U}, /* timpani */
    {31700U,31200U,500U,3U,90U,520U,62U,0U,0U,5U,0U,10U}  /* choir */
};

/* Helmholtz friction curve. While the bow and string surfaces stick they move
 * together; once the string slips faster than the rosin can hold it, the
 * coefficient falls. That falling mu(|v_rel|) is what makes a bowed string
 * self-oscillate and what limits its amplitude: the loop is driven where the
 * slope is steep and released where the curve flattens.
 *
 * mu(v) = mu_s / (1 + |v|/v0), tabulated in Q15 over 0..15*2048 and linearly
 * interpolated. A nearest-entry table would add audible distortion because the
 * force is a nonlinear function of the string's own velocity.
 */
static const uint16_t FRIC_Q15[16] = {
    32767U, 19460U, 13860U, 10748U, 8782U, 7438U, 6423U, 5669U,
     5079U,  4587U,  4194U,  3866U, 3572U, 3343U, 3136U, 2949U
};

static int32_t friction_q15(int32_t v)
{
    int32_t a = v < 0 ? -v : v;
    if (a > 32767) a = 32767;
    const unsigned idx = (unsigned)(a >> 11);
    const int32_t frac = a & 2047;
    const int32_t lo = (int32_t)FRIC_Q15[idx];
    const int32_t hi = (int32_t)FRIC_Q15[idx < 15U ? idx + 1U : 15U];
    return lo + (((hi - lo) * frac) >> 11);
}

static int32_t triangle32(uint32_t phase)
{
    const uint32_t x = phase >> 16;
    return x < 32768U ? (int32_t)(x * 2U) - 32768
                      : 98303 - (int32_t)(x * 2U);
}

/* ------------------------------------------------------------ part notes */

static void part_notes_reset(void)
{
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        for (unsigned i = 0; i < PART_NOTES; ++i) {
            runtime[p].notes[i].used = 0U;
            runtime[p].notes[i].note = 0U;
            runtime[p].notes[i].id = 0U;
        }
        runtime[p].next_id = 1U;
        runtime[p].last_len_q16 = 0U;
        orch_divisi_init(&orch_part_get(p)->divisi);
    }
}

static int part_note_add(unsigned part, unsigned note, uint32_t *out_id)
{
    part_runtime_t *rt = &runtime[part];
    for (unsigned i = 0; i < PART_NOTES; ++i) {
        if (rt->notes[i].used && rt->notes[i].note == note) {
            rt->notes[i].id = rt->next_id++;
            *out_id = rt->notes[i].id;
            return (int)i;
        }
    }
    for (unsigned i = 0; i < PART_NOTES; ++i) {
        if (!rt->notes[i].used) {
            rt->notes[i].used = 1U;
            rt->notes[i].note = (uint8_t)note;
            rt->notes[i].id = rt->next_id++;
            *out_id = rt->notes[i].id;
            return (int)i;
        }
    }
    return -1;
}

static void part_note_remove(unsigned part, unsigned note)
{
    part_runtime_t *rt = &runtime[part];
    for (unsigned i = 0; i < PART_NOTES; ++i)
        if (rt->notes[i].used && rt->notes[i].note == note) rt->notes[i].used = 0U;
}

static unsigned part_note_count(unsigned part)
{
    unsigned n = 0U;
    for (unsigned i = 0; i < PART_NOTES; ++i) n += runtime[part].notes[i].used ? 1U : 0U;
    return n;
}

/* --------------------------------------------------------------- voices */

static void voice_stop(unsigned i)
{
    voices[i].active = 0U;
    voices[i].gate = 0U;
    voices[i].releasing = 0U;
    voices[i].env = 0;
}

static unsigned pick_voice(unsigned part)
{
    const struct orch_part *cfg = orch_part_get(part);
    unsigned owned = 0U;
    int quietest = -1, oldest = -1;
    int32_t quiet_env = 0x7fffffff;
    uint32_t oldest_age = UINT32_MAX;

    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i)
        if (voices[i].active && voices[i].part == part) ++owned;

    /* the part's ceiling applies first: prefer freeing one of its own */
    if (cfg && owned >= cfg->voice_ceiling) {
        for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
            voice_t *v = &voices[i];
            if (!v->active || v->part != part) continue;
            if (v->releasing && v->env < quiet_env) { quiet_env = v->env; quietest = (int)i; }
            if (!v->releasing && v->age < oldest_age) { oldest_age = v->age; oldest = (int)i; }
        }
        if (quietest >= 0) return (unsigned)quietest;
        if (oldest >= 0) return (unsigned)oldest;
    }

    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        if (!voices[i].active) return i;
    }
    /* the pool is full: releasing first, otherwise the oldest note */
    quietest = -1; oldest = -1; quiet_env = 0x7fffffff; oldest_age = UINT32_MAX;
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        voice_t *v = &voices[i];
        if (v->releasing && v->env < quiet_env) { quiet_env = v->env; quietest = (int)i; }
        if (!v->releasing && v->age < oldest_age) { oldest_age = v->age; oldest = (int)i; }
    }
    if (quietest >= 0) return (unsigned)quietest;
    return (oldest >= 0) ? (unsigned)oldest : 0U;
}

static void voice_start(unsigned part, unsigned note, unsigned velocity,
                        const struct orch_intent *in, unsigned player,
                        int8_t player_detune_cents, uint8_t player_pan)
{
    const struct orch_part *cfg = orch_part_get(part);
    const unsigned family = cfg ? cfg->family : ORCH_FAM_STRINGS;
    const unsigned instrument = cfg ? cfg->instrument : ORCH_INST_VIOLIN;
    const struct instrument_profile *ip = &INST_PROFILE[instrument < ORCH_INST_COUNT ? instrument : 0U];
    const unsigned i = pick_voice(part);
    voice_t *v = &voices[i];
    struct artic_profile prof;

    artic_profile(in->articulation, family, &prof);
    if (in->articulation == ORCH_ART_AUTO) {
        prof.damp = ip->damping;
        prof.attack_ms = ip->attack_ms;
        prof.release_ms = ip->release_ms;
        if (ip->pluck) { prof.pluck = 1U; prof.excite = EXC_STRIKE; }
    } else {
        const int adjusted = (int)prof.damp + (int)ip->damping - 2;
        prof.damp = (uint8_t)(adjusted < 1 ? 1 : (adjusted > 6 ? 6 : adjusted));
        prof.release_ms = (uint16_t)(((uint32_t)prof.release_ms * ip->release_ms) / 180U);
    }
    const unsigned family_exc = family_excite(family);
    const unsigned excite = (prof.excite != 0xFFU) ? prof.excite : family_exc;

    for (unsigned n = 0; n < DELAY_SIZE; ++n) v->delay[n] = 0;
    v->write = 0U;
    v->lp = 0;
    v->dc = 0;
    v->exc_dc = 0;
    v->age = ++age_counter;
    v->note = (uint8_t)note;
    v->channel = (uint8_t)(part < ORCH_PARTS ? part : 0U); /* channel identity is kept by the note */
    v->part = (uint8_t)part;
    v->family = (uint8_t)family;
    v->instrument = (uint8_t)instrument;
    v->artic = in->articulation;
    v->player = (uint8_t)player;
    v->excite = (uint8_t)excite;
    v->damp = prof.damp > 6U ? 6U : prof.damp;
    v->velocity = (uint8_t)velocity;
    v->active = 1U;
    v->gate = (prof.pluck || excite == EXC_STRIKE) ? 0U : 1U;
    v->releasing = 0U;
    v->struck = 0U;
    v->strike_left = 0U;

    /* piano key -> string/bore length; flageolet sounds an octave higher */
    unsigned sounding = note;
    if (prof.semis) {
        const int s = (int)sounding + prof.semis;
        sounding = (unsigned)(s > 127 ? 127 : s);
    }
    if (sounding > 127U) sounding = 127U;
    uint32_t target = note_len_q16(sounding);
    if (ip->half_period) target >>= 1;
    if (target < (8U << 16)) target = 8U << 16;

    uint32_t start = target;
    v->glide_left = 0U;
    if (in->connected && in->glide_ms && runtime[part].last_len_q16) {
        start = runtime[part].last_len_q16;
        v->glide_left = ((uint32_t)in->glide_ms * ORCH_SAMPLE_RATE) / 1000U;
    }
    v->len_q16 = start;
    v->len_target_q16 = target;
    if (v->glide_left) {
        v->len_step = (start > target ? (start - target) : (target - start)) / v->glide_left;
        if (!v->len_step) v->len_step = 1U;
        if (start > target) v->len_step = (uint32_t)(0U - v->len_step);
    } else {
        v->len_step = 0U;
    }
    runtime[part].last_len_q16 = target;

    /* section place: a few cents, per player slot (positive = sharper, so a
     * shorter loop) */
    if (player_detune_cents) {
        const int32_t shift = (int32_t)player_detune_cents;
        const int32_t d1 = (int32_t)(v->len_q16 / 1200) * shift;
        const int32_t d2 = (int32_t)(v->len_target_q16 / 1200) * shift;
        const int32_t a1 = (int32_t)v->len_q16 - d1;
        const int32_t a2 = (int32_t)v->len_target_q16 - d2;
        v->len_q16 = (uint32_t)(a1 > 16 ? a1 : 16);
        v->len_target_q16 = (uint32_t)(a2 > 16 ? a2 : 16);
    }
    {
        /* linear pan as Q8 gains that sum to 256, so a centred voice keeps
         * unity: left = 256 - pan, right = pan (pan 0..255) */
        const int32_t pan = player_pan & 0xFF;
        v->pan_l = (int16_t)(256 - pan);
        v->pan_r = (int16_t)pan;
    }

    /* envelopes: attack from the intent and the articulation */
    uint32_t atk_ms = (prof.attack_ms * in->attack_mul) >> 16;
    if (atk_ms < 2U) atk_ms = 2U;
    if (atk_ms > 400U) atk_ms = 400U;
    v->env = 0;
    {
        const uint32_t frames = (atk_ms * ORCH_SAMPLE_RATE) / 1000U;
        v->env_inc = (int32_t)(ENV_MAX / (frames ? frames : 1U));
        if (v->env_inc < 1) v->env_inc = 1;
        v->attack_left = frames ? frames : 1U;
        uint32_t rel_ms = ((uint32_t)prof.release_ms * in->release_mul) >> 16;
        if (rel_ms < 30U) rel_ms = 30U;
        if (rel_ms > 3000U) rel_ms = 3000U;
        const uint32_t rel_frames = (rel_ms * ORCH_SAMPLE_RATE) / 1000U;
        v->env_dec = (int32_t)(ENV_MAX / (rel_frames ? rel_frames : 1U));
        if (v->env_dec < 1) v->env_dec = 1;
    }

    v->energy_q16 = (int32_t)in->energy;
    v->accent_q16 = (int32_t)in->accent;

    /* vibrato: a rate per player, depth from the intent, delayed onset */
    {
        static const uint16_t RATE[ORCH_DIVISI_MAX] = { 8U, 7U, 9U, 8U };
        v->vib_inc = RATE[player % ORCH_DIVISI_MAX];
        v->vib_phase = (uint16_t)(rng & 0xFFFFU);
        const uint32_t cents = (uint32_t)(((uint64_t)ip->vibrato_cents * in->vib_intent) >> 16);
        v->vib_depth_max = (uint32_t)(((uint64_t)v->len_target_q16 * cents * 38U) >> 16);
        v->vib_depth = 0U;
        v->vib_ramp = v->vib_depth_max / ((350U * ORCH_SAMPLE_RATE) / 1000U);
        if (!v->vib_ramp && v->vib_depth_max) v->vib_ramp = 1U;
        v->vib_delay = (uint32_t)((in->phrase_start ? 180U : 120U) * ORCH_SAMPLE_RATE / 1000U);
    }

    /* A short, light string has a low mechanical impedance, so the same bow
     * force drives a high note harder than a low one and breaks the motion.
     * Scale the force with the loop length; a 400-sample string (A2) is the
     * reference. */
    {
        uint32_t ref = target >> 16;
        if (ref < 16U) ref = 16U;
        uint32_t g = (ref * 256U) / 400U;
        if (g < 64U) g = 64U;
        if (g > 2048U) g = 2048U;
        v->bow_gain = (uint16_t)g;
    }

    v->body_len = (uint16_t)(ORCH_SAMPLE_RATE / (ip->body_hz ? ip->body_hz : 440U));
    if (v->body_len < 8U) v->body_len = 8U;
    if (v->body_len > 512U) v->body_len = 512U;
    v->body_write = 0U; v->body_lp = 0;
    for (unsigned n = 0; n < v->body_len; ++n) v->body[n] = 0;
    v->excite_phase = rng ^ (note * 0x9e3779b9U);
    v->excite_inc = ip->half_period ? (uint32_t)(0x7fffffffU / (uint32_t)(target >> 16))
                                    : (uint32_t)(0xffffffffU / (uint32_t)(target >> 16));

    /* a stroke ends by itself; a pluck rings with the gate already closed */
    v->stroke_left = prof.stroke_ms
                         ? (uint32_t)prof.stroke_ms * ORCH_SAMPLE_RATE / 1000U
                         : 0U;

    /* the bow bite: a few milliseconds of rosin to set the string in motion,
     * after which the friction holds it. Measured without it, a bowed note
     * swells for nearly a second before it reaches its level. */
    if (excite == EXC_BOW) {
        v->strike_left = (uint16_t)((10U * ORCH_SAMPLE_RATE) / 1000U);
        const int32_t bite = (int32_t)velocity * 90;
        for (unsigned n = 0; n < 24U && n < DELAY_SIZE; ++n)
            v->delay[n] = (int16_t)clamp16((noise16() * bite) >> 8);
    }

    /* the strike: a short noise burst through the bridge */
    if (excite == EXC_STRIKE || prof.pluck) {
        v->struck = 1U;
        v->strike_left = (uint16_t)(((family == ORCH_FAM_MALLETS) ? 260U : 180U) *
                                    ORCH_SAMPLE_RATE / 1000U);
        const int32_t amp = (int32_t)velocity * 160 + (int32_t)prof.accent * 60;
        for (unsigned n = 0; n < 64U && n < DELAY_SIZE; ++n)
            v->delay[n] = (int16_t)clamp16((noise16() * amp) >> 8);
        if (v->strike_left > 512U) v->strike_left = 512U;
    }
    (void)v->channel;
}

/* ---------------------------------------------------------------- events */

static void enqueue(unsigned ch, unsigned note, unsigned velocity, unsigned type)
{
    const uint8_t next = (uint8_t)((event_write + 1U) & (EVENT_CAP - 1U));
    if (next == event_read) return;
    events[event_write].channel = (uint8_t)(ch & 15U);
    events[event_write].note = (uint8_t)(note & 127U);
    events[event_write].velocity = (uint8_t)(velocity & 127U);
    events[event_write].type = (uint8_t)type;
    event_write = next;
}

void orch_note_event(unsigned ch, unsigned note, unsigned velocity)
{
    if (ch < 16U && note < 128U && velocity < 128U) enqueue(ch, note, velocity, 0U);
}
void orch_panic(void) { enqueue(0U, 0U, 0U, 2U); }

void orch_midi_short_message(const uint8_t *m, unsigned bytes)
{
    if (!m || bytes < 2U || m[0] < 0x80U || m[0] >= 0xf0U) return;
    const unsigned kind = m[0] & 0xf0U, ch = m[0] & 15U;
    if ((kind == 0x80U || kind == 0x90U) && bytes >= 3U)
        enqueue(ch, m[1], (kind == 0x80U || m[2] == 0U) ? 0U : m[2], 0U);
    else if (kind == 0xb0U && bytes >= 3U && m[1] == 64U)
        enqueue(ch, 0U, m[2] >= 64U, 1U);
}

/* The note-on path: interpret, plan divisi, realize the players. */
static void part_note_start(unsigned part, unsigned note, unsigned velocity)
{
    if (!velocity) return;
    struct orch_part *cfg = orch_part_get(part);
    if (!cfg) return;

    part_runtime_t *rt = &runtime[part];
    struct orch_intent *in = &rt->intent;

    orch_adaptive_note(&cfg->adaptive, in, note, velocity, sample_counter,
                       cfg->articulation);
    orch_adaptive_velocity(&cfg->adaptive, in, velocity);

    uint32_t id = 0U;
    const int slot = part_note_add(part, note, &id);
    if (slot < 0) return;

    /* plan over every sounding note of this part */
    uint8_t notes[PART_NOTES];
    uint32_t ids[PART_NOTES];
    unsigned count = 0U;
    for (unsigned i = 0; i < PART_NOTES; ++i) {
        if (!rt->notes[i].used) continue;
        notes[count] = rt->notes[i].note;
        ids[count] = rt->notes[i].id;
        ++count;
    }
    orch_divisi_plan(&cfg->divisi, notes, ids, count, in->density,
                     cfg->player_cap, &rt->plan);

    /* realize only the players that took this new note */
    for (unsigned p = 0; p < rt->plan.players; ++p) {
        const unsigned idx = rt->plan.assignment[p];
        if (idx >= count || ids[idx] != id) continue;
        voice_start(part, note, velocity, in, p,
                    rt->plan.detune_cents[p], rt->plan.pan[p]);
    }
}

static void engine_note_on(unsigned ch, unsigned note, unsigned velocity)
{
    const unsigned part = orch_part_for_channel(ch);
    if (part == 0xFFU || !velocity) return;
    if (arps[part].enabled) {
        arp_hold(part, note, velocity);
        return;
    }
    part_note_start(part, note, velocity);
}

static void part_note_stop(unsigned part, unsigned note)
{
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        voice_t *v = &voices[i];
        if (!v->active || v->part != part || v->note != note) continue;
        v->gate = 0U;
        v->releasing = 1U;
    }
    part_note_remove(part, note);
}

static void engine_note_off(unsigned ch, unsigned note, unsigned sustain_on)
{
    const unsigned part = orch_part_for_channel(ch);
    if (part == 0xFFU) return;
    if (arps[part].enabled) {
        /* the key is let go but the pattern keeps running on the others */
        arp_unhold(part, note);
        return;
    }
    struct orch_part *cfg = orch_part_get(part);
    if (cfg) orch_adaptive_off(&cfg->adaptive, sample_counter);

    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        voice_t *v = &voices[i];
        if (!v->active || v->part != part || v->note != note) continue;
        v->gate = 0U;
        if (!sustain_on) v->releasing = 1U;
    }
    if (!sustain_on) part_note_remove(part, note);
}

static void engine_sustain(unsigned ch, unsigned on)
{
    if (ch >= ORCH_MIDI_CHANNELS) return;
    sustain[ch] = (uint8_t)(on ? 1U : 0U);
    if (on) return;
    const unsigned part = orch_part_for_channel(ch);
    if (part == 0xFFU) return;
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        voice_t *v = &voices[i];
        if (v->active && v->part == part && !v->gate) v->releasing = 1U;
    }
}

/* ------------------------------------------------------- arpeggiator impl */

static const uint8_t ARP_STEPS_PER_QUARTER[5] = { 2U, 3U, 4U, 6U, 8U };

static void arp_clear(arp_t *a)
{
    a->count = 0U;
    a->index = 0U;
    a->sounding = ARP_NONE;
}

static void arp_rebuild_order(arp_t *a)
{
    for (unsigned i = 0; i < a->count; ++i) a->order[i] = (uint8_t)i;
    for (unsigned i = 1; i < a->count; ++i) {
        const uint8_t key = a->order[i];
        unsigned j = i;
        while (j && a->held[a->order[j - 1U]] > a->held[key]) {
            a->order[j] = a->order[j - 1U];
            --j;
        }
        a->order[j] = key;
    }
}

static void arp_hold(unsigned part, unsigned note, unsigned velocity)
{
    arp_t *a = &arps[part];
    if (a->count >= ARP_MAX_HELD) return;
    for (unsigned i = 0; i < a->count; ++i)
        if (a->held[i] == note) { a->vel[i] = (uint8_t)velocity; return; }
    a->held[a->count] = (uint8_t)note;
    a->vel[a->count] = (uint8_t)velocity;
    ++a->count;
    arp_rebuild_order(a);
    if (a->sounding == ARP_NONE) {
        a->index = 0U;
        a->next_at = sample_counter;   /* the first key sounds at once */
    }
}

static void arp_unhold(unsigned part, unsigned note)
{
    arp_t *a = &arps[part];
    unsigned i = 0U;
    for (; i < a->count; ++i)
        if (a->held[i] == note) break;
    if (i >= a->count) return;
    for (unsigned j = i; j + 1U < a->count; ++j) {
        a->held[j] = a->held[j + 1U];
        a->vel[j] = a->vel[j + 1U];
    }
    --a->count;
    arp_rebuild_order(a);
    if (!a->count) {
        if (a->sounding != ARP_NONE) part_note_stop(part, a->sounding);
        arp_clear(a);
        return;
    }
    if (a->index >= a->count) a->index = 0U;
}

static void arp_reset_all(void)
{
    for (unsigned part = 0; part < ORCH_PARTS; ++part) {
        if (arps[part].sounding != ARP_NONE)
            part_note_stop(part, arps[part].sounding);
        arp_clear(&arps[part]);
    }
}

/* The pattern: without a sorted order the notes play as they were pressed. */
static unsigned arp_pick(const arp_t *a, unsigned step)
{
    const unsigned total = (unsigned)a->count * a->octaves;
    if (!total) return 0U;
    unsigned idx;
    switch (a->mode) {
    case ORCH_ARP_DOWN:
        idx = total - 1U - (step % total);
        break;
    case ORCH_ARP_UPDOWN: {
        const unsigned period = total > 1U ? total * 2U - 2U : 1U;
        const unsigned k = step % period;
        idx = k < total ? k : period - k;
        break;
    }
    case ORCH_ARP_RANDOM:
        idx = (unsigned)((step * 2654435761U) >> 7) % total; /* deterministic */
        break;
    default:
        idx = step % total;
        break;
    }
    const unsigned octave = idx / a->count;
    const unsigned pos = idx % a->count;
    const unsigned base = (a->mode == ORCH_ARP_AS_PLAYED)
                              ? a->held[pos]
                              : a->held[a->order[pos]];
    unsigned note = base + 12U * octave;
    if (note > 127U) note -= 12U;
    return note;
}

static unsigned arp_velocity(const arp_t *a, unsigned step)
{
    const unsigned total = (unsigned)a->count * a->octaves;
    if (!total) return 0U;
    const unsigned pos = (step % total) % a->count;
    const unsigned idx = (a->mode == ORCH_ARP_AS_PLAYED) ? pos : a->order[pos];
    return a->vel[idx];
}

static void arp_tick(void)
{
    for (unsigned part = 0; part < ORCH_PARTS; ++part) {
        arp_t *a = &arps[part];
        if (!a->enabled || !a->count) continue;

        if (a->sounding != ARP_NONE &&
            (int32_t)(sample_counter - a->off_at) >= 0) {
            part_note_stop(part, a->sounding);
            a->sounding = ARP_NONE;
        }

        unsigned rate = ARP_STEPS_PER_QUARTER[a->div < 5U ? a->div : 2U];
        uint32_t step_samples = (uint32_t)((ORCH_SAMPLE_RATE * 60U) /
                                           ((uint32_t)tempo * rate));
        if (step_samples < 16U) step_samples = 16U;

        if ((int32_t)(sample_counter - a->next_at) >= 0) {
            const unsigned note = arp_pick(a, a->index);
            const unsigned vel = arp_velocity(a, a->index);
            const unsigned gate = a->gate < 5U ? 5U : (a->gate > 100U ? 100U : a->gate);
            const unsigned total = (unsigned)a->count * a->octaves;
            a->index = (uint8_t)((a->index + 1U) % (total ? total : 1U));
            if (a->sounding != ARP_NONE) part_note_stop(part, a->sounding);
            part_note_start(part, note, vel);
            a->sounding = (uint8_t)note;
            a->sounding_vel = (uint8_t)vel;
            a->off_at = sample_counter + (step_samples * gate) / 100U;
            a->next_at += step_samples;
            if ((int32_t)(sample_counter - a->next_at) > 0) a->next_at = sample_counter;
        }
    }
}

static void drain_events(void)
{
    while (event_read != event_write) {
        const event_t e = events[event_read];
        event_read = (uint8_t)((event_read + 1U) & (EVENT_CAP - 1U));
        if (e.type == 2U) {
            for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) voice_stop(i);
            for (unsigned c = 0; c < ORCH_MIDI_CHANNELS; ++c) sustain[c] = 0U;
            arp_reset_all();
            part_notes_reset();
        } else if (e.type == 1U) {
            engine_sustain(e.channel, e.velocity);
        } else if (e.velocity) {
            engine_note_on(e.channel, e.note, e.velocity);
        } else {
            engine_note_off(e.channel, e.note, e.channel < ORCH_MIDI_CHANNELS &&
                                                    sustain[e.channel]);
        }
    }
}

/* ------------------------------------------------------------- sequencer */

void orch_seq_clear(void)
{
    for (unsigned s = 0; s < ORCH_SEQ_STEPS; ++s) {
        pattern_count[s] = 0U;
        for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) {
            pattern[s][n].note = 60U;
            pattern[s][n].velocity = 0U;
            pattern[s][n].gate_percent = 80U;
        }
    }
}

void orch_seq_set_part(unsigned part)
{
    seq_part = (uint8_t)(part < ORCH_PARTS ? part : 0U);
}

void orch_init(void)
{
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) voice_stop(i);
    event_read = event_write = 0U;
    for (unsigned c = 0; c < ORCH_MIDI_CHANNELS; ++c) sustain[c] = 0U;
    orch_parts_init();
    part_notes_reset();
    orch_seq_clear();
    tempo = 120U;
    seq_enabled = seq_started = seq_stop_pending = 0U;
    seq_part = 0U;
    current_step = 0U;
    sample_counter = step_start = 0U;
    for (unsigned part = 0; part < ORCH_PARTS; ++part) {
        arp_clear(&arps[part]);
        arps[part].enabled = 0U;
        arps[part].mode = ORCH_ARP_UP;
        arps[part].octaves = 1U;
        arps[part].div = 2U;       /* 1/16 */
        arps[part].gate = 60U;
        arps[part].rng = 1U;
    }
    for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) {
        seq_active[n] = 0U; seq_off_at[n] = 0U; seq_note[n] = 0U;
    }
}

void orch_seq_set_tempo(uint16_t bpm)
{
    if (bpm < 30U) bpm = 30U;
    if (bpm > 300U) bpm = 300U;
    tempo = bpm;
}

void orch_seq_set_enabled(unsigned enabled)
{
    if (seq_enabled && !enabled) seq_stop_pending = 1U;
    seq_enabled = enabled ? 1U : 0U;
    seq_started = 0U;
}

int orch_seq_set_step(unsigned step, unsigned slot, const struct orch_step_note *n)
{
    if (step >= ORCH_SEQ_STEPS || slot >= ORCH_STEP_NOTES || !n ||
        n->note > 127U || n->velocity > 127U || n->gate_percent > 100U) return -1;
    pattern[step][slot] = *n;
    if (n->velocity && pattern_count[step] <= slot) pattern_count[step] = (uint8_t)(slot + 1U);
    else if (!n->velocity && pattern_count[step] == slot + 1U)
        while (pattern_count[step] && !pattern[step][pattern_count[step] - 1U].velocity)
            --pattern_count[step];
    return 0;
}

/* Sequencer notes enter the same part path as MIDI: the reserved source is
 * SEQ_CHANNEL, mapped to the chosen part by seq_part. */
static void seq_release_voice(unsigned note)
{
    const unsigned part = seq_part;
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        voice_t *v = &voices[i];
        if (v->active && v->part == part && v->note == note && v->gate) {
            v->gate = 0U;
            v->releasing = 1U;
        }
    }
    part_note_remove(part, note);
}

static void seq_start_voice(unsigned note, unsigned velocity)
{
    const unsigned part = seq_part;
    struct orch_part *cfg = orch_part_get(part);
    if (!cfg) return;
    struct orch_intent *in = &runtime[part].intent;
    orch_adaptive_note(&cfg->adaptive, in, note, velocity, sample_counter,
                       cfg->articulation);
    orch_adaptive_velocity(&cfg->adaptive, in, velocity);
    uint32_t id = 0U;
    if (part_note_add(part, note, &id) < 0) return;

    uint8_t notes[PART_NOTES];
    uint32_t ids[PART_NOTES];
    unsigned count = 0U;
    for (unsigned i = 0; i < PART_NOTES; ++i) {
        if (!runtime[part].notes[i].used) continue;
        notes[count] = runtime[part].notes[i].note;
        ids[count] = runtime[part].notes[i].id;
        ++count;
    }
    orch_divisi_plan(&cfg->divisi, notes, ids, count, in->density,
                     cfg->player_cap, &runtime[part].plan);
    for (unsigned p = 0; p < runtime[part].plan.players; ++p) {
        const unsigned idx = runtime[part].plan.assignment[p];
        if (idx >= count || ids[idx] != id) continue;
        voice_start(part, note, velocity, in, p,
                    runtime[part].plan.detune_cents[p], runtime[part].plan.pan[p]);
    }
}

static void seq_tick(void)
{
    const uint32_t step_samples = (ORCH_SAMPLE_RATE * 60U) / ((uint32_t)tempo * 4U);
    if (!seq_enabled) {
        if (seq_stop_pending) {
            for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n)
                if (seq_active[n]) { seq_release_voice(seq_note[n]); seq_active[n] = 0U; }
            seq_stop_pending = 0U;
        }
        return;
    }
    if (!seq_started) {
        seq_started = 1U;
        current_step = 15U;
        step_start = sample_counter - step_samples;
    }
    for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) {
        if (seq_active[n] && (int32_t)(sample_counter - seq_off_at[n]) >= 0) {
            seq_release_voice(seq_note[n]);
            seq_active[n] = 0U;
        }
    }
    if ((uint32_t)(sample_counter - step_start) >= step_samples) {
        step_start += step_samples;
        current_step = (uint8_t)((current_step + 1U) & 15U);
        for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) {
            const struct orch_step_note *p = &pattern[current_step][n];
            if (!p->velocity) continue;
            seq_start_voice(p->note, p->velocity);
            seq_note[n] = p->note;
            seq_active[n] = 1U;
            const uint32_t gate = p->gate_percent > 100U ? 100U : p->gate_percent;
            seq_off_at[n] = sample_counter + (step_samples * gate) / 100U;
        }
    }
}

/* --------------------------------------------------------------- render */

static void voice_tick(voice_t *v)
{
    if (!v->active) return;

    uint32_t len = v->len_q16;
    if (v->glide_left) {
        if (v->len_step & 0x80000000U) {
            const uint32_t step = 0U - v->len_step;
            v->len_q16 = v->len_q16 > step ? v->len_q16 - step : 16U;
        } else {
            v->len_q16 += v->len_step;
        }
        --v->glide_left;
        len = v->len_q16;
    }
    /* vibrato: delayed, then growing; a triangle on the loop length */
    if (v->vib_depth_max) {
        if (v->vib_delay) {
            --v->vib_delay;
        } else {
            v->vib_phase = (uint16_t)(v->vib_phase + v->vib_inc);
            if (v->vib_depth < v->vib_depth_max) {
                v->vib_depth += v->vib_ramp;
                if (v->vib_depth > v->vib_depth_max) v->vib_depth = v->vib_depth_max;
            }
        }
        const int32_t tri = (int32_t)(v->vib_phase >> 7) - 256; /* -256..255 */
        const int32_t delta = (tri * (int32_t)v->vib_depth) >> 8;
        const int32_t modulated = (int32_t)len + delta;
        if (modulated > 16) len = (uint32_t)modulated;
    }

    uint32_t whole = len >> 16;
    if (whole < 4U) whole = 4U;
    if (whole > DELAY_SIZE - 4U) whole = DELAY_SIZE - 4U;
    const uint32_t frac = (len >> 8) & 0xFFU;
    const uint32_t i0 = (v->write + DELAY_SIZE - whole) & DELAY_MASK;
    const uint32_t i1 = (i0 + 1U) & DELAY_MASK;
    const int32_t s0 = v->delay[i0], s1 = v->delay[i1];
    int32_t delayed = (s0 * (int32_t)(256U - frac) + s1 * (int32_t)frac) >> 8;

    /* Loop damping. A one-pole lowpass applied every sample would eat the
     * fundamental over a round trip (0.994^200 is 0.28), so the loop uses a
     * two-tap smoother instead: highs die within a round trip, the
     * fundamental rings for seconds. More damping = darker and shorter. */
    const unsigned damp = v->damp ? v->damp : 2U; /* 1 dark .. 4+ bright */
    int32_t smoothed = delayed;
    if (damp <= 3U) smoothed = (delayed + v->lp) >> 1;
    if (damp <= 2U) smoothed = (smoothed + v->lp) >> 1;
    v->lp = delayed;
    v->out = smoothed;
    /* loop gain just under one: the string rings, the bow keeps it alive */
    const struct instrument_profile *ip = &INST_PROFILE[v->instrument];
    int32_t gain = (int32_t)ip->loop_gain + (((v->energy_q16 - ORCH_Q16) * 300) >> 16);
    if (gain < 28000) gain = 28000;
    if (gain > 32700) gain = 32700;
    int32_t loop = (smoothed * gain) >> 15;
    if (ip->half_period) loop = -loop;

    /* excitation: scaled in Q16 so the products stay in 32 bits */
    if (v->strike_left) {
        --v->strike_left;
        const int32_t burst = (noise16() * (int32_t)v->velocity) >> 8;
        loop += burst >> 1;
    }
    if (v->gate && v->excite != EXC_STRIKE) {
        const int32_t noise = noise16();
        switch (v->excite) {
        case EXC_BOW: {
            /* Helmholtz friction, added to the string's velocity loop. The
             * force depends on the relative velocity at the contact,
             * v_bow - v_string: the string is dragged while it sticks and
             * released when it slips, which is what produces the sawtooth and
             * the pitch. Two measured traps are handled here:
             *
             * - the friction force has a mean, and a near-lossless loop
             *   integrates a mean into a huge offset (2.74 M of DC and no
             *   pitch on the first attempt). Only the fluctuation enters, so
             *   the running mean of the force is subtracted. The loop itself is
             *   not filtered: a filter in the loop shifts the pitch.
             * - soft playing is a light, slightly faster stroke rather than a
             *   slow drag; below the minimum bow force the motion degrades into
             *   surface noise instead of a tone.
             */
            const int32_t soft = (v->velocity < 64U)
                                     ? (int32_t)(64U - v->velocity) * 110
                                     : 0;
            const int32_t bow = ((int32_t)v->velocity + 12) * 150 + soft;
            const int32_t vrel = bow - delayed;   /* + = bow leads the string */
            const int32_t mu = friction_q15(vrel);
            /* bow force rises with bow speed squared, as a player's does */
            const int32_t vsq = (int32_t)v->velocity * (int32_t)v->velocity;
            const int32_t fmax = ((int32_t)ip->bow_pressure * (48 + (vsq >> 6))) >> 7;
            int32_t force = (mu * fmax) >> 15;    /* magnitude */
            force = vrel < 0 ? -force : force;    /* direction follows the slip */
            v->exc_dc += (force - v->exc_dc) >> 10;   /* ~7 Hz: the DC term */
            int32_t exc = force - v->exc_dc;
            exc = (exc * (int32_t)v->bow_gain) >> 8;
            loop += exc >> 3;
            loop += (noise * ip->air_noise) >> 12;    /* rosin hiss, very low */
            loop += (delayed * (int32_t)v->accent_q16) >> 24;
            break;
        }
        case EXC_BREATH: {
            const int32_t jet = triangle32(v->excite_phase);
            v->excite_phase += v->excite_inc;
            loop += ((jet * (int32_t)v->velocity) >> 8) / 3;
            loop += (noise * ip->air_noise) >> 10;
            break;
        }
        case EXC_LIP: {
            const int32_t lip = triangle32(v->excite_phase);
            v->excite_phase += v->excite_inc;
            int32_t pressure = ((lip * (int32_t)v->velocity) >> 7) - (delayed >> 1);
            if (pressure > 12000) pressure = 12000;
            if (pressure < -12000) pressure = -12000;
            loop += pressure >> 2;
            break;
        }
        case EXC_VOICE: {
            const int32_t fundamental = triangle32(v->excite_phase);
            const int32_t overtone = triangle32(v->excite_phase * 2U);
            v->excite_phase += v->excite_inc;
            loop += ((fundamental * 3 + overtone) * (int32_t)v->velocity) >> 11;
            break;
        }
        case EXC_STRIKE:
        default:
            break;
        }
    }
    v->delay[v->write] = (int16_t)clamp16(loop);
    v->write = (v->write + 1U) & DELAY_MASK;
    {
        const int32_t body_delayed = v->body[v->body_write];
        v->body_lp += (body_delayed - v->body_lp) >> 2;
        const int32_t body_feedback = (v->body_lp * (int32_t)ip->body_gain) >> 15;
        v->body[v->body_write] = (int16_t)clamp16(body_feedback + (smoothed >> 3));
        ++v->body_write;
        if (v->body_write >= v->body_len) v->body_write = 0U;
        /* a constant offset in the loop eats headroom without making sound */
        const int32_t body_out = smoothed + ((v->body_lp * (int32_t)ip->body_mix) >> 7);
        v->dc += (body_out - v->dc) >> 8;
        v->out = body_out - v->dc;
    }

    /* the envelope: the attack runs even for a pluck (the gate is already
     * closed), then a held note sustains and a released one decays */
    if (v->attack_left) {
        --v->attack_left;
        v->env += v->env_inc;
        if (v->env > ENV_MAX) v->env = ENV_MAX;
    } else if (v->gate) {
        v->env = ENV_MAX;
    } else {
        v->env -= v->env_dec;
        if (v->env <= 8) voice_stop((unsigned)(v - voices));
    }
    if (v->stroke_left) {
        if (!--v->stroke_left) v->gate = 0U;
    }
}

void orch_render_q23(int32_t *out, unsigned frames)
{
    if (!out) return;
    drain_events();
    for (unsigned f = 0; f < frames; ++f) {
        seq_tick();
        arp_tick();
        int64_t left = 0, right = 0;
        unsigned active = 0U;
        for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
            voice_t *v = &voices[i];
            if (!v->active) continue;
            voice_tick(v);
            if (!v->active) continue;
            ++active;
            const int32_t amp = (v->out * v->env >> 15);
            const int32_t g = (amp * (int32_t)v->velocity) >> 7;
            left += (int64_t)g * v->pan_l;
            right += (int64_t)g * v->pan_r;
        }
        const int64_t div = active ? (int64_t)active : 1;
        int64_t l = left / div;
        int64_t r = right / div;
        if (l > Q23_MAX) l = Q23_MAX;
        if (l < Q23_MIN) l = Q23_MIN;
        if (r > Q23_MAX) r = Q23_MAX;
        if (r < Q23_MIN) r = Q23_MIN;
        out[f * 2U] = (int32_t)l;
        out[f * 2U + 1U] = (int32_t)r;
        ++sample_counter;
    }
}

/* ----------------------------------------------------------- inspection */

unsigned orch_active_voices(void)
{
    unsigned n = 0U;
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) n += voices[i].active ? 1U : 0U;
    return n;
}

unsigned orch_voices_of_part(unsigned part)
{
    unsigned n = 0U;
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i)
        n += (voices[i].active && voices[i].part == part) ? 1U : 0U;
    return n;
}

unsigned orch_note_count(unsigned part)
{
    return part < ORCH_PARTS ? part_note_count(part) : 0U;
}

const struct orch_intent *orch_last_intent(unsigned part)
{
    return part < ORCH_PARTS ? &runtime[part].intent : 0;
}

int orch_arp_set_enabled(unsigned part, unsigned on)
{
    if (part >= ORCH_PARTS) return -1;
    arp_t *a = &arps[part];
    a->enabled = (uint8_t)(on ? 1U : 0U);
    if (!a->enabled && a->sounding != ARP_NONE) {
        part_note_stop(part, a->sounding);
        arp_clear(a);
    }
    return 0;
}

int orch_arp_set_mode(unsigned part, unsigned mode)
{
    if (part >= ORCH_PARTS || mode >= ORCH_ARP_MODE_COUNT) return -1;
    arps[part].mode = (uint8_t)mode;
    arps[part].index = 0U;
    return 0;
}

int orch_arp_set_octaves(unsigned part, unsigned octaves)
{
    if (part >= ORCH_PARTS) return -1;
    if (octaves < 1U) octaves = 1U;
    if (octaves > 3U) octaves = 3U;
    arps[part].octaves = (uint8_t)octaves;
    arps[part].index = 0U;
    return 0;
}

int orch_arp_set_division(unsigned part, unsigned div)
{
    if (part >= ORCH_PARTS) return -1;
    if (div > 4U) div = 4U;
    arps[part].div = (uint8_t)div;
    return 0;
}

int orch_arp_set_gate(unsigned part, unsigned percent)
{
    if (part >= ORCH_PARTS) return -1;
    if (percent < 5U) percent = 5U;
    if (percent > 100U) percent = 100U;
    arps[part].gate = (uint8_t)percent;
    return 0;
}

unsigned orch_arp_enabled(unsigned part)
{
    return part < ORCH_PARTS ? arps[part].enabled : 0U;
}

unsigned orch_arp_held(unsigned part)
{
    return part < ORCH_PARTS ? arps[part].count : 0U;
}

unsigned orch_seq_position(void)
{
    return seq_enabled ? current_step : 0xFFU;
}

unsigned orch_arp_mode_of(unsigned part) { return part < ORCH_PARTS ? arps[part].mode : 0U; }
unsigned orch_arp_octaves_of(unsigned part) { return part < ORCH_PARTS ? arps[part].octaves : 1U; }
unsigned orch_arp_div_of(unsigned part) { return part < ORCH_PARTS ? arps[part].div : 2U; }
unsigned orch_arp_gate_of(unsigned part) { return part < ORCH_PARTS ? arps[part].gate : 60U; }

unsigned orch_arp_sounding(unsigned part)
{
    return part < ORCH_PARTS ? arps[part].sounding : ARP_NONE;
}

int orch_engine_configure_part(unsigned part, unsigned family, uint16_t channels)
{
    return orch_part_configure(part, family, channels);
}
int orch_engine_set_instrument(unsigned part, unsigned instrument)
{
    return orch_part_set_instrument(part, instrument);
}
int orch_engine_set_articulation(unsigned part, unsigned articulation)
{
    return orch_part_set_articulation(part, articulation);
}
int orch_engine_set_player_cap(unsigned part, unsigned cap)
{
    return orch_part_set_player_cap(part, cap);
}
int orch_engine_set_voice_ceiling(unsigned part, unsigned voices_)
{
    struct orch_part *cfg = orch_part_get(part);
    if (!cfg) return -1;
    if (!voices_) voices_ = 1U;
    if (voices_ > ORCH_MAX_VOICES) voices_ = ORCH_MAX_VOICES;
    cfg->voice_ceiling = (uint8_t)voices_;
    return 0;
}
