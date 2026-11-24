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
#include "orch_performance.h"

#ifndef FM1_AUDIO_DATA
#define FM1_AUDIO_DATA __attribute__((section(".pool")))
#endif

#define ORCH_AUDIO FM1_AUDIO_CODE

#define DELAY_SIZE 2048U
#define DELAY_MASK (DELAY_SIZE - 1U)
#define EVENT_CAP 64U
#define ORCH_ENV_MAX 32767
#define Q23_MAX 8388607
#define Q23_MIN (-8388608)
#define Q23_ONE 8388608U
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
    uint32_t age, note_id;
    uint32_t legato_transition_left, legato_transition_total;
    uint32_t loop_gain_q23; /* per-sample gain derived from per-round-trip loss */
    int32_t lp;      /* previous loop sample (the damping filter's state) */
    int32_t dc;      /* output DC blocker state */
    int32_t out;     /* last radiated sample */
    int32_t env, env_inc, env_dec, env_decay_step;
    int32_t sustain_level;
    int32_t accent_q16;
    int32_t energy_q16;
    int32_t excite_follow; /* rectified model-energy follower, Q15 */
    uint32_t level_follow; /* slow output-level follower, rectified PCM */
    uint16_t level_gain_q15;
    uint8_t level_update_count;
    uint32_t expr_phase, expr_inc;
    int32_t expr_q15;
    uint16_t expr_depth;
    uint16_t vib_phase, vib_inc;
    uint32_t lfo_phase, lfo_inc, lfo_delay_left, lfo_fade_frames, lfo_fade_gain;
    uint32_t lfo_hold;
    int16_t lfo_value;
    uint8_t lfo_depth;
    uint32_t vib_depth, vib_depth_max, vib_ramp;
    uint32_t vib_delay;
    uint32_t stroke_left;
    uint32_t attack_left, decay_left, release_frames;
    uint8_t env_stage, auto_release;
    uint16_t strike_left;
    int16_t pan_l, pan_r;
    uint8_t note, channel, part, family, instrument, artic, player, excite, damp;
    uint8_t velocity, gate, active, releasing, struck;
} voice_t;

typedef struct {
    uint8_t note;
    uint32_t id;
    uint8_t used, held;
} part_note_t;

typedef struct {
    part_note_t notes[PART_NOTES];
    struct orch_divisi_plan plan;
    uint32_t next_id;
    uint32_t last_len_q16;
    struct orch_intent intent;
} part_runtime_t;

typedef struct { uint8_t channel, note, velocity, type; } event_t;

static voice_t voices[ORCH_MAX_VOICES] FM1_AUDIO_DATA;
static event_t events[EVENT_CAP];
static part_runtime_t runtime[ORCH_PARTS];
static volatile uint8_t event_write, event_read;
static uint8_t sustain[ORCH_MIDI_CHANNELS];
static struct orch_step_note pattern[ORCH_SEQ_STEPS][ORCH_STEP_NOTES];
static uint8_t pattern_count[ORCH_SEQ_STEPS];
static uint8_t seq_note[ORCH_STEP_NOTES], seq_active[ORCH_STEP_NOTES];
static uint32_t seq_note_id[ORCH_STEP_NOTES];
/* Optional arranger context is learned from incoming MIDI only, never from
 * generated sequencer/ARP notes. Counts preserve pitch classes when notes
 * overlap in different octaves. */
static uint8_t seq_adaptive;

/* Optional one-way orchestral call/response. Disabled by default. */
static uint8_t response_enabled, response_leader, response_follower;
static int8_t response_interval;
static uint8_t response_note, response_stage, response_phrase_count, response_velocity;
static uint32_t response_on_at, response_off_at;
static uint8_t seq_input_pc_count[ORCH_PARTS][12];
static uint16_t seq_input_chord_mask[ORCH_PARTS];
static uint8_t seq_input_note[ORCH_PARTS], seq_input_velocity[ORCH_PARTS];
static uint8_t seq_input_have_note[ORCH_PARTS];
static uint8_t seq_input_activity[ORCH_PARTS];
static int8_t seq_input_direction[ORCH_PARTS];
static uint32_t seq_input_last_on[ORCH_PARTS], seq_input_ioi[ORCH_PARTS];
static uint32_t seq_off_at[ORCH_STEP_NOTES];
static uint32_t age_counter, sample_counter, step_start;

static void seq_learn_input(unsigned part, unsigned note, unsigned velocity);
static volatile uint16_t tempo = 120U;
static volatile uint8_t seq_enabled, seq_stop_pending, seq_part;
static uint8_t seq_started, current_step;
static uint32_t rng = 0x6d2b79f5U;

/* ---------------------------------------------------------------- helpers */

/* A restrained register contour compensates the model's broad spectral
 * tilt without flattening musical dynamics: bass notes get a little more
 * target level, while the extreme upper register gets a little less.
 * Piecewise-linear ramps avoid audible steps at the band boundaries. */
static uint32_t register_target_scale_q15(unsigned note)
{
    if (note < 36U) return 35389U; /* +8%, lowest register */
    if (note < 60U) return 35389U - ((note - 36U) * 2622U) / 24U;
    if (note < 108U) return 32767U - ((note - 60U) * 1966U) / 48U;
    return 30801U;                 /* -6%, extreme upper register */
}

static uint16_t level_compensation_gain(unsigned velocity, uint32_t level,
                                         unsigned note)
{
    if (level <= 64U) return 32767U;
    uint32_t target = (velocity * 5000U) / 100U;
    /* MIDI velocity and the register contour keep target below 7,000, so
     * both products fit in 32 bits; avoid a 64-bit divide on the audio path. */
    target = (target * register_target_scale_q15(note)) >> 15;
    uint32_t gain = (target << 15) / level;
    if (gain < 16384U) gain = 16384U;
    if (gain > 65535U) gain = 65535U;
    return (uint16_t)gain;
}

#ifdef FM1_HOST_TEST
uint16_t orch_test_level_compensation_gain(unsigned velocity, uint32_t level)
{
    return level_compensation_gain(velocity, level, 60U);
}
uint16_t orch_test_register_target_scale_q15(unsigned note)
{
    return (uint16_t)register_target_scale_q15(note);
}
#endif

static int32_t clamp16(int32_t x)
{
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return x;
}

static int32_t triangle32(uint32_t phase);

/* Cheap fixed-point LFO waveform generation; no floating point in the ISR. */
static int32_t lfo_wave_value(unsigned wave, uint32_t phase, uint32_t *hold)
{
    const int32_t tri = triangle32(phase);
    switch (wave) {
    case ORCH_LFO_TRIANGLE: return tri;
    case ORCH_LFO_SAW: return (int32_t)(phase >> 16) - 32768;
    case ORCH_LFO_SQUARE: return (phase & 0x80000000U) ? 32767 : -32768;
    case ORCH_LFO_SAMPLE_HOLD:
        return (int32_t)(*hold >> 16) - 32768;
    case ORCH_LFO_SINE:
    default: {
        const int32_t a = tri < 0 ? -tri : tri;
        return (tri * (49152 - (a >> 1))) >> 15;
    }
    }
}
static uint32_t lfo_phase_increment(const struct orch_lfo *lfo)
{
    if (!lfo) return 0U;
    if (lfo->sync_division) {
        static const uint8_t mul[5] = {0U,1U,2U,4U,4U};
        static const uint8_t div[5] = {1U,1U,1U,1U,3U};
        const unsigned d = lfo->sync_division;
        /* 2^32 / (60 * 44100) ~= 1626.02. Use integer arithmetic
         * and a small constant divisor; avoid unavailable __udivdi3. */
        const uint32_t numerator = (uint32_t)tempo * mul[d] * 1626U;
        return numerator / div[d];
    }
    /* 2^32 / (44100 * 1000) ~= 97.45. The 9/20 correction
     * keeps free-rate tuning within a few hundredths of a percent. */
    return (uint32_t)lfo->rate_millihz * 97U +
           ((uint32_t)lfo->rate_millihz * 9U) / 20U;
}
static uint32_t loop_gain_per_sample_q23(unsigned trip_gain_q15, unsigned samples)
{
    if (!samples) samples = 1U;
    if (trip_gain_q15 > 32767U) trip_gain_q15 = 32767U;
    /* For small losses, (1-loss)^(1/N) ~= 1-loss/N.
     * Q15 -> Q23 contributes a factor 2^8, so the numerator fits uint32_t:
     * avoid a 64-bit divide on the real-time note-on path. */
    uint32_t loss_q23 = ((32768U - trip_gain_q15) << 8) / samples;
    if (!loss_q23) loss_q23 = 1U; /* never permit a marginal, lossless loop */
    if (loss_q23 >= Q23_ONE) loss_q23 = Q23_ONE - 1U;
    return Q23_ONE - loss_q23;
}

