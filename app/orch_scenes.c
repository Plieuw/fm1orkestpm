/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Factory orchestra scenes: eight complete starting points for all eight
 * parts — instrument, articulation, divisi, ADSR, LFO, ARP/phrase settings,
 * the master effects, a 16-step motif and a call/response pairing.
 *
 * The data and the recall come from the panel of the 0.3.x feature line
 * (feature/orchestra-factory-presets, app/fm1_app.c); they live here now so
 * the one panel (app/orch_ui.c) and the host tests can recall them. Control
 * task only: a recall writes the part settings the renderer reads.
 */
#include "orch_scenes.h"

#include "orch_engine.h"
#include "orch_fx.h"
#include "orch_part.h"
#include "orch_performance.h"
#include "orch_resonance.h"

/* Articulations, short names for the table */
#define AU ORCH_ART_AUTO
#define LG ORCH_ART_LEGATO
#define DT ORCH_ART_DETACHE
#define ST ORCH_ART_STACCATO
#define PZ ORCH_ART_PIZZICATO
#define TS ORCH_ART_TASTO

/* Four orchestral sections plus their adaptive performance and master-effect
 * starting points. The player's live MIDI input remains the source for
 * adaptive phrasing; a scene only chooses its musical context: which parts
 * the adaptive layer may articulate (AUTO) and which are pinned, whether the
 * sequencer follows the live harmony, how the ARP listens, how much the
 * hall's shared strings ring, and the four effects with all their settings.
 * Retuned for 0.6.7: the velocities sit on the even dB line, the delay locks
 * to the tempo (MIDI clock or the playing), chorus only where a sound wants
 * it, shimmer only in the open scenes. */
typedef struct {
    const char *name;
    uint8_t inst[ORCH_PARTS], art[ORCH_PARTS], cap[ORCH_PARTS];
    uint8_t arp, phrase, arp_type, phrase_ge, bpm, division;
    uint8_t density, range, cluster, accent, quantize;
    uint8_t resonance;        /* the shared strings, 0..100 */
    uint8_t seq_adaptive;     /* the motif follows the live harmony */
    uint8_t arp_adaptive, arp_order, ratchet, strum;
    uint8_t fx_on[ORCH_FX_SLOT_COUNT], fx_mix[ORCH_FX_SLOT_COUNT];
    /* RATE DEPTH SPREAD TONE / RATE DEPTH WIDTH TAPS /
     * TIME FEEDBACK SYNC DAMP / DECAY SHIM EARLY DAMP */
    uint8_t fx_p[ORCH_FX_SLOT_COUNT][ORCH_FX_PARAMS];
} scene_t;

