/* --- the stage ------------------------------------------------------------ */

/* The stage page is a seating plan drawn as vectors (0.6.0; a photograph of a
 * hall before). The conductor's podium is at the bottom; the sections sit in
 * rings around it as an orchestra does — strings in front, woodwinds behind
 * them, brass behind those, percussion and choir at the back — and every one
 * of the eight parts is a curved desk in its ring, at the angle its
 * instrument's seat gives it (orch_stage.c, the same seat the stereo pan
 * comes from). A desk fills with amber as its part's voices sound; the
 * selected part's desk is framed. Around the podium the light shows what
 * leaves the instrument (orch_level.inc), and in front of the stage the
 * listener sits where KNOB3 and KNOB4 put them.
 *
 * Everything is a path through the anti-aliased rasterizer
 * (orch_ui_path.inc). A desk is redrawn on its own when its part starts or
 * stops: the desks do not overlap, so nothing around one is touched. */

#define POD_X 120
#define POD_Y 168                  /* the podium, the centre of the rings */
#define SEAT_Y0 178U               /* the listener's band, under the stage */
#define SEAT_Y1 188U

/* A ring per family: inner and outer radius. */
static const uint8_t ring_in[ORCH_FAM_COUNT] = { 24U, 54U, 80U, 106U, 106U };
static const uint8_t ring_out[ORCH_FAM_COUNT] = { 50U, 76U, 102U, 126U, 126U };

/* The stage's own colours: the boards, a desk at rest, the frame of the
 * selected one. Readings stay white/amber on these. */
#define C_BOARDS 0x18A2
#define C_DESK   0x3165

/* The hall's lights no longer cycle: the page has one frame. */
static unsigned stage_frame(void) { return 0U; }

/* The voices of the eight parts as one word, three bits a part. */
static uint32_t drawn_voices;
static uint32_t stage_voices(void)
{
    uint32_t v = 0U;
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        unsigned n = orch_voices_of_part(p);
        if (n > 7U) n = 7U;
        v |= (uint32_t)n << (3U * p);
    }
    return v;
}

/* Where the desks are: each part in its family's ring, at the angle of its
 * instrument's seat (0 hard left .. 255 hard right, as 256ths of a turn
 * around twelve o'clock). Desks in one ring are then pushed apart until they
 * no longer touch, and the ring is re-centred if that pushed it off the
 * stage. Computed for all eight at once, whenever the layout may have changed. */
static int8_t desk_mid[ORCH_PARTS];
static uint8_t desk_fam[ORCH_PARTS], desk_half[ORCH_PARTS];
static uint32_t desk_layout_sig = 0xFFFFFFFFUL;

static uint32_t stage_layout_signature(void)
{
    uint32_t sig = 2166136261UL;
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const struct orch_part *q = orch_part_get(p);
        sig = (sig ^ (q ? q->instrument : 0U)) * 16777619UL;
        sig = (sig ^ (q ? q->family : 0U)) * 16777619UL;
    }
    return sig;
}

