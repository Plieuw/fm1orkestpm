/* --- edits ---------------------------------------------------------------- */

static void set_family(unsigned family)
{
    struct orch_part *part = orch_part_get(ui.part);
    if (!part || family >= ORCH_FAM_COUNT) return;
    orch_engine_configure_part(ui.part, family, part->channels);
}

static void step_instrument(int delta)
{
    const struct orch_part *part = orch_part_get(ui.part);
    if (!part || !delta) return;
    const unsigned first = family_first(part->family);
    const unsigned count = family_count(part->family);
    int index = (int)(part->instrument - first) + delta;
    while (index < 0) index += (int)count;
    while (index >= (int)count) index -= (int)count;
    orch_engine_set_instrument(ui.part, first + (unsigned)index);
}

static void step_art(int delta)
{
    const struct orch_part *part = orch_part_get(ui.part);
    if (!part || !delta) return;
    int art = (int)part->articulation + delta;
    while (art < 0) art += (int)ORCH_ART_COUNT;
    while (art >= (int)ORCH_ART_COUNT) art -= (int)ORCH_ART_COUNT;
    orch_engine_set_articulation(ui.part, (unsigned)art);
}

/* One tempo for the whole instrument: the sequencer, the echo's SYNC and every
 * part's ARP/phrase clock (an external MIDI clock still overrides the last). */
static void apply_tempo(unsigned bpm)
{
    if (bpm < 40U) bpm = 40U;
    if (bpm > 250U) bpm = 250U;
    ui.tempo = (uint16_t)bpm;
    orch_seq_set_tempo(ui.tempo);
    orch_fx_set_tempo(ui.tempo);
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        struct orch_perf_config pc = perf_of(p);
        if (pc.bpm != ui.tempo) { pc.bpm = ui.tempo; perf_put(p, &pc); }
    }
}

static int step_range(int v, int delta, int lo, int hi)
{
    v += delta;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

static int step_wrap(int v, int delta, int count)
{
    v += delta;
    while (v < 0) v += count;
    while (v >= count) v -= count;
    return v;
}

/* A time in milliseconds by a knob: fine at the short end, coarse at the long
 * one, so 1 ms and 4 s are both a few detents away. */
static int step_ms(int v, int delta, int lo, int hi)
{
    const int mag = v < 20 ? 1 : v < 100 ? 5 : v < 500 ? 20 : v < 2000 ? 100 : 250;
    return step_range(v, delta * mag, lo, hi);
}

static void adsr_edit(int da, int dd, int ds, int dr)
{
    const struct orch_part *part = orch_part_get(ui.part);
    if (!part) return;
    orch_engine_set_adsr(ui.part,
        (unsigned)step_ms((int)part->adsr.attack_ms, da, 1, 4000),
        (unsigned)step_ms((int)part->adsr.decay_ms, dd, 1, 4000),
        (unsigned)step_range((int)part->adsr.sustain_percent, ds, 0, 100),
        (unsigned)step_ms((int)part->adsr.release_ms, dr, 1, 6000));
}

static void lfo_edit(int ddepth, int drate, int dwave, int ddest)
{
    const struct orch_part *part = orch_part_get(ui.part);
    if (!part) return;
    const struct orch_lfo *l = &part->lfo;
    unsigned dest_i = 0U;
    for (unsigned i = 0; i < 8U; ++i)
        if (lfo_dest_set[i] == l->destinations) dest_i = i;
    dest_i = (unsigned)step_wrap((int)dest_i, ddest, 8);
    orch_engine_set_lfo(ui.part,
        (unsigned)step_wrap((int)l->wave, dwave, 5),
        lfo_dest_set[dest_i],
        (unsigned)step_range((int)l->depth, ddepth, 0, 100),
        (unsigned)step_range((int)l->rate_millihz, drate * 100, 100, 20000),
        l->sync_division, l->delay_ms, l->fade_ms);
}

/* A scene is recalled whole: the parts, their ARP/phrase, the FX and the motif,
 * which the SEQUENCE page then shows; the tempo comes with it. */
static void recall_scene(unsigned index)
{
    if (orch_scene_apply(index) != 0) return;
    ui.scene = (uint8_t)index;
    ui.seq_on = 0U;
    ui_fill(seq_lanes, 0U, (unsigned)sizeof seq_lanes);
    for (unsigned st = 0; st < ORCH_SEQ_STEPS; ++st)
        orch_scene_step(index, st, &seq_lanes[orch_scene_seq_part(index) % ORCH_PARTS][st][0]);
    apply_tempo(orch_scene_bpm(index));
}

/* --- the ARP page's five banks ------------------------------------------ */

static int perf_differs(const struct orch_perf_config *a, const struct orch_perf_config *b)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    for (unsigned i = 0; i < sizeof *a; ++i) if (x[i] != y[i]) return 1;
    return 0;
}

