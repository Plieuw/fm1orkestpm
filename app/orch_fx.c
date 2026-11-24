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
 * an octave-up granular pitch shifter in the reverb's feedback path. Static
 * buffer cost: about 100 KB of int16, which is the price of four real effects;
 * every effect is off by default.
 */
#include <stdint.h>

#include "orch_engine.h"
#include "orch_fx.h"

#ifndef FM1_AUDIO_DATA
#define FM1_AUDIO_DATA __attribute__((section(".pool")))
#endif

#define Q15_MAX 32767
#define Q15_MIN (-32768)

/* Modulated line shared by the chorus and the ensemble: 93 ms, enough for the
 * three ensemble taps at 18/23/29 ms plus their sweep. Both effects read the
 * dry signal from it, so it is written once per sample. */
#define MOD_SIZE 1024U
#define MOD_MASK (MOD_SIZE - 1U)

/* One echo line per channel: 371 ms, a power of two so the index wraps with a
 * mask. The usable delay range is shorter. */
#define DLY_SIZE 4096U
#define DLY_MASK (DLY_SIZE - 1U)

/* Octave-up grain buffer: two 512-sample grains, read at double speed. */
#define PB_SIZE 2048U
#define PB_MASK (PB_SIZE - 1U)
#define PB_GRAIN 512U

#define COMB_MAX 256U
#define AP_MAX 128U
#define COMB_COUNT 4U
#define AP_COUNT 2U

static int16_t mod_l[MOD_SIZE], mod_r[MOD_SIZE];
static uint32_t mod_w;
static uint32_t chorus_phase, ensemble_phase;

static int16_t dly_l[DLY_SIZE] FM1_AUDIO_DATA;
static int16_t dly_r[DLY_SIZE] FM1_AUDIO_DATA;
static uint32_t dly_w;
static int32_t dly_cur;

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
static volatile uint8_t fx_a[ORCH_FX_SLOT_COUNT];
static volatile uint8_t fx_b[ORCH_FX_SLOT_COUNT];
static volatile uint8_t reset_pending;

/* --- helpers -------------------------------------------------------------- */

static void fx_clear_bytes(void *ptr, unsigned bytes)
{
    uint8_t *p = (uint8_t *)ptr;
    for (unsigned i = 0U; i < bytes; ++i) p[i] = 0U;
}

static int32_t clamp15(int32_t x)
{
    if (x > Q15_MAX) return Q15_MAX;
    if (x < Q15_MIN) return Q15_MIN;
    return x;
}

static int32_t tri_q14(uint32_t phase)
{
    const uint32_t p = phase >> 16;                       /* one cycle */
    const uint32_t t = (p < 32768U) ? p : 65535U - p;     /* 0..32768 */
    return (int32_t)t - 16384;                            /* +/- half swing */
}

/* One cycle per second is 2^32 in phase; the rate is in milli-hertz so the
 * slowest chorus (0.15 Hz) is still an integer. */
static uint32_t lfo_inc(unsigned percent, unsigned min_mhz, unsigned max_mhz)
{
    const uint32_t mhz = min_mhz + ((max_mhz - min_mhz) * percent) / 100U;
    return (uint32_t)(((uint64_t)mhz << 32) / ((uint64_t)ORCH_SAMPLE_RATE * 1000U));
}

static unsigned mix_q8(unsigned percent)
{
    if (percent > 100U) percent = 100U;
    return percent * 255U / 100U;
}

static int32_t crossfade(int32_t dry, int32_t wet, unsigned mix)
{
    return (dry * (int32_t)(256U - mix) + wet * (int32_t)mix) >> 8;
}

/* Fractional read from the shared modulated line, delay in Q8 samples. */
static int32_t mod_read(const int16_t *buf, int32_t delay_q8)
{
    if (delay_q8 < 256) delay_q8 = 256;
    if (delay_q8 > (int32_t)(MOD_SIZE - 2U) << 8) delay_q8 = (int32_t)(MOD_SIZE - 2U) << 8;
    const uint32_t whole = (uint32_t)(delay_q8 >> 8);
    const uint32_t frac = (uint32_t)(delay_q8 & 0xFF);
    const uint32_t i0 = (mod_w + MOD_SIZE - whole) & MOD_MASK;
    const uint32_t i1 = (i0 + 1U) & MOD_MASK;
    return ((int32_t)buf[i0] * (int32_t)(256U - frac) +
            (int32_t)buf[i1] * (int32_t)frac) >> 8;
}

