/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FM1 application adapter: audio, MIDI, keyboard and a lightweight panel UI.
 *
 * The FM1 Core calls groove_* from its control task and fm1_app_render from
 * the audio interrupt. Keep all drawing and control reads out of the renderer.
 */
#include <stdint.h>
#include "fm1.h"
#include "fm1_app.h"
#include "fm1_engine_control.h"
#include "orch_engine.h"
#include "orch_fx.h"

#define PANEL_KEY_COUNT 27U
#define PANEL_FIRST_NOTE 53U /* F3; FM1 key bitmap is F3..G5. */
#define PANEL_PARTS 4U

static const uint8_t instrument_default[PANEL_PARTS] = {
    ORCH_INST_VIOLIN, ORCH_INST_FLUTE, ORCH_INST_TRUMPET,
    ORCH_INST_MARIMBA
};
static const char *const instrument_names[ORCH_INST_COUNT] = {
    "VIOLIN", "VIOLA", "CELLO", "BASS", "HARP",
    "FLUTE", "PICCOLO", "CLARINET", "OBOE", "BASSOON",
    "TRUMPET", "FRENCH HORN", "TROMBONE", "TUBA",
    "MARIMBA", "XYLOPHONE", "TIMPANI", "CHOIR"
};
static const uint8_t articulation_values[] = {
    ORCH_ART_AUTO, ORCH_ART_LEGATO, ORCH_ART_DETACHE,
    ORCH_ART_STACCATO, ORCH_ART_PIZZICATO
};
static const char *const articulation_names[] = {
    "AUTO", "LEGATO", "DETACHE", "STACCATO", "PIZZ"
};

static uint8_t selected_part;
static uint8_t selected_instrument[PANEL_PARTS];
static uint8_t selected_cap[PANEL_PARTS];
static uint8_t selected_art[PANEL_PARTS];
static uint8_t key_channel[PANEL_KEY_COUNT];
static uint32_t previous_keys;
static uint32_t previous_buttons;
static uint32_t last_panel_poll;
static uint16_t key_velocity = 96U;
static uint8_t panel_dirty = 1U;
static uint8_t panel_perf_page; /* 0 orchestra, 1 performance, 2 phrase detail */
static struct orch_perf_config perf_config[PANEL_PARTS];
static uint8_t selected_preset;
static uint8_t preset_page_return;

/* Factory scenes: four orchestral sections plus their adaptive performance
 * and master-effect starting points. The player's live MIDI input remains
 * the source for adaptive phrasing; presets only choose its musical context. */
typedef struct {
    const char *name;
    uint8_t inst[PANEL_PARTS], art[PANEL_PARTS], cap[PANEL_PARTS];
    uint8_t arp, phrase, arp_type, phrase_ge, bpm, division;
    uint8_t density, range, cluster, accent, quantize;
    uint8_t fx_on[ORCH_FX_SLOT_COUNT], fx_mix[ORCH_FX_SLOT_COUNT];
} orchestra_preset_t;

static const orchestra_preset_t orchestra_presets[] = {
    { "CINEMA STR", {0,1,2,3}, {0,0,0,0}, {4,3,3,2}, 0,0,0,0,88,4,92,3,2,58,1,
      {1,1,0,1},{24,32,0,30} },
    { "BAROQUE", {0,1,4,5}, {2,2,0,0}, {3,2,2,1}, 1,0,2,0,112,4,80,2,2,42,2,
      {1,0,0,1},{18,0,0,20} },
    { "WIND CHOIR", {0,5,7,8}, {0,0,0,0}, {2,3,2,2}, 0,1,0,8,76,5,88,3,2,50,1,
      {1,0,1,1},{22,0,18,34} },
    { "BRASS FAN", {0,10,11,12}, {0,3,0,3}, {1,3,3,2}, 0,0,0,0,104,4,76,2,3,72,2,
      {0,0,0,1},{0,0,0,22} },
    { "PIZZ MOTION", {0,1,4,14}, {4,4,4,3}, {3,2,2,1}, 1,1,5,12,126,5,105,2,3,64,1,
      {1,1,0,1},{28,20,0,24} },
    { "AMBIENT", {2,3,5,17}, {0,0,0,0}, {2,2,2,2}, 0,1,0,19,68,6,72,4,2,36,3,
      {1,1,1,1},{38,42,36,48} },
    { "OSTINATO", {0,2,5,10}, {1,1,0,0}, {3,3,2,2}, 1,1,8,26,120,4,100,2,2,55,1,
      {1,0,1,0},{20,0,24,0} },
    { "FULL ORCH", {0,2,5,10}, {0,0,0,0}, {4,4,3,3}, 0,0,0,0,100,4,90,3,3,60,0,
      {1,1,0,1},{25,25,0,28} }
};
#define ORCH_PRESET_COUNT ((unsigned)(sizeof orchestra_presets / sizeof orchestra_presets[0]))

