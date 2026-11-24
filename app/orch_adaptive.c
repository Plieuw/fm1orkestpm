/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "orch_adaptive.h"

/* Time constants in samples at 44.1 kHz. */
#define MS(x) ((uint32_t)(x) * (ORCH_ADAPTIVE_SR / 1000U))
#define SEC(x) (MS((uint32_t)((x) * 1000.0f)))

#define DEFAULT_IOI   SEC(0.6)
#define DEFAULT_DUTY  ((uint32_t)ORCH_Q16 * 4U / 5U)
#define DEFAULT_GAP   SEC(9)

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
static uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
/* Q16 * Q16, 64-bit intermediate (both operands may be large). */
static uint32_t mul_q16(uint32_t a, uint32_t b)
{
    return (uint32_t)(((uint64_t)a * (uint64_t)b) >> 16);
}
/* Q16 * samples */
static uint32_t mul_q16_samples(uint32_t a, uint32_t samples)
{
    return (uint32_t)(((uint64_t)a * (uint64_t)samples) >> 16);
}

void orch_adaptive_init(struct orch_adaptive *a)
{
    if (!a) return;
    a->prev_note = -1;
    a->ioi_avg = DEFAULT_IOI;
    a->gap_avg = 0U;
    a->last_on = 0U;
    a->off_now = 0U;
    a->peak_note = 0U;
    a->duty_avg = (uint16_t)DEFAULT_DUTY;
    a->vel_avg = 160U; /* Q8, ~0.625 */
    a->have_prev = 0U;
    a->have_off = 0U;
    a->idx = 0U;
    a->n_hist = 0U;
    a->gap_n = 0U;
    a->phrase_n = 0U;
    a->vel_n = 0U;
    a->density = 2U;
    a->held_notes = 0U;
}

void orch_adaptive_reset_phrase(struct orch_adaptive *a)
{
    if (!a) return;
    a->idx = 0U;
    a->n_hist = 0U;
    a->gap_n = 0U;
    a->have_prev = 0U;
    a->have_off = 0U;
    a->peak_note = 0U;
    a->density = 2U;
    a->held_notes = 0U;
}

unsigned orch_adaptive_resolve(unsigned articulation, unsigned length_class)
{
    if (articulation != ORCH_ART_AUTO && articulation < ORCH_ART_COUNT)
        return articulation;
    switch (length_class) {
    case ORCH_LEN_TIED: return ORCH_ART_LEGATO;
    case ORCH_LEN_SHORT: return ORCH_ART_STACCATO;
    case ORCH_LEN_FAST: return ORCH_ART_SPICCATO;
    case ORCH_LEN_ACCENTED: return ORCH_ART_MARTELE;
    case ORCH_LEN_NORMAL:
    default: return ORCH_ART_DETACHE;
    }
}

/* Deterministic phrase arc over eight phrases (Himalia v1.34), Q16. */
static const uint32_t ORCH_ARC[8] = {
    62259U, 64225U, 65536U, 67502U, 69468U, 70778U, 66846U, 63570U
};

