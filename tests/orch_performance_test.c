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


static unsigned first_adaptive_arp_note(int descending)
{
    struct orch_perf_config c; struct orch_perf_event e;
    orch_perf_init(); orch_perf_default_config(&c);
    c.arp_enabled=1U; c.external_clock=1U; c.latch=0U;
    c.arp_type=18U; c.quantize=0U; c.division=4U;
    CHECK(orch_perf_set_config(0U,&c)==0);
    CHECK(orch_perf_note_event(0U,60U,90U)==1);
    CHECK(orch_perf_note_event(0U,64U,90U)==1);
    CHECK(orch_perf_note_event(0U,67U,90U)==1);
    if (descending) {
        CHECK(orch_perf_note_event(0U,72U,90U)==1);
        CHECK(orch_perf_note_event(0U,70U,90U)==1);
        CHECK(orch_perf_note_event(0U,72U,0U)==1);
        CHECK(orch_perf_note_event(0U,70U,0U)==1);
    } else {
        CHECK(orch_perf_note_event(0U,70U,90U)==1);
        CHECK(orch_perf_note_event(0U,72U,90U)==1);
        CHECK(orch_perf_note_event(0U,70U,0U)==1);
        CHECK(orch_perf_note_event(0U,72U,0U)==1);
    }
    orch_perf_midi_realtime(0xFAU,0U);
    for (unsigned tick=1U;tick<=12U;++tick) {
        orch_perf_midi_realtime(0xF8U,tick*918U);
        while (orch_perf_pop_event(&e)) if (e.note_on) return e.note;
    }
    return 255U;
}
static void test_adaptive_contour_changes_arp_direction(void)
{
    const unsigned ascending=first_adaptive_arp_note(0);
    const unsigned descending=first_adaptive_arp_note(1);
    CHECK(ascending==60U);
    CHECK(descending==67U);
    CHECK(ascending!=descending);
}

static void test_phrase_arc_has_peak_and_cadence(void)
{
    const unsigned start=orch_test_phrase_arc_velocity(100U,0U);
    const unsigned peak=orch_test_phrase_arc_velocity(100U,8U);
    const unsigned cadence=orch_test_phrase_arc_velocity(100U,15U);
    CHECK(start<peak);
    CHECK(peak>cadence);
    CHECK(cadence<start);
    /* the swell is steps on the even dB line: the same at pp as at ff */
    for (unsigned pos=0U;pos<16U;++pos)
        CHECK((int)orch_test_phrase_arc_velocity(40U,pos)-40 ==
              (int)orch_test_phrase_arc_velocity(100U,pos)-100);
}

/* The ARP's ear hears the played dynamic: the same keys under breath 0 read
 * as a quiet passage and thin the chord cluster; the held notes keep their
 * velocity. */
static void test_arp_hears_the_played_dynamic(void)
{
    unsigned loud_hits=0U, soft_hits=0U;
    for (unsigned pass=0U;pass<2U;++pass) {
        orch_perf_init();
        struct orch_perf_config c; orch_perf_default_config(&c);
        c.phrase_enabled=1U; c.cluster=2U;
        CHECK(orch_perf_set_config(0U,&c)==0);
        const unsigned played=pass?20U:100U;
        CHECK(orch_perf_note_event_played(0U,60U,100U,played)==1);
        CHECK(orch_perf_note_event_played(0U,64U,100U,played)==1);
        CHECK(orch_perf_note_event_played(0U,67U,100U,played)==1);
        unsigned hits=0U, velocity_ok=1U;
        for (uint32_t t=0U;t<2U*44100U;++t) {
            orch_perf_advance_sample(t);
            struct orch_perf_event e;
            while (orch_perf_pop_event(&e))
                if (e.note_on) { ++hits; if (e.velocity<60U) velocity_ok=0U; }
        }
        CHECK(velocity_ok);
        if (pass) soft_hits=hits; else loud_hits=hits;
    }
    CHECK(soft_hits>0U && soft_hits<loud_hits);
}

