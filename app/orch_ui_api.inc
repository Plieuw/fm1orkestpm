/* --- entry points --------------------------------------------------------- */

void orch_ui_init(void)
{
    ui.page = PAGE_HOME;
    ui.part = 0U;
    ui.tempo = 120U;
    ui.seq_on = 0U;
    ui.rec = 0U;
    ui.seq_step = 0U;
    ui.scale = SCALE_OFF;
    ui.transpose = 0;
    ui.saved = 0U;
    ui.fx_sel = 0U;
    ui.fx_bank = 0U;
    ui.scene = 0U;
    ui.arp_bank = 0U;
    ui.seq_bank = 0U;
    ui.buttons = 0U;
    ui.keys = 0U;
    ui.knob = 0U;
    ui.hot_cell = 4U;
    ui.chord_left = 0U;
    ui.chord_off_at = 0U;
    ui.hot_until = 0U;
    ui_fill(enc_at, 0U, (unsigned)sizeof enc_at);
    ui_fill(enc_way, 0U, (unsigned)sizeof enc_way);
    ui_fill(enc_speed, 0U, (unsigned)sizeof enc_speed);
    orch_ui_master_q23 = 0U;   /* silent until the knob says otherwise */
    orch_stage_reset();
    next_scan = 0U;
    ui_fill(shown_voices, 0xFFU, (unsigned)sizeof shown_voices);
    ui_fill(seq_lanes, 0U, (unsigned)sizeof seq_lanes);

    fm1_controls_init();
    fm1_screen_init();
    fm1_screen_fill(0, 0, SW, SW, C_BG);
    apply_tempo(ui.tempo);
    load_state();   /* before the first block, on every platform */
    draw_title();
    draw_page();
    drawn_signature = page_signature();
    drawn_live = page_live_signature();
    drawn_strip = strip_signature();
    update_leds();
}

