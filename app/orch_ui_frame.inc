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

static void draw_page(void)
{
    fm1_screen_fill(0, TITLE_H, SW, STRIP_Y - 1U - TITLE_H, C_BG);
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
