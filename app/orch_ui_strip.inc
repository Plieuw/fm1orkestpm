/* --- the knobs' cells ----------------------------------------------------- */

/* Every page gives its four knobs a job, and the strip under the body shows
 * what each one is and where it stands: a knob with its value drawn as an arc
 * around it, its name under it, and the value under that. The arc is the
 * readout a player can see from a metre away; the number is there when the
 * value is not a percentage (a rate, a mode, a step). */
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
        const int range = hi - lo;
        const int at = value < lo ? lo : value > hi ? hi : value;
        pct = (unsigned)(((at - lo) * 100) / range);
    }
    cell_text(i, label, number, pct, 1);
}

/* The pointer on the knob, drawn the way the angle reads: twelve o'clock at
 * half of the range. */
static void sign(unsigned cx, unsigned cy, unsigned pct, uint16_t c)
{
    const unsigned i = (192U + (pct > 100U ? 100U : pct) * 128U / 100U) >> 3;
    const int x = (int)cx + dir_x[i & 31U] * 5 / 64;
    const int y = (int)cy + dir_y[i & 31U] * 5 / 64;
    pen((int)cx, (int)cy, x, y, 2U, c, C_ROW);
    disc(cx, cy, 2U, C_TEXT, c, C_ROW);
}

/* A knob: a cap shaded from the light at its top left, the pointer, and the
 * value as an amber arc from 7:30 to 4:30 around it. */
static void draw_knob(unsigned cx, unsigned y, unsigned pct, int lit)
{
    const unsigned cy = y + 10U;
    disc(cx, cy, 10U, mix565(C_ROW, C_TEXT, 70U), mix565(C_ROW, C_BG, 90U),
         C_BG);
    disc(cx, cy, 8U, mix565(C_ROW, C_TEXT, 40U), C_ROW, C_BG);
    sign(cx, cy, pct, lit ? C_AMBER2 : C_DIM);
    arc_soft(cx, cy, 13U, 192U, 320U, 1U, mix565(C_BG, C_TEXT, 26U), C_BG);
    if (pct) {
        const unsigned a = 192U + (pct > 100U ? 100U : pct) * 128U / 100U;
        arc_soft(cx, cy, 13U, 192U, a, 2U, lit ? C_AMBER2 : C_AMBER, C_BG);
    }
}

/* The strip along the bottom: a hairline, then the four knobs with their
 * names and values on the case's own coffee. */
static void draw_strip(void)
{
    const int hot = (int32_t)(fm1_millis() - ui.hot_until) < 0;
    fm1_screen_fill(0, STRIP_Y - 1U, SW, 1U, C_LINE);
    fm1_screen_fill(0, STRIP_Y, SW, STRIP_H, C_BG);
    for (unsigned c = 0; c < 4U; ++c) {
        const struct knob_cell *k = &cells[c];
        const unsigned cx = c * 60U + 30U;
        draw_knob(cx, STRIP_Y + 9U, k->pct, k->lit);
        text_center(cx, STRIP_Y + 28U, k->label, C_DIM, C_BG);
        text_center(cx, STRIP_Y + 39U, k->value,
                    (hot && ui.hot_cell == c) ? C_AMBER2
                                              : (k->lit ? C_TEXT : C_DIM),
                    C_BG);
    }
}
