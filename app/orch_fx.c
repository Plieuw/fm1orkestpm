/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Master effect chain for the orchestral engine.
 *
 * Four fixed-point effects after the voice mix, in this order:
 *
 *   chorus  ->  ensemble  ->  echo  ->  shimmer reverb
 *
 * No samples, no allocation, no libc and no locks. The panel writes one byte
 * per setting and the audio interrupt reads those bytes once per block, so a
 * setting can never be seen half-written.
 *
 * The reverb is a Schroeder/Freeverb network (four damped comb filters and two
 * allpass filters per channel, the right channel tuned +23 samples). Shimmer is
 * an octave-up granular pitch shifter in the reverb's feedback path, and EARLY
 * is a short reflection of the input ahead of the network. Static buffer cost:
 * about 100 KB of int16, which is the price of four real effects; every effect
 * is off by default.
 *
 * This file holds the state, the helpers and the settings; the chain itself,
 * which is one loop over the frames, is the include at the bottom.
 */
#include "orch_engine.h"
#include "orch_fx.h"

#define Q15_MAX 32767
#define Q15_MIN (-32768)

#ifndef FM1_AUDIO_DATA
#define FM1_AUDIO_DATA __attribute__((section(".pool")))
#endif

#ifdef ORCH_FX_ROOMY
#define MOD_SIZE 4096U          /* 93 ms: ensemble taps at 18/23/29 ms */
#define DLY_SIZE 16384U         /* 371 ms */
#define PB_SIZE 4096U
#define PB_GRAIN 1024U
#define COMB_MAX 1400U
#define AP_MAX 600U
#define ENS_BASE 800U, 1024U, 1280U
#define COMB_TUNE 1116U, 1188U, 1277U, 1356U
#define AP_TUNE 556U, 441U
#define DELAY_MIN 2646
#define DELAY_STEP 88           /* 60..259 ms */
#else
#define MOD_SIZE 1024U          /* 23 ms: ensemble taps at 5/7/9 ms */
#define DLY_SIZE 8192U          /* 186 ms (pool) */
#define PB_SIZE 2048U
#define PB_GRAIN 512U
#define COMB_MAX 512U
#define AP_MAX 256U
#define ENS_BASE 240U, 320U, 400U
#define COMB_TUNE 376U, 400U, 432U, 464U
#define AP_TUNE 204U, 162U
#define DELAY_MIN 600
#define DELAY_STEP 58           /* 14..145 ms */
#endif
#define MOD_MASK (MOD_SIZE - 1U)
#define DLY_MASK (DLY_SIZE - 1U)
#define PB_MASK (PB_SIZE - 1U)

/* Early reflections in front of the reverb: 1024 samples of the input, read at
 * three short taps, so EARLY puts a floor under the tail instead of a pre-delay
 * the player has to wait for. */
#define ER_SIZE 1024U
#define ER_MASK (ER_SIZE - 1U)

#define COMB_COUNT 4U
#define AP_COUNT 2U

static int16_t mod_l[MOD_SIZE], mod_r[MOD_SIZE];
static uint32_t mod_w;
static uint32_t chorus_phase, ensemble_phase;
static int32_t chorus_lp_l, chorus_lp_r;

static int16_t dly_l[DLY_SIZE] FM1_AUDIO_DATA;
static int16_t dly_r[DLY_SIZE] FM1_AUDIO_DATA;
static uint32_t dly_w;
static int32_t dly_cur, dly_lp_l, dly_lp_r;

static int16_t er_buf[ER_SIZE] FM1_AUDIO_DATA;
static uint32_t er_w;

static int16_t comb_buf[2][COMB_COUNT][COMB_MAX] FM1_AUDIO_DATA;
static uint32_t comb_len[2][COMB_COUNT];
static uint32_t comb_w[2][COMB_COUNT];
static int32_t comb_lp[2][COMB_COUNT];

static int16_t ap_buf[2][AP_COUNT][AP_MAX] FM1_AUDIO_DATA;
static uint32_t ap_len[2][AP_COUNT];
static uint32_t ap_w[2][AP_COUNT];

static int16_t pb[PB_SIZE];
static uint32_t pb_w, pb_phase;
static int32_t shimmer_src;

/* Written by the control task, read by the renderer: one byte each. */
static volatile uint8_t fx_on[ORCH_FX_SLOT_COUNT];
static volatile uint8_t fx_mix[ORCH_FX_SLOT_COUNT];
static volatile uint8_t fx_p[ORCH_FX_SLOT_COUNT][ORCH_FX_PARAMS];
static volatile uint16_t fx_tempo;
static uint32_t mix_now[ORCH_FX_SLOT_COUNT];   /* the mixes as they sound (orch_fx_dsp.inc) */
static unsigned mod_fill;                       /* samples until the modulation line is full */
static volatile uint8_t reset_pending;

/* No libc in the firmware: the JieLi build is freestanding. */
static void fx_clear(void *ptr, unsigned bytes)
{
    uint8_t *p = (uint8_t *)ptr;
    for (unsigned i = 0U; i < bytes; ++i) p[i] = 0U;
}

