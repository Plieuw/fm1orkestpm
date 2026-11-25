/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * FM1 Orchestral performance engine.
 *
 * New implementation based on the musical feature set documented in
 * Plieuw/hawk-chua (main, e750df6a2e13490a4f01b84d3f9c553f4612163f):
 * arpeggiation, generated phrases, groove, MIDI clock and interlocking logic.
 * No HAWK CHUA source tables or implementation code are included here.
 *
 * Bounded state only: no allocation, locks, floating point or 64-bit divides.
 */
#include "orch_performance.h"

#define PERF_HELD 16U
#define PERF_ACTIVE 8U
#define PERF_QUEUE 64U
#define PERF_QUEUE_MASK (PERF_QUEUE - 1U)
#define PERF_MAX_STEP_NOTES 8U
#define PERF_Q16 65536U
#define PERF_CLOCK_TIMEOUT 22050U

static const uint8_t division_ticks[ORCH_PERF_DIVISIONS] =
    {36U,24U,18U,16U,12U,9U,8U,6U,4U,3U};

/* Original rhythm vocabulary for the 46 generated-phrase slots. A bit is
 * an event; patterns are deliberately compact and can be expanded later
 * without changing the public API. */
static const uint16_t phrase_rhythm[ORCH_PERF_GE_COUNT] = {
    0x5555U,0x2492U,0x1111U,0x9249U,0x3333U,0x0F0FU,0x8421U,0x6996U,
    0x8888U,0xEEEEU,0x1248U,0x8421U,0xA5A5U,0x5A5AU,0xF00FU,0x0FF0U,
    0x8889U,0x1119U,0xCCCCU,0x333CU,0x8428U,0x2842U,0x9696U,0x6969U,
    0xE4E4U,0x4E4EU,0x9248U,0x2489U,0xAAAAU,0x5556U,0xC3C3U,0x3C3CU,
    0xF888U,0x888FU,0xD6D6U,0x6D6DU,0xB6B6U,0x6B6BU,0xF0F8U,0x8F0FU,
    0xE1E1U,0x1E1EU,0xF111U,0x111FU,0xDB6DU,0x6DB6U
};

static const uint8_t velocity_grooves[8][8] = {
    {5,5,5,5,5,5,5,5}, {9,5,5,5,9,5,5,5},
    {9,3,6,3,9,3,6,3}, {5,0,7,0,5,0,7,0},
    {3,4,5,6,7,8,9,8}, {9,8,7,6,5,4,3,4},
    {9,3,3,6,9,3,3,6}, {5,5,0,5,5,0,5,0}
};
static const uint8_t gate_grooves[8][8] = {
    {5,5,5,5,5,5,5,5}, {8,2,8,2,8,2,8,2},
    {2,8,2,8,2,8,2,8}, {3,3,3,8,3,3,3,8},
    {8,5,3,8,5,3,8,5}, {2,4,6,8,6,4,2,4},
    {9,1,9,1,9,1,9,1}, {4,4,7,4,4,4,7,4}
};

typedef struct { uint8_t note, velocity; } held_note_t;
typedef struct { uint8_t note; uint32_t off_at; uint8_t used; } active_note_t;
typedef struct { uint8_t note, velocity, used; uint32_t on_at, gate_samples; } pending_note_t;
typedef struct {
    struct orch_perf_config cfg;
    held_note_t held[PERF_HELD];
    uint8_t held_count, keys_down, running, first_pending;
    active_note_t active[PERF_ACTIVE];
    pending_note_t pending[PERF_ACTIVE*3U];
    uint8_t active_count, pending_count;
    uint32_t step, phrase_step, clock_phase, clock_count;
    uint32_t clock_inc; /* cached BPM-to-Q16 phase increment */
    uint32_t last_sample, step_len, pending_at;
    uint32_t ext_last_tick, ext_tick_len, ext_age;
    uint8_t pending_step, alt_state, sr_state;
    uint8_t last_note, last_velocity;
    /* Player-derived musical context. Updated only on incoming MIDI notes. */
    uint8_t input_note, input_velocity_avg, input_activity, input_phrase_age;
    int8_t input_direction;
    uint8_t input_have_note, input_peak, input_after_rest;
    uint32_t input_last_on, input_ioi_avg;
    uint32_t rng;
} perf_part_t;

static perf_part_t parts[ORCH_PERF_PARTS];
static struct orch_perf_event queue[PERF_QUEUE];
static volatile uint8_t q_read, q_write;

static uint32_t rnd(perf_part_t *p)
{
    p->rng = p->rng * 1664525U + 1013904223U;
    return p->rng >> 8;
}

void orch_perf_default_config(struct orch_perf_config *c)
{
    if (!c) return;
    c->bpm=128U; c->arp_enabled=0U; c->phrase_enabled=0U;
    c->external_clock=0U; c->latch=0U; c->arp_type=0U; c->phrase_ge=0U;
    c->division=4U; c->gate=80U; c->swing=0U; c->logic=ORCH_PERF_OR;
    c->inverse=0U; c->quantize=0U; c->root=0U; c->density=100U;
    c->range=2U; c->accent=48U; c->cluster=1U; c->repeat=0U;
    c->velocity_groove=0U; c->gate_groove=0U;
    c->arp_order=0U; c->arp_adaptive=0U; c->ratchet=1U; c->strum=0U;
}