#ifdef FM1_HOST_TEST
uint32_t orch_test_loop_gain_per_sample_q23(unsigned trip_gain_q15, unsigned samples)
{
    return loop_gain_per_sample_q23(trip_gain_q15, samples);
}
#endif

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
            runtime[p].notes[i].held = 0U;
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
        if (!rt->notes[i].used) {
            rt->notes[i].used = 1U;
            rt->notes[i].held = 1U;
            rt->notes[i].note = (uint8_t)note;
            rt->notes[i].id = rt->next_id++;
            if (!rt->next_id) rt->next_id = 1U;
            *out_id = rt->notes[i].id;
            return (int)i;
        }
    }
    return -1;
}

/* MIDI Note Off consumes the oldest still-held instance of this pitch.
 * Repeated same-pitch Note Ons therefore remain independent. */
static int part_note_pop_oldest(unsigned part, unsigned note, uint32_t *out_id)
{
    part_runtime_t *rt = &runtime[part];
    int found = -1;
    uint32_t oldest = UINT32_MAX;
    for (unsigned i = 0; i < PART_NOTES; ++i) {
        const part_note_t *n = &rt->notes[i];
        if (n->used && n->held && n->note == note && n->id < oldest) {
            oldest = n->id;
            found = (int)i;
        }
    }
    if (found < 0) return 0;
    rt->notes[found].held = 0U;
    *out_id = rt->notes[found].id;
    return 1;
}

static void part_note_remove_id(unsigned part, uint32_t id)
{
    part_runtime_t *rt = &runtime[part];
    for (unsigned i = 0; i < PART_NOTES; ++i)
        if (rt->notes[i].used && rt->notes[i].id == id)
            rt->notes[i].used = rt->notes[i].held = 0U;
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
    (void)part;
    int quietest = -1;
    int oldest = -1;
    int32_t quiet_env = 0x7fffffff;
    uint32_t oldest_age = UINT32_MAX;

    /* One global pool of exactly eight mono voices. Parts do not reserve
     * voices and cannot impose a private ceiling. A free slot always wins.
     * If all slots are occupied, steal the quietest release tail first;
     * otherwise use last-note priority by replacing the oldest held note.
     * This keeps the newest musical information and lets all parts compete
     * dynamically for the same eight physical-model voices. */
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        voice_t *v = &voices[i];
        if (!v->active) return i;
        if (v->releasing && v->env < quiet_env) {
            quiet_env = v->env;
            quietest = (int)i;
        }
        if (!v->releasing && v->age < oldest_age) {
            oldest_age = v->age;
            oldest = (int)i;
        }
    }

    if (quietest >= 0) return (unsigned)quietest;
    return (oldest >= 0) ? (unsigned)oldest : 0U;
}

/* A connected note in the same part/player should continue the existing
 * resonator instead of allocating a fresh, silent waveguide. This is the
 * physical-model equivalent of a legato bow/breath connection. */
static int find_legato_voice(unsigned part, unsigned player, unsigned family)
{
    int exact = -1;
    int fallback = -1;
    uint32_t exact_age = 0U;
    uint32_t fallback_age = 0U;
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        const voice_t *v = &voices[i];
        /* The divisi planner can renumber desks when a note is released.
         * Prefer the same desk, but for a connected melody fall back to the
         * newest resonator in this part/family if the desk index changed.
         * The adaptive connection window bounds this reuse; no arbitrary
         * envelope threshold should force a second voice after a quiet note.
         * Percussive mallets are excluded by the caller. */
        if (!v->active || v->part != part || v->family != family) continue;
        if (v->player == player && v->age >= exact_age) {
            exact = (int)i;
            exact_age = v->age;
        }
        if (v->age >= fallback_age) {
            fallback = (int)i;
            fallback_age = v->age;
        }
    }
    return exact >= 0 ? exact : fallback;
}