void orch_adaptive_note(struct orch_adaptive *a, struct orch_intent *out,
                        unsigned note, unsigned velocity, uint32_t now,
                        unsigned articulation_fixed)
{
    if (!a || !out || note > 127U) return;

    const uint32_t gap = a->have_off ? (now - a->off_now) : DEFAULT_GAP;
    const uint32_t ioi = a->have_prev ? (now - a->last_on) : a->ioi_avg;

    /* --- phrase continuity ------------------------------------------------ */
    const int rest = gap > SEC(0.25) && gap > (a->ioi_avg * 3U) / 5U &&
                     (a->gap_n < 2U || gap > (a->gap_avg * 17U) / 10U + MS(50));
    const int new_phrase = !a->have_prev ||
                           (a->held_notes == 0U && (rest ||
                            ioi > (a->ioi_avg * 12U) / 5U || gap > SEC(1.2)));
    const uint32_t conn_limit = (a->ioi_avg * 15U) / 100U;
    const uint32_t conn_cap = conn_limit < MS(40) ? conn_limit : MS(40);
    const int connected = !new_phrase && (a->held_notes > 0U || gap < conn_cap);

    if (gap < SEC(3)) {
        a->gap_avg = a->gap_n ? (a->gap_avg * 7U + gap * 3U) / 10U : gap;
        if (a->gap_n < 255U) ++a->gap_n;
    }
    if (!a->have_prev) a->gap_n = 0U;

    const int interval = new_phrase ? 0 : ((int)note - (int)a->prev_note);
    const uint32_t note_q8 = note * 256U;
    if (new_phrase) {
        a->idx = 0U;
        a->peak_note = (uint16_t)note_q8;
        if (a->have_prev) ++a->phrase_n;
    }

    /* --- gate length as information --------------------------------------- */
    if (a->have_prev && ioi < SEC(3) && ioi > 0U) {
        const uint32_t g = (a->have_off && a->off_now > a->last_on)
                               ? clamp_u32(a->off_now - a->last_on, 0U, ioi)
                               : ioi;
        const uint32_t duty = (g * ORCH_Q16) / ioi;
        const uint32_t d = a->n_hist ? (a->duty_avg + duty) / 2U : duty;
        a->duty_avg = (uint16_t)(d > 65535U ? 65535U : d);
        if (a->n_hist < 255U) ++a->n_hist;
    }

    const uint32_t pred_ioi = a->n_hist ? a->ioi_avg : DEFAULT_IOI;
    const uint32_t pred_gate = mul_q16_samples(a->duty_avg, pred_ioi);
    const uint32_t duty_now = pred_ioi ? (pred_gate * ORCH_Q16) / pred_ioi : 0U;

    unsigned length_class;
    if (!a->n_hist || connected || duty_now > (ORCH_Q16 * 9U) / 10U)
        length_class = ORCH_LEN_TIED;
    else if (pred_ioi < MS(110))
        length_class = ORCH_LEN_FAST;
    else if (duty_now < (ORCH_Q16 * 55U) / 100U || pred_gate < MS(180))
        length_class = (pred_ioi < MS(280)) ? ORCH_LEN_FAST : ORCH_LEN_SHORT;
    else
        length_class = ORCH_LEN_NORMAL;

    /* --- energy, attack, vibrato, accent ---------------------------------- */
    int32_t e = 56360 + (int32_t)(a->idx < 4U ? a->idx : 4U) * 2949;
    int32_t vib = ORCH_Q16;
    int32_t att = ORCH_Q16;
    int32_t acc = ORCH_Q16;
    int new_high = 0;

    const int clamped_interval = interval < -7 ? -7 : (interval > 7 ? 7 : interval);
    e += 1442 * clamped_interval;

    if (!new_phrase && note_q8 > (uint32_t)a->peak_note + 128U) {
        e += 6554;
        vib += 13107;
        acc = (int32_t)mul_q16((uint32_t)acc, 78643U); /* *1.2 */
        a->peak_note = (uint16_t)note_q8;
        new_high = 1;
    }
    /* faster than the music around it drives the line forward; slower
     * broadens it (Himalia v1.36) */
    if (!new_phrase && a->n_hist >= 3U && ioi < SEC(3) && ioi > MS(5) &&
        a->ioi_avg > MS(50)) {
        const uint32_t r = (a->ioi_avg * 100U) / ioi; /* percent */
        if (r > 112U) {
            const uint32_t over = r > 200U ? 100U : r - 100U;
            e += (int32_t)((5243U * over) / 100U);
            att = (int32_t)mul_q16((uint32_t)att, 58982U); /* *0.9 */
        } else if (r < 88U) {
            const uint32_t under = r < 28U ? 60U : 100U - r;
            e -= 2621;
            vib += (int32_t)((16384U * under) / 100U);
        }
    }
    {
        const int leap = interval < 0 ? -interval : interval;
        if (leap >= 5) {
            att = (int32_t)mul_q16((uint32_t)att, 72090U); /* *1.1 */
            vib += 6554;
        }
        if (!new_phrase && leap == 0)
            e = (a->idx & 1U) ? (e * 97) / 100 : (e * 102) / 100;
    }
    if (new_phrase) att = (int32_t)mul_q16((uint32_t)att, 81920U); /* *1.25 */

    /* vibrato from the note length: quick notes hardly any */
    if (pred_ioi < MS(220)) vib = (vib * 35) / 100;
    else if (pred_ioi < MS(500)) {
        const uint32_t t = ((pred_ioi - MS(220)) * 100U) / MS(280);
        vib = (vib * (int32_t)(35U + (65U * t) / 100U)) / 100;
    }
    /* arc over eight phrases */
    {
        const uint32_t arc = ORCH_ARC[a->phrase_n & 7U];
        e = (int32_t)mul_q16((uint32_t)(e < 0 ? 0 : e), arc);
        if (arc > ORCH_Q16) vib += (int32_t)((arc - ORCH_Q16) * 3U / 2U);
    }
    /* heavy beats: a down-bow and a little weight */
    if (!new_phrase && (a->idx & 3U) == 0U) {
        e += e / 20;
        acc = (int32_t)mul_q16((uint32_t)acc, 81920U);
    }
    /* a short or normal note on a heavy beat gets a bitten martele */
    if ((new_phrase || (a->idx & 3U) == 0U) && a->n_hist &&
        (length_class == ORCH_LEN_NORMAL || length_class == ORCH_LEN_SHORT) &&
        pred_ioi >= MS(200)) {
        length_class = ORCH_LEN_ACCENTED;
    }
    /* fast notes are attacked crisper, slow ones placed: sqrt(ioi/0.5 s) */
    {
        static const uint32_t SQRT[8] = {
            46341U, 52429U, 57926U, 62730U, 65536U, 70938U, 75000U, 78500U
        };
        const uint32_t base = pred_ioi ? pred_ioi : DEFAULT_IOI;
        uint32_t i = (base * 8U) / MS(2000);
        if (base >= MS(2000)) i = 7U;
        att = (int32_t)mul_q16((uint32_t)att, SQRT[i < 8U ? i : 7U]);
    }

    e = clamp_i32(e, 39322, 95027);     /* 0.60 .. 1.45 */
    att = clamp_i32(att, 39322, 104858); /* 0.6 .. 1.6 */
    vib = clamp_i32(vib, 9830, 104858);  /* 0.15 .. 1.6 */
    acc = clamp_i32(acc, 32768, 163840); /* 0.5 .. 2.5 */

    /* divisi density follows the phrase, as an arranger writes it */
    {
        int d = new_phrase ? 2 : (int)a->density + 1;
        if (new_high && interval >= 3) ++d;
        if (d < 1) d = 1;
        if (d > (int)ORCH_DIVISI_MAX) d = (int)ORCH_DIVISI_MAX;
        a->density = (uint8_t)d;
    }

    out->energy = (uint32_t)e;
    out->attack_mul = (uint32_t)att;
    out->release_mul = ORCH_Q16;
    out->vib_intent = (uint32_t)vib;
    out->accent = (uint32_t)acc;
    out->glide_ms = 0U;
    out->articulation = (uint8_t)orch_adaptive_resolve(articulation_fixed, length_class);
    out->length_class = (uint8_t)length_class;
    out->density = a->density;
    out->phrase_start = (uint8_t)(new_phrase ? 1U : 0U);
    out->phrase_end = 0U;
    out->connected = (uint8_t)(connected ? 1U : 0U);
    out->down_bow = (uint8_t)((!new_phrase && (a->idx & 3U) == 0U) ? 1U : 0U);
    out->new_high = (uint8_t)new_high;

    /* glide: connected notes travel further for bigger intervals */
    if (connected) {
        const int leap = interval < 0 ? -interval : interval;
        uint32_t ms = (out->articulation == ORCH_ART_PORTAMENTO) ? 200U
                    : (out->articulation == ORCH_ART_LEGATO) ? 30U
                    : (uint32_t)(12 + leap * 3);
        if (ms > 250U) ms = 250U;
        out->glide_ms = (uint16_t)ms;
    }

    /* --- history ---------------------------------------------------------- */
    if (ioi < SEC(3) && a->have_prev)
        a->ioi_avg = (a->ioi_avg * 7U + ioi * 3U) / 10U;
    a->prev_note = (int16_t)note;
    a->have_prev = 1U;
    if (a->idx < 255U) ++a->idx;
    a->last_on = now;
    if (a->held_notes < 255U) ++a->held_notes;

    (void)velocity;
}