static void push_event(unsigned part, unsigned note, unsigned velocity, unsigned on)
{
    const uint8_t next=(uint8_t)((q_write+1U)&PERF_QUEUE_MASK);
    if (next==q_read) return;
    queue[q_write].part=(uint8_t)part;
    queue[q_write].note=(uint8_t)note;
    queue[q_write].velocity=(uint8_t)velocity;
    queue[q_write].note_on=(uint8_t)(on?1U:0U);
    q_write=next;
}

static void release_all(unsigned part, perf_part_t *p)
{
    for (unsigned i=0;i<PERF_ACTIVE;++i) {
        if (p->active[i].used) {
            push_event(part,p->active[i].note,0U,0U);
            p->active[i].used=0U;
        }
    }
    p->active_count=0U;
    p->pending_count=0U;
    p->pending_step=0U;
    for(unsigned i=0;i<PERF_ACTIVE*3U;++i) p->pending[i].used=0U;
}

static void active_off_due(unsigned part, perf_part_t *p, uint32_t now)
{
    for (unsigned i=0;i<PERF_ACTIVE;++i) {
        if (p->active[i].used && (int32_t)(now-p->active[i].off_at)>=0) {
            push_event(part,p->active[i].note,0U,0U);
            p->active[i].used=0U;
            if (p->active_count) --p->active_count;
        }
    }
}

static void generated_on(unsigned part, perf_part_t *p, unsigned note,
                         unsigned velocity, uint32_t now, uint32_t gate_samples)
{
    if (note>127U || !velocity) return;
    for (unsigned i=0;i<PERF_ACTIVE;++i) {
        if (p->active[i].used && p->active[i].note==note) {
            push_event(part,note,0U,0U);
            p->active[i].used=0U;
            if (p->active_count) --p->active_count;
        }
    }
    for (unsigned i=0;i<PERF_ACTIVE;++i) {
        if (!p->active[i].used) {
            p->active[i].used=1U;
            p->active[i].note=(uint8_t)note;
            p->active[i].off_at=now+(gate_samples?gate_samples:1U);
            ++p->active_count;
            push_event(part,note,velocity,1U);
            p->last_note=(uint8_t)note;
            p->last_velocity=(uint8_t)velocity;
            return;
        }
    }
}

static unsigned held_sorted(const perf_part_t *p, uint8_t *notes, uint8_t *vel)
{
    unsigned n=p->held_count;
    for (unsigned i=0;i<n;++i) { notes[i]=p->held[i].note; vel[i]=p->held[i].velocity; }
    for (unsigned i=1;i<n;++i) {
        unsigned j=i;
        while (j && notes[j-1U]>notes[j]) {
            uint8_t t=notes[j]; notes[j]=notes[j-1U]; notes[j-1U]=t;
            t=vel[j]; vel[j]=vel[j-1U]; vel[j-1U]=t;
            --j;
        }
    }
    return n;
}

/* The ARP's own order: by pitch, or as the keys were played (0.6.0). */
static unsigned held_for_arp(const perf_part_t *p, uint8_t *notes, uint8_t *vel)
{
    if (!p->cfg.arp_order) return held_sorted(p,notes,vel);
    const unsigned n=p->held_count;
    for (unsigned i=0;i<n;++i) { notes[i]=p->held[i].note; vel[i]=p->held[i].velocity; }
    return n;
}

/* Find the chord tone closest to the player's latest pitch across the
 * available register. Equal-distance ties follow the player's contour. */
static unsigned contour_anchor(const perf_part_t *p, const uint8_t *notes,
                               unsigned n, unsigned octaves)
{
    const unsigned total=n*octaves;
    unsigned best=0U;
    unsigned best_distance=256U;
    for(unsigned i=0U;i<total;++i) {
        const int pitch=(int)notes[i%n]+(int)(12U*(i/n));
        const unsigned distance=(unsigned)(pitch>(int)p->input_note ?
            pitch-(int)p->input_note : (int)p->input_note-pitch);
        if(distance<best_distance ||
           (distance==best_distance && p->input_direction>0 && i>best) ||
           (distance==best_distance && p->input_direction<0 && i<best)) {
            best=i; best_distance=distance;
        }
    }
    return best;
}

static unsigned root_note(const perf_part_t *p, const uint8_t *notes)
{
    return p->cfg.root ? (unsigned)(p->cfg.root-1U) : (unsigned)(notes[0]%12U);
}