static void voice_start(unsigned part, unsigned note, unsigned velocity,
                        uint32_t note_id, const struct orch_intent *in, unsigned player,
                        int8_t player_detune_cents, uint8_t player_pan)
{
    const struct orch_part *cfg = orch_part_get(part);
    const unsigned family = cfg ? cfg->family : ORCH_FAM_STRINGS;
    const unsigned instrument = cfg ? cfg->instrument : ORCH_INST_VIOLIN;
    const struct instrument_profile *ip = &INST_PROFILE[instrument < ORCH_INST_COUNT ? instrument : 0U];
    const int legato_slot = (in->connected && family != ORCH_FAM_MALLETS)
                               ? find_legato_voice(part, player, family) : -1;
    const unsigned i = legato_slot >= 0 ? (unsigned)legato_slot : pick_voice(part);
    const int legato_reuse = legato_slot >= 0;
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

    if (!legato_reuse) {
        for (unsigned n = 0; n < DELAY_SIZE; ++n) v->delay[n] = 0;
        v->write = 0U;
        v->lp = 0;
        v->dc = 0;
    }
    v->age = ++age_counter;
    v->note_id = note_id;
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
    if (!legato_reuse) {
        v->level_follow = 0U;
        v->level_gain_q15 = 32767U;
        v->level_update_count = 0U;
    }
    v->active = 1U;
    v->gate = (prof.pluck || excite == EXC_STRIKE) ? 0U : 1U;
    v->releasing = 0U;
    v->struck = 0U;
    if (!legato_reuse) v->excite_follow = 0;
    v->expr_phase = legato_reuse ? v->expr_phase : rng;
    v->expr_inc = 0U;
    v->expr_q15 = 0;
    v->expr_depth = 0U;
    v->env_stage = 1U;
    v->auto_release = (uint8_t)(prof.pluck || excite == EXC_STRIKE);
    /* A connected bow/breath change is blended in over a short window.
     * Keep the resonator ringing; soften only the new excitation edge. */
    if (legato_reuse) {
        unsigned transition_ms = family == ORCH_FAM_STRINGS ? 28U :
                                 family == ORCH_FAM_BRASS ? 18U : 22U;
        if (in->down_bow) transition_ms += 5U;
        v->legato_transition_left = transition_ms * ORCH_SAMPLE_RATE / 1000U;
        v->legato_transition_total = v->legato_transition_left;
    } else {
        v->legato_transition_left = 0U;
        v->legato_transition_total = 0U;
    }
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
    if (legato_reuse) start = v->len_q16;
    if (in->connected && in->glide_ms && runtime[part].last_len_q16) {
        if (!legato_reuse) start = runtime[part].last_len_q16;
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

    /* ADSR: instrument-shaped attack/decay/sustain/release. Adaptive intent
     * scales the authored attack/release; the model's damping is independent. */
    {
        const struct orch_adsr *adsr = cfg ? &cfg->adsr : 0;
        uint32_t atk_base = (adsr && adsr->attack_ms) ? adsr->attack_ms : prof.attack_ms;
        uint32_t dec_ms = (adsr && adsr->decay_ms) ? adsr->decay_ms : ((uint32_t)prof.attack_ms * 2U + 40U);
        uint32_t rel_base = (adsr && adsr->release_ms) ? adsr->release_ms : prof.release_ms;
        uint32_t atk_ms = (atk_base * in->attack_mul) >> 16;
        uint32_t rel_ms = (rel_base * in->release_mul) >> 16;
        /* Shape decay and sustain from musical intent, not clock tempo.
         * Connected notes retain more body; phrase openings breathe in;
         * down-bows/new peaks speak more firmly; phrase endings taper. */
        {
            uint32_t decay_scale = 65536U;
            if (in->phrase_start) decay_scale += 6554U; /* +10% */
            if (in->connected) decay_scale += 11796U;   /* +18% */
            if (in->new_high) decay_scale += 6554U;     /* +10% */
            if (in->down_bow) decay_scale -= 5243U;     /* -8% */
            if (in->phrase_end) decay_scale -= 6554U;   /* -10% */
            dec_ms = (dec_ms * decay_scale) >> 16;
        }
        if (atk_ms < 2U) atk_ms = 2U;
        if (atk_ms > 400U) atk_ms = 400U;
        if (dec_ms < 20U) dec_ms = 20U;
        if (dec_ms > 800U) dec_ms = 800U;
        if (rel_ms < 30U) rel_ms = 30U;
        if (rel_ms > 3000U) rel_ms = 3000U;
        const uint32_t atk_frames = (atk_ms * ORCH_SAMPLE_RATE) / 1000U;
        const uint32_t dec_frames = (dec_ms * ORCH_SAMPLE_RATE) / 1000U;
        v->release_frames = (rel_ms * ORCH_SAMPLE_RATE) / 1000U;
        v->attack_left = atk_frames ? atk_frames : 1U;
        v->decay_left = dec_frames ? dec_frames : 1U;
        if (legato_reuse) {
            /* Keep the current envelope level and make a short connected
             * bow/breath transition rather than retriggering from silence. */
            const uint32_t legato_frames = (12U * ORCH_SAMPLE_RATE) / 1000U;
            v->attack_left = legato_frames ? legato_frames : 1U;
            v->env_inc = (ORCH_ENV_MAX - v->env) / (int32_t)v->attack_left;
        } else {
            v->env = 0;
            v->env_inc = ORCH_ENV_MAX / (int32_t)v->attack_left;
        }
        if (v->env_inc < 1) v->env_inc = 1;
        /* A modest held sustain leaves room for bow/breath dynamics. Plucks
         * use a lower sustain then enter release automatically after decay. */
        {
            int sustain = adsr ? adsr->sustain_percent : 80U;
            /* Recompute the same small phrase bias at the sustain target.
             * This makes the ADSR's S stage respond to legato, accents and
             * phrase direction while preserving the user's base setting. */
            if (in->connected) sustain += 3;
            if (in->new_high) sustain += 4;
            if (in->down_bow) sustain += 2;
            if (in->phrase_start) sustain -= 2;
            if (in->phrase_end) sustain -= 12;
            if (family == ORCH_FAM_STRINGS || family == ORCH_FAM_WOODWINDS ||
                family == ORCH_FAM_BRASS || family == ORCH_FAM_CHOIR) {
                const int energy_delta = ((int)in->energy - (int)ORCH_Q16) / 16384;
                sustain += energy_delta > 4 ? 4 : (energy_delta < -4 ? -4 : energy_delta);
            }
            if (sustain < 20) sustain = 20;
            if (sustain > 96) sustain = 96;
            if (v->auto_release && sustain > 60) sustain = 60;
            v->sustain_level = (ORCH_ENV_MAX * sustain) / 100;
        }
        v->env_decay_step = (ORCH_ENV_MAX - v->sustain_level) /
                            (int32_t)v->decay_left;
        if (v->env_decay_step < 1) v->env_decay_step = 1;
        v->env_dec = ORCH_ENV_MAX / (int32_t)(v->release_frames ? v->release_frames : 1U);
        if (v->env_dec < 1) v->env_dec = 1;
    }

    /* Slow phrase breath: family-specific, deterministic micro-dynamics.
     * Strings swell through bow pressure, winds/choir through breath support;
     * struck instruments deliberately receive no sustain wobble. */
    {
        unsigned depth = 0U;
        switch (family) {
        case ORCH_FAM_STRINGS: depth = 1500U; break;  /* +/- 4.6% */
        case ORCH_FAM_WOODWINDS: depth = 1850U; break;/* +/- 5.6% */
        case ORCH_FAM_BRASS: depth = 1050U; break;    /* +/- 3.2% */
        case ORCH_FAM_CHOIR: depth = 2100U; break;    /* +/- 6.4% */
        case ORCH_FAM_MALLETS:
        default: depth = 0U; break;
        }
        /* More expressive intent and phrase peaks can breathe a little more,
         * but the ceiling stays musical and never changes the panel ADSR. */
        if (in->new_high) depth += depth / 8U;
        if (depth > 2400U) depth = 2400U;
        v->expr_depth = (uint16_t)depth;
        {
            unsigned rate_millihz = 120U +
                (unsigned)in->density * 18U + (in->new_high ? 25U : 0U);
            /* Legato phrases breathe more slowly; articulated accents move
             * the bow/breath support slightly faster, never in BPM lock. */
            if (in->connected) rate_millihz = (rate_millihz * 82U) / 100U;
            if (in->down_bow) rate_millihz = (rate_millihz * 108U) / 100U;
            if (rate_millihz < 90U) rate_millihz = 90U;
            if (rate_millihz > 360U) rate_millihz = 360U;
            v->expr_inc = rate_millihz * 97U +
                          (rate_millihz * 9U) / 20U;
        }
    }

    v->energy_q16 = (int32_t)in->energy;
    v->accent_q16 = (int32_t)in->accent;
    {
        int32_t trip_gain = (int32_t)ip->loop_gain +
                            (((v->energy_q16 - ORCH_Q16) * 300) >> 16);
        if (trip_gain < 28000) trip_gain = 28000;
        if (trip_gain > 32700) trip_gain = 32700;
        uint32_t loop_samples = v->len_target_q16 >> 16;
        if (loop_samples < 4U) loop_samples = 4U;
        v->loop_gain_q23 = loop_gain_per_sample_q23((unsigned)trip_gain, loop_samples);
    }

    /* Assignable part LFO. Each voice has independent phase and fade state. */
    {
        const struct orch_lfo *lfo = cfg ? &cfg->lfo : 0;
        v->lfo_phase = rng;
        v->lfo_hold = rng ^ 0xa5a5f00dU;
        v->lfo_inc = lfo_phase_increment(lfo);
        if (lfo) {
            /* Let the adaptive phrase intent breathe the modulation gently:
             * 0.85..1.15x rate and 0.60..1.20x depth, always bounded. */
            const uint32_t rate_scale = 55705U +
                (uint32_t)(((uint64_t)in->vib_intent * 19661U) >> 16);
            const uint32_t depth_scale = 39322U +
                (uint32_t)(((uint64_t)in->vib_intent * 39322U) >> 16);
            v->lfo_inc = (uint32_t)(((uint64_t)v->lfo_inc * rate_scale) >> 16);
            unsigned depth = (unsigned)(((uint32_t)lfo->depth * depth_scale) >> 16);
            v->lfo_depth = (uint8_t)(depth > 100U ? 100U : depth);
        } else {
            v->lfo_depth = 0U;
        }
        v->lfo_delay_left = lfo ? ((uint32_t)lfo->delay_ms * ORCH_SAMPLE_RATE) / 1000U : 0U;
        v->lfo_fade_frames = lfo ? ((uint32_t)lfo->fade_ms * ORCH_SAMPLE_RATE) / 1000U : 0U;
        v->lfo_fade_gain = v->lfo_fade_frames ? 0U : 32767U;
        v->lfo_value = 0;
    }

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

    if (!legato_reuse) {
        v->body_len = (uint16_t)(ORCH_SAMPLE_RATE / (ip->body_hz ? ip->body_hz : 440U));
        if (v->body_len < 8U) v->body_len = 8U;
        if (v->body_len > 512U) v->body_len = 512U;
        v->body_write = 0U; v->body_lp = 0;
        for (unsigned n = 0; n < v->body_len; ++n) v->body[n] = 0;
        v->excite_phase = rng ^ (note * 0x9e3779b9U);
    }
    v->excite_inc = ip->half_period ? (uint32_t)(0x7fffffffU / (uint32_t)(target >> 16))
                                    : (uint32_t)(0xffffffffU / (uint32_t)(target >> 16));

    /* a stroke ends by itself; a pluck rings with the gate already closed */
    v->stroke_left = prof.stroke_ms
                         ? (uint32_t)prof.stroke_ms * ORCH_SAMPLE_RATE / 1000U
                         : 0U;

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
    events[event_write].note = (uint8_t)(type == 3U ? note : (note & 127U));
    events[event_write].velocity = (uint8_t)(velocity & 127U);
    events[event_write].type = (uint8_t)type;
    event_write = next;
}

void orch_note_event(unsigned ch, unsigned note, unsigned velocity)
{
    if (ch < 16U && note < 128U && velocity < 128U) enqueue(ch, note, velocity, 0U);
}
void orch_reset_notes(void) { enqueue(0U, 0U, 0U, 2U); }

void orch_midi_realtime(unsigned status)
{
    if(status==0xF8U || status==0xFAU || status==0xFBU || status==0xFCU)
        enqueue(0U,status,0U,3U);
}

void orch_midi_clock_tempo(unsigned bpm)
{
    if(!bpm)return;
    if(bpm<40U)bpm=40U;
    if(bpm>250U)bpm=250U;
    enqueue(0U,bpm&127U,(bpm>>7)&127U,4U);
}

void orch_midi_short_message(const uint8_t *m, unsigned bytes)
{
    if (!m || !bytes) return;
    if (m[0] == 0xF8U || m[0] == 0xFAU || m[0] == 0xFBU || m[0] == 0xFCU) {
        orch_midi_realtime(m[0]);
        return;
    }
    if (bytes < 2U || m[0] < 0x80U || m[0] >= 0xf0U) return;
    const unsigned kind = m[0] & 0xf0U, ch = m[0] & 15U;
    if ((kind == 0x80U || kind == 0x90U) && bytes >= 3U)
        enqueue(ch, m[1], (kind == 0x80U || m[2] == 0U) ? 0U : m[2], 0U);
    else if (kind == 0xb0U && bytes >= 3U) {
        if (m[1] == 64U)
            enqueue(ch, 0U, m[2] >= 64U, 1U);
        else if (m[1] == 120U || m[1] == 123U)
            enqueue(ch, 0U, 0U, 5U);
    }
}

/* The note-on path: interpret, plan divisi, realize the players. */
/* A connected melody must advance the lead player even when the
 * divisi planner's hysteresis would otherwise keep every player on the old
 * held chord. Reassign only one desk (the last active player) to the new note;
 * the remaining desks retain their chord tones. */
static void divisi_promote_connected_note(struct orch_part *cfg,
        struct orch_divisi_plan *plan, const uint8_t *notes,
        const uint32_t *ids, unsigned count, uint32_t newest_id)
{
    if (!cfg || !plan || !notes || !ids || !count || !plan->players) return;
    for (unsigned p = 0; p < plan->players; ++p) {
        const unsigned idx = plan->assignment[p];
        if (idx < count && ids[idx] == newest_id) return;
    }
    unsigned newest = count;
    for (unsigned i = 0; i < count; ++i)
        if (ids[i] == newest_id) { newest = i; break; }
    if (newest >= count) return;
    const unsigned desk = plan->players - 1U;
    plan->assignment[desk] = (uint8_t)newest;
    cfg->divisi.assigned[desk] = 1U;
    cfg->divisi.note_of_player[desk] = notes[newest];
    cfg->divisi.id_of_player[desk] = newest_id;
}

static void engine_note_on_part(unsigned part, unsigned note, unsigned velocity)
{
    if (part >= ORCH_PARTS || !velocity) return;
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
    if (in->connected)
        divisi_promote_connected_note(cfg, &rt->plan, notes, ids, count, id);

    /* Realize only the players that took this new note. A connected
     * melody must never disappear if a divisi assignment is temporarily
     * retained by hysteresis: in that case hand the lead desk to this note. */
    unsigned started = 0U;
    for (unsigned p = 0; p < rt->plan.players; ++p) {
        const unsigned idx = rt->plan.assignment[p];
        if (idx >= count || ids[idx] != id) continue;
        voice_start(part, note, velocity, id, in, p,
                    rt->plan.detune_cents[p], rt->plan.pan[p]);
        ++started;
    }
    if (!started && in->connected) {
        const unsigned p = rt->plan.players ? rt->plan.players - 1U : 0U;
        voice_start(part, note, velocity, id, in, p,
                    rt->plan.detune_cents[p], rt->plan.pan[p]);
    }
}

static void engine_note_on(unsigned ch, unsigned note, unsigned velocity)
{
    const unsigned part=orch_part_for_channel(ch);
    if(part!=0xFFU) engine_note_on_part(part,note,velocity);
}

static void engine_note_off_part(unsigned part, unsigned note, unsigned sustain_on)
{
    if (part >= ORCH_PARTS) return;
    uint32_t id = 0U;
    if (!part_note_pop_oldest(part, note, &id)) return;
    struct orch_part *cfg = orch_part_get(part);
    if (cfg) orch_adaptive_off(&cfg->adaptive, sample_counter);

    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        voice_t *v = &voices[i];
        if (!v->active || v->part != part || v->note_id != id) continue;
        v->gate = 0U;
        if (!sustain_on) v->releasing = 1U;
    }
    if (!sustain_on) part_note_remove_id(part, id);
}