static int pct_delta(int delta);

/* One knob (0..3) of the ARP page, by bank. */
static void arp_knob(struct orch_perf_config *pc, unsigned knob, int delta, int step)
{
    switch (ui.arp_bank * 4U + knob) {
    case 0: pc->arp_type = (uint8_t)step_wrap((int)pc->arp_type, step, (int)ORCH_PERF_ARP_TYPES); break;
    case 1: pc->division = (uint8_t)step_range((int)pc->division, step, 0, (int)ORCH_PERF_DIVISIONS - 1); break;
    case 2: pc->gate = (uint8_t)step_range((int)pc->gate, pct_delta(delta) * 5, 5, 100); break;
    case 3:   /* the phrase: off, then the 46 generated phrases */
        if (!pc->phrase_enabled) {
            if (step > 0) { pc->phrase_enabled = 1U; pc->phrase_ge = 0U; }
        } else {
            const int ge = (int)pc->phrase_ge + step;
            if (ge < 0) pc->phrase_enabled = 0U;
            else pc->phrase_ge = (uint8_t)(ge >= (int)ORCH_PERF_GE_COUNT ? (int)ORCH_PERF_GE_COUNT - 1 : ge);
        }
        break;
    case 4: pc->swing = (uint8_t)step_range((int)pc->swing, pct_delta(delta) * 6, 0, 127); break;
    case 5: pc->ratchet = (uint8_t)step_range((int)pc->ratchet, step, 1, 4); break;
    case 6: pc->strum = (uint8_t)step_range((int)pc->strum, pct_delta(delta) * 2, 0, 40); break;
    case 7: pc->latch = (uint8_t)(step > 0); break;
    case 8: pc->density = (uint8_t)step_range((int)pc->density, pct_delta(delta) * 6, 0, 127); break;
    case 9: pc->range = (uint8_t)step_range((int)pc->range, step, 1, 4); break;
    case 10: pc->cluster = (uint8_t)step_range((int)pc->cluster, step, 1, 4); break;
    case 11: pc->accent = (uint8_t)step_range((int)pc->accent, pct_delta(delta) * 6, 0, 127); break;
    case 12: pc->arp_order = (uint8_t)(step > 0); break;
    case 13: pc->arp_adaptive = (uint8_t)(step > 0); break;
    case 14: pc->external_clock = (uint8_t)(step > 0); break;
    case 15: pc->repeat = (uint8_t)step_range((int)pc->repeat, step, 0, 3); break;
    case 16: pc->velocity_groove = (uint8_t)step_wrap((int)pc->velocity_groove, step, 8); break;
    case 17: pc->gate_groove = (uint8_t)step_wrap((int)pc->gate_groove, step, 8); break;
    case 18: pc->quantize = (uint8_t)step_wrap((int)pc->quantize, step, 18); break;
    case 19: pc->root = (uint8_t)step_wrap((int)pc->root, step, 13); break;
    default: break;
    }
}

static int clamp_percent(int v)
{
    if (v < 0) return 0;
    if (v > 100) return 100;
    return v;
}

/* One note for each of the eight parts, each on its own channel: a chord
 * across the whole orchestra, low parts low. */
static const uint8_t chord_notes[ORCH_PARTS] = { 67U, 64U, 60U, 48U, 72U, 76U, 55U, 64U };

static void play_chord(void)
{
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        orch_note_event(p, chord_notes[p], 100U);
    ui.chord_left = ORCH_PARTS;
    ui.chord_off_at = fm1_millis() + 900U;
}

static void release_chord(void)
{
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        orch_note_event(p, chord_notes[p], 0U);
    ui.chord_left = 0U;
}

/* --- sequencer editing ---------------------------------------------------- */

static void seq_apply_step(unsigned step)
{
    orch_seq_set_part(ui.part);
    for (unsigned s = 0; s < SEQ_SLOTS; ++s)
        orch_seq_set_step(step, s, &seq_notes[step][s]);   /* lane: ui.part */
}