static unsigned quantize_note(const perf_part_t *p, const uint8_t *held,
                              unsigned held_n, unsigned note)
{
    static const uint16_t scales[15]={
        0xAB5U,0x5ADU,0x6ADU,0x5ABU,0xAD5U,0x6B5U,0x56BU,0x9ADU,
        0xAADU,0x295U,0x4A9U,0x4E9U,0x555U,0x18DU,0x9B3U
    };
    if (!p->cfg.quantize || !held_n || note>127U) return note;
    uint32_t held_mask=0U;
    for (unsigned i=0;i<held_n;++i) held_mask |= 1U<<(held[i]%12U);
    const unsigned root=root_note(p,held);
    held_mask=((held_mask>>root)|(held_mask<<(12U-root)))&0xFFFU;
    uint32_t mask=held_mask;
    if (p->cfg.quantize==2U) {
        const uint32_t minor=(held_mask&(1U<<3)) && !(held_mask&(1U<<4));
        mask |= minor ? 0x5ADU : 0xAB5U;
    } else if (p->cfg.quantize>=3U) {
        mask=scales[(p->cfg.quantize-3U)%15U];
    }
    if (!mask) return note;
    const unsigned pc=(note+12U-root)%12U;
    for (unsigned d=0;d<7U;++d) {
        if (mask & (1U<<((pc+12U-d)%12U))) return note>=d?note-d:note+12U-d;
        if (mask & (1U<<((pc+d)%12U))) return note+d<=127U?note+d:note+d-12U;
    }
    return note;
}

static unsigned groove_velocity(const perf_part_t *p, unsigned step, unsigned base)
{
    const unsigned v=velocity_grooves[p->cfg.velocity_groove&7U][step&7U];
    if (!v) return 0U;
    const int delta=((int)v-5)*((int)p->cfg.accent+64)/127;
    int out=(int)base+delta*3;
    if (out<1) out=1;
    if (out>127) out=127;
    return (unsigned)out;
}

static unsigned groove_gate(const perf_part_t *p, unsigned step, unsigned base)
{
    const unsigned g=gate_grooves[p->cfg.gate_groove&7U][step&7U];
    if (!g) return base;
    unsigned out=base*g/5U;
    if (out<5U) out=5U;
    if (out>100U) out=100U;
    return out;
}

/* Shared musical envelope: rise, peak, then breathe into the cadence. The
 * gate follows it as a share; the velocity as steps on the engine's even dB
 * line (0.25 dB a step), so the swell is the same -6.9 .. +3.2 dB at pp as
 * at ff. (Until 0.6.4 the velocity was scaled by the share too, which on the
 * even line made the arc 3 dB deep at pp and 10 dB at ff.) */
static const uint8_t phrase_arc[16] = {
    82U, 86U, 90U, 95U, 101U, 107U, 113U, 118U,
    120U, 116U, 108U, 99U, 90U, 82U, 74U, 68U
};
static const int8_t PHRASE_ARC_STEPS[16] = {
    -14, -10, -7, -4, 1, 5, 8, 11, 12, 10, 5, -1, -7, -14, -21, -26
};

static unsigned arc_velocity(unsigned velocity, unsigned position)
{
    const int value=(int)velocity+PHRASE_ARC_STEPS[position&15U];
    return value<1?1U:(value>127?127U:(unsigned)value);
}

static unsigned arc_gate(unsigned gate, unsigned position)
{
    unsigned value=(gate*(unsigned)phrase_arc[position&15U]+50U)/100U;
    if(value<5U)value=5U;
    if(value>100U)value=100U;
    return value;
}

#ifdef FM1_HOST_TEST
unsigned orch_test_phrase_arc_velocity(unsigned velocity, unsigned position)
{ return arc_velocity(velocity,position); }
#endif

static unsigned arp_pick(perf_part_t *p, const uint8_t *notes, const uint8_t *vel,
                         unsigned n, unsigned *out, unsigned *out_vel)
{
    const unsigned type=p->cfg.arp_type%ORCH_PERF_ARP_TYPES;
    unsigned octaves=(type%3U==0U)?1U:(type%3U==1U)?2U:4U;
    /* Adaptive span (optional since 0.6.0): the register follows the
     * player's recent activity. Off, the type's span is the span, so the
     * same type always sounds the same. */
    if (p->cfg.arp_adaptive) {
        if (p->input_activity < 72U && octaves > 1U) --octaves;
        else if (p->input_activity > 150U && octaves < 4U) ++octaves;
    }
    const unsigned total=n*octaves;
    unsigned idx=0U;
    if (type<15U) {
        const unsigned family=type/3U, pos=p->step;
        if (family==0U) idx=pos%total;
        else if (family==1U) idx=total-1U-(pos%total);
        else if (family==2U) {
            const unsigned cycle=total>1U?2U*total-2U:1U, q=pos%cycle;
            idx=q<total?q:cycle-q;
        } else if (family==3U) {
            const unsigned q=pos%(2U*total);
            idx=q<total?q:2U*total-1U-q;
        } else idx=rnd(p)%total;
        *out=(unsigned)notes[idx%n]+12U*(idx/n);
        *out_vel=vel[idx%n];
        return 1U;
    }

    /* New compact step-pattern families: the rhythm bitmask controls rests;
     * the selector mode changes register, chord order and accents. */
    static const uint16_t masks[16]={
        0x5555U,0x2492U,0x0F0FU,0x8421U,0x3333U,0xAAAAU,0x9696U,0x6996U,
        0x1111U,0xE4E4U,0x5A5AU,0x8889U,0xC3C3U,0x3C3CU,0xF00FU,0x0FF0U
    };
    const unsigned pat=(type-15U)%16U, step=p->step&15U;
    if (!(masks[pat]&(1U<<step))) return 0U;
    const unsigned mode=pat%8U;
    idx=(mode==0U)?step%n:
        (mode==1U)?(n-1U-step%n):
        (mode==2U)?((step&1U)?n-1U-step/2U:step/2U)%n:
        (mode==3U)?(p->input_have_note && p->input_direction ?
                    (p->input_direction > 0 ? step%n : n-1U-step%n) : rnd(p)%n):
        (mode==4U)?(p->last_note?((step+n)%n):0U):
        (mode==5U)?(step*2U)%n:
        (mode==6U)?(step/2U)%n:
        (p->input_have_note ?
            (contour_anchor(p,notes,n,1U)%n +
             (p->input_direction<0 ? n-((step?step-1U:0U)%n) :
              ((step?step-1U:0U)%n)))%n :
            (n-1U-(step/2U)%n));
    *out=(unsigned)notes[idx]+12U*((pat%4U==1U)?1U:0U);
    *out_vel=vel[idx];
    return 1U;
}

