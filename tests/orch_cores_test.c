/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The second core: the orchestra renders the same, bit for bit, on one core
 * or two — a second core that runs its job at once, one that is a real
 * thread (build with -fsanitize=thread to look for races), one that refuses
 * jobs, and one that takes a job and then stops answering. */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "orch_engine.h"
#include "orch_fx.h"
#include "orch_resonance.h"

static unsigned failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

#define BLOCKS 900U
static int32_t ref[BLOCKS][256], got[BLOCKS][256];

/* a busy orchestra: eight parts, chords, a sweeping pedal and bend, the
 * mod wheel, an ARP and the sequencer, through the hall's strings */
static void play(int32_t (*out)[256])
{
    orch_init();
    orch_resonance_set_amount(40U);
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const uint8_t vol[3] = { (uint8_t)(0xB0U | p), 7U, 127U };
        orch_midi_short_message(vol, 3U);
        orch_engine_set_player_cap(p, 2U);
    }
    struct orch_perf_config pc;
    orch_perf_get_config(2U, &pc);
    pc.arp_enabled = 1U;
    orch_perf_set_config(2U, &pc);
    struct orch_step_note sn = { 72U, 96U, 60U, 100U };
    orch_seq_set_part(5U);
    for (unsigned st = 0; st < 16U; st += 3U) orch_seq_set_step(st, 0U, &sn);
    orch_seq_set_enabled(1U);
    for (unsigned b = 0; b < BLOCKS; ++b) {
        if (b % 90U == 0U)
            for (unsigned p = 0; p < ORCH_PARTS; ++p)
                orch_note_event(p, 40U + 7U * p + (b / 90U) % 5U, 60U + (b * 7U + p * 11U) % 67U);
        if (b % 90U == 70U)
            for (unsigned p = 0; p < ORCH_PARTS; ++p)
                orch_note_event(p, 40U + 7U * p + (b / 90U) % 5U, 0U);
        if (b % 3U == 0U) {
            const uint8_t expr[3] = { 0xB1U, 11U, (uint8_t)(64U + (b % 64U)) };
            const uint8_t bend[3] = { 0xE3U, 0U, (uint8_t)(48U + (b % 32U)) };
            const uint8_t wheel[3] = { 0xB0U, 1U, (uint8_t)(b % 128U) };
            orch_midi_short_message(expr, 3U);
            orch_midi_short_message(bend, 3U);
            orch_midi_short_message(wheel, 3U);
        }
        memset(out[b], 0, sizeof out[b]);
        orch_render_q23(out[b], 128U);
    }
    orch_seq_set_enabled(0U);
}

static int same(void) { return memcmp(ref, got, sizeof ref) == 0; }

/* a second core that does its job at once */
static int run_now(void (*fn)(uint32_t), uint32_t arg) { fn(arg); return 1; }
static int wait_ok(void) { return 0; }

/* a real one: a thread a job */
static pthread_t worker;
static void (*job_fn)(uint32_t);
static uint32_t job_arg;
static void *thread_main(void *x) { (void)x; job_fn(job_arg); return 0; }
static int run_thread(void (*fn)(uint32_t), uint32_t arg)
{
    job_fn = fn; job_arg = arg;
    return pthread_create(&worker, 0, thread_main, 0) == 0;
}
static int wait_thread(void) { return pthread_join(worker, 0) != 0; }

/* one that will not take a job, and one that takes it and never answers */
static int run_never(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
static int run_drop(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 1; }
static int wait_gone(void) { return -1; }

int main(void)
{
    orch_fx_init();
    orch_set_second_core(0, 0);
    play(ref);
    {
        int64_t energy = 0;
        for (unsigned b = 0; b < BLOCKS; ++b)
            for (unsigned i = 0; i < 256U; ++i) energy += ref[b][i] < 0 ? -ref[b][i] : ref[b][i];
        CHECK(energy > 0);
    }

    uint32_t before = orch_second_core_chunks();
    orch_set_second_core(run_now, wait_ok);
    play(got);
    CHECK(same());
    CHECK(orch_second_core_chunks() - before > BLOCKS);   /* it really shared */

    before = orch_second_core_chunks();
    orch_set_second_core(run_thread, wait_thread);
    play(got);
    CHECK(same());
    CHECK(orch_second_core_chunks() - before > BLOCKS);

    orch_set_second_core(run_never, wait_ok);
    play(got);
    CHECK(same());

    orch_set_second_core(run_drop, wait_gone);
    play(got);
    CHECK(same());

    orch_set_second_core(0, 0);
    if (failures) { printf("%u core test(s) failed\n", failures); return 1; }
    printf("orch_cores: all checks passed (one core and two: the same, bit for bit)\n");
    return 0;
}
