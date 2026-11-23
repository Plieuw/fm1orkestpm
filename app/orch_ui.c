/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FM-1 panel for the orchestral engine.
 *
 * The printed panel labels and the matrix ids come from the FM-1 board itself
 * (buttons 0..13 and encoders 0..6 as M-VAVE numbers them):
 *
 *   buttons   OCT- OCT+ FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC
 *   encoders  SELECT ALGORITHM PRESETS KNOB1 KNOB2 KNOB3 KNOB4
 *
 * and this file gives every one of them a job (see docs/PANEL.md).
 * The panel has no text support in the SDK, so the 5x7 font lives here and one
 * text line is drawn into a small buffer and blitted in one transfer.
 *
 * Nothing here runs in the audio interrupt: groove_screen_control() is the
 * control task, and the only word it shares with the renderer is the master
 * gain.
 */
#include <string.h>

#include "fm1.h"
#include "orch_engine.h"
#include "orch_part.h"
#include "orch_ui.h"

#define SW 240U
#define TITLE_H 22U
#define BODY_Y (TITLE_H + 4U)
#define UI_IDLE_MS 25U
#define UNITY_Q23 (1UL << 23)
#define SEQ_SLOTS 4U

enum {
    C_BG = 0x0000, C_TITLE = 0x001F, C_ROW = 0x1082, C_SEL = 0x2124,
    C_TEXT = 0xFFFF, C_DIM = 0x8410, C_METER = 0x07E0, C_ART = 0xFD20,
    C_STEP = 0x4208, C_NOW = 0x07E0, C_REC = 0xF800
};

/* The matrix ids, from the board's own numbering. */
enum {
    B_OCT_DOWN = 0, B_OCT_UP, B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO,
    B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_COUNT
};

/* The encoder roles, in matrix order. */
enum { E_SELECT = 0, E_ALGO, E_KNOB1, E_KNOB2, E_KNOB3, E_KNOB4, E_PRESET };

enum { PAGE_HOME = 0, PAGE_ARP, PAGE_SEQ, PAGE_GLO, PAGE_COUNT };

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
};

volatile uint32_t orch_ui_master_q23 = UNITY_Q23;

static struct {
    uint8_t page, part, seq_on, rec, seq_step, scale, saved;
    int8_t transpose;
    uint16_t tempo;
    uint32_t buttons, keys;
    uint16_t knob;
    uint8_t chord_left;
    uint32_t chord_off_at;
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
    static const char *const names[PAGE_COUNT] = { "PARTS", "ARP", "SEQUENCER", "GLOBAL" };
    return p < PAGE_COUNT ? names[p] : "?";
}

/* Note names for the sequencer, C4 = 60. */
static void note_name(unsigned note, char out[4])
{
    static const char *const names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    if (note > 127U) note = 127U;
    out[0] = names[note % 12U][0];
    out[1] = names[note % 12U][1];
    out[2] = (char)('0' + (note / 12U) - 1U);
    out[3] = '\0';
}

static unsigned family_first(unsigned family)
{
    static const uint8_t first[ORCH_FAM_COUNT] = { ORCH_INST_VIOLIN,
        ORCH_INST_FLUTE, ORCH_INST_TRUMPET, ORCH_INST_MARIMBA, ORCH_INST_CHOIR };
    return family < ORCH_FAM_COUNT ? first[family] : 0U;
}

static unsigned family_count(unsigned family)
{
    static const uint8_t count[ORCH_FAM_COUNT] = { 5U, 5U, 4U, 3U, 1U };
    return family < ORCH_FAM_COUNT ? count[family] : 1U;
}

static unsigned put(char *d, unsigned n, unsigned cap, const char *s)
{
    while (*s && n + 1U < cap) d[n++] = *s++;
    d[n] = '\0';
    return n;
}