static unsigned phrase_pick(perf_part_t *p, const uint8_t *notes,
                            const uint8_t *vel, unsigned n,
                            unsigned out[4], unsigned out_vel[4])
{
    const unsigned ge=p->cfg.phrase_ge%ORCH_PERF_GE_COUNT;
    const unsigned pos=p->phrase_step&15U;
    if (!(phrase_rhythm[ge]&(1U<<pos))) return 0U;
    {
        unsigned density=p->cfg.density;
        /* Follow the player's activity and dynamics: energetic, flowing
         * input opens the phrase; sparse/soft input leaves orchestral space. */
        density=(density*(p->input_activity ? p->input_activity : 100U))/100U;
        if (density>127U) density=127U;
        if ((p->phrase_step&15U)>=12U && density>24U) density-=24U;
        /* Long player phrases taper before the next breath / entrance. */
        if (p->input_phrase_age>=10U && density>20U) density-=20U;
        /* A long player rest starts a fresh phrase softly: leave a little
         * breathing room before the line fills out again. */
        if (p->input_after_rest && (pos<2U) && density>18U) density-=18U;
        if ((rnd(p)&127U)>density) return 0U;
    }
    const unsigned mode=ge%7U;
    unsigned range=p->cfg.range<1U?1U:p->cfg.range>4U?4U:p->cfg.range;
    if (p->input_activity<72U && range>1U) --range;
    else if (p->input_activity>150U && range<4U) ++range;
    const unsigned total=n*range;
    unsigned idx=0U;
    switch (mode) {
    case 0U: idx=p->phrase_step%total; break;
    case 1U: idx=total-1U-(p->phrase_step%total); break;
    case 2U: { const unsigned cyc=total>1U?2U*total-2U:1U,q=p->phrase_step%cyc;idx=q<total?q:cyc-q;break; }
    case 3U: idx=rnd(p)%total; break;
    case 4U: {
        int w=(int)(p->phrase_step%total)+((rnd(p)&1U)?1:-1);
        if(w<0) w=0;
        if(w>=(int)total) w=(int)total-1;
        idx=(unsigned)w;
        break;
    }
    case 5U: idx=(p->phrase_step&1U)?total-1U-(p->phrase_step/2U)%total:(p->phrase_step/2U)%total;break;
    default:
        if(p->input_have_note) {
            const unsigned anchor=contour_anchor(p,notes,n,range);
            const unsigned offset=(p->phrase_step/2U)%total;
            idx=(anchor+(p->input_direction<0 ? total-offset : offset))%total;
        } else idx=(pos*3U+ge)%total;
        break;
    }
    unsigned cluster=p->cfg.cluster<1U?1U:p->cfg.cluster>4U?4U:p->cfg.cluster;
    /* Thin chord clusters in quiet passages; allow fuller divisi at peaks. */
    if (p->input_velocity_avg<52U && cluster>1U) --cluster;
    if (p->input_peak && cluster<4U) ++cluster;
    const unsigned count=cluster<n?cluster:n;
    for (unsigned i=0;i<count;++i) {
        unsigned k=(idx+i)%total;
        unsigned note=(unsigned)notes[k%n]+12U*(k/n);
        note=quantize_note(p,notes,n,note);
        int v=(int)vel[k%n];
        if (pos%4U==0U) v+=((int)p->cfg.accent)/4;
        else if (pos%4U==2U) v-=((int)p->cfg.accent)/8;
        if (p->input_peak && i==0U) v+=((int)p->cfg.accent)/5;
        if (p->input_after_rest && pos==0U && i==0U) v+=((int)p->cfg.accent)/8;
        if (v<1) v=1;
        if (v>127) v=127;
        out[i]=note; out_vel[i]=(unsigned)v;
    }
    return count;
}

static void append_unique(unsigned *notes, unsigned *vel, unsigned *count,
                          unsigned note, unsigned velocity)
{
    if (note>127U || !velocity) return;
    for (unsigned i=0;i<*count;++i) if (notes[i]==note) return;
    if (*count>=PERF_MAX_STEP_NOTES) return;
    notes[*count]=note; vel[*count]=velocity; ++*count;
}

