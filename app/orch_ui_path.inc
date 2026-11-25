/* --- paths --------------------------------------------------------------- */

/* The pages are built from paths, not from loops over pixels. A path is a list
 * of points in whole pixels; curves and arcs are flattened into that list when
 * the path is built, so the rasterizer only ever sees straight edges and one
 * scanline fill can cover a whole row. On the board every fill call is a window
 * command plus a transfer, so the number of calls is the cost that matters: a
 * disc used to cost a call per pixel and now costs two per row.
 *
 * There is no framebuffer to read, so edge pixels are mixed towards the colour
 * the shape is drawn on: the caller passes it. */

#define VP_MAX 192U            /* points in one path, and in one outline */

struct vpath {
    unsigned n;
    int16_t x[VP_MAX], y[VP_MAX];
};

static struct vpath vp ORCH_UI_DATA;      /* the path being built */
static struct vpath vp_out ORCH_UI_DATA;  /* the outline of a stroke, built from vp */

/* 256ths of a turn from twelve o'clock, in 1/64ths of the radius. */
static const int8_t dir_x[32] = {
    0, 12, 24, 35, 45, 53, 59, 63, 64, 63, 59, 53, 45, 35, 24, 12,
    0, -12, -24, -35, -45, -53, -59, -63, -64, -63, -59, -53, -45, -35, -24, -12
};
static const int8_t dir_y[32] = {
    -64, -63, -59, -53, -45, -35, -24, -12, 0, 12, 24, 35, 45, 53, 59, 63,
    64, 63, 59, 53, 45, 35, 24, 12, 0, -12, -24, -35, -45, -53, -59, -63
};

static int vp_cos(unsigned a) { return dir_x[(a >> 3) & 31U]; }
static int vp_sin(unsigned a) { return dir_y[(a >> 3) & 31U]; }

static void vp_reset(void)
{
    vp.n = 0U;
}

static void vp_add(int x, int y)
{
    if (vp.n >= VP_MAX) return;
    if (x < -2048) x = -2048;
    if (x > 2048) x = 2048;
    if (y < -2048) y = -2048;
    if (y > 2048) y = 2048;
    vp.x[vp.n] = (int16_t)x;
    vp.y[vp.n] = (int16_t)y;
    ++vp.n;
}

static void vp_move(int x, int y)
{
    vp_reset();
    vp_add(x, y);
}

static void vp_line(int x, int y)
{
    vp_add(x, y);
}

/* A quadratic curve, flattened with as many steps as it bends. */
static void vp_quad(int cx, int cy, int x, int y)
{
    const int x0 = vp.n ? vp.x[vp.n - 1U] : x;
    const int y0 = vp.n ? vp.y[vp.n - 1U] : y;
    const int dx = cx - x0, dy = cy - y0;
    const unsigned bend = (unsigned)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
    unsigned steps = 3U + bend / 8U;
    if (steps > 16U) steps = 16U;
    for (unsigned i = 1U; i <= steps; ++i) {
        const int t = (int)(i * 16U / steps);      /* 1/16ths along */
        const int u = 16 - t;
        vp_add((x0 * u * u + 2 * cx * u * t + x * t * t) / 256,
               (y0 * u * u + 2 * cy * u * t + y * t * t) / 256);
    }
}

/* An arc of a circle, a0..a1 in 256ths of a turn from twelve o'clock. */
static void vp_arc(int cx, int cy, unsigned r, unsigned a0, unsigned a1)
{
    if (a1 <= a0) return;
    unsigned steps = (a1 - a0) * r / 96U + 2U;
    if (steps > 64U) steps = 64U;
    if (!vp.n)
        vp_add(cx + vp_cos(a0) * (int)r / 64, cy + vp_sin(a0) * (int)r / 64);
    for (unsigned i = 1U; i <= steps; ++i) {
        const unsigned a = a0 + (a1 - a0) * i / steps;
        vp_add(cx + vp_cos(a) * (int)r / 64, cy + vp_sin(a) * (int)r / 64);
    }
}

/* The same arc walked the other way: a ring goes out along its rim and back
 * along its inner edge, and a capsule's end is a half circle that has to bulge
 * past the point it is drawn around. */
static void vp_arc_rev(int cx, int cy, unsigned r, unsigned a0, unsigned a1)
{
    if (a1 >= a0) return;
    unsigned steps = (a0 - a1) * r / 96U + 2U;
    if (steps > 64U) steps = 64U;
    if (!vp.n)
        vp_add(cx + vp_cos(a0) * (int)r / 64, cy + vp_sin(a0) * (int)r / 64);
    for (unsigned i = 1U; i <= steps; ++i) {
        const unsigned a = a0 - (a0 - a1) * i / steps;
        vp_add(cx + vp_cos(a) * (int)r / 64, cy + vp_sin(a) * (int)r / 64);
    }
}

/* A whole circle: from twelve o'clock round, so the path closes where it
 * started instead of leaving a chord across the top right. */
static void vp_circle(int cx, int cy, unsigned r)
{
    vp_move(cx, cy - (int)r);
    vp_arc(cx, cy, r, 0U, 256U);
}

/* The arc index whose direction best matches (dx, dy), in 1/256ths of a turn:
 * how a cap or a squashed end is placed without a tangent function. */
static unsigned vp_angle(int dx, int dy)
{
    int best = 0, score = -1000000;
    for (unsigned i = 0; i < 32U; ++i) {
        const int s = (int)dir_x[i] * dx + (int)dir_y[i] * dy;
        if (s > score) { score = s; best = (int)i; }
    }
    return (unsigned)best * 8U;
}