static int32_t mod_tap(const int16_t *buf, int32_t base, uint32_t phase,
                       unsigned depth, uint32_t extra_phase)
{
    const int32_t off = (tri_q14(phase + extra_phase) * (int32_t)depth) >> 14;
    return mod_read(buf, (base + off) << 8);
}

/* One octave up: two grains read at double speed with complementary triangular
 * windows, which keeps the loop amplitude flat across the grain crossfade. */
static int32_t shimmer_octave(int32_t in)
{
    pb[pb_w] = (int16_t)clamp15(in);
    const uint32_t a = pb_phase;
    uint32_t b = a + PB_GRAIN / 2U;
    if (b >= PB_GRAIN) b -= PB_GRAIN;
    const uint32_t d1 = 2U * PB_GRAIN - a;
    const uint32_t d2 = 2U * PB_GRAIN - b;
    const int32_t s1 = pb[(pb_w + PB_SIZE - d1) & PB_MASK];
    const int32_t s2 = pb[(pb_w + PB_SIZE - d2) & PB_MASK];
    const int32_t w1 = (a < PB_GRAIN / 2U) ? (int32_t)a * 2
                                           : (int32_t)(PB_GRAIN - a) * 2;
    const int32_t out = (s1 * w1 + s2 * (1024 - w1)) >> 10;
    pb_w = (pb_w + 1U) & PB_MASK;
    if (++pb_phase >= PB_GRAIN) pb_phase = 0U;
    return out;
}

static void clear_state(void)
{
    fx_clear_bytes(mod_l, (unsigned)sizeof mod_l);
    fx_clear_bytes(mod_r, (unsigned)sizeof mod_r);
    fx_clear_bytes(dly_l, (unsigned)sizeof dly_l);
    fx_clear_bytes(dly_r, (unsigned)sizeof dly_r);
    fx_clear_bytes(comb_buf, (unsigned)sizeof comb_buf);
    fx_clear_bytes(comb_lp, (unsigned)sizeof comb_lp);
    fx_clear_bytes(ap_buf, (unsigned)sizeof ap_buf);
    fx_clear_bytes(pb, (unsigned)sizeof pb);
    mod_w = 0U;
    chorus_phase = ensemble_phase = 0U;
    dly_w = 0U;
    dly_cur = -1;
    pb_w = pb_phase = 0U;
    shimmer_src = 0;
    reset_pending = 0U;
}

/* --- public control ------------------------------------------------------- */

void orch_fx_init(void)
{
    static const uint16_t comb_tune[COMB_COUNT] = { 188U, 200U, 216U, 232U };
    static const uint16_t ap_tune[AP_COUNT] = { 102U, 81U };
    static const uint8_t defaults_mix[ORCH_FX_SLOT_COUNT] = { 40U, 30U, 25U, 25U };
    static const uint8_t defaults_a[ORCH_FX_SLOT_COUNT] = { 25U, 30U, 50U, 55U };
    static const uint8_t defaults_b[ORCH_FX_SLOT_COUNT] = { 50U, 50U, 35U, 25U };

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
        fx_a[s] = defaults_a[s];
        fx_b[s] = defaults_b[s];
    }
    clear_state();
}

void orch_fx_reset(void) { reset_pending = 1U; }

/* Keep slot validation and percent clamping in one place. These helpers
 * run in the control path; the audio renderer still reads the byte arrays
 * directly, so this refactor adds no per-sample work. */
static int set_fx_value(volatile uint8_t *values, unsigned slot,
                        unsigned value, unsigned percent)
{
    if (slot >= ORCH_FX_SLOT_COUNT) return -1;
    if (percent && value > 100U) value = 100U;
    values[slot] = (uint8_t)value;
    return 0;
}

static unsigned get_fx_value(const volatile uint8_t *values, unsigned slot)
{
    return slot < ORCH_FX_SLOT_COUNT ? values[slot] : 0U;
}