/* A tiny 3x5 uppercase font. Drawing is control-task-only and uses the
 * platform's documented RGB565 rectangle primitive; no hidden SDK symbols. */
static void glyph_rows(char c, uint8_t rows[5])
{
    static const uint8_t blank[5] = {0,0,0,0,0};
    const uint8_t *g = blank;
#define G(ch,a,b,c_,d,e) case ch: { static const uint8_t v[5]={a,b,c_,d,e}; g=v; break; }
    switch (c) {
    G('A',2,5,7,5,5) G('B',6,5,6,5,6) G('C',3,4,4,4,3)
    G('D',6,5,5,5,6) G('E',7,4,6,4,7) G('F',7,4,6,4,4)
    G('G',3,4,5,5,3) G('H',5,5,7,5,5) G('I',7,2,2,2,7)
    G('J',1,1,1,5,2) G('K',5,5,6,5,5) G('L',4,4,4,4,7)
    G('M',5,7,7,5,5) G('N',5,7,7,7,5) G('O',2,5,5,5,2)
    G('P',6,5,6,4,4) G('Q',2,5,5,7,3) G('R',6,5,6,5,5)
    G('S',3,4,2,1,6) G('T',7,2,2,2,2) G('U',5,5,5,5,7)
    G('V',5,5,5,5,2) G('W',5,5,7,7,5) G('X',5,5,2,5,5)
    G('Y',5,5,2,2,2) G('Z',7,1,2,4,7)
    G('0',7,5,5,5,7) G('1',2,6,2,2,7) G('2',6,1,7,4,7)
    G('3',6,1,3,1,6) G('4',5,5,7,1,1) G('5',7,4,6,1,6)
    G('6',3,4,7,5,7) G('7',7,1,2,2,2) G('8',7,5,7,5,7)
    G('9',7,5,7,1,6) G('-',0,0,7,0,0) G('/',1,1,2,4,4)
    G(':',0,2,0,2,0) G('+',0,2,7,2,0)
    default: break;
    }
#undef G
    for (unsigned i=0;i<5U;++i) rows[i]=g[i];
}

static void panel_text(unsigned x, unsigned y, const char *s, uint16_t color)
{
    while (*s && x < 232U) {
        uint8_t rows[5];
        glyph_rows(*s++, rows);
        for (unsigned yy=0; yy<5U; ++yy)
            for (unsigned xx=0; xx<3U; ++xx)
                if (rows[yy] & (1U << (2U-xx)))
                    fm1_screen_fill(x+xx*3U,y+yy*3U,3U,3U,color);
        x += 12U;
    }
}

static unsigned channel_first(unsigned part)
{
    return part * 4U; /* default groups: 1-4, 5-8, 9-12, 13-16 */
}

static void panel_apply_instrument(unsigned part, unsigned instrument)
{
    if (part >= PANEL_PARTS) return;
    instrument %= ORCH_INST_COUNT;
    fm1_engine_pause();
    (void)orch_engine_set_instrument(part, instrument);
    fm1_engine_resume();
    selected_instrument[part]=(uint8_t)instrument;
    panel_dirty=1U;
}