static unsigned put_num(char *d, unsigned n, unsigned cap, unsigned value,
                        unsigned width)
{
    char digits[6];
    unsigned len = 0;
    if (value > 99999U) value = 99999U;
    do { digits[len++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    while (len < width) digits[len++] = '0';
    while (len && n + 1U < cap) d[n++] = digits[--len];
    d[n] = '\0';
    return n;
}

static unsigned put_signed(char *d, unsigned n, unsigned cap, int value)
{
    if (value < 0) n = put(d, n, cap, "-");
    else n = put(d, n, cap, "+");
    if (value < 0) value = -value;
    return put_num(d, n, cap, (unsigned)value, 1U);
}

/* One label / value line of a settings page. */
static void setting(unsigned y, const char *label, const char *value, uint16_t vc)
{
    char line[42];
    unsigned n = put(line, 0, sizeof line, label);
    while (n < 10U) n = put(line, n, sizeof line, " ");
    put(line, n, sizeof line, value);
    text(8U, y, line, C_TEXT, C_BG, SW - 16U);
    /* the value in its own colour, over the label row */
    text(8U + 60U, y, value, vc, C_BG, SW - 76U);
}

static void setting_num(unsigned y, const char *label, unsigned value,
                        unsigned width, uint16_t vc)
{
    char number[8];
    char line[42];
    put_num(number, 0, sizeof number, value, width);
    unsigned n = put(line, 0, sizeof line, label);
    while (n < 10U) n = put(line, n, sizeof line, " ");
    put(line, n, sizeof line, number);
    text(8U, y, line, C_TEXT, C_BG, SW - 16U);
    text(8U + 60U, y, number, vc, C_BG, SW - 76U);
}

/* --- drawing -------------------------------------------------------------- */

static void draw_meter(unsigned p)
{
    const struct orch_part *part = orch_part_get(p);
    const unsigned ceiling = part->voice_ceiling ? part->voice_ceiling : 1U;
    const unsigned y = BODY_Y + p * 30U + 22U;
    unsigned voices = orch_voices_of_part(p);
    if (voices > ceiling) voices = ceiling;
    fm1_screen_fill(8U, y, SW - 16U, 5U, C_BG);
    if (voices)
        fm1_screen_fill(8U, y, (SW - 16U) * voices / ceiling, 5U,
                        p == ui.part ? C_METER : C_DIM);
    shown_voices[p] = (uint8_t)voices;
}

static void draw_part_row(unsigned p, uint16_t bg)
{
    const unsigned y = BODY_Y + p * 30U;
    const struct orch_part *part = orch_part_get(p);
    char line[40];
    unsigned n;

    fm1_screen_fill(0, y, SW, 30U, bg);
    shown_voices[p] = 0xFFU;

    n = put(line, 0, sizeof line, "P");
    n = put_num(line, n, sizeof line, p + 1U, 1U);
    n = put(line, n, sizeof line, " ");
    n = put(line, n, sizeof line, orch_family_name(part->family));
    while (n < 26U) n = put(line, n, sizeof line, " ");
    n = put_num(line, n, sizeof line, part->player_cap, 1U);
    put(line, n, sizeof line, "PL");
    text(8U, y + 2U, line, C_TEXT, bg, SW - 16U);

    n = put(line, 0, sizeof line, inst_short(part->instrument));
    while (n < 26U) n = put(line, n, sizeof line, " ");
    put(line, n, sizeof line, art_short(part->articulation));
    text(8U, y + 11U, line, (p == ui.part) ? C_TEXT : C_DIM, bg, SW - 16U);

    draw_meter(p);
}

static void page_parts(void)
{
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        draw_part_row(p, p == ui.part ? C_SEL : C_ROW);
    text(8U, BODY_Y + 4U * 30U + 6U,
         "B1 OCT-  B2 OCT+  B3 FX  B4 SCL", C_DIM, C_BG, SW - 16U);
    text(8U, BODY_Y + 4U * 30U + 18U,
         "B5/6 ART  B8 EDIT  B9 GLOBAL", C_DIM, C_BG, SW - 16U);
    text(8U, BODY_Y + 4U * 30U + 30U,
         "B11 SAVE  B12 ARP  B13 SEQ", C_DIM, C_BG, SW - 16U);
    text(8U, BODY_Y + 4U * 30U + 42U,
         "B14 RUN  B15 REC  KNOBS EDIT", C_DIM, C_BG, SW - 16U);
}

static void page_arp(void)
{
    char line[40];
    unsigned n = put(line, 0, sizeof line, "PART ");
    n = put_num(line, n, sizeof line, ui.part + 1U, 1U);
    n = put(line, n, sizeof line, "  ");
    put(line, n, sizeof line, orch_arp_enabled(ui.part) ? "ARP ON" : "ARP OFF");
    text(8U, BODY_Y + 4U, line, orch_arp_enabled(ui.part) ? C_METER : C_DIM,
         C_BG, SW - 16U);

    setting(BODY_Y + 22U, "MODE", arp_mode_name(orch_arp_mode_of(ui.part)), C_ART);
    setting_num(BODY_Y + 36U, "OCTAVES", orch_arp_octaves_of(ui.part), 1U, C_TEXT);
    setting(BODY_Y + 50U, "RATE", div_name(orch_arp_div_of(ui.part)), C_TEXT);
    setting_num(BODY_Y + 64U, "GATE %", orch_arp_gate_of(ui.part), 3U, C_TEXT);
    setting_num(BODY_Y + 78U, "HELD", orch_arp_held(ui.part), 1U,
                orch_arp_held(ui.part) ? C_METER : C_DIM);

    text(8U, BODY_Y + 100U, "K1 RATE  K2 OCT  K3 GATE  K4 MODE",
         C_DIM, C_BG, SW - 16U);
    text(8U, BODY_Y + 112U, "B12 TOGGLES  B10 PARTS  ENC1 PART",
         C_DIM, C_BG, SW - 16U);
}

static void page_seq(void)
{
    char line[40];
    unsigned n = put(line, 0, sizeof line, "PART ");
    n = put_num(line, n, sizeof line, ui.part + 1U, 1U);
    n = put(line, n, sizeof line, ui.seq_on ? "  RUNNING" : "  STOPPED");
    text(8U, BODY_Y + 2U, line, ui.seq_on ? C_METER : C_DIM, C_BG, SW - 16U);
    text(150U, BODY_Y + 2U, ui.rec ? "REC" : "    ", ui.rec ? C_REC : C_DIM,
         C_BG, 70U);

    /* sixteen steps: the selected one is highlighted and the playing one has
     * a green bar under it, so a running pattern is visible while it runs */
    for (unsigned i = 0; i < ORCH_SEQ_STEPS; ++i) {
        const unsigned x = 8U + i * 14U;
        const unsigned y = BODY_Y + 16U;
        const uint16_t bg = (i == ui.seq_step) ? C_SEL : C_STEP;
        fm1_screen_fill(x, y, 12U, 16U, bg);
        if (seq_notes[i][0].velocity)
            fm1_screen_fill(x + 2U, y + 11U, 8U, 3U, C_ART);
        if (i == orch_seq_position())
            fm1_screen_fill(x, y + 13U, 12U, 3U, C_NOW);
    }

    n = put(line, 0, sizeof line, "STEP ");
    n = put_num(line, n, sizeof line, ui.seq_step + 1U, 2U);
    n = put(line, n, sizeof line, "  NOTES");
    text(8U, BODY_Y + 40U, line, C_TEXT, C_BG, SW - 16U);

    char notes[40];
    unsigned len = 0U;
    for (unsigned s = 0; s < SEQ_SLOTS; ++s) {
        if (!seq_notes[ui.seq_step][s].velocity) {
            len = put(notes, len, sizeof notes, " -  ");
            continue;
        }
        char nn[4];
        note_name(seq_notes[ui.seq_step][s].note, nn);
        len = put(notes, len, sizeof notes, " ");
        len = put(notes, len, sizeof notes, nn);
        len = put(notes, len, sizeof notes, " ");
    }
    text(8U, BODY_Y + 52U, notes, C_ART, C_BG, SW - 16U);

    setting_num(BODY_Y + 68U, "VELOCITY", seq_notes[ui.seq_step][0].velocity, 3U,
                C_TEXT);
    setting_num(BODY_Y + 82U, "GATE %",
                seq_notes[ui.seq_step][0].gate_percent, 3U, C_TEXT);

    text(8U, BODY_Y + 100U, "K1 VEL  K2 GATE  K4 CLEAR STEP",
         C_DIM, C_BG, SW - 16U);
    text(8U, BODY_Y + 112U, "B15 REC: KEYS WRITE A STEP", C_DIM, C_BG, SW - 16U);
}

static void page_global(void)
{
    setting_num(BODY_Y + 4U, "TEMPO", ui.tempo, 3U, C_TEXT);

    char number[8];
    put_signed(number, 0, sizeof number, ui.transpose);
    setting(BODY_Y + 18U, "TRANSPOSE", number, C_TEXT);
    setting(BODY_Y + 32U, "SCALE", scale_name(ui.scale), C_ART);
    setting_num(BODY_Y + 46U, "VOLUME", (unsigned)((ui.knob * 100U) / 1023U), 2U,
                C_TEXT);
    setting(BODY_Y + 60U, "SAVE", ui.saved ? "DONE" : "B11 TO SAVE",
            ui.saved ? C_METER : C_DIM);

    text(8U, BODY_Y + 84U, "FX: DIVISI 1<->4  SCL: SCALE",
         C_DIM, C_BG, SW - 16U);
    text(8U, BODY_Y + 96U, "OCT-/+: TRANSPOSE  ART: ENV/LFO",
         C_DIM, C_BG, SW - 16U);
    text(8U, BODY_Y + 108U, "K4: PANIC (ALL NOTES OFF)", C_DIM, C_BG, SW - 16U);
}

static void draw_title(void)
{
    char line[40];
    fm1_screen_fill(0, 0, SW, TITLE_H, C_TITLE);
    unsigned n = put(line, 0, sizeof line, page_name(ui.page));
    while (n < 12U) n = put(line, n, sizeof line, " ");
    n = put(line, n, sizeof line, "P");
    n = put_num(line, n, sizeof line, ui.part + 1U, 1U);
    while (n < 22U) n = put(line, n, sizeof line, " ");
    n = put_num(line, n, sizeof line, ui.tempo, 3U);
    put(line, n, sizeof line, " BPM");
    text(6U, 7U, line, C_TEXT, C_TITLE, SW - 12U);
}

static void draw_page(void)
{
    fm1_screen_fill(0, TITLE_H, SW, SW - TITLE_H, C_BG);
    switch (ui.page) {
    case PAGE_ARP: page_arp(); break;
    case PAGE_SEQ: page_seq(); break;
    case PAGE_GLO: page_global(); break;
    case PAGE_HOME:
    default: page_parts(); break;
    }
}

/* The pages show live values (a running step, held arp notes). The control task
 * compares what it drew with what the engine says, and repaints on a change. */
static uint32_t page_signature(void)
{
    uint32_t sig = (uint32_t)ui.page * 0x9E3779B1UL;
    switch (ui.page) {
    case PAGE_ARP:
        sig ^= (uint32_t)orch_arp_held(ui.part) * 31U;
        sig ^= (uint32_t)orch_arp_enabled(ui.part) << 8;
        sig ^= (uint32_t)orch_arp_mode_of(ui.part) << 12;
        sig ^= (uint32_t)orch_arp_octaves_of(ui.part) << 16;
        sig ^= (uint32_t)orch_arp_div_of(ui.part) << 20;
        sig ^= (uint32_t)orch_arp_gate_of(ui.part) << 24;
        break;
    case PAGE_SEQ:
        sig ^= (uint32_t)orch_seq_position() << 1;
        sig ^= (uint32_t)ui.seq_on << 9;
        sig ^= (uint32_t)ui.rec << 10;
        sig ^= (uint32_t)ui.seq_step << 12;
        break;
    default:
        break;
    }
    return sig;
}

static void update_meters(void)
{
    if (ui.page != PAGE_HOME) return;
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const struct orch_part *part = orch_part_get(p);
        const unsigned ceiling = part->voice_ceiling ? part->voice_ceiling : 1U;
        unsigned voices = orch_voices_of_part(p);
        if (voices > ceiling) voices = ceiling;
        if (voices != shown_voices[p]) draw_meter(p);
    }
}

/* --- edits ---------------------------------------------------------------- */

static void set_family(unsigned family)
{
    struct orch_part *part = orch_part_get(ui.part);
    if (!part || family >= ORCH_FAM_COUNT) return;
    orch_engine_configure_part(ui.part, family, part->channels);
}

static void step_instrument(int delta)
{
    const struct orch_part *part = orch_part_get(ui.part);
    if (!part || !delta) return;
    const unsigned first = family_first(part->family);
    const unsigned count = family_count(part->family);
    int index = (int)(part->instrument - first) + delta;
    while (index < 0) index += (int)count;
    while (index >= (int)count) index -= (int)count;
    orch_engine_set_instrument(ui.part, first + (unsigned)index);
}

static void step_art(int delta)
{
    const struct orch_part *part = orch_part_get(ui.part);
    if (!part || !delta) return;
    int art = (int)part->articulation + delta;
    while (art < 0) art += (int)ORCH_ART_COUNT;
    while (art >= (int)ORCH_ART_COUNT) art -= (int)ORCH_ART_COUNT;
    orch_engine_set_articulation(ui.part, (unsigned)art);
}

static void play_chord(void)
{
    static const uint8_t notes[ORCH_PARTS] = { 48U, 60U, 64U, 67U };
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        orch_note_event(p * 4U, notes[p], 100U);
    ui.chord_left = ORCH_PARTS;
    ui.chord_off_at = fm1_millis() + 900U;
}

static void release_chord(void)
{
    static const uint8_t notes[ORCH_PARTS] = { 48U, 60U, 64U, 67U };
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        orch_note_event(p * 4U, notes[p], 0U);
    ui.chord_left = 0U;
}

/* --- sequencer editing ---------------------------------------------------- */

static void seq_apply_step(unsigned step)
{
    for (unsigned s = 0; s < SEQ_SLOTS; ++s)
        orch_seq_set_step(step, s, &seq_notes[step][s]);
}

static void seq_write_note(unsigned note)
{
    unsigned slot = SEQ_SLOTS;
    for (unsigned s = 0; s < SEQ_SLOTS; ++s)
        if (!seq_notes[ui.seq_step][s].velocity) { slot = s; break; }
    if (slot >= SEQ_SLOTS) slot = 0U;   /* all four taken: replace the first */
    seq_notes[ui.seq_step][slot].note = (uint8_t)note;
    seq_notes[ui.seq_step][slot].velocity = 100U;
    seq_notes[ui.seq_step][slot].gate_percent = 80U;
    seq_apply_step(ui.seq_step);
    ui.seq_step = (uint8_t)((ui.seq_step + 1U) % ORCH_SEQ_STEPS);
}

static void seq_clear_step(void)
{
    for (unsigned s = 0; s < SEQ_SLOTS; ++s) {
        seq_notes[ui.seq_step][s].note = 0U;
        seq_notes[ui.seq_step][s].velocity = 0U;
        seq_notes[ui.seq_step][s].gate_percent = 0U;
    }
    seq_apply_step(ui.seq_step);
}

static void seq_touch_slot(unsigned slot_delta, int note_delta, int vel_delta,
                           int gate_delta)
{
    struct orch_step_note *s = &seq_notes[ui.seq_step][0];
    (void)slot_delta;
    if (!s->velocity) {
        s->note = 60U;
        s->velocity = 100U;
        s->gate_percent = 80U;
    }
    if (note_delta) {
        int note = (int)s->note + note_delta;
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        s->note = (uint8_t)note;
    }
    if (vel_delta) {
        int vel = (int)s->velocity + vel_delta;
        if (vel < 1) vel = 1;
        if (vel > 127) vel = 127;
        s->velocity = (uint8_t)vel;
    }
    if (gate_delta) {
        int gate = (int)s->gate_percent + gate_delta;
        if (gate < 5) gate = 5;
        if (gate > 100) gate = 100;
        s->gate_percent = (uint8_t)gate;
    }
    seq_apply_step(ui.seq_step);
}

/* --- save ----------------------------------------------------------------- */

static uint32_t record_check(const struct save_record *r)
{
    const uint8_t *b = (const uint8_t *)r;
    uint32_t sum = 2166136261UL;
    for (unsigned i = 0; i < sizeof *r - sizeof r->check; ++i) {
        sum ^= b[i];
        sum *= 16777619UL;
    }
    return sum;
}

static void save_state(void)
{
    struct save_record r;
    memset(&r, 0, sizeof r);
    r.magic = SAVE_MAGIC;
    r.tempo = ui.tempo;
    r.transpose = ui.transpose;
    r.scale = ui.scale;
    r.seq_on = ui.seq_on;
    r.part = ui.part;
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const struct orch_part *part = orch_part_get(p);
        r.family[p] = part->family;
        r.instrument[p] = part->instrument;
        r.articulation[p] = part->articulation;
        r.players[p] = part->player_cap;
        r.voices[p] = part->voice_ceiling;
        r.arp_on[p] = (uint8_t)orch_arp_enabled(p);
        r.arp_mode[p] = (uint8_t)orch_arp_mode_of(p);
        r.arp_oct[p] = (uint8_t)orch_arp_octaves_of(p);
        r.arp_div[p] = (uint8_t)orch_arp_div_of(p);
        r.arp_gate[p] = (uint8_t)orch_arp_gate_of(p);
    }
    r.check = record_check(&r);

    if (fm1_flash_size() < sizeof r) return;
    if (fm1_flash_erase(0U) != FM1_FLASH_OK) return;
    if (fm1_flash_write(0U, &r, sizeof r) != FM1_FLASH_OK) return;
    ui.saved = 1U;
}