static void perform_step(unsigned part, perf_part_t *p, uint32_t now,
                         uint32_t step_len)
{
    uint8_t held[PERF_HELD], held_vel[PERF_HELD];
    const unsigned n=held_sorted(p,held,held_vel);
    if (!n) { ++p->phrase_step; release_all(part,p); p->running=0U; return; }

    unsigned arp_note=0U, arp_vel=0U, arp_hit=0U;
    unsigned seq_notes[4], seq_vel[4], seq_n=0U;
    if (p->cfg.arp_enabled) {
        uint8_t order[PERF_HELD], order_vel[PERF_HELD];
        const unsigned an=held_for_arp(p,order,order_vel);
        arp_hit=arp_pick(p,order,order_vel,an,&arp_note,&arp_vel);
    }
    if (p->cfg.phrase_enabled)
        seq_n=phrase_pick(p,held,held_vel,n,seq_notes,seq_vel);
    const unsigned seq_hit=seq_n?1U:0U;
    unsigned use_arp=arp_hit, use_seq=seq_hit;
    if (p->cfg.arp_enabled && p->cfg.phrase_enabled) {
        switch (p->cfg.logic%ORCH_PERF_LOGIC_COUNT) {
        case ORCH_PERF_AND: use_arp=arp_hit&&seq_hit; use_seq=use_arp; break;
        case ORCH_PERF_XOR: use_arp=arp_hit&&!seq_hit; use_seq=seq_hit&&!arp_hit; break;
        case ORCH_PERF_NOR:
            use_arp=use_seq=0U;
            if(!arp_hit&&!seq_hit) {
                use_arp=1U;
                arp_note=p->last_note?p->last_note:held[0];
                arp_vel=held_vel[0];
            }
            break;
        case ORCH_PERF_ALT:
            use_arp=arp_hit&&!p->alt_state; use_seq=seq_hit&&p->alt_state;
            if (arp_hit||seq_hit) p->alt_state=(uint8_t)!p->alt_state;
            break;
        case ORCH_PERF_SR:
            if(arp_hit&&seq_hit)p->sr_state=(uint8_t)!p->sr_state;
            else if(arp_hit)p->sr_state=1U;
            else if(seq_hit)p->sr_state=0U;
            use_arp=arp_hit&&p->sr_state; use_seq=seq_hit&&!p->sr_state;
            break;
        default: break; /* OR */
        }
        if (p->cfg.inverse) {
            const unsigned old_a=use_arp,old_s=use_seq;
            use_arp=arp_hit&&!old_a; use_seq=seq_hit&&!old_s;
        }
    }

    /* Ties extend the current notes; rests still advance the pattern. */
    if (!use_arp && !use_seq) {
        if (p->cfg.logic!=ORCH_PERF_NOR || (arp_hit||seq_hit)) release_all(part,p);
        ++p->step; ++p->phrase_step;
        return;
    }
    release_all(part,p);
    unsigned out[PERF_MAX_STEP_NOTES], outv[PERF_MAX_STEP_NOTES], count=0U;
    if (use_arp) {
        const unsigned note=quantize_note(p,held,n,arp_note);
        append_unique(out,outv,&count,note,arc_velocity(groove_velocity(p,p->step,arp_vel),p->phrase_step));
    }
    if (use_seq) {
        for (unsigned i=0;i<seq_n;++i)
            append_unique(out,outv,&count,quantize_note(p,held,n,seq_notes[i]),
                          arc_velocity(groove_velocity(p,p->step,seq_vel[i]),p->phrase_step));
    }
    if (!count) { ++p->step; ++p->phrase_step; return; }

    const unsigned gate_base=p->cfg.gate>100U?100U:p->cfg.gate;
    const unsigned gate_pct=arc_gate(groove_gate(p,p->step,gate_base),p->phrase_step);
    /* Ratchet (0.6.0): the step is hit 1..4 times, each hit its share of the
     * step with the same gate; strum: the notes of a chord step one after
     * the other, 0..40 ms apart, low to high. Both go through the pending
     * table the phrase echoes already use. */
    const unsigned ratchet=p->cfg.ratchet<1U?1U:(p->cfg.ratchet>4U?4U:p->cfg.ratchet);
    const uint32_t hit_len=step_len/ratchet;
    const uint32_t gate_samples=(hit_len*gate_pct)/100U;
    const uint32_t strum=(uint32_t)(p->cfg.strum>40U?40U:p->cfg.strum)*(ORCH_PERF_SAMPLE_RATE/1000U);
    for (unsigned hit=0;hit<ratchet;++hit) {
        for (unsigned i=0;i<count;++i) {
            const uint32_t at=now+hit*hit_len+i*strum;
            const unsigned vel=hit?(outv[i]*(8U-hit)/8U):outv[i];
            if (at==now) { generated_on(part,p,out[i],vel,now,gate_samples); continue; }
            for (unsigned a=0;a<PERF_ACTIVE*3U;++a) {
                pending_note_t *pn=&p->pending[a];
                if (pn->used) continue;
                pn->used=1U;
                pn->note=(uint8_t)out[i];
                pn->velocity=(uint8_t)(vel?vel:1U);
                pn->on_at=at;
                pn->gate_samples=gate_samples;
                ++p->pending_count;
                break;
            }
        }
    }

    /* Repeat is a light echo inside the step. The active-note table handles
     * repeated pitches safely by ending the previous instance first. */
    if (p->cfg.phrase_enabled && p->cfg.repeat) {
        const unsigned reps=p->cfg.repeat>3U?3U:p->cfg.repeat;
        for (unsigned rep=1U;rep<=reps;++rep) {
            const uint32_t at=now+(step_len*rep)/(reps+1U);
            for (unsigned i=0;i<count;++i) {
                for (unsigned a=0;a<PERF_ACTIVE*3U;++a) {
                    pending_note_t *pn=&p->pending[a];
                    if (pn->used) continue;
                    pn->used=1U;
                    pn->note=(uint8_t)out[i];
                    /* each echo 5, 12, 24 dB down (as 3/4, 1/2, 1/4 of
                     * the velocity sounded at mf), the same at any dynamic */
                    {
                        static const uint8_t ECHO_STEPS[4] = { 0U, 20U, 47U, 95U };
                        const int v=(int)outv[i]-(int)ECHO_STEPS[rep];
                        pn->velocity=(uint8_t)(v<1?1:v);
                    }
                    pn->on_at=at;
                    pn->gate_samples=gate_samples*3U/4U;
                    ++p->pending_count;
                    break;
                }
            }
        }
    }
    ++p->step;
    ++p->phrase_step;
}

