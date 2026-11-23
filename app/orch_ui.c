/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FM-1 panel for the orchestral engine.
 *
 * The board gives a 240x240 RGB565 panel with no text support, so this file
 * carries its own 5x7 font and draws one text line into a small buffer, then
 * blits it in a single SPI transfer. Nothing here runs in the audio
 * interrupt: groove_screen_control() is the control task.
 *
 * Panel map
 *   buttons 1..4   select part
 *   buttons 5..9   family (strings, woodwinds, brass, mallets, choir)
 *   button 10/11   articulation down / up
 *   button 12      sequencer run / stop
 *   button 13      panic (all notes off)
 *   button 14      play a four-part C major chord
 *   encoder 1      instrument inside the family
 *   encoder 2      divisi players
 *   encoder 3      voices this part may hold
 *   encoder 4      articulation
 *   encoder 5      tempo
 *   encoder 6      part
 *   encoder 7      family
 *   knob (ADC)     master volume
 *
 * Key notes reach the engine through groove_midi_note(); this file only
 * mirrors the key matrix on the LEDs, so a note is never counted twice.
 */
#include <string.h>

#include "fm1.h"
#include "orch_engine.h"
#include "orch_part.h"
#include "orch_ui.h"

#define SW 240U
#define ROW_Y 26U
#define ROW_H 34U
#define FOOT_Y (ROW_Y + ORCH_PARTS * ROW_H)
#define UI_IDLE_MS 25U
#define UNITY_Q23 (1UL << 23)

enum {
    C_BG = 0x0000, C_TITLE = 0x001F, C_ROW = 0x1082, C_SEL = 0x2124,
    C_TEXT = 0xFFFF, C_DIM = 0x8410, C_METER = 0x07E0, C_ART = 0xFD20
};

volatile uint32_t orch_ui_master_q23 = UNITY_Q23;

static struct {
    uint8_t part, seq_on;
    uint16_t tempo;
    uint32_t buttons, keys;
    uint16_t knob;
    uint8_t chord_left;
    uint32_t chord_off_at;
} ui;

static uint16_t text_buf[SW * 8U];
static uint8_t shown_voices[ORCH_PARTS];
static uint32_t next_scan;

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
    return glyph_rows[36]; /* space */
}

/* Draw one 8-pixel-high line over `width` pixels, background included, so the
 * call also erases what was there before. */
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

/* Append text, never past `cap` (returns the new length). */
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

/* --- drawing -------------------------------------------------------------- */

/* Voices as a meter: how much of this part's ceiling is sounding. Redrawing
 * only this strip keeps a sounding note visible without re-sending the text. */
static void draw_meter(unsigned p)
{
    const struct orch_part *part = orch_part_get(p);
    const unsigned ceiling = part->voice_ceiling ? part->voice_ceiling : 1U;
    const unsigned y = ROW_Y + p * ROW_H + 24U;
    unsigned voices = orch_voices_of_part(p);
    if (voices > ceiling) voices = ceiling;
    fm1_screen_fill(6U, y, SW - 12U, 5U, C_BG);
    if (voices)
        fm1_screen_fill(6U, y, (SW - 12U) * voices / ceiling, 5U,
                        p == ui.part ? C_METER : C_DIM);
    shown_voices[p] = (uint8_t)voices;
}

static void update_meters(void)
{
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const struct orch_part *part = orch_part_get(p);
        unsigned voices = orch_voices_of_part(p);
        const unsigned ceiling = part->voice_ceiling ? part->voice_ceiling : 1U;
        if (voices > ceiling) voices = ceiling;
        if (voices != shown_voices[p]) draw_meter(p);
    }
}

