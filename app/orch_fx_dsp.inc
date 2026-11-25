/* --- helpers for the chain ------------------------------------------------ */

static int32_t clamp15(int32_t x)
{
    if (x > Q15_MAX) return Q15_MAX;
    if (x < Q15_MIN) return Q15_MIN;
    return x;
}

/* A shift that rounds towards zero. The reverb's loops use it: an arithmetic
 * shift rounds negative values down, and with the damping below that bias
 * kept a decayed reverb on a constant offset (measured 8..16 LSB of Q15 at
 * the output, for ever) instead of reaching silence. */
static int32_t shr0(int32_t x, unsigned bits)
{
    return x < 0 ? -((-x) >> bits) : x >> bits;
}

/* The combs' damping low-pass moves by at least one step towards its input,
 * so it reaches the input exactly: with a plain shift it stopped short by up
 * to 255/DAMP steps, and the comb fed that remainder back for ever. */
static int32_t step_to(int32_t x, unsigned bits)
{
    const int32_t round = (1 << bits) - 1;
    return x < 0 ? -((-x + round) >> bits) : (x + round) >> bits;
}

static int32_t tri_q14(uint32_t phase)
{
    const uint32_t p = phase >> 16;                       /* one cycle */
    const uint32_t t = (p < 32768U) ? p : 65535U - p;     /* 0..32768 */
    return (int32_t)t - 16384;                            /* +/- half swing */
}

/* One cycle per second is 2^32 in phase; the rate is in milli-hertz so the
 * slowest chorus (0.15 Hz) is still an integer. 2^32 / 44 100 000 is
 * 97.3920, kept as 97 + 25690/65536 so the PI32v2 needs no 64-bit divide. */
