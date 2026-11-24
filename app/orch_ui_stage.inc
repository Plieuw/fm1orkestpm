/* --- the stage ------------------------------------------------------------ */

/* The parts page is drawn, not written: four desks on the left, each a column
 * that fills with the voices that part is sounding, and beside them the hall
 * itself — a wall with an arch, the stage floor in perspective, the players on
 * it and the audience in front. */

#define DESK_X0 6U
#define DESK_W 13U
#define DESK_GAP 5U
#define DESK_TOP 40U
#define DESK_H 108U
#define HALL_X 84U
#define HALL_W (SW - HALL_X - 4U)
#define HALL_Y 34U
#define HALL_H 152U

static unsigned desk_x(unsigned p)
{
    return DESK_X0 + p * (DESK_W + DESK_GAP);
}

static void draw_desk(unsigned p)
{
    const struct orch_part *part = orch_part_get(p);
    const unsigned ceiling = part->voice_ceiling ? part->voice_ceiling : 1U;
    const int sel = (p == ui.part);
    const unsigned x = desk_x(p);
    unsigned voices = orch_voices_of_part(p);
    if (voices > ceiling) voices = ceiling;
    shown_voices[p] = (uint8_t)voices;

    fm1_screen_fill(x, DESK_TOP, DESK_W, DESK_H, C_CARD);
    if (voices) {
        const unsigned lit = (DESK_H - 2U) * voices / ceiling;
        fm1_screen_fill(x + 1U, DESK_TOP + DESK_H - 1U - lit, DESK_W - 2U, lit,
                        C_METER);
        fm1_screen_fill(x + 1U, DESK_TOP + DESK_H - 1U - lit, DESK_W - 2U, 1U,
                        mix565(C_METER, C_TEXT, 150U));
    }

    char digit[2] = { (char)('0' + p + 1U), '\0' };
    text_center(x + DESK_W / 2U, DESK_TOP + DESK_H + 4U, digit,
                sel ? C_AMBER : C_DIM, C_BG);

    if (sel) {
        fm1_screen_fill(x - 2U, DESK_TOP - 2U, DESK_W + 4U, 1U, C_AMBER);
        fm1_screen_fill(x - 2U, DESK_TOP + DESK_H + 1U, DESK_W + 4U, 1U, C_AMBER);
        fm1_screen_fill(x - 2U, DESK_TOP - 2U, 1U, DESK_H + 3U, C_AMBER);
        fm1_screen_fill(x + DESK_W + 1U, DESK_TOP - 2U, 1U, DESK_H + 3U, C_AMBER);
    }
}

/* The players on the boards, one per part, at its section's place: lit when it
 * sounds. They are the part of the hall that moves, so they are drawn on their
 * own whenever a part starts or stops — the wall, the arch and the audience are
 * only drawn when the page is. */
static void draw_players(void)
{
    const unsigned cx = HALL_X + HALL_W / 2U;
    const unsigned floor_y = HALL_Y + 92U;

    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const struct orch_part *q = orch_part_get(p);
        const unsigned pan = orch_stage_place(q->instrument, 128U);
        const int off = ((int)pan - 128) * 42 / 128;
        const unsigned x = (unsigned)((int)cx + off);
        const unsigned y = floor_y + 26U - (unsigned)(off < 0 ? -off : off) / 9U;
        if (orch_voices_of_part(p))
            disc(x, y, 4U, C_AMBER2, C_AMBER, C_WOOD);
        else
            disc(x, y, 3U, mix565(C_WOOD, C_BG, 130U), C_WOOD, C_WOOD);
    }
}

/* The hall's own art: the wall, the opening with its arch, the boards in
 * perspective, the pool of light, the curtains and the rows of the audience.
 * None of it changes while the panel is on the stage page, and all of it is
 * what a knob turn used to repaint — so it is drawn once, when the page is
 * entered, and the players, the listener and the instrument's name are drawn
 * on their own. */
