/* --- strings -------------------------------------------------------------- */

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

/* --- the knobs' cells ----------------------------------------------------- */

/* Every page gives its four knobs a job, and the cells under the body say what
 * each one is and where it stands, as HAWK CHUA's screen does: the knob, its
 * label under it and the value under that. The value lights for a second after
 * its knob turns, so the eye finds the one that moved. */
struct knob_cell {
    const char *label;
    char value[10];
    uint8_t pct, lit;
};

static struct knob_cell cells[4];

static void cell_text(unsigned i, const char *label, const char *value,
                      unsigned pct, int lit)
{
    if (i >= 4U) return;
    cells[i].label = label;
    unsigned n = 0;
    while (value[n] && n + 1U < sizeof cells[i].value) {
        cells[i].value[n] = value[n];
        ++n;
    }
    cells[i].value[n] = '\0';
    cells[i].pct = (uint8_t)(pct > 100U ? 100U : pct);
    cells[i].lit = (uint8_t)(lit != 0);
}

static void cell_number(unsigned i, const char *label, int value, int lo, int hi)
{
    char number[10];
    if (value < 0) put_signed(number, 0, sizeof number, value);
    else put_num(number, 0, sizeof number, (unsigned)value, 1U);
    unsigned pct = 0U;
    if (hi > lo) {
        const int span = hi - lo;
        const int at = value < lo ? lo : value > hi ? hi : value;
        pct = (unsigned)(((at - lo) * 100) / span);
    }
    cell_text(i, label, number, pct, 1);
}

static void page_knobs(void)
{
    const struct orch_part *part = orch_part_get(ui.part);
    switch (ui.page) {
    case PAGE_ARP:
        cell_text(0, "RATE", div_name(orch_arp_div_of(ui.part)),
                  orch_arp_div_of(ui.part) * 25U, 1);
        cell_number(1, "OCT", (int)orch_arp_octaves_of(ui.part), 1, 3);
        cell_number(2, "GATE", (int)orch_arp_gate_of(ui.part), 5, 100);
        cell_text(3, "MODE", arp_mode_name(orch_arp_mode_of(ui.part)),
                  orch_arp_mode_of(ui.part) * 25U, 1);
        break;
    case PAGE_SEQ: {
        const struct orch_step_note *note = &seq_notes[ui.seq_step][0];
        cell_number(0, "VEL", (int)note->velocity, 1, 127);
        cell_number(1, "GATE", (int)note->gate_percent, 5, 100);
        cell_number(2, "TEMPO", (int)ui.tempo, 30, 300);
        cell_text(3, "CLEAR", "STEP", 0U, 0);
        break;
    }
    case PAGE_GLO:
        cell_text(0, "SCALE", scale_name(ui.scale), ui.scale * 33U, ui.scale != 0U);
        cell_number(1, "TRANSP", ui.transpose, -24, 24);
        cell_number(2, "TEMPO", (int)ui.tempo, 30, 300);
        cell_text(3, "PANIC", "ALL OFF", 0U, 1);
        break;
    case PAGE_FX: {
        const unsigned s = ui.fx_sel < ORCH_FX_SLOT_COUNT ? ui.fx_sel : 0U;
        const int on = (int)orch_fx_enabled(s);
        cell_number(0, "MIX", (int)orch_fx_mix(s), 0, 100);
        cell_number(1, orch_fx_param_a_name(s), (int)orch_fx_param_a(s), 0, 100);
        cell_number(2, orch_fx_param_b_name(s), (int)orch_fx_param_b(s), 0, 100);
        cell_text(3, "POWER", on ? "ON" : "OFF", on ? 100U : 0U, on);
        break;
    }
    case PAGE_HOME:
    default:
        if (!part) break;
        cell_number(0, "PLAYERS", (int)part->player_cap, 1, 4);
        cell_number(1, "VOICES", (int)part->voice_ceiling, 1, ORCH_PART_VOICE_CEIL);
        cell_number(2, "LISTENER", orch_stage_listener_x(), -100, 100);
        cell_number(3, "DEPTH", (int)orch_stage_listener_depth(), 0, 100);
        break;
    }
}

/* A knob: a filled disc with a white pointer from 7:30 to 4:30, drawn with
 * rectangle fills only (there is no line primitive here). */
static const uint8_t knob_half[23] = {
    1, 4, 6, 8, 9, 9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 10, 10, 9, 9, 8, 6, 4, 1
};
/* The pointer's end for 17 positions, from the left (0) round the top to the
 * right (16), in a 23 pixel disc: x, y from its centre. */
static const int8_t knob_arm[17][2] = {
    { -6, 6 }, { -7, 4 }, { -8, 2 }, { -8, -1 }, { -7, -3 }, { -6, -5 },
    { -4, -7 }, { -2, -8 }, { 0, -8 }, { 2, -8 }, { 4, -7 }, { 6, -5 },
    { 7, -3 }, { 8, -1 }, { 8, 2 }, { 7, 4 }, { 6, 6 }
};

static void draw_knob(unsigned cx, unsigned y, unsigned pct, int lit)
{
    const uint16_t body = lit ? C_SEL : C_ROW;
    for (unsigned row = 0; row < 23U; ++row) {
        const unsigned half = knob_half[row];
        fm1_screen_fill(cx - half, y + row, half * 2U, 1U, body);
    }
    const int8_t *arm = knob_arm[(pct > 100U ? 100U : pct) * 16U / 100U];
    for (unsigned k = 3U; k <= 7U; k += 2U) {
        const int px = (int)cx + (int)arm[0] * (int)k / 8;
        const int py = (int)y + 11 + (int)arm[1] * (int)k / 8;
        fm1_screen_fill((unsigned)px, (unsigned)py, 2U, 2U, lit ? C_TEXT : C_DIM);
    }
}

