/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include "orch_performance.h"
static unsigned failures;
#define CHECK(x) do { if(!(x)){++failures;printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);} } while(0)
static void test_arp_capture_and_release(void) {
    struct orch_perf_config c; struct orch_perf_event e;
    orch_perf_init(); orch_perf_default_config(&c); c.arp_enabled=1U;
    CHECK(orch_perf_set_config(0U,&c)==0);
    CHECK(orch_perf_note_event(0U,60U,100U)==1);
    orch_perf_advance_sample(0U);
    CHECK(orch_perf_pop_event(&e)==1);
    CHECK(e.part==0U && e.note==60U && e.velocity>0U && e.note_on==1U);
    CHECK(orch_perf_note_event(0U,60U,0U)==1);
    CHECK(orch_perf_pop_event(&e)==1 && e.note==60U && e.note_on==0U);
}
static void test_phrase_generator(void) {
    struct orch_perf_config c; struct orch_perf_event e;
    orch_perf_init(); orch_perf_default_config(&c); c.phrase_enabled=1U;
    c.phrase_ge=0U; c.density=127U; c.cluster=1U;
    CHECK(orch_perf_set_config(1U,&c)==0);
    CHECK(orch_perf_note_event(1U,64U,96U)==1);
    orch_perf_advance_sample(0U);
    CHECK(orch_perf_pop_event(&e)==1);
    CHECK(e.part==1U && e.note==64U && e.note_on==1U);
}
static void test_latch_and_clock_stop(void) {
    struct orch_perf_config c; struct orch_perf_event e;
    orch_perf_init(); orch_perf_default_config(&c); c.arp_enabled=1U;
    c.latch=1U; c.external_clock=1U;
    CHECK(orch_perf_set_config(2U,&c)==0);
    CHECK(orch_perf_note_event(2U,55U,90U)==1);
    orch_perf_midi_realtime(0xFAU,0U);
    orch_perf_midi_realtime(0xF8U,0U);
    CHECK(orch_perf_pop_event(&e)==1 && e.note_on==1U);
    CHECK(orch_perf_note_event(2U,55U,0U)==1);
    orch_perf_midi_realtime(0xFCU,128U);
    CHECK(orch_perf_pop_event(&e)==1 && e.note_on==0U);
}

static void test_all_pattern_slots(void)
{
    struct orch_perf_config c;
    struct orch_perf_event e;
    for (unsigned type=0U; type<ORCH_PERF_ARP_TYPES; ++type) {
        unsigned hits=0U;
        orch_perf_init(); orch_perf_default_config(&c);
        c.arp_enabled=1U; c.external_clock=1U; c.arp_type=(uint8_t)type;
        CHECK(orch_perf_set_config(0U,&c)==0);
        CHECK(orch_perf_note_event(0U,60U,100U)==1);
        orch_perf_midi_realtime(0xFAU,0U);
        for(unsigned tick=1U;tick<=200U;++tick) {
            orch_perf_midi_realtime(0xF8U,tick*918U);
            while(orch_perf_pop_event(&e)) if(e.note_on) ++hits;
        }
        CHECK(hits>0U);
    }
    for (unsigned ge=0U; ge<ORCH_PERF_GE_COUNT; ++ge) {
        unsigned hits=0U;
        orch_perf_init(); orch_perf_default_config(&c);
        c.phrase_enabled=1U; c.external_clock=1U; c.phrase_ge=(uint8_t)ge;
        c.density=127U; c.cluster=1U;
        CHECK(orch_perf_set_config(0U,&c)==0);
        CHECK(orch_perf_note_event(0U,64U,100U)==1);
        orch_perf_midi_realtime(0xFAU,0U);
        for(unsigned tick=1U;tick<=200U;++tick) {
            orch_perf_midi_realtime(0xF8U,tick*918U);
            while(orch_perf_pop_event(&e)) if(e.note_on) ++hits;
        }
        CHECK(hits>0U);
    }
}

int main(void) {
    test_arp_capture_and_release(); test_phrase_generator(); test_latch_and_clock_stop();
    test_all_pattern_slots();
    if(failures){printf("%u performance test(s) failed\n",failures);return 1;}
    puts("orch_performance: all checks passed"); return 0;
}
