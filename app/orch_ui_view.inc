/* --- strings -------------------------------------------------------------- */

/* Note names for the sequencer, C4 = 60. */
static void note_name(unsigned note, char out[4])
{
    static const char *const names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    if (note > 127U) note = 127U;
    const char *name = names[note % 12U];
    const char octave = (char)('0' + (note / 12U) - 1U);
    /* a name is "C" or "C#": only the second one has a second letter, so the
     * octave goes where the second character would be — writing it after the
     * terminator is how "C4" came out as "C" */
    out[0] = name[0];
    out[1] = name[1] ? name[1] : octave;
    out[2] = name[1] ? octave : '\0';
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

static unsigned span(const char *s)
{
    unsigned n = 0;
    while (s[n]) ++n;
    return n * 6U;
}

/* --- the page's furniture ------------------------------------------------ */

/* One row of a settings page: the label in grey at the left, the value in its
 * own colour against the right edge. Right-aligned, so a value that grows a
 * digit pushes left instead of running into the label (the FX page used to
 * overlap its columns, which is what this shape is for). */
static void kv(unsigned y, const char *label, const char *value, uint16_t vc,
               uint16_t bg)
{
    text(10U, y, label, C_DIM, bg, SW - 20U);
    const unsigned w = span(value);
    if (w + 10U < SW)
        text(SW - 10U - w, y, value, vc, bg, w);
    else
        text(10U, y, value, vc, bg, SW - 20U);
}

static void kv_num(unsigned y, const char *label, unsigned value, unsigned width,
                   uint16_t vc, uint16_t bg)
{
    char number[8];
    put_num(number, 0, sizeof number, value, width);
    kv(y, label, number, vc, bg);
}

/* A card: the case's darker coffee with a hairline around it. */
static void card(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t fill)
{
    fm1_screen_fill(x, y, w, h, fill);
    fm1_screen_fill(x, y, w, 1U, C_LINE);
    fm1_screen_fill(x, y + h - 1U, w, 1U, C_LINE);
    fm1_screen_fill(x, y, 1U, h, C_LINE);
    fm1_screen_fill(x + w - 1U, y, 1U, h, C_LINE);
}

/* A meter: the dark trough, then the filled part. */
static void bar(unsigned x, unsigned y, unsigned w, unsigned h, unsigned pct,
                uint16_t fg, uint16_t bg)
{
    if (pct > 100U) pct = 100U;
    fm1_screen_fill(x, y, w, h, bg);
    if (pct) fm1_screen_fill(x, y, w * pct / 100U, h, fg);
}

static void text_center(unsigned cx, unsigned y, const char *s, uint16_t fg,
                        uint16_t bg)
{
    const unsigned w = span(s);
    if (!w) return;
    const unsigned x = w / 2U < cx ? cx - w / 2U : 0U;
    text(x, y, s, fg, bg, w);
}
