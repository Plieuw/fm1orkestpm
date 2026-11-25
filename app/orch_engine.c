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
#include "orch_resonance.h"
#include "orch_cc.h"
#include "orch_fx.h"

#ifndef FM1_AUDIO_DATA
#define FM1_AUDIO_DATA __attribute__((section(".pool")))
#endif

#define ORCH_AUDIO FM1_AUDIO_CODE

/* Two lengths of delay line (0.7.1): sixteen voices would not fit the pool
 * at 2048 samples each, and only the low notes need that much. Voices 0..7
 * have 2048 (down to 21.5 Hz, the contrabassoon's and the timpani's
 * floor), 8..15 have 1024 (down to 43 Hz with a whole tone of bend to
 * spare). A note picks a voice its line can hold (pick_voice). */
#define DELAY_SIZE 2048U                 /* the long lines; note_len_q16's ceiling */
#define SHORT_SIZE 1024U
#define ORCH_LONG_VOICES 8U
#define BODY_SIZE 416U                   /* the lowest body, 110 Hz, is 401 samples */
#define EVENT_CAP 64U
#define ORCH_ENV_MAX 32767
#define Q23_MAX 8388607
#define Q23_MIN (-8388608)
#define Q23_ONE 8388608U
#define PART_NOTES 8U
#define SEQ_CHANNEL 16U

enum orch_excite { EXC_BOW = 0, EXC_BREATH, EXC_LIP, EXC_STRIKE, EXC_VOICE };