static const scene_t scenes[ORCH_SCENE_COUNT] = {
    /* strings carry, winds colour; the adaptive layer articulates them all */
    { "CINEMA STR", {0,1,2,3,5,26,11,17},   /* the celesta for sparkle */
      {AU,AU,AU,AU,AU,AU,AU,AU}, {4,3,3,2,2,2,2,1},
      0,0,0,0,88,4, 92,3,2,58,1, 35, 0, 1,0,1,0,
      {0,1,0,1},{0,22,0,32},
      {{25,50,40,100},{20,40,70,100},{50,35,0,100},{70,10,40,55}} },
    /* detache strings and the harpsichord in a dry room; the ARP measured, by pitch */
    { "BAROQUE", {0,1,27,5,2,6,8,14},       /* the harpsichord as continuo */
      {DT,DT,AU,AU,AU,DT,AU,AU}, {3,2,2,1,2,2,2,1},
      1,0,2,0,112,4, 80,2,2,42,2, 20, 1, 0,0,1,0,
      {0,0,0,1},{0,0,0,20},
      {{25,50,40,100},{30,50,50,100},{50,35,0,100},{45,0,50,60}} },
    /* breathing entries: chords spread a little, an echo on the eighth */
    { "WIND CHOIR", {0,5,7,8,18,9,17,2},    /* the english horn */
      {AU,AU,AU,AU,AU,AU,AU,AU}, {2,3,2,2,2,2,1,2},
      0,1,0,8,76,5, 88,3,2,50,1, 25, 1, 1,0,1,15,
      {0,0,1,1},{0,0,14,34},
      {{25,50,40,100},{30,50,50,100},{50,30,100,60},{62,18,30,50}} },
    /* compact calls, close and dry; the low brass short */
    { "BRASS FAN", {0,10,11,12,13,22,2,17}, /* the bass trombone */
      {AU,ST,AU,ST,ST,AU,AU,AU}, {1,3,3,2,2,2,2,1},
      0,0,0,0,104,4, 76,2,3,72,2, 15, 0, 0,0,1,0,
      {0,0,0,1},{0,0,0,24},
      {{25,50,40,100},{30,50,50,100},{50,35,0,100},{55,0,55,45}} },
    /* pizzicato that rings in the hall's strings; a triplet echo */
    { "PIZZ MOTION", {0,1,4,14,2,5,6,16}, {PZ,PZ,PZ,ST,PZ,AU,AU,ST}, {3,2,2,1,2,2,2,1},
      1,1,5,12,126,5, 105,2,3,64,1, 45, 1, 1,1,1,0,
      {0,1,1,1},{0,14,18,24},
      {{25,50,40,100},{30,40,60,100},{50,35,50,50},{50,0,45,50}} },
    /* sul tasto strings, long tails, the shimmer open */
    { "AMBIENT", {2,3,5,17,6,7,11,24},      /* the vibraphone */
      {TS,TS,AU,AU,AU,AU,AU,AU}, {2,2,2,2,2,2,2,1},
      0,1,0,19,68,6, 72,4,2,36,3, 60, 1, 1,0,1,30,
      {1,1,1,1},{18,36,30,48},
      {{10,50,80,60},{15,60,100,100},{50,55,100,70},{90,60,20,65}} },
    /* an interlocking pulse; the motif and the ARP follow the live harmony */
    { "OSTINATO", {0,2,5,10,1,6,11,14}, {DT,DT,AU,AU,DT,AU,AU,ST}, {3,3,2,2,2,2,2,1},
      1,1,8,26,120,4, 100,2,2,55,1, 25, 1, 1,0,1,0,
      {0,0,1,1},{0,0,20,22},
      {{25,50,40,100},{30,50,50,100},{50,35,100,55},{50,0,40,50}} },
    /* everyone, adaptive, a broad hall */
    { "FULL ORCH", {0,2,5,10,1,6,11,17}, {AU,AU,AU,AU,AU,AU,AU,AU}, {4,4,3,3,3,3,2,2},
      0,0,0,0,100,4, 90,3,3,60,0, 30, 0, 1,0,1,0,
      {0,1,0,1},{0,16,0,30},
      {{25,50,40,100},{20,40,70,100},{50,35,0,100},{66,8,40,55}} }
};

/* Independent 16-step orchestral motifs. 60 is a neutral pitch anchor;
 * adaptive mode revoices against the player's live chord/contour. Zero
 * velocity is a rest. Each row is one complete scene's step/velocity/gate
 * contour, not a replacement for the live ARP or phrase generator. */
