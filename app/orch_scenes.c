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

static const uint8_t ART_VALUES[5] = {
    ORCH_ART_AUTO, ORCH_ART_LEGATO, ORCH_ART_DETACHE,
    ORCH_ART_STACCATO, ORCH_ART_PIZZICATO
};

/* Four orchestral sections plus their adaptive performance and master-effect
 * starting points. The player's live MIDI input remains the source for
 * adaptive phrasing; a scene only chooses its musical context. */
typedef struct {
    const char *name;
    uint8_t inst[ORCH_PARTS], art[ORCH_PARTS], cap[ORCH_PARTS];
    uint8_t arp, phrase, arp_type, phrase_ge, bpm, division;
    uint8_t density, range, cluster, accent, quantize;
    uint8_t fx_on[ORCH_FX_SLOT_COUNT], fx_mix[ORCH_FX_SLOT_COUNT];
} scene_t;

static const scene_t scenes[ORCH_SCENE_COUNT] = {
    { "CINEMA STR", {0,1,2,3,5,6,11,17}, {0,0,0,0,0,0,0,0}, {4,3,3,2,2,2,2,1}, 0,0,0,0,88,4,92,3,2,58,1,
      {1,1,0,1},{24,32,0,30} },
    { "BAROQUE", {0,1,4,5,2,6,8,14}, {2,2,0,0,0,2,0,0}, {3,2,2,1,2,2,2,1}, 1,0,2,0,112,4,80,2,2,42,2,
      {1,0,0,1},{18,0,0,20} },
    { "WIND CHOIR", {0,5,7,8,6,9,17,2}, {0,0,0,0,0,0,0,0}, {2,3,2,2,2,2,1,2}, 0,1,0,8,76,5,88,3,2,50,1,
      {1,0,1,1},{22,0,18,34} },
    { "BRASS FAN", {0,10,11,12,13,9,2,17}, {0,3,0,3,3,0,0,0}, {1,3,3,2,2,2,2,1}, 0,0,0,0,104,4,76,2,3,72,2,
      {0,0,0,1},{0,0,0,22} },
    { "PIZZ MOTION", {0,1,4,14,2,5,6,16}, {4,4,4,3,4,0,0,3}, {3,2,2,1,2,2,2,1}, 1,1,5,12,126,5,105,2,3,64,1,
      {1,1,0,1},{28,20,0,24} },
    { "AMBIENT", {2,3,5,17,6,7,11,14}, {0,0,0,0,0,0,0,0}, {2,2,2,2,2,2,2,1}, 0,1,0,19,68,6,72,4,2,36,3,
      {1,1,1,1},{38,42,36,48} },
    { "OSTINATO", {0,2,5,10,1,6,11,14}, {1,1,0,0,1,0,0,3}, {3,3,2,2,2,2,2,1}, 1,1,8,26,120,4,100,2,2,55,1,
      {1,0,1,0},{20,0,24,0} },
    { "FULL ORCH", {0,2,5,10,1,6,11,17}, {0,0,0,0,0,0,0,0}, {4,4,3,3,3,3,2,2}, 0,0,0,0,100,4,90,3,3,60,0,
      {1,1,0,1},{25,25,0,28} }
};

/* Independent 16-step orchestral motifs. 60 is a neutral pitch anchor;
 * adaptive mode revoices against the player's live chord/contour. Zero
 * velocity is a rest. Each row is one complete scene's step/velocity/gate
 * contour, not a replacement for the live ARP or phrase generator. */
