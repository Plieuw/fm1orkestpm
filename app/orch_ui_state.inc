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

static int clamp_percent(int v)
{
    if (v < 0) return 0;
    if (v > 100) return 100;
    return v;
}

static void play_chord(void)
{
    static const uint8_t notes[ORCH_PARTS] = { 48U, 60U, 64U, 67U };
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        orch_note_event(p * 4U, notes[p], 100U);
    ui.chord_left = ORCH_PARTS;
    ui.chord_off_at = fm1_millis() + 900U;
}

static void release_chord(void)
{
    static const uint8_t notes[ORCH_PARTS] = { 48U, 60U, 64U, 67U };
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        orch_note_event(p * 4U, notes[p], 0U);
    ui.chord_left = 0U;
}

/* --- sequencer editing ---------------------------------------------------- */

static void seq_apply_step(unsigned step)
{
    for (unsigned s = 0; s < SEQ_SLOTS; ++s)
        orch_seq_set_step(step, s, &seq_notes[step][s]);
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
    seq_apply_step(ui.seq_step);
    ui.seq_step = (uint8_t)((ui.seq_step + 1U) % ORCH_SEQ_STEPS);
}

static void seq_clear_step(void)
{
    for (unsigned s = 0; s < SEQ_SLOTS; ++s) {
        seq_notes[ui.seq_step][s].note = 0U;
        seq_notes[ui.seq_step][s].velocity = 0U;
        seq_notes[ui.seq_step][s].gate_percent = 0U;
    }
    seq_apply_step(ui.seq_step);
}

static void seq_touch_slot(unsigned slot_delta, int note_delta, int vel_delta,
                           int gate_delta)
{
    struct orch_step_note *s = &seq_notes[ui.seq_step][0];
    (void)slot_delta;
    if (!s->velocity) {
        s->note = 60U;
        s->velocity = 100U;
        s->gate_percent = 80U;
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

static uint32_t record_check(const struct save_record *r)
{
    const uint8_t *b = (const uint8_t *)r;
    uint32_t sum = 2166136261UL;
    for (unsigned i = 0; i < offsetof(struct save_record, check); ++i) {
        sum ^= b[i];
        sum *= 16777619UL;
    }
    return sum;
}

static uint32_t fx_record_check(const struct save_record *r)
{
    const uint8_t *b = (const uint8_t *)&r->fx_on[0];
    uint32_t sum = 2166136261UL;
    for (unsigned i = 0; i < ORCH_FX_SLOT_COUNT * 6U; ++i) {
        sum ^= b[i];
        sum *= 16777619UL;
    }
    return sum;
}

static uint32_t stage_record_check(const struct save_record *r)
{
    const uint8_t *b = (const uint8_t *)&r->listener_x;
    uint32_t sum = 2166136261UL;
    for (unsigned i = 0; i < 2U; ++i) {
        sum ^= b[i];
        sum *= 16777619UL;
    }
    return sum;
}

static void save_state(void)
{
    struct save_record r;
    memset(&r, 0, sizeof r);
    r.magic = SAVE_MAGIC;
    r.tempo = ui.tempo;
    r.transpose = ui.transpose;
    r.scale = ui.scale;
    r.seq_on = ui.seq_on;
    r.part = ui.part;
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const struct orch_part *part = orch_part_get(p);
        r.family[p] = part->family;
        r.instrument[p] = part->instrument;
        r.articulation[p] = part->articulation;
        r.players[p] = part->player_cap;
        r.voices[p] = part->voice_ceiling;
        r.arp_on[p] = (uint8_t)orch_arp_enabled(p);
        r.arp_mode[p] = (uint8_t)orch_arp_mode_of(p);
        r.arp_oct[p] = (uint8_t)orch_arp_octaves_of(p);
        r.arp_div[p] = (uint8_t)orch_arp_div_of(p);
        r.arp_gate[p] = (uint8_t)orch_arp_gate_of(p);
    }
    for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s) {
        r.fx_on[s] = (uint8_t)orch_fx_enabled(s);
        r.fx_mix[s] = (uint8_t)orch_fx_mix(s);
        for (unsigned i = 0; i < ORCH_FX_PARAMS; ++i)
            r.fx_p[s][i] = (uint8_t)orch_fx_param(s, i);
    }
    r.fx_check = fx_record_check(&r);
    r.listener_x = (int8_t)orch_stage_listener_x();
    r.listener_depth = (uint8_t)orch_stage_listener_depth();
    r.stage_check = stage_record_check(&r);
    r.check = record_check(&r);

    if (fm1_flash_size() < sizeof r) return;
    if (fm1_flash_erase(0U) != FM1_FLASH_OK) return;
    if (fm1_flash_write(0U, &r, sizeof r) != FM1_FLASH_OK) return;
    ui.saved = 1U;
}

