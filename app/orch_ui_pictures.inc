/* --- the menus' own pictures ---------------------------------------------- */

/* Each of the four menus has a picture of its own, taken in the same hall at
 * night and reduced the same way as the hall itself: the harp the arp plays on
 * ARP, the score the sequencer follows on SEQUENCE, the console at the back of
 * the hall on CONSOLE, and the rack the effects live in on FX. They are the
 * source images in tools/panel_photo/ run through tools/panel_photo.py, so they
 * can be rebuilt from the repository alone (tools/panel_photo/regen.sh).
 *
 * These pages are read rather than watched, and a photograph behind a fader or
 * a number is a fight. So the picture is not the page's ground: it is a band
 * under the page's work, a strip of the hall 30 pixels high, held between a
 * hairline and the knob strip, and the numbers stay on the case above it.
 *
 * The band is drawn when the page is entered and left alone after that: a knob
 * turn repaints the values over it, not the picture under them. */

#define PIC_Y 158U                  /* the band's top, under the page's work */
#define PIC_HAIRLINE (PIC_Y - 1U)   /* the line it hangs under */

#include "orch_ui_pic_arp.inc"
#include "orch_ui_pic_seq.inc"
#include "orch_ui_pic_glo.inc"
#include "orch_ui_pic_fx.inc"

static const struct panel_picture pic_arp = {
    pic_arp_palette, pic_arp_frame_runs, pic_arp_frame_rows,
    PIC_ARP_FRAMES, PIC_ARP_ROWS, PIC_ARP_BLOCK
};
static const struct panel_picture pic_seq = {
    pic_seq_palette, pic_seq_frame_runs, pic_seq_frame_rows,
    PIC_SEQ_FRAMES, PIC_SEQ_ROWS, PIC_SEQ_BLOCK
};
static const struct panel_picture pic_glo = {
    pic_glo_palette, pic_glo_frame_runs, pic_glo_frame_rows,
    PIC_GLO_FRAMES, PIC_GLO_ROWS, PIC_GLO_BLOCK
};
static const struct panel_picture pic_fx = {
    pic_fx_palette, pic_fx_frame_runs, pic_fx_frame_rows,
    PIC_FX_FRAMES, PIC_FX_ROWS, PIC_FX_BLOCK
};

/* The picture under a page, or nothing for the pages that have no menu of
 * their own (the stage is the hall, drawn by the stage page). */
static const struct panel_picture *page_picture(unsigned page)
{
    switch (page) {
    case PAGE_ARP: return &pic_arp;
    case PAGE_SEQ: return &pic_seq;
    case PAGE_GLO: return &pic_glo;
    case PAGE_FX: return &pic_fx;
    default: return NULL;
    }
}

/* The band of a page, and the hairline that separates it from the work. */
static void draw_page_picture(unsigned page)
{
    const struct panel_picture *pic = page_picture(page);
    if (!pic) return;
    fm1_screen_fill(0U, PIC_HAIRLINE, SW, 1U, C_LINE);
    picture(pic, 0U, 0U, PIC_Y);
}
