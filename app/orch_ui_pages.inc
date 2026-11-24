/* --- the knobs' jobs ------------------------------------------------------ */

/* SYNC is a division of the beat, not a percentage: the strip says which one
 * rather than a number that means nothing on its own. */
static const char *fx_sync_name(unsigned percent)
{
    if (!percent) return "FREE";
    if (percent > 66U) return "1/8";
    if (percent > 33U) return "1/8T";
    return "1/16";
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
        /* four parameters an effect, two of them on the strip at a time: the
         * bank says which pair, and PRESETS turns it over */
        const unsigned s = ui.fx_sel < ORCH_FX_SLOT_COUNT ? ui.fx_sel : 0U;
        const int on = (int)orch_fx_enabled(s);
        const unsigned b = ui.fx_bank ? 2U : 0U;
        cell_number(0, "MIX", (int)orch_fx_mix(s), 0, 100);
        if (s == ORCH_FX_DELAY && b == 2U)
            cell_text(1, orch_fx_param_name(s, b),
                      fx_sync_name(orch_fx_param(s, b)),
                      orch_fx_param(s, b), 1);
        else
            cell_number(1, orch_fx_param_name(s, b),
                        (int)orch_fx_param(s, b), 0, 100);
        cell_number(2, orch_fx_param_name(s, b + 1U),
                    (int)orch_fx_param(s, b + 1U), 0, 100);
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

/* The arp's card runs from the top of the page down to the picture band: a plot
 * of 100 rows, so the card ends two pixels above the band's hairline. */
#define ARP_PLOT_H 100U
#define ARP_CARD_H (ARP_PLOT_H + 22U)

/* The keys that are held, as dots under the axis. The arp itself does not move
 * once a mode is chosen, so this row is all a new chord has to repaint. */
static void draw_arp_keys(void)
{
    const unsigned held = orch_arp_held(ui.part);
    const unsigned x0 = 12U, w = SW - 24U;
    const unsigned axis = BODY_Y + 10U + ARP_PLOT_H;

    fm1_screen_fill(x0, axis + 6U, w, 8U, C_CARD);
    for (unsigned i = 0; i < 8U; ++i) {
        const unsigned x = x0 + 6U + i * (w - 12U) / 7U;
        disc(x, axis + 9U, 2U, i < held ? C_AMBER2 : C_LINE, C_BG, C_CARD);
    }
}

/* --- arp ------------------------------------------------------------------ */

/* The arpeggio as a picture: the shape of the mode across three octaves, with
 * the gate drawn as the length each note is held and the held keys as the
 * dots under the axis. Nothing in words except the rate, which is a fraction. */
static void page_arp(void)
{
    const unsigned mode = orch_arp_mode_of(ui.part);
    const unsigned oct = orch_arp_octaves_of(ui.part);
    const unsigned gate = orch_arp_gate_of(ui.part);
    const unsigned on = orch_arp_enabled(ui.part);
    const unsigned top = BODY_Y + 10U, h = ARP_PLOT_H;
    const unsigned x0 = 12U, w = SW - 24U;
    const unsigned axis = top + h;

    /* the frame the pattern is drawn in */
    card(6U, BODY_Y + 4U, SW - 12U, ARP_CARD_H, C_CARD);
    fm1_screen_fill(x0, axis, w, 1U, C_LINE);

    /* which way it runs: up, down, up-down, random, as played */
    const unsigned n = 8U;
    unsigned order[8];
    for (unsigned i = 0; i < n; ++i) {
        unsigned k = i;
        switch (mode) {
        case 1: k = n - 1U - i; break;                       /* down */
        case 2: k = i < n / 2U ? i : n - 1U - i; break;       /* up-down */
        case 3: k = (i * 5U + 3U) % n; break;                 /* random */
        default: break;
        }
        order[i] = k * oct / 2U;
    }

    for (unsigned i = 0; i < n; ++i) {
        const unsigned x = x0 + i * (w - 1U) / (n - 1U);
        const unsigned up = order[i] * (h - 14U) / ((n - 1U) * oct / 2U + 1U) + 6U;
        const unsigned y = axis - up;
        /* the gate: how long this note is held, as a bar on its own row */
        const unsigned len = 4U + gate * (w / n - 3U) / 100U;
        fm1_screen_fill(x, y, len < w - x ? len : w - x, 2U,
                        on ? mix565(C_CARD, C_METER, 90U) : C_LINE);
        disc(x, y, 3U, on ? C_METER : C_DIM, mix565(C_CARD, C_METER, 60U), C_CARD);
        if (i + 1U < n) {
            const unsigned nx = x0 + (i + 1U) * (w - 1U) / (n - 1U);
            const unsigned nup = order[i + 1U] * (h - 14U) / ((n - 1U) * oct / 2U + 1U) + 6U;
            pen((int)x, (int)y, (int)nx, (int)(axis - nup), 2U,
                on ? mix565(C_CARD, C_AMBER, 90U) : C_LINE, C_CARD);
        }
    }

    draw_arp_keys();

    /* the rate, as one word, over the frame */
    text(12U, BODY_Y + 6U, div_name(orch_arp_div_of(ui.part)),
         on ? C_AMBER : C_DIM, C_CARD, 40U);
    text(SW - 12U - span(arp_mode_name(mode)) - 6U, BODY_Y + 6U,
         arp_mode_name(mode), C_DIM, C_CARD, 60U);
}

/* One step of the grid: its card, filled by its velocity, framed when it is the
 * open step and underlined when it is the one playing. It is drawn on its own
 * as well, because a running sequencer changes two cells a step and nothing
 * else on the page. */
static void draw_step_cell(unsigned i)
{
    const unsigned y = BODY_Y + 8U;
    const unsigned cell = 14U;
    const unsigned x = 8U + i * cell;
    const uint16_t bg = (i == ui.seq_step) ? C_ROW : C_CARD;

    fm1_screen_fill(x, y + 8U, 12U, 26U, bg);
    fm1_screen_fill(x, y + 8U, 12U, 1U, C_LINE);
    fm1_screen_fill(x + 11U, y + 8U, 1U, 26U, C_LINE);
    fm1_screen_fill(x, y + 33U, 12U, 1U, C_LINE);
    const unsigned vel = seq_notes[i][0].velocity;
    if (vel) {
        const unsigned h = 2U + vel * 22U / 127U;
        fm1_screen_fill(x + 2U, y + 32U - h, 8U, h, C_AMBER);
        fm1_screen_fill(x + 2U, y + 32U - h, 8U, 1U,
                        mix565(C_AMBER, C_TEXT, 90U));
    }
    if (i == ui.seq_step) {
        fm1_screen_fill(x, y + 8U, 12U, 1U, C_AMBER);
        fm1_screen_fill(x, y + 33U, 12U, 1U, C_AMBER);
        fm1_screen_fill(x, y + 8U, 1U, 26U, C_AMBER);
        fm1_screen_fill(x + 11U, y + 8U, 1U, 26U, C_AMBER);
    }
    if (i == orch_seq_position()) {
        fm1_screen_fill(x + 2U, y + 35U, 8U, 2U, ui.seq_on ? C_METER : C_CARD);
    }
}

/* --- sequencer ------------------------------------------------------------ */

/* The pattern as a grid of steps and, under it, the shape of the selected
 * step: its four notes as a line, so a phrase reads at a glance. */
static void page_seq(void)
{
    const unsigned y = BODY_Y + 8U;

    if (ui.rec) {
        disc(SW - 12U, y + 4U, 3U, C_REC, mix565(C_REC, C_BG, 120U), C_BG);
    }

    for (unsigned i = 0; i < ORCH_SEQ_STEPS; ++i)
        draw_step_cell(i);

    /* the selected step's notes as a line over the whole range of the keys */
    card(6U, BODY_Y + 50U, SW - 12U, 76U, C_CARD);
    unsigned char pitch[ORCH_SEQ_STEPS];
    unsigned count = 0U;
    unsigned lo = 127U, hi = 0U;
    unsigned last = 60U;                  /* a rest holds the note before it */
    for (unsigned i = 0; i < ORCH_SEQ_STEPS; ++i) {
        const unsigned note = seq_notes[i][0].velocity ? seq_notes[i][0].note : last;
        last = note;
        pitch[count++] = (unsigned char)note;
        if (note < lo) lo = note;
        if (note > hi) hi = note;
    }
    if (hi <= lo) { lo = 48U; hi = 72U; }
    unsigned char curve_v[ORCH_SEQ_STEPS];
    for (unsigned i = 0; i < count; ++i)
        curve_v[i] = pitch[i] <= hi
                         ? (unsigned char)(4U + (pitch[i] - lo) * 92U / (hi - lo))
                         : 4U;
    curve(curve_v, count, 10U, BODY_Y + 58U, SW - 20U, 60U,
          ui.seq_on ? C_METER : C_AMBER, C_CARD);

    /* the step that is open, at its own height, amber */
    {
        const unsigned i = ui.seq_step < count ? ui.seq_step : 0U;
        const unsigned x = 10U + i * (SW - 21U) / (count - 1U);
        const unsigned yy = BODY_Y + 58U + 60U - 1U -
            (unsigned)(curve_v[i] * 59U / 100U);
        disc(x, yy, 3U, C_AMBER2, C_AMBER, C_CARD);
    }

    /* the open step's number and its notes, small, in the card's corners */
    {
        char line[24];
        unsigned n = put(line, 0, sizeof line, "STEP ");
        put_num(line, n, sizeof line, ui.seq_step + 1U, 2U);
        text(10U, BODY_Y + 52U, line, C_DIM, C_CARD, 54U);

        n = 0;
        for (unsigned s = 0; s < SEQ_SLOTS; ++s) {
            if (!seq_notes[ui.seq_step][s].velocity) {
                n = put(line, n, sizeof line, "- ");
                continue;
            }
            char nn[4];
            note_name(seq_notes[ui.seq_step][s].note, nn);
            n = put(line, n, sizeof line, nn);
            n = put(line, n, sizeof line, " ");
        }
        text(SW - 10U - span(line), BODY_Y + 52U, line, C_TEXT, C_CARD,
             span(line));
    }
}