static void draw_row(unsigned p, uint16_t bg)
{
    const unsigned y = ROW_Y + p * ROW_H;
    const struct orch_part *part = orch_part_get(p);
    char line[40];
    unsigned n;

    fm1_screen_fill(0, y, SW, ROW_H, bg);
    shown_voices[p] = 0xFFU;

    n = put(line, 0, sizeof line, "P");
    n = put_num(line, n, sizeof line, p + 1U, 1U);
    n = put(line, n, sizeof line, " ");
    n = put(line, n, sizeof line, orch_family_name(part->family));
    while (n < 26U) n = put(line, n, sizeof line, " ");
    n = put_num(line, n, sizeof line, part->player_cap, 1U);
    put(line, n, sizeof line, "PL");
    text(6U, y + 2U, line, C_TEXT, bg, SW - 12U);

    n = put(line, 0, sizeof line, inst_short(part->instrument));
    while (n < 26U) n = put(line, n, sizeof line, " ");
    put(line, n, sizeof line, art_short(part->articulation));
    text(6U, y + 12U, line, (p == ui.part) ? C_TEXT : C_DIM, bg, SW - 12U);

    draw_meter(p);
}

static void draw_footer(void)
{
    char line[40];
    unsigned n;

    fm1_screen_fill(0, FOOT_Y, SW, SW - FOOT_Y, C_BG);

    n = put(line, 0, sizeof line, "PART ");
    n = put_num(line, n, sizeof line, ui.part + 1U, 1U);
    n = put(line, n, sizeof line, "  TEMPO ");
    n = put_num(line, n, sizeof line, ui.tempo, 3U);
    n = put(line, n, sizeof line, ui.seq_on ? "  RUN" : "  STOP");
    text(6U, FOOT_Y + 6U, line, C_TEXT, C_BG, SW - 12U);

    n = put(line, 0, sizeof line, "VOL ");
    n = put_num(line, n, sizeof line, (unsigned)((ui.knob * 100U) / 1023U), 2U);
    n = put(line, n, sizeof line, "  ART ");
    n = put(line, n, sizeof line, art_short(orch_part_get(ui.part)->articulation));
    text(6U, FOOT_Y + 18U, line, C_ART, C_BG, SW - 12U);

    text(6U, FOOT_Y + 32U, "B1-4 PART  B5-9 FAMILY", C_DIM, C_BG, SW - 12U);
    text(6U, FOOT_Y + 42U, "B10/11 ART  B12 RUN B13 PANIC", C_DIM, C_BG, SW - 12U);
    text(6U, FOOT_Y + 52U, "B14 CHORD  ENC1-7 EDIT", C_DIM, C_BG, SW - 12U);
}