static void stage_layout(void)
{
    int mid[ORCH_PARTS];
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const struct orch_part *q = orch_part_get(p);
        const unsigned f = q && q->family < ORCH_FAM_COUNT ? q->family : 0U;
        desk_fam[p] = (uint8_t)f;
        desk_half[p] = (uint8_t)(f == ORCH_FAM_STRINGS ? 10U : (f == ORCH_FAM_WOODWINDS ? 8U : 7U));
        mid[p] = ((int)orch_stage_seat(q ? q->instrument : 0U) - 128) * 52 / 128;
    }
    /* mallets and choir share the back ring */
    for (unsigned ring = 0; ring < 4U; ++ring) {
        unsigned idx[ORCH_PARTS], n = 0U;
        for (unsigned p = 0; p < ORCH_PARTS; ++p) {
            const unsigned r = desk_fam[p] >= 3U ? 3U : desk_fam[p];
            if (r == ring) idx[n++] = p;
        }
        for (unsigned i = 1; i < n; ++i) {                   /* by angle, then part */
            const unsigned k = idx[i];
            unsigned j = i;
            while (j && (mid[idx[j - 1U]] > mid[k] ||
                         (mid[idx[j - 1U]] == mid[k] && idx[j - 1U] > k))) {
                idx[j] = idx[j - 1U]; --j;
            }
            idx[j] = k;
        }
        for (unsigned i = 1; i < n; ++i) {
            const int gap = (int)desk_half[idx[i - 1U]] + (int)desk_half[idx[i]] + 1;
            if (mid[idx[i]] < mid[idx[i - 1U]] + gap) mid[idx[i]] = mid[idx[i - 1U]] + gap;
        }
        if (n) {
            const int lo = mid[idx[0]] - (int)desk_half[idx[0]];
            const int hi = mid[idx[n - 1U]] + (int)desk_half[idx[n - 1U]];
            int shift = 0;
            if (hi > 66) shift = 66 - hi;
            if (lo + shift < -66) shift = -(lo + hi) / 2;   /* too full: centre it */
            for (unsigned i = 0; i < n; ++i) mid[idx[i]] += shift;
        }
    }
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        desk_mid[p] = (int8_t)(mid[p] < -120 ? -120 : (mid[p] > 120 ? 120 : mid[p]));
    desk_layout_sig = stage_layout_signature();
}

static void desk_place(unsigned p, unsigned *fam, int *a_mid, int *half)
{
    *fam = desk_fam[p];
    *a_mid = desk_mid[p];
    *half = desk_half[p];
}

/* An annular sector: out along the outer arc, back along the inner one. The
 * angles are offset by a turn so a desk on the left (negative angle) stays a
 * valid range for vp_arc(). */
static void sector(int a0, int a1, unsigned r0, unsigned r1, uint16_t fill, uint16_t bg)
{
    vp_reset();
    vp_arc(POD_X, POD_Y, r1, (unsigned)(a0 + 256), (unsigned)(a1 + 256));
    vp_arc_rev(POD_X, POD_Y, r0, (unsigned)(a1 + 256), (unsigned)(a0 + 256));
    vp_fill(&vp, fill, bg);
}

static void draw_instrument_name(void)
{
    const struct orch_part *part = orch_part_get(ui.part);
    fm1_screen_fill(8U, TITLE_H + 4U, 120U, 9U, C_BG);
    if (part)
        text(8U, TITLE_H + 5U, inst_short(part->instrument), C_DIM, C_BG, 120U);
}

/* One desk: lit by the voices of its part (a full desk at four), framed
 * when it is the selected part, with the part's number on it. */
static void draw_desk(unsigned p)
{
    unsigned fam;
    int mid, half;
    desk_place(p, &fam, &mid, &half);
    unsigned voices = orch_voices_of_part(p);
    if (voices > 4U) voices = 4U;
    shown_voices[p] = (uint8_t)voices;

    const unsigned r0 = ring_in[fam], r1 = ring_out[fam];
    const int sel = (p == ui.part);
    const uint16_t lit = voices ? mix565(C_DESK, C_AMBER, 90U + voices * 40U) : C_DESK;
    /* the frame is the desk drawn a pixel larger in amber, the desk inside */
    sector(mid - half, mid + half, r0, r1, sel ? C_AMBER2 : C_BOARDS, C_BOARDS);
    sector(mid - half + 1, mid + half - 1, r0 + 2U, r1 - 2U, lit,
           sel ? C_AMBER2 : C_BOARDS);

    /* the part's number in the middle of the desk */
    {
        const unsigned rm = (r0 + r1) / 2U;
        const int x = POD_X + ((vp_sin14((unsigned)(mid + 256)) * (int)rm) >> 14);
        const int y = POD_Y - ((vp_cos14((unsigned)(mid + 256)) * (int)rm) >> 14);
        char n[2] = { (char)('1' + p), 0 };
        text((unsigned)(x - 2), (unsigned)(y - 3), n, voices ? C_BG : C_DIM, lit, 6U);
    }
}

static void draw_players(void)
{
    for (unsigned p = 0; p < ORCH_PARTS; ++p) draw_desk(p);
    if (ui.part < ORCH_PARTS) draw_desk(ui.part);   /* its frame on top */
}

