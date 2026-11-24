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
    ui.buttons = 0U;
    ui.keys = 0U;
    ui.knob = 0U;
    ui.hot_cell = 4U;
    ui.chord_left = 0U;
    ui.chord_off_at = 0U;
    ui.hot_until = 0U;
    memset(enc_at, 0, sizeof enc_at);
    memset(enc_way, 0, sizeof enc_way);
    memset(enc_speed, 0, sizeof enc_speed);
    orch_ui_master_q23 = 0U;   /* silent until the knob says otherwise */
    orch_stage_reset();
    next_scan = 0U;
    memset(shown_voices, 0xFF, sizeof shown_voices);
    memset(seq_notes, 0, sizeof seq_notes);

    fm1_controls_init();
    fm1_screen_init();
    fm1_screen_fill(0, 0, SW, SW, C_BG);
    load_state();
    draw_title();
    draw_page();
    drawn_signature = page_signature();
    drawn_live = page_live_signature();
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
        /* First press opens the FX page; on that page the button is still the
         * four-part sound check it was before. */
        if (ui.page == PAGE_FX) play_chord();
        else ui.page = PAGE_FX;
        dirty = 1;
    }
    if (pressed & (1UL << B_SCL)) {
        ui.scale = (uint8_t)((ui.scale + 1U) % SCALE_COUNT);
        dirty = 1;
    }
    if (pressed & (1UL << B_ENV)) { step_art(-1); dirty = 1; }
    if (pressed & (1UL << B_LFO)) { step_art(1); dirty = 1; }
    if (pressed & (1UL << B_EDIT)) { ui.page = PAGE_HOME; dirty = 1; }
    if (pressed & (1UL << B_GLO)) { ui.page = PAGE_GLO; dirty = 1; }
    if (pressed & (1UL << B_HOME)) { ui.page = PAGE_HOME; dirty = 1; }
    if (pressed & (1UL << B_SAVE)) { save_state(); dirty = 1; }
    if (pressed & (1UL << B_ARP)) {
        ui.page = PAGE_ARP;
        orch_arp_set_enabled(ui.part, !orch_arp_enabled(ui.part));
        dirty = 1;
    }
    if (pressed & (1UL << B_SEQ)) { ui.page = PAGE_SEQ; dirty = 1; }
    if (pressed & (1UL << B_PLAY)) {
        ui.seq_on = (uint8_t)!ui.seq_on;
        orch_seq_set_enabled(ui.seq_on);
        orch_seq_set_part(ui.part);
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
        switch (e) {
        case E_SELECT: {
            int p = (int)ui.part + step;
            while (p < 0) p += (int)ORCH_PARTS;
            while (p >= (int)ORCH_PARTS) p -= (int)ORCH_PARTS;
            ui.part = (uint8_t)p;
            dirty = 1;
            break;
        }
        case E_PRESET:
            step_instrument(step);
            dirty = 1;
            break;
        case E_ALGO:
            switch (ui.page) {
            case PAGE_FX: {
                int s = (int)ui.fx_sel + step;
                while (s < 0) s += (int)ORCH_FX_SLOT_COUNT;
                while (s >= (int)ORCH_FX_SLOT_COUNT) s -= (int)ORCH_FX_SLOT_COUNT;
                ui.fx_sel = (uint8_t)s;
                break;
            }
            case PAGE_ARP: {
                int m = (int)orch_arp_mode_of(ui.part) + step;
                while (m < 0) m += (int)ORCH_ARP_MODE_COUNT;
                while (m >= (int)ORCH_ARP_MODE_COUNT) m -= (int)ORCH_ARP_MODE_COUNT;
                orch_arp_set_mode(ui.part, (unsigned)m);
                break;
            }
            case PAGE_SEQ: {
                int s = (int)ui.seq_step + step;
                while (s < 0) s += (int)ORCH_SEQ_STEPS;
                while (s >= (int)ORCH_SEQ_STEPS) s -= (int)ORCH_SEQ_STEPS;
                ui.seq_step = (uint8_t)s;
                break;
            }
            case PAGE_GLO: {
                int sc = (int)ui.scale + step;
                while (sc < 0) sc += SCALE_COUNT;
                while (sc >= SCALE_COUNT) sc -= SCALE_COUNT;
                ui.scale = (uint8_t)sc;
                break;
            }
            default: {
                int f = (int)part->family + step;
                while (f < 0) f += (int)ORCH_FAM_COUNT;
                while (f >= (int)ORCH_FAM_COUNT) f -= (int)ORCH_FAM_COUNT;
                set_family((unsigned)f);
                break;
            }
            }
            dirty = 1;
            break;
        case E_KNOB1:
            if (ui.page == PAGE_FX) {
                orch_fx_set_mix(ui.fx_sel,
                    (unsigned)clamp_percent((int)orch_fx_mix(ui.fx_sel) + pct_delta(delta) * 5));
            } else if (ui.page == PAGE_ARP) {
                int d = (int)orch_arp_div_of(ui.part) + step;
                if (d < 0) d = 0;
                if (d > 4) d = 4;
                orch_arp_set_division(ui.part, (unsigned)d);
            } else if (ui.page == PAGE_SEQ) {
                seq_touch_slot(0U, 0, pct_delta(delta) * 5, 0);
            } else {
                int cap = (int)part->player_cap + step;
                if (cap < 1) cap = 1;
                if (cap > 4) cap = 4;
                orch_engine_set_player_cap(ui.part, (unsigned)cap);
            }
            dirty = 1;
            break;
        case E_KNOB2:
            if (ui.page == PAGE_FX) {
                orch_fx_set_param_a(ui.fx_sel,
                    (unsigned)clamp_percent((int)orch_fx_param_a(ui.fx_sel) + pct_delta(delta) * 5));
            } else if (ui.page == PAGE_ARP) {
                int o = (int)orch_arp_octaves_of(ui.part) + step;
                if (o < 1) o = 1;
                if (o > 3) o = 3;
                orch_arp_set_octaves(ui.part, (unsigned)o);
            } else if (ui.page == PAGE_SEQ) {
                seq_touch_slot(0U, 0, 0, pct_delta(delta) * 5);
            } else if (ui.page == PAGE_GLO) {
                int t = (int)ui.transpose + delta;
                if (t < -24) t = -24;
                if (t > 24) t = 24;
                ui.transpose = (int8_t)t;
            } else {
                int c = (int)part->voice_ceiling + delta;
                if (c < 1) c = 1;
                if (c > (int)ORCH_PART_VOICE_CEIL) c = (int)ORCH_PART_VOICE_CEIL;
                orch_engine_set_voice_ceiling(ui.part, (unsigned)c);
            }
            dirty = 1;
            break;
        case E_KNOB3:
            if (ui.page == PAGE_FX) {
                orch_fx_set_param_b(ui.fx_sel,
                    (unsigned)clamp_percent((int)orch_fx_param_b(ui.fx_sel) + pct_delta(delta) * 5));
            } else if (ui.page == PAGE_ARP) {
                int g = (int)orch_arp_gate_of(ui.part) + pct_delta(delta) * 5;
                orch_arp_set_gate(ui.part, (unsigned)g);
            } else if (ui.page == PAGE_GLO || ui.page == PAGE_SEQ) {
                int bpm = (int)ui.tempo + delta * 2;
                if (bpm < 30) bpm = 30;
                if (bpm > 300) bpm = 300;
                ui.tempo = (uint16_t)bpm;
                orch_seq_set_tempo(ui.tempo);
            } else {
                /* the parts page is where the listener sits: KNOB3 moves it
                 * across the stage, 5 a detent and at most 10 when flicked */
                orch_stage_set_listener(
                    orch_stage_listener_x() + pct_delta(delta) * 5,
                    (int)orch_stage_listener_depth());
            }
            dirty = 1;
            break;
        case E_KNOB4:
            if (ui.page == PAGE_FX)
                orch_fx_set_enabled(ui.fx_sel, !orch_fx_enabled(ui.fx_sel));
            else if (ui.page == PAGE_SEQ) seq_clear_step();
            else if (ui.page == PAGE_GLO) { orch_panic(); orch_fx_reset(); }
            else if (ui.page == PAGE_ARP) {
                int m = (int)orch_arp_mode_of(ui.part) + step;
                while (m < 0) m += (int)ORCH_ARP_MODE_COUNT;
                while (m >= (int)ORCH_ARP_MODE_COUNT) m -= (int)ORCH_ARP_MODE_COUNT;
                orch_arp_set_mode(ui.part, (unsigned)m);
            } else {
                /* KNOB4 is how far back the listener sits: back narrows the
                 * stage, front opens it up again */
                orch_stage_set_listener(orch_stage_listener_x(),
                    (int)orch_stage_listener_depth() + pct_delta(delta) * 5);
            }
            dirty = 1;
            break;
        default: break;
        }
    }

    /* the knob is the master level, independent of the page */
    if (ui.knob != controls->knob) {
        const uint32_t k = controls->knob;
        const uint32_t full = 1023U * 1023U;
        ui.knob = (uint16_t)k;
        orch_ui_master_q23 = (uint32_t)(((uint64_t)k * k * UNITY_Q23) / full);
    }

    if (dirty) {
        draw_title();
        draw_page();
        drawn_signature = page_signature();
        drawn_live = page_live_signature();
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

    if (ui.chord_left && (int32_t)(now - ui.chord_off_at) >= 0) release_chord();
    update_meters();

    /* A setting of the page changed: draw it. Otherwise only what moves on its
     * own is repainted — a sequencer step, the arp's held keys, a part starting
     * or stopping. Both are compared every scan, so a page never lags. */
    const uint32_t sig = page_signature();
    const uint32_t live = page_live_signature();
    if (sig != drawn_signature) {
        drawn_signature = sig;
        drawn_live = live;
        draw_page();
    } else if (live != drawn_live) {
        page_live(drawn_live);
        drawn_live = live;
    }
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
