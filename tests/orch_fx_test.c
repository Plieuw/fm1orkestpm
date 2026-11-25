/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test for the master effect chain: bypass is bit exact, the echo arrives
 * at the delay time it was set to, the reverb tail decays, shimmer changes the
 * tail, and driving every effect hard stays inside the Q23 range. */
#include <stdio.h>
#include <string.h>

#include "orch_fx.h"

#define Q23_MAX 8388607
#define Q23_MIN (-8388608)
#define FRAMES 128U
#define BIG 6000U

static int failures;
static int32_t block[FRAMES * 2U];
static int32_t big[BIG * 2U];

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static int32_t abs32(int32_t v) { return v < 0 ? -v : v; }

static void only(unsigned slot)
{
    for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s)
        orch_fx_set_enabled(s, s == slot);
}

static void test_bypass(void)
{
    orch_fx_init();
    for (unsigned i = 0; i < FRAMES * 2U; ++i)
        block[i] = (int32_t)(i * 7919U % 4000001U) - 2000000;
    int32_t copy[FRAMES * 2U];
    memcpy(copy, block, sizeof copy);
    orch_fx_process(block, FRAMES);
    CHECK(memcmp(copy, block, sizeof copy) == 0);
}

static void test_api(void)
{
    orch_fx_init();
    CHECK(orch_fx_set_mix(ORCH_FX_CHORUS, 250) == 0);
    CHECK(orch_fx_mix(ORCH_FX_CHORUS) == 100U);
    CHECK(orch_fx_set_param(ORCH_FX_REVERB, 0U, 101) == 0);
    CHECK(orch_fx_param(ORCH_FX_REVERB, 0U) == 100U);
    CHECK(orch_fx_set_param(ORCH_FX_CHORUS, ORCH_FX_PARAMS, 50) == -1);
    CHECK(orch_fx_param(ORCH_FX_CHORUS, ORCH_FX_PARAMS) == 0U);
    CHECK(orch_fx_set_enabled(99U, 1U) == -1);
    CHECK(orch_fx_enabled(99U) == 0U);
    CHECK(orch_fx_name(ORCH_FX_DELAY) != 0);
    /* four parameters an effect, and each one says what it is */
    CHECK(strcmp(orch_fx_param_name(ORCH_FX_DELAY, 0U), "TIME") == 0);
    CHECK(strcmp(orch_fx_param_name(ORCH_FX_DELAY, 2U), "SYNC") == 0);
    CHECK(strcmp(orch_fx_param_name(ORCH_FX_DELAY, 3U), "DAMP") == 0);
    CHECK(strcmp(orch_fx_param_name(ORCH_FX_REVERB, 1U), "SHIM") == 0);
    CHECK(strcmp(orch_fx_param_name(ORCH_FX_REVERB, 2U), "EARLY") == 0);
    CHECK(strcmp(orch_fx_param_name(ORCH_FX_CHORUS, 3U), "TONE") == 0);
    CHECK(strcmp(orch_fx_param_name(ORCH_FX_ENSEMBLE, 2U), "WIDTH") == 0);
    CHECK(strcmp(orch_fx_param_name(99U, 0U), "?") == 0);
    for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s) CHECK(orch_fx_enabled(s) == 0U);
    CHECK(orch_fx_any_enabled() == 0U);
    orch_fx_set_enabled(ORCH_FX_DELAY, 1U);
    CHECK(orch_fx_any_enabled() == 1U);
}

static void test_delay(void)
{
    orch_fx_init();
    only(ORCH_FX_DELAY);
    orch_fx_set_mix(ORCH_FX_DELAY, 100);
    orch_fx_set_param(ORCH_FX_DELAY, 0U, 0);   /* shortest time: 14 ms = 600 on
                                                * the FM-1's compact line */
    orch_fx_set_param(ORCH_FX_DELAY, 1U, 50);

    memset(big, 0, sizeof big);
    big[0] = 400000;
    orch_fx_process(big, BIG);

    const int32_t first = big[600U * 2U];
    /* ping-pong: the second echo of the left channel comes back on the right */
    const int32_t second = big[(600U * 2U) * 2U + 1U];
    CHECK(first != 0);
    CHECK(abs32(big[560U * 2U]) < 1000);      /* silence before the echo */
    CHECK(second != 0);
    CHECK(abs32(second) < abs32(first));      /* feedback decays */
    CHECK(first >= Q23_MIN && first <= Q23_MAX);

    /* mix 0 leaves the echo out entirely */
    orch_fx_set_mix(ORCH_FX_DELAY, 0);
    memset(big, 0, sizeof big);
    big[0] = 400000;
    orch_fx_process(big, BIG);
    CHECK(abs32(big[600U * 2U]) < 2000);
}

/* SYNC puts the echo on the beat instead of where the time knob points. The
 * FM-1's line is 93 ms, so the tests use tempos whose sixteenth fits: 240 BPM
 * is 2 756 samples and 180 BPM 3 675, and the free time the knob asked for
 * (600) stays empty. A division longer than the line halves until it fits. */
