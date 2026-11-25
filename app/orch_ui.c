/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FM-1 panel for the orchestral engine.
 *
 * The printed panel labels and the matrix ids come from the FM-1 board itself
 * (buttons 0..13 and encoders 0..6 as M-VAVE numbers them):
 *
 *   buttons   OCT- OCT+ FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC
 *   encoders  SELECT ALGORITHM KNOB1 KNOB2 KNOB3 KNOB4 PRESETS
 *
 * and this file gives every one of them a job (§ "Panel map" in docs/PANEL.md).
 * The panel has no text support in the SDK, so the 5x7 font lives beside this
 * file (orch_ui_font.inc) and one text line is drawn into a small buffer and
 * blitted in one transfer.
 *
 * The look is the browser page's: coffee, a hairline, amber for a value, green
 * for a section that is sounding — and it is drawn, not printed: the shapes the
 * pages are made of live in orch_ui_draw.inc. Five things on the panel are
 * photographs instead (0.4.0 also had the hall on the stage page; since 0.6.0 that is a vector seating plan), the
 * band under the work of each of the four menus (orch_ui_pictures.inc).
 *
 * Nothing here runs in the audio interrupt: groove_screen_control() is the
 * control task. It shares the master gain and one byte per FX setting with the
 * renderer, and nothing else.
 *
 * 0.5.0: the panel of the 0.4.0 line drives the engine of the 0.3.x feature
 * line — eight parts on one shared pool of eight voices, the ARP/phrase engine
 * (orch_performance), per-part ADSR and LFO (the ENV and LFO pages) and the
 * factory scenes (orch_scenes, on the CONSOLE). The same source runs on the
 * FM1 Core SDK, in the browser and, through runtime/hawk/orch_hawk_panel.c,
 * in the HAWK/Felucca firmware, which is freestanding: no <string.h> here.
 */
#include <stddef.h>

#include "fm1.h"
#include "fm1_engine_control.h"
#include "orch_engine.h"
#include "orch_fx.h"
#include "orch_level.h"
#include "orch_part.h"
#include "orch_performance.h"
#include "orch_resonance.h"
#include "orch_scenes.h"
#include "orch_stage.h"
#include "orch_ui.h"

#define SW 240U
#define TITLE_H 26U                    /* the page name, and the page's bar */
#define BODY_Y (TITLE_H + 4U)
#define STRIP_Y 190U                  /* the knob cells, under the page body */
#define ROW_H 26U                     /* one part row on the stage page */
#define STRIP_H (SW - STRIP_Y)
#define UI_IDLE_MS 25U
#define UNITY_Q23 (1UL << 23)
#define SEQ_SLOTS 4U

/* The panel's palette is the browser page's: the case's coffee, a hairline, and
 * amber for everything that is a value or a choice; green is a section that is
 * sounding (or a step that is playing), and the wine red is only for recording.
 * The hall keeps its burgundy curtains and wooden floor, on the same coffee. */
enum {
    C_BG = 0x1082, C_CARD = 0x2104, C_ROW = 0x2945, C_LINE = 0x39A5,
    C_TEXT = 0xFFFF, C_DIM = 0xA4D1,
    C_AMBER = 0xF524, C_AMBER2 = 0xFE6E,
    C_METER = 0x07E0, C_NOW = 0x07E0, C_REC = 0xE2A7,
    C_WOOD = 0x8200, C_CURTAIN = 0x8800, C_SHELL = 0x2104,
    /* the names the rest of the panel and the host tests already use */
    C_TITLE = C_BG, C_SEL = C_ROW, C_ART = C_AMBER, C_STEP = C_CARD
};

/* The matrix ids, from the board's own numbering. */
enum {
    B_OCT_DOWN = 0, B_OCT_UP, B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO,
    B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_COUNT
};

/* The encoder roles, in matrix order: PRESETS is 6, KNOB1..4 are 2..5. */
enum { E_SELECT = 0, E_ALGO, E_KNOB1, E_KNOB2, E_KNOB3, E_KNOB4, E_PRESET };

enum { PAGE_HOME = 0, PAGE_ARP, PAGE_SEQ, PAGE_GLO, PAGE_FX, PAGE_ENV, PAGE_LFO,
       PAGE_COUNT };

enum { SCALE_OFF = 0, SCALE_MAJOR, SCALE_MINOR, SCALE_PENTA, SCALE_COUNT };

/* "OR60": the 0.6.0 record (0.5.0's "OR50" had a shorter ARP block and is
 * not read). "OR50" was the 0.5.0 record. The 0.4.0 one ("ORCH") held four parts and the
 * 0.4.0 arpeggiator's settings, so it is not read: a 0.4.0 record was never
 * written on an FM-1 (that line ran in the browser only). New blocks are
 * appended after `check` with their own FNV-1a, as the FX and the listener
 * were in 0.4.0, so a later panel can still load this one. */
#define SAVE_MAGIC 0x3036524FUL /* "OR60" */

