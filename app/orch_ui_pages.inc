/* --- the knobs' jobs ------------------------------------------------------ */

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

/* --- the pages ------------------------------------------------------------ */

static void page_arp(void)
{
    const int on = (int)orch_arp_enabled(ui.part);
    const unsigned held = orch_arp_held(ui.part);
    char line[40];
    unsigned n;

    n = put(line, 0, sizeof line, "PART ");
    n = put_num(line, n, sizeof line, ui.part + 1U, 1U);
    kv(BODY_Y + 2U, line, on ? "ARP ON" : "ARP OFF", on ? C_METER : C_DIM, C_BG);

    kv(BODY_Y + 22U, "MODE", arp_mode_name(orch_arp_mode_of(ui.part)), C_AMBER, C_BG);
    kv_num(BODY_Y + 36U, "OCTAVES", orch_arp_octaves_of(ui.part), 1U, C_TEXT, C_BG);
    kv(BODY_Y + 50U, "RATE", div_name(orch_arp_div_of(ui.part)), C_TEXT, C_BG);
    kv_num(BODY_Y + 64U, "GATE %", orch_arp_gate_of(ui.part), 3U, C_TEXT, C_BG);
    kv_num(BODY_Y + 78U, "HELD NOTES", held, 1U, held ? C_METER : C_DIM, C_BG);

    /* how much of the part's voices the arpeggiator has taken */
    bar(10U, BODY_Y + 92U, SW - 20U, 3U,
        held >= 8U ? 100U : held * 12U, C_METER, C_CARD);

    text(10U, BODY_Y + 104U, "PLAY ARP ON/OFF   SELECT PART", C_DIM, C_BG, SW - 20U);
    text(10U, BODY_Y + 116U, "ALGORITHM: MODE   PRESETS: SOUND", C_DIM, C_BG, SW - 20U);
    text(10U, BODY_Y + 128U, "KNOBS: RATE OCT GATE MODE", C_DIM, C_BG, SW - 20U);
}

static void page_seq(void)
{
    char line[40];
    unsigned n;

    n = put(line, 0, sizeof line, "PART ");
    n = put_num(line, n, sizeof line, ui.part + 1U, 1U);
    kv(BODY_Y + 2U, line,
       ui.rec ? "REC ARMED" : ui.seq_on ? "RUNNING" : "STOPPED",
       ui.rec ? C_REC : ui.seq_on ? C_METER : C_DIM, C_BG);

    /* sixteen steps: the selected one is framed in amber, a step with notes
     * has an amber bar and the step that is sounding has a green one, so a
     * running pattern is visible while it runs */
    for (unsigned i = 0; i < ORCH_SEQ_STEPS; ++i) {
        const unsigned x = 8U + i * 14U;
        const unsigned y = BODY_Y + 18U;
        fm1_screen_fill(x, y, 12U, 18U, i == ui.seq_step ? C_ROW : C_CARD);
        if (seq_notes[i][0].velocity)
            fm1_screen_fill(x + 2U, y + 10U, 8U, 3U, C_AMBER2);
        if (i == orch_seq_position())
            fm1_screen_fill(x + 2U, y + 14U, 8U, 2U, C_METER);
        if (i == ui.seq_step) {
            fm1_screen_fill(x, y, 12U, 1U, C_AMBER);
            fm1_screen_fill(x, y + 17U, 12U, 1U, C_AMBER);
            fm1_screen_fill(x, y, 1U, 18U, C_AMBER);
            fm1_screen_fill(x + 11U, y, 1U, 18U, C_AMBER);
        }
    }

    n = put(line, 0, sizeof line, "STEP ");
    n = put_num(line, n, sizeof line, ui.seq_step + 1U, 2U);
    text(10U, BODY_Y + 42U, line, C_AMBER, C_BG, SW - 20U);

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
    text(10U, BODY_Y + 54U, notes, C_TEXT, C_BG, SW - 20U);

    kv_num(BODY_Y + 72U, "VELOCITY", seq_notes[ui.seq_step][0].velocity, 3U,
           C_TEXT, C_BG);
    kv_num(BODY_Y + 86U, "GATE %", seq_notes[ui.seq_step][0].gate_percent, 3U,
           C_TEXT, C_BG);

    text(10U, BODY_Y + 104U, "REC WRITES A STEP FROM THE KEYS",
         C_DIM, C_BG, SW - 20U);
    text(10U, BODY_Y + 116U, "PLAY RUNS   ALGORITHM: STEP",
         C_DIM, C_BG, SW - 20U);
}