static void panel_apply_cap(unsigned part, unsigned cap)
{
    if (part >= PANEL_PARTS) return;
    if (cap < 1U) cap=1U;
    if (cap > 4U) cap=4U;
    fm1_engine_pause();
    (void)orch_engine_set_player_cap(part,cap);
    fm1_engine_resume();
    selected_cap[part]=(uint8_t)cap;
    panel_dirty=1U;
}

static void panel_apply_articulation(unsigned part, unsigned index)
{
    const unsigned count=(unsigned)(sizeof articulation_values/sizeof articulation_values[0]);
    if (part >= PANEL_PARTS) return;
    index %= count;
    fm1_engine_pause();
    (void)orch_engine_set_articulation(part,articulation_values[index]);
    fm1_engine_resume();
    selected_art[part]=(uint8_t)index;
    panel_dirty=1U;
}

static void panel_apply_performance(unsigned part)
{
    if (part >= PANEL_PARTS) return;
    fm1_engine_pause();
    (void)orch_perf_set_config(part, &perf_config[part]);
    fm1_engine_resume();
    panel_dirty = 1U;
}

static void panel_draw(void)
{
    if (panel_perf_page == 2U) {
        static const uint16_t bg=0x0841U, ink=0xFFFFU, cyan=0x07FFU;
        static const uint16_t amber=0xFD20U;
        struct orch_perf_config *pc=&perf_config[selected_part];
        char line[24];
        fm1_screen_fill(0U,0U,FM1_SCREEN_WIDTH,FM1_SCREEN_HEIGHT,bg);
        panel_text(8U,8U,"PHRASE DETAIL",cyan);
        panel_text(8U,30U,"DENSITY",amber);
        line[0]=(char)('0'+(pc->density/100U)%10U);line[1]=(char)('0'+(pc->density/10U)%10U);line[2]=(char)('0'+pc->density%10U);line[3]=0;
        panel_text(152U,30U,line,ink);
        panel_text(8U,54U,"OCTAVE RANGE",amber);
        line[0]=(char)('0'+pc->range);line[1]=0;panel_text(152U,54U,line,ink);
        panel_text(8U,78U,"CHORD CLUSTER",amber);
        line[0]=(char)('0'+pc->cluster);line[1]=0;panel_text(152U,78U,line,ink);
        panel_text(8U,102U,"REPEATS",amber);
        line[0]=(char)('0'+pc->repeat);line[1]=0;panel_text(152U,102U,line,ink);
        panel_text(8U,126U,"ACCENT",amber);
        line[0]=(char)('0'+(pc->accent/100U)%10U);line[1]=(char)('0'+(pc->accent/10U)%10U);line[2]=(char)('0'+pc->accent%10U);line[3]=0;
        panel_text(152U,126U,line,ink);
        panel_text(8U,150U,pc->inverse?"INVERSE ON":"INVERSE OFF",cyan);
        line[0]='R';line[1]='O';line[2]='O';line[3]='T';line[4]=' ';
        if(pc->root==0U){line[5]='A';line[6]='U';line[7]='T';line[8]='O';line[9]=0;}
        else{line[5]=(char)('0'+pc->root/10U);line[6]=(char)('0'+pc->root%10U);line[7]=0;}
        panel_text(8U,174U,line,ink);
        panel_text(8U,204U,"B5 INV B6 ROOT B7 AUTO",cyan);
        panel_text(8U,224U,"B8 QTZ B14 PAGE",amber);
        return;
    }
    if (panel_perf_page == 1U) {
        static const uint16_t bg=0x0841U, ink=0xFFFFU, cyan=0x07FFU;
        static const uint16_t amber=0xFD20U, green=0x07E0U;
        static const char *const logic_names[] = {"OR","AND","XOR","NOR","ALT","S/R"};
        static const char *const part_labels[PANEL_PARTS] = {
            "PART 1 CH 1-4", "PART 2 CH 5-8",
            "PART 3 CH 9-12", "PART 4 CH 13-16"
        };
        struct orch_perf_config *pc=&perf_config[selected_part];
        char line[24];
        fm1_screen_fill(0U,0U,FM1_SCREEN_WIDTH,FM1_SCREEN_HEIGHT,bg);
        panel_text(8U,8U,"FM1 PERFORMANCE",cyan);
        panel_text(8U,30U,part_labels[selected_part],ink);
        panel_text(8U,52U,pc->arp_enabled?"ARP ON":"ARP OFF",amber);
        panel_text(120U,52U,pc->phrase_enabled?"SEQ ON":"SEQ OFF",amber);
        line[0]='B';line[1]='P';line[2]='M';line[3]=' ';
        line[4]=(char)('0'+(pc->bpm/100U)%10U);
        line[5]=(char)('0'+(pc->bpm/10U)%10U);
        line[6]=(char)('0'+pc->bpm%10U);line[7]=0;
        panel_text(8U,76U,line,ink);
        line[0]='D';line[1]='I';line[2]='V';line[3]=' ';
        line[4]=(char)('0'+pc->division+1U);line[5]=0;
        panel_text(120U,76U,line,ink);
        line[0]='A';line[1]='R';line[2]='P';line[3]=' ';
        line[4]=(char)('0'+(pc->arp_type/10U)%10U);
        line[5]=(char)('0'+pc->arp_type%10U);line[6]=0;
        panel_text(8U,100U,line,ink);
        line[0]='G';line[1]='E';line[2]=' ';
        line[3]=(char)('0'+(pc->phrase_ge/10U)%10U);
        line[4]=(char)('0'+pc->phrase_ge%10U);line[5]=0;
        panel_text(120U,100U,line,ink);
        line[0]='G';line[1]='A';line[2]='T';line[3]='E';line[4]=' ';
        line[5]=(char)('0'+(pc->gate/100U)%10U);
        line[6]=(char)('0'+(pc->gate/10U)%10U);
        line[7]=(char)('0'+pc->gate%10U);line[8]=0;
        panel_text(8U,124U,line,ink);
        line[0]='S';line[1]='W';line[2]='I';line[3]='N';line[4]='G';line[5]=' ';
        line[6]=(char)('0'+(pc->swing/100U)%10U);
        line[7]=(char)('0'+(pc->swing/10U)%10U);
        line[8]=(char)('0'+pc->swing%10U);line[9]=0;
        panel_text(120U,124U,line,ink);
        panel_text(8U,148U,logic_names[pc->logic%ORCH_PERF_LOGIC_COUNT],amber);
        panel_text(72U,148U,pc->latch?"LATCH ON":"LATCH OFF",ink);
        panel_text(8U,172U,pc->external_clock?"CLOCK EXT":"CLOCK INT",cyan);
        line[0]='V';line[1]='G';line[2]=' ';line[3]=(char)('0'+pc->velocity_groove);line[4]=0;
        panel_text(120U,172U,line,ink);
        line[0]='G';line[1]='G';line[2]=' ';line[3]=(char)('0'+pc->gate_groove);line[4]=0;
        panel_text(168U,172U,line,ink);
        const unsigned step=orch_perf_current_step(selected_part)&15U;
        fm1_screen_fill(8U,196U,216U,6U,0x39E7U);
        fm1_screen_fill(8U,196U,(216U*step)/15U,6U,green);
        panel_text(8U,208U,"B8 ARP B9 SEQ B10 HOLD",cyan);
        panel_text(8U,228U,"B11 LOGIC B12 CLK B13 QTZ B14 PAGE",amber);
        return;
    }
    static const uint16_t bg=0x0841U, ink=0xFFFFU, cyan=0x07FFU;
    static const uint16_t amber=0xFD20U, green=0x07E0U, red=0xF800U;
    const unsigned p=selected_part;
    static const char *const part_labels[PANEL_PARTS] = {
        "PART 1 CH 1-4", "PART 2 CH 5-8",
        "PART 3 CH 9-12", "PART 4 CH 13-16"
    };
    char line[24];
    fm1_screen_fill(0U,0U,FM1_SCREEN_WIDTH,FM1_SCREEN_HEIGHT,bg);
    panel_text(8U,8U,"FM1 ORCHESTRA",cyan);
    panel_text(8U,32U,part_labels[p],ink);
    panel_text(8U,56U,"INSTRUMENT",amber);
    panel_text(8U,76U,instrument_names[selected_instrument[p]],ink);
    panel_text(8U,100U,"ARTICULATION",amber);
    panel_text(8U,120U,articulation_names[selected_art[p]],ink);
    panel_text(8U,144U,"DIVISI PLAYERS",amber);
    line[0]=(char)('0'+selected_cap[p]); line[1]='/'; line[2]='4'; line[3]=0;
    panel_text(8U,164U,line,ink);
    fm1_screen_fill(8U,188U,216U,8U,0x39E7U);
    fm1_screen_fill(8U,188U,(216U*selected_cap[p])/4U,8U,green);
    panel_text(8U,204U,"ENC1 INST ENC2 CAP",cyan);
    panel_text(8U,224U,"B1-4 PART B5 PANIC B14 PERF",red);
}