struct save_part {
    uint8_t family, instrument, articulation, players;
    uint16_t attack_ms, decay_ms, release_ms;
    uint8_t sustain_percent;
    uint8_t lfo_wave, lfo_dest, lfo_depth, lfo_sync;
    uint8_t pad;
    uint16_t lfo_rate_millihz, lfo_delay_ms, lfo_fade_ms;
};

struct save_record {
    uint32_t magic;
    uint16_t tempo;
    int8_t transpose;
    uint8_t scale, seq_on, part, scene, seq_part;
    struct save_part parts[ORCH_PARTS];
    struct orch_perf_config perf[ORCH_PARTS];
    struct orch_step_note steps[ORCH_PARTS][ORCH_SEQ_STEPS][4];
    uint8_t seq_length, lane_muted[ORCH_PARTS];
    uint8_t fx_on[ORCH_FX_SLOT_COUNT], fx_mix[ORCH_FX_SLOT_COUNT];
    uint8_t fx_p[ORCH_FX_SLOT_COUNT][ORCH_FX_PARAMS];
    int8_t listener_x;
    uint8_t listener_depth;
    uint8_t pad[2];
    uint32_t check;
    /* 0.6.3, appended with its own checksum so an "OR60" record of 0.6.0..2
     * still loads: the orchestra's shared resonance */
    uint8_t resonance;
    uint8_t pad2[3];
    uint32_t res_check;
};

#define MASTER_MAX_Q23 (4UL << 23)   /* +12 dB at the top of the knob */
volatile uint32_t orch_ui_master_q23 = UNITY_Q23;

static struct {
    uint8_t page, part, seq_on, rec, seq_step, scale, saved, fx_sel, fx_bank;
    uint8_t scene;                    /* the factory scene last recalled */
    uint8_t save_req;                 /* SAVE pressed: written after the edits */
    uint8_t arp_bank;                 /* which four ARP settings the knobs hold */
    uint8_t seq_bank;                 /* SEQUENCE: 0 the step, 1 the pattern */
    int8_t transpose;
    uint16_t tempo;
    uint32_t buttons, keys;
    uint16_t knob;
    uint8_t hot_cell;                 /* the knob cell that moved last (4: none) */
    uint8_t chord_left;
    uint32_t chord_off_at;
    uint32_t hot_until;
} ui;

/* What the panel wrote into the sequencer; the engine only has a setter, so the
 * screen shows this mirror. */
static struct orch_step_note seq_lanes[ORCH_PARTS][ORCH_SEQ_STEPS][SEQ_SLOTS];
/* The lane of the selected part: every part has its own since 0.6.0. */
#define seq_notes (seq_lanes[ui.part])

/* Platforms with a tight RAM section can place the panel's two big buffers
 * elsewhere (the HAWK firmware build puts them in its .pool). */
#ifndef ORCH_UI_DATA
#define ORCH_UI_DATA
#endif
static uint16_t text_buf[SW * 8U] ORCH_UI_DATA;
static uint8_t shown_voices[ORCH_PARTS];
static uint32_t next_scan;
static uint32_t drawn_signature;   /* the page's own settings, as last drawn */
static uint32_t drawn_live;        /* and its live values */
static uint32_t drawn_strip;       /* and the knob strip */
static uint8_t hall_static;        /* the hall's picture is on screen */
static uint8_t page_pic_page = 0xFFU; /* the page whose picture is on screen */
static uint8_t page_pic_static;    /* and that it is there, under the values */


/* --- strings -------------------------------------------------------------- */

static const char *inst_short(unsigned instrument)
{
    static const char *const names[ORCH_INST_COUNT] = {
        "VIOLIN", "VIOLA", "CELLO", "BASS", "HARP",
        "FLUTE", "PICCOLO", "CLARINET", "OBOE", "BASSOON",
        "TRUMPET", "HORN", "TROMBONE", "TUBA",
        "MARIMBA", "XYLO", "TIMPANI", "CHOIR"
    };
    return instrument < ORCH_INST_COUNT ? names[instrument] : "?";
}

/* The performance engine's 31 ARP types: five orders (up, down, up-down,
 * up-down with the ends repeated, random) at one, two and four octaves, then
 * sixteen step patterns with rests. */
static const char *arp_type_name(unsigned t)
{
    static const char *const names[15] = {
        "UP 1", "UP 2", "UP 4", "DOWN 1", "DOWN 2", "DOWN 4",
        "UPDN 1", "UPDN 2", "UPDN 4", "PEND 1", "PEND 2", "PEND 4",
        "RAND 1", "RAND 2", "RAND 4"
    };
    static const char *const pat[16] = {
        "PAT 1", "PAT 2", "PAT 3", "PAT 4", "PAT 5", "PAT 6", "PAT 7", "PAT 8",
        "PAT 9", "PAT 10", "PAT 11", "PAT 12", "PAT 13", "PAT 14", "PAT 15", "PAT 16"
    };
    if (t < 15U) return names[t];
    if (t < ORCH_PERF_ARP_TYPES) return pat[t - 15U];
    return "?";
}

