/* --- the frame ------------------------------------------------------------ */

/* The top of every page: its name in amber on the case, the part and the tempo
 * at the right, and a bar of five segments that says which page is open. */
static void draw_title(void)
{
    char right[16];
    fm1_screen_fill(0, 0, SW, TITLE_H, C_BG);
    text(8U, 5U, page_name(ui.page), C_AMBER, C_BG, SW - 96U);

    unsigned n = put(right, 0, sizeof right, "P");
    n = put_num(right, n, sizeof right, ui.part + 1U, 1U);
    n = put(right, n, sizeof right, "  ");
    n = put_num(right, n, sizeof right, ui.tempo, 3U);
    text(SW - 8U - span(right), 5U, right, C_DIM, C_BG, span(right));

    for (unsigned p = 0; p < PAGE_COUNT; ++p) {
        const unsigned x = 8U + p * 26U;
        fm1_screen_fill(x, 18U, 20U, 3U, p == ui.page ? C_AMBER : C_LINE);
    }
}

/* The page's own content: the knob cells first (the strip reads them), then
 * whatever the page draws. On the stage page the photograph is left alone once
 * it is on screen — a knob turn there repaints the lamps, the players and the
 * marks, not the hall. */
static void draw_body(void)
{
    page_knobs();
    if (ui.page == PAGE_HOME) {
        if (!hall_static) {
            fm1_screen_fill(0, TITLE_H, SW, STRIP_Y - 1U - TITLE_H, C_BG);
            page_parts_full();
            hall_static = 1;
        } else {
            page_parts();
        }
        page_pic_static = 0;     /* the hall is not a menu's picture: a page
                                  * entered again draws its band again */
        return;
    }
    hall_static = 0;            /* this page paints over the hall */
    /* A menu's picture is drawn when the page is entered and left alone after
     * that, the way the hall is: a knob turn repaints the values over the
     * picture, and the picture itself is not painted again. */
    if (page_pic_page != ui.page) {
        page_pic_page = ui.page;
        page_pic_static = 0;
    }
    if (!page_pic_static) {
        fm1_screen_fill(0, TITLE_H, SW, STRIP_Y - 1U - TITLE_H, C_BG);
        draw_page_picture(ui.page);
        page_pic_static = 1;
    }
    switch (ui.page) {
    case PAGE_ARP: page_arp(); break;
    case PAGE_SEQ: page_seq(); break;
    case PAGE_GLO: page_global(); break;
    case PAGE_FX: page_fx(); break;
    case PAGE_ENV: page_env(); break;
    case PAGE_LFO: page_lfo(); break;
    case PAGE_HOME:
    default: break;
    }
}

static void draw_page(void)
{
    draw_body();
    draw_strip();
}

/* The strip has values of its own: the four cells, and which one was turned
 * last. It is compared apart from the page, so the highlight that follows a
 * knob can fade without repainting the page behind it. */
static uint32_t strip_signature(void)
{
    uint32_t sig = ((int32_t)(fm1_millis() - ui.hot_until) < 0)
                       ? 1U | ((uint32_t)ui.hot_cell << 4)
                       : 0U;
    for (unsigned c = 0; c < 4U; ++c) {
        sig = sig * 131U + cells[c].pct + 257U * cells[c].lit;
        for (const char *v = cells[c].value; *v; ++v)
            sig = sig * 33U + (uint8_t)*v;
    }
    return sig;
}

/* The pages show live values (a running step, held arp notes) next to values
 * that only change when a control does. The control task compares them apart:
 * a live change repaints the few pixels it moves, and only a change to the
 * page's own settings repaints the page. */