static void load_state(void)
{
    struct save_record r;
    if (fm1_flash_size() < sizeof r) return;
    if (fm1_flash_read(0U, &r, sizeof r) != FM1_FLASH_OK) return;
    if (r.magic != SAVE_MAGIC || r.check != record_check(&r)) return;
    if (r.tempo < 30U || r.tempo > 300U) return;

    ui.tempo = r.tempo;
    ui.transpose = r.transpose;
    ui.scale = r.scale < SCALE_COUNT ? r.scale : SCALE_OFF;
    ui.part = r.part < ORCH_PARTS ? r.part : 0U;
    orch_seq_set_tempo(ui.tempo);
    ui.seq_on = r.seq_on ? 1U : 0U;
    orch_seq_set_enabled(ui.seq_on);
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        if (r.family[p] >= ORCH_FAM_COUNT) continue;
        orch_engine_configure_part(p, r.family[p], orch_part_get(p)->channels);
        if (r.instrument[p] < ORCH_INST_COUNT)
            orch_engine_set_instrument(p, r.instrument[p]);
        if (r.articulation[p] < ORCH_ART_COUNT)
            orch_engine_set_articulation(p, r.articulation[p]);
        orch_engine_set_player_cap(p, r.players[p]);
        orch_engine_set_voice_ceiling(p, r.voices[p]);
        orch_arp_set_enabled(p, r.arp_on[p]);
        orch_arp_set_mode(p, r.arp_mode[p]);
        orch_arp_set_octaves(p, r.arp_oct[p]);
        orch_arp_set_division(p, r.arp_div[p]);
        orch_arp_set_gate(p, r.arp_gate[p]);
    }
    ui.saved = 1U;
}