static void test_sync(void)
{
    orch_fx_init();
    orch_fx_set_tempo(240U);
    only(ORCH_FX_DELAY);
    orch_fx_set_mix(ORCH_FX_DELAY, 100);
    orch_fx_set_param(ORCH_FX_DELAY, 0U, 0);     /* free time would be 600 */
    orch_fx_set_param(ORCH_FX_DELAY, 1U, 0);     /* one echo, no tail */
    orch_fx_set_param(ORCH_FX_DELAY, 2U, 10);    /* SYNC: a sixteenth */

    memset(big, 0, sizeof big);
    big[0] = 400000;
    orch_fx_process(big, BIG);
    CHECK(abs32(big[2756U * 2U]) > 10000);       /* on the beat */
    CHECK(abs32(big[600U * 2U]) < 2000);         /* not on the knob's time */

    /* slower tempo, later echo */
    orch_fx_set_tempo(180U);
    orch_fx_reset();
    memset(big, 0, sizeof big);
    big[0] = 400000;
    orch_fx_process(big, BIG);
    CHECK(abs32(big[2756U * 2U]) < 2000);
    CHECK(abs32(big[3675U * 2U]) > 10000);

    /* 120 BPM: a sixteenth (5 512) does not fit, so it lands on the 32nd */
    orch_fx_set_tempo(120U);
    orch_fx_reset();
    memset(big, 0, sizeof big);
    big[0] = 400000;
    orch_fx_process(big, BIG);
    CHECK(abs32(big[2756U * 2U]) > 10000);
}

/* One run of the reverb, returning the energy of an early and a late window.
 * With `sine` the input is a short tone burst, otherwise an impulse. */
static void reverb_run(unsigned shimmer, int sine, uint64_t *early, uint64_t *late)
{
    orch_fx_init();
    only(ORCH_FX_REVERB);
    orch_fx_set_mix(ORCH_FX_REVERB, 100);
    orch_fx_set_param(ORCH_FX_REVERB, 0U, 60);
    orch_fx_set_param(ORCH_FX_REVERB, 1U, shimmer);

    uint64_t e1 = 0, e2 = 0;
    for (unsigned base = 0; base < 40000U; base += FRAMES) {
        memset(block, 0, sizeof block);
        for (unsigned f = 0; f < FRAMES; ++f) {
            const unsigned n = base + f;
            int32_t in = 0;
            if (sine) {
                if (n < 4410U) {
                    /* square is enough of an excitation and stays fixed point */
                    in = (n / 50U) % 2U ? 700000 : -700000;
                }
            } else if (n == 0U) {
                in = 700000;
            }
            block[f * 2U] = in;
            block[f * 2U + 1U] = in;
        }
        orch_fx_process(block, FRAMES);
        for (unsigned f = 0; f < FRAMES; ++f) {
            const unsigned n = base + f;
            const int64_t v = block[f * 2U];
            const uint64_t a = (uint64_t)(v < 0 ? -v : v);
            if (n >= 200U && n < 4400U) e1 += a;
            if (n >= 14000U && n < 22000U) e2 += a;
        }
    }
    *early = e1;
    *late = e2;
}

static void test_reverb(void)
{
    uint64_t early = 0, late = 0, shimmer_early = 0, shimmer_late = 0;

    reverb_run(0U, 0, &early, &late);
    CHECK(early > 0U);
    CHECK(late > 0U);
    CHECK(late < early);              /* the tail decays */

    reverb_run(70U, 1, &shimmer_early, &shimmer_late);
    uint64_t dry_early = 0, dry_late = 0;
    reverb_run(0U, 1, &dry_early, &dry_late);
    CHECK(shimmer_late > 0U);
    CHECK(shimmer_late != dry_late);  /* the octave-up loop changes the tail */
    CHECK(shimmer_late < shimmer_early);   /* and still dies with it */
    /* the loop must not run away at its widest either */
    uint64_t wide_early = 0, wide_late = 0;
    orch_fx_init();
    reverb_run(70U, 1, &wide_early, &wide_late);
    CHECK(wide_late < wide_early);
}

static void test_bounded(void)
{
    orch_fx_init();
    for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s) {
        orch_fx_set_enabled(s, 1U);
        orch_fx_set_mix(s, 100U);
        for (unsigned i = 0; i < ORCH_FX_PARAMS; ++i)
            orch_fx_set_param(s, i, 100U);
    }
    int32_t peak = 0;
    uint32_t phase = 0;
    for (unsigned b = 0; b < 600U; ++b) {          /* ~1.7 s of full-scale input */
        for (unsigned f = 0; f < FRAMES; ++f) {
            phase += 1234567U;
            const int32_t v = (int32_t)(phase >> 8) - 4194304; /* +-half scale */
            block[f * 2U] = v;
            block[f * 2U + 1U] = -v;
        }
        orch_fx_process(block, FRAMES);
        for (unsigned i = 0; i < FRAMES * 2U; ++i) {
            const int32_t a = abs32(block[i]);
            if (a > peak) peak = a;
        }
    }
    CHECK(peak > 0);
    for (unsigned i = 0; i < FRAMES * 2U; ++i) {
        CHECK(block[i] >= Q23_MIN);
        CHECK(block[i] <= Q23_MAX);
    }

    /* panic flushes the tails */
    orch_fx_reset();
    memset(block, 0, sizeof block);
    orch_fx_process(block, FRAMES);
    int32_t after = 0;
    for (unsigned i = 0; i < FRAMES * 2U; ++i) {
        if (abs32(block[i]) > after) after = abs32(block[i]);
    }
    CHECK(after == 0);
}

int main(void)
{
    test_bypass();
    test_api();
    test_delay();
    test_sync();
    test_reverb();
    test_bounded();

    if (failures) {
        printf("orch_fx: %d failure(s)\n", failures);
        return 1;
    }
    printf("orch_fx: all checks passed\n");
    return 0;
}