static uint32_t page_signature(void)
{
    uint32_t sig = (uint32_t)ui.page * 0x9E3779B1UL;
    switch (ui.page) {
    case PAGE_ARP: {
        const struct orch_perf_config pc = perf_of(ui.part);
        sig = sig * 31U + ui.part;
        sig = sig * 31U + pc.arp_enabled + 2U * pc.phrase_enabled;
        sig = sig * 31U + pc.arp_type;
        sig = sig * 31U + pc.division;
        sig = sig * 31U + pc.gate;
        sig = sig * 31U + pc.logic;
        sig = sig * 31U + pc.phrase_ge;
        sig = sig * 31U + ui.arp_bank;
        {
            const uint8_t *b = (const uint8_t *)&pc;
            for (unsigned i = 0; i < sizeof pc; ++i) sig = sig * 31U + b[i];
        }
        break;
    }
    case PAGE_ENV:
    case PAGE_LFO: {
        const struct orch_part *part = orch_part_get(ui.part);
        sig = sig * 31U + ui.part;
        sig = sig * 31U + part->instrument;
        sig = sig * 31U + part->adsr.attack_ms;
        sig = sig * 31U + part->adsr.decay_ms;
        sig = sig * 31U + part->adsr.sustain_percent;
        sig = sig * 31U + part->adsr.release_ms;
        sig = sig * 31U + part->lfo.wave + 8U * part->lfo.destinations;
        sig = sig * 31U + part->lfo.depth;
        break;
    }
    case PAGE_SEQ:
        sig ^= (uint32_t)ui.seq_on << 9;
        sig ^= (uint32_t)ui.rec << 10;
        sig ^= (uint32_t)ui.seq_step << 12;
        sig ^= (uint32_t)seq_notes[ui.seq_step][0].velocity << 16;
        sig ^= (uint32_t)seq_notes[ui.seq_step][0].gate_percent << 24;
        sig = sig * 31U + ui.part;
        sig = sig * 31U + ui.seq_bank + 2U * orch_seq_lane_muted(ui.part);
        sig = sig * 31U + orch_seq_length();
        sig = sig * 31U + seq_notes[ui.seq_step][0].note;
        sig = sig * 31U + seq_notes[ui.seq_step][0].chance;
        break;
    case PAGE_FX:
        for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s) {
            sig = sig * 31U + (uint32_t)orch_fx_enabled(s) + 3U * orch_fx_mix(s);
            for (unsigned i = 0; i < ORCH_FX_PARAMS; ++i)
                sig = sig * 31U + (uint32_t)(i + 5U) * orch_fx_param(s, i);
        }
        sig = sig * 31U + ui.fx_sel + 8U * ui.fx_bank;
        break;
    case PAGE_HOME:
    default:
        sig = sig * 31U + (uint32_t)(orch_stage_listener_x() + 128);
        sig = sig * 31U + orch_stage_listener_depth();
        break;
    }
    return sig;
}

static uint32_t page_live_signature(void)
{
    switch (ui.page) {
    case PAGE_ARP: return (uint32_t)orch_perf_held(ui.part) | 0x400U;
    case PAGE_SEQ: return (uint32_t)orch_seq_position() | 0x400U;
    case PAGE_HOME: {
        /* the voices as four nibbles, which of the hall's frames is on screen,
         * and the output the light in front of the boards follows — all 24
         * steps of it, in bits 24..28: the lights of a playing hall change on
         * their own */
        uint32_t sig = 0x400U | ((uint32_t)(stage_frame() + 1U) << 20);
        sig |= ((uint32_t)orch_output_level() & 0x1FU) << 24;
        /* the 24 bits of the eight parts' voices, folded into the 19 bits
         * the frame, the light and the 0x400 mark leave free */
        const uint32_t v = stage_voices();
        return sig | ((v ^ (v >> 19) * 0x9E3U) & 0xFFBFFU);
    }
    default: return 0x400U;
    }
}

/* What a live value costs: the arp's row of dots, the two sequencer cells that
 * changed (the one that was playing and the one that is), or on the stage the
 * players and the lamps — when the hall's own lights step, the photograph under
 * them — and, when the output moves, the light in front of the boards.
 * Everything else stays as it was drawn. */
static void page_live(uint32_t was)
{
    switch (ui.page) {
    case PAGE_ARP:
        draw_arp_keys();
        break;
    case PAGE_SEQ: {
        const unsigned now = orch_seq_position();
        const unsigned before = (was != 0x400U) ? (unsigned)(was & 0x3FFU)
                                                : now;
        if (before != now) {
            draw_step_cell(before % ORCH_SEQ_STEPS);
            draw_step_cell(now % ORCH_SEQ_STEPS);
        }
        break;
    }
    case PAGE_HOME: {
        /* three things move here, each with its own cost: the hall's lights
         * (the band), a part starting or stopping (the lamps and the players),
         * and what comes out (the light in front of the boards) */
        const unsigned f = stage_frame();
        const uint32_t voices = stage_voices();
        if (((was >> 20) & 0xFU) != (unsigned)(f + 1U)) {
            page_parts_photo(f);
            drawn_voices = voices;
            draw_stage_light(1);
        } else if (drawn_voices != voices) {
            drawn_voices = voices;
            page_parts();
            draw_stage_light(1);
        } else {
            draw_stage_light(0);
        }
        break;
    }
    default:
        break;
    }
}

static void update_meters(void)
{
    if (ui.page != PAGE_HOME) return;
    int players = 0;
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const unsigned ceiling = 4U;   /* as draw_desk() */
        unsigned voices = orch_voices_of_part(p);
        if (voices > ceiling) voices = ceiling;
        if (voices != shown_voices[p]) { draw_desk(p); players = 1; }
    }
    if (players) draw_players();
}
