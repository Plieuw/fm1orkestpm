/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host measurement harness. Not a pass/fail test: it prints the numbers that
 * claims in the docs are based on, so another agent can re-measure instead of
 * trusting a commit message.
 *
 *   measure          one line per family/note
 *   measure repeat   the same note six times, which shows how much the timbre
 *                    depends on the initial transient
 *
 * Build via tests/run_measure.sh. Everything uses the public engine API only.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "orch_engine.h"

#define PI 3.14159265358979323846
#define SR 44100.0
#define BLOCKS 300
#define SKIP_BLOCKS 150   /* ignore the attack when analysing */
#define CAP (128 * BLOCKS)

static int32_t block[256];
static double mono[CAP];

static double note_hz(unsigned note)
{
    return 440.0 * pow(2.0, ((double)note - 69.0) / 12.0);
}

/* Normalized autocorrelation at one lag. */
static double corr(long n, long lag)
{
    double num = 0, a = 0, b = 0;
    if (lag < 1 || n <= lag) return 0;
    for (long i = 0; i + lag < n; ++i) {
        const double x = mono[i], y = mono[i + lag];
        num += x * y; a += x * x; b += y * y;
    }
    return (a > 0 && b > 0) ? num / sqrt(a * b) : 0;
}

/* Goertzel magnitude of one bin over the analysis window. */
static double bin(long n, double hz)
{
    const double w = 2.0 * PI * hz / SR, c = 2.0 * cos(w);
    double s1 = 0, s2 = 0;
    for (long i = 0; i < n; ++i) {
        const double s = mono[i] + c * s1 - s2;
        s2 = s1; s1 = s;
    }
    const double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return p > 0 ? sqrt(p) / (double)n : 0;
}

/* Pitch inside a +-7 % window around the expected period, with parabolic
 * refinement. The window is what makes this immune to the octave errors a wide
 * search makes on a waveform whose second harmonic is strong. */
static double pitch_cents(long n, double want, double *r_out, double *hz_out)
{
    long lo = (long)(want * 0.93), hi = (long)(want * 1.07);
    if (hi > n / 2) hi = n / 2;
    double best = -2; long bl = lo;
    for (long L = lo; L <= hi; ++L) {
        const double r = corr(n, L);
        if (r > best) { best = r; bl = L; }
    }
    const double rm = corr(n, bl - 1), rp = corr(n, bl + 1);
    const double den = rm - 2.0 * best + rp;
    double off = den != 0 ? 0.5 * (rm - rp) / den : 0.0;
    if (off > 0.5) off = 0.5;
    if (off < -0.5) off = -0.5;
    if (r_out) *r_out = best;
    const double got = SR / ((double)bl + off);
    if (hz_out) *hz_out = got;
    return 1200.0 * log(got / (SR / want)) / log(2.0);
}

static void render_note(unsigned family, unsigned note, unsigned velocity,
                        long *n_out, long long *peak, long long *dc)
{
    orch_init();
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        orch_engine_configure_part(p, family, p == 0 ? 0x000F : 0);
    orch_note_event(0, note, velocity);

    long n = 0;
    long long pk = 0, sum = 0, cnt = 0;
    for (int b = 0; b < BLOCKS; ++b) {
        memset(block, 0, sizeof block);
        orch_render_q23(block, 128);
        for (unsigned i = 0; i < 256; i += 2) {
            const long v = block[i];
            const long a = v < 0 ? -v : v;
            if (a > pk) pk = a;
            if (b >= SKIP_BLOCKS && n < CAP) mono[n++] = (double)v;
            if (b >= SKIP_BLOCKS) { sum += v; ++cnt; }
        }
    }
    *n_out = n;
    *peak = pk;
    *dc = cnt ? sum / cnt : 0;
}

static void one(const char *family_name, unsigned family, unsigned note,
                unsigned velocity)
{
    long n;
    long long peak, dc;
    render_note(family, note, velocity, &n, &peak, &dc);
    const double want = SR / note_hz(note);
    double r = 0, hz = 0;
    const double cents = pitch_cents(n, want, &r, &hz);
    const double h1 = bin(n, note_hz(note));
    const double h2 = bin(n, 2.0 * note_hz(note));
    const double h3 = bin(n, 3.0 * note_hz(note));
    printf("%-9s note=%3u vel=%3u  %7.1f Hz (want %7.1f) %+6.1f c  r=%.2f"
           "  peak=%8lld  dc=%6lld  h2/h1=%5.2f h3/h1=%5.2f\n",
           family_name, note, velocity, hz, note_hz(note),
           cents, r, peak, dc, h1 > 0 ? h2 / h1 : 0, h1 > 0 ? h3 / h1 : 0);
}

static void repeats(unsigned family, unsigned note, unsigned velocity)
{
    long n;
    long long peak, dc;
    for (unsigned k = 0; k < 6; ++k) {
        render_note(family, note, velocity, &n, &peak, &dc);
        const double want = SR / note_hz(note);
        double r = 0, hz = 0;
        const double cents = pitch_cents(n, want, &r, &hz);
        const double h1 = bin(n, note_hz(note));
        const double h2 = bin(n, 2.0 * note_hz(note));
        const double h3 = bin(n, 3.0 * note_hz(note));
        printf("take %u  %+6.1f c  r=%.2f  h2/h1=%5.2f h3/h1=%5.2f  peak=%lld\n",
               k, cents, r, h1 > 0 ? h2 / h1 : 0, h1 > 0 ? h3 / h1 : 0, peak);
    }
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "soft") == 0) {
        /* the soft end of the even velocity line: still a tone? */
        const char *fam[ORCH_FAM_COUNT] = {
            "strings", "woodwinds", "brass", "mallets", "choir"
        };
        const unsigned vels[5] = { 8U, 20U, 40U, 64U, 100U };
        for (unsigned f = 0; f < ORCH_FAM_COUNT; ++f)
            for (unsigned k = 0; k < 5; ++k)
                one(fam[f], f, 57U, vels[k]);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "repeat") == 0) {
        repeats(ORCH_FAM_STRINGS, 60U, 100U);
        return 0;
    }
    const char *names[ORCH_FAM_COUNT] = {
        "strings", "woodwinds", "brass", "mallets", "choir"
    };
    const unsigned notes[3] = { 45U, 60U, 76U };
    for (unsigned f = 0; f < ORCH_FAM_COUNT; ++f)
        for (unsigned k = 0; k < 3; ++k)
            one(names[f], f, notes[k], 100U);
    return 0;
}
