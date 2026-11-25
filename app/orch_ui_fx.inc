/* --- the console and the FX page --- */

/* The console as a console: three faders, their numbers under them, and the
 * note map as seven degrees, lit for the ones the map keeps. */
static void page_global(void)
{
    const char *label[3] = { "TEMPO", "TRANSP", "VOLUME" };
    char value[3][6];
    unsigned pct[3];

    put_num(value[0], 0, sizeof value[0], ui.tempo, 3U);
    put_signed(value[1], 0, sizeof value[1], ui.transpose);
    put_num(value[2], 0, sizeof value[2], (unsigned)((ui.knob * 100U) / 1023U), 2U);
    pct[0] = (unsigned)((ui.tempo - 30U) * 100U / 270U);
    pct[1] = (unsigned)((ui.transpose + 24) * 100 / 48);
    pct[2] = (unsigned)((ui.knob * 100U) / 1023U);

    for (unsigned i = 0; i < 3U; ++i) {
        const unsigned x = 34U + i * 58U;
        fm1_screen_fill(x - 5U, BODY_Y + 6U, 11U, 80U, C_CARD);
        fm1_screen_fill(x, BODY_Y + 12U, 1U, 68U, C_LINE);
        const unsigned y = BODY_Y + 80U - pct[i] * 68U / 100U;
        fm1_screen_fill(x, y, 1U, BODY_Y + 80U - y, C_AMBER);
        fm1_screen_fill(x - 5U, y - 4U, 11U, 8U, C_ROW);
        fm1_screen_fill(x - 5U, y - 4U, 11U, 1U, C_TEXT);
        /* the two words under a fader are cleared first: a value that grows a
         * digit must not leave the old one on the picture behind it */
        fm1_screen_fill(x - 14U, BODY_Y + 89U, 28U, 24U, C_BG);
        text_center(x, BODY_Y + 92U, label[i], C_DIM, C_BG);
        text_center(x, BODY_Y + 104U, value[i], C_TEXT, C_BG);
    }

    /* the note map, as the degrees it keeps */
    {
        const unsigned x0 = 200U;
        for (unsigned d = 0; d < 7U; ++d) {
            const unsigned y = BODY_Y + 12U + d * 12U;
            disc(x0, y, 4U, ui.scale != SCALE_OFF ? C_AMBER2 : C_ROW,
                 ui.scale != SCALE_OFF ? C_AMBER : C_ROW, C_BG);
        }
        fm1_screen_fill(x0 - 14U, BODY_Y + 98U, SW - (x0 - 14U), 10U, C_BG);
        text(x0 - 14U, BODY_Y + 100U, scale_name(ui.scale), C_DIM, C_BG, 60U);
    }

    /* save, once, as a word rather than a line of instructions */
    kv(BODY_Y + 118U, "SAVE", ui.saved ? "DONE" : "TO WRITE",
       ui.saved ? C_METER : C_DIM, C_BG);
}

/* What each effect does to a note, drawn from the two parameters that shape
 * it: a wobble for the chorus and the ensemble, echoes for the delay, a tail
 * that keeps ringing for the shimmer. */
static void fx_shape(unsigned sel, unsigned x, unsigned y, unsigned w,
                     unsigned h, uint16_t fg, uint16_t bg)
{
    unsigned char v[32];
    const unsigned a = orch_fx_param(sel, 0U), b = orch_fx_param(sel, 1U);

    for (unsigned i = 0; i < 32U; ++i) {
        unsigned val;
        if (sel <= 1U) {                    /* chorus, ensemble: a wobble */
            const unsigned period = 6U + (100U - a) / 6U;
            const unsigned ph = (i + period * 32U) % (2U * period);
            const int tri = ph < period ? (int)ph : 2 * (int)period - (int)ph;
            const unsigned amp = 8U + b * 26U / 100U;
            val = 44U + (unsigned)(((int)(tri * 32U / (int)period) - 16) *
                                   (int)amp / 32);
        } else if (sel == 2U) {             /* delay: echo, echo, echo */
            const unsigned gap = 3U + a * 7U / 100U;
            const unsigned k = i / gap;
            const unsigned amp = k < 4U ? 70U / (1U + k * (1U + b / 40U)) : 0U;
            val = (i % gap) == 0U ? 6U + amp : 6U;
        } else {                            /* shimmer: a long tail */
            const unsigned fall = 40U + b * 40U / 100U;
            const unsigned ring = (i * (1U + a / 20U)) & 3U;
            val = 84U - i * fall / 32U + (ring ? 4U : 0U);
        }
        v[i] = (unsigned char)(val > 96U ? 96U : val);
    }
    curve(v, 32U, x, y, w, h, fg, bg);
}

static void page_fx(void)
{
    const unsigned y0 = BODY_Y + 10U;

    for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s) {
        const unsigned y = y0 + s * 17U;
        const int on = (int)orch_fx_enabled(s);
        const uint16_t bg = (s == ui.fx_sel) ? C_ROW : C_CARD;
        fm1_screen_fill(6U, y, SW - 12U, 14U, bg);
        if (s == ui.fx_sel) fm1_screen_fill(6U, y, 3U, 14U, C_AMBER);
        text(13U, y + 4U, orch_fx_name(s), on ? C_AMBER2 : C_DIM, bg, 50U);
        bar(66U, y + 6U, 44U, 3U, orch_fx_mix(s), on ? C_AMBER : C_LINE,
            mix565(bg, C_BG, 150U));
        /* a lamp for on, and its own curve for the one that is open */
        disc(SW - 16U, y + 7U, 3U, on ? C_METER : C_LINE,
             mix565(bg, C_METER, 90U), bg);
    }

    const unsigned sel = ui.fx_sel < ORCH_FX_SLOT_COUNT ? ui.fx_sel : 0U;
    const int on = (int)orch_fx_enabled(sel);
    const unsigned top = y0 + ORCH_FX_SLOT_COUNT * 17U + 4U;
    card(6U, top, SW - 12U, 46U, C_CARD);
    fx_shape(sel, 12U, top + 4U, SW - 24U, 14U,
             on ? mix565(C_METER, C_TEXT, 60U) : C_DIM, C_CARD);

    /* All four parameters of the open effect, two to a line. The pair the strip
     * is on is the bright one, so the page and the knobs agree on where the
     * next turn goes. */
    {
        const unsigned bank = ui.fx_bank ? 2U : 0U;
        for (unsigned i = 0; i < ORCH_FX_PARAMS; ++i) {
            const unsigned cx = 62U + (i % 2U) * 116U;
            const unsigned y = top + 20U + (i / 2U) * 10U;
            char line[24];
            unsigned n = put(line, 0, sizeof line, orch_fx_param_name(sel, i));
            n = put(line, n, sizeof line, " ");
            if (sel == ORCH_FX_DELAY && i == 2U)
                put(line, n, sizeof line, fx_sync_name(orch_fx_param(sel, i)));
            else
                put_num(line, n, sizeof line, orch_fx_param(sel, i), 2U);
            fm1_screen_fill(cx - 56U, y, 112U, 9U, C_CARD);
            text_center(cx, y, line,
                        (i / 2U) == (bank / 2U) ? C_TEXT : C_DIM, C_CARD);
        }
    }
}
