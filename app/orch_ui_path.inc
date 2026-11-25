/* --- paths --------------------------------------------------------------- */

/* The pages are built from paths, not from loops over pixels. A path is a list
 * of points in 1/16ths of a pixel (0.6.0; whole pixels before); curves and arcs
 * are flattened into that list when the path is built, so the rasterizer only
 * sees straight edges. It samples every row at four heights and every edge to
 * a sixteenth of a pixel, keeps the coverage of each pixel of the row, and
 * then sends a run of fully covered pixels as one fill and an edge pixel as a
 * one-pixel fill of its mixed colour: anti-aliased in both directions, where
 * 0.5.0 sampled one height a row (a shallow two-pixel line broke into dashes)
 * and placed points on whole pixels (a circle had 32 corners at 1/64 of its
 * radius each). On the board every fill call is a window command plus a
 * transfer, so the number of calls is still the cost that matters.
 *
 * There is no framebuffer to read, so edge pixels are mixed towards the colour
 * the shape is drawn on: the caller passes it. */

#define VP_MAX 192U            /* points in one path, and in one outline */
#define VP_SUB 16              /* subpixel steps a pixel */

struct vpath {
    unsigned n;
    int16_t x[VP_MAX], y[VP_MAX];    /* 1/16 px */
};

static struct vpath vp ORCH_UI_DATA;      /* the path being built */
static struct vpath vp_out ORCH_UI_DATA;  /* the outline of a stroke, built from vp */
static uint8_t vp_cov[SW] ORCH_UI_DATA;   /* one row's coverage, 0..255 */

/* 256ths of a turn from twelve o'clock, in 1/64ths of the radius: the knob
 * pointers and the cap placement use these. */
static const int8_t dir_x[32] = {
    0, 12, 24, 35, 45, 53, 59, 63, 64, 63, 59, 53, 45, 35, 24, 12,
    0, -12, -24, -35, -45, -53, -59, -63, -64, -63, -59, -53, -45, -35, -24, -12
};
static const int8_t dir_y[32] = {
    -64, -63, -59, -53, -45, -35, -24, -12, 0, 12, 24, 35, 45, 53, 59, 63,
    64, 63, 59, 53, 45, 35, 24, 12, 0, -12, -24, -35, -45, -53, -59, -63
};

/* sin over a quarter turn in 64 steps, Q14: the arcs use it */
static const uint16_t vp_sinq[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756,
    5139, 5520, 5897, 6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102,
    9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406,
    12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635,
    14811, 14978, 15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986,
    16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384
};

/* sin and cos of a in 256ths of a turn, Q14 */
static int vp_sin14(unsigned a)
{
    a &= 255U;
    if (a < 64U) return (int)vp_sinq[a];
    if (a < 128U) return (int)vp_sinq[128U - a];
    if (a < 192U) return -(int)vp_sinq[a - 128U];
    return -(int)vp_sinq[256U - a];
}
static int vp_cos14(unsigned a) { return vp_sin14(a + 64U); }

static void vp_reset(void)
{
    vp.n = 0U;
}

/* a point in 1/16 px */
static void vp_add16(int x, int y)
{
    if (vp.n >= VP_MAX) return;
    if (x < -32000) x = -32000;
    if (x > 32000) x = 32000;
    if (y < -32000) y = -32000;
    if (y > 32000) y = 32000;
    vp.x[vp.n] = (int16_t)x;
    vp.y[vp.n] = (int16_t)y;
    ++vp.n;
}

/* a point on a pixel's centre: the old integer grid, so callers keep their
 * coordinates */
