/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "orch_resonance.h"

#ifndef FM1_AUDIO_DATA
#define FM1_AUDIO_DATA __attribute__((section(".pool")))
#endif

/* The eight strings: loop length in Q8 samples at 44.1 kHz (less the
 * damping filter's quarter sample, so they sound at pitch), and the feedback
 * per round trip, Q15, for a T60 of 2.5 s at G2 falling to 1.6 s at E5. */
static const uint32_t RES_LEN_Q8[ORCH_RESONATORS] = {
    115201U - 64U, 86303U - 64U, 76888U - 64U, 57601U - 64U,
    51316U - 64U, 38444U - 64U, 25658U - 64U, 17125U - 64U
};
static const uint16_t RES_FB_Q15[ORCH_RESONATORS] = {
    31857U, 32044U, 32107U, 32241U, 32286U, 32381U, 32482U, 32554U
};
/* left share of each string, Q8: the low strings to the right (the cellos'
 * side), the high ones to the left (the violins'), as the orchestra sits */
static const uint8_t RES_PAN_L[ORCH_RESONATORS] = {
    96U, 104U, 112U, 136U, 144U, 152U, 160U, 168U
};
/* each buffer two samples longer than its loop, a power of two to wrap */
#ifndef RES_DRIVE
#define RES_DRIVE 6
#endif
#define RES_SIZE 512U
#define RES_MASK (RES_SIZE - 1U)

static int32_t res_buf[ORCH_RESONATORS][RES_SIZE] FM1_AUDIO_DATA;
static int32_t res_lp[ORCH_RESONATORS];
static uint32_t res_follow[ORCH_RESONATORS];
static uint32_t res_w;
static volatile uint8_t res_amount = 25U;
static volatile uint8_t res_clear_pending;

static int32_t shr0_64(int64_t x, unsigned bits)
{
    return (int32_t)(x < 0 ? -((-x) >> bits) : x >> bits);
}

void orch_resonance_clear(void) { res_clear_pending = 1U; }

static void clear_now(void)
{
    for (unsigned k = 0; k < ORCH_RESONATORS; ++k) {
        for (unsigned i = 0; i < RES_SIZE; ++i) res_buf[k][i] = 0;
        res_lp[k] = 0;
        res_follow[k] = 0U;
    }
    res_w = 0U;
    res_clear_pending = 0U;
}

void orch_resonance_init(void)
{
    clear_now();
    res_amount = 25U;
}

void orch_resonance_set_amount(unsigned percent)
{
    res_amount = (uint8_t)(percent > 100U ? 100U : percent);
}

unsigned orch_resonance_amount(void) { return res_amount; }

unsigned orch_resonance_ring(unsigned k)
{
    if (k >= ORCH_RESONATORS) return 0U;
    const uint32_t f = res_follow[k] >> 12;      /* Q23 peak / 4096 */
    return f > 255U ? 255U : (unsigned)f;
}

void orch_resonance_process(int32_t *stereo, unsigned frames)
{
    if (!stereo) return;
    if (res_clear_pending) clear_now();
    const unsigned amount = res_amount;
    if (!amount) return;
    /* added on top of the dry signal, at most half the bank's side */
    const int32_t wet = (int32_t)(amount * 16384U / 100U);

    for (unsigned f = 0; f < frames; ++f) {
        const int32_t in = (stereo[f * 2U] >> 1) + (stereo[f * 2U + 1U] >> 1);
        int64_t sum_l = 0, sum_r = 0;
        for (unsigned k = 0; k < ORCH_RESONATORS; ++k) {
            /* the read: `whole` back and the sample before it, by the fraction */
            const uint32_t len = RES_LEN_Q8[k];
            const uint32_t whole = len >> 8, frac = len & 0xFFU;
            const uint32_t i0 = (res_w + RES_SIZE - whole) & RES_MASK;
            const uint32_t i1 = (i0 + RES_SIZE - 1U) & RES_MASK;
            const int64_t a = res_buf[k][i0], b = res_buf[k][i1];
            const int32_t y = (int32_t)((a * (int64_t)(256U - frac) + b * (int64_t)frac) >> 8);
            /* a quarter-sample smoother: the strings' highs die first */
            const int32_t s = y - shr0_64((int64_t)(y - res_lp[k]), 2);
            res_lp[k] = y;
            /* input scaled by (1 - g) * RES_DRIVE: at resonance the loop's
             * gain is 1 / (1 - g), so a string rings at most RES_DRIVE times
             * what feeds it, and only for a steady tone on its own pitch */
            const int32_t g = (int32_t)RES_FB_Q15[k];
            const int32_t x = shr0_64((int64_t)in * (32768 - g) * RES_DRIVE, 15) +
                              shr0_64((int64_t)s * g, 15);
            res_buf[k][res_w] = x;
            sum_l += (int64_t)y * RES_PAN_L[k];
            sum_r += (int64_t)y * (256U - RES_PAN_L[k]);
            {   /* how hard it rings, for the panel: a slow peak follower */
                const uint32_t m = (uint32_t)(y < 0 ? -y : y);
                if (m > res_follow[k]) res_follow[k] = m;
                else res_follow[k] -= (res_follow[k] >> 11) + 1U;
                if (res_follow[k] > 0x7FFFFFFFU) res_follow[k] = 0U;
            }
        }
        res_w = (res_w + 1U) & RES_MASK;
        /* each string's left and right shares sum to 256 and there are
         * eight, so the bank's side averages the strings; it is added on top
         * of the dry signal at up to one half, inside the headroom the even
         * 1/n mix leaves (the full orchestra at 127 and resonance 100 peaks
         * near 3 M of 8.4 M, see test_shared_resonance) */
        const int32_t rl = (int32_t)(sum_l >> 11), rr = (int32_t)(sum_r >> 11);
        stereo[f * 2U] += shr0_64((int64_t)rl * wet, 15);
        stereo[f * 2U + 1U] += shr0_64((int64_t)rr * wet, 15);
    }
}