static uint32_t lfo_inc(unsigned percent, unsigned min_mhz, unsigned max_mhz)
{
    const uint32_t mhz = min_mhz + ((max_mhz - min_mhz) * percent) / 100U;
    return mhz * 97U + ((mhz * 25690U) >> 16);
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

/* One pole low pass: `k` is how much of the input gets through in Q8, so 256 is
 * no filtering at all. Used for the chorus's TONE and the echo's DAMP. */
static int32_t lowpass(int32_t state, int32_t in, int32_t k)
{
    return state + (((in - state) * k) >> 8);
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

/* The echo's SYNC: sixty to one, the time snaps to a division of the beat the
 * sequencer runs at, so two echoes land on the bar instead of beside it. 0 is
 * free time, then an eighth, an eighth triplet and a sixteenth — the longest
 * the line holds at any tempo. */
static int32_t sync_samples(unsigned percent)
{
    if (!percent) return 0;
    const unsigned quarter = (ORCH_SAMPLE_RATE * 60U) / (fx_tempo ? fx_tempo : 120U);
    unsigned div = 4U;                                   /* sixteenth */
    if (percent > 66U) div = 2U;                         /* eighth */
    else if (percent > 33U) div = 3U;                    /* eighth triplet */
    unsigned samples = quarter / div;
    /* a line shorter than the division halves it until it fits: still on the
     * grid, only a finer one (the FM-1 line is 186 ms) */
    while (samples > DLY_SIZE - 2U) samples /= 2U;
    if (samples < 64U) samples = 64U;
    return (int32_t)samples;
}

/* --- the chain ------------------------------------------------------------ */
/* Included by orch_fx.c: the settings and the buffers are above, so this is
 * only the loop that walks the frames. */

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

    /* Chorus: speed, sweep, how far the two sides are apart, and how dark the
     * wet path is. SPREAD at 0 sweeps both sides together, at 100 half a cycle
     * apart, which is the width the chorus had before it was a parameter. */
    const uint32_t chorus_inc = lfo_inc(fx_p[ORCH_FX_CHORUS][0], 150U, 5000U);
    const unsigned chorus_depth = 60U + (unsigned)fx_p[ORCH_FX_CHORUS][1] * 5U / 2U;
    const uint32_t chorus_spread = (uint32_t)fx_p[ORCH_FX_CHORUS][2] * 21474836U;
    const int32_t chorus_tone = 40 + (int32_t)fx_p[ORCH_FX_CHORUS][3] * 210 / 100;

    /* Ensemble: the same two, then WIDTH spaces the three taps further apart or
     * closer together and TAPS takes them from one to three. */
    const uint32_t ensemble_inc = lfo_inc(fx_p[ORCH_FX_ENSEMBLE][0], 90U, 1800U);
    const unsigned ensemble_depth = 60U + (unsigned)fx_p[ORCH_FX_ENSEMBLE][1] * 4U;
    const unsigned ensemble_taps = 1U + (unsigned)fx_p[ORCH_FX_ENSEMBLE][3] * 2U / 100U;
    const unsigned ensemble_width = 60U + (unsigned)fx_p[ORCH_FX_ENSEMBLE][2] * 80U / 100U;

    /* Echo: free time (14..145 ms on the FM-1's line, 60..259 roomy), or — with SYNC — the same line snapped to the
     * beat; feedback up to 90 %, and DAMP takes the top off every repeat. */
    const int32_t delay_sync = sync_samples(fx_p[ORCH_FX_DELAY][2]);
    const int32_t delay_target = delay_sync ? delay_sync
                                            : DELAY_MIN + (int32_t)fx_p[ORCH_FX_DELAY][0] * DELAY_STEP;
    const int32_t delay_fb = (int32_t)fx_p[ORCH_FX_DELAY][1] * 295;
    const int32_t delay_damp = 256 - (int32_t)fx_p[ORCH_FX_DELAY][3] * 200 / 100;
    if (dly_cur < 0) dly_cur = delay_target;

    /* Reverb: 0.70..0.92 comb feedback, shimmer capped at 70 % so the loop
     * decays, EARLY as the level of the reflections in front of the network,
     * and DAMP as how much top the combs lose each pass. */
    const int32_t rev_fb = 22938 + (int32_t)fx_p[ORCH_FX_REVERB][0] * 72;
    unsigned shimmer = fx_p[ORCH_FX_REVERB][1];
    if (shimmer > 70U) shimmer = 70U;
    /* The octave goes back into the network, whose gain at the comb
     * frequencies is about 1 / (1 - feedback): 6x at DECAY 60. Fed in as it
     * was, SHIM 40 made the tail grow until it clipped (measured: x50 in a
     * second on the 0.4.0 sizes, x1200 on the FM-1's). Scaling it by
     * (1 - feedback) keeps the loop gain at SHIM x 0.7 at most, so the
     * shimmer rings with the tail and dies with it. */
    const unsigned shimmer_q8 =
        (mix_q8(shimmer) * (unsigned)(32768 - rev_fb) * 45U) >> 20;
    const unsigned early_q8 = mix_q8(fx_p[ORCH_FX_REVERB][2]);
    const int32_t comb_damp = 90 - (int32_t)fx_p[ORCH_FX_REVERB][3] * 70 / 100;

    for (unsigned f = 0; f < frames; ++f) {
        int32_t l = stereo[f * 2U] >> 8;
        int32_t r = stereo[f * 2U + 1U] >> 8;
        const int32_t dry_l = l, dry_r = r;

        if (chorus_on || ensemble_on) {
            mod_l[mod_w] = (int16_t)clamp15(dry_l);
            mod_r[mod_w] = (int16_t)clamp15(dry_r);
            if (chorus_on) {
                chorus_phase += chorus_inc;
                int32_t wl = mod_tap(mod_l, 512, chorus_phase, chorus_depth, 0U);
                int32_t wr = mod_tap(mod_r, 512, chorus_phase, chorus_depth,
                                     chorus_spread);
                chorus_lp_l = lowpass(chorus_lp_l, wl, chorus_tone);
                chorus_lp_r = lowpass(chorus_lp_r, wr, chorus_tone);
                l = crossfade(l, chorus_lp_l, chorus_mix);
                r = crossfade(r, chorus_lp_r, chorus_mix);
            }
            if (ensemble_on) {
                ensemble_phase += ensemble_inc;
                /* up to three taps, each with its own LFO phase */
                static const uint16_t base[3] = { ENS_BASE };
                int32_t wl = 0, wr = 0;
                for (unsigned t = 0; t < ensemble_taps; ++t) {
                    const uint32_t ph = ensemble_phase + t * 0x55555555U;
                    const int32_t span = (int32_t)base[t] * (int32_t)ensemble_width / 100;
                    wl += mod_tap(mod_l, span, ph, ensemble_depth, 0U);
                    wr += mod_tap(mod_r, span, ph, ensemble_depth, 0x20000000U);
                }
                l = crossfade(l, wl / (int32_t)ensemble_taps, ensemble_mix);
                r = crossfade(r, wr / (int32_t)ensemble_taps, ensemble_mix);
            }
            mod_w = (mod_w + 1U) & MOD_MASK;
        }

        if (delay_on) {
            if (dly_cur < delay_target) ++dly_cur;
            else if (dly_cur > delay_target) --dly_cur;
            const uint32_t back = (uint32_t)dly_cur;
            const int32_t el = lowpass(dly_lp_l,
                                       dly_l[(dly_w + DLY_SIZE - back) & DLY_MASK],
                                       delay_damp);
            const int32_t er = lowpass(dly_lp_r,
                                       dly_r[(dly_w + DLY_SIZE - back) & DLY_MASK],
                                       delay_damp);
            dly_lp_l = el;
            dly_lp_r = er;
            dly_l[dly_w] = (int16_t)clamp15(l + ((er * delay_fb) >> 15));
            dly_r[dly_w] = (int16_t)clamp15(r + ((el * delay_fb) >> 15));
            dly_w = (dly_w + 1U) & DLY_MASK;
            l = crossfade(l, el, delay_mix);
            r = crossfade(r, er, delay_mix);
        }

        if (reverb_on) {
            int32_t rin = (((l + r) >> 1) * 983) >> 16;   /* ~0.015 of the sum */

            /* the reflections: three short taps of the input, so the tail has a
             * floor under it before the combs have built up */
            er_buf[er_w] = (int16_t)clamp15((l + r) >> 1);
            if (early_q8) {
                const int32_t e1 = er_buf[(er_w + ER_SIZE - 331U) & ER_MASK];
                const int32_t e2 = er_buf[(er_w + ER_SIZE - 485U) & ER_MASK];
                const int32_t e3 = er_buf[(er_w + ER_SIZE - 719U) & ER_MASK];
                rin += (((e1 + e2 + e3) / 3) * (int32_t)early_q8) >> 8;
            }
            er_w = (er_w + 1U) & ER_MASK;

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
                    comb_lp[ch][c] += step_to((y - comb_lp[ch][c]) * comb_damp, 8);
                    const int32_t fb = shr0(comb_lp[ch][c] * rev_fb, 15);
                    comb_buf[ch][c][comb_w[ch][c]] = (int16_t)clamp15(x + fb);
                    if (++comb_w[ch][c] >= comb_len[ch][c]) comb_w[ch][c] = 0U;
                    sum += y;
                }
                x = shr0(sum, 2);
                for (unsigned a = 0; a < AP_COUNT; ++a) {
                    const int32_t y = ap_buf[ch][a][ap_w[ch][a]];
                    ap_buf[ch][a][ap_w[ch][a]] = (int16_t)clamp15(x + shr0(y, 1));
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