static void panel_part_select(unsigned part)
{
    if (part >= PANEL_PARTS || part==selected_part) return;
    selected_part=(uint8_t)part;
    panel_dirty=1U;
}

void groove_screen_init(void)
{
    fm1_controls_init();
    fm1_screen_init();
    selected_part=0U;
    for (unsigned p=0;p<PANEL_PARTS;++p) {
        selected_instrument[p]=instrument_default[p];
        selected_cap[p]=2U;
        selected_art[p]=0U;
        orch_perf_default_config(&perf_config[p]);
        (void)orch_perf_set_config(p,&perf_config[p]);
    }
    for (unsigned k=0;k<PANEL_KEY_COUNT;++k) key_channel[k]=0U;
    previous_keys=0U;
    previous_buttons=0U;
    last_panel_poll=0U;
    key_velocity=96U;
    panel_perf_page=0U;
    panel_dirty=1U;
    panel_draw();
}

void groove_screen_control(void)
{
    struct fm1_controls controls;
    const uint32_t now=fm1_millis();
    if ((uint32_t)(now-last_panel_poll)<10U) return;
    last_panel_poll=now;
    fm1_controls_read(&controls);

    const uint32_t keys=controls.keys & ((1UL<<PANEL_KEY_COUNT)-1UL);
    const uint32_t pressed=keys & ~previous_keys;
    const uint32_t released=previous_keys & ~keys;
    for (unsigned k=0;k<PANEL_KEY_COUNT;++k) {
        const uint32_t bit=1UL<<k;
        const unsigned note=PANEL_FIRST_NOTE+k;
        if (pressed & bit) {
            const unsigned ch=channel_first(selected_part);
            key_channel[k]=(uint8_t)ch;
            unsigned knob=controls.knob;
            if (knob>1023U) knob=1023U;
            key_velocity=(uint16_t)(48U+knob*79U/1023U);
            orch_note_event(ch,note,key_velocity);
            panel_dirty=1U;
        }
        if (released & bit) {
            orch_note_event(key_channel[k],note,0U);
            panel_dirty=1U;
        }
    }
    previous_keys=keys;

    const uint32_t buttons=controls.buttons;
    const uint32_t edges=buttons & ~previous_buttons;
    previous_buttons=buttons;
    for (unsigned p=0;p<PANEL_PARTS;++p)
        if (edges & (1UL<<p)) panel_part_select(p);
    if (edges & (1UL<<4)) orch_panic();
    if (edges & (1UL<<13)) { panel_perf_page=(uint8_t)((panel_perf_page+1U)%4U); panel_dirty=1U; }
    if (!panel_perf_page) {
        if (edges & (1UL<<5)) panel_apply_articulation(selected_part,selected_art[selected_part]+1U);
        if (edges & (1UL<<6)) panel_apply_cap(selected_part,(selected_cap[selected_part]%4U)+1U);
        const int instrument_delta=controls.encoders[0];
        if (instrument_delta) {
            int next=(int)selected_instrument[selected_part]+instrument_delta;
            while(next<0)next+=(int)ORCH_INST_COUNT;
            while(next>=(int)ORCH_INST_COUNT)next-=(int)ORCH_INST_COUNT;
            panel_apply_instrument(selected_part,(unsigned)next);
        }
        if (controls.encoders[1]) {
            int next=(int)selected_cap[selected_part]+controls.encoders[1];
            if(next<1) next=1;
            if(next>4) next=4;
            panel_apply_cap(selected_part,(unsigned)next);
        }
        if (controls.encoders[2]) {
            int next=(int)selected_art[selected_part]+controls.encoders[2];
            const int count=(int)(sizeof articulation_values/sizeof articulation_values[0]);
            while(next<0) next+=count;
            while(next>=count) next-=count;
            panel_apply_articulation(selected_part,(unsigned)next);
        }
        if (controls.encoders[3]) {
            int next=(int)selected_part+controls.encoders[3];
            while(next<0)next+=(int)PANEL_PARTS;
            while(next>=(int)PANEL_PARTS)next-=(int)PANEL_PARTS;
            panel_part_select((unsigned)next);
        }
    } else if (panel_perf_page == 1U) {
        struct orch_perf_config *pc=&perf_config[selected_part];
        uint8_t changed=0U;
        if (edges & (1UL<<5)) { pc->velocity_groove=(uint8_t)((pc->velocity_groove+1U)&7U); changed=1U; }
        if (edges & (1UL<<6)) { pc->gate_groove=(uint8_t)((pc->gate_groove+1U)&7U); changed=1U; }
        if (edges & (1UL<<7)) { pc->arp_enabled=(uint8_t)!pc->arp_enabled; changed=1U; }
        if (edges & (1UL<<8)) { pc->phrase_enabled=(uint8_t)!pc->phrase_enabled; changed=1U; }
        if (edges & (1UL<<9)) { pc->latch=(uint8_t)!pc->latch; changed=1U; }
        if (edges & (1UL<<10)) { pc->logic=(uint8_t)((pc->logic+1U)%ORCH_PERF_LOGIC_COUNT); changed=1U; }
        if (edges & (1UL<<11)) { pc->external_clock=(uint8_t)!pc->external_clock; changed=1U; }
        if (edges & (1UL<<12)) { pc->quantize=(uint8_t)((pc->quantize+1U)%18U); changed=1U; }
        if (controls.encoders[0]) {
            if (pc->arp_enabled) {
                int next=(int)pc->arp_type+controls.encoders[0];
                while(next<0)next+=(int)ORCH_PERF_ARP_TYPES;
                while(next>=(int)ORCH_PERF_ARP_TYPES)next-=(int)ORCH_PERF_ARP_TYPES;
                pc->arp_type=(uint8_t)next;
            } else {
                int next=(int)pc->phrase_ge+controls.encoders[0];
                while(next<0)next+=(int)ORCH_PERF_GE_COUNT;
                while(next>=(int)ORCH_PERF_GE_COUNT)next-=(int)ORCH_PERF_GE_COUNT;
                pc->phrase_ge=(uint8_t)next;
            }
            changed=1U;
        }
        if (controls.encoders[1]) {
            int next=(int)pc->bpm+controls.encoders[1];
            if(next<40)next=40;
            if(next>250)next=250;
            pc->bpm=(uint16_t)next; changed=1U;
        }
        if (controls.encoders[2]) {
            int next=(int)pc->division+controls.encoders[2];
            while(next<0)next+=(int)ORCH_PERF_DIVISIONS;
            while(next>=(int)ORCH_PERF_DIVISIONS)next-=(int)ORCH_PERF_DIVISIONS;
            pc->division=(uint8_t)next; changed=1U;
        }
        if (controls.encoders[3]) {
            int next=(int)pc->swing+controls.encoders[3]*4;
            if(next<0)next=0;
            if(next>127)next=127;
            pc->swing=(uint8_t)next; changed=1U;
        }
        unsigned knob=controls.knob;
        if(knob>1023U) knob=1023U;
        const uint8_t gate=(uint8_t)(5U+knob*95U/1023U);
        if(gate!=pc->gate){pc->gate=gate;changed=1U;}
        if(changed) panel_apply_performance(selected_part);
    } else {
        struct orch_perf_config *pc=&perf_config[selected_part];
        uint8_t changed=0U;
        if (edges & (1UL<<5)) { pc->inverse=(uint8_t)!pc->inverse; changed=1U; }
        if (edges & (1UL<<6)) { pc->root=(uint8_t)((pc->root+1U)%13U); changed=1U; }
        if (edges & (1UL<<7)) { pc->root=0U; changed=1U; }
        if (edges & (1UL<<8)) { pc->quantize=(uint8_t)((pc->quantize+1U)%18U); changed=1U; }
        if (controls.encoders[0]) {
            int v=(int)pc->density+controls.encoders[0]*4;
            if(v<0)v=0;
            if(v>127)v=127;
            pc->density=(uint8_t)v;changed=1U;
        }
        if (controls.encoders[1]) {
            int v=(int)pc->range+controls.encoders[1];
            if(v<1)v=1;
            if(v>4)v=4;
            pc->range=(uint8_t)v;changed=1U;
        }
        if (controls.encoders[2]) {
            int v=(int)pc->cluster+controls.encoders[2];
            if(v<1)v=1;
            if(v>4)v=4;
            pc->cluster=(uint8_t)v;changed=1U;
        }
        if (controls.encoders[3]) {
            int v=(int)pc->repeat+controls.encoders[3];
            if(v<0)v=0;
            if(v>3)v=3;
            pc->repeat=(uint8_t)v;changed=1U;
        }
        unsigned knob=controls.knob;
        if(knob>1023U)knob=1023U;
        const uint8_t accent=(uint8_t)(knob*127U/1023U);
        if(accent!=pc->accent){pc->accent=accent;changed=1U;}
        if(changed)panel_apply_performance(selected_part);
    }
    for (unsigned b=0;b<14U;++b)
        fm1_led_set(b,(buttons>>b)&1U);
    for (unsigned k=0;k<PANEL_KEY_COUNT;++k)
        fm1_led_set(14U+k,(keys>>k)&1U);
    if (panel_dirty) {
        panel_draw();
        panel_dirty=0U;
    }
}

