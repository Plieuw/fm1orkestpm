/* --- the envelope and the LFO --------------------------------------------- */

/* Two pages for what a part does to every note it plays, both drawn as the
 * shape they make: the envelope as its four stages on a time line, the LFO as
 * two cycles of its wave at its depth. The numbers are on the strip. They have
 * no photograph of their own: the card runs down to the strip. */
#define SHAPE_CARD_H (STRIP_Y - BODY_Y - 12U)

/* The ADSR as a line: attack up, decay down to the sustain, a held stretch,
 * the release to nought. The time axis is the sum of the three times plus a
 * fixed hold, so a long release looks long next to a short attack. */
static void page_env(void)
{
    const struct orch_part *part = orch_part_get(ui.part);
    if (!part) return;
    const unsigned x0 = 14U, w = SW - 28U;
    const unsigned top = BODY_Y + 22U, h = SHAPE_CARD_H - 34U;
    const unsigned base = top + h;
    const unsigned a = part->adsr.attack_ms, d = part->adsr.decay_ms;
    const unsigned r = part->adsr.release_ms, s = part->adsr.sustain_percent;
    const unsigned hold = 400U;
    const unsigned total = a + d + hold + r;

    card(6U, BODY_Y + 4U, SW - 12U, SHAPE_CARD_H, C_CARD);
    fm1_screen_fill(x0, base, w, 1U, C_LINE);

    const unsigned xa = x0 + a * w / total;
    const unsigned xd = xa + d * w / total;
    const unsigned xs = xd + hold * w / total;
    const unsigned xr = x0 + w - 1U;
    const unsigned ys = base - s * h / 100U;
    pen((int)x0, (int)base, (int)xa, (int)top, 2U, C_AMBER, C_CARD);
    pen((int)xa, (int)top, (int)xd, (int)ys, 2U, C_AMBER, C_CARD);
    pen((int)xd, (int)ys, (int)xs, (int)ys, 2U, C_METER, C_CARD);
    pen((int)xs, (int)ys, (int)xr, (int)base, 2U, C_AMBER, C_CARD);
    disc(xa, top, 3U, C_AMBER2, C_AMBER, C_CARD);
    disc(xd, ys, 3U, C_AMBER2, C_AMBER, C_CARD);
    disc(xs, ys, 3U, C_AMBER2, C_AMBER, C_CARD);

    text(12U, BODY_Y + 8U, inst_short(part->instrument), C_DIM, C_CARD, 100U);
    text(SW - 12U - span("ADSR"), BODY_Y + 8U, "ADSR", C_DIM, C_CARD, 40U);
}

/* Two cycles of the wave, as tall as the depth, and its destinations as three
 * lamps under it. The rate is on the strip in tenths of a hertz. */
static void page_lfo(void)
{
    const struct orch_part *part = orch_part_get(ui.part);
    if (!part) return;
    const unsigned x0 = 14U, w = SW - 28U;
    const unsigned mid = BODY_Y + 16U + (SHAPE_CARD_H - 40U) / 2U;
    const unsigned amp = (SHAPE_CARD_H - 52U) / 2U * part->lfo.depth / 100U + 1U;
    const unsigned dest = part->lfo.destinations;
    unsigned char v[32];

    card(6U, BODY_Y + 4U, SW - 12U, SHAPE_CARD_H, C_CARD);
    fm1_screen_fill(x0, mid, w, 1U, C_LINE);
    for (unsigned i = 0; i < 32U; ++i) {
        const unsigned ph = (i * 2U * 256U / 32U) & 255U;   /* two cycles */
        int y;                                             /* -100..100 */
        switch (part->lfo.wave) {
        case ORCH_LFO_TRIANGLE:
            y = ph < 128U ? (int)ph * 200 / 128 - 100 : 300 - (int)ph * 200 / 128;
            break;
        case ORCH_LFO_SAW: y = (int)ph * 200 / 255 - 100; break;
        case ORCH_LFO_SQUARE: y = ph < 128U ? 100 : -100; break;
        case ORCH_LFO_SAMPLE_HOLD: y = (int)(((i / 4U) * 2654435761U) >> 25) - 64; break;
        default: {                                         /* sine, by parabolas */
            const int q = (int)(ph & 127U) - 64;
            y = 100 - q * q * 100 / 4096;
            if (ph >= 128U) y = -y;
            break;
        }
        }
        v[i] = (unsigned char)(50 + y / 2);                /* 0..100 for curve() */
    }
    curve(v, 32U, x0, mid - amp, w, 2U * amp,
          dest ? C_METER : C_DIM, C_CARD);

    {
        static const char *const names[3] = { "PITCH", "TREM", "BRIGHT" };
        for (unsigned i = 0; i < 3U; ++i) {
            const unsigned x = 20U + i * 72U;
            const int lit = (dest >> i) & 1U;
            disc(x, BODY_Y + SHAPE_CARD_H - 10U, 3U, lit ? C_AMBER2 : C_LINE,
                 lit ? C_AMBER : C_LINE, C_CARD);
            text(x + 8U, BODY_Y + SHAPE_CARD_H - 14U, names[i],
                 lit ? C_TEXT : C_DIM, C_CARD, 60U);
        }
    }
    text(12U, BODY_Y + 8U, inst_short(part->instrument), C_DIM, C_CARD, 100U);
    text(SW - 12U - span(wave_name(part->lfo.wave)), BODY_Y + 8U,
         wave_name(part->lfo.wave), C_DIM, C_CARD, 60U);
}