static void clock_step(unsigned part, perf_part_t *p, uint32_t now)
{
    const uint32_t div=division_ticks[p->cfg.division%ORCH_PERF_DIVISIONS];
    uint32_t step_len=p->cfg.external_clock && p->ext_tick_len
        ? p->ext_tick_len*div
        : (ORCH_PERF_SAMPLE_RATE*60U*div)/(p->cfg.bpm*24U);
    if (!step_len) step_len=1U;
    p->step_len=step_len;
    if ((p->step&1U) && p->cfg.swing) {
        const uint32_t swing=(uint32_t)p->cfg.swing*33U/127U;
        p->pending_at=now+(step_len*swing)/100U;
        p->pending_step=1U;
    } else {
        perform_step(part,p,now,step_len);
    }
}

void orch_perf_init(void)
{
    q_read=q_write=0U;
    for (unsigned i=0;i<ORCH_PERF_PARTS;++i) {
        perf_part_t *p=&parts[i];
        orch_perf_default_config(&p->cfg);
        p->clock_inc=((uint32_t)p->cfg.bpm*24U*PERF_Q16)/
                     (60U*ORCH_PERF_SAMPLE_RATE);
        p->held_count=p->keys_down=p->running=p->first_pending=0U;
        p->active_count=p->pending_count=0U; p->step=p->phrase_step=0U;
        p->clock_phase=p->clock_count=0U; p->last_sample=0U;
        p->step_len=p->pending_at=0U; p->pending_step=0U;
        p->ext_last_tick=p->ext_tick_len=0U; p->ext_age=0U;
        p->alt_state=p->sr_state=p->last_note=p->last_velocity=0U;
        p->input_note=p->input_velocity_avg=0U; p->input_activity=100U;
        p->input_phrase_age=p->input_peak=p->input_after_rest=0U;
        p->input_direction=0; p->input_have_note=0U;
        p->input_last_on=p->input_ioi_avg=0U;
        p->rng=0x6D2B79F5U^(i*0x9E3779B9U);
        for(unsigned n=0;n<PERF_HELD;++n){p->held[n].note=0U;p->held[n].velocity=0U;}
        for(unsigned n=0;n<PERF_ACTIVE;++n){p->active[n].used=0U;p->active[n].note=0U;p->active[n].off_at=0U;}
        for(unsigned n=0;n<PERF_ACTIVE*3U;++n){p->pending[n].used=0U;p->pending[n].note=0U;p->pending[n].velocity=0U;p->pending[n].on_at=0U;p->pending[n].gate_samples=0U;}
    }
}

int orch_perf_set_config(unsigned part, const struct orch_perf_config *c)
{
    if (part>=ORCH_PERF_PARTS || !c) return -1;
    perf_part_t *p=&parts[part];
    const uint8_t was=p->cfg.arp_enabled||p->cfg.phrase_enabled;
    p->cfg=*c;
    if(p->cfg.bpm<40U) p->cfg.bpm=40U;
    if(p->cfg.bpm>250U) p->cfg.bpm=250U;
    p->clock_inc=((uint32_t)p->cfg.bpm*24U*PERF_Q16)/
                 (60U*ORCH_PERF_SAMPLE_RATE);
    if(p->cfg.arp_type>=ORCH_PERF_ARP_TYPES)p->cfg.arp_type%=ORCH_PERF_ARP_TYPES;
    p->cfg.phrase_ge%=ORCH_PERF_GE_COUNT;
    p->cfg.division%=ORCH_PERF_DIVISIONS;
    if(p->cfg.gate>100U)p->cfg.gate=100U;
    if(p->cfg.logic>=ORCH_PERF_LOGIC_COUNT)p->cfg.logic=ORCH_PERF_OR;
    if(p->cfg.root>12U)p->cfg.root=0U;
    if(p->cfg.range<1U) p->cfg.range=1U;
    if(p->cfg.range>4U) p->cfg.range=4U;
    if(p->cfg.cluster<1U) p->cfg.cluster=1U;
    if(p->cfg.cluster>4U) p->cfg.cluster=4U;
    p->cfg.velocity_groove&=7U; p->cfg.gate_groove&=7U;
    if(p->cfg.ratchet<1U)p->cfg.ratchet=1U;
    if(p->cfg.ratchet>4U)p->cfg.ratchet=4U;
    if(p->cfg.strum>40U)p->cfg.strum=40U;
    p->cfg.arp_order=p->cfg.arp_order?1U:0U;
    p->cfg.arp_adaptive=p->cfg.arp_adaptive?1U:0U;
    if(was && !(p->cfg.arp_enabled||p->cfg.phrase_enabled)) {
        release_all(part,p); p->held_count=p->keys_down=p->running=0U;
    }
    if(p->cfg.latch==0U && p->keys_down==0U && was &&
       (p->cfg.arp_enabled||p->cfg.phrase_enabled) && p->held_count) {
        p->held_count=0U; release_all(part,p); p->running=0U;
    }
    if(!was && (p->cfg.arp_enabled||p->cfg.phrase_enabled) && p->held_count) p->first_pending=1U;
    return 0;
}