int orch_fx_set_enabled(unsigned slot, unsigned on)
{
    return set_fx_value(fx_on, slot, on ? 1U : 0U, 0U);
}

int orch_fx_set_mix(unsigned slot, unsigned percent)
{
    return set_fx_value(fx_mix, slot, percent, 1U);
}

int orch_fx_set_param_a(unsigned slot, unsigned percent)
{
    return set_fx_value(fx_a, slot, percent, 1U);
}

int orch_fx_set_param_b(unsigned slot, unsigned percent)
{
    return set_fx_value(fx_b, slot, percent, 1U);
}

unsigned orch_fx_enabled(unsigned slot) { return get_fx_value(fx_on, slot); }
unsigned orch_fx_mix(unsigned slot) { return get_fx_value(fx_mix, slot); }
unsigned orch_fx_param_a(unsigned slot) { return get_fx_value(fx_a, slot); }
unsigned orch_fx_param_b(unsigned slot) { return get_fx_value(fx_b, slot); }

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

const char *orch_fx_param_a_name(unsigned slot)
{
    static const char *const names[ORCH_FX_SLOT_COUNT] = {
        "RATE", "RATE", "TIME", "DECAY"
    };
    return slot < ORCH_FX_SLOT_COUNT ? names[slot] : "?";
}

const char *orch_fx_param_b_name(unsigned slot)
{
    static const char *const names[ORCH_FX_SLOT_COUNT] = {
        "DEPTH", "WIDTH", "FBACK", "SHIM"
    };
    return slot < ORCH_FX_SLOT_COUNT ? names[slot] : "?";
}

/* --- the chain ------------------------------------------------------------ */