/* The listener: an amber seat in the band under the stage, left to right by
 * x and lower (further back) by depth. */
static void draw_hall_marks(void)
{
    const unsigned w = SW - 40U;
    fm1_screen_fill(0U, SEAT_Y0, SW, SEAT_Y1 - SEAT_Y0 + 1U, C_BG);
    fm1_screen_fill(20U, SEAT_Y0 + 4U, w, 1U, C_LINE);       /* the front row */
    const int x = (int)SW / 2 + orch_stage_listener_x() * (int)(w / 2U) / 100;
    const unsigned y = SEAT_Y0 + 2U + orch_stage_listener_depth() * 6U / 100U;
    disc((unsigned)x, y, 3U, C_AMBER2, C_AMBER, C_BG);
    draw_instrument_name();
}

/* The light around the podium: a ring whose arc opens with the level, 24 of
 * 24 a half circle across the front of the strings. Throttled as in 0.4.0,
 * and incremental: a change paints only the two short arcs between the old
 * and the new width (lit when it opens, the boards' colour when it closes),
 * so a moving level costs a few dozen fills, not the ring. */
#define LIP_MS 50U
#define LIP_R 15U
#define LIP_W 4U
static unsigned lip_span;   /* the half-width on screen, 256ths of a turn */
static void draw_stage_light(unsigned force)
{
    static uint32_t drawn_at;
    const uint32_t now = fm1_millis();
    if (!force && (uint32_t)(now - drawn_at) < LIP_MS) return;
    drawn_at = now;
    const unsigned level = orch_output_level();
    const unsigned span = level * 64U / ORCH_LEVEL_MAX;
    const uint16_t lit = C_AMBER;

    if (force) {
        arc_soft((unsigned)POD_X, (unsigned)POD_Y, LIP_R, 256U - 64U, 256U + 64U, LIP_W,
                 C_BOARDS, C_BOARDS);
        if (span)
            arc_soft((unsigned)POD_X, (unsigned)POD_Y, LIP_R, 256U - span, 256U + span,
                     LIP_W, lit, C_BOARDS);
        disc((unsigned)POD_X, (unsigned)POD_Y, 6U, C_AMBER,
             mix565(C_AMBER, C_BOARDS, 120U), C_BOARDS);
        lip_span = span;
        return;
    }
    if (span == lip_span) return;
    const unsigned a = span < lip_span ? span : lip_span;
    const unsigned b = span < lip_span ? lip_span : span;
    const uint16_t c = span > lip_span ? lit : C_BOARDS;
    arc_soft((unsigned)POD_X, (unsigned)POD_Y, LIP_R, 256U - b, 256U - a, LIP_W, c, C_BOARDS);
    arc_soft((unsigned)POD_X, (unsigned)POD_Y, LIP_R, 256U + a, 256U + b, LIP_W, c, C_BOARDS);
    lip_span = span;
}

/* The page is entered: the boards, the desks, the podium, the listener. */
static void page_parts_full(void)
{
    stage_layout();
    fm1_screen_fill(0U, TITLE_H, SW, SEAT_Y0 - TITLE_H, C_BG);
    vp_reset();
    vp_arc(POD_X, POD_Y, 132U, 256U - 70U, 256U + 70U);
    vp_line(POD_X + 20, POD_Y + 8);
    vp_line(POD_X - 20, POD_Y + 8);
    vp_fill(&vp, C_BOARDS, C_BG);
    draw_players();
    draw_hall_marks();
    draw_stage_light(1);
}

/* A setting changed, or a part started or stopped: the desks and the marks;
 * if a part changed instrument the desks have moved, and the boards are
 * painted again under them. */
static void page_parts(void)
{
    if (stage_layout_signature() != desk_layout_sig) {
        page_parts_full();
        return;
    }
    draw_players();
    draw_hall_marks();
}

/* (0.4.0's hall changed its lights in a band; the plan has no such band.) */
static void page_parts_photo(unsigned f)
{
    (void)f;
    draw_players();
}