/* the orchestra's tempo reaches every ARP (orch_engine.c: MIDI clock, else
 * the playing); the clock-locked ones keep counting the clock's own ticks */
void orch_perf_set_tempo(unsigned bpm)
{
    if(bpm<40U) bpm=40U;
    if(bpm>250U) bpm=250U;
    for(unsigned i=0;i<ORCH_PERF_PARTS;++i) {
        perf_part_t *p=&parts[i];
        p->cfg.bpm=(uint16_t)bpm;
        p->clock_inc=((uint32_t)bpm*24U*PERF_Q16)/(60U*ORCH_PERF_SAMPLE_RATE);
    }
}

int orch_perf_get_config(unsigned part, struct orch_perf_config *c)
{
    if(part>=ORCH_PERF_PARTS||!c)return -1;
    *c=parts[part].cfg; return 0;
}

int orch_perf_note_event(unsigned part, unsigned note, unsigned velocity)
{
    return orch_perf_note_event_played(part,note,velocity,velocity);
}

/* `played` is how hard the part is being played (the velocity under the
 * breath and expression controllers, orch_cc_dynamic): the ARP's ear learns
 * the contour from it, the held notes keep their velocity. */
int orch_perf_note_event_played(unsigned part, unsigned note, unsigned velocity,
                                unsigned played)
{
    if(part>=ORCH_PERF_PARTS||note>127U||velocity>127U)return 0;
    if(played>127U)played=127U;
    if(velocity&&!played)played=1U;
    perf_part_t *p=&parts[part];
    if(!p->cfg.arp_enabled&&!p->cfg.phrase_enabled)return 0;
    if(velocity) {
        /* Learn the player's contour before the generated notes are chosen.
         * This state is intentionally separate from generated_on(), avoiding
         * feedback loops where the ARP learns from its own output. */
        {
            const unsigned previous_note=p->input_note;
            const unsigned previous_velocity=p->input_velocity_avg;
            const unsigned had_note=p->input_have_note;
            p->input_peak=0U;
            if (had_note) {
                const int delta=(int)note-(int)previous_note;
                if (delta>=2) p->input_direction=1;
                else if (delta<=-2) p->input_direction=-1;
                if (p->input_last_on && p->last_sample>p->input_last_on) {
                    const uint32_t ioi=p->last_sample-p->input_last_on;
                    if (ioi<PERF_CLOCK_TIMEOUT*8U)
                        p->input_ioi_avg=p->input_ioi_avg?
                            (p->input_ioi_avg*3U+ioi)/4U:ioi;
                }
                if (p->input_ioi_avg > (ORCH_PERF_SAMPLE_RATE * 3U) / 2U) {
                    p->input_phrase_age=0U;
                    p->input_after_rest=1U;
                } else {
                    if (p->input_phrase_age<15U) ++p->input_phrase_age;
                    p->input_after_rest=0U;
                }
                if (delta>=5) p->input_peak=1U;
            } else p->input_phrase_age=0U;
            if (had_note && played>previous_velocity+20U) p->input_peak=1U;
            p->input_note=(uint8_t)note; p->input_have_note=1U;
            p->input_last_on=p->last_sample;
            p->input_velocity_avg=previous_velocity?
                (uint8_t)((previous_velocity*3U+played)/4U):(uint8_t)played;
        }
        p->input_activity=100U;
        if (p->input_ioi_avg) {
            const uint32_t nominal=(ORCH_PERF_SAMPLE_RATE*3U)/4U;
            if (p->input_ioi_avg<nominal/2U) p->input_activity=165U;
            else if (p->input_ioi_avg>nominal*2U) p->input_activity=65U;
            else p->input_activity=(uint8_t)(165U-
                ((p->input_ioi_avg-nominal/2U)*100U)/(nominal*3U/2U));
        }
        if(!p->keys_down&&p->cfg.latch) p->held_count=0U;
        ++p->keys_down;
        for(unsigned i=0;i<p->held_count;++i) if(p->held[i].note==note)return 1;
        if(p->held_count<PERF_HELD) {
            p->held[p->held_count].note=(uint8_t)note;
            p->held[p->held_count].velocity=(uint8_t)velocity;
            ++p->held_count;
        }
        if(!p->running) {p->running=1U;p->first_pending=(uint8_t)!p->cfg.external_clock;p->step=p->phrase_step=0U;p->clock_count=0U;p->clock_phase=0U;p->ext_age=0U;}
    } else {
        if(p->keys_down)--p->keys_down;
        if(!p->cfg.latch) {
            for(unsigned i=0;i<p->held_count;++i) if(p->held[i].note==note) {
                for(unsigned j=i+1U;j<p->held_count;++j)p->held[j-1U]=p->held[j];
                --p->held_count; break;
            }
            if(!p->held_count) {release_all(part,p);p->running=p->first_pending=0U;}
        }
    }
    return 1;
}

