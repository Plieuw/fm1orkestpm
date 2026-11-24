/* --- drawing ------------------------------------------------------------- */

/* The panel writes to the screen and cannot read it back, so nothing here
 * blends against what is already there: every shape takes the colour it is
 * drawn on (bg) and mixes towards it at the edges. That is what makes a disc
 * look round instead of stepped — the display is 240 pixels wide and the
 * screen is the only graphics the instrument has. */

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

/* A vertical gradient, one colour a row: the cheapest way to stop a field
 * looking like a flat block. */
static void grad(unsigned x, unsigned y, unsigned w, unsigned h,
                 uint16_t top, uint16_t bottom)
{
    for (unsigned r = 0; r < h; ++r)
        fm1_screen_fill(x, y + r, w, 1U,
                        mix565(top, bottom, h > 1U ? r * 255U / (h - 1U) : 0U));
}

/* A filled disc, shaded from its core to its edge and softened against bg.
 * Used for every knob, every player and the listener's seat. */
static void disc(unsigned cx, unsigned cy, unsigned r, uint16_t core,
                 uint16_t edge, uint16_t bg)
{
    if (!r) return;
    for (unsigned dy = 0; dy <= r; ++dy) {
        const unsigned half = isqrt(r * r - dy * dy);
        if (!half) continue;
        for (unsigned dx = 0; dx < half; ++dx) {
            /* the shade of the ring this pixel sits in */
            const unsigned d = isqrt(dx * dx + dy * dy);
            const uint16_t c = mix565(core, edge, d * 255U / r);
            /* the pixel covering only part of the disc falls toward bg */
            const unsigned cov = half - d < 2U && half > 2U ? 128U : 0U;
            const uint16_t px = cov ? mix565(c, bg, cov) : c;
            const unsigned x = cx - half + dx, y = cy - dy;
            fm1_screen_fill(x, y, 1U, 1U, px);
            if (dy) fm1_screen_fill(x, cy + dy, 1U, 1U, px);
            if (dx) {
                const unsigned xr = cx + half - dx;
                fm1_screen_fill(xr, y, 1U, 1U, px);
                if (dy) fm1_screen_fill(xr, cy + dy, 1U, 1U, px);
            }
        }
    }
}

/* A capsule between two points, softened at its sides: the pen the arcs and
 * the curves are drawn with. */
static void pen(int x0, int y0, int x1, int y1, unsigned w, uint16_t fg,
                uint16_t bg)
{
    const int dx = x1 - x0, dy = y1 - y0;
    const int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                          ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    const int half = (int)w / 2;
    if (steps <= 0) { fm1_screen_fill((unsigned)x0, (unsigned)y0, w, w, fg); return; }
    for (int i = 0; i <= steps; ++i) {
        const int x = x0 + dx * i / steps, y = y0 + dy * i / steps;
        for (int oy = -half; oy <= half; ++oy)
            for (int ox = -half; ox <= half; ++ox) {
                const unsigned px = (unsigned)(x + ox), py = (unsigned)(y + oy);
                if (px < SW && py < SW) fm1_screen_fill(px, py, 1U, 1U, fg);
            }
        /* the soft shoulder, on the sides only */
        if (half) {
            const unsigned px = (unsigned)(x + half + 1), py = (unsigned)y;
            if (px < SW) fm1_screen_fill(px, py, 1U, 1U, mix565(fg, bg, 96U));
        }
    }
}

/* An arc of an annulus, a0..a1 in 256ths of a turn from twelve o'clock, drawn
 * as short capsules: a knob's value, a meter's scale, a stage light. */
static const int8_t dir_x[32] = {
    0, 12, 24, 35, 45, 53, 59, 63, 64, 63, 59, 53, 45, 35, 24, 12,
    0, -12, -24, -35, -45, -53, -59, -63, -64, -63, -59, -53, -45, -35, -24, -12
};
static const int8_t dir_y[32] = {
    -64, -63, -59, -53, -45, -35, -24, -12, 0, 12, 24, 35, 45, 53, 59, 63,
    64, 63, 59, 53, 45, 35, 24, 12, 0, -12, -24, -35, -45, -53, -59, -63
};

static void arc_soft(unsigned cx, unsigned cy, unsigned r, unsigned a0,
                     unsigned a1, unsigned w, uint16_t fg, uint16_t bg)
{
    if (a1 <= a0) return;
    for (unsigned a = a0; a <= a1; ++a) {
        const unsigned i = (a >> 3) & 31U;
        const int x = (int)cx + dir_x[i] * (int)r / 64;
        const int y = (int)cy + dir_y[i] * (int)r / 64;
        if (a > a0) {
            const unsigned j = ((a - 1U) >> 3) & 31U;
            const int px = (int)cx + dir_x[j] * (int)r / 64;
            const int py = (int)cy + dir_y[j] * (int)r / 64;
            pen(px, py, x, y, w, fg, bg);
        }
        if (a == a0 || a == a1) pen(x, y, x, y, w, fg, bg);
    }
}

/* A curve through points, for levels over time: the sequencer's pitches, the
 * delay's echoes, the ensemble's wobble. */
static void curve(const unsigned char *v, unsigned n, unsigned x, unsigned y,
                  unsigned w, unsigned h, uint16_t fg, uint16_t bg)
{
    int px = -1, py = -1;
    for (unsigned i = 0; i < n; ++i) {
        const int cx = (int)x + (n > 1U ? (int)(i * (w - 1U) / (n - 1U)) : 0);
        const int cy = (int)(y + h - 1U) - (int)((v[i] * (h - 1U)) / 100U);
        if (px >= 0) pen(px, py, cx, cy, 2U, fg, bg);
        px = cx; py = cy;
    }
}
