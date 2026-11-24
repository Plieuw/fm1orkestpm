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
 * The panel has no text support in the SDK, so the 5x7 font lives here and one
 * text line is drawn into a small buffer and blitted in one transfer.
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
#define TITLE_H 22U
#define BODY_Y (TITLE_H + 4U)
#define STRIP_Y 190U                  /* the knob cells, under the page body */
#define ROW_H 26U                     /* one part row on the stage page */
#define STRIP_H (SW - STRIP_Y)
#define UI_IDLE_MS 25U
#define UNITY_Q23 (1UL << 23)
#define SEQ_SLOTS 4U

enum {
    C_BG = 0x0000, C_TITLE = 0x6000, C_ROW = 0x3081, C_SEL = 0x6941,
    C_TEXT = 0xFFFF, C_DIM = 0x8410, C_METER = 0x07E0, C_ART = 0xFD20,
    C_STEP = 0x38C1, C_NOW = 0x07E0, C_REC = 0xF800,
    /* the hall: burgundy curtains, a warm wooden floor and a warm stone shell.
     * The rows, the sequencer cells and the knob discs take the wood's own
     * tint instead of the old blue-greys, so every page sits in the same room.
     * Text stays white or mid grey on them, so nothing loses contrast. */
    C_CURTAIN = 0x8800, C_WOOD = 0x8200, C_SHELL = 0x39A6
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

/* --- font ----------------------------------------------------------------- */

static const char glyph_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -./:";

/* One byte per row, top row first; bit 4 is the leftmost column. */
static const uint8_t glyph_rows[][7] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, /* B */
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, /* C */
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, /* D */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, /* E */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, /* F */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* G */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* H */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* I */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, /* J */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, /* L */
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, /* N */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, /* P */
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, /* Q */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* U */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* V */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}, /* W */
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, /* X */
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, /* Y */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, /* Z */
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, /* 0 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, /* 2 */
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, /* 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, /* 4 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, /* 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, /* 6 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, /* 8 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, /* 9 */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, /* - */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}, /* . */
    {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}, /* / */
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}  /* : */
};

static const uint8_t *glyph(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (unsigned i = 0; glyph_chars[i]; ++i)
        if (glyph_chars[i] == c) return glyph_rows[i];
    return glyph_rows[36];
}

static void text(unsigned x, unsigned y, const char *s, uint16_t fg, uint16_t bg,
                 unsigned width)
{
    if (x >= SW || y > SW - 8U) return;
    if (width > SW - x) width = SW - x;
    if (!width) return;

    for (unsigned i = 0; i < width * 8U; ++i) text_buf[i] = bg;
    unsigned cx = 0;
    for (; *s; ++s) {
        if (cx + 6U > width) break;
        const uint8_t *g = glyph(*s);
        for (unsigned row = 0; row < 7U; ++row) {
            const unsigned bits = g[row];
            for (unsigned col = 0; col < 5U; ++col)
                if (bits & (16U >> col)) text_buf[row * width + cx + col] = fg;
        }
        cx += 6U;
    }
    fm1_screen_blit(x, y, width, 8U, text_buf, (size_t)width * 8U);
}

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

