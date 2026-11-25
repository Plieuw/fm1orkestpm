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

/* the strings' lines, int16 holding the signal / 256 (0.7.1: half the
 * pool, 8 KB, for the sixteen voices); a string's quietest ring is then
 * 2^-15 of full scale, -90 dB, below anything it adds to */
/* in ordinary RAM, not the pool: the HAWK build wants 8 KB of the pool
 * free and sixteen voices leave 6.6 KB; RAM has 21 KB (0.7.1 CI figures) */
static int16_t res_buf[ORCH_RESONATORS][RES_SIZE];
static int32_t res_lp[ORCH_RESONATORS];
static uint32_t res_follow[ORCH_RESONATORS];
static uint32_t res_w;
static volatile uint8_t res_amount = 25U;
static volatile uint8_t res_clear_pending;
/* the strings' level as it sounds, Q15: it moves to a new amount along a
 * line, 20 ms from none to the most, so RESON or CC71 turned fast does not
 * step each block */
static int32_t wet_now;
#define WET_SLEW (16384 / (int32_t)(44100U / 50U) + 1)

void orch_resonance_clear(void) { res_clear_pending = 1U; }

static void clear_now(void)
{
    for (unsigned k = 0; k < ORCH_RESONATORS; ++k) {
        for (unsigned i = 0; i < RES_SIZE; ++i) res_buf[k][i] = 0;
        res_lp[k] = 0;
        res_follow[k] = 0U;
    }
    res_w = 0U;
    wet_now = 0;
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
    /* added on top of the dry signal, at most half the bank's side */
    const int32_t wet_to = (int32_t)(amount * 16384U / 100U);
    if (!wet_to && !wet_now) return;

    /* the strings' constants, worked out once a block: where each reads,
     * how much of the input it takes, its pan */
    uint32_t whole[ORCH_RESONATORS], frac[ORCH_RESONATORS];
    int32_t feed[ORCH_RESONATORS];
    for (unsigned k = 0; k < ORCH_RESONATORS; ++k) {
        whole[k] = RES_LEN_Q8[k] >> 8;
        frac[k] = RES_LEN_Q8[k] & 0xFFU;
        /* input scaled by (1 - g) * RES_DRIVE: at resonance the loop's gain
         * is 1 / (1 - g), so a string rings at most RES_DRIVE times what
         * feeds it, and only for a steady tone on its own pitch */
        feed[k] = (32768 - (int32_t)RES_FB_Q15[k]) * RES_DRIVE;
    }
    int32_t last_y[ORCH_RESONATORS] = { 0 };

    /* All in 32 bits (0.7.0; before, five 64-bit products a string a
     * sample): the read's fraction on the difference, the feedback split
     * into its high and low halves so it stays exact. */
    for (unsigned f = 0; f < frames; ++f) {
        if (wet_now < wet_to) wet_now = wet_to - wet_now > WET_SLEW ? wet_now + WET_SLEW : wet_to;
        else if (wet_now > wet_to) wet_now = wet_now - wet_to > WET_SLEW ? wet_now - WET_SLEW : wet_to;
        const int32_t in = (stereo[f * 2U] >> 1) + (stereo[f * 2U + 1U] >> 1);
        const int32_t in_s = in >> 5;
        int32_t sum_l = 0, sum_r = 0;
        for (unsigned k = 0; k < ORCH_RESONATORS; ++k) {
            /* the read: `whole` back and the sample before it, by the fraction */
            const uint32_t i0 = (res_w + RES_SIZE - whole[k]) & RES_MASK;
            const uint32_t i1 = (i0 + RES_SIZE - 1U) & RES_MASK;
            const int32_t a = (int32_t)res_buf[k][i0] * 256, b = (int32_t)res_buf[k][i1] * 256;
            const int32_t y = a + ((((b - a) >> 2) * (int32_t)frac[k]) >> 6);
            /* a quarter-sample smoother: the strings' highs die first */
            const int32_t sm = y - (y - res_lp[k]) / 4;
            res_lp[k] = y;
            const int32_t g = (int32_t)RES_FB_Q15[k];
            const int32_t fb = (sm >> 15) * g + (((sm & 0x7FFF) * g) >> 15);
            {
                int32_t x = (((in_s * feed[k]) >> 10) + fb) / 256;
                if (x > 32767) x = 32767;
                if (x < -32768) x = -32768;
                res_buf[k][res_w] = (int16_t)x;
            }
            const int32_t yp = y >> 4;
            sum_l += yp * (int32_t)RES_PAN_L[k];
            sum_r += yp * (int32_t)(256U - RES_PAN_L[k]);
            last_y[k] = y;
        }
        res_w = (res_w + 1U) & RES_MASK;
        /* each string's left and right shares sum to 256 and there are
         * eight, so the bank's side averages the strings; it is added on top
         * of the dry signal at up to one half, inside the headroom the
         * 1.36 / n mix leaves */
        const int32_t rl = sum_l >> 7, rr = sum_r >> 7;
        stereo[f * 2U] += ((rl >> 8) * wet_now) >> 7;
        stereo[f * 2U + 1U] += ((rr >> 8) * wet_now) >> 7;
    }
    /* how hard each rings, for the panel: a slow peak follower, once a block */
    for (unsigned k = 0; k < ORCH_RESONATORS; ++k) {
        const uint32_t m = (uint32_t)(last_y[k] < 0 ? -last_y[k] : last_y[k]);
        if (m > res_follow[k]) res_follow[k] = m;
        else res_follow[k] -= (res_follow[k] >> 4) + 1U;
        if (res_follow[k] > 0x7FFFFFFFU) res_follow[k] = 0U;
    }
}
