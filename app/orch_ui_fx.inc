/* --- the console and the FX page --- */

static void page_global(void)
{
    char number[8];
    put_signed(number, 0, sizeof number, ui.transpose);

    card(4U, BODY_Y + 4U, SW - 8U, 74U, C_CARD);
    kv_num(BODY_Y + 10U, "TEMPO", ui.tempo, 3U, C_TEXT, C_CARD);
    kv(BODY_Y + 24U, "TRANSPOSE", number, C_TEXT, C_CARD);
    kv(BODY_Y + 38U, "SCALE", scale_name(ui.scale), C_AMBER, C_CARD);
    kv_num(BODY_Y + 52U, "VOLUME", (unsigned)((ui.knob * 100U) / 1023U), 2U,
           C_TEXT, C_CARD);
    kv(BODY_Y + 66U, "SAVE", ui.saved ? "DONE" : "SAVE TO WRITE",
       ui.saved ? C_METER : C_DIM, C_CARD);

    text(10U, BODY_Y + 86U, "FX: DIVISI 1<->4   SCL: SCALE", C_DIM, C_BG, SW - 20U);
    text(10U, BODY_Y + 98U, "OCT-/+: TRANSPOSE   ART: ENV/LFO", C_DIM, C_BG, SW - 20U);
    text(10U, BODY_Y + 110U, "K4: PANIC (ALL NOTES OFF)", C_DIM, C_BG, SW - 20U);
}

static void page_fx(void)
{
    text(10U, BODY_Y + 2U, "MASTER FX AFTER THE MIX", C_DIM, C_BG, SW - 20U);

    /* one line per effect: its name, whether it is on, and its mix. The two
     * parameters belong to the effect that is selected, so they get the card
     * under the list instead of a column that would not fit. */
    const unsigned y0 = BODY_Y + 14U;
    for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s) {
        const unsigned y = y0 + s * 13U;
        const int on = (int)orch_fx_enabled(s);
        const uint16_t bg = (s == ui.fx_sel) ? C_ROW : C_BG;
        char line[12];
        unsigned n = put(line, 0, sizeof line, "MIX ");
        put_num(line, n, sizeof line, orch_fx_mix(s), 2U);

        fm1_screen_fill(4U, y - 1U, SW - 8U, 12U, bg);
        if (s == ui.fx_sel) fm1_screen_fill(4U, y - 1U, 3U, 12U, C_AMBER);
        text(12U, y, orch_fx_name(s), on ? C_AMBER2 : C_DIM, bg, 96U);
        text(112U, y, on ? "ON" : "OFF", on ? C_METER : C_DIM, bg, 24U);
        text(SW - 10U - span(line), y, line, C_TEXT, bg, span(line));
    }

    const unsigned sel = ui.fx_sel < ORCH_FX_SLOT_COUNT ? ui.fx_sel : 0U;
    const int on = (int)orch_fx_enabled(sel);
    const unsigned base = y0 + ORCH_FX_SLOT_COUNT * 13U + 4U;
    card(4U, base, SW - 8U, 42U, C_CARD);
    text(12U, base + 5U, orch_fx_name(sel), on ? C_AMBER : C_DIM, C_CARD, 130U);
    text(SW - 10U - span(on ? "ON" : "OFF"), base + 5U, on ? "ON" : "OFF",
         on ? C_METER : C_DIM, C_CARD, span(on ? "ON" : "OFF"));

    char line[40];
    unsigned n = put(line, 0, sizeof line, orch_fx_param_a_name(sel));
    n = put(line, n, sizeof line, " ");
    put_num(line, n, sizeof line, orch_fx_param_a(sel), 2U);
    text(12U, base + 18U, line, C_TEXT, C_CARD, 96U);

    n = put(line, 0, sizeof line, orch_fx_param_b_name(sel));
    n = put(line, n, sizeof line, " ");
    put_num(line, n, sizeof line, orch_fx_param_b(sel), 2U);
    text(120U, base + 18U, line, C_TEXT, C_CARD, 108U);

    text(12U, base + 30U, "ALGORITHM SELECTS THE EFFECT", C_DIM, C_CARD, 200U);

    text(10U, base + 50U, "FX AGAIN: TEST CHORD", C_DIM, C_BG, SW - 20U);
    text(10U, base + 62U, "SAVE KEEPS THE FX SETTINGS", C_DIM, C_BG, SW - 20U);
    text(10U, base + 74U, "SHARED BY ALL PARTS", C_AMBER, C_BG, SW - 20U);
}