static void test_contour_anchor_arp_starts_near_player(void)
{
    struct orch_perf_config c; struct orch_perf_event e;
    unsigned first=255U;
    orch_perf_init(); orch_perf_default_config(&c);
    c.arp_enabled=1U; c.external_clock=1U; c.arp_type=22U;
    c.division=4U; c.latch=0U;
    CHECK(orch_perf_set_config(0U,&c)==0);
    CHECK(orch_perf_note_event(0U,60U,90U)==1);
    CHECK(orch_perf_note_event(0U,64U,90U)==1);
    CHECK(orch_perf_note_event(0U,67U,90U)==1);
    CHECK(orch_perf_note_event(0U,65U,90U)==1);
    CHECK(orch_perf_note_event(0U,70U,90U)==1);
    CHECK(orch_perf_note_event(0U,70U,0U)==1);
    CHECK(orch_perf_note_event(0U,65U,0U)==1);
    orch_perf_midi_realtime(0xFAU,0U);
    for(unsigned tick=1U;tick<=36U;++tick) {
        orch_perf_midi_realtime(0xF8U,tick*918U);
        while(orch_perf_pop_event(&e)) if(e.note_on && first==255U) first=e.note;
    }
    CHECK(first==67U);
}

/* Run an internal-clock ARP for `samples` and collect its note-ons. */
static unsigned run_ons(unsigned part, uint32_t samples, unsigned *notes, unsigned cap)
{
    struct orch_perf_event e;
    unsigned n=0U;
    for (uint32_t t=0U;t<samples;++t) {
        orch_perf_advance_sample(t);
        while (orch_perf_pop_event(&e))
            if (e.note_on && e.part==part) { if (n<cap) notes[n]=e.note; ++n; }
    }
    return n;
}

/* As played: the order of the keys, not of the pitches. */
static void test_arp_as_played(void)
{
    struct orch_perf_config c;
    unsigned got[8];
    orch_perf_init(); orch_perf_default_config(&c);
    c.arp_enabled=1U; c.arp_order=1U; c.arp_type=0U; c.division=7U; c.bpm=120U;
    orch_perf_set_config(0U,&c);
    orch_perf_note_event(0U,67U,100U);
    orch_perf_note_event(0U,60U,100U);
    orch_perf_note_event(0U,64U,100U);
    const unsigned n=run_ons(0U,22050U,got,8U);
    CHECK(n>=3U);
    CHECK(got[0]==67U && got[1]==60U && got[2]==64U);
    orch_perf_init(); c.arp_order=0U; orch_perf_set_config(0U,&c);
    orch_perf_note_event(0U,67U,100U);
    orch_perf_note_event(0U,60U,100U);
    orch_perf_note_event(0U,64U,100U);
    run_ons(0U,22050U,got,8U);
    CHECK(got[0]==60U && got[1]==64U && got[2]==67U);
}

/* Ratchet: twice as many hits in the same time. */
static void test_arp_ratchet(void)
{
    struct orch_perf_config c;
    unsigned got[64];
    orch_perf_init(); orch_perf_default_config(&c);
    c.arp_enabled=1U; c.division=4U; c.bpm=120U;
    orch_perf_set_config(0U,&c);
    orch_perf_note_event(0U,60U,100U);
    const unsigned one=run_ons(0U,44100U,got,64U);
    orch_perf_init(); c.ratchet=2U; orch_perf_set_config(0U,&c);
    orch_perf_note_event(0U,60U,100U);
    const unsigned two=run_ons(0U,44100U,got,64U);
    CHECK(one>=3U);
    CHECK(two>=2U*one-1U && two<=2U*one+1U);
}

/* Without the adaptive span a two-octave type stays within two octaves,
 * however busy the player is. */
static void test_arp_fixed_span(void)
{
    struct orch_perf_config c;
    unsigned got[64];
    orch_perf_init(); orch_perf_default_config(&c);
    c.arp_enabled=1U; c.arp_type=1U; c.division=9U; c.bpm=200U;
    orch_perf_set_config(0U,&c);
    for (unsigned k=0;k<6U;++k) { orch_perf_note_event(0U,60U+k,127U); orch_perf_note_event(0U,60U+k,0U); }
    orch_perf_note_event(0U,60U,127U);
    orch_perf_note_event(0U,64U,127U);
    const unsigned n=run_ons(0U,44100U,got,64U);
    unsigned top=0U;
    for (unsigned i=0;i<n&&i<64U;++i) if (got[i]>top) top=got[i];
    CHECK(n>0U && top<=64U+12U);
}

int main(void) {
    test_arp_as_played();
    test_arp_ratchet();
    test_arp_fixed_span();
    test_arp_capture_and_release(); test_phrase_generator(); test_latch_and_clock_stop();
    test_all_pattern_slots(); test_adaptive_contour_changes_arp_direction(); test_contour_anchor_arp_starts_near_player(); test_phrase_arc_has_peak_and_cadence();
    test_arp_hears_the_played_dynamic();
    if(failures){printf("%u performance test(s) failed\n",failures);return 1;}
    puts("orch_performance: all checks passed"); return 0;
}
