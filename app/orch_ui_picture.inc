/* --- pictures ------------------------------------------------------------- */

/* A picture the panel can hold: the palette it was reduced to, and per frame
 * the runs each row is made of. tools/panel_photo.py writes them — one run is
 * three hex characters, the palette index and the run's width in cells — and
 * this is the one place that turns them into fills, so the hall and the four
 * menus are drawn the same way.
 *
 * `block` is the size of a cell on the panel: 2 for the pictures here, which is
 * 240 x 164 for the hall and 240 x 30 for a menu's band. */
struct panel_picture {
    const uint16_t *palette;
    const char *const *frame_runs;
    const uint16_t *const *frame_rows;
    unsigned frames;                  /* frames of the animation */
    unsigned rows;                    /* rows of cells in each of them */
    unsigned block;                   /* pixels a cell is on the panel */
};

static unsigned picture_cell(char c)
{
    return (c >= '0' && c <= '9') ? (unsigned)(c - '0')
                                  : (unsigned)(c - 'a' + 10);
}

/* The rows first..last of one frame, at the corner the caller gives: a band of
 * a picture, for an animation that only moves in some of its rows. */
static void picture_rows(const struct panel_picture *pic, unsigned f,
                         unsigned x0, unsigned y0, unsigned first,
                         unsigned last)
{
    if (f >= pic->frames) f = 0U;
    const char *runs = pic->frame_runs[f];
    const uint16_t *rows = pic->frame_rows[f];

    for (unsigned y = first; y <= last && y < pic->rows; ++y) {
        const unsigned py = y0 + y * pic->block;
        unsigned px = x0;
        for (unsigned i = rows[y]; i < rows[y + 1U]; ++i) {
            const char *run = runs + i * 3U;
            const unsigned w = (picture_cell(run[1]) << 4) | picture_cell(run[2]);
            fm1_screen_fill(px, py, w * pic->block, pic->block,
                            pic->palette[picture_cell(run[0])]);
            px += w * pic->block;
        }
    }
}

static void picture(const struct panel_picture *pic, unsigned f, unsigned x0,
                    unsigned y0)
{
    picture_rows(pic, f, x0, y0, 0U, pic->rows - 1U);
}