static void seq_write_note(unsigned note)
{
    unsigned slot = SEQ_SLOTS;
    for (unsigned s = 0; s < SEQ_SLOTS; ++s)
        if (!seq_notes[ui.seq_step][s].velocity) { slot = s; break; }
    if (slot >= SEQ_SLOTS) slot = 0U;   /* all four taken: replace the first */
    seq_notes[ui.seq_step][slot].note = (uint8_t)note;
    seq_notes[ui.seq_step][slot].velocity = 100U;
    seq_notes[ui.seq_step][slot].gate_percent = 80U;
    seq_notes[ui.seq_step][slot].chance = 100U;
    seq_apply_step(ui.seq_step);
    ui.seq_step = (uint8_t)((ui.seq_step + 1U) % orch_seq_length());
}

static void seq_clear_step(void)
{
    for (unsigned s = 0; s < SEQ_SLOTS; ++s) {
        seq_notes[ui.seq_step][s].note = 0U;
        seq_notes[ui.seq_step][s].velocity = 0U;
        seq_notes[ui.seq_step][s].gate_percent = 0U;
        seq_notes[ui.seq_step][s].chance = 100U;
    }
    seq_apply_step(ui.seq_step);
}

static void seq_touch_slot(int chance_delta, int note_delta, int vel_delta,
                           int gate_delta)
{
    struct orch_step_note *s = &seq_notes[ui.seq_step][0];
    if (!s->velocity) {
        s->note = 60U;
        s->velocity = 100U;
        s->gate_percent = 80U;
        s->chance = 100U;
    }
    if (chance_delta) {
        int c = (int)(s->chance ? s->chance : 100U) + chance_delta;
        if (c < 5) c = 5;
        if (c > 100) c = 100;
        s->chance = (uint8_t)c;
    }
    if (note_delta) {
        int note = (int)s->note + note_delta;
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        s->note = (uint8_t)note;
    }
    if (vel_delta) {
        int vel = (int)s->velocity + vel_delta;
        if (vel < 1) vel = 1;
        if (vel > 127) vel = 127;
        s->velocity = (uint8_t)vel;
    }
    if (gate_delta) {
        int gate = (int)s->gate_percent + gate_delta;
        if (gate < 5) gate = 5;
        if (gate > 100) gate = 100;
        s->gate_percent = (uint8_t)gate;
    }
    seq_apply_step(ui.seq_step);
}

/* --- save ----------------------------------------------------------------- */

static uint32_t fnv(const void *ptr, unsigned bytes)
{
    const uint8_t *b = (const uint8_t *)ptr;
    uint32_t sum = 2166136261UL;
    for (unsigned i = 0; i < bytes; ++i) {
        sum ^= b[i];
        sum *= 16777619UL;
    }
    return sum;
}

static uint32_t record_check(const struct save_record *r)
{
    return fnv(r, (unsigned)offsetof(struct save_record, check));
}

static struct save_record rec_buf;   /* not on the control task's stack */
/* the firmware keeps the record in one 3 840-byte storage object */
typedef char save_record_fits[sizeof(struct save_record) <= 3840U ? 1 : -1];

static void save_state(void)
{
    struct save_record *r = &rec_buf;
    ui_fill(r, 0U, (unsigned)sizeof *r);
    r->magic = SAVE_MAGIC;
    r->tempo = ui.tempo;
    r->transpose = ui.transpose;
    r->scale = ui.scale;
    r->seq_on = ui.seq_on;
    r->part = ui.part;
    r->scene = ui.scene;
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const struct orch_part *part = orch_part_get(p);
        struct save_part *sp = &r->parts[p];
        sp->family = part->family;
        sp->instrument = part->instrument;
        sp->articulation = part->articulation;
        sp->players = part->player_cap;
        sp->attack_ms = part->adsr.attack_ms;
        sp->decay_ms = part->adsr.decay_ms;
        sp->sustain_percent = part->adsr.sustain_percent;
        sp->release_ms = part->adsr.release_ms;
        sp->lfo_wave = part->lfo.wave;
        sp->lfo_dest = part->lfo.destinations;
        sp->lfo_depth = part->lfo.depth;
        sp->lfo_sync = part->lfo.sync_division;
        sp->lfo_rate_millihz = part->lfo.rate_millihz;
        sp->lfo_delay_ms = part->lfo.delay_ms;
        sp->lfo_fade_ms = part->lfo.fade_ms;
        r->perf[p] = perf_of(p);
    }
    for (unsigned st = 0; st < ORCH_SEQ_STEPS; ++st)
        for (unsigned sl = 0; sl < SEQ_SLOTS; ++sl)
            for (unsigned l = 0; l < ORCH_PARTS; ++l)
                r->steps[l][st][sl] = seq_lanes[l][st][sl];
    for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s) {
        r->fx_on[s] = (uint8_t)orch_fx_enabled(s);
        r->fx_mix[s] = (uint8_t)orch_fx_mix(s);
        for (unsigned i = 0; i < ORCH_FX_PARAMS; ++i)
            r->fx_p[s][i] = (uint8_t)orch_fx_param(s, i);
    }
    r->seq_length = (uint8_t)orch_seq_length();
    for (unsigned l = 0; l < ORCH_PARTS; ++l)
        r->lane_muted[l] = (uint8_t)orch_seq_lane_muted(l);
    r->listener_x = (int8_t)orch_stage_listener_x();
    r->listener_depth = (uint8_t)orch_stage_listener_depth();
    r->check = record_check(r);
    r->resonance = (uint8_t)orch_resonance_amount();
    r->res_check = fnv(&r->resonance, 4U);

    if (fm1_flash_size() < sizeof *r) return;
    if (fm1_flash_erase(0U) != FM1_FLASH_OK) return;
    if (fm1_flash_write(0U, r, sizeof *r) != FM1_FLASH_OK) return;
    ui.saved = 1U;
}