typedef struct { uint8_t note, velocity, gate; } scene_step_t;
static const scene_step_t scene_steps[ORCH_SCENE_COUNT][ORCH_SEQ_STEPS] = {
 /* Cinema: spacious pulse, phrase-shaped rests */
 {{60,94,92},{0,0,0},{64,85,92},{0,0,0},{67,97,96},{0,0,0},{64,84,92},{0,0,0},
  {62,92,92},{0,0,0},{65,85,92},{0,0,0},{69,100,96},{0,0,0},{65,83,88},{0,0,0}},
 /* Baroque: measured eighth-note motion */
 {{60,106,68},{64,89,68},{67,97,68},{64,89,68},{62,103,68},{65,87,68},{69,95,68},{65,87,68},
  {64,105,68},{67,89,68},{71,97,68},{67,89,68},{65,102,68},{69,87,68},{72,97,68},{69,87,68}},
 /* Wind choir: breath-like spaced entries */
 {{60,94,96},{0,0,0},{0,0,0},{64,85,96},{0,0,0},{67,95,100},{0,0,0},{0,0,0},
  {62,92,96},{0,0,0},{0,0,0},{65,85,96},{0,0,0},{69,97,100},{0,0,0},{0,0,0}},
 /* Brass fanfare: dotted ceremonial calls */
 {{60,119,62},{0,0,0},{67,108,62},{0,0,0},{72,120,72},{0,0,0},{67,103,62},{0,0,0},
  {65,116,62},{0,0,0},{69,107,62},{0,0,0},{74,123,76},{0,0,0},{69,106,62},{0,0,0}},
 /* Pizz motion: syncopated short notes */
 {{60,110,42},{0,0,0},{64,91,38},{67,102,42},{0,0,0},{64,94,38},{0,0,0},{69,107,42},
  {0,0,0},{67,92,38},{64,102,42},{0,0,0},{62,94,38},{0,0,0},{65,108,42},{0,0,0}},
 /* Ambient: sparse slow anchors */
 {{60,83,100},{0,0,0},{0,0,0},{0,0,0},{67,80,100},{0,0,0},{0,0,0},{0,0,0},
  {62,82,100},{0,0,0},{0,0,0},{0,0,0},{69,81,100},{0,0,0},{0,0,0},{0,0,0}},
 /* Ostinato: interlocking pulse with rests */
 {{60,107,74},{0,0,0},{67,94,62},{0,0,0},{60,103,74},{64,89,58},{0,0,0},{67,97,62},
  {60,106,74},{0,0,0},{67,92,62},{64,89,58},{60,102,74},{0,0,0},{69,100,62},{0,0,0}},
 /* Full orchestra: broad anchor + cadence */
 {{60,97,84},{0,0,0},{64,85,84},{0,0,0},{67,100,90},{0,0,0},{69,89,84},{0,0,0},
  {67,95,84},{0,0,0},{65,85,84},{0,0,0},{64,97,88},{0,0,0},{60,91,96},{0,0,0}}
};
static const uint8_t scene_seq_enabled[ORCH_SCENE_COUNT] = {0,1,0,0,1,0,1,0};
static const uint8_t scene_seq_part[ORCH_SCENE_COUNT] = {0,0,1,2,0,1,0,0};
static const uint8_t scene_response_enabled[ORCH_SCENE_COUNT] = {1,1,1,1,0,1,1,1};
static const uint8_t scene_response_leader[ORCH_SCENE_COUNT] = {0,0,1,2,0,1,0,0};
static const uint8_t scene_response_follower[ORCH_SCENE_COUNT] = {1,1,0,3,2,3,2,1};
static const int8_t scene_response_interval[ORCH_SCENE_COUNT] = {12,7,-5,12,0,7,5,0};


const char *orch_scene_name(unsigned index)
{
    return index < ORCH_SCENE_COUNT ? scenes[index].name : "?";
}

unsigned orch_scene_bpm(unsigned index)
{
    return index < ORCH_SCENE_COUNT ? scenes[index].bpm : 128U;
}

unsigned orch_scene_seq_part(unsigned index)
{
    return index < ORCH_SCENE_COUNT ? scene_seq_part[index] : 0U;
}

unsigned orch_scene_seq_enabled(unsigned index)
{
    return index < ORCH_SCENE_COUNT ? scene_seq_enabled[index] : 0U;
}