/* --- the rasterizer ------------------------------------------------------ */

static void vp_sort(int32_t *v, unsigned n)
{
    for (unsigned i = 1U; i < n; ++i) {          /* a row has few crossings */
        const int32_t key = v[i];
        unsigned j = i;
        while (j && v[j - 1U] > key) { v[j] = v[j - 1U]; --j; }
        v[j] = key;
    }
}

/* One row of a fill: the span [a, b) in 1/16ths of a pixel, with the end pixels
 * mixed towards bg by the part of them the shape actually covers. */
static void vp_span(unsigned y, int32_t a, int32_t b, uint16_t colour,
                    uint16_t bg)
{
    if (b <= a) return;
    if (a < 0) a = 0;
    if (b > (int32_t)SW * 16) b = (int32_t)SW * 16;
    if (b <= a) return;

    const int x0 = (int)(a >> 4), x1 = (int)((b - 1) >> 4);
    const int first = (int)((a + 15) >> 4);      /* first fully covered */
    const int last = (int)((b >> 4) - 1);        /* last fully covered */

    if (first <= last)
        fm1_screen_fill((unsigned)first, y, (unsigned)(last - first + 1), 1U,
                        colour);
    if (x0 < first) {
        const unsigned t = (16U - (unsigned)(a & 15)) * 16U;   /* 0..255 */
        fm1_screen_fill((unsigned)x0, y, 1U, 1U, mix565(bg, colour, t));
    }
    if (last < x1) {
        const unsigned frac = (unsigned)((b & 15) ? (b & 15) : 16);
        fm1_screen_fill((unsigned)x1, y, 1U, 1U,
                        mix565(bg, colour, frac * 16U));
    }
}

/* Fill a path, even-odd, one span per row. When `r` is given the rows are
 * shaded from `core` in the middle to `edge` at the rim, which is how a disc
 * gets its shape. */
static void vp_fill_shaded(const struct vpath *p, uint16_t core,
                           uint16_t edge, uint16_t bg, int r, int cy)
{
    if (p->n < 3U) return;
    int ymin = 4096, ymax = -4096;
    for (unsigned i = 0; i < p->n; ++i) {
        const int y = p->y[i];
        if (y < ymin) ymin = y;
        if (y > ymax) ymax = y;
    }
    if (ymin < 0) ymin = 0;
    if (ymax > (int)SW - 1) ymax = (int)SW - 1;

    for (int y = ymin; y <= ymax; ++y) {
        const int32_t yc = (int32_t)y * 16 + 8;   /* the row's centre line */
        int32_t xs[VP_MAX];
        unsigned m = 0U;
        for (unsigned i = 0; i < p->n; ++i) {
            const unsigned j = i + 1U < p->n ? i + 1U : 0U;
            const int32_t y0 = (int32_t)p->y[i] * 16;
            const int32_t y1 = (int32_t)p->y[j] * 16;
            if ((y0 <= yc) == (y1 <= yc)) continue;      /* no crossing */
            const int32_t x0 = (int32_t)p->x[i] * 16;
            const int32_t x1 = (int32_t)p->x[j] * 16;
            if (m < VP_MAX)
                xs[m++] = x0 + (yc - y0) * (x1 - x0) / (y1 - y0);
        }
        if (m < 2U) continue;
        vp_sort(xs, m);

        uint16_t colour = core;
        if (r > 0 && core != edge) {
            int d = y - cy;
            if (d < 0) d = -d;
            if (d > r) d = r;
            colour = mix565(core, edge, (unsigned)d * 255U / (unsigned)r);
        }
        for (unsigned k = 0; k + 1U < m; k += 2U)
            vp_span((unsigned)y, xs[k], xs[k + 1U], colour, bg);
    }
}

static void vp_fill(const struct vpath *p, uint16_t colour, uint16_t bg)
{
    vp_fill_shaded(p, colour, colour, bg, 0, 0);
}

/* A stroke along the path: the outline of a pen of width w, filled as one
 * polygon. Arcs and curves are drawn with it, and so is the knob's pointer. */
static void vp_stroke(const struct vpath *p, unsigned w, uint16_t colour,
                      uint16_t bg)
{
    if (p->n < 2U) return;
    const int half = (int)(w ? (w + 1U) / 2U : 1U);
    vp_out.n = 0U;

    for (unsigned side = 0; side < 2U; ++side) {
        for (unsigned i = 0; i < p->n; ++i) {
            const unsigned at = side ? p->n - 1U - i : i;
            const unsigned a = at ? at - 1U : 0U;
            const unsigned b = at + 1U < p->n ? at + 1U : p->n - 1U;
            const int dx = p->x[b] - p->x[a], dy = p->y[b] - p->y[a];
            const unsigned len = isqrt((unsigned)(dx * dx) + (unsigned)(dy * dy));
            if (!len) continue;
            const int nx = -dy * half / (int)len, ny = dx * half / (int)len;
            if (vp_out.n >= VP_MAX) break;
            vp_out.x[vp_out.n] = (int16_t)(p->x[at] + (side ? nx : -nx));
            vp_out.y[vp_out.n] = (int16_t)(p->y[at] + (side ? ny : -ny));
            ++vp_out.n;
        }
    }
    if (vp_out.n >= 3U) vp_fill(&vp_out, colour, bg);
}