static void load_state(void)
{
    struct save_record r;
    if (fm1_flash_size() < sizeof r) return;
    if (fm1_flash_read(0U, &r, sizeof r) != FM1_FLASH_OK) return;
    if (r.magic != SAVE_MAGIC || r.check != record_check(&r)) return;
    if (r.tempo < 30U || r.tempo > 300U) return;

    ui.tempo = r.tempo;
    ui.transpose = r.transpose;
    ui.scale = r.scale < SCALE_COUNT ? r.scale : SCALE_OFF;
    ui.part = r.part < ORCH_PARTS ? r.part : 0U;
    orch_seq_set_tempo(ui.tempo);
    orch_fx_set_tempo(ui.tempo);
    ui.seq_on = r.seq_on ? 1U : 0U;
    orch_seq_set_enabled(ui.seq_on);
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        if (r.family[p] >= ORCH_FAM_COUNT) continue;
        orch_engine_configure_part(p, r.family[p], orch_part_get(p)->channels);
        if (r.instrument[p] < ORCH_INST_COUNT)
            orch_engine_set_instrument(p, r.instrument[p]);
        if (r.articulation[p] < ORCH_ART_COUNT)
            orch_engine_set_articulation(p, r.articulation[p]);
        orch_engine_set_player_cap(p, r.players[p]);
        orch_engine_set_voice_ceiling(p, r.voices[p]);
        orch_arp_set_enabled(p, r.arp_on[p]);
        orch_arp_set_mode(p, r.arp_mode[p]);
        orch_arp_set_octaves(p, r.arp_oct[p]);
        orch_arp_set_division(p, r.arp_div[p]);
        orch_arp_set_gate(p, r.arp_gate[p]);
    }
    /* Older records have no FX block: their fx_check cannot match, so the FX
     * keep whatever the panel had instead of loading erased flash bytes. */
    if (r.fx_check == fx_record_check(&r)) {
        for (unsigned s = 0; s < ORCH_FX_SLOT_COUNT; ++s) {
            orch_fx_set_enabled(s, r.fx_on[s]);
            orch_fx_set_mix(s, r.fx_mix[s]);
            for (unsigned i = 0; i < ORCH_FX_PARAMS; ++i)
                orch_fx_set_param(s, i, r.fx_p[s][i]);
        }
    }
    if (r.stage_check == stage_record_check(&r))
        orch_stage_set_listener((int)r.listener_x, (int)r.listener_depth);
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
    for (unsigned b = 0; b < 4U; ++b) fm1_led_set(b, b == ui.part);
    const struct orch_part *part = orch_part_get(ui.part);
    const unsigned family = part ? part->family : 0U;
    for (unsigned b = 4U; b < 9U; ++b) fm1_led_set(b, b - 4U == family);
    for (unsigned b = 9U; b < 14U; ++b)
        fm1_led_set(b, (ui.buttons >> b) & 1U);
    fm1_led_set(B_ARP, orch_arp_enabled(ui.part));
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
