/* --- the stage ------------------------------------------------------------ */

/* The stage page is a photograph, not a drawing. A real hall — wall, arch,
 * curtains, boards and the rows of seats — is in app/orch_ui_photo.inc, reduced
 * by tools/panel_photo.py to a palette of sixteen colours and, per row, the runs
 * of one colour; each run is one fill. On top of it the panel draws what moves:
 * the four lamps in the audience for the parts that sound, the players on the
 * boards, the listener's seat, the name of the instrument — and, while something
 * is sounding, the hall's own lights, cycling through the three frames the
 * source images gave us.
 *
 * 240 x 164 at 2 x 2 pixels, kept as the runs a row is made of: one fill per
 * stretch of one colour, about 1 500 for the whole hall. */

#define PHOTO_Y TITLE_H

/* the four parts, as stand lights in the dark row of seats */
#define LAMP_X0 10U
#define LAMP_W 5U
#define LAMP_GAP 6U
#define LAMP_TOP 122U
#define LAMP_H 52U
#define LAMP_DARK 0x18C3             /* a seat back in the dark, not the case */

/* where the boards are in the photograph: the players stand on that line */
#define STAGE_Y 118U
#define STAGE_X0 62U
#define STAGE_X1 178U
#define SEAT_Y 148U

/* The animation: the rest frame while the hall is quiet, and the two lit frames
 * in turn while a part is sounding. A slow cycle — the lights of a hall change
 * with the music, not with the frame rate. */
static unsigned stage_frame(void)
{
    unsigned playing = 0;
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        if (orch_voices_of_part(p)) playing = 1;
    if (!playing || PHOTO_FRAMES < 2U) return 0U;
    return 1U + (unsigned)((fm1_millis() / 1200U) % (PHOTO_FRAMES - 1U));
}

static unsigned photo_cell(char c)
{
    return (c >= '0' && c <= '9') ? (unsigned)(c - '0')
                                  : (unsigned)(c - 'a' + 10);
}

/* The photograph: per row the runs it is made of, three hex characters each —
 * the palette index, then the run's width in cells. A row of the hall is ten
 * or so fills, not a hundred and twenty. */
static void draw_photo(unsigned f)
{
    if (f >= PHOTO_FRAMES) f = 0U;
    const char *runs = photo_frame_runs[f];
    const uint16_t *rows = photo_frame_rows[f];
    for (unsigned y = 0; y < PHOTO_ROWS; ++y) {
        const unsigned py = PHOTO_Y + y * PHOTO_BLOCK;
        unsigned px = 0;
        for (unsigned i = rows[y]; i < rows[y + 1U]; ++i) {
            const char *run = runs + i * 3U;
            const unsigned w = (photo_cell(run[1]) << 4) | photo_cell(run[2]);
            fm1_screen_fill(px, py, w * PHOTO_BLOCK, PHOTO_BLOCK,
                            photo_palette[photo_cell(run[0])]);
            px += w * PHOTO_BLOCK;
        }
    }
}

/* A part: a narrow lamp in the seats that fills from the bottom up with the
 * voices that part is sounding, amber-capped when it is the selected one. */
static void draw_desk(unsigned p)
{
    const struct orch_part *part = orch_part_get(p);
    const unsigned ceiling = part->voice_ceiling ? part->voice_ceiling : 1U;
    const int sel = (p == ui.part);
    const unsigned x = LAMP_X0 + p * (LAMP_W + LAMP_GAP);
    unsigned voices = orch_voices_of_part(p);
    if (voices > ceiling) voices = ceiling;
    shown_voices[p] = (uint8_t)voices;

    fm1_screen_fill(x, LAMP_TOP, LAMP_W, LAMP_H, LAMP_DARK);
    if (voices) {
        const unsigned lit = (LAMP_H - 2U) * voices / ceiling;
        fm1_screen_fill(x + 1U, LAMP_TOP + LAMP_H - 1U - lit, LAMP_W - 2U, lit,
                        C_METER);
        fm1_screen_fill(x + 1U, LAMP_TOP + LAMP_H - 1U - lit, LAMP_W - 2U, 1U,
                        mix565(C_METER, C_TEXT, 150U));
    }
    if (sel) fm1_screen_fill(x, LAMP_TOP - 2U, LAMP_W, 1U, C_AMBER);
}

/* The players on the boards, one per part, at its section's place: lit when it
 * sounds. They are the part of the hall that moves, so they are drawn on their
 * own whenever a part starts or stops. */
static void draw_players(void)
{
    const unsigned cx = (STAGE_X0 + STAGE_X1) / 2U;

    for (unsigned p = 0; p < ORCH_PARTS; ++p) {
        const struct orch_part *q = orch_part_get(p);
        const unsigned pan = orch_stage_place(q->instrument, 128U);
        const int off = ((int)pan - 128) * (int)(STAGE_X1 - STAGE_X0) / 256;
        const unsigned x = (unsigned)((int)cx + off);
        const unsigned y = STAGE_Y - 6U - (unsigned)(off < 0 ? -off : off) / 14U;
        if (orch_voices_of_part(p))
            disc(x, y, 4U, C_AMBER2, C_AMBER, C_SHELL);
        else
            disc(x, y, 3U, mix565(C_SHELL, C_BG, 130U), C_SHELL, C_SHELL);
    }
}

/* The listener's seat, amber among the seats, and the one word on the page: the
 * instrument of the selected part. */
static void draw_hall_marks(void)
{
    const unsigned cx = 120U;
    {
        const unsigned row = 1U - orch_stage_listener_depth() / 60U;
        const unsigned half = 30U + row * 10U;
        const int x = (int)cx + orch_stage_listener_x() * (int)half / 100;
        disc((unsigned)x, SEAT_Y + row * 9U, 3U, C_AMBER2, C_AMBER, C_SHELL);
    }
    {
        const struct orch_part *part = orch_part_get(ui.part);
        if (part)
            text(10U, PHOTO_Y + 6U, inst_short(part->instrument), C_DIM, C_BG,
                 120U);
    }
}

/* Everything the panel puts over the photograph. */
static void draw_hall_overlays(void)
{
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        draw_desk(p);
    draw_players();
    draw_hall_marks();
}

/* The page is entered: the photograph, then what stands on it. */
static void page_parts_full(void)
{
    draw_photo(stage_frame());
    draw_hall_overlays();
}

/* A setting changed, or a part started or stopped: the photograph stays, only
 * the lamps, the players and the marks are redrawn. */
static void page_parts(void)
{
    draw_hall_overlays();
}

/* The hall's lights changed: the frame is repainted under the marks. */
static void page_parts_photo(unsigned f)
{
    draw_photo(f);
    draw_hall_overlays();
}