static void clear_state(void)
{
    fx_clear(mod_l, (unsigned)sizeof mod_l);
    fx_clear(mod_r, (unsigned)sizeof mod_r);
    fx_clear(dly_l, (unsigned)sizeof dly_l);
    fx_clear(dly_r, (unsigned)sizeof dly_r);
    fx_clear(er_buf, (unsigned)sizeof er_buf);
    fx_clear(comb_buf, (unsigned)sizeof comb_buf);
    fx_clear(comb_lp, (unsigned)sizeof comb_lp);
    fx_clear(ap_buf, (unsigned)sizeof ap_buf);
    fx_clear(pb, (unsigned)sizeof pb);
    mod_w = 0U;
    chorus_phase = ensemble_phase = 0U;
    chorus_lp_l = chorus_lp_r = 0;
    dly_w = 0U;
    dly_cur = -1;
    dly_lp_l = dly_lp_r = 0;
    er_w = 0U;
    pb_w = pb_phase = 0U;
    shimmer_src = 0;
    reset_pending = 0U;
}

/* --- public control ------------------------------------------------------- */

void orch_fx_init(void)
{
    static const uint16_t comb_tune[COMB_COUNT] = { COMB_TUNE };
    static const uint16_t ap_tune[AP_COUNT] = { AP_TUNE };
    static const uint8_t defaults_mix[ORCH_FX_SLOT_COUNT] = { 40U, 30U, 25U, 25U };
    /* RATE, DEPTH, SPREAD, TONE / RATE, DEPTH, WIDTH, TAPS /
     * TIME, FEEDBACK, SYNC, DAMP / DECAY, SHIM, EARLY, DAMP */
    static const uint8_t defaults_p[ORCH_FX_SLOT_COUNT][ORCH_FX_PARAMS] = {
        { 25U, 50U, 40U, 100U },
        { 30U, 50U, 50U, 100U },
        { 50U, 35U, 0U, 100U },
        { 55U, 25U, 30U, 60U }
    };

    for (unsigned ch = 0; ch < 2U; ++ch) {
        for (unsigned c = 0; c < COMB_COUNT; ++c) {
            comb_len[ch][c] = (uint32_t)comb_tune[c] + ch * 23U;
            comb_w[ch][c] = 0U;
        }
        for (unsigned a = 0; a < AP_COUNT; ++a) {
            ap_len[ch][a] = (uint32_t)ap_tune[a] + ch * 23U;
            ap_w[ch][a] = 0U;
        }
    }
    for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s) {
        fx_on[s] = 0U;              /* off until the player asks for it */
        mix_now[s] = 0U;
        mod_fill = MOD_SIZE;
        fx_mix[s] = defaults_mix[s];
        for (unsigned i = 0; i < ORCH_FX_PARAMS; ++i) fx_p[s][i] = defaults_p[s][i];
    }
    fx_tempo = 120U;
    clear_state();
}

void orch_fx_reset(void) { reset_pending = 1U; }

void orch_fx_set_tempo(unsigned bpm)
{
    if (bpm < 30U) bpm = 30U;
    if (bpm > 300U) bpm = 300U;
    fx_tempo = (uint16_t)bpm;
}

int orch_fx_set_enabled(unsigned slot, unsigned on)
{
    if (slot >= ORCH_FX_SLOT_COUNT) return -1;
    fx_on[slot] = on ? 1U : 0U;
    return 0;
}

int orch_fx_set_mix(unsigned slot, unsigned percent)
{
    if (slot >= ORCH_FX_SLOT_COUNT) return -1;
    if (percent > 100U) percent = 100U;
    fx_mix[slot] = (uint8_t)percent;
    return 0;
}

int orch_fx_set_param(unsigned slot, unsigned index, unsigned percent)
{
    if (slot >= ORCH_FX_SLOT_COUNT || index >= ORCH_FX_PARAMS) return -1;
    if (percent > 100U) percent = 100U;
    fx_p[slot][index] = (uint8_t)percent;
    return 0;
}

unsigned orch_fx_enabled(unsigned slot)
{
    return slot < ORCH_FX_SLOT_COUNT ? fx_on[slot] : 0U;
}

unsigned orch_fx_mix(unsigned slot)
{
    return slot < ORCH_FX_SLOT_COUNT ? fx_mix[slot] : 0U;
}

unsigned orch_fx_param(unsigned slot, unsigned index)
{
    if (slot >= ORCH_FX_SLOT_COUNT || index >= ORCH_FX_PARAMS) return 0U;
    return fx_p[slot][index];
}

unsigned orch_fx_any_enabled(void)
{
    unsigned any = 0U;
    for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s) any |= fx_on[s];
    return any;
}

const char *orch_fx_name(unsigned slot)
{
    static const char *const names[ORCH_FX_SLOT_COUNT] = {
        "CHORUS", "ENSEMBLE", "DELAY", "SHIMMER"
    };
    return slot < ORCH_FX_SLOT_COUNT ? names[slot] : "?";
}

const char *orch_fx_param_name(unsigned slot, unsigned index)
{
    static const char *const names[ORCH_FX_SLOT_COUNT][ORCH_FX_PARAMS] = {
        { "RATE", "DEPTH", "SPREAD", "TONE" },
        { "RATE", "DEPTH", "WIDTH", "TAPS" },
        { "TIME", "FEEDBACK", "SYNC", "DAMP" },
        { "DECAY", "SHIM", "EARLY", "DAMP" }
    };
    if (slot >= ORCH_FX_SLOT_COUNT || index >= ORCH_FX_PARAMS) return "?";
    return names[slot][index];
}

#include "orch_fx_dsp.inc"
