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
 * for a section that is sounding, and the same one shape for every row.
 *
 * Nothing here runs in the audio interrupt: groove_screen_control() is the
 * control task. It shares the master gain and one byte per FX setting with the
 * renderer, and nothing else.
 */
#include <stddef.h>
#include <string.h>

#include "fm1.h"
#include "orch_engine.h"
#include "orch_fx.h"
#include "orch_part.h"
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

enum { PAGE_HOME = 0, PAGE_ARP, PAGE_SEQ, PAGE_GLO, PAGE_FX, PAGE_COUNT };

enum { SCALE_OFF = 0, SCALE_MAJOR, SCALE_MINOR, SCALE_PENTA, SCALE_COUNT };

#define SAVE_MAGIC 0x4F524348UL /* "ORCH" */

struct save_record {
    uint32_t magic;
    uint16_t tempo;
    int8_t transpose;
    uint8_t scale, seq_on, part;
    uint8_t family[ORCH_PARTS], instrument[ORCH_PARTS], articulation[ORCH_PARTS];
    uint8_t players[ORCH_PARTS], voices[ORCH_PARTS];
    uint8_t arp_on[ORCH_PARTS], arp_mode[ORCH_PARTS], arp_oct[ORCH_PARTS];
    uint8_t arp_div[ORCH_PARTS], arp_gate[ORCH_PARTS];
    uint32_t check;
    /* Appended after the original checksum, so a record written by the panel
     * before the FX existed still validates and loads its first part. */
    uint8_t fx_on[ORCH_FX_SLOT_COUNT], fx_mix[ORCH_FX_SLOT_COUNT];
    uint8_t fx_a[ORCH_FX_SLOT_COUNT], fx_b[ORCH_FX_SLOT_COUNT];
    uint32_t fx_check;
    /* Same again for the stage: a record written before the listener existed
     * keeps its parts and its FX, and only the seat is taken from the panel. */
    int8_t listener_x;
    uint8_t listener_depth;
    uint32_t stage_check;
};

volatile uint32_t orch_ui_master_q23 = UNITY_Q23;

static struct {
    uint8_t page, part, seq_on, rec, seq_step, scale, saved, fx_sel;
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
static struct orch_step_note seq_notes[ORCH_SEQ_STEPS][SEQ_SLOTS];

static uint16_t text_buf[SW * 8U];
static uint8_t shown_voices[ORCH_PARTS];
static uint32_t next_scan;
static uint32_t drawn_signature;


/* --- strings -------------------------------------------------------------- */

static const char *art_short(unsigned a)
{
    static const char *const names[ORCH_ART_COUNT] = {
        "AUTO", "PORTA", "LEGATO", "DETACHE", "MARTELE", "STACC", "SPICC",
        "PIZZ", "FLAG", "TASTO", "PONT", "COLGNO"
    };
    return a < ORCH_ART_COUNT ? names[a] : "?";
}

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

static const char *arp_mode_name(unsigned m)
{
    static const char *const names[ORCH_ARP_MODE_COUNT] = {
        "UP", "DOWN", "UPDOWN", "RANDOM", "AS PLAYED"
    };
    return m < ORCH_ARP_MODE_COUNT ? names[m] : "?";
}

static const char *div_name(unsigned d)
{
    static const char *const names[5] = { "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    return d < 5U ? names[d] : "?";
}

static const char *scale_name(unsigned s)
{
    static const char *const names[SCALE_COUNT] = { "OFF", "MAJOR", "MINOR", "PENTA" };
    return s < SCALE_COUNT ? names[s] : "?";
}

static const char *page_name(unsigned p)
{
    /* the page names follow the hall: the parts page is the stage, the global
     * page is the console at the back of it */
    static const char *const names[PAGE_COUNT] = {
        "STAGE", "ARP", "SEQUENCE", "CONSOLE", "FX"
    };
    return p < PAGE_COUNT ? names[p] : "?";
}

/* The panel is one translation unit, and its parts live in .inc files that
 * this file includes in order: strings and the knob cells first, then the
 * pages that use them, then the edits and the entry points. Nothing here is
 * compiled on its own. */
#include "orch_ui_font.inc"    /* the 5x7 font and text() */
#include "orch_ui_view.inc"    /* strings, rows, cards, meters */
#include "orch_ui_strip.inc"   /* the four knobs' cells along the bottom */
#include "orch_ui_stage.inc"   /* the parts page and the hall */
#include "orch_ui_pages.inc"   /* the arp and sequencer pages */
#include "orch_ui_fx.inc"      /* the console and the FX page */
#include "orch_ui_frame.inc"   /* the title, the page switch, the repaints */
#include "orch_ui_state.inc"   /* edits, save, note map, leds, speed */
#include "orch_ui_api.inc"     /* the entry points the app calls */