void orch_scene_step(unsigned index, unsigned step, struct orch_step_note *out)
{
    if (!out) return;
    out->note = out->velocity = out->gate_percent = 0U;
    out->chance = 100U;
    if (index >= ORCH_SCENE_COUNT || step >= ORCH_SEQ_STEPS) return;
    const scene_step_t *s = &scene_steps[index][step];
    out->note = s->note;
    out->velocity = s->velocity;
    out->gate_percent = s->gate;
    out->chance = 100U;
}

int orch_scene_apply(unsigned index)
{
    /* Per-scene, per-section ADSR: time values are milliseconds and sustain
     * is percent. These contours shape the VCA only; model damping remains
     * an independent physical-instrument property. */
    static const struct {
        uint16_t a, d, r;
        uint8_t s;
    } scene_adsr[ORCH_SCENE_COUNT][ORCH_PARTS] = {
        {{45,240,900,88},{50,260,950,90},{65,320,1100,90},{75,350,1200,90},{55,280,1000,88},{40,230,850,86},{48,250,900,88},{115,280,1200,90}},
        {{20,120,260,80},{24,140,300,82},{28,150,350,82},{8,80,500,65},{58,240,560,86},{38,170,360,84},{42,200,460,86},{3,90,600,48}},
        {{60,220,520,85},{45,200,480,88},{70,240,600,88},{85,280,700,90},{45,200,480,88},{62,250,580,88},{115,280,1200,90},{58,240,560,86}},
        {{12,100,220,82},{22,150,360,86},{25,170,380,86},{35,180,450,88},{55,230,520,88},{62,250,580,86},{42,200,460,86},{115,280,1200,90}},
        {{3,70,420,45},{3,80,480,45},{4,100,550,50},{2,65,320,42},{58,240,560,86},{38,170,360,84},{45,200,480,86},{4,100,700,50}},
        {{90,400,1500,90},{100,450,1600,90},{110,500,1800,92},{120,550,1900,92},{62,250,580,88},{38,190,440,86},{48,230,520,88},{115,280,1200,90}},
        {{18,110,260,78},{25,140,340,82},{35,160,400,84},{12,100,250,80},{42,210,480,86},{38,180,420,86},{45,210,480,86},{4,100,700,50}},
        {{32,180,420,82},{58,240,560,86},{38,170,360,84},{12,120,260,80},{42,210,480,86},{38,180,420,86},{45,210,480,86},{115,280,1200,90}}
    };
    /* Scene modulation: restrained per-section movement, synchronized
     * where it supports the rhythmic character. sync_division: 0=free,
     * 1=quarter, 2=eighth, 3=sixteenth, 4=dotted eighth. */
    static const struct {
        uint8_t wave, dest, depth, sync;
        uint16_t rate, delay, fade;
    } scene_lfo[ORCH_SCENE_COUNT][ORCH_PARTS] = {
        {{0,1,14,0,4200,180,700},{0,1,12,0,3900,220,800},{0,1,10,0,3400,260,900},{0,1,8,0,3000,300,1000},{0,1,9,0,3300,260,850},{0,1,8,0,3100,300,900},{0,1,8,0,3000,320,950},{0,5,8,0,700,400,1400}},
        {{1,1,10,4,5000,40,250},{1,1,8,4,5000,60,300},{1,1,7,4,4500,80,350},{0,0,0,0,5000,0,0},{1,1,8,4,5000,80,300},{1,1,7,4,4500,100,350},{1,1,7,4,4500,100,350},{0,0,0,0,5000,0,0}},
        {{0,1,8,0,2800,300,900},{0,1,10,0,3200,250,800},{0,1,8,0,2600,350,1000},{0,1,6,0,2400,400,1100},{0,1,8,0,3000,300,900},{0,1,7,0,2800,350,950},{0,1,8,0,2600,350,1000},{0,5,8,0,800,500,1600}},
        {{0,0,0,0,5000,0,0},{0,0,0,0,5000,0,0},{0,0,0,0,5000,0,0},{1,2,10,2,5000,0,250},{0,0,0,0,5000,0,0},{0,0,0,0,5000,0,0},{1,2,8,2,5000,0,250},{0,0,0,0,5000,0,0}},
        {{1,2,12,2,5000,0,120},{1,2,10,2,5000,0,150},{1,2,8,2,5000,0,180},{1,2,8,2,5000,0,100},{1,2,8,2,5000,0,150},{1,2,8,2,5000,0,150},{1,2,8,2,5000,0,150},{1,2,8,2,5000,0,100}},
        {{0,5,18,0,900,500,1800},{0,5,16,0,750,650,2000},{0,5,14,0,650,800,2200},{0,5,12,0,550,900,2400},{0,5,14,0,650,800,1900},{0,5,12,0,600,850,2000},{0,5,12,0,600,850,2000},{0,5,10,0,500,900,2200}},
        {{3,2,12,2,5000,0,120},{3,2,10,2,5000,0,150},{1,1,8,2,5000,80,250},{3,2,8,2,5000,0,100},{3,2,8,2,5000,0,150},{3,2,8,2,5000,0,150},{1,1,8,2,5000,80,250},{3,2,8,2,5000,0,100}},
        {{0,1,10,0,3600,180,700},{0,1,10,0,3300,200,750},{0,1,8,0,3000,250,850},{0,1,8,0,2800,300,900},{0,1,8,0,3200,220,800},{0,1,8,0,3000,260,850},{0,1,8,0,2800,300,900},{0,5,8,0,700,450,1400}}
    };
    if (index >= ORCH_SCENE_COUNT) return -1;
    const scene_t *p = &scenes[index];
    orch_reset_notes();
    for (unsigned part=0U; part<ORCH_PARTS; ++part) {
        struct orch_perf_config pc;
        (void)orch_engine_set_instrument(part,p->inst[part]);
        (void)orch_engine_set_articulation(part,p->art[part]);
        (void)orch_engine_set_player_cap(part,p->cap[part]);
        (void)orch_engine_set_adsr(part,scene_adsr[index][part].a,
            scene_adsr[index][part].d,scene_adsr[index][part].s,
            scene_adsr[index][part].r);
        (void)orch_engine_set_lfo(part,scene_lfo[index][part].wave,
            scene_lfo[index][part].dest,scene_lfo[index][part].depth,
            scene_lfo[index][part].rate,scene_lfo[index][part].sync,
            scene_lfo[index][part].delay,scene_lfo[index][part].fade);
        orch_perf_default_config(&pc);
        pc.arp_enabled=p->arp;
        pc.phrase_enabled=p->phrase;
        pc.arp_type=p->arp_type;
        pc.phrase_ge=p->phrase_ge;
        pc.bpm=p->bpm;
        pc.division=p->division;
        pc.density=p->density;
        pc.range=p->range;
        pc.cluster=p->cluster;
        pc.accent=p->accent;
        pc.quantize=p->quantize;
        pc.arp_adaptive=p->arp_adaptive;
        pc.arp_order=p->arp_order;
        pc.ratchet=p->ratchet;
        pc.strum=p->strum;
        /* Complete ARP/SEQ state, not just the pattern selector. */
        switch (index) {
        case 0U: /* Cinema: broad, legato phrases */
            pc.gate=88U; pc.swing=8U;
            pc.logic=ORCH_PERF_OR; pc.inverse=0U;
            pc.root=0U; pc.repeat=1U;
            pc.velocity_groove=4U; pc.gate_groove=0U;
            pc.latch=0U; pc.external_clock=0U; break;
        case 1U: /* Baroque: measured pulse and light ornament */
            pc.gate=72U; pc.swing=0U;
            pc.logic=ORCH_PERF_ALT; pc.inverse=0U;
            pc.root=0U; pc.repeat=2U;
            pc.velocity_groove=1U; pc.gate_groove=1U;
            pc.latch=1U; pc.external_clock=0U; break;
        case 2U: /* Wind choir: breathing, spacious phrase */
            pc.gate=92U; pc.swing=12U;
            pc.logic=ORCH_PERF_OR; pc.inverse=0U;
            pc.root=0U; pc.repeat=1U;
            pc.velocity_groove=4U; pc.gate_groove=0U;
            pc.latch=0U; pc.external_clock=0U; break;
        case 3U: /* Brass: strong accents, compact notes */
            pc.gate=68U; pc.swing=0U;
            pc.logic=ORCH_PERF_AND; pc.inverse=0U;
            pc.root=0U; pc.repeat=0U;
            pc.velocity_groove=2U; pc.gate_groove=6U;
            pc.latch=0U; pc.external_clock=0U; break;
        case 4U: /* Pizzicato: short, syncopated, repeating */
            pc.gate=54U; pc.swing=24U;
            pc.logic=ORCH_PERF_ALT; pc.inverse=0U;
            pc.root=0U; pc.repeat=2U;
            pc.velocity_groove=6U; pc.gate_groove=7U;
            pc.latch=1U; pc.external_clock=0U; break;
        case 5U: /* Ambient: long gates, slow evolving movement */
            pc.gate=100U; pc.swing=16U;
            pc.logic=ORCH_PERF_OR; pc.inverse=0U;
            pc.root=0U; pc.repeat=3U;
            pc.velocity_groove=4U; pc.gate_groove=0U;
            pc.latch=1U; pc.external_clock=0U; break;
        case 6U: /* Ostinato: interlocking rhythmic motion */
            pc.gate=76U; pc.swing=18U;
            pc.logic=ORCH_PERF_XOR; pc.inverse=0U;
            pc.root=0U; pc.repeat=1U;
            pc.velocity_groove=2U; pc.gate_groove=3U;
            pc.latch=1U; pc.external_clock=0U; break;
        default: /* Full orchestra: balanced, adaptable foundation */
            pc.gate=84U; pc.swing=6U;
            pc.logic=ORCH_PERF_OR; pc.inverse=0U;
            pc.root=0U; pc.repeat=1U;
            pc.velocity_groove=0U; pc.gate_groove=0U;
            pc.latch=0U; pc.external_clock=0U; break;
        }
        (void)orch_perf_set_config(part,&pc);
    }
    /* The scene's master effects: on/off, mix and all four settings, so
     * nothing of the last scene's effects is left behind. */
    for (unsigned s=0U; s<ORCH_FX_SLOT_COUNT; ++s) {
        (void)orch_fx_set_enabled(s,p->fx_on[s]);
        (void)orch_fx_set_mix(s,p->fx_mix[s]);
        for (unsigned k=0U; k<ORCH_FX_PARAMS; ++k)
            (void)orch_fx_set_param(s,k,p->fx_p[s][k]);
    }
    /* the hall's shared strings, and whether the motif follows the playing */
    orch_resonance_set_amount(p->resonance);
    orch_seq_set_adaptive(p->seq_adaptive);
    /* The 16-step motif and the cross-part call/response are recalled too;
     * the sequencer is stopped until PLAY, as the 0.3.14 scenes left it. */
    orch_seq_set_enabled(0U);
    orch_seq_clear_all();
    orch_seq_set_length(ORCH_SEQ_STEPS);
    orch_seq_set_part(scene_seq_part[index]);   /* the motif's lane */
    for (unsigned st=0U; st<ORCH_SEQ_STEPS; ++st) {
        struct orch_step_note n;
        orch_scene_step(index, st, &n);
        if (n.velocity) (void)orch_seq_set_step(st, 0U, &n);
    }
    orch_call_response_configure(scene_response_enabled[index],
        scene_response_leader[index], scene_response_follower[index],
        scene_response_interval[index]);
    return 0;
}