void orch_ui_handle(const struct fm1_controls *controls)
{
    if (!controls) return;

    const uint32_t pressed = controls->buttons & ~ui.buttons;
    const uint32_t key_down = controls->keys & ~ui.keys;
    int dirty = 0;

    ui.buttons = controls->buttons;
    ui.keys = controls->keys;

    /* Every edit below writes settings the renderer reads, and some reset
     * state it walks (a part's divisi, the ARP's held notes): they are one
     * transaction against the audio interrupt. Drawing comes after. */
    fm1_engine_pause();

    if (ui.rec && ui.page == PAGE_SEQ) {
        for (unsigned k = 0; k < 27U; ++k)
            if (key_down & (1UL << k)) { seq_write_note(53U + k); dirty = 1; }
    }

    if (pressed & (1UL << B_OCT_DOWN)) {
        if (ui.transpose > -24) ui.transpose = (int8_t)(ui.transpose - 12);
        dirty = 1;
    }
    if (pressed & (1UL << B_OCT_UP)) {
        if (ui.transpose < 24) ui.transpose = (int8_t)(ui.transpose + 12);
        dirty = 1;
    }
    if (pressed & (1UL << B_FX)) {
        /* First press opens the FX page; on that page the button is the
         * eight-part sound check. */
        if (ui.page == PAGE_FX) play_chord();
        else ui.page = PAGE_FX;
        dirty = 1;
    }
    if (pressed & (1UL << B_SCL)) {
        ui.scale = (uint8_t)((ui.scale + 1U) % SCALE_COUNT);
        dirty = 1;
    }
    if (pressed & (1UL << B_ENV)) { ui.page = PAGE_ENV; dirty = 1; }
    if (pressed & (1UL << B_LFO)) { ui.page = PAGE_LFO; dirty = 1; }
    if (pressed & (1UL << B_EDIT)) { ui.page = PAGE_HOME; dirty = 1; }
    if (pressed & (1UL << B_GLO)) { ui.page = PAGE_GLO; dirty = 1; }
    if (pressed & (1UL << B_HOME)) { ui.page = PAGE_HOME; dirty = 1; }
    if (pressed & (1UL << B_SAVE)) { ui.save_req = 1U; dirty = 1; }
    if (pressed & (1UL << B_ARP)) {
        /* first press opens the page, a press on it turns the part's ARP on
         * or off (the phrase has its own knob) */
        if (ui.page == PAGE_ARP) {
            struct orch_perf_config pc = perf_of(ui.part);
            pc.arp_enabled = (uint8_t)!pc.arp_enabled;
            perf_put(ui.part, &pc);
        } else {
            ui.page = PAGE_ARP;
        }
        dirty = 1;
    }
    if (pressed & (1UL << B_SEQ)) { ui.page = PAGE_SEQ; dirty = 1; }
    if (pressed & (1UL << B_PLAY)) {
        ui.seq_on = (uint8_t)!ui.seq_on;
        orch_seq_set_part(ui.part);
        orch_seq_set_enabled(ui.seq_on);
        dirty = 1;
    }
    if (pressed & (1UL << B_REC)) { ui.rec = (uint8_t)!ui.rec; dirty = 1; }

    for (unsigned e = 0; e < 7U; ++e) {
        struct orch_part *part = orch_part_get(ui.part);
        int delta = controls->encoders[e];
        if (!part || !delta) continue;
        delta = enc_accel(e, delta);
        /* A menu entry is one detent a step: a quick turn must never skip an
         * entry, so only the ranges below take the accelerated count, and the
         * percentages only half of it (pct_delta). */
        const int step = delta > 0 ? 1 : -1;
        if (e >= E_KNOB1 && e <= E_KNOB4) {
            ui.hot_cell = (uint8_t)(e - E_KNOB1);
            ui.hot_until = fm1_millis() + 1000U;
        }
        const struct orch_perf_config pc_was = perf_of(ui.part);
        struct orch_perf_config pc = pc_was;
        switch (e) {
        case E_SELECT:
            ui.part = (uint8_t)step_wrap((int)ui.part, step, (int)ORCH_PARTS);
            break;
        case E_PRESET:
            /* on FX this encoder turns the strip over to the effect's other two
             * parameters; everywhere else it steps the instrument */
            if (ui.page == PAGE_FX) ui.fx_bank = (uint8_t)!ui.fx_bank;
            else if (ui.page == PAGE_SEQ)    /* the step, or the pattern */
                ui.seq_bank = (uint8_t)!ui.seq_bank;
            else if (ui.page == PAGE_ARP)    /* the ARP page's five banks */
                ui.arp_bank = (uint8_t)step_wrap((int)ui.arp_bank, step, (int)ARP_BANKS);
            else step_instrument(step);
            break;
        case E_ALGO:
            switch (ui.page) {
            case PAGE_FX:
                ui.fx_sel = (uint8_t)step_wrap((int)ui.fx_sel, step, (int)ORCH_FX_SLOT_COUNT);
                break;
            case PAGE_ARP:   /* how the ARP and the phrase share the notes */
                pc.logic = (uint8_t)step_wrap((int)pc.logic, step, (int)ORCH_PERF_LOGIC_COUNT);
                break;
            case PAGE_SEQ:
                ui.seq_step = (uint8_t)step_wrap((int)ui.seq_step, step, (int)orch_seq_length());
                break;
            case PAGE_GLO:
                ui.scale = (uint8_t)step_wrap((int)ui.scale, step, SCALE_COUNT);
                break;
            default:
                set_family((unsigned)step_wrap((int)part->family, step, (int)ORCH_FAM_COUNT));
                break;
            }
            break;
        case E_KNOB1:
            switch (ui.page) {
            case PAGE_FX:
                orch_fx_set_mix(ui.fx_sel,
                    (unsigned)clamp_percent((int)orch_fx_mix(ui.fx_sel) + pct_delta(delta) * 5));
                break;
            case PAGE_ARP: arp_knob(&pc, 0U, delta, step); break;
            case PAGE_SEQ:
                if (ui.seq_bank)
                    orch_seq_set_length((unsigned)step_range((int)orch_seq_length(), delta, 1, 16));
                else seq_touch_slot(0, delta, 0, 0);              /* the note */
                break;
            case PAGE_GLO:
                ui.scale = (uint8_t)step_wrap((int)ui.scale, step, SCALE_COUNT);
                break;
            case PAGE_ENV: adsr_edit(delta, 0, 0, 0); break;
            case PAGE_LFO: lfo_edit(pct_delta(delta) * 5, 0, 0, 0); break;
            default:
                orch_engine_set_player_cap(ui.part,
                    (unsigned)step_range((int)part->player_cap, step, 1, 4));
                break;
            }
            break;
        case E_KNOB2:
            switch (ui.page) {
            case PAGE_FX: {
                const unsigned i = ui.fx_bank ? 2U : 0U;
                orch_fx_set_param(ui.fx_sel, i,
                    (unsigned)clamp_percent((int)orch_fx_param(ui.fx_sel, i) + pct_delta(delta) * 5));
                break;
            }
            case PAGE_ARP: arp_knob(&pc, 1U, delta, step); break;
            case PAGE_SEQ:
                if (ui.seq_bank)
                    apply_tempo((unsigned)step_range((int)ui.tempo, delta * 2, 40, 250));
                else seq_touch_slot(0, 0, pct_delta(delta) * 5, 0); /* velocity */
                break;
            case PAGE_GLO:
                ui.transpose = (int8_t)step_range((int)ui.transpose, delta, -24, 24);
                break;
            case PAGE_ENV: adsr_edit(0, delta, 0, 0); break;
            case PAGE_LFO: lfo_edit(0, delta, 0, 0); break;
            default: step_art(step); break;
            }
            break;
        case E_KNOB3:
            switch (ui.page) {
            case PAGE_FX: {
                const unsigned i = ui.fx_bank ? 3U : 1U;
                orch_fx_set_param(ui.fx_sel, i,
                    (unsigned)clamp_percent((int)orch_fx_param(ui.fx_sel, i) + pct_delta(delta) * 5));
                break;
            }
            case PAGE_ARP: arp_knob(&pc, 2U, delta, step); break;
            case PAGE_SEQ:
                if (ui.seq_bank) orch_seq_set_lane_muted(ui.part, step < 0);
                else seq_touch_slot(0, 0, 0, pct_delta(delta) * 5); /* gate */
                break;
            case PAGE_GLO:
                apply_tempo((unsigned)step_range((int)ui.tempo, delta * 2, 40, 250));
                break;
            case PAGE_ENV: adsr_edit(0, 0, pct_delta(delta) * 5, 0); break;
            case PAGE_LFO: lfo_edit(0, 0, step, 0); break;
            default:
                /* the stage page is where the listener sits: KNOB3 moves it
                 * across the stage, 5 a detent and at most 10 when flicked */
                orch_stage_set_listener(
                    orch_stage_listener_x() + pct_delta(delta) * 5,
                    (int)orch_stage_listener_depth());
                break;
            }
            break;
        case E_KNOB4:
            switch (ui.page) {
            case PAGE_FX:
                orch_fx_set_enabled(ui.fx_sel, !orch_fx_enabled(ui.fx_sel));
                break;
            case PAGE_SEQ:
                if (ui.seq_bank) seq_clear_step();
                else seq_touch_slot(pct_delta(delta) * 5, 0, 0, 0); /* chance */
                break;
            case PAGE_GLO:
                /* a scene is recalled as soon as it is chosen, as the PRESETS
                 * encoder did on the 0.3.14 panel; SAVE keeps the result */
                recall_scene((unsigned)step_wrap((int)ui.scene, step, (int)ORCH_SCENE_COUNT));
                break;
            case PAGE_ARP: arp_knob(&pc, 3U, delta, step); break;
            case PAGE_ENV: adsr_edit(0, 0, 0, delta); break;
            case PAGE_LFO: lfo_edit(0, 0, 0, step); break;
            default:
                /* KNOB4 is how far back the listener sits: back narrows the
                 * stage, front opens it up again */
                orch_stage_set_listener(orch_stage_listener_x(),
                    (int)orch_stage_listener_depth() + pct_delta(delta) * 5);
                break;
            }
            break;
        default: break;
        }
        if (perf_differs(&pc, &pc_was)) perf_put(ui.part, &pc);
        dirty = 1;
    }
    fm1_engine_resume();

    /* the knob is the master level, independent of the page */
    if (ui.knob != controls->knob) {
        const uint32_t k = controls->knob;
        const uint32_t full = 1023U * 1023U;
        ui.knob = (uint16_t)k;
        /* k^2 * 2^23 / 1023^2 without a 64-bit divide (none on the PI32v2):
         * 2^23 / 1023^2 = 8.0157 = 8 + 33/2048 */
        uint32_t g = k * k * 8U + (k * k * 33U) / 2048U;
        if (g > UNITY_Q23) g = UNITY_Q23;
        (void)full;
        orch_ui_master_q23 = g;
    }

    /* the flash write is the one slow thing a button does: outside the
     * transaction, so the audio keeps running while the sector is written */
    if (ui.save_req) { ui.save_req = 0U; save_state(); }

    if (dirty) {
        draw_title();
        draw_body();
        patch_strip();
        drawn_signature = page_signature();
        drawn_live = page_live_signature();
        drawn_strip = strip_signature();
    }
    update_leds();
}