static void draw_hall_static(void)
{
    const unsigned cx = HALL_X + HALL_W / 2U;
    const unsigned arch_cy = HALL_Y + 66U;
    const unsigned floor_y = HALL_Y + 92U;
    const unsigned seat_y = floor_y + 44U;

    /* the wall, warm towards its foot */
    grad(HALL_X, HALL_Y, HALL_W, floor_y - HALL_Y, C_BG,
         mix565(C_BG, C_WOOD, 80U));

    /* the opening of the stage: a dark arch set into the wall, round at the
     * top, straight at the sides, filled a row at a time */
    for (unsigned r = 0; r < 42U; ++r) {
        const int drop = 41 - (int)r;
        const unsigned half = drop < 44 ? isqrt(44U * 44U - (unsigned)(drop * drop))
                                        : 44U;
        fm1_screen_fill(cx - half, HALL_Y + 26U + r, half * 2U, 1U,
                        mix565(C_BG, C_WOOD, 20U));
    }
    arc_soft(cx, arch_cy, 46U, 186U, 326U, 2U, mix565(C_BG, C_AMBER, 120U), C_BG);
    arc_soft(cx, arch_cy, 42U, 186U, 326U, 1U, mix565(C_BG, C_WOOD, 160U), C_BG);

    /* the floor, in perspective: narrow at the back, wide at the front */
    for (unsigned r = 0; r < 42U; ++r) {
        const unsigned w = 40U + r * 3U;
        fm1_screen_fill(cx - w / 2U, floor_y + r, w, 1U,
                        mix565(C_WOOD, mix565(C_WOOD, C_TEXT, 40U),
                               r * 255U / 42U));
    }
    fm1_screen_fill(cx - 62U, floor_y + 42U, 124U, 1U,
                    mix565(C_WOOD, C_TEXT, 150U));

    /* a pool of light on the boards, where the stage is lit */
    for (unsigned r = 0; r < 9U; ++r) {
        const unsigned w = 30U - r * 3U;
        fm1_screen_fill(cx - w / 2U, floor_y + 10U + r, w, 1U,
                        mix565(C_WOOD, C_AMBER2, 34U + r * 7U));
    }

    /* the curtains at the sides of the opening, in the hall's wine red */
    fm1_screen_fill(cx - 56U, arch_cy - 28U, 5U, 56U, C_CURTAIN);
    fm1_screen_fill(cx + 51U, arch_cy - 28U, 5U, 56U, C_CURTAIN);

    /* the audience: a dark band with two rows of seats in it, the listener's
     * seat is not one of them — it moves, and is drawn with the players */
    fm1_screen_fill(HALL_X, seat_y - 5U, HALL_W, HALL_H - (seat_y - HALL_Y) + 3U,
                    mix565(C_BG, C_WOOD, 26U));
    for (unsigned row = 0; row < 2U; ++row) {
        const unsigned y = seat_y + row * 7U;
        const unsigned half = 44U + row * 12U;
        const unsigned n = 7U + row * 2U;
        for (unsigned s = 0; s < n; ++s) {
            const unsigned x = cx - half + s * 2U * half / (n - 1U);
            disc(x, y, 2U, mix565(C_BG, C_TEXT, 120U), C_BG, C_BG);
        }
    }
}

/* What does move in the hall: the players, the listener's seat and the name of
 * the instrument on the selected desk. */
static void draw_hall_live(void)
{
    const unsigned cx = HALL_X + HALL_W / 2U;
    const unsigned floor_y = HALL_Y + 92U;
    const unsigned seat_y = floor_y + 44U;

    draw_players();

    /* the listener's seat, amber among the audience */
    {
        const unsigned row = 1U - orch_stage_listener_depth() / 60U;
        const unsigned half = 44U + row * 12U;
        const int x = (int)cx + orch_stage_listener_x() * (int)half / 100;
        disc((unsigned)x, seat_y + row * 7U, 3U, C_AMBER2, C_AMBER, C_BG);
    }

    {
        const struct orch_part *part = orch_part_get(ui.part);
        if (part) {
            char line[20];
            put(line, 0, sizeof line, inst_short(part->instrument));
            text(HALL_X + 4U, HALL_Y + 4U, line, C_DIM, C_BG, HALL_W - 8U);
        }
    }
}

static void page_parts_full(void)
{
    draw_hall_static();
    draw_hall_live();
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        draw_desk(p);
}

/* Everything on the stage page except the hall's art: what a knob turn or a
 * part starting actually changes. */
static void page_parts(void)
{
    draw_hall_live();
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        draw_desk(p);
}