/* --- note mapping --------------------------------------------------------- */

int orch_ui_map_note(int note)
{
    int out = note + (int)ui.transpose;
    if (ui.scale != SCALE_OFF) {
        static const uint8_t major[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const uint8_t minor[7] = { 0, 2, 3, 5, 7, 8, 10 };
        static const uint8_t penta[5] = { 0, 2, 4, 7, 9 };
        const uint8_t *set = major;
        unsigned len = 7U;
        if (ui.scale == SCALE_MINOR) { set = minor; len = 7U; }
        else if (ui.scale == SCALE_PENTA) { set = penta; len = 5U; }
        int pc = out % 12;
        if (pc < 0) pc += 12;
        int snapped = 0;
        for (unsigned i = 0; i < len; ++i) {
            if (set[i] <= pc) snapped = set[i];
            else break;
        }
        out += snapped - pc;
    }
    if (out < 0 || out > 127) return -1;
    return out;
}

/* --- leds ----------------------------------------------------------------- */

static void update_leds(void)
{
    for (unsigned b = 0; b < 4U; ++b) fm1_led_set(b, b == ui.part);
    const struct orch_part *part = orch_part_get(ui.part);
    const unsigned family = part ? part->family : 0U;
    for (unsigned b = 4U; b < 9U; ++b) fm1_led_set(b, b - 4U == family);
    for (unsigned b = 9U; b < 14U; ++b)
        fm1_led_set(b, (ui.buttons >> b) & 1U);
    fm1_led_set(B_ARP, orch_arp_enabled(ui.part));
    fm1_led_set(B_PLAY, ui.seq_on);
    fm1_led_set(B_REC, ui.rec);
    fm1_led_set(B_SAVE, ui.saved);
    for (unsigned k = 0; k < 27U; ++k)
        fm1_led_set(14U + k, (ui.keys >> k) & 1U);
}

/* --- entry points --------------------------------------------------------- */

void orch_ui_init(void)
{
    ui.page = PAGE_HOME;
    ui.part = 0U;
    ui.tempo = 120U;
    ui.seq_on = 0U;
    ui.rec = 0U;
    ui.seq_step = 0U;
    ui.scale = SCALE_OFF;
    ui.transpose = 0;
    ui.saved = 0U;
    ui.buttons = 0U;
    ui.keys = 0U;
    ui.knob = 0U;
    ui.chord_left = 0U;
    ui.chord_off_at = 0U;
    orch_ui_master_q23 = 0U;   /* silent until the knob says otherwise */
    next_scan = 0U;
    memset(shown_voices, 0xFF, sizeof shown_voices);
    memset(seq_notes, 0, sizeof seq_notes);

    fm1_controls_init();
    fm1_screen_init();
    fm1_screen_fill(0, 0, SW, SW, C_BG);
    load_state();
    draw_title();
    draw_page();
    drawn_signature = page_signature();
    update_leds();
}

void orch_ui_handle(const struct fm1_controls *controls)
{
    if (!controls) return;

    const uint32_t pressed = controls->buttons & ~ui.buttons;
    const uint32_t key_down = controls->keys & ~ui.keys;
    int dirty = 0;

    ui.buttons = controls->buttons;
    ui.keys = controls->keys;

    if (ui.rec && ui.page == PAGE_SEQ) {
        for (unsigned k = 0; k < 27U; ++k)
            if (key_down & (1UL << k)) { seq_write_note(53U + k); dirty = 1; }
    }

    if (pressed & (1UL << B_OCT_DOWN)) {
        if (ui.transpose > -24) ui.transpose = (int8_t)(ui.transpose - 12);
        dirty = 1;
    }
    if (pressed & (1UL << B_OCT_UP)) {
        if (ui.transpose < 24) ui.transpose = (int8_t)(ui.transpose + 12);
        dirty = 1;
    }
    if (pressed & (1UL << B_FX)) { play_chord(); dirty = 1; }
    if (pressed & (1UL << B_SCL)) {
        ui.scale = (uint8_t)((ui.scale + 1U) % SCALE_COUNT);
        dirty = 1;
    }
    if (pressed & (1UL << B_ENV)) { step_art(-1); dirty = 1; }
    if (pressed & (1UL << B_LFO)) { step_art(1); dirty = 1; }
    if (pressed & (1UL << B_EDIT)) { ui.page = PAGE_HOME; dirty = 1; }
    if (pressed & (1UL << B_GLO)) { ui.page = PAGE_GLO; dirty = 1; }
    if (pressed & (1UL << B_HOME)) { ui.page = PAGE_HOME; dirty = 1; }
    if (pressed & (1UL << B_SAVE)) { save_state(); dirty = 1; }
    if (pressed & (1UL << B_ARP)) {
        ui.page = PAGE_ARP;
        orch_arp_set_enabled(ui.part, !orch_arp_enabled(ui.part));
        dirty = 1;
    }
    if (pressed & (1UL << B_SEQ)) { ui.page = PAGE_SEQ; dirty = 1; }
    if (pressed & (1UL << B_PLAY)) {
        ui.seq_on = (uint8_t)!ui.seq_on;
        orch_seq_set_enabled(ui.seq_on);
        orch_seq_set_part(ui.part);
        dirty = 1;
    }
    if (pressed & (1UL << B_REC)) { ui.rec = (uint8_t)!ui.rec; dirty = 1; }

    for (unsigned e = 0; e < 7U; ++e) {
        const int delta = controls->encoders[e];
        struct orch_part *part = orch_part_get(ui.part);
        if (!part || !delta) continue;
        switch (e) {
        case E_SELECT: {
            int p = (int)ui.part + delta;
            while (p < 0) p += (int)ORCH_PARTS;
            while (p >= (int)ORCH_PARTS) p -= (int)ORCH_PARTS;
            ui.part = (uint8_t)p;
            dirty = 1;
            break;
        }
        case E_PRESET:
            step_instrument(delta);
            dirty = 1;
            break;
        case E_ALGO:
            switch (ui.page) {
            case PAGE_ARP: {
                int m = (int)orch_arp_mode_of(ui.part) + delta;
                while (m < 0) m += (int)ORCH_ARP_MODE_COUNT;
                while (m >= (int)ORCH_ARP_MODE_COUNT) m -= (int)ORCH_ARP_MODE_COUNT;
                orch_arp_set_mode(ui.part, (unsigned)m);
                break;
            }
            case PAGE_SEQ: {
                int s = (int)ui.seq_step + delta;
                while (s < 0) s += (int)ORCH_SEQ_STEPS;
                while (s >= (int)ORCH_SEQ_STEPS) s -= (int)ORCH_SEQ_STEPS;
                ui.seq_step = (uint8_t)s;
                break;
            }
            case PAGE_GLO: {
                int sc = (int)ui.scale + delta;
                while (sc < 0) sc += SCALE_COUNT;
                while (sc >= SCALE_COUNT) sc -= SCALE_COUNT;
                ui.scale = (uint8_t)sc;
                break;
            }
            default: {
                int f = (int)part->family + delta;
                while (f < 0) f += (int)ORCH_FAM_COUNT;
                while (f >= (int)ORCH_FAM_COUNT) f -= (int)ORCH_FAM_COUNT;
                set_family((unsigned)f);
                break;
            }
            }
            dirty = 1;
            break;
        case E_KNOB1:
            if (ui.page == PAGE_ARP) {
                int d = (int)orch_arp_div_of(ui.part) + delta;
                if (d < 0) d = 0;
                if (d > 4) d = 4;
                orch_arp_set_division(ui.part, (unsigned)d);
            } else if (ui.page == PAGE_SEQ) {
                seq_touch_slot(0U, 0, delta * 4, 0);
            } else {
                int cap = (int)part->player_cap + delta;
                if (cap < 1) cap = 1;
                if (cap > 4) cap = 4;
                orch_engine_set_player_cap(ui.part, (unsigned)cap);
            }
            dirty = 1;
            break;
        case E_KNOB2:
            if (ui.page == PAGE_ARP) {
                int o = (int)orch_arp_octaves_of(ui.part) + delta;
                if (o < 1) o = 1;
                if (o > 3) o = 3;
                orch_arp_set_octaves(ui.part, (unsigned)o);
            } else if (ui.page == PAGE_SEQ) {
                seq_touch_slot(0U, 0, 0, delta * 5);
            } else if (ui.page == PAGE_GLO) {
                int t = (int)ui.transpose + delta;
                if (t < -24) t = -24;
                if (t > 24) t = 24;
                ui.transpose = (int8_t)t;
            } else {
                int c = (int)part->voice_ceiling + delta;
                if (c < 1) c = 1;
                if (c > (int)ORCH_PART_VOICE_CEIL) c = (int)ORCH_PART_VOICE_CEIL;
                orch_engine_set_voice_ceiling(ui.part, (unsigned)c);
            }
            dirty = 1;
            break;
        case E_KNOB3:
            if (ui.page == PAGE_ARP) {
                int g = (int)orch_arp_gate_of(ui.part) + delta * 5;
                orch_arp_set_gate(ui.part, (unsigned)g);
            } else {
                int bpm = (int)ui.tempo + delta * 4;
                if (bpm < 30) bpm = 30;
                if (bpm > 300) bpm = 300;
                ui.tempo = (uint16_t)bpm;
                orch_seq_set_tempo(ui.tempo);
            }
            dirty = 1;
            break;
        case E_KNOB4:
            if (ui.page == PAGE_SEQ) seq_clear_step();
            else if (ui.page == PAGE_GLO) orch_panic();
            else if (ui.page == PAGE_ARP) {
                int m = (int)orch_arp_mode_of(ui.part) + delta;
                while (m < 0) m += (int)ORCH_ARP_MODE_COUNT;
                while (m >= (int)ORCH_ARP_MODE_COUNT) m -= (int)ORCH_ARP_MODE_COUNT;
                orch_arp_set_mode(ui.part, (unsigned)m);
            } else {
                int t = (int)ui.transpose + delta;
                if (t < -24) t = -24;
                if (t > 24) t = 24;
                ui.transpose = (int8_t)t;
            }
            dirty = 1;
            break;
        default: break;
        }
    }

    /* the knob is the master level, independent of the page */
    if (ui.knob != controls->knob) {
        const uint32_t k = controls->knob;
        const uint32_t full = 1023U * 1023U;
        ui.knob = (uint16_t)k;
        orch_ui_master_q23 = (uint32_t)(((uint64_t)k * k * UNITY_Q23) / full);
    }

    if (dirty) {
        draw_title();
        draw_page();
        drawn_signature = page_signature();
    }
    update_leds();
}

void orch_ui_control(void)
{
    const uint32_t now = fm1_millis();
    if ((uint32_t)(now - next_scan) < UI_IDLE_MS) return;
    next_scan = now;

    struct fm1_controls controls;
    fm1_controls_read(&controls);
    orch_ui_handle(&controls);

    if (ui.chord_left && (int32_t)(now - ui.chord_off_at) >= 0) release_chord();
    update_meters();

    const uint32_t sig = page_signature();
    if (sig != drawn_signature) {
        drawn_signature = sig;
        draw_page();
    }
}

unsigned orch_ui_part(void) { return ui.part; }
unsigned orch_ui_tempo(void) { return ui.tempo; }
unsigned orch_ui_page(void) { return ui.page; }
unsigned orch_ui_transpose(void) { return (unsigned)(int)ui.transpose; }
unsigned orch_ui_scale(void) { return ui.scale; }
unsigned orch_ui_recording(void) { return ui.rec; }
unsigned orch_ui_seq_step(void) { return ui.seq_step; }
unsigned orch_ui_saved(void) { return ui.saved; }