void orch_perf_midi_realtime(unsigned status, uint32_t sample_now)
{
    for(unsigned i=0;i<ORCH_PERF_PARTS;++i) {
        perf_part_t *p=&parts[i];
        if(!p->cfg.external_clock || (!p->cfg.arp_enabled&&!p->cfg.phrase_enabled))continue;
        if(status==0xFAU) {
            release_all(i,p);
            p->clock_count=division_ticks[p->cfg.division%ORCH_PERF_DIVISIONS]-1U;
            p->step=p->phrase_step=0U;
            p->first_pending=0U;
            p->running=1U;
            p->ext_last_tick=sample_now;
            p->ext_age=0U;
        } else if(status==0xFBU) {
            p->running=1U;
            p->ext_age=0U;
        } else if(status==0xFCU) {
            release_all(i,p);p->running=p->first_pending=p->pending_step=0U;
            p->clock_count=0U;p->ext_last_tick=0U;p->ext_age=0U;
        } else if(status==0xF8U) {
            if(p->ext_last_tick) {
                const uint32_t d=sample_now-p->ext_last_tick;
                if(d) p->ext_tick_len=p->ext_tick_len?(p->ext_tick_len*3U+d)/4U:d;
            }
            p->ext_last_tick=sample_now;p->ext_age=0U;
            ++p->clock_count;
            if(p->running && p->clock_count%division_ticks[p->cfg.division%ORCH_PERF_DIVISIONS]==0U)
                clock_step(i,p,sample_now);
        }
    }
}

void orch_perf_advance_sample(uint32_t sample_now)
{
    for(unsigned i=0;i<ORCH_PERF_PARTS;++i) {
        perf_part_t *p=&parts[i];
        p->last_sample=sample_now;
        if(!p->running && !p->active_count && !p->pending_count) continue;
        active_off_due(i,p,sample_now);
        if(p->pending_count) for(unsigned k=0;k<PERF_ACTIVE*3U;++k) {
            pending_note_t *pn=&p->pending[k];
            if(pn->used && (int32_t)(sample_now-pn->on_at)>=0) {
                generated_on(i,p,pn->note,pn->velocity,sample_now,pn->gate_samples);
                pn->used=0U;
                if(p->pending_count)--p->pending_count;
            }
        }
        if(p->pending_step && (int32_t)(sample_now-p->pending_at)>=0) {
            p->pending_step=0U;
            perform_step(i,p,sample_now,p->step_len?p->step_len:1U);
        }
        if(!p->running || (!p->cfg.arp_enabled&&!p->cfg.phrase_enabled))continue;
        if(p->cfg.external_clock) {
            if(p->ext_age<PERF_CLOCK_TIMEOUT) ++p->ext_age;
            else if(p->running) {
                release_all(i,p);
                p->running=0U;
            }
            continue;
        }
        if(p->first_pending) {
            p->first_pending=0U;
            clock_step(i,p,sample_now);
        }
        p->clock_phase+=p->clock_inc;
        if(p->clock_phase>=PERF_Q16) {
            p->clock_phase-=PERF_Q16;
            ++p->clock_count;
            if(p->clock_count%division_ticks[p->cfg.division%ORCH_PERF_DIVISIONS]==0U)
                clock_step(i,p,sample_now);
        }
    }
}

int orch_perf_pop_event(struct orch_perf_event *e)
{
    if(!e||q_read==q_write)return 0;
    *e=queue[q_read];
    q_read=(uint8_t)((q_read+1U)&PERF_QUEUE_MASK);
    return 1;
}

void orch_perf_reset_notes(void)
{
    for(unsigned i=0;i<ORCH_PERF_PARTS;++i) {
        release_all(i,&parts[i]);
        parts[i].held_count=parts[i].keys_down=parts[i].running=0U;
        parts[i].first_pending=parts[i].pending_step=0U;
        parts[i].clock_phase=parts[i].clock_count=0U;
    }
    q_read=q_write=0U;
}

unsigned orch_perf_current_step(unsigned part)
{
    return part<ORCH_PERF_PARTS?parts[part].step:0U;
}

unsigned orch_perf_held(unsigned part)
{
    return part<ORCH_PERF_PARTS?parts[part].held_count:0U;
}

void orch_perf_set_external_tempo(unsigned bpm)
{
    if (bpm < 40U) bpm = 40U;
    if (bpm > 250U) bpm = 250U;
    const uint32_t tick = (ORCH_PERF_SAMPLE_RATE * 60U) / (bpm * 24U);
    for (unsigned i=0;i<ORCH_PERF_PARTS;++i)
        if (parts[i].cfg.external_clock) parts[i].ext_tick_len = tick ? tick : 1U;
}