static void load_state(void)
{
    struct save_record *r = &rec_buf;
    if (fm1_flash_size() < sizeof *r) return;
    if (fm1_flash_read(0U, r, sizeof *r) != FM1_FLASH_OK) return;
    if (r->magic != SAVE_MAGIC || r->check != record_check(r)) return;
    if (r->tempo < 40U || r->tempo > 250U) return;

    ui.transpose = r->transpose;
    ui.scale = r->scale < SCALE_COUNT ? r->scale : SCALE_OFF;
    ui.part = r->part < ORCH_PARTS ? r->part : 0U;
    ui.scene = r->scene < ORCH_SCENE_COUNT ? r->scene : 0U;
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const struct save_part *sp = &r->parts[p];
        if (sp->family >= ORCH_FAM_COUNT) continue;
        orch_engine_configure_part(p, sp->family, orch_part_get(p)->channels);
        if (sp->instrument < ORCH_INST_COUNT)
            orch_engine_set_instrument(p, sp->instrument);
        if (sp->articulation < ORCH_ART_COUNT)
            orch_engine_set_articulation(p, sp->articulation);
        orch_engine_set_player_cap(p, sp->players);
        orch_engine_set_adsr(p, sp->attack_ms, sp->decay_ms,
                             sp->sustain_percent, sp->release_ms);
        orch_engine_set_lfo(p, sp->lfo_wave, sp->lfo_dest, sp->lfo_depth,
                            sp->lfo_rate_millihz, sp->lfo_sync,
                            sp->lfo_delay_ms, sp->lfo_fade_ms);
        perf_put(p, &r->perf[p]);
    }
    {
        const uint8_t keep = ui.part;
        for (unsigned l = 0; l < ORCH_PARTS; ++l) {
            ui.part = (uint8_t)l;
            for (unsigned st = 0; st < ORCH_SEQ_STEPS; ++st) {
                for (unsigned sl = 0; sl < SEQ_SLOTS; ++sl) {
                    struct orch_step_note *n = &seq_notes[st][sl];
                    *n = r->steps[l][st][sl];
                    if (n->note > 127U) n->note = 60U;
                    if (n->velocity > 127U) n->velocity = 127U;
                    if (n->gate_percent > 100U) n->gate_percent = 100U;
                    if (n->chance > 100U) n->chance = 100U;
                }
                seq_apply_step(st);
            }
            orch_seq_set_lane_muted(l, r->lane_muted[l]);
        }
        ui.part = keep;
        orch_seq_set_length(r->seq_length ? r->seq_length : ORCH_SEQ_STEPS);
    }
    for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s) {
        orch_fx_set_enabled(s, r->fx_on[s]);
        orch_fx_set_mix(s, r->fx_mix[s]);
        for (unsigned i = 0; i < ORCH_FX_PARAMS; ++i)
            orch_fx_set_param(s, i, r->fx_p[s][i]);
    }
    orch_stage_set_listener((int)r->listener_x, (int)r->listener_depth);
    if (r->res_check == fnv(&r->resonance, 4U))
        orch_resonance_set_amount(r->resonance);
    apply_tempo(r->tempo);
    ui.seq_on = r->seq_on ? 1U : 0U;
    orch_seq_set_part(ui.part);
    orch_seq_set_enabled(ui.seq_on);
    ui.saved = 1U;
}