static void vp_add(int x, int y)
{
    vp_add16(x * VP_SUB + VP_SUB / 2, y * VP_SUB + VP_SUB / 2);
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
    const int x1 = x * VP_SUB + VP_SUB / 2, y1 = y * VP_SUB + VP_SUB / 2;
    const int qx = cx * VP_SUB + VP_SUB / 2, qy = cy * VP_SUB + VP_SUB / 2;
    const int x0 = vp.n ? vp.x[vp.n - 1U] : x1;
    const int y0 = vp.n ? vp.y[vp.n - 1U] : y1;
    const int dx = (qx - x0) / VP_SUB, dy = (qy - y0) / VP_SUB;
    const unsigned bend = (unsigned)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
    unsigned steps = 4U + bend / 4U;
    if (steps > 24U) steps = 24U;
    for (unsigned i = 1U; i <= steps; ++i) {
        const int t = (int)(i * 64U / steps);      /* 1/64ths along */
        const int u = 64 - t;
        vp_add16((x0 * u * u + 2 * qx * u * t + x1 * t * t) / 4096,
                 (y0 * u * u + 2 * qy * u * t + y1 * t * t) / 4096);
    }
}

static void vp_arc_point(int cx, int cy, unsigned r, unsigned a)
{
    const int r16 = (int)r * VP_SUB;
    vp_add16(cx * VP_SUB + VP_SUB / 2 + ((vp_sin14(a) * r16) >> 14),
             cy * VP_SUB + VP_SUB / 2 - ((vp_cos14(a) * r16) >> 14));
}

/* An arc of a circle, a0..a1 in 256ths of a turn from twelve o'clock. */
static void vp_arc(int cx, int cy, unsigned r, unsigned a0, unsigned a1)
{
    if (a1 <= a0) return;
    unsigned steps = (a1 - a0) * (r + 4U) / 48U + 2U;
    if (steps > 96U) steps = 96U;
    if (!vp.n) vp_arc_point(cx, cy, r, a0);
    for (unsigned i = 1U; i <= steps; ++i)
        vp_arc_point(cx, cy, r, a0 + (a1 - a0) * i / steps);
}

/* The same arc walked the other way: a ring goes out along its rim and back
 * along its inner edge, and a capsule's end is a half circle that has to bulge
 * past the point it is drawn around. */
static void vp_arc_rev(int cx, int cy, unsigned r, unsigned a0, unsigned a1)
{
    if (a1 >= a0) return;
    unsigned steps = (a0 - a1) * (r + 4U) / 48U + 2U;
    if (steps > 96U) steps = 96U;
    if (!vp.n) vp_arc_point(cx, cy, r, a0);
    for (unsigned i = 1U; i <= steps; ++i)
        vp_arc_point(cx, cy, r, a0 - (a0 - a1) * i / steps);
}