void orch_fx_process(int32_t *stereo, unsigned frames)
{
    if (!stereo || !frames) return;
    if (reset_pending) clear_state();

    const unsigned chorus_on = fx_on[ORCH_FX_CHORUS];
    const unsigned ensemble_on = fx_on[ORCH_FX_ENSEMBLE];
    const unsigned delay_on = fx_on[ORCH_FX_DELAY];
    const unsigned reverb_on = fx_on[ORCH_FX_REVERB];
    if (!(chorus_on | ensemble_on | delay_on | reverb_on)) return;

    const unsigned chorus_mix = mix_q8(fx_mix[ORCH_FX_CHORUS]);
    const unsigned ensemble_mix = mix_q8(fx_mix[ORCH_FX_ENSEMBLE]);
    const unsigned delay_mix = mix_q8(fx_mix[ORCH_FX_DELAY]);
    const unsigned reverb_mix = mix_q8(fx_mix[ORCH_FX_REVERB]);

    const uint32_t chorus_inc = lfo_inc(fx_a[ORCH_FX_CHORUS], 150U, 5000U);
    const unsigned chorus_depth = 60U + (unsigned)fx_b[ORCH_FX_CHORUS] * 5U / 2U;
    const uint32_t ensemble_inc = lfo_inc(fx_a[ORCH_FX_ENSEMBLE], 90U, 1800U);
    const unsigned ensemble_depth = 60U + (unsigned)fx_b[ORCH_FX_ENSEMBLE] * 4U;

    /* Compact 14..79 ms slap/echo line, and up to 90 % feedback. */
    const int32_t delay_target = 600 + (int32_t)fx_a[ORCH_FX_DELAY] * 29;
    const int32_t delay_fb = (int32_t)fx_b[ORCH_FX_DELAY] * 295;
    if (dly_cur < 0) dly_cur = delay_target;

    /* 0.70..0.92 comb feedback; shimmer is capped at 70 % so the loop decays. */
    const int32_t rev_fb = 22938 + (int32_t)fx_a[ORCH_FX_REVERB] * 72;
    unsigned shimmer = fx_b[ORCH_FX_REVERB];
    if (shimmer > 70U) shimmer = 70U;
    const unsigned shimmer_q8 = mix_q8(shimmer);

    for (unsigned f = 0; f < frames; ++f) {
        int32_t l = stereo[f * 2U] >> 8;
        int32_t r = stereo[f * 2U + 1U] >> 8;
        const int32_t dry_l = l, dry_r = r;

        if (chorus_on || ensemble_on) {
            mod_l[mod_w] = (int16_t)clamp15(dry_l);
            mod_r[mod_w] = (int16_t)clamp15(dry_r);
            if (chorus_on) {
                chorus_phase += chorus_inc;
                const int32_t wl = mod_tap(mod_l, 512, chorus_phase, chorus_depth, 0U);
                const int32_t wr = mod_tap(mod_r, 512, chorus_phase, chorus_depth, 0x20000000U);
                l = crossfade(l, wl, chorus_mix);
                r = crossfade(r, wr, chorus_mix);
            }
            if (ensemble_on) {
                ensemble_phase += ensemble_inc;
                int32_t wl = 0, wr = 0;
                /* three short ensemble taps at roughly 5/7/9 ms */
                static const uint16_t base[3] = { 240U, 320U, 400U };
                for (unsigned t = 0; t < 3U; ++t) {
                    const uint32_t ph = ensemble_phase + t * 0x55555555U;
                    wl += mod_tap(mod_l, (int32_t)base[t], ph, ensemble_depth, 0U);
                    wr += mod_tap(mod_r, (int32_t)base[t], ph, ensemble_depth, 0x20000000U);
                }
                l = crossfade(l, wl / 3, ensemble_mix);
                r = crossfade(r, wr / 3, ensemble_mix);
            }
            mod_w = (mod_w + 1U) & MOD_MASK;
        }

        if (delay_on) {
            if (dly_cur < delay_target) ++dly_cur;
            else if (dly_cur > delay_target) --dly_cur;
            const uint32_t back = (uint32_t)dly_cur;
            const int32_t el = dly_l[(dly_w + DLY_SIZE - back) & DLY_MASK];
            const int32_t er = dly_r[(dly_w + DLY_SIZE - back) & DLY_MASK];
            dly_l[dly_w] = (int16_t)clamp15(l + ((er * delay_fb) >> 15));
            dly_r[dly_w] = (int16_t)clamp15(r + ((el * delay_fb) >> 15));
            dly_w = (dly_w + 1U) & DLY_MASK;
            l = crossfade(l, el, delay_mix);
            r = crossfade(r, er, delay_mix);
        }

        if (reverb_on) {
            int32_t rin = (((l + r) >> 1) * 983) >> 16;   /* ~0.015 of the sum */
            if (shimmer_q8) {
                const int32_t oct = shimmer_octave(shimmer_src);
                rin += (oct * (int32_t)shimmer_q8) >> 8;
            }
            if (rin > 20000) rin = 20000;
            if (rin < -20000) rin = -20000;

            int32_t wet_l = 0, wet_r = 0;
            for (unsigned ch = 0; ch < 2U; ++ch) {
                int32_t x = rin, sum = 0;
                for (unsigned c = 0; c < COMB_COUNT; ++c) {
                    const int32_t y = comb_buf[ch][c][comb_w[ch][c]];
                    comb_lp[ch][c] += ((y - comb_lp[ch][c]) * 51) >> 8;
                    const int32_t fb = (comb_lp[ch][c] * rev_fb) >> 15;
                    comb_buf[ch][c][comb_w[ch][c]] = (int16_t)clamp15(x + fb);
                    if (++comb_w[ch][c] >= comb_len[ch][c]) comb_w[ch][c] = 0U;
                    sum += y;
                }
                x = sum >> 2;
                for (unsigned a = 0; a < AP_COUNT; ++a) {
                    const int32_t y = ap_buf[ch][a][ap_w[ch][a]];
                    ap_buf[ch][a][ap_w[ch][a]] = (int16_t)clamp15(x + (y >> 1));
                    if (++ap_w[ch][a] >= ap_len[ch][a]) ap_w[ch][a] = 0U;
                    x = y - x;
                }
                if (ch == 0U) wet_l = x; else wet_r = x;
            }
            shimmer_src = (wet_l + wet_r) >> 1;
            l = crossfade(l, wet_l, reverb_mix);
            r = crossfade(r, wet_r, reverb_mix);
        }

        stereo[f * 2U] = clamp15(l) * 256;
        stereo[f * 2U + 1U] = clamp15(r) * 256;
    }
}
