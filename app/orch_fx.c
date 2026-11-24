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
#include <string.h>

#include "orch_engine.h"
#include "orch_fx.h"

#define Q15_MAX 32767
#define Q15_MIN (-32768)

/* Modulated line shared by the chorus and the ensemble: 93 ms, enough for the
 * three ensemble taps at 18/23/29 ms plus their sweep. Both effects read the
 * dry signal from it, so it is written once per sample. */
#define MOD_SIZE 4096U
#define MOD_MASK (MOD_SIZE - 1U)

/* One echo line per channel: 371 ms, a power of two so the index wraps with a
 * mask. The usable delay range is shorter. */
#define DLY_SIZE 16384U
#define DLY_MASK (DLY_SIZE - 1U)

/* Octave-up grain buffer: two 1024-sample grains, read at double speed. */
#define PB_SIZE 4096U
#define PB_MASK (PB_SIZE - 1U)
#define PB_GRAIN 1024U

/* Early reflections in front of the reverb: 1024 samples of the input, read at
 * three short taps, so EARLY puts a floor under the tail instead of a pre-delay
 * the player has to wait for. */
#define ER_SIZE 1024U
#define ER_MASK (ER_SIZE - 1U)

#define COMB_MAX 1400U
#define AP_MAX 600U
#define COMB_COUNT 4U
#define AP_COUNT 2U

static int16_t mod_l[MOD_SIZE], mod_r[MOD_SIZE];
static uint32_t mod_w;
static uint32_t chorus_phase, ensemble_phase;
static int32_t chorus_lp_l, chorus_lp_r;

static int16_t dly_l[DLY_SIZE], dly_r[DLY_SIZE];
static uint32_t dly_w;
static int32_t dly_cur, dly_lp_l, dly_lp_r;

static int16_t er_buf[ER_SIZE];
static uint32_t er_w;

static int16_t comb_buf[2][COMB_COUNT][COMB_MAX];
static uint32_t comb_len[2][COMB_COUNT];
static uint32_t comb_w[2][COMB_COUNT];
static int32_t comb_lp[2][COMB_COUNT];

static int16_t ap_buf[2][AP_COUNT][AP_MAX];
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
static volatile uint8_t reset_pending;

static void clear_state(void)
{
    memset(mod_l, 0, sizeof mod_l);
    memset(mod_r, 0, sizeof mod_r);
    memset(dly_l, 0, sizeof dly_l);
    memset(dly_r, 0, sizeof dly_r);
    memset(er_buf, 0, sizeof er_buf);
    memset(comb_buf, 0, sizeof comb_buf);
    memset(comb_lp, 0, sizeof comb_lp);
    memset(ap_buf, 0, sizeof ap_buf);
    memset(pb, 0, sizeof pb);
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
    static const uint16_t comb_tune[COMB_COUNT] = { 1116U, 1188U, 1277U, 1356U };
    static const uint16_t ap_tune[AP_COUNT] = { 556U, 441U };
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