void groove_midi_init(void)
{
    orch_init();
    for(unsigned p=0U;p<PANEL_PARTS;++p)
        (void)orch_perf_set_config(p,&perf_config[p]);
}
void groove_midi_poll(void) {}
void groove_midi_panic(void) { orch_panic(); }
unsigned groove_midi_mode(unsigned track) { (void)track; return 0U; }
void groove_midi_note(unsigned track,unsigned note,unsigned velocity)
{
    orch_note_event(track&15U,note,velocity);
}
void groove_midi_track_reset(unsigned track) { (void)track; }
void groove_midi_transport(unsigned owner,uint8_t status) { (void)owner; orch_midi_realtime(status); }
void groove_midi_clock(unsigned owner,unsigned tempo) { (void)owner; orch_midi_clock_tempo(tempo); }
void groove_midi_clock_reset(void) { orch_midi_realtime(0xFCU); }

void fm1_app_midi_received(unsigned source,const uint8_t *message,size_t bytes)
{
    (void)source;
    orch_midi_short_message(message,(unsigned)bytes);
}

FM1_AUDIO_CODE int fm1_app_render(int32_t *output,unsigned frames)
{
    if (!output || frames!=FM1_BLOCK_FRAMES) return -1;
    orch_render_q23(output,frames);
    return 0;
}