typedef struct { uint8_t note, velocity, gate; } scene_step_t;
static const scene_step_t scene_steps[ORCH_SCENE_COUNT][ORCH_SEQ_STEPS] = {
 /* Cinema: spacious pulse, phrase-shaped rests */
 {{60,72,92},{0,0,0},{64,62,92},{0,0,0},{67,76,96},{0,0,0},{64,60,92},{0,0,0},
  {62,70,92},{0,0,0},{65,62,92},{0,0,0},{69,80,96},{0,0,0},{65,58,88},{0,0,0}},
 /* Baroque: measured eighth-note motion */
 {{60,88,68},{64,66,68},{67,76,68},{64,66,68},{62,84,68},{65,64,68},{69,74,68},{65,64,68},
  {64,86,68},{67,66,68},{71,76,68},{67,66,68},{65,82,68},{69,64,68},{72,76,68},{69,64,68}},
 /* Wind choir: breath-like spaced entries */
 {{60,72,96},{0,0,0},{0,0,0},{64,62,96},{0,0,0},{67,74,100},{0,0,0},{0,0,0},
  {62,70,96},{0,0,0},{0,0,0},{65,62,96},{0,0,0},{69,76,100},{0,0,0},{0,0,0}},
 /* Brass fanfare: dotted ceremonial calls */
 {{60,110,62},{0,0,0},{67,92,62},{0,0,0},{72,112,72},{0,0,0},{67,84,62},{0,0,0},
  {65,104,62},{0,0,0},{69,90,62},{0,0,0},{74,116,76},{0,0,0},{69,88,62},{0,0,0}},
 /* Pizz motion: syncopated short notes */
 {{60,94,42},{0,0,0},{64,68,38},{67,82,42},{0,0,0},{64,72,38},{0,0,0},{69,90,42},
  {0,0,0},{67,70,38},{64,82,42},{0,0,0},{62,72,38},{0,0,0},{65,92,42},{0,0,0}},
 /* Ambient: sparse slow anchors */
 {{60,58,100},{0,0,0},{0,0,0},{0,0,0},{67,52,100},{0,0,0},{0,0,0},{0,0,0},
  {62,56,100},{0,0,0},{0,0,0},{0,0,0},{69,54,100},{0,0,0},{0,0,0},{0,0,0}},
 /* Ostinato: interlocking pulse with rests */
 {{60,90,74},{0,0,0},{67,72,62},{0,0,0},{60,84,74},{64,66,58},{0,0,0},{67,76,62},
  {60,88,74},{0,0,0},{67,70,62},{64,66,58},{60,82,74},{0,0,0},{69,80,62},{0,0,0}},
 /* Full orchestra: broad anchor + cadence */
 {{60,76,84},{0,0,0},{64,62,84},{0,0,0},{67,80,90},{0,0,0},{69,66,84},{0,0,0},
  {67,74,84},{0,0,0},{65,62,84},{0,0,0},{64,76,88},{0,0,0},{60,68,96},{0,0,0}}
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
    return index < ORCH_SCENE_COUNT ? scenes[index].bpm : 120U;
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
    if (index >= ORCH_SCENE_COUNT || step >= ORCH_SEQ_STEPS) return;
    const scene_step_t *s = &scene_steps[index][step];
    out->note = s->note;
    out->velocity = s->velocity;
    out->gate_percent = s->gate;
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
    /* Full performance snapshots: ARP + phrase sequencer share the same
     * timing/groove/clock controls, but each scene gets its own character. */
    static const uint8_t fx_a[ORCH_SCENE_COUNT][ORCH_FX_SLOT_COUNT] = {
        {28,34,42,62}, {18,22,28,38}, {24,42,36,48}, {20,26,32,30},
        {36,30,44,52}, {16,38,56,78}, {22,28,30,36}, {26,32,40,58}
    };
    static const uint8_t fx_b[ORCH_SCENE_COUNT][ORCH_FX_SLOT_COUNT] = {
        {32,46,28,42}, {18,30,22,18}, {40,54,34,36}, {24,42,20,16},
        {48,38,46,34}, {62,72,58,86}, {28,34,24,20}, {36,48,38,44}
    };
    if (index >= ORCH_SCENE_COUNT) return -1;
    const scene_t *p = &scenes[index];
    orch_reset_notes();
    for (unsigned part=0U; part<ORCH_PARTS; ++part) {
        struct orch_perf_config pc;
        (void)orch_engine_set_instrument(part,p->inst[part]);
        (void)orch_engine_set_articulation(part,ART_VALUES[p->art[part] < 5U ? p->art[part] : 0U]);
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
    /* The scene's master effects: on/off and mix from the scene, the first two
     * of each effect's four parameters from its character table. */
    for (unsigned s=0U; s<ORCH_FX_SLOT_COUNT; ++s) {
        (void)orch_fx_set_enabled(s,p->fx_on[s]);
        (void)orch_fx_set_mix(s,p->fx_mix[s]);
        (void)orch_fx_set_param(s,0U,fx_a[index][s]);
        (void)orch_fx_set_param(s,1U,fx_b[index][s]);
    }
    /* The 16-step motif and the cross-part call/response are recalled too;
     * the sequencer is stopped until PLAY, as the 0.3.14 scenes left it. */
    orch_seq_set_enabled(0U);
    orch_seq_clear();
    for (unsigned st=0U; st<ORCH_SEQ_STEPS; ++st) {
        struct orch_step_note n;
        orch_scene_step(index, st, &n);
        if (n.velocity) (void)orch_seq_set_step(st, 0U, &n);
    }
    orch_seq_set_part(scene_seq_part[index]);
    orch_call_response_configure(scene_response_enabled[index],
        scene_response_leader[index], scene_response_follower[index],
        scene_response_interval[index]);
    return 0;
}