static void engine_note_off(unsigned ch, unsigned note, unsigned sustain_on)
{
    const unsigned part=orch_part_for_channel(ch);
    if(part!=0xFFU) engine_note_off_part(part,note,sustain_on);
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
        if (v->active && v->part == part && !v->gate)
            v->releasing = 1U;
    }
    /* Also clear sustained note records whose voices were stolen meanwhile. */
    for (unsigned n = 0; n < PART_NOTES; ++n)
        if (runtime[part].notes[n].used && !runtime[part].notes[n].held)
            part_note_remove_id(part, runtime[part].notes[n].id);
}


/* Keep an answer inside the player's currently held harmony whenever
 * that information exists. The configured interval remains the fallback. */
static unsigned response_harmonic_note(unsigned part, unsigned raw)
{
    if (part >= ORCH_PARTS) return raw;
    const uint16_t mask = seq_input_chord_mask[part];
    if (!mask) return raw;
    unsigned best = raw, best_dist = 128U;
    for (unsigned n = 36U; n <= 96U; ++n) {
        if (!(mask & (uint16_t)(1U << (n % 12U)))) continue;
        const unsigned d = n > raw ? n - raw : raw - n;
        if (d < best_dist) { best = n; best_dist = d; }
    }
    return best;
}

/* When both wind and brass parts are configured, let them trade the answer.
 * Other configurations retain the explicitly selected follower. */
