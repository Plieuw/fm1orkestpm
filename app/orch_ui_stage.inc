/* --- the stage ------------------------------------------------------------ */

/* The stage page is a photograph, not a drawing. A real hall — wall, arch,
 * curtains, boards and the rows of seats — is in app/orch_ui_photo.inc, reduced
 * by tools/panel_photo.py to a palette of sixteen colours and, per row, the runs
 * of one colour; each run is one fill. On top of it the panel draws what moves:
 * the four lamps in the audience for the parts that sound, the players on the
 * boards, the listener's seat, the name of the instrument — and, while something
 * is sounding, the hall's own lights, cycling through the frames the source
 * images gave us: a band of rows, not the whole hall.
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

/* The animation: the rest frame while the hall is quiet, and the lit frames of
 * the source images in turn while a part is sounding. Only the rows in
 * PHOTO_BAND_LO..PHOTO_BAND_HI differ between them — the stage and its curtains,
 * above the boards — so a step redraws that band, not the hall. A slow cycle:
 * the lights of a hall change with the music, not with the frame rate. */
#define LIGHT_STEP_MS 2400U
static unsigned stage_frame(void)
{
    unsigned playing = 0;
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        if (orch_voices_of_part(p)) playing = 1;
    if (!playing || PHOTO_FRAMES < 2U) return 0U;
    return (unsigned)((fm1_millis() / LIGHT_STEP_MS) % PHOTO_FRAMES);
}

static void draw_players(void);
static void draw_instrument_name(void);

static unsigned photo_cell(char c)
{
    return (c >= '0' && c <= '9') ? (unsigned)(c - '0')
                                  : (unsigned)(c - 'a' + 10);
}

/* The photograph: per row the runs it is made of, three hex characters each —
 * the palette index, then the run's width in cells. A row of the hall is ten
 * or so fills, not a hundred and twenty. */
static void draw_photo_rows(unsigned f, unsigned first, unsigned last)
{
    if (f >= PHOTO_FRAMES) f = 0U;
    const char *runs = photo_frame_runs[f];
    const uint16_t *rows = photo_frame_rows[f];
    for (unsigned y = first; y <= last && y < PHOTO_ROWS; ++y) {
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

static void draw_photo(unsigned f)
{
    draw_photo_rows(f, 0U, PHOTO_ROWS - 1U);
}

/* The lights stepped: only the band where the frames differ is repainted, and
 * the players, who stand in it. The seats, the lamps and the front of the hall
 * are the same picture they were, so they stay as they are. */
static void draw_photo_band(unsigned f)
{
    draw_photo_rows(f, PHOTO_BAND_LO, PHOTO_BAND_HI);
    draw_players();
    draw_instrument_name();
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

/* The one word on the page: the instrument of the selected part. It sits at the
 * top of the photograph, inside the band the lights change in, so a step of the
 * lights puts it back. */
static void draw_instrument_name(void)
{
    const struct orch_part *part = orch_part_get(ui.part);
    if (part)
        text(10U, PHOTO_Y + 6U, inst_short(part->instrument), C_DIM, C_BG,
             120U);
}

/* The listener's seat, amber among the seats, under the band and above the
 * strip: it is drawn when the page is entered and when it is moved. */
static void draw_hall_marks(void)
{
    const unsigned cx = 120U;
    const unsigned row = 1U - orch_stage_listener_depth() / 60U;
    const unsigned half = 30U + row * 10U;
    const int x = (int)cx + orch_stage_listener_x() * (int)half / 100;
    disc((unsigned)x, SEAT_Y + row * 9U, 3U, C_AMBER2, C_AMBER, C_SHELL);
    draw_instrument_name();
}

/* The light from the podium: what spills off the front of the boards, as wide
 * as the orchestra is big and as bright as the music is loud. It follows
 * orch_output_level(), which is calibrated on this hall's own orchestra — eight
 * voices fill it, 24 of 24 — and which narrows as the sound dies, so a chord
 * left ringing takes the light down while the keys are still down. It is the
 * one part of the page that moves with the music at the panel's own rate.
 *
 * It is drawn in a trough of the hall's own dark colour and rebuilt from four
 * bars, so an update is five fills and nothing under it has to be repainted. */
#define LIP_X0 68U
#define LIP_X1 172U
#define LIP_Y 145U                  /* the dark floor in front of the boards */
#define LIP_H 7U
#define LIP_DARK 0x1860             /* that floor's own colour, out of the photo */
#define LIP_MS 50U                  /* slower than the control task scans */
static void draw_stage_light(unsigned force)
{
    static uint32_t drawn_at;
    const uint32_t now = fm1_millis();
    if (!force && (uint32_t)(now - drawn_at) < LIP_MS) return;
    drawn_at = now;

    const unsigned w = LIP_X1 - LIP_X0;
    fm1_screen_fill(LIP_X0, LIP_Y, w, LIP_H, LIP_DARK);
    const unsigned level = orch_output_level();
    if (!level) return;
    /* a cone, not a line: narrow where it leaves the boards, wider as it falls
     * on the floor in front of them — three bands of the light, and the level
     * is 0..24, the orchestra's own scale, so eight voices fill the width */
    const unsigned lit = (w - 8U) * level / ORCH_LEVEL_MAX;
    const int cx = (int)(LIP_X0 + w / 2U);
    const int half = (int)lit / 2;
    const uint16_t dim = mix565(C_AMBER, LIP_DARK, 150U);
    const uint16_t mid = mix565(C_AMBER, LIP_DARK, 70U);
    fm1_screen_fill((unsigned)(cx - half / 2), LIP_Y, (unsigned)half, 3U, dim);
    fm1_screen_fill((unsigned)(cx - half * 3 / 4), LIP_Y + 3U,
                    (unsigned)(half * 3 / 2), 3U, mid);
    fm1_screen_fill((unsigned)(cx - half), LIP_Y + 6U, lit, 1U, C_AMBER);
    fm1_screen_fill((unsigned)(cx - half / 3), LIP_Y + 1U,
                    (unsigned)(half * 2 / 3), 1U, C_AMBER2);
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
    draw_stage_light(1);
}

/* A setting changed, or a part started or stopped: the photograph stays, only
 * the lamps, the players and the marks are redrawn. */
static void page_parts(void)
{
    draw_hall_overlays();
}

/* The hall's lights changed: only the band that differs is repainted, under
 * the marks — a step of the animation costs the band, not the hall. */
static void page_parts_photo(unsigned f)
{
    draw_photo_band(f);
}