static void text_center(unsigned cx, unsigned y, const char *s, uint16_t fg)
{
    unsigned n = 0;
    while (s[n]) ++n;
    if (!n) return;
    const unsigned w = n * 6U;              /* six pixels a glyph: the last needs it too */
    const unsigned x = w / 2U < cx ? cx - w / 2U : 0U;
    text(x, y, s, fg, C_BG, w);
}

static void draw_strip(void)
{
    const int hot = (int32_t)(fm1_millis() - ui.hot_until) < 0;
    /* the knob cells stand on the hall's wooden rail */
    fm1_screen_fill(0, STRIP_Y - 4U, SW, 1U, C_WOOD);
    fm1_screen_fill(0, STRIP_Y - 3U, SW, STRIP_H, C_BG);
    for (unsigned c = 0; c < 4U; ++c) {
        const struct knob_cell *k = &cells[c];
        const unsigned cx = c * 60U + 30U;
        draw_knob(cx, STRIP_Y, k->pct, k->lit);
        text_center(cx, STRIP_Y + 24U, k->label, C_DIM);
        text_center(cx, STRIP_Y + 34U, k->value,
                    (hot && ui.hot_cell == c) ? C_ART : (k->lit ? C_TEXT : C_DIM));
    }
}

/* --- drawing -------------------------------------------------------------- */

static void draw_meter(unsigned p)
{
    const struct orch_part *part = orch_part_get(p);
    const unsigned ceiling = part->voice_ceiling ? part->voice_ceiling : 1U;
    const unsigned y = BODY_Y + p * ROW_H + 20U;
    unsigned voices = orch_voices_of_part(p);
    if (voices > ceiling) voices = ceiling;
    fm1_screen_fill(8U, y, SW - 16U, 4U, C_BG);
    if (voices)
        fm1_screen_fill(8U, y, (SW - 16U) * voices / ceiling, 4U,
                        p == ui.part ? C_METER : C_DIM);
    shown_voices[p] = (uint8_t)voices;
}

static void draw_part_row(unsigned p, uint16_t bg)
{
    const unsigned y = BODY_Y + p * ROW_H;
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
    text(8U, y + 2U, line, C_TEXT, bg, SW - 16U);

    n = put(line, 0, sizeof line, inst_short(part->instrument));
    while (n < 26U) n = put(line, n, sizeof line, " ");
    put(line, n, sizeof line, art_short(part->articulation));
    text(8U, y + 11U, line, (p == ui.part) ? C_TEXT : C_DIM, bg, SW - 16U);

    draw_meter(p);
}

/* The hall under the four rows, as the listener hears it: an orchestra shell
 * behind the players, the wooden floor they stand on, the curtains at the
 * sides, and the audience row in front where the lit seat is the listener.
 * A part stands on its section's place (the selected part in green), so the
 * picture follows KNOB3 and KNOB4 without a new page. Rectangle fills only:
 * this is a silhouette of a concert hall, not a picture of one. */
static const uint8_t shell_half[12] = {
    6, 14, 22, 30, 38, 46, 54, 62, 70, 78, 86, 92
};

static void draw_hall(void)
{
    const unsigned top = BODY_Y + 4U * ROW_H + 3U;
    const unsigned floor_y = top + 14U;
    const unsigned left = 30U;
    const unsigned right = SW - 30U;
    const unsigned span = right - left;
    const unsigned seat_y = floor_y + 30U;

    /* the shell that carries the sound out over the orchestra */
    for (unsigned row = 0; row < 12U; ++row) {
        const unsigned half = shell_half[row];
        fm1_screen_fill(SW / 2U - half, top + row, half * 2U, 1U, C_SHELL);
    }

    /* the floor: a band the width of the stage, one shade darker at the back */
    fm1_screen_fill(left, floor_y, span, 26U, C_WOOD);
    fm1_screen_fill(left + 10U, floor_y, span - 20U, 3U, C_SHELL);
    fm1_screen_fill(left, floor_y + 26U, span, 1U, C_DIM);  /* the front edge */

    /* the curtains, barely open */
    fm1_screen_fill(left - 10U, top + 2U, 6U, seat_y - top - 2U, C_CURTAIN);
    fm1_screen_fill(right + 4U, top + 2U, 6U, seat_y - top - 2U, C_CURTAIN);

    /* the parts on their section's place, the selected one in green */
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const struct orch_part *part = orch_part_get(p);
        const unsigned pan = orch_stage_place(part->instrument, 128U);
        const unsigned x = left + 6U + pan * (span - 12U) / 255U;
        fm1_screen_fill(x - 3U, floor_y + 8U, 7U, 7U,
                        p == ui.part ? C_NOW : C_TEXT);
        fm1_screen_fill(x - 3U, floor_y + 8U, 7U, 1U, C_DIM);
    }

    /* the audience row; the lit seat is where the listener sits */
    for (unsigned s = 0; s < 7U; ++s)
        fm1_screen_fill(left + s * 26U, seat_y, 18U, 4U, C_STEP);
    const unsigned lx = left +
        (unsigned)((orch_stage_listener_x() + 100) * (int)(span - 18U) / 200);
    fm1_screen_fill(lx, seat_y, 18U, 4U, C_ART);
    /* the ends of the row, so the hall has a left and a right */
    fm1_screen_fill(left - 6U, seat_y + 1U, 4U, 2U, C_DIM);
    fm1_screen_fill(right + 2U, seat_y + 1U, 4U, 2U, C_DIM);
}

static void page_parts(void)
{
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        draw_part_row(p, p == ui.part ? C_SEL : C_ROW);
    draw_hall();
}