static unsigned response_section_part(void)
{
    const struct orch_part *base = orch_part_get(response_follower);
    if (!base) return response_follower;
    if (base->family != ORCH_FAM_WOODWINDS && base->family != ORCH_FAM_BRASS)
        return response_follower;
    const unsigned wanted = base->family == ORCH_FAM_WOODWINDS ?
                            ORCH_FAM_BRASS : ORCH_FAM_WOODWINDS;
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const struct orch_part *candidate = orch_part_get(p);
        if (p != response_leader && candidate && candidate->enabled &&
            candidate->family == wanted) return p;
    }
    return response_follower;
}

static void response_schedule(unsigned leader, unsigned note, unsigned velocity)
{
    if (!response_enabled || leader != response_leader || !velocity ||
        response_stage != 0U || response_leader == response_follower) return;
    /* Two complementary motifs prevent every answer becoming the same
     * transposed copy. They are intentionally small and deterministic. */
    static const int8_t motif[4] = { 0, 2, -2, 0 };
    if (++response_phrase_count & 1U) return;
    int answer = (int)note + (int)response_interval +
                 (int)motif[(response_phrase_count >> 1) & 3U];
    if (answer < 36) answer = 36;
    if (answer > 96) answer = 96;
    answer = (int)response_harmonic_note(leader, (unsigned)answer);
    response_note = (uint8_t)answer;
    response_follower = (uint8_t)response_section_part();
    const uint32_t quarter = (ORCH_SAMPLE_RATE * 60U) / (uint32_t)tempo;
    const struct orch_part *follower_cfg = orch_part_get(response_follower);
    /* Strings become a quiet harmonic bed; winds/brass make shorter replies. */
    if (follower_cfg && follower_cfg->family == ORCH_FAM_STRINGS) {
        response_velocity = 54U;
        response_on_at = sample_counter + quarter;
        response_off_at = response_on_at + quarter * 2U;
    } else {
        response_velocity = 72U;
        response_on_at = sample_counter + quarter / 2U;
        response_off_at = response_on_at + quarter / 2U;
    }
    response_stage = 1U;
}

static void response_tick(void)
{
    if (!response_enabled || response_stage==0U) return;
    if (response_stage==1U && (int32_t)(sample_counter-response_on_at)>=0) {
        engine_note_on_part(response_follower,response_note,response_velocity);
        response_stage=2U;
    }
    if (response_stage==2U && (int32_t)(sample_counter-response_off_at)>=0) {
        engine_note_off_part(response_follower,response_note,0U);
        response_stage=0U;
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
            part_notes_reset();
            orch_perf_reset_notes();
        } else if (e.type == 3U) {
            orch_perf_midi_realtime(e.note,sample_counter);
        } else if (e.type == 4U) {
            orch_perf_set_external_tempo((unsigned)e.note | ((unsigned)e.velocity<<7));
        } else if (e.type == 1U) {
            engine_sustain(e.channel, e.velocity);
        } else if (e.type == 5U) {
            sustain[e.channel] = 0U;
            for (unsigned n = 0; n < 128U; ++n)
                engine_note_off(e.channel, n, 0U);
        } else {
            const unsigned part=orch_part_for_channel(e.channel);
            if (part != 0xFFU) seq_learn_input(part, e.note, e.velocity);
            if (part != 0xFFU && e.velocity) response_schedule(part,e.note,e.velocity);
            if (!orch_perf_note_event(part,e.note,e.velocity)) {
                if (e.velocity) engine_note_on(e.channel,e.note,e.velocity);
                else engine_note_off(e.channel,e.note,e.channel<ORCH_MIDI_CHANNELS &&
                                                        sustain[e.channel]);
            }
        }
    }
}