/* Its ten divisions, longest first (24 ticks a quarter). */
static const char *div_name(unsigned d)
{
    static const char *const names[ORCH_PERF_DIVISIONS] = {
        "1/4.", "1/4", "1/8.", "1/4T", "1/8", "1/16.", "1/8T", "1/16", "1/16T", "1/32"
    };
    return d < ORCH_PERF_DIVISIONS ? names[d] : "?";
}

static const char *wave_name(unsigned w)
{
    static const char *const names[5] = { "SINE", "TRI", "SAW", "SQUARE", "S+H" };
    return w < 5U ? names[w] : "?";
}

/* The LFO's destinations as one choice each, in the order the knob walks them. */
static const uint8_t lfo_dest_set[8] = {
    0U, ORCH_LFO_PITCH, ORCH_LFO_TREMOLO, ORCH_LFO_BRIGHTNESS,
    ORCH_LFO_PITCH | ORCH_LFO_TREMOLO, ORCH_LFO_PITCH | ORCH_LFO_BRIGHTNESS,
    ORCH_LFO_TREMOLO | ORCH_LFO_BRIGHTNESS,
    ORCH_LFO_PITCH | ORCH_LFO_TREMOLO | ORCH_LFO_BRIGHTNESS
};
static const char *lfo_dest_name(unsigned d)
{
    static const char *const names[8] = {
        "OFF", "PITCH", "TREM", "BRIGHT", "P+T", "P+B", "T+B", "ALL"
    };
    for (unsigned i = 0; i < 8U; ++i)
        if (lfo_dest_set[i] == d) return names[i];
    return "?";
}

static const char *art_name(unsigned a)
{
    static const char *const names[ORCH_ART_COUNT] = {
        "AUTO", "PORTA", "LEGATO", "DETACHE", "MARTELE", "STACC", "SPICC",
        "PIZZ", "FLAGEO", "TASTO", "PONTI", "LEGNO"
    };
    return a < ORCH_ART_COUNT ? names[a] : "?";
}

/* No libc in the firmware build. */
static void ui_fill(void *ptr, uint8_t value, unsigned bytes)
{
    uint8_t *p = (uint8_t *)ptr;
    for (unsigned i = 0; i < bytes; ++i) p[i] = value;
}

static const char *scale_name(unsigned s)
{
    static const char *const names[SCALE_COUNT] = { "OFF", "MAJOR", "MINOR", "PENTA" };
    return s < SCALE_COUNT ? names[s] : "?";
}

/* The part's ARP/phrase settings live in the performance engine; the panel
 * reads them back and writes a whole changed copy. */
static struct orch_perf_config perf_of(unsigned part)
{
    struct orch_perf_config pc;
    orch_perf_default_config(&pc);
    (void)orch_perf_get_config(part, &pc);
    return pc;
}

static void perf_put(unsigned part, const struct orch_perf_config *pc)
{
    (void)orch_perf_set_config(part, pc);
}

#define ARP_BANKS 5U
static const char *arp_bank_name(unsigned b)
{
    static const char *const names[ARP_BANKS] = { "PLAY", "FEEL", "PHRASE", "ORDER", "GROOVE" };
    return b < ARP_BANKS ? names[b] : "?";
}

static const char *page_name(unsigned p)
{
    /* the page names follow the hall: the parts page is the stage, the global
     * page is the console at the back of it */
    static const char *const names[PAGE_COUNT] = {
        "STAGE", "ARP", "SEQUENCE", "CONSOLE", "FX", "ENVELOPE", "LFO"
    };
    return p < PAGE_COUNT ? names[p] : "?";
}

/* The panel is one translation unit, and its parts live in .inc files that
 * this file includes in order: strings and the knob cells first, then the
 * pages that use them, then the edits and the entry points. Nothing here is
 * compiled on its own. */
#include "orch_ui_font.inc"    /* the 5x7 font and text() */
#include "orch_ui_draw.inc"    /* colour, geometry, gradients, discs, arcs */
#include "orch_ui_view.inc"    /* strings, rows, cards, meters */

#include "orch_ui_picture.inc" /* drawing any picture the panel holds */
#include "orch_ui_pictures.inc" /* the four menus' pictures, and which is whose */
#include "orch_ui_strip.inc"   /* the four knobs' cells along the bottom */
#include "orch_level.inc"     /* the output the light over the boards follows */
#include "orch_ui_stage.inc"   /* the parts page and the hall */
#include "orch_ui_pages.inc"   /* the arp and sequencer pages */
#include "orch_ui_fx.inc"      /* the console and the FX page */
#include "orch_ui_shape.inc"   /* the envelope and LFO pages */
#include "orch_ui_frame.inc"   /* the title, the page switch, the repaints */
#include "orch_ui_state.inc"   /* edits, save, note map, leds, speed */
#include "orch_ui_api.inc"     /* the entry points the app calls */