void orch_adaptive_velocity(struct orch_adaptive *a, struct orch_intent *io,
                            unsigned velocity)
{
    if (!a || !io || velocity > 127U) return;
    const int32_t vel = (int32_t)(velocity * 2U); /* Q8, 0..254 */
    const int32_t dv = a->vel_n ? vel - (int32_t)a->vel_avg : 0;
    if (a->vel_n) a->vel_avg = (uint16_t)(((int32_t)a->vel_avg * 3 + vel) / 4);
    else a->vel_avg = (uint16_t)vel;
    if (a->vel_n < 255U) ++a->vel_n;

    /* ev = 1 + 0.30 * dv/256, clamped to 0.6 .. 1.5 */
    int32_t ev = ORCH_Q16 + (dv * 30 * ORCH_Q16) / (100 * 256);
    ev = clamp_i32(ev, 39322, 98304);
    io->energy = clamp_u32(mul_q16(io->energy, (uint32_t)ev), 39322U, 98304U);

    int32_t acc = ORCH_Q16;
    if (dv > 30) acc = ORCH_Q16 + (dv - 30) * 220; /* up to ~2.5 */
    else if (dv < -30) acc = (ORCH_Q16 * 7) / 10;
    io->accent = clamp_u32(mul_q16(io->accent, (uint32_t)acc), 26214U, 163840U);

    /* a clearly harder note opens the divisi one part, a softer one closes it */
    if (dv > 38 && io->density < ORCH_DIVISI_MAX) ++io->density;
    else if (dv < -38 && io->density > 1U) --io->density;
}

void orch_adaptive_off(struct orch_adaptive *a, uint32_t now)
{
    if (!a) return;
    a->off_now = now;
    a->have_off = 1U;
    if (a->held_notes) --a->held_notes;
}