/* A whole circle. */
static void vp_circle(int cx, int cy, unsigned r)
{
    vp_reset();
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

/* Add one sub-row's span [a, b) (1/16 px) to the row's coverage; a sub-row is
 * a quarter of the row, so a fully covered pixel gains 64 (255 after four). */
static void vp_cover(int32_t a, int32_t b, int *lo, int *hi)
{
    if (a < 0) a = 0;
    if (b > (int32_t)SW * VP_SUB) b = (int32_t)SW * VP_SUB;
    if (b <= a) return;
    const int x0 = (int)(a / VP_SUB), x1 = (int)((b - 1) / VP_SUB);
    if (x0 < *lo) *lo = x0;
    if (x1 > *hi) *hi = x1;
    if (x0 == x1) {
        const int v = vp_cov[x0] + (int)(b - a) * 4;
        vp_cov[x0] = (uint8_t)(v > 255 ? 255 : v);
        return;
    }
    {
        const int v = vp_cov[x0] + (int)((x0 + 1) * VP_SUB - a) * 4;
        vp_cov[x0] = (uint8_t)(v > 255 ? 255 : v);
    }
    for (int x = x0 + 1; x < x1; ++x) {
        const int v = vp_cov[x] + 64;
        vp_cov[x] = (uint8_t)(v > 255 ? 255 : v);
    }
    {
        const int v = vp_cov[x1] + (int)(b - x1 * VP_SUB) * 4;
        vp_cov[x1] = (uint8_t)(v > 255 ? 255 : v);
    }
}

/* Send one row: runs of full pixels as one fill, edge pixels mixed. */
static void vp_emit_row(unsigned y, int lo, int hi, uint16_t colour, uint16_t bg)
{
    int x = lo;
    while (x <= hi) {
        const unsigned c = vp_cov[x];
        if (!c) { ++x; continue; }
        if (c >= 250U) {
            int e = x;
            while (e + 1 <= hi && vp_cov[e + 1] >= 250U) ++e;
            fm1_screen_fill((unsigned)x, y, (unsigned)(e - x + 1), 1U, colour);
            for (int k = x; k <= e; ++k) vp_cov[k] = 0U;
            x = e + 1;
            continue;
        }
        /* edge pixels in eight shades: neighbours of one shade (a shallow
         * edge) go out as one fill */
        const unsigned q = c >> 5;
        int e = x;
        while (e + 1 <= hi && vp_cov[e + 1] && vp_cov[e + 1] < 250U &&
               (vp_cov[e + 1] >> 5) == q) ++e;
        fm1_screen_fill((unsigned)x, y, (unsigned)(e - x + 1), 1U,
                        mix565(bg, colour, q * 32U + 16U));
        for (int k = x; k <= e; ++k) vp_cov[k] = 0U;
        x = e + 1;
    }
}

/* Fill a path, even-odd. When `r` is given the rows are shaded from `core` in
 * the middle to `edge` at the rim, which is how a disc gets its shape. */
static void vp_fill_shaded(const struct vpath *p, uint16_t core,
                           uint16_t edge, uint16_t bg, int r, int cy)
{
    if (p->n < 3U) return;
    int ymin = 32767, ymax = -32768;
    for (unsigned i = 0; i < p->n; ++i) {
        const int y = p->y[i];
        if (y < ymin) ymin = y;
        if (y > ymax) ymax = y;
    }
    int row0 = ymin / VP_SUB, row1 = ymax / VP_SUB;
    if (row0 < 0) row0 = 0;
    if (row1 > (int)SW - 1) row1 = (int)SW - 1;

    for (int y = row0; y <= row1; ++y) {
        int lo = (int)SW, hi = -1;
        for (unsigned sub = 0; sub < 4U; ++sub) {
            const int32_t yc = (int32_t)y * VP_SUB + 2 + (int32_t)sub * 4;
            int32_t xs[VP_MAX];
            unsigned m = 0U;
            for (unsigned i = 0; i < p->n; ++i) {
                const unsigned j = i + 1U < p->n ? i + 1U : 0U;
                const int32_t y0 = p->y[i], y1 = p->y[j];
                if ((y0 <= yc) == (y1 <= yc)) continue;      /* no crossing */
                const int32_t x0 = p->x[i], x1 = p->x[j];
                if (m < VP_MAX)
                    xs[m++] = x0 + (yc - y0) * (x1 - x0) / (y1 - y0);
            }
            if (m < 2U) continue;
            vp_sort(xs, m);
            for (unsigned k = 0; k + 1U < m; k += 2U)
                vp_cover(xs[k], xs[k + 1U], &lo, &hi);
        }
        if (hi < lo) continue;
        uint16_t colour = core;
        if (r > 0 && core != edge) {
            int d = y - cy;
            if (d < 0) d = -d;
            if (d > r) d = r;
            colour = mix565(core, edge, (unsigned)d * 255U / (unsigned)r);
        }
        vp_emit_row((unsigned)y, lo, hi, colour, bg);
    }
}

static void vp_fill(const struct vpath *p, uint16_t colour, uint16_t bg)
{
    vp_fill_shaded(p, colour, colour, bg, 0, 0);
}

/* A stroke along the path: the outline of a pen of width w, filled as one
 * polygon. The normals are taken in 1/16 px, so a two-pixel line keeps its
 * width at every slope. */
static void vp_stroke(const struct vpath *p, unsigned w, uint16_t colour,
                      uint16_t bg)
{
    if (p->n < 2U) return;
    const int half = (int)(w ? w : 1U) * VP_SUB / 2;
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