/* --- note mapping --------------------------------------------------------- */

int orch_ui_map_note(int note)
{
    int out = note + (int)ui.transpose;
    if (ui.scale != SCALE_OFF) {
        static const uint8_t major[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const uint8_t minor[7] = { 0, 2, 3, 5, 7, 8, 10 };
        static const uint8_t penta[5] = { 0, 2, 4, 7, 9 };
        const uint8_t *set = major;
        unsigned len = 7U;
        if (ui.scale == SCALE_MINOR) { set = minor; len = 7U; }
        else if (ui.scale == SCALE_PENTA) { set = penta; len = 5U; }
        int pc = out % 12;
        if (pc < 0) pc += 12;
        int snapped = 0;
        for (unsigned i = 0; i < len; ++i) {
            if (set[i] <= pc) snapped = set[i];
            else break;
        }
        out += snapped - pc;
    }
    if (out < 0 || out > 127) return -1;
    return out;
}

/* --- leds ----------------------------------------------------------------- */

static void update_leds(void)
{
    /* the keys of the part's family under ENV..HOME as in 0.4.0, the buttons
     * that toggle something lit while it is on */
    const struct orch_part *part = orch_part_get(ui.part);
    const struct orch_perf_config pc = perf_of(ui.part);
    (void)part;
    for (unsigned b = 0; b < 14U; ++b)
        fm1_led_set(b, (ui.buttons >> b) & 1U);
    fm1_led_set(B_OCT_DOWN, ui.transpose < 0);
    fm1_led_set(B_OCT_UP, ui.transpose > 0);
    fm1_led_set(B_SCL, ui.scale != SCALE_OFF);
    fm1_led_set(B_ENV, ui.page == PAGE_ENV);
    fm1_led_set(B_LFO, ui.page == PAGE_LFO);
    fm1_led_set(B_EDIT, ui.page == PAGE_HOME);
    fm1_led_set(B_GLO, ui.page == PAGE_GLO);
    fm1_led_set(B_ARP, pc.arp_enabled || pc.phrase_enabled);
    fm1_led_set(B_SEQ, ui.page == PAGE_SEQ);
    fm1_led_set(B_PLAY, ui.seq_on);
    fm1_led_set(B_REC, ui.rec);
    fm1_led_set(B_SAVE, ui.saved);
    fm1_led_set(B_FX, orch_fx_any_enabled());
    for (unsigned k = 0; k < 27U; ++k)
        fm1_led_set(14U + k, (ui.keys >> k) & 1U);
}

/* --- knob speed ----------------------------------------------------------- */

/* A knob turned fast goes faster, as HAWK CHUA's does (firmware/src/ui.c,
 * knob_accel): the detents a second in the same direction give x1 (under 12 a
 * second), x2, x4, then x8, so a range crosses in one flick; slowing down
 * brings the factor back by halves, and one detent is still one step. Two calls
 * in the same millisecond are taken unscaled: a hand cannot do that, and it
 * keeps the host tests deterministic. */
static uint32_t enc_at[7];
static int8_t enc_way[7];
static uint8_t enc_speed[7];

static int32_t enc_accel(unsigned role, int32_t detents)
{
    const int8_t way = (int8_t)(detents > 0 ? 1 : -1);
    const uint32_t now = fm1_millis();
    const uint32_t since = now - enc_at[role];
    const uint32_t n = (uint32_t)(detents > 0 ? detents : -detents);
    uint32_t factor = 1U;
    if (!since) {
        enc_at[role] = now;
        enc_way[role] = way;
        return detents;
    }
    if (way == enc_way[role] && since <= 150U) {
        const uint32_t rate = n * 1000U / since;
        factor = rate < 12U ? 1U : rate < 24U ? 2U : rate < 40U ? 4U : 8U;
        if (factor < enc_speed[role]) {
            const uint32_t half = enc_speed[role] / 2U;
            if (half > factor) factor = half;
        }
    }
    enc_speed[role] = (uint8_t)factor;
    enc_way[role] = way;
    enc_at[role] = now;
    return detents * (int32_t)factor;
}

/* The percentages keep the 5 % a detent the panel has always had, so turning
 * slowly is exactly as fine as it was; a fast turn may double that step, never
 * more. A range that jumped 40 % a detent would skip the values the fine step
 * exists for. The tempo and the transpose do take the full count. */
static int pct_delta(int delta)
{
    if (delta > 2) return 2;
    if (delta < -2) return -2;
    return delta;
}
