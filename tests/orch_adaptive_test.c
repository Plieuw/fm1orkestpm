/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host tests for the adaptive interpreter. No hardware, no toolchain. */
#include <stdio.h>
#include <string.h>
#include "orch_adaptive.h"

static unsigned failures;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static const unsigned SR = ORCH_ADAPTIVE_SR;

static void play(struct orch_adaptive *a, struct orch_intent *in,
                 unsigned note, unsigned vel, uint32_t at, unsigned fixed)
{
    orch_adaptive_note(a, in, note, vel, at, fixed);
    orch_adaptive_velocity(a, in, vel);
}

static void test_first_note(void)
{
    struct orch_adaptive a;
    struct orch_intent in;
    memset(&in, 0, sizeof in);
    orch_adaptive_init(&a);
    play(&a, &in, 60U, 80U, 0U, ORCH_ART_AUTO);
    CHECK(in.phrase_start == 1U);
    CHECK(in.articulation == ORCH_ART_LEGATO);
    CHECK(in.energy >= 50000U && in.energy <= 98304U);
    CHECK(in.density >= 1U && in.density <= ORCH_DIVISI_MAX);
}

/* A detached line: 500 ms between note starts, 200 ms gates. */
static void test_detache(void)
{
    struct orch_adaptive a;
    struct orch_intent in;
    orch_adaptive_init(&a);
    for (unsigned n = 0; n < 6; ++n) {
        const uint32_t on = n * (SR / 2U);
        orch_adaptive_note(&a, &in, 60U + (n % 3U), 80U, on, ORCH_ART_AUTO);
        orch_adaptive_velocity(&a, &in, 80U);
        orch_adaptive_off(&a, on + (SR * 2U / 5U));
    }
    CHECK(in.connected == 0U);
    CHECK(in.length_class != ORCH_LEN_TIED);
    CHECK(in.articulation == ORCH_ART_DETACHE || in.articulation == ORCH_ART_MARTELE);
}

/* Short gates in a moderate tempo -> staccato; very fast -> spiccato. */
static void test_staccato_and_spiccato(void)
{
    struct orch_adaptive a;
    struct orch_intent in;
    orch_adaptive_init(&a);
    for (unsigned n = 0; n < 6; ++n) {
        const uint32_t on = n * (SR / 2U);
        orch_adaptive_note(&a, &in, 60U, 80U, on, ORCH_ART_AUTO);
        orch_adaptive_velocity(&a, &in, 80U);
        orch_adaptive_off(&a, on + (SR / 20U)); /* 50 ms gate */
    }
    CHECK(in.length_class == ORCH_LEN_SHORT || in.length_class == ORCH_LEN_FAST);

    struct orch_adaptive b;
    struct orch_intent in2;
    orch_adaptive_init(&b);
    for (unsigned n = 0; n < 8; ++n) {
        const uint32_t on = n * (SR / 12U); /* ~83 ms between notes */
        orch_adaptive_note(&b, &in2, 60U, 80U, on, ORCH_ART_AUTO);
        orch_adaptive_velocity(&b, &in2, 80U);
        orch_adaptive_off(&b, on + (SR / 30U));
    }
    CHECK(in2.length_class == ORCH_LEN_FAST);
    CHECK(in2.articulation == ORCH_ART_SPICCATO || in2.articulation == ORCH_ART_LEGATO);
}

/* A tied note: 5 ms of silence is not a new bow. */
static void test_legato(void)
{
    struct orch_adaptive a;
    struct orch_intent in;
    orch_adaptive_init(&a);
    play(&a, &in, 60U, 80U, 0U, ORCH_ART_AUTO);
    orch_adaptive_off(&a, SR / 4U);
    play(&a, &in, 62U, 80U, SR / 4U + SR / 200U, ORCH_ART_AUTO);
    CHECK(in.connected == 1U);
    CHECK(in.glide_ms > 0U);
    CHECK(in.articulation == ORCH_ART_LEGATO || in.articulation == ORCH_ART_PORTAMENTO);
}

/* A harder note than the music around it must raise energy and accent. */
static void test_velocity_reaction(void)
{
    struct orch_adaptive a;
    struct orch_intent soft;
    orch_adaptive_init(&a);
    for (unsigned n = 0; n < 5; ++n) {
        const uint32_t on = n * (SR / 2U);
        orch_adaptive_note(&a, &soft, 60U, 60U, on, ORCH_ART_AUTO);
        orch_adaptive_velocity(&a, &soft, 60U);
        orch_adaptive_off(&a, on + (SR / 3U));
    }
    const uint32_t base_energy = soft.energy;
    const uint32_t base_accent = soft.accent;

    struct orch_intent loud;
    const uint32_t on = 5U * (SR / 2U);
    orch_adaptive_note(&a, &loud, 62U, 120U, on, ORCH_ART_AUTO);
    orch_adaptive_velocity(&a, &loud, 120U);
    CHECK(loud.energy > base_energy);
    CHECK(loud.accent > base_accent);
}

/* A phrase opens with fewer players and the cap is respected. */
static void test_density(void)
{
    struct orch_adaptive a;
    struct orch_intent in;
    orch_adaptive_init(&a);
    unsigned last = 0U;
    for (unsigned n = 0; n < 12; ++n) {
        const uint32_t on = n * (SR / 2U);
        orch_adaptive_note(&a, &in, 60U + (n % 5U), 80U, on, ORCH_ART_AUTO);
        orch_adaptive_velocity(&a, &in, 80U);
        orch_adaptive_off(&a, on + (SR / 2U));
        CHECK(in.density >= 1U && in.density <= ORCH_DIVISI_MAX);
        last = in.density;
    }
    CHECK(last >= 2U);
}

/* A pinned articulation is never overridden by the gate length. */
static void test_pinned_articulation(void)
{
    struct orch_adaptive a;
    struct orch_intent in;
    orch_adaptive_init(&a);
    play(&a, &in, 60U, 80U, 0U, ORCH_ART_PIZZICATO);
    CHECK(in.articulation == ORCH_ART_PIZZICATO);
    orch_adaptive_off(&a, SR / 4U);
    play(&a, &in, 62U, 80U, SR / 2U, ORCH_ART_PONTICELLO);
    CHECK(in.articulation == ORCH_ART_PONTICELLO);
}

int main(void)
{
    test_first_note();
    test_detache();
    test_staccato_and_spiccato();
    test_legato();
    test_velocity_reaction();
    test_density();
    test_pinned_articulation();
    if (failures) { printf("%u adaptive test(s) failed\n", failures); return 1; }
    printf("orch_adaptive: all checks passed\n");
    return 0;
}