static void drain_performance_events(void)
{
    struct orch_perf_event e;
    while(orch_perf_pop_event(&e)) {
        if(e.note_on) engine_note_on_part(e.part,e.note,e.velocity);
        else engine_note_off_part(e.part,e.note,0U);
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

/* Configure a sparse, tempo-synchronised answer line between two parts.
 * Interval is semitones (-24..24). Disabled by default. */
void orch_call_response_configure(unsigned enabled, unsigned leader,
                                  unsigned follower, int interval)
{
    if (leader>=ORCH_PARTS || follower>=ORCH_PARTS || leader==follower) {
        response_enabled=0U; response_stage=0U; return;
    }
    if (interval < -24) interval=-24;
    if (interval > 24) interval=24;
    response_leader=(uint8_t)leader;
    response_follower=(uint8_t)follower;
    response_interval=(int8_t)interval;
    response_enabled=(uint8_t)(enabled?1U:0U);
    response_stage=0U;
    response_phrase_count=0U;
}

void orch_seq_set_adaptive(unsigned enabled)
{
    seq_adaptive = enabled ? 1U : 0U;
}

/* Called only for physical MIDI input. This gives the arranger a read-only
 * picture of the player's harmony, contour and activity. */
static void seq_learn_input(unsigned part, unsigned note, unsigned velocity)
{
    if (part >= ORCH_PARTS || note > 127U) return;
    const unsigned pc = note % 12U;
    if (velocity) {
        if (seq_input_last_on[part] && sample_counter > seq_input_last_on[part]) {
            const uint32_t ioi = sample_counter - seq_input_last_on[part];
            if (ioi < ORCH_SAMPLE_RATE * 8U)
                seq_input_ioi[part] = seq_input_ioi[part] ?
                    (seq_input_ioi[part] * 3U + ioi) / 4U : ioi;
        }
        if (seq_input_have_note[part]) {
            const int delta = (int)note - (int)seq_input_note[part];
            if (delta >= 2) seq_input_direction[part] = 1;
            else if (delta <= -2) seq_input_direction[part] = -1;
        }
        seq_input_note[part] = (uint8_t)note;
        seq_input_have_note[part] = 1U;
        seq_input_velocity[part] = seq_input_velocity[part] ?
            (uint8_t)((seq_input_velocity[part] * 3U + velocity) / 4U) :
            (uint8_t)velocity;
        seq_input_last_on[part] = sample_counter;
        if (seq_input_pc_count[part][pc] < 255U)
            ++seq_input_pc_count[part][pc];
    } else if (seq_input_pc_count[part][pc]) {
        --seq_input_pc_count[part][pc];
    }
    seq_input_chord_mask[part] = 0U;
    for (unsigned i = 0; i < 12U; ++i)
        if (seq_input_pc_count[part][i])
            seq_input_chord_mask[part] |= (uint16_t)(1U << i);
    if (!seq_input_chord_mask[part]) {
        seq_input_have_note[part] = 0U;
        seq_input_velocity[part] = 0U;
        seq_input_direction[part] = 0;
    }

    seq_input_activity[part] = 100U;
    if (seq_input_ioi[part]) {
        const uint32_t nominal = ORCH_SAMPLE_RATE * 3U / 4U;
        if (seq_input_ioi[part] < nominal / 2U) seq_input_activity[part] = 165U;
        else if (seq_input_ioi[part] > nominal * 2U) seq_input_activity[part] = 65U;
        else seq_input_activity[part] = (uint8_t)(165U -
            ((seq_input_ioi[part] - nominal / 2U) * 100U) / (nominal * 3U / 2U));
    }
}

static unsigned seq_adapt_note(unsigned part, unsigned note)
{
    if (!seq_adaptive || part >= ORCH_PARTS) return note;
    const uint16_t mask = seq_input_chord_mask[part];
    if (!mask || !seq_input_have_note[part] ||
        (uint32_t)(sample_counter - seq_input_last_on[part]) >= ORCH_SAMPLE_RATE * 2U)
        return note;
    /* Keep the stored step as the anchor, but gently pull its register toward
     * the player's latest pitch. A 2:1 weighting prevents the arranger from
     * simply copying the input melody while still following register changes. */
    const unsigned target = (note * 2U + seq_input_note[part]) / 3U;
    unsigned best = note, best_dist = 128U;
    for (unsigned candidate = 0; candidate < 128U; ++candidate) {
        if (!(mask & (uint16_t)(1U << (candidate % 12U)))) continue;
        const unsigned dist = candidate > target ? candidate - target : target - candidate;
        if (dist < best_dist ||
            (dist == best_dist && seq_input_direction[part] > 0 && candidate > best) ||
            (dist == best_dist && seq_input_direction[part] < 0 && candidate < best)) {
            best = candidate;
            best_dist = dist;
        }
    }
    return best;
}

#ifdef FM1_HOST_TEST
unsigned orch_test_seq_adapt_note(unsigned part, unsigned note)
{
    return seq_adapt_note(part, note);
}
#endif

void orch_init(void)
{
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) voice_stop(i);
    event_read = event_write = 0U;
    for (unsigned c = 0; c < ORCH_MIDI_CHANNELS; ++c) sustain[c] = 0U;
    orch_parts_init();
    part_notes_reset();
    orch_seq_clear();
    orch_perf_init();
    tempo = 120U;
    seq_enabled = seq_started = seq_stop_pending = 0U;
    seq_part = 0U;
    seq_adaptive = 0U;
    response_enabled=response_stage=response_phrase_count=0U;
    response_leader=0U; response_follower=1U; response_interval=7;
    current_step = 0U;
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        seq_input_chord_mask[p] = 0U;
        seq_input_note[p] = seq_input_velocity[p] = 0U;
        seq_input_have_note[p] = 0U;
        seq_input_activity[p] = 100U;
        seq_input_direction[p] = 0;
        seq_input_last_on[p] = seq_input_ioi[p] = 0U;
        for (unsigned pc = 0; pc < 12U; ++pc) seq_input_pc_count[p][pc] = 0U;
    }
    sample_counter = step_start = 0U;
    for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) {
        seq_active[n] = 0U; seq_off_at[n] = 0U; seq_note[n] = 0U; seq_note_id[n] = 0U;
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
static void seq_release_voice(unsigned note, uint32_t note_id)
{
    const unsigned part = seq_part;
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        voice_t *v = &voices[i];
        if (v->active && v->part == part && v->note_id == note_id &&
            v->note == note && v->gate) {
            v->gate = 0U;
            v->releasing = 1U;
        }
    }
    part_note_remove_id(part, note_id);
}

static uint32_t seq_start_voice(unsigned note, unsigned velocity)
{
    const unsigned part = seq_part;
    struct orch_part *cfg = orch_part_get(part);
    if (!cfg) return 0U;
    struct orch_intent *in = &runtime[part].intent;
    orch_adaptive_note(&cfg->adaptive, in, note, velocity, sample_counter,
                       cfg->articulation);
    orch_adaptive_velocity(&cfg->adaptive, in, velocity);
    uint32_t id = 0U;
    if (part_note_add(part, note, &id) < 0) return 0U;

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
    if (in->connected)
        divisi_promote_connected_note(cfg, &runtime[part].plan, notes, ids, count, id);
    for (unsigned p = 0; p < runtime[part].plan.players; ++p) {
        const unsigned idx = runtime[part].plan.assignment[p];
        if (idx >= count || ids[idx] != id) continue;
        voice_start(part, note, velocity, id, in, p,
                    runtime[part].plan.detune_cents[p], runtime[part].plan.pan[p]);
    }
    return id;
}

/* Match the ARP/phrase engine's 16-step orchestral phrase arc. */
static const uint8_t seq_phrase_arc[16] = {
    82U, 86U, 90U, 95U, 101U, 107U, 113U, 118U,
    120U, 116U, 108U, 99U, 90U, 82U, 74U, 68U
};

#ifdef FM1_HOST_TEST
unsigned orch_test_seq_phrase_arc_velocity(unsigned velocity, unsigned position)
{
    unsigned value=(velocity*(unsigned)seq_phrase_arc[position&15U]+50U)/100U;
    return value<1U?1U:(value>127U?127U:value);
}
#endif

static void seq_tick(void)
{
    if (!seq_enabled) {
        if (seq_stop_pending) {
            for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n)
                if (seq_active[n]) { seq_release_voice(seq_note[n], seq_note_id[n]); seq_active[n] = 0U; }
            seq_stop_pending = 0U;
        }
        return;
    }
    /* Tempo changes rarely; cache the integer step length instead of
     * dividing once per audio sample. */
    static uint16_t cached_tempo;
    static uint32_t cached_step_samples;
    const uint16_t current_tempo = tempo;
    if (!cached_step_samples || cached_tempo != current_tempo) {
        cached_tempo = current_tempo;
        cached_step_samples = (ORCH_SAMPLE_RATE * 60U) /
                              ((uint32_t)current_tempo * 4U);
    }
    const uint32_t step_samples = cached_step_samples;
    if (!seq_started) {
        seq_started = 1U;
        current_step = 15U;
        step_start = sample_counter - step_samples;
    }
    for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) {
        if (seq_active[n] && (int32_t)(sample_counter - seq_off_at[n]) >= 0) {
            seq_release_voice(seq_note[n], seq_note_id[n]);
            seq_active[n] = 0U;
        }
    }
    if ((uint32_t)(sample_counter - step_start) >= step_samples) {
        step_start += step_samples;
        current_step = (uint8_t)((current_step + 1U) & 15U);
        for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) {
            const struct orch_step_note *p = &pattern[current_step][n];
            if (!p->velocity) continue;
            /* In adaptive mode, keep the user's rhythm and step data, but
             * voice chord tones from the live harmony and thin inner voices
             * during quiet/sparse playing. Slot zero remains the anchor. */
            const unsigned have_input = seq_input_have_note[seq_part] &&
                (uint32_t)(sample_counter - seq_input_last_on[seq_part]) < ORCH_SAMPLE_RATE * 2U;
            if (seq_adaptive && have_input && n > 0U &&
                (seq_input_activity[seq_part] < 82U ||
                 seq_input_velocity[seq_part] < 46U) &&
                (current_step & 3U) != 0U) continue;
            const unsigned played_note = seq_adapt_note(seq_part, p->note);
            unsigned velocity = p->velocity;
            if (seq_adaptive && have_input) {
                const int delta = ((int)seq_input_velocity[seq_part] - 80) / 5;
                int adjusted = (int)velocity + delta;
                if ((current_step & 3U) == 0U) adjusted += 5;
                if (adjusted < 1) adjusted = 1;
                if (adjusted > 127) adjusted = 127;
                /* A full 16-step arc gives the line a rise, peak and release;
                 * it changes playback velocity only, never stored step data. */
                adjusted = (adjusted * (int)seq_phrase_arc[current_step] + 50) / 100;
                if (adjusted < 1) adjusted = 1;
                if (adjusted > 127) adjusted = 127;
                velocity = (unsigned)adjusted;
            }
            seq_note_id[n] = seq_start_voice(played_note, velocity);
            seq_note[n] = (uint8_t)played_note;
            seq_active[n] = 1U;
            /* Preserve the stored gate as the base; adaptive activity adds
             * legato space for relaxed input and shorter articulation when
             * the player is moving quickly. */
            unsigned gate = p->gate_percent > 100U ? 100U : p->gate_percent;
            if (seq_adaptive && have_input) {
                unsigned adaptive_gate = 82U;
                if (seq_input_activity[seq_part] < 82U) adaptive_gate = 94U;
                else if (seq_input_activity[seq_part] > 135U) adaptive_gate = 68U;
                gate = (gate * 3U + adaptive_gate) / 4U;
                gate = (gate * (unsigned)seq_phrase_arc[current_step] + 50U) / 100U;
            }
            if (gate < 8U) gate = 8U;
            if (gate > 100U) gate = 100U;
            seq_off_at[n] = sample_counter + (step_samples * gate) / 100U;
        }
    }
}