static void draw_title(void)
{
    fm1_screen_fill(0, 0, SW, ROW_Y, C_TITLE);
    text(6U, 8U, "FM1 ORCHESTRA", C_TEXT, C_TITLE, SW - 12U);
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

static void apply_knob(void)
{
    const uint32_t k = ui.knob;
    const uint32_t full = 1023U * 1023U;
    orch_ui_master_q23 = (uint32_t)(((uint64_t)k * k * UNITY_Q23) / full);
}

static void update_leds(void)
{
    for (unsigned b = 0; b < 4U; ++b) fm1_led_set(b, b == ui.part);
    const struct orch_part *part = orch_part_get(ui.part);
    const unsigned family = part ? part->family : 0U;
    for (unsigned b = 4U; b < 9U; ++b) fm1_led_set(b, b - 4U == family);
    for (unsigned b = 9U; b < 14U; ++b)
        fm1_led_set(b, (ui.buttons >> b) & 1U);
    for (unsigned k = 0; k < 27U; ++k)
        fm1_led_set(14U + k, (ui.keys >> k) & 1U);
}

/* --- entry points --------------------------------------------------------- */

void orch_ui_init(void)
{
    ui.part = 0U;
    ui.tempo = 120U;
    ui.seq_on = 0U;
    ui.buttons = 0U;
    ui.keys = 0U;
    ui.knob = 0U;
    ui.chord_left = 0U;
    ui.chord_off_at = 0U;
    orch_ui_master_q23 = 0U;   /* silent until the knob says otherwise */
    next_scan = 0U;
    memset(shown_voices, 0xFF, sizeof shown_voices);

    fm1_controls_init();
    fm1_screen_init();
    fm1_screen_fill(0, 0, SW, SW, C_BG);
    draw_title();
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        draw_row(p, p == ui.part ? C_SEL : C_ROW);
    draw_footer();
    update_leds();
}

void orch_ui_handle(const struct fm1_controls *controls)
{
    if (!controls) return;

    const uint32_t pressed = controls->buttons & ~ui.buttons;
    uint8_t dirty_part = 0xFFU;
    ui.buttons = controls->buttons;
    ui.keys = controls->keys;

    for (unsigned b = 0; b < ORCH_PARTS; ++b) {
        if (!(pressed & (1UL << b))) continue;
        if (ui.part != b) { ui.part = (uint8_t)b; dirty_part = (uint8_t)b; }
    }
    for (unsigned f = 0; f < ORCH_FAM_COUNT; ++f) {
        if (!(pressed & (1UL << (4U + f)))) continue;
        set_family(f);
        dirty_part = ui.part;
    }
    if (pressed & (1UL << 9)) { step_art(-1); dirty_part = ui.part; }
    if (pressed & (1UL << 10)) { step_art(1); dirty_part = ui.part; }
    if (pressed & (1UL << 11)) {
        ui.seq_on = (uint8_t)!ui.seq_on;
        orch_seq_set_enabled(ui.seq_on);
    }
    if (pressed & (1UL << 12)) orch_panic();
    if (pressed & (1UL << 13)) play_chord();

    for (unsigned e = 0; e < 7U; ++e) {
        const int delta = controls->encoders[e];
        struct orch_part *part = orch_part_get(ui.part);
        if (!part || !delta) continue;
        switch (e) {
        case 0: step_instrument(delta); dirty_part = ui.part; break;
        case 1: {
            int cap = (int)part->player_cap + delta;
            if (cap < 1) cap = 1;
            if (cap > 4) cap = 4;
            orch_engine_set_player_cap(ui.part, (unsigned)cap);
            dirty_part = ui.part;
            break;
        }
        case 2: {
            int ceiling = (int)part->voice_ceiling + delta;
            if (ceiling < 1) ceiling = 1;
            if (ceiling > (int)ORCH_PART_VOICE_CEIL)
                ceiling = (int)ORCH_PART_VOICE_CEIL;
            orch_engine_set_voice_ceiling(ui.part, (unsigned)ceiling);
            dirty_part = ui.part;
            break;
        }
        case 3: step_art(delta); dirty_part = ui.part; break;
        case 4: {
            int bpm = (int)ui.tempo + delta * 4;
            if (bpm < 30) bpm = 30;
            if (bpm > 300) bpm = 300;
            ui.tempo = (uint16_t)bpm;
            orch_seq_set_tempo((uint16_t)ui.tempo);
            break;
        }
        case 5: {
            int p = (int)ui.part + delta;
            while (p < 0) p += (int)ORCH_PARTS;
            while (p >= (int)ORCH_PARTS) p -= (int)ORCH_PARTS;
            ui.part = (uint8_t)p;
            dirty_part = ui.part;
            break;
        }
        case 6: {
            int f = (int)part->family + delta;
            while (f < 0) f += (int)ORCH_FAM_COUNT;
            while (f >= (int)ORCH_FAM_COUNT) f -= (int)ORCH_FAM_COUNT;
            set_family((unsigned)f);
            dirty_part = ui.part;
            break;
        }
        default: break;
        }
    }

    if (controls->knob != ui.knob) {
        ui.knob = controls->knob;
        apply_knob();
    }

    if (dirty_part != 0xFFU) {
        /* Re-draw the parts list and the footer, which together show every
         * value an encoder or button can change. */
        for (unsigned q = 0; q < ORCH_PARTS; ++q)
            draw_row(q, q == ui.part ? C_SEL : C_ROW);
        draw_footer();
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
}

unsigned orch_ui_part(void) { return ui.part; }
unsigned orch_ui_tempo(void) { return ui.tempo; }
