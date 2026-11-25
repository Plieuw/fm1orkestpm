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
    case PAGE_ARP: {
        const struct orch_perf_config pc = perf_of(ui.part);
        cell_text(0, "TYPE", arp_type_name(pc.arp_type),
                  pc.arp_type * 100U / (ORCH_PERF_ARP_TYPES - 1U), pc.arp_enabled);
        cell_text(1, "RATE", div_name(pc.division),
                  pc.division * 100U / (ORCH_PERF_DIVISIONS - 1U), 1);
        cell_number(2, "GATE", (int)pc.gate, 5, 100);
        if (pc.phrase_enabled) {
            char ge[8];
            unsigned n = put(ge, 0, sizeof ge, "GE ");
            put_num(ge, n, sizeof ge, pc.phrase_ge + 1U, 1U);
            cell_text(3, "PHRASE", ge, pc.phrase_ge * 100U / (ORCH_PERF_GE_COUNT - 1U), 1);
        } else {
            cell_text(3, "PHRASE", "OFF", 0U, 0);
        }
        break;
    }
    case PAGE_SEQ: {
        const struct orch_step_note *note = &seq_notes[ui.seq_step][0];
        cell_number(0, "VEL", (int)note->velocity, 1, 127);
        cell_number(1, "GATE", (int)note->gate_percent, 5, 100);
        cell_number(2, "TEMPO", (int)ui.tempo, 40, 250);
        cell_text(3, "CLEAR", "STEP", 0U, 0);
        break;
    }
    case PAGE_GLO:
        cell_text(0, "SCALE", scale_name(ui.scale), ui.scale * 33U, ui.scale != 0U);
        cell_number(1, "TRANSP", ui.transpose, -24, 24);
        cell_number(2, "TEMPO", (int)ui.tempo, 40, 250);
        cell_text(3, "SCENE", orch_scene_name(ui.scene),
                  ui.scene * 100U / (ORCH_SCENE_COUNT - 1U), 1);
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
    case PAGE_ENV:
        if (!part) break;
        cell_number(0, "ATTACK", (int)part->adsr.attack_ms, 1, 4000);
        cell_number(1, "DECAY", (int)part->adsr.decay_ms, 1, 4000);
        cell_number(2, "SUSTAIN", (int)part->adsr.sustain_percent, 0, 100);
        cell_number(3, "RELEASE", (int)part->adsr.release_ms, 1, 6000);
        break;
    case PAGE_LFO:
        if (!part) break;
        cell_number(0, "DEPTH", (int)part->lfo.depth, 0, 100);
        cell_number(1, "RATE", (int)(part->lfo.rate_millihz / 100U), 1, 200);
        cell_text(2, "WAVE", wave_name(part->lfo.wave), part->lfo.wave * 25U, 1);
        cell_text(3, "DEST", lfo_dest_name(part->lfo.destinations),
                  part->lfo.destinations * 14U, part->lfo.destinations != 0U);
        break;
    case PAGE_HOME:
    default:
        if (!part) break;
        cell_number(0, "PLAYERS", (int)part->player_cap, 1, 4);
        cell_text(1, "ARTIC", art_name(part->articulation),
                  part->articulation * 100U / (ORCH_ART_COUNT - 1U), 1);
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
    const unsigned held = orch_perf_held(ui.part);
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
    const struct orch_perf_config pc = perf_of(ui.part);
    const unsigned type = pc.arp_type % ORCH_PERF_ARP_TYPES;
    const unsigned gate = pc.gate;
    const unsigned on = pc.arp_enabled || pc.phrase_enabled;
    const unsigned top = BODY_Y + 10U, h = ARP_PLOT_H;
    const unsigned x0 = 12U, w = SW - 24U;
    const unsigned axis = top + h;
    /* the order of the type and its octaves (1, 2 or 4); the step patterns
     * run up with rests, drawn as the dimmed dots */
    const unsigned family = type < 15U ? type / 3U : 0U;
    const unsigned oct = type < 15U ? (type % 3U == 0U ? 1U : type % 3U == 1U ? 2U : 4U) : 1U;
    static const uint16_t pat_mask[16] = {
        0x5555U,0x2492U,0x0F0FU,0x8421U,0x3333U,0xAAAAU,0x9696U,0x6996U,
        0x1111U,0xE4E4U,0x5A5AU,0x8889U,0xC3C3U,0x3C3CU,0xF00FU,0x0FF0U
    };
    const uint16_t mask = type < 15U ? 0xFFFFU : pat_mask[(type - 15U) & 15U];

    /* the frame the pattern is drawn in */
    card(6U, BODY_Y + 4U, SW - 12U, ARP_CARD_H, C_CARD);
    fm1_screen_fill(x0, axis, w, 1U, C_LINE);

    const unsigned n = 8U;
    unsigned order[8];
    for (unsigned i = 0; i < n; ++i) {
        unsigned k = i;
        switch (family) {
        case 1: k = n - 1U - i; break;                         /* down */
        case 2: k = i < n / 2U ? i * 2U : (n - 1U - i) * 2U; break; /* up-down */
        case 3: k = i < n / 2U ? i * 2U : (n - i) * 2U - 1U; break; /* pendulum */
        case 4: k = (i * 5U + 3U) % n; break;                   /* random */
        default: break;
        }
        if (k > n - 1U) k = n - 1U;
        order[i] = k * oct;
    }
    const unsigned span_top = (n - 1U) * oct + 1U;

    for (unsigned i = 0; i < n; ++i) {
        const unsigned x = x0 + i * (w - 1U) / (n - 1U);
        const unsigned up = order[i] * (h - 14U) / span_top + 6U;
        const unsigned y = axis - up;
        const int hit = (mask >> (i * 2U)) & 1U;
        /* the gate: how long this note is held, as a bar on its own row */
        const unsigned len = 4U + gate * (w / n - 3U) / 100U;
        if (hit)
            fm1_screen_fill(x, y, len < w - x ? len : w - x, 2U,
                            on ? mix565(C_CARD, C_METER, 90U) : C_LINE);
        disc(x, y, 3U, !hit ? C_LINE : on ? C_METER : C_DIM,
             mix565(C_CARD, C_METER, 60U), C_CARD);
        if (i + 1U < n) {
            const unsigned nx = x0 + (i + 1U) * (w - 1U) / (n - 1U);
            const unsigned nup = order[i + 1U] * (h - 14U) / span_top + 6U;
            pen((int)x, (int)y, (int)nx, (int)(axis - nup), 2U,
                on ? mix565(C_CARD, C_AMBER, 90U) : C_LINE, C_CARD);
        }
    }

    draw_arp_keys();

    /* the rate and the type, as words over the frame; the stream logic when
     * the arp and the phrase both play */
    text(12U, BODY_Y + 6U, div_name(pc.division), on ? C_AMBER : C_DIM, C_CARD, 40U);
    {
        static const char *const logic[ORCH_PERF_LOGIC_COUNT] = {
            "OR", "AND", "XOR", "NOR", "ALT", "S/R"
        };
        const char *word = !pc.arp_enabled ? (pc.phrase_enabled ? "PHRASE" : "OFF")
                         : pc.phrase_enabled ? logic[pc.logic % ORCH_PERF_LOGIC_COUNT]
                         : arp_type_name(type);
        text(SW - 12U - span(word) - 6U, BODY_Y + 6U, word, C_DIM, C_CARD, 60U);
    }
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
