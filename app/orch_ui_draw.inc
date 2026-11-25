/* --- drawing ------------------------------------------------------------- */

/* The screen writes to the display and cannot read it back, so nothing here
 * blends against what is already there: every shape takes the colour it is
 * drawn on (bg) and mixes towards it at the edges. That is what makes a disc
 * look round instead of stepped.
 *
 * Shapes are paths (orch_ui_path.inc): a circle becomes a polygon, an arc a
 * strip between two arcs, a curve a polyline. The rasterizer fills them one
 * span per row, so the cost of a disc is its height in fill calls, not its
 * area — on a display where every call is a window command plus a transfer
 * that is most of the drawing time. */

static uint16_t mix565(uint16_t a, uint16_t b, unsigned t)
{
    if (t > 255U) t = 255U;
    const unsigned ar = (a >> 11) & 31U, ag = (a >> 5) & 63U, ab = a & 31U;
    const unsigned br = (b >> 11) & 31U, bg = (b >> 5) & 63U, bb = b & 31U;
    const unsigned r = (ar * (255U - t) + br * t) / 255U;
    const unsigned g = (ag * (255U - t) + bg * t) / 255U;
    const unsigned bl = (ab * (255U - t) + bb * t) / 255U;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

/* floor(sqrt(v)), bit by bit: no maths library on the board. */
static unsigned isqrt(unsigned v)
{
    unsigned r = 0U, b = 1U << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

#include "orch_ui_path.inc"

/* A filled disc, shaded from its core to its edge and softened against bg:
 * a knob's cap, a player on the stage, the listener's seat, an FX lamp. */
static void disc(unsigned cx, unsigned cy, unsigned r, uint16_t core,
                 uint16_t edge, uint16_t bg)
{
    if (!r) return;
    vp_circle((int)cx, (int)cy, r);
    vp_fill_shaded(&vp, core, edge, bg, (int)r, (int)cy);
}

/* A capsule between two points: the shaft as a quad, the ends as half-discs,
 * so a line has a round end and no seam where the pieces meet. */
static void pen(int x0, int y0, int x1, int y1, unsigned w, uint16_t fg,
                uint16_t bg)
{
    const int half = (int)(w ? (w + 1U) / 2U : 1U);
    int dx = x1 - x0, dy = y1 - y0;
    const unsigned len = isqrt((unsigned)(dx * dx) + (unsigned)(dy * dy));
    if (!len) { disc((unsigned)x0, (unsigned)y0, (unsigned)half, fg, fg, bg); return; }

    const int nx = -dy * half / (int)len, ny = dx * half / (int)len;
    const unsigned a = vp_angle(nx, ny);

    vp_move(x0 + nx, y0 + ny);
    vp_line(x1 + nx, y1 + ny);
    vp_arc_rev(x1, y1, (unsigned)half, a, a - 128U);      /* the far cap */
    vp_line(x0 - nx, y0 - ny);
    vp_arc_rev(x0, y0, (unsigned)half, a - 128U, a - 256U);  /* the near cap */
    vp_fill(&vp, fg, bg);
}

/* An arc of a ring, a0..a1 in 256ths of a turn from twelve o'clock, w pixels
 * wide: a knob's value, a meter's scale, the arch of the stage. */
static void arc_soft(unsigned cx, unsigned cy, unsigned r, unsigned a0,
                     unsigned a1, unsigned w, uint16_t fg, uint16_t bg)
{
    if (a1 <= a0) return;
    const unsigned half = w ? (w + 1U) / 2U : 1U;
    const unsigned outer = r + half;
    const unsigned inner = r > half ? r - half : 0U;

    vp_move((int)cx + vp_cos(a0) * (int)outer / 64,
            (int)cy + vp_sin(a0) * (int)outer / 64);
    vp_arc((int)cx, (int)cy, outer, a0, a1);
    if (inner) vp_arc_rev((int)cx, (int)cy, inner, a1, a0);
    else vp_line((int)cx, (int)cy);
    vp_fill(&vp, fg, bg);
}

/* A curve through values, for levels over time: the sequencer's pitches, the
 * delay's echoes, the ensemble's wobble. It runs through the points as
 * quadratics — each point is the control of the bow between its neighbours —
 * so a phrase reads as a line rather than as a join-the-dots. */
static int curve_x(unsigned i, unsigned n, unsigned x, unsigned w)
{
    return (int)x + (int)(i * (w - 1U) / (n - 1U));
}

static int curve_y(unsigned i, const unsigned char *v, unsigned y, unsigned h)
{
    return (int)(y + h - 1U) - (int)(v[i] * (h - 1U) / 100U);
}

static void curve(const unsigned char *v, unsigned n, unsigned x, unsigned y,
                  unsigned w, unsigned h, uint16_t fg, uint16_t bg)
{
    if (n < 2U) return;
    vp_move(curve_x(0U, n, x, w), curve_y(0U, v, y, h));
    for (unsigned i = 1U; i + 1U < n; ++i) {
        const int midx = (curve_x(i, n, x, w) + curve_x(i + 1U, n, x, w)) / 2;
        const int midy = (curve_y(i, v, y, h) + curve_y(i + 1U, v, y, h)) / 2;
        vp_quad(curve_x(i, n, x, w), curve_y(i, v, y, h), midx, midy);
    }
    vp_line(curve_x(n - 1U, n, x, w), curve_y(n - 1U, v, y, h));
    vp_stroke(&vp, 2U, fg, bg);
}