/* --------------------------------------------------------------- render */

/* Return the part's LFO destination mask so the mixer can reuse the same
 * config lookup. This function is the per-sample hot path: resolve the part,
 * LFO and instrument profile once, not separately for each modulation stage. */
static unsigned voice_tick(voice_t *v)
{
    if (!v->active) return 0U;

    const struct orch_part *cfg = orch_part_get(v->part);
    const struct orch_lfo *lfo = cfg ? &cfg->lfo : 0;
    const struct instrument_profile *ip = &INST_PROFILE[v->instrument];
    const unsigned lfo_destinations = lfo ? lfo->destinations : 0U;
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
    /* Phrase-shaped sustain expression, about 0.14-0.30 Hz. A smooth
     * triangle-derived sine avoids abrupt corners and runs only while the
     * note is sustaining/releasing. Its depth is instrument-family specific. */
    if (v->expr_depth && (v->env_stage == 3U || v->env_stage == 4U)) {
        v->expr_phase += v->expr_inc;
        {
            int32_t tri = triangle32(v->expr_phase);
            const int32_t mag = tri < 0 ? -tri : tri;
            const int32_t sine = (tri * (49152 - (mag >> 1))) >> 15;
            v->expr_q15 = (sine * (int32_t)v->expr_depth) >> 15;
        }
    } else {
        v->expr_q15 = 0;
    }

    /* General LFO runs independently of the legacy expressive vibrato. */
    {
        if (lfo && lfo_destinations && v->lfo_depth) {
            if (v->lfo_delay_left) --v->lfo_delay_left;
            else {
                const uint32_t old_phase = v->lfo_phase;
                v->lfo_phase += v->lfo_inc;
                if (lfo->wave == ORCH_LFO_SAMPLE_HOLD && v->lfo_phase < old_phase)
                    v->lfo_hold = v->lfo_hold * 1664525U + 1013904223U;
                if (v->lfo_fade_gain < 32767U) {
                    const uint32_t step = v->lfo_fade_frames
                        ? (32767U / v->lfo_fade_frames) + 1U : 32767U;
                    v->lfo_fade_gain += step;
                    if (v->lfo_fade_gain > 32767U) v->lfo_fade_gain = 32767U;
                }
                int32_t value = lfo_wave_value(lfo->wave, v->lfo_phase, &v->lfo_hold);
                value = (value * (int32_t)v->lfo_fade_gain) >> 15;
                v->lfo_value = (int16_t)value;
                if (lfo->destinations & ORCH_LFO_PITCH) {
                    const int32_t cents = ((int32_t)v->lfo_depth * 50) / 100;
                    const int32_t delta = (int32_t)(((int64_t)len * cents * 38 * value) >> 31);
                    const int32_t modulated = (int32_t)len + delta;
                    if (modulated > 16) len = (uint32_t)modulated;
                }
            }
        } else {
            v->lfo_value = 0;
        }
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
    /* ADSR also shapes excitation timbre. A rectified follower tracks recent
     * acoustic energy; the attack contour supplies a short-lived transient. */
    {
        int32_t target = v->out < 0 ? -v->out : v->out;
        if (target > 32767) target = 32767;
        const int32_t delta = target - v->excite_follow;
        v->excite_follow += delta >> (delta > 0 ? 2 : 6);
    }
    const int32_t env_q15 = v->env < 0 ? 0 :
        (v->env > ORCH_ENV_MAX ? ORCH_ENV_MAX : v->env);
    const int32_t transient_q15 = (v->env_stage == 1U)
        ? (ORCH_ENV_MAX - env_q15) : 0;
    const int32_t env_excitation_q15 = 16384 + (env_q15 >> 1);
    const int32_t expression_gain_q15 = 32767 + v->expr_q15;
    int32_t excitation_gain_q15 =
        (env_excitation_q15 * (expression_gain_q15 >> 1)) >> 14;
    if (v->legato_transition_left) {
        const uint32_t total = v->legato_transition_total ? v->legato_transition_total : 1U;
        const uint32_t left = v->legato_transition_left;
        const uint32_t elapsed = total > left ? total - left : 0U;
        /* 72% -> 100% smooth excitation handover; no silence dip. */
        const int32_t blend = 23593 + (int32_t)((uint64_t)elapsed * 9174U / total);
        excitation_gain_q15 = (excitation_gain_q15 * blend) >> 15;
        --v->legato_transition_left;
    }
    int damp_value = v->damp ? v->damp : 2U;
    damp_value += (transient_q15 >> 13) + (v->excite_follow >> 14);
    if (damp_value > 6) damp_value = 6;
    {
        if (lfo_destinations & ORCH_LFO_BRIGHTNESS) {
            const int bounded_depth = v->lfo_depth > 24U ? 24 : (int)v->lfo_depth;
            const int shift = ((int)v->lfo_value * bounded_depth) / (32768 * 12);
            damp_value += shift;
            if (damp_value < 1) damp_value = 1;
            if (damp_value > 6) damp_value = 6;
        }
    }
    const unsigned damp = (unsigned)damp_value; /* 1 dark .. 6 bright */
    int32_t smoothed = delayed;
    if (damp <= 3U) smoothed = (delayed + v->lp) >> 1;
    if (damp <= 2U) smoothed = (smoothed + v->lp) >> 1;
    v->lp = delayed;
    v->out = smoothed;
    /* loop loss is calibrated per round trip at note start, not per sample:
     * otherwise low notes decay faster simply because their delay is longer. */
    int32_t loop = (int32_t)(((int64_t)smoothed * v->loop_gain_q23) >> 23);
    if (ip->half_period) loop = -loop;

    /* excitation: scaled in Q16 so the products stay in 32 bits */
    if (v->strike_left) {
        --v->strike_left;
        const int32_t burst = (noise16() * (int32_t)v->velocity) >> 8;
        loop += ((burst >> 1) * excitation_gain_q15) >> 15;
    }
    if (v->gate && v->excite != EXC_STRIKE) {
        const int32_t noise = noise16();
        switch (v->excite) {
        case EXC_BOW: {
            /* Rosin noise, not a friction feedback. Feeding the string's own
             * motion back with a constant bow speed injects a fixed offset;
             * measured on this engine: DC 2.74 M against a 2.97 M peak and no
             * pitch at all (f0/DC = 0). The profile's bow_pressure scales the
             * rosin, the intent's energy the stroke, the accent the bite.
             * The real Helmholtz friction model is still not ported. */
            const int32_t rosin = (int32_t)(ip->bow_pressure >> 4);
            const int32_t drive = ((int32_t)v->velocity * rosin) >> 6;
            int32_t exc = (noise * drive) >> 15;
            exc = (exc * (int32_t)(65536 + ((v->energy_q16 - ORCH_Q16) >> 1))) >> 16;
            {
                const int32_t air = (noise * ip->air_noise) >> 10;
                const int32_t bite = (delayed * (int32_t)v->accent_q16) >> 24;
                loop += ((exc + air + bite) * excitation_gain_q15) >> 15;
            }
            break;
        }
        case EXC_BREATH: {
            const int32_t jet = triangle32(v->excite_phase);
            v->excite_phase += v->excite_inc;
            {
                const int32_t breath = (((jet * (int32_t)v->velocity) >> 8) / 3) +
                                       ((noise * ip->air_noise) >> 10);
                loop += (breath * excitation_gain_q15) >> 15;
            }
            break;
        }
        case EXC_LIP: {
            const int32_t lip = triangle32(v->excite_phase);
            v->excite_phase += v->excite_inc;
            int32_t pressure = ((lip * (int32_t)v->velocity) >> 7) - (delayed >> 1);
            if (pressure > 12000) pressure = 12000;
            if (pressure < -12000) pressure = -12000;
            loop += ((pressure >> 2) * excitation_gain_q15) >> 15;
            break;
        }
        case EXC_VOICE: {
            const int32_t fundamental = triangle32(v->excite_phase);
            const int32_t overtone = triangle32(v->excite_phase * 2U);
            v->excite_phase += v->excite_inc;
            {
                const int32_t voiced = ((fundamental * 3 + overtone) *
                                        (int32_t)v->velocity) >> 11;
                loop += (voiced * excitation_gain_q15) >> 15;
            }
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

    /* Gentle per-instrument loudness compensation. The rectified output
     * follower estimates each model's sustained level; gain moves slowly
     * toward a common target proportional to MIDI velocity, so velocity
     * expression is retained. A +/-6 dB bound prevents noise pumping and
     * leaves the instrument's spectral identity untouched. */
    {
        const int32_t magnitude = v->out < 0 ? -v->out : v->out;
        const uint32_t current = v->level_follow;
        if ((uint32_t)magnitude > current)
            v->level_follow = current + (((uint32_t)magnitude - current) >> 9);
        else
            v->level_follow = current - ((current - (uint32_t)magnitude) >> 12);
        if (++v->level_update_count >= 16U) {
            v->level_update_count = 0U;
            if (v->level_follow > 64U)
                v->level_gain_q15 =
                    level_compensation_gain(v->velocity, v->level_follow, v->note);
        }
    }

    /* Four-stage per-voice ADSR. A MIDI note-off can enter release from
     * any point in the contour. Plucks/strikes complete attack + decay before
     * their automatic release, so the physical model is allowed to ring. */
    if (v->releasing && v->env_stage != 4U) {
        v->env_stage = 4U;
        v->env_dec = (int32_t)(v->env / (int32_t)(v->release_frames ? v->release_frames : 1U));
        if (v->env_dec < 1) v->env_dec = 1;
    }
    switch (v->env_stage) {
    case 1U: /* attack */
        if (v->attack_left) --v->attack_left;
        v->env += v->env_inc;
        if (!v->attack_left || v->env >= ORCH_ENV_MAX) {
            v->env = ORCH_ENV_MAX;
            v->env_stage = 2U;
        }
        break;
    case 2U: /* decay */
        if (v->decay_left) --v->decay_left;
        v->env -= v->env_decay_step;
        if (!v->decay_left || v->env <= v->sustain_level) {
            v->env = v->sustain_level;
            v->env_stage = 3U;
        }
        break;
    case 3U: /* sustain */
        v->env = v->sustain_level;
        if (!v->gate || v->auto_release) {
            v->releasing = 1U;
            v->env_stage = 4U;
            v->env_dec = (int32_t)(v->env / (int32_t)(v->release_frames ? v->release_frames : 1U));
            if (v->env_dec < 1) v->env_dec = 1;
        }
        break;
    case 4U: /* release */
        v->env -= v->env_dec;
        if (v->env <= 8) voice_stop((unsigned)(v - voices));
        break;
    default:
        voice_stop((unsigned)(v - voices));
        break;
    }
    if (v->stroke_left) {
        if (!--v->stroke_left) v->gate = 0U;
    }
    return lfo_destinations;
}

/* Division by the small active-voice count is common on every sample.
 * Keep the divisors constant at compile time so the target compiler can use
 * reciprocal multiplies instead of a general integer divide helper. */
static int32_t divide_voice_sum(int32_t value, unsigned voices_active)
{
    switch (voices_active) {
    case 1U: return value;
    case 2U: return value / 2;
    case 3U: return value / 3;
    case 4U: return value / 4;
    case 5U: return value / 5;
    case 6U: return value / 6;
    case 7U: return value / 7;
    case 8U: return value / 8;
    default: return 0;
    }
}

void orch_render_q23(int32_t *out, unsigned frames)
{
    if (!out) return;
    drain_events();
    for (unsigned f = 0; f < frames; ++f) {
        orch_perf_advance_sample(sample_counter);
        drain_performance_events();
        response_tick();
        seq_tick();
        /* Pan is Q8 (0..256), not Q15. Accumulate the original Q8 products
         * in int32_t; with at most eight voices the sum is bounded below
         * 2^31. This preserves the original gain and avoids __divdi3. */
        int32_t left = 0, right = 0;
        unsigned active = 0U;
        for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
            voice_t *v = &voices[i];
            if (!v->active) continue;
            const unsigned lfo_destinations = voice_tick(v);
            if (!v->active) continue;
            ++active;
            int32_t amp = (v->out * v->env >> 15);
            amp = (amp * (int32_t)v->level_gain_q15) >> 15;
            amp = (amp * (32767 + v->expr_q15)) >> 15;
            {
                if (lfo_destinations & ORCH_LFO_TREMOLO) {
                    const int32_t depth = (int32_t)v->lfo_depth * 245 / 100;
                    const int32_t gain = 32767 - ((v->lfo_value + 32768) * depth >> 16);
                    amp = (amp * gain) >> 15;
                }
            }
            const int32_t g = (amp * (int32_t)v->velocity) >> 7;
            left += g * (int32_t)v->pan_l;
            right += g * (int32_t)v->pan_r;
        }
        int32_t l = active ? divide_voice_sum(left, active) : 0;
        int32_t r = active ? divide_voice_sum(right, active) : 0;
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

#ifdef FM1_HOST_TEST
int32_t orch_test_expression_q15(void)
{
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i)
        if (voices[i].active) return voices[i].expr_q15;
    return 0;
}
#endif

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
int orch_engine_set_adsr(unsigned part, unsigned attack_ms, unsigned decay_ms,
                         unsigned sustain_percent, unsigned release_ms)
{
    return orch_part_set_adsr(part, attack_ms, decay_ms, sustain_percent, release_ms);
}
int orch_engine_set_lfo(unsigned part, unsigned wave, unsigned destinations,
                        unsigned depth, unsigned rate_millihz,
                        unsigned sync_division, unsigned delay_ms, unsigned fade_ms)
{
    return orch_part_set_lfo(part, wave, destinations, depth, rate_millihz,
                             sync_division, delay_ms, fade_ms);
}
