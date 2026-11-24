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
        const int range = hi - lo;
        const int at = value < lo ? lo : value > hi ? hi : value;
        pct = (unsigned)(((at - lo) * 100) / range);
    }
    cell_text(i, label, number, pct, 1);
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
    for (unsigned row = 0; row < 23U; ++row) {
        const unsigned half = knob_half[row];
        fm1_screen_fill(cx - half, y + row, half * 2U, 1U, lit ? C_ROW : C_CARD);
    }
    const int8_t *arm = knob_arm[(pct > 100U ? 100U : pct) * 16U / 100U];
    for (unsigned k = 3U; k <= 7U; k += 2U) {
        const int px = (int)cx + (int)arm[0] * (int)k / 8;
        const int py = (int)y + 11 + (int)arm[1] * (int)k / 8;
        fm1_screen_fill((unsigned)px, (unsigned)py, 2U, 2U,
                        lit ? C_AMBER2 : C_DIM);
    }
}

/* The strip along the bottom: a hairline, then the four knobs with their
 * labels and values on the case's own coffee. */
static void draw_strip(void)
{
    const int hot = (int32_t)(fm1_millis() - ui.hot_until) < 0;
    fm1_screen_fill(0, STRIP_Y - 1U, SW, 1U, C_LINE);
    fm1_screen_fill(0, STRIP_Y, SW, STRIP_H, C_BG);
    for (unsigned c = 0; c < 4U; ++c) {
        const struct knob_cell *k = &cells[c];
        const unsigned cx = c * 60U + 30U;
        draw_knob(cx, STRIP_Y + 4U, k->pct, k->lit);
        text_center(cx, STRIP_Y + 28U, k->label, C_DIM, C_BG);
        text_center(cx, STRIP_Y + 38U, k->value,
                    (hot && ui.hot_cell == c) ? C_AMBER2
                                              : (k->lit ? C_TEXT : C_DIM),
                    C_BG);
    }
}
