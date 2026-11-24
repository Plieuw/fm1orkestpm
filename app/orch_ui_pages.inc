static void page_arp(void)
{
    char line[40];
    unsigned n = put(line, 0, sizeof line, "PART ");
    n = put_num(line, n, sizeof line, ui.part + 1U, 1U);
    n = put(line, n, sizeof line, "  ");
    put(line, n, sizeof line, orch_arp_enabled(ui.part) ? "ARP ON" : "ARP OFF");
    /* the page header is a band of wood, as the floor is on the stage page */
    fm1_screen_fill(0, BODY_Y, SW, 12U, C_ROW);
    fm1_screen_fill(0, BODY_Y + 12U, SW, 1U, C_WOOD);
    text(8U, BODY_Y + 4U, line, orch_arp_enabled(ui.part) ? C_METER : C_DIM,
         C_ROW, SW - 16U);

    setting(BODY_Y + 22U, "MODE", arp_mode_name(orch_arp_mode_of(ui.part)), C_ART);
    setting_num(BODY_Y + 36U, "OCTAVES", orch_arp_octaves_of(ui.part), 1U, C_TEXT);
    setting(BODY_Y + 50U, "RATE", div_name(orch_arp_div_of(ui.part)), C_TEXT);
    setting_num(BODY_Y + 64U, "GATE %", orch_arp_gate_of(ui.part), 3U, C_TEXT);
    setting_num(BODY_Y + 78U, "HELD", orch_arp_held(ui.part), 1U,
                orch_arp_held(ui.part) ? C_METER : C_DIM);

    text(8U, BODY_Y + 108U, "B12 TOGGLES  B10 PARTS  ENC1 PART",
         C_DIM, C_BG, SW - 16U);
}

static void page_seq(void)
{
    char line[40];
    unsigned n = put(line, 0, sizeof line, "PART ");
    n = put_num(line, n, sizeof line, ui.part + 1U, 1U);
    n = put(line, n, sizeof line, ui.seq_on ? "  RUNNING" : "  STOPPED");
    fm1_screen_fill(0, BODY_Y, SW, 12U, C_ROW);
    fm1_screen_fill(0, BODY_Y + 12U, SW, 1U, C_WOOD);
    text(8U, BODY_Y + 2U, line, ui.seq_on ? C_METER : C_DIM, C_ROW, SW - 16U);
    text(150U, BODY_Y + 2U, ui.rec ? "REC" : "    ", ui.rec ? C_REC : C_DIM,
         C_ROW, 70U);

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

    text(8U, BODY_Y + 108U, "B15 REC: KEYS WRITE A STEP", C_DIM, C_BG, SW - 16U);
}

/* A one pixel wooden frame, like the rack panel the console sits in. The text
 * stays on black inside it, so its contrast is exactly what it was. */
static void wood_frame(unsigned x, unsigned y, unsigned w, unsigned h)
{
    fm1_screen_fill(x, y, w, 1U, C_WOOD);
    fm1_screen_fill(x, y + h - 1U, w, 1U, C_WOOD);
    fm1_screen_fill(x, y, 1U, h, C_WOOD);
    fm1_screen_fill(x + w - 1U, y, 1U, h, C_WOOD);
}

static void page_global(void)
{
    wood_frame(4U, BODY_Y, SW - 8U, 72U);
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

static void page_fx(void)
{
    fm1_screen_fill(0, BODY_Y, SW, 12U, C_ROW);
    fm1_screen_fill(0, BODY_Y + 12U, SW, 1U, C_WOOD);
    text(8U, BODY_Y + 2U, "MASTER FX AFTER THE MIX", C_DIM, C_ROW, SW - 16U);

    char line[40];
    for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s) {
        const unsigned y = BODY_Y + 18U + s * 14U;
        const uint16_t bg = (s == ui.fx_sel) ? C_SEL : C_BG;
        const int on = (int)orch_fx_enabled(s);
        if (s == ui.fx_sel) fm1_screen_fill(4U, y - 2U, SW - 8U, 11U, bg);
        text(8U, y, orch_fx_name(s), on ? C_METER : C_DIM, bg, 56U);
        text(66U, y, on ? "ON" : "OFF", on ? C_METER : C_DIM, bg, 20U);

        unsigned n = put(line, 0, sizeof line, "MIX ");
        put_num(line, n, sizeof line, orch_fx_mix(s), 2U);
        text(94U, y, line, C_TEXT, bg, 40U);

        n = put(line, 0, sizeof line, orch_fx_param_a_name(s));
        put(line, n, sizeof line, " ");
        put_num(line, n, sizeof line, orch_fx_param_a(s), 2U);
        text(138U, y, line, C_TEXT, bg, 48U);

        n = put(line, 0, sizeof line, orch_fx_param_b_name(s));
        put(line, n, sizeof line, " ");
        put_num(line, n, sizeof line, orch_fx_param_b(s), 2U);
        text(190U, y, line, C_TEXT, bg, 50U);
    }

    const unsigned sel = ui.fx_sel < ORCH_FX_SLOT_COUNT ? ui.fx_sel : 0U;
    const unsigned base = BODY_Y + 78U;
    char number[40];
    unsigned n = put(number, 0, sizeof number, orch_fx_name(sel));
    n = put(number, n, sizeof number, "  ");
    n = put(number, n, sizeof number, orch_fx_enabled(sel) ? "ON" : "OFF");
    text(8U, base, number, orch_fx_enabled(sel) ? C_ART : C_DIM, C_BG, SW - 16U);

    text(8U, base + 16U, "ALGORITHM SELECTS THE EFFECT", C_DIM, C_BG, SW - 16U);
    text(8U, base + 30U, "FX AGAIN: TEST CHORD", C_DIM, C_BG, SW - 16U);
    text(8U, base + 44U, "SAVE KEEPS THE FX SETTINGS", C_DIM, C_BG, SW - 16U);
    text(8U, base + 58U, "SHARED BY ALL PARTS", C_ART, C_BG, SW - 16U);
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
    fm1_screen_fill(0, TITLE_H, SW, STRIP_Y - 4U - TITLE_H, C_BG);
    /* the curtain rail under the title, on every page */
    fm1_screen_fill(0, TITLE_H, SW, 1U, C_CURTAIN);
    page_knobs();
    switch (ui.page) {
    case PAGE_ARP: page_arp(); break;
    case PAGE_SEQ: page_seq(); break;
    case PAGE_GLO: page_global(); break;
    case PAGE_FX: page_fx(); break;
    case PAGE_HOME:
    default: page_parts(); break;
    }
    draw_strip();
}

/* The pages show live values (a running step, held arp notes). The control task
 * compares what it drew with what the engine says, and repaints on a change. */
static uint32_t page_signature(void)
{
    uint32_t sig = (uint32_t)ui.page * 0x9E3779B1UL;
    if ((int32_t)(fm1_millis() - ui.hot_until) < 0)
        sig ^= 0x40U | ((uint32_t)ui.hot_cell << 8);   /* the lit cell, and back */
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
    case PAGE_FX:
        for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s)
            sig = sig * 31U + (uint32_t)orch_fx_enabled(s)
                + 3U * orch_fx_mix(s) + 7U * orch_fx_param_a(s)
                + 11U * orch_fx_param_b(s);
        sig = sig * 31U + ui.fx_sel;
        break;
    case PAGE_HOME:
    default:
        sig = sig * 31U + (uint32_t)(orch_stage_listener_x() + 128);
        sig = sig * 31U + orch_stage_listener_depth();
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