typedef struct {
    int16_t *delay;      /* its line: long_lines or short_lines (orch_init) */
    uint32_t dmask;      /* the line's length less one */
    int16_t body[BODY_SIZE];
    uint32_t write;
    uint16_t body_len, body_write;
    int32_t body_lp;
    uint32_t excite_phase, excite_inc;
    uint32_t len_q16, len_target_q16, len_step;
    uint32_t glide_left;
    uint32_t age, note_id;
    uint32_t legato_transition_left, legato_transition_total;
    uint32_t legato_transition_step_q16;
    uint32_t loop_gain_q23; /* per-sample gain derived from per-round-trip loss */
    int32_t lp;      /* previous loop sample (the damping filter's state) */
    int32_t dc;      /* output DC blocker state */
    int32_t exc_dc;  /* running mean of the friction force */
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
    uint16_t bow_gain;   /* Q8 force scale, proportional to the string length */
    int16_t pan_l, pan_r;
    uint32_t rng;        /* its own noise, so two cores never share one generator */
    /* worked out once a chunk (voice_control), used every sample */
    uint32_t c_len, c_len_end;   /* the read's length, Q16, and where the line goes */
    int32_t c_len_step;
    int32_t c_amp, c_amp_end, c_amp_step;   /* the output gain, Q15 x 2^8 */
    int32_t c_b_q15, c_exc_gain;
    int32_t bow_speed, bow_fmax;  /* the bow, for this note (voice_start) */
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
static int16_t long_lines[ORCH_LONG_VOICES][DELAY_SIZE] FM1_AUDIO_DATA;
static int16_t short_lines[ORCH_MAX_VOICES - ORCH_LONG_VOICES][SHORT_SIZE] FM1_AUDIO_DATA;
static event_t events[EVENT_CAP];
static part_runtime_t runtime[ORCH_PARTS];
static volatile uint8_t event_write, event_read;
static uint8_t sustain[ORCH_MIDI_CHANNELS];
static struct orch_step_note pattern[ORCH_PARTS][ORCH_SEQ_STEPS][ORCH_STEP_NOTES];
static uint8_t pattern_count[ORCH_PARTS][ORCH_SEQ_STEPS];
static volatile uint8_t seq_length = ORCH_SEQ_STEPS, seq_lane_muted[ORCH_PARTS];
static uint32_t seq_rng = 0x2545F491U;
static uint8_t seq_note[ORCH_PARTS][ORCH_STEP_NOTES], seq_active[ORCH_PARTS][ORCH_STEP_NOTES];
static uint32_t seq_note_id[ORCH_PARTS][ORCH_STEP_NOTES];
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
static uint32_t seq_off_at[ORCH_PARTS][ORCH_STEP_NOTES];
static uint32_t age_counter, sample_counter, step_start;

static void seq_learn_input(unsigned part, unsigned note, unsigned velocity);
static volatile uint16_t tempo = ORCH_DEFAULT_BPM;
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
    switch (wave) {
    case ORCH_LFO_TRIANGLE:
        return triangle32(phase);
    case ORCH_LFO_SAW:
        return (int32_t)(phase >> 16) - 32768;
    case ORCH_LFO_SQUARE:
        return (phase & 0x80000000U) ? 32767 : -32768;
    case ORCH_LFO_SAMPLE_HOLD:
        return (int32_t)(*hold >> 16) - 32768;
    case ORCH_LFO_SINE:
    default: {
        const int32_t tri = triangle32(phase);
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

/* A voice's own noise: what runs per sample uses only this, so a voice
 * renders the same on either core and the two never write one state */
static int32_t noise_v(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return (int16_t)(x >> 16);
}
/* Semitone ratio table from A, Q16. */
static const uint32_t RATIO_A[12] = {
    65536U, 69433U, 73562U, 77936U, 82570U, 87480U,
    92682U, 98209U, 104048U, 110245U, 116802U, 123743U
};

/* 2^shift / d without a 64-bit divide (the PI32v2 has no __udivdi3): shift and
 * subtract, 48 steps, note-on only. */
static uint32_t div_pow2(unsigned shift, uint32_t d)
{
    if (!d) d = 1U;
    uint64_t rem = 0U;
    uint32_t q = 0U;
    for (int bit = (int)shift; bit >= 0; --bit) {
        rem = (rem << 1) | (bit == (int)shift ? 1U : 0U);
        if (rem >= d) { rem -= d; if (bit < 32) q |= 1U << bit; }
    }
    return q;
}

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
    {31700U,31200U,500U,3U,90U,520U,62U,0U,0U,5U,0U,10U}, /* choir */
    /* 0.7.1 */
    {32050U,31250U,700U,3U,34U,260U,50U,1U,0U,4U,0U,36U}, /* english horn: a darker oboe */
    {32200U,31300U,300U,3U,34U,280U,50U,1U,0U,2U,0U,30U}, /* bass clarinet */
    {32300U,31400U,160U,3U,48U,360U,58U,1U,0U,1U,0U,28U}, /* contrabassoon */
    {32050U,31000U,700U,3U,30U,220U,38U,1U,0U,3U,0U,46U}, /* alto flute: breathier */
    {32200U,31200U,300U,3U,40U,300U,62U,1U,0U,2U,0U,22U}, /* bass trombone */
    {32500U,29000U,2600U,2U,1U,1400U,70U,0U,1U,0U,0U,80U}, /* glockenspiel: long, bright */
    {32550U,29500U,1100U,2U,2U,1800U,72U,0U,1U,0U,0U,70U}, /* vibraphone: rings on */
    {32600U,30000U,400U,2U,2U,2500U,78U,0U,1U,0U,0U,60U}, /* tubular bells */
    {32300U,29200U,2000U,2U,1U,900U,74U,0U,1U,0U,0U,76U}, /* celesta */
    {32450U,31100U,900U,4U,2U,700U,58U,0U,1U,0U,0U,6U},  /* harpsichord: a bright quill */
    {31700U,31200U,700U,3U,80U,520U,60U,0U,0U,5U,0U,10U}, /* women's choir */
    {31700U,31250U,350U,3U,95U,560U,64U,0U,0U,4U,0U,10U}  /* men's choir */
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
 * (Ported from beta/0.4.0-podium-light, commit 6ec0249.)
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

/* One global pool of sixteen mono voices. Parts do not reserve voices. A
 * note that needs a long line (need_long) looks only among voices 0..7;
 * any other looks at the short ones first, so the long ones stay free for
 * the low notes, then at the long ones. In that order: a free voice wins;
 * else the quietest release tail is taken; else the oldest held note (last
 * note priority: the newest music is kept). */
static unsigned pick_voice(unsigned part, int need_long)
{
    (void)part;
    int quietest = -1;
    int oldest = -1;
    int32_t quiet_env = 0x7fffffff;
    uint32_t oldest_age = UINT32_MAX;
    const unsigned count = need_long ? ORCH_LONG_VOICES : ORCH_MAX_VOICES;
    for (unsigned k = 0; k < count; ++k) {
        /* short voices first unless the note needs a long line */
        const unsigned i = need_long ? k
            : (k < ORCH_MAX_VOICES - ORCH_LONG_VOICES ? ORCH_LONG_VOICES + k
                                                      : k - (ORCH_MAX_VOICES - ORCH_LONG_VOICES));
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
static int find_legato_voice(unsigned part, unsigned player, unsigned family, int need_long)
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
        if (need_long && v->dmask + 1U < DELAY_SIZE) continue;   /* its line is too short */
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
    struct artic_profile prof;
    artic_profile(in->articulation, family, &prof);

    /* how long a line the note needs: its loop with a whole tone of bend
     * and the vibrato to spare (1/8 more, and 16 samples) */
    int need_long;
    {
        unsigned sounding = note + prof.semis;
        if (sounding > 127U) sounding = 127U;
        uint32_t need = note_len_q16(sounding);
        if (ip->half_period) need >>= 1;
        need_long = (need >> 16) + (need >> 19) + 16U > SHORT_SIZE - 8U;
    }
    const int legato_slot = (in->connected && family != ORCH_FAM_MALLETS)
                               ? find_legato_voice(part, player, family, need_long) : -1;
    const unsigned i = legato_slot >= 0 ? (unsigned)legato_slot : pick_voice(part, need_long);
    const int legato_reuse = legato_slot >= 0;
    voice_t *v = &voices[i];
    const uint32_t line_max_q16 = (v->dmask - 7U) << 16;   /* the line's size less 8 */
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
        for (unsigned n = 0; n <= v->dmask; ++n) v->delay[n] = 0;
        v->write = 0U;
        v->lp = 0;
        v->dc = 0;
        v->exc_dc = 0;
    }
    v->age = ++age_counter;
    (void)noise16();
    v->rng = rng ^ (v->age * 0x9E3779B9U);
    if (!v->rng) v->rng = 0x6d2b79f5U;
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
    {
        /* the bow's speed and force for this note (worked out here once, not
         * every sample): soft playing is a light, slightly faster stroke;
         * the force rises with the speed squared, as a player's does */
        const struct instrument_profile *bp = &INST_PROFILE[instrument < ORCH_INST_COUNT ? instrument : 0U];
        const int32_t soft = velocity < 64U ? (int32_t)(64U - velocity) * 110 : 0;
        const int32_t vsq = (int32_t)velocity * (int32_t)velocity;
        v->bow_speed = ((int32_t)velocity + 12) * 150 + soft;
        v->bow_fmax = ((int32_t)bp->bow_pressure * (48 + (vsq >> 6))) >> 7;
    }
    if (!legato_reuse) {
        v->c_len = 0U;            /* the first chunk starts its lines here */
        v->c_amp = 0;
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
        v->legato_transition_step_q16 = v->legato_transition_total
            ? (uint32_t)(((uint64_t)9174U << 16) / v->legato_transition_total)
            : 0U;
    } else {
        v->legato_transition_left = 0U;
        v->legato_transition_total = 0U;
        v->legato_transition_step_q16 = 0U;
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
    if (target > line_max_q16) target = line_max_q16;

    uint32_t start = target;
    v->glide_left = 0U;
    if (legato_reuse) start = v->len_q16;
    if (in->connected && in->glide_ms && runtime[part].last_len_q16) {
        if (!legato_reuse) start = runtime[part].last_len_q16;
        v->glide_left = ((uint32_t)in->glide_ms * ORCH_SAMPLE_RATE) / 1000U;
    }
    if (start > line_max_q16) start = line_max_q16;   /* a glide from below the line */
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
        if (v->body_len > BODY_SIZE) v->body_len = BODY_SIZE;
        v->body_write = 0U; v->body_lp = 0;
        for (unsigned n = 0; n < v->body_len; ++n) v->body[n] = 0;
        v->excite_phase = rng ^ (note * 0x9e3779b9U);
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
    /* The excitation oscillator (jet, lip, voice) runs one cycle a period:
     * 2^32 / period, the period in Q16 samples (twice the loop for the
     * half-period bores). 0.5.0 divided by the whole samples only, which
     * pulled every wind, brass and choir note to SR/whole: +23 c at E5,
     * measured. Note-on only, so the 64-bit divide is not in the sample loop. */
    v->excite_inc = div_pow2(ip->half_period ? 47U : 48U, target);

    /* a stroke ends by itself; a pluck rings with the gate already closed */
    v->stroke_left = prof.stroke_ms
                         ? (uint32_t)prof.stroke_ms * ORCH_SAMPLE_RATE / 1000U
                         : 0U;

    /* the bow bite: a few milliseconds of rosin to set the string in motion,
     * after which the friction holds it. Measured without it, a bowed note
     * swells for nearly a second before it reaches its level. A legato note
     * keeps the moving string and gets no new bite. */
    if (excite == EXC_BOW && !legato_reuse && !prof.pluck) {
        v->strike_left = (uint16_t)((10U * ORCH_SAMPLE_RATE) / 1000U);
        const int32_t bite = (int32_t)velocity * 90;
        for (unsigned n = 0; n < 24U; ++n)
            v->delay[n] = (int16_t)clamp16((noise16() * bite) >> 8);
    }

    /* the strike: a short noise burst through the bridge */
    if (excite == EXC_STRIKE || prof.pluck) {
        v->struck = 1U;
        v->strike_left = (uint16_t)(((family == ORCH_FAM_MALLETS) ? 260U : 180U) *
                                    ORCH_SAMPLE_RATE / 1000U);
        int32_t amp = (int32_t)velocity * 160 + (int32_t)prof.accent * 60;
        if (v->bow_gain < 256U) amp = (amp * (int32_t)v->bow_gain) >> 8;  /* as above */
        for (unsigned n = 0; n < 64U; ++n)
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

/* ------------------------------------------------------------- the tempo
 *
 * One tempo for the whole orchestra: the sequencer, every ARP, the delay.
 * Where it comes from, in order:
 *   1. MIDI clock: while ticks arrive (half a second without one ends it),
 *      the tempo is the clock's, measured over each beat of 24 ticks;
 *   2. otherwise the playing: the time between the notes played in (chords
 *      counted once, the ARP's and the sequencer's own notes not at all) is
 *      folded into the beat's octave, and when two in a row agree within
 *      12 % the tempo moves a quarter of the way there. It follows, it does
 *      not jump: rubato bends it a little, a steady line takes it along;
 *   3. at power on, and until either says otherwise, ORCH_DEFAULT_BPM.
 * The panel's TEMPO sets it too when no clock runs, and shows wherever it
 * went; while a clock runs the panel shows the clock's and cannot move it. */
static uint32_t clock_last, clock_beat_start, clock_age;
static uint8_t clock_ticks, clock_alive;
static uint32_t follow_last;      /* sample of the last onset, 0 = none */
static uint32_t follow_prev_q4;   /* the previous interval's tempo, Q4 */
static uint32_t follow_q4;        /* the followed tempo, Q4 */

static void tempo_reset(void)
{
    clock_last = clock_beat_start = clock_age = 0U;
    clock_ticks = clock_alive = 0U;
    follow_last = follow_prev_q4 = 0U;
    follow_q4 = (uint32_t)tempo << 4;
}

static void tempo_apply(unsigned bpm)
{
    if (bpm < 40U) bpm = 40U;
    if (bpm > 250U) bpm = 250U;
    if (bpm == tempo) return;
    tempo = (uint16_t)bpm;
    orch_perf_set_tempo(bpm);
    orch_fx_set_tempo(bpm);
}

static void tempo_clock_tick(void)
{
    const uint32_t now = sample_counter;
    clock_age = 0U;
    if (!clock_alive) {
        clock_alive = 1U;
        clock_ticks = 0U;
        clock_beat_start = now;
    }
    clock_last = now;
    if (++clock_ticks >= 24U) {
        const uint32_t span = now - clock_beat_start;   /* one beat */
        clock_ticks = 0U;
        clock_beat_start = now;
        if (span >= 10584U && span <= 66150U) {         /* 250..40 BPM */
            const unsigned bpm = (2646000U + span / 2U) / span;
            /* a new tempo is taken at once; a beat's jitter (the clock is
             * read once a block) is halved away */
            const unsigned d = bpm > tempo ? bpm - tempo : tempo - bpm;
            tempo_apply(d > 3U ? bpm : (tempo + bpm + 1U) / 2U);
            follow_q4 = (uint32_t)tempo << 4;
        }
    }
}

/* once per sample: the clock ends half a second after its last tick */
static void tempo_tick(void)
{
    if (clock_alive && ++clock_age > ORCH_SAMPLE_RATE / 2U) {
        clock_alive = 0U;
        follow_last = follow_prev_q4 = 0U;
        follow_q4 = (uint32_t)tempo << 4;
    }
}

static void tempo_follow_onset(void)
{
    if (clock_alive) return;
    const uint32_t now = sample_counter;
    if (!follow_last) { follow_last = now; return; }
    const uint32_t ioi = now - follow_last;
    if (ioi < ORCH_SAMPLE_RATE / 10U) return;        /* the rest of a chord */
    follow_last = now;
    if (ioi > ORCH_SAMPLE_RATE * 2U) { follow_prev_q4 = 0U; return; }   /* a rest */
    /* the interval as a tempo, folded into the beat's octave around the
     * current one: eighths and halves read as the same pulse */
    uint32_t cand = (2646000U * 16U) / ioi;
    while (cand * 1000U > follow_q4 * 1414U) cand >>= 1;
    while (cand * 1414U < follow_q4 * 1000U) cand <<= 1;
    const uint32_t prev = follow_prev_q4;
    follow_prev_q4 = cand;
    if (!prev) return;
    const uint32_t diff = cand > prev ? cand - prev : prev - cand;
    if (diff * 100U > cand * 12U) return;            /* not yet a pulse */
    follow_q4 = (uint32_t)((int32_t)follow_q4 + ((int32_t)cand - (int32_t)follow_q4) / 4);
    tempo_apply((follow_q4 + 8U) >> 4);
}

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
        else if (m[1] == 120U || m[1] >= 123U)   /* sound off, notes off, modes */
            enqueue(ch, 0U, 0U, 5U);
        else
            enqueue(ch, m[1], m[2], 6U);   /* the rest: orch_cc.c */
    } else if (kind == 0xc0U)
        enqueue(ch, 0U, m[1], 7U);         /* program change */
    else if (kind == 0xe0U && bytes >= 3U)
        enqueue(ch, m[1], m[2], 8U);       /* pitch bend, LSB then MSB */
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

/* Shared realization path for live MIDI and generated ARP/sequencer notes.
 * Keeping adaptive analysis, note IDs, divisi planning and voice handover in
 * one place prevents the two event sources from drifting apart. */
static uint32_t engine_start_part_note(unsigned part, unsigned note, unsigned velocity)
{
    if (part >= ORCH_PARTS || note > 127U || !velocity || velocity > 127U)
        return 0U;
    struct orch_part *cfg = orch_part_get(part);
    if (!cfg) return 0U;

    part_runtime_t *rt = &runtime[part];
    struct orch_intent *in = &rt->intent;
    /* the adaptive layer hears how hard the part is played: the velocity
     * under the breath and expression controllers (orch_cc_dynamic) */
    const unsigned played = orch_cc_dynamic(part, velocity);
    orch_adaptive_note(&cfg->adaptive, in, note, played, sample_counter,
                       cfg->articulation);
    orch_adaptive_velocity(&cfg->adaptive, in, played);

    uint32_t id = 0U;
    if (part_note_add(part, note, &id) < 0) return 0U;

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

    unsigned started = 0U;
    for (unsigned p = 0; p < rt->plan.players; ++p) {
        const unsigned idx = rt->plan.assignment[p];
        if (idx >= count || ids[idx] != id) continue;
        voice_start(part, note, velocity, id, in, p,
                    rt->plan.detune_cents[p], rt->plan.pan[p]);
        ++started;
    }
    /* Hysteresis may temporarily leave the newest connected note unassigned.
     * Always let its lead desk continue the phrase instead of dropping it. */
    if (!started && in->connected) {
        const unsigned p = rt->plan.players ? rt->plan.players - 1U : 0U;
        voice_start(part, note, velocity, id, in, p,
                    rt->plan.detune_cents[p], rt->plan.pan[p]);
    }
    return id;
}

static void engine_note_on_part(unsigned part, unsigned note, unsigned velocity)
{
    (void)engine_start_part_note(part, note, velocity);
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
    const uint32_t rel = cfg ? orch_adaptive_off(&cfg->adaptive, sample_counter)
                             : (uint32_t)ORCH_Q16;

    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        voice_t *v = &voices[i];
        if (!v->active || v->part != part || v->note_id != id) continue;
        if (rel != (uint32_t)ORCH_Q16 && v->gate) {
            /* the phrase decides how this note ends (orch_adaptive_off) */
            uint32_t f = (uint32_t)(((uint64_t)v->release_frames * rel) >> 16);
            v->release_frames = f ? f : 1U;
        }
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
        response_velocity = 81U;   /* 0.6.3's 54, on the even line */
        response_on_at = sample_counter + quarter;
        response_off_at = response_on_at + quarter * 2U;
    } else {
        response_velocity = 94U;   /* 0.6.3's 72 */
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
            if (e.note == 0xF8U) tempo_clock_tick();
            orch_perf_midi_realtime(e.note,sample_counter);
        } else if (e.type == 4U) {
            /* the platform measured the clock already: take its tempo */
            const unsigned bpm = (unsigned)e.note | ((unsigned)e.velocity<<7);
            orch_perf_set_external_tempo(bpm);
            if (clock_alive) { tempo_apply(bpm); follow_q4 = (uint32_t)tempo << 4; }
        } else if (e.type == 1U) {
            engine_sustain(e.channel, e.velocity);
        } else if (e.type >= 6U && e.type <= 8U) {
            /* a part's controllers; the global ones (resonance, effects)
             * answer even a channel no part listens to */
            const unsigned part = orch_part_for_channel(e.channel);
            if (e.type == 8U)
                orch_cc_apply(part, ORCH_CC_BEND,
                              (unsigned)e.note | ((unsigned)e.velocity << 7));
            else
                orch_cc_apply(part, e.type == 7U ? ORCH_CC_PROGRAM : e.note, e.velocity);
        } else if (e.type == 5U) {
            sustain[e.channel] = 0U;
            for (unsigned n = 0; n < 128U; ++n)
                engine_note_off(e.channel, n, 0U);
        } else {
            const unsigned part=orch_part_for_channel(e.channel);
            /* the ears (sequencer, call and response, ARP) hear the played
             * dynamic; the instrument gets the velocity */
            const unsigned played = part != 0xFFU ? orch_cc_dynamic(part, e.velocity) : e.velocity;
            if (part != 0xFFU && e.velocity) tempo_follow_onset();
            if (part != 0xFFU) seq_learn_input(part, e.note, played);
            if (part != 0xFFU && e.velocity) response_schedule(part,e.note,played);
            if (!orch_perf_note_event_played(part,e.note,e.velocity,played)) {
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

static void seq_clear_lane(unsigned lane)
{
    for (unsigned st = 0; st < ORCH_SEQ_STEPS; ++st) {
        pattern_count[lane][st] = 0U;
        for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) {
            pattern[lane][st][n].note = 60U;
            pattern[lane][st][n].velocity = 0U;
            pattern[lane][st][n].gate_percent = 80U;
            pattern[lane][st][n].chance = 100U;
        }
    }
}

void orch_seq_clear(void) { seq_clear_lane(seq_part); }

void orch_seq_clear_all(void)
{
    for (unsigned l = 0; l < ORCH_PARTS; ++l) seq_clear_lane(l);
}

void orch_seq_set_part(unsigned part)
{
    seq_part = (uint8_t)(part < ORCH_PARTS ? part : 0U);
}

void orch_seq_set_length(unsigned steps)
{
    if (steps < 1U) steps = 1U;
    if (steps > ORCH_SEQ_STEPS) steps = ORCH_SEQ_STEPS;
    seq_length = (uint8_t)steps;
}

unsigned orch_seq_length(void) { return seq_length; }

void orch_seq_set_lane_muted(unsigned part, unsigned muted)
{
    if (part < ORCH_PARTS) seq_lane_muted[part] = (uint8_t)(muted ? 1U : 0U);
}

unsigned orch_seq_lane_muted(unsigned part)
{
    return part < ORCH_PARTS ? seq_lane_muted[part] : 0U;
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

/* Mix gain by the number of voices sounding, Q12: 1.36 / n, never above
 * unity. The headroom is shared out evenly: every sounding voice gets an
 * equal share. The share is sized by what one voice measurably reaches, not
 * by full scale: the loudest voice there is (a double bass C2 bowed at
 * velocity 127, whose friction peak varies from note to note) reaches
 * 5.0..5.6 M of the 8.39 M full scale, so n voices at 1.36 / n reach at most
 * n x 5.6 M x 1.36 / n = 7.6 M, all in phase, which separate notes never are.
 * No limiter, no knee, nothing bends the waveform. (0.6.1/0.6.2 used 1 / n,
 * which sized every share for a voice at full scale and left the full
 * orchestra near 3 M; a lone voice stays at unity, since 1.5 x on one double
 * bass reached full scale; 0.6.0 had 2.5 / sqrt(n) and a soft knee.)
 * tests/orch_engine_test.c test_gain_staging holds the bound.
 *
 * When the count changes the gain glides: down within about 1.5 ms (a new
 * voice takes its share quickly, so the sum stays within the bound) and up
 * over about 25 ms (a voice ending lets the others grow back gently). */
static const uint16_t MIX_GAIN_Q12[ORCH_MAX_VOICES + 1U] = {
    0U, 4096U, 2785U, 1857U, 1393U, 1114U, 928U, 796U, 696U,
    619U, 557U, 506U, 464U, 428U, 398U, 371U, 348U
};
static int32_t mix_gain_q12 = 4096;

/* Integer saturation only, for the arithmetic's sake: at 1.36 / n the sum stays
 * within full scale (the expression gain can lift a voice a few percent over
 * unity, so the clamp stays as a guard). */
static int32_t clamp_q23(int32_t x)
{
    if (x > Q23_MAX) return Q23_MAX;
    if (x < Q23_MIN) return Q23_MIN;
    return x;
}

/* zero a static block without libc (no memset in the firmware's app
 * objects) and without a zero copy of it in flash */
static void zero_words(void *p, unsigned bytes)
{
    uint32_t *w = (uint32_t *)p;
    for (unsigned i = 0; i < bytes / 4U; ++i) w[i] = 0U;
}

void orch_init(void)
{
    orch_resonance_clear();
    orch_cc_init();
    mix_gain_q12 = (int32_t)MIX_GAIN_Q12[1];
    /* every voice and part back to nothing: a fresh start sounds the same
     * every time (the two-core test compares runs bit for bit) */
    zero_words(voices, sizeof voices);
    zero_words(runtime, sizeof runtime);
    zero_words(long_lines, sizeof long_lines);
    zero_words(short_lines, sizeof short_lines);
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        if (i < ORCH_LONG_VOICES) {
            voices[i].delay = long_lines[i];
            voices[i].dmask = DELAY_SIZE - 1U;
        } else {
            voices[i].delay = short_lines[i - ORCH_LONG_VOICES];
            voices[i].dmask = SHORT_SIZE - 1U;
        }
    }
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) voice_stop(i);
    event_read = event_write = 0U;
    for (unsigned c = 0; c < ORCH_MIDI_CHANNELS; ++c) sustain[c] = 0U;
    orch_parts_init();
    part_notes_reset();
    orch_seq_clear_all();
    orch_perf_init();
    tempo = ORCH_DEFAULT_BPM;
    tempo_reset();
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
    age_counter = 0U;               /* a fresh start sounds the same every time */
    rng = 0x6d2b79f5U;
    seq_rng = 0x2545F491U;
    for (unsigned l = 0; l < ORCH_PARTS; ++l) {
        seq_lane_muted[l] = 0U;
        for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) {
            seq_active[l][n] = 0U; seq_off_at[l][n] = 0U;
            seq_note[l][n] = 0U; seq_note_id[l][n] = 0U;
        }
    }
    seq_length = ORCH_SEQ_STEPS;
}

void orch_seq_set_tempo(uint16_t bpm)
{
    if (clock_alive) return;          /* the MIDI clock always has the say */
    if (bpm < 40U) bpm = 40U;
    if (bpm > 250U) bpm = 250U;
    tempo = bpm;
    follow_q4 = (uint32_t)bpm << 4;
}

unsigned orch_tempo_from_clock(void) { return clock_alive; }

unsigned orch_seq_tempo(void) { return tempo; }
unsigned orch_seq_enabled(void) { return seq_enabled; }

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
    const unsigned l = seq_part;
    pattern[l][step][slot] = *n;
    /* a chance outside 1..99 plays always (a caller written before the field
     * existed may leave it unset) */
    if (!n->chance || n->chance > 100U) pattern[l][step][slot].chance = 100U;
    if (n->velocity && pattern_count[l][step] <= slot) pattern_count[l][step] = (uint8_t)(slot + 1U);
    else if (!n->velocity && pattern_count[l][step] == slot + 1U)
        while (pattern_count[l][step] && !pattern[l][step][pattern_count[l][step] - 1U].velocity)
            --pattern_count[l][step];
    return 0;
}

/* Sequencer notes enter the same part path as MIDI, each lane on its part. */
static void seq_release_voice(unsigned part, unsigned note, uint32_t note_id)
{
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

static uint32_t seq_start_voice(unsigned part, unsigned note, unsigned velocity)
{
    return engine_start_part_note(part, note, velocity);
}

/* The ARP/phrase engine's 16-step orchestral phrase arc, in velocity steps
 * on the even line (orch_performance.c PHRASE_ARC_STEPS): the same swell in
 * dB at every dynamic, -6.9 .. +3.2 dB. */
static const int8_t seq_phrase_arc[16] = {
    -14, -10, -7, -4, 1, 5, 8, 11, 12, 10, 5, -1, -7, -14, -21, -26
};

static unsigned seq_arc_velocity(int velocity, unsigned position)
{
    const int value = velocity + seq_phrase_arc[position & 15U];
    return value < 1 ? 1U : (value > 127 ? 127U : (unsigned)value);
}

#ifdef FM1_HOST_TEST
unsigned orch_test_seq_phrase_arc_velocity(unsigned velocity, unsigned position)
{
    return seq_arc_velocity((int)velocity, position);
}
#endif

static void seq_tick(void)
{
    if (!seq_enabled) {
        if (seq_stop_pending) {
            for (unsigned l = 0; l < ORCH_PARTS; ++l)
                for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n)
                    if (seq_active[l][n]) {
                        seq_release_voice(l, seq_note[l][n], seq_note_id[l][n]);
                        seq_active[l][n] = 0U;
                    }
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
    const unsigned length = seq_length ? seq_length : ORCH_SEQ_STEPS;
    if (!seq_started) {
        seq_started = 1U;
        current_step = (uint8_t)(length - 1U);
        step_start = sample_counter - step_samples;
    }
    for (unsigned l = 0; l < ORCH_PARTS; ++l)
        for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) {
            if (seq_active[l][n] && (int32_t)(sample_counter - seq_off_at[l][n]) >= 0) {
                seq_release_voice(l, seq_note[l][n], seq_note_id[l][n]);
                seq_active[l][n] = 0U;
            }
        }
    if ((uint32_t)(sample_counter - step_start) < step_samples) return;
    step_start += step_samples;
    current_step = (uint8_t)((current_step + 1U) % length);
    for (unsigned l = 0; l < ORCH_PARTS; ++l) {
        if (seq_lane_muted[l]) continue;
        for (unsigned n = 0; n < pattern_count[l][current_step]; ++n) {
            const struct orch_step_note *p = &pattern[l][current_step][n];
            if (!p->velocity) continue;
            /* chance: a step below 100 % plays that share of the passes */
            if (p->chance && p->chance < 100U) {
                seq_rng = seq_rng * 1664525U + 1013904223U;
                if ((seq_rng >> 25) % 100U >= p->chance) continue;
            }
            /* In adaptive mode, keep the user's rhythm and step data, but
             * voice chord tones from the live harmony and thin inner voices
             * during quiet/sparse playing. Slot zero remains the anchor. */
            const unsigned have_input = seq_input_have_note[l] &&
                (uint32_t)(sample_counter - seq_input_last_on[l]) < ORCH_SAMPLE_RATE * 2U;
            if (seq_adaptive && have_input && n > 0U &&
                (seq_input_activity[l] < 82U || seq_input_velocity[l] < 46U) &&
                (current_step & 3U) != 0U) continue;
            const unsigned played_note = seq_adapt_note(l, p->note);
            unsigned velocity = p->velocity;
            if (seq_adaptive && have_input) {
                const int delta = ((int)seq_input_velocity[l] - 80) / 5;
                int adjusted = (int)velocity + delta;
                if ((current_step & 3U) == 0U) adjusted += 5;
                if (adjusted < 1) adjusted = 1;
                if (adjusted > 127) adjusted = 127;
                /* A full 16-step arc gives the line a rise, peak and release;
                 * it changes playback velocity only, never stored step data. */
                velocity = seq_arc_velocity(adjusted, current_step);
            }
            if (seq_active[l][n]) seq_release_voice(l, seq_note[l][n], seq_note_id[l][n]);
            seq_note_id[l][n] = seq_start_voice(l, played_note, velocity);
            seq_note[l][n] = (uint8_t)played_note;
            seq_active[l][n] = 1U;
            /* Preserve the stored gate as the base; adaptive activity adds
             * legato space for relaxed input and shorter articulation when
             * the player is moving quickly. */
            unsigned gate = p->gate_percent > 100U ? 100U : p->gate_percent;
            if (seq_adaptive && have_input) {
                unsigned adaptive_gate = 82U;
                if (seq_input_activity[l] < 82U) adaptive_gate = 94U;
                else if (seq_input_activity[l] > 135U) adaptive_gate = 68U;
                gate = (gate * 3U + adaptive_gate) / 4U;
                gate = (gate * (unsigned)seq_phrase_arc[current_step] + 50U) / 100U;
            }
            if (gate < 8U) gate = 8U;
            if (gate > 100U) gate = 100U;
            seq_off_at[l][n] = sample_counter + (step_samples * gate) / 100U;
        }
    }
}

/* --------------------------------------------------------------- render */

/* Return the part's LFO destination mask so the mixer can reuse the same
 * config lookup. This function is the per-sample hot path: resolve the part,
 * LFO and instrument profile once, not separately for each modulation stage. */
/* ------------------------------------------------- a voice, in two rates
 *
 * What moves slowly is worked out once a chunk (32 samples, 1378 times a
 * second): the pitch's glide, bend, vibrato and LFO, the phrase expression,
 * the tremolo, the excitation's strength and the damping. The loop length
 * and the output gain are then drawn as straight lines across the chunk, so
 * no modulation steps; the waveguide, the excitation, the body, the
 * followers and the envelope run every sample. Before 0.7.0 all of it ran
 * every sample, about 415 instructions a voice-sample on the host, most of
 * them recomputing what had hardly changed. */

/* once a chunk of n samples: the slow parts, and the lines for the chunk */
static void voice_control(voice_t *v, unsigned n)
{
    const struct orch_part *cfg = orch_part_get(v->part);
    const struct orch_lfo *lfo = cfg ? &cfg->lfo : 0;
    const unsigned lfo_destinations = lfo ? lfo->destinations : 0U;

    /* the glide, n samples on */
    uint32_t len = v->len_q16;
    if (v->glide_left) {
        const uint32_t k = v->glide_left < n ? v->glide_left : n;
        if (v->len_step & 0x80000000U) {
            const uint32_t step = (0U - v->len_step) * k;
            v->len_q16 = v->len_q16 > step + 16U ? v->len_q16 - step : 16U;
        } else {
            v->len_q16 += v->len_step * k;
        }
        v->glide_left -= k;
        len = v->len_q16;
    }
    /* pitch bend (orch_cc.c): the loop's length by the part's ratio */
    if (orch_cc_bend_q16[v->part] != 65536) {
        uint64_t bent = ((uint64_t)len * (uint32_t)orch_cc_bend_q16[v->part]) >> 16;
        if (bent > ((uint64_t)(v->dmask - 7U) << 16)) bent = (uint64_t)(v->dmask - 7U) << 16;
        if (bent < (16U << 16)) bent = 16U << 16;
        len = (uint32_t)bent;
    }
    /* phrase-shaped sustain expression, about 0.14-0.30 Hz, while the note
     * sustains or releases; its depth is the family's */
    if (v->expr_depth && (v->env_stage == 3U || v->env_stage == 4U)) {
        v->expr_phase += v->expr_inc * n;
        int32_t tri = triangle32(v->expr_phase);
        const int32_t mag = tri < 0 ? -tri : tri;
        const int32_t sine = (tri * (49152 - (mag >> 1))) >> 15;
        v->expr_q15 = (sine * (int32_t)v->expr_depth) >> 15;
    } else {
        v->expr_q15 = 0;
    }

    /* the part's LFO; the modulation wheel (CC1) is vibrato on it, as on a
     * Yamaha, and runs it even where the part has none set */
    int32_t tremolo_q15 = 32767;
    {
        const unsigned wheel = (unsigned)(orch_cc_mod_q8[v->part] >> 8);
        if (lfo && ((lfo_destinations && v->lfo_depth) || wheel)) {
            if (v->lfo_delay_left) {
                v->lfo_delay_left = v->lfo_delay_left > n ? v->lfo_delay_left - n : 0U;
            } else {
                const uint32_t old_phase = v->lfo_phase;
                v->lfo_phase += v->lfo_inc * n;
                if (lfo->wave == ORCH_LFO_SAMPLE_HOLD && v->lfo_phase < old_phase)
                    v->lfo_hold = v->lfo_hold * 1664525U + 1013904223U;
                if (v->lfo_fade_gain < 32767U) {
                    const uint32_t step = v->lfo_fade_frames
                        ? (32767U / v->lfo_fade_frames) + 1U : 32767U;
                    v->lfo_fade_gain += step * n;
                    if (v->lfo_fade_gain > 32767U) v->lfo_fade_gain = 32767U;
                }
                int32_t value = lfo_wave_value(lfo->wave, v->lfo_phase, &v->lfo_hold);
                value = (value * (int32_t)v->lfo_fade_gain) >> 15;
                v->lfo_value = (int16_t)value;
                const int32_t pitch_depth =
                    ((lfo->destinations & ORCH_LFO_PITCH) ? (int32_t)v->lfo_depth : 0) +
                    (int32_t)wheel;
                if (pitch_depth) {
                    const int32_t cents = (pitch_depth * 50) / 100;
                    const int32_t delta = (int32_t)(((int64_t)len * cents * 38 * value) >> 31);
                    const int32_t modulated = (int32_t)len + delta;
                    if (modulated > 16) len = (uint32_t)modulated;
                }
            }
            if (lfo_destinations & ORCH_LFO_TREMOLO) {
                const int32_t depth = (int32_t)v->lfo_depth * 245 / 100;
                tremolo_q15 = 32767 - ((v->lfo_value + 32768) * depth >> 16);
            }
        } else {
            v->lfo_value = 0;
        }
    }

    /* vibrato: delayed, then growing; a triangle on the loop length */
    if (v->vib_depth_max) {
        if (v->vib_delay) {
            v->vib_delay = v->vib_delay > n ? v->vib_delay - n : 0U;
        } else {
            v->vib_phase = (uint16_t)(v->vib_phase + v->vib_inc * n);
            if (v->vib_depth < v->vib_depth_max) {
                v->vib_depth += v->vib_ramp * n;
                if (v->vib_depth > v->vib_depth_max) v->vib_depth = v->vib_depth_max;
            }
        }
        const int32_t tri = (int32_t)(v->vib_phase >> 7) - 256; /* -256..255 */
        const int32_t delta = (tri * (int32_t)v->vib_depth) >> 8;
        const int32_t modulated = (int32_t)len + delta;
        if (modulated > 16) len = (uint32_t)modulated;
    }

    /* the excitation's strength: the envelope, the phrase expression, the
     * breath controller and a legato handover */
    const int32_t env_q15 = v->env < 0 ? 0 :
        (v->env > ORCH_ENV_MAX ? ORCH_ENV_MAX : v->env);
    const int32_t transient_q15 = (v->env_stage == 1U)
        ? (ORCH_ENV_MAX - env_q15) : 0;
    const int32_t env_excitation_q15 = 16384 + (env_q15 >> 1);
    const int32_t expression_gain_q15 = 32767 + v->expr_q15;
    int32_t excitation_gain_q15 =
        (env_excitation_q15 * (expression_gain_q15 >> 1)) >> 14;
    excitation_gain_q15 = (excitation_gain_q15 * orch_cc_excite_q15[v->part]) >> 15;
    if (v->legato_transition_left) {
        const uint32_t total = v->legato_transition_total ? v->legato_transition_total : 1U;
        const uint32_t left = v->legato_transition_left;
        const uint32_t elapsed = total > left ? total - left : 0U;
        /* 72% -> 100% smooth excitation handover */
        const int32_t blend = 23593 +
            (int32_t)((elapsed * v->legato_transition_step_q16) >> 16);
        excitation_gain_q15 = (excitation_gain_q15 * blend) >> 15;
        v->legato_transition_left = left > n ? left - n : 0U;
    }
    v->c_exc_gain = excitation_gain_q15;

    /* Loop damping as a first-order FIR, y = x - b (x - x[-1]): DC passes
     * whole, Nyquist at 1 - 2b, and its delay at low frequencies is b
     * samples. b moves with the profile's damping, the attack transient, the
     * energy follower, CC74 and the LFO. damp_q8 is 1.0 dark .. 6.0 bright. */
    int32_t damp_q8 = (int32_t)(v->damp ? v->damp : 2U) << 8;
    damp_q8 += (transient_q15 >> 5) + (v->excite_follow >> 6);
    damp_q8 += orch_cc_bright_q8[v->part];               /* CC74 */
    if (lfo_destinations & ORCH_LFO_BRIGHTNESS) {
        const int bounded_depth = v->lfo_depth > 24U ? 24 : (int)v->lfo_depth;
        damp_q8 += ((int32_t)v->lfo_value * bounded_depth) / (12 * 128);
    }
    if (damp_q8 < 256) damp_q8 = 256;
    if (damp_q8 > 1536) damp_q8 = 1536;
    /* b in Q15: 0.75 at 1.0, 0.5 at 2.0, 0.25 at 3.0, none from 4.0 up */
    int32_t b_q15 = damp_q8 >= 1024 ? 0 : ((1024 - damp_q8) * 32) ;
    if (b_q15 > 24576) b_q15 = 24576;
    v->c_b_q15 = b_q15;

    /* The read's length: the note's period less the filter's own delay b, so
     * the pitch stays put while the filter moves; a line from where the last
     * chunk ended to where this one ends */
    {
        const uint32_t comp = (uint32_t)b_q15 << 1;          /* Q16 samples */
        len = len > comp + (4U << 16) ? len - comp : (4U << 16);
    }
    if (!v->c_len) v->c_len = len;                       /* a new note: no line from nowhere */
    v->c_len_step = ((int32_t)len - (int32_t)v->c_len) / (int32_t)n;
    v->c_len_end = len;

    /* the output's gain: the loudness follower's, the phrase expression and
     * the tremolo, also as a line across the chunk (Q15 x 2^8) */
    if (v->level_follow > 64U)
        v->level_gain_q15 = level_compensation_gain(v->velocity, v->level_follow, v->note);
    {
        int32_t g = (int32_t)v->level_gain_q15;
        g = (g * (32767 + v->expr_q15)) >> 15;
        g = (g * tremolo_q15) >> 15;
        const int32_t to = g << 8;
        if (!v->c_amp) v->c_amp = to;
        v->c_amp_step = (to - v->c_amp) / (int32_t)n;
        v->c_amp_end = to;
    }
}

/* every sample: the waveguide, the excitation, the body, the followers, the
 * envelope. Returns the output gain for this sample, Q15. */
static int32_t voice_sample(voice_t *v)
{
    const struct instrument_profile *ip = &INST_PROFILE[v->instrument];
    v->c_len += (uint32_t)v->c_len_step;
    const uint32_t len = v->c_len;
    const int32_t b_q15 = v->c_b_q15;
    const int32_t excitation_gain_q15 = v->c_exc_gain;

    /* the attack's brightness and the damping follow the sound */
    {
        int32_t target = v->out < 0 ? -v->out : v->out;
        if (target > 32767) target = 32767;
        const int32_t delta = target - v->excite_follow;
        v->excite_follow += delta >> (delta > 0 ? 2 : 6);
    }

    uint32_t whole = len >> 16;
    if (whole < 4U) whole = 4U;
    if (whole > v->dmask - 3U) whole = v->dmask - 3U;
    const uint32_t frac = (len >> 8) & 0xFFU;
    const uint32_t i0 = (v->write - whole) & v->dmask;
    const uint32_t i1 = (i0 - 1U) & v->dmask;   /* one older */
    const int32_t s0 = v->delay[i0], s1 = v->delay[i1];
    int32_t delayed = (s0 * (int32_t)(256U - frac) + s1 * (int32_t)frac) >> 8;

    int32_t smoothed = delayed - (((delayed - v->lp) * b_q15) >> 15);
    v->lp = delayed;
    v->out = smoothed;
    /* loop loss is calibrated per round trip at note start, not per sample:
     * otherwise low notes decay faster simply because their delay is longer. */
    int32_t loop = (int32_t)(((int64_t)smoothed * v->loop_gain_q23) >> 23);
    if (ip->half_period) loop = -loop;

    /* excitation: scaled in Q16 so the products stay in 32 bits */
    if (v->strike_left) {
        --v->strike_left;
        /* A short loop passes the burst through its excitation point many
         * times while it lasts, so the same burst rang a high mallet note far
         * louder than a low one (measured with an even 1/n mix: timpani C7
         * 13.6 M, marimba C6 6.8 M, a cello 3-4 M). The burst scales with the
         * loop's length as the bow force does (bow_gain, 400 samples = 1.0,
         * at least 0.25). */
        const int32_t burst = (noise_v(&v->rng) * (int32_t)v->velocity) >> 8;
        const int32_t scaled = v->bow_gain < 256U ? (burst * (int32_t)v->bow_gain) >> 8 : burst;
        loop += ((scaled >> 1) * excitation_gain_q15) >> 15;
    }
    if (v->gate && v->excite != EXC_STRIKE) {
        const int32_t noise = noise_v(&v->rng);
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
             *
             * The ADSR/energy excitation gain of the 0.3.14 line scales the
             * whole drive, as it did the rosin noise this replaces. */
            const int32_t vrel = v->bow_speed - delayed;   /* + = bow leads the string */
            const int32_t mu = friction_q15(vrel);
            int32_t force = (mu * v->bow_fmax) >> 15;    /* magnitude */
            force = vrel < 0 ? -force : force;    /* direction follows the slip */
            v->exc_dc += (force - v->exc_dc) >> 10;   /* ~7 Hz: the DC term */
            int32_t exc = force - v->exc_dc;
            exc = (exc * (int32_t)v->bow_gain) >> 8;
            {
                const int32_t air = (noise * ip->air_noise) >> 12;   /* rosin hiss */
                const int32_t bite = (delayed * (int32_t)v->accent_q16) >> 24;
                loop += (((exc >> 3) + air + bite) * excitation_gain_q15) >> 15;
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
    v->write = (v->write + 1U) & v->dmask;
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

    /* the loudness follower (its gain is worked out once a chunk) */
    {
        const int32_t magnitude = v->out < 0 ? -v->out : v->out;
        const uint32_t current = v->level_follow;
        if ((uint32_t)magnitude > current)
            v->level_follow = current + (((uint32_t)magnitude - current) >> 9);
        else
            v->level_follow = current - ((current - (uint32_t)magnitude) >> 12);
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
    v->c_amp += v->c_amp_step;
    return v->c_amp >> 8;
}

/* ------------------------------------------------------- the render
 *
 * Two phases a chunk of ORCH_CHUNK samples:
 *
 *  1. control, one sample at a time on this core: the event queues, the ARP,
 *     the sequencer, call and response, the tempo, and the controllers'
 *     lines, whose gain and balance a part has at each sample are written
 *     down for phase 2. A note the ARP or the sequencer starts inside the
 *     chunk starts at its beginning: at most 32 samples (0.7 ms) early.
 *  2. the voices, each through the whole chunk: every other sounding voice
 *     on the second core when one answers (orch_set_second_core), the rest
 *     here; each core sums its voices into its own buffers. A voice's render
 *     reads its part's settings and the controllers' values and writes only
 *     itself (its own noise too), and the sums are integers, so the output is
 *     the same bit for bit on one core or two (tests/orch_cores_test.c).
 *
 * Then the mix: the 1.36 / n share by how many sounded at each sample, the
 * guard, and the hall's shared strings. */
#define ORCH_CHUNK 32U
static int32_t chunk_gain[ORCH_PARTS][ORCH_CHUNK];
static int32_t chunk_pan_l[ORCH_PARTS][ORCH_CHUNK], chunk_pan_r[ORCH_PARTS][ORCH_CHUNK];
static int32_t sum_l[2][ORCH_CHUNK], sum_r[2][ORCH_CHUNK];
static uint8_t sum_n[2][ORCH_CHUNK];

static orch_core_run_fn second_run;
static orch_core_wait_fn second_wait;
static uint32_t second_chunks;    /* chunks the second core shared, for the panel and tests */

void orch_set_second_core(orch_core_run_fn run, orch_core_wait_fn wait)
{
    second_run = run;
    second_wait = run ? wait : 0;
}

uint32_t orch_second_core_chunks(void) { return second_chunks; }

/* the voices in `mask` through `n` samples, into the sums of `side` */
static void render_voices(uint32_t mask, unsigned n, unsigned side)
{
    int32_t *L = sum_l[side], *R = sum_r[side];
    uint8_t *cnt = sum_n[side];
    for (unsigned f = 0; f < n; ++f) { L[f] = 0; R[f] = 0; cnt[f] = 0U; }
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        if (!(mask >> i & 1U)) continue;
        voice_t *v = &voices[i];
        const unsigned part = v->part < ORCH_PARTS ? v->part : 0U;
        const int32_t vel_q12 = (int32_t)orch_cc_velocity_q12(v->family, v->velocity);
        voice_control(v, n);
        for (unsigned f = 0; f < n; ++f) {
            if (!v->active) break;
            const int32_t gain_q15 = voice_sample(v);
            if (!v->active) break;
            ++cnt[f];
            int32_t amp = (v->out * v->env >> 15);
            amp = (amp * gain_q15) >> 15;
            /* velocity on an even dB line (orch_cc_tables.inc), then the
             * part's volume x expression x dynamics at this sample */
            int32_t g = (int32_t)(((int64_t)amp * vel_q12) >> 12);
            g = (int32_t)(((int64_t)g * chunk_gain[part][f]) >> 15);
            /* CC10: a balance on the seat the stage gave the part. Pan is Q8;
             * eight voices of these products stay below 2^31 */
            L[f] += g * (((int32_t)v->pan_l * chunk_pan_l[part][f]) >> 15);
            R[f] += g * (((int32_t)v->pan_r * chunk_pan_r[part][f]) >> 15);
        }
    }
}

/* the second core's job: arg = its voices | the chunk's length << 16 */
static void second_core_job(uint32_t arg)
{
    render_voices(arg & 0xFFFFU, (unsigned)(arg >> 16), 1U);
}

static void render_chunk(int32_t *out, unsigned n)
{
    /* 1. control, a sample at a time */
    for (unsigned f = 0; f < n; ++f) {
        orch_perf_advance_sample(sample_counter + f);
        orch_cc_step();
        tempo_tick();
        drain_performance_events();
        response_tick();
        seq_tick();
        for (unsigned p = 0; p < ORCH_PARTS; ++p) {
            chunk_gain[p][f] = orch_cc_gain_q15[p];
            chunk_pan_l[p][f] = orch_cc_pan_l_q15[p];
            chunk_pan_r[p][f] = orch_cc_pan_r_q15[p];
        }
    }

    /* 2. the voices: every other sounding one to the second core */
    uint32_t own = 0U, theirs = 0U;
    unsigned sounding = 0U;
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        if (!voices[i].active) continue;
        if (sounding++ & 1U) theirs |= 1U << i;
        else own |= 1U << i;
    }
    if (!theirs || !second_run || !second_run(second_core_job, theirs | (uint32_t)n << 16)) {
        own |= theirs;
        theirs = 0U;
    }
    render_voices(own, n, 0U);
    if (theirs) {
        ++second_chunks;
        if (second_wait && second_wait())        /* it stopped answering: here */
            render_voices(theirs, n, 1U);
        for (unsigned f = 0; f < n; ++f) {
            sum_l[0][f] += sum_l[1][f];
            sum_r[0][f] += sum_r[1][f];
            sum_n[0][f] = (uint8_t)(sum_n[0][f] + sum_n[1][f]);
        }
    }

    /* 3. the mix */
    for (unsigned f = 0; f < n; ++f) {
        const unsigned active = sum_n[0][f];
        /* the share glides to its new value, so a voice starting does not
         * step the others */
        if (active) {
            const int32_t want = (int32_t)MIX_GAIN_Q12[active <= ORCH_MAX_VOICES ? active : ORCH_MAX_VOICES];
            if (want < mix_gain_q12)       /* a voice came in: ~1.5 ms */
                mix_gain_q12 += (want - mix_gain_q12 - 63) / 64;
            else if (want > mix_gain_q12)  /* a voice ended: ~25 ms */
                mix_gain_q12 += (want - mix_gain_q12 + 1023) / 1024;
        }
        /* a 64-bit multiply and a shift: no divide on the PI32v2 */
        out[f * 2U] = clamp_q23((int32_t)(((int64_t)sum_l[0][f] * mix_gain_q12) >> 12));
        out[f * 2U + 1U] = clamp_q23((int32_t)(((int64_t)sum_r[0][f] * mix_gain_q12) >> 12));
    }
    sample_counter += n;
}

void orch_render_q23(int32_t *out, unsigned frames)
{
    if (!out) return;
    drain_events();
    for (unsigned done = 0; done < frames; ) {
        const unsigned n = frames - done > ORCH_CHUNK ? ORCH_CHUNK : frames - done;
        render_chunk(out + done * 2U, n);
        done += n;
    }
    /* the whole orchestra into its shared strings (orch_resonance.c) */
    orch_resonance_process(out, frames);
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

unsigned orch_seq_position(void)
{
    return seq_enabled ? current_step : 0xFFU;
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
