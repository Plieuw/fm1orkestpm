/* --- the stage ------------------------------------------------------------ */

/* The parts page is the hall itself: four rows for the four parts, and under
 * them the room they are playing in. */

static void draw_meter(unsigned p)
{
    const struct orch_part *part = orch_part_get(p);
    const unsigned ceiling = part->voice_ceiling ? part->voice_ceiling : 1U;
    const unsigned y = BODY_Y + p * ROW_H + 14U;
    unsigned voices = orch_voices_of_part(p);
    if (voices > ceiling) voices = ceiling;
    bar(150U, y, 76U, 5U, voices * 100U / ceiling, C_METER, C_CARD);
    shown_voices[p] = (uint8_t)voices;
}

static void draw_part_row(unsigned p)
{
    const unsigned y = BODY_Y + p * ROW_H;
    const struct orch_part *part = orch_part_get(p);
    const int sel = (p == ui.part);
    const uint16_t bg = sel ? C_ROW : C_BG;
    char line[40];
    unsigned n;

    fm1_screen_fill(4U, y + 1U, SW - 8U, ROW_H - 1U, bg);
    fm1_screen_fill(4U, y + ROW_H - 1U, SW - 8U, 1U, C_LINE);
    if (sel) fm1_screen_fill(4U, y + 1U, 3U, ROW_H - 2U, C_AMBER);
    shown_voices[p] = 0xFFU;

    /* the desk: its number and the family it is seated with, the players right */
    n = put(line, 0, sizeof line, "P");
    n = put_num(line, n, sizeof line, p + 1U, 1U);
    n = put(line, n, sizeof line, "  ");
    put(line, n, sizeof line, orch_family_name(part->family));
    text(12U, y + 4U, line, sel ? C_AMBER : C_TEXT, bg, 130U);

    n = put(line, 0, sizeof line, "");
    n = put_num(line, n, sizeof line, part->player_cap, 1U);
    put(line, n, sizeof line, "PL");
    text(SW - 10U - span(line), y + 4U, line, C_DIM, bg, span(line));

    /* the instrument and how it is played, with the voices it is using */
    n = put(line, 0, sizeof line, inst_short(part->instrument));
    n = put(line, n, sizeof line, "  ");
    put(line, n, sizeof line, art_short(part->articulation));
    text(12U, y + 14U, line, C_DIM, bg, 130U);

    draw_meter(p);
}

/* The hall under the four rows, as the listener hears it: an orchestra shell
 * behind the players, the wooden floor they stand on, the curtains at the
 * sides, and the audience row in front where the lit seat is the listener.
 * A part stands on its section's place (the selected part in green), so the
 * picture follows KNOB3 and KNOB4 without a new page. Rectangle fills only:
 * this is a silhouette of a concert hall, not a picture of one. */
static const uint8_t shell_half[12] = {
    6, 14, 22, 30, 38, 46, 54, 62, 70, 78, 86, 92
};

static void draw_hall(void)
{
    const unsigned top = BODY_Y + 4U * ROW_H + 2U;
    const unsigned floor_y = top + 12U;
    const unsigned left = 30U;
    const unsigned right = SW - 30U;
    const unsigned span = right - left;
    const unsigned seat_y = floor_y + 26U;

    /* the shell that carries the sound out over the orchestra */
    for (unsigned row = 0; row < 12U; ++row) {
        const unsigned half = shell_half[row];
        fm1_screen_fill(SW / 2U - half, top + row, half * 2U, 1U, C_SHELL);
    }

    /* the floor: a band the width of the stage, one shade darker at the back */
    fm1_screen_fill(left, floor_y, span, 22U, C_WOOD);
    fm1_screen_fill(left + 10U, floor_y, span - 20U, 2U, C_SHELL);
    fm1_screen_fill(left, floor_y + 22U, span, 1U, C_LINE);  /* the front edge */

    /* the curtains, barely open */
    fm1_screen_fill(left - 10U, top + 2U, 6U, seat_y - top - 2U, C_CURTAIN);
    fm1_screen_fill(right + 4U, top + 2U, 6U, seat_y - top - 2U, C_CURTAIN);

    /* the parts on their section's place, the selected one in green */
    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const struct orch_part *part = orch_part_get(p);
        const unsigned pan = orch_stage_place(part->instrument, 128U);
        const unsigned x = left + 6U + pan * (span - 12U) / 255U;
        fm1_screen_fill(x - 3U, floor_y + 6U, 7U, 7U,
                        p == ui.part ? C_METER : C_TEXT);
        fm1_screen_fill(x - 3U, floor_y + 6U, 7U, 1U, C_LINE);
    }

    /* the audience row; the lit seat is where the listener sits */
    for (unsigned s = 0; s < 7U; ++s)
        fm1_screen_fill(left + s * 26U, seat_y, 18U, 4U, C_ROW);
    const unsigned lx = left +
        (unsigned)((orch_stage_listener_x() + 100) * (int)(span - 18U) / 200);
    fm1_screen_fill(lx, seat_y, 18U, 4U, C_AMBER);
    /* the ends of the row, so the hall has a left and a right */
    fm1_screen_fill(left - 6U, seat_y + 1U, 4U, 2U, C_DIM);
    fm1_screen_fill(right + 2U, seat_y + 1U, 4U, 2U, C_DIM);
}

static void page_parts(void)
{
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        draw_part_row(p);
    draw_hall();
}