void orch_ui_control(void)
{
    const uint32_t now = fm1_millis();
    if ((uint32_t)(now - next_scan) < UI_IDLE_MS) return;
    next_scan = now;

    struct fm1_controls controls;
    fm1_controls_read(&controls);
    orch_ui_handle(&controls);

    if (ui.chord_left && (int32_t)(now - ui.chord_off_at) >= 0) {
        fm1_engine_pause();
        release_chord();
        fm1_engine_resume();
    }
    update_meters();

    /* Three things can change: the page's settings, the values that move on
     * their own, and the strip. Each has its own cost — a page body, a couple
     * of cells, or one knob. */
    const uint32_t sig = page_signature();
    const uint32_t live = page_live_signature();
    const uint32_t strip = strip_signature();
    if (sig != drawn_signature) {
        drawn_signature = sig;
        drawn_live = live;
        drawn_strip = strip;
        draw_body();
        draw_strip();
    } else if (live != drawn_live) {
        page_live(drawn_live);
        drawn_live = live;
    } else if (strip != drawn_strip) {
        page_knobs();
        patch_strip();
        drawn_strip = strip;
    }
}

/* Someone else had the screen (the firmware's update countdown): paint the
 * whole panel again, the hall and the pictures included. */
void orch_ui_repaint(void)
{
    hall_static = 0;
    page_pic_page = 0xFFU;
    page_pic_static = 0;
    fm1_screen_fill(0, 0, SW, SW, C_BG);
    draw_title();
    draw_page();
    drawn_signature = page_signature();
    drawn_live = page_live_signature();
    drawn_strip = strip_signature();
    update_leds();
}

unsigned orch_ui_part(void) { return ui.part; }
unsigned orch_ui_tempo(void) { return ui.tempo; }
unsigned orch_ui_page(void) { return ui.page; }
unsigned orch_ui_transpose(void) { return (unsigned)(int)ui.transpose; }
unsigned orch_ui_scale(void) { return ui.scale; }
unsigned orch_ui_recording(void) { return ui.rec; }
unsigned orch_ui_seq_step(void) { return ui.seq_step; }
unsigned orch_ui_saved(void) { return ui.saved; }
unsigned orch_ui_fx_selection(void) { return ui.fx_sel; }
