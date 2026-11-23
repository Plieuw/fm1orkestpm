/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "orch_divisi.h"

/* Each player slot has its own place in the section and its own intonation.
 * These are per slot, not per note, so a line keeps the same desk colour. */
static const int8_t SLOT_DETUNE[ORCH_DIVISI_MAX] = { 0, 4, -5, 7 };
static const uint8_t SLOT_PAN[ORCH_DIVISI_MAX] = { 96U, 160U, 72U, 184U };

void orch_divisi_init(struct orch_divisi_state *d)
{
    if (!d) return;
    d->players = 0U;
    for (unsigned p = 0; p < ORCH_DIVISI_MAX; ++p) {
        d->note_of_player[p] = 0xFFU;
        d->id_of_player[p] = 0U;
        d->assigned[p] = 0U;
    }
}

static int find_note(const uint8_t *notes, unsigned count, unsigned note)
{
    for (unsigned i = 0; i < count; ++i)
        if (notes[i] == note) return (int)i;
    return -1;
}

void orch_divisi_plan(struct orch_divisi_state *d,
                      const uint8_t *notes, const uint32_t *ids,
                      unsigned count, unsigned density, unsigned player_cap,
                      struct orch_divisi_plan *out)
{
    if (!d || !out) return;

    if (!notes || !ids || count == 0U) {
        orch_divisi_init(d);
        out->players = 0U;
        out->active_mask = 0U;
        for (unsigned p = 0; p < ORCH_DIVISI_MAX; ++p) {
            out->assignment[p] = 0xFFU;
            out->detune_cents[p] = SLOT_DETUNE[p];
            out->pan[p] = SLOT_PAN[p];
        }
        return;
    }

    unsigned players = density < 1U ? 1U : density;
    if (player_cap && players > player_cap) players = player_cap;
    if (players > ORCH_DIVISI_MAX) players = ORCH_DIVISI_MAX;
    if (players < 1U) players = 1U;

    uint8_t taken[ORCH_DIVISI_MAX];
    for (unsigned i = 0; i < ORCH_DIVISI_MAX; ++i) taken[i] = 0U;
    for (unsigned p = 0; p < ORCH_DIVISI_MAX; ++p) out->assignment[p] = 0xFFU;

    /* 1. keep a held note on the player it already had (hysteresis) */
    for (unsigned p = 0; p < players; ++p) {
        if (!d->assigned[p]) continue;
        const int i = find_note(notes, count, d->note_of_player[p]);
        if (i >= 0 && !taken[(unsigned)i]) {
            out->assignment[p] = (uint8_t)i;
            taken[(unsigned)i] = 1U;
        } else {
            d->assigned[p] = 0U;
        }
    }

    /* 2. new notes: prefer the middle of the section for the middle of the
     *    chord, so the outer desks take the outer notes */
    unsigned next_new = 0U;
    for (unsigned p = 0; p < players; ++p) {
        if (out->assignment[p] != 0xFFU) continue;
        unsigned pick = 0xFFU;
        for (unsigned i = 0; i < count; ++i) {
            if (taken[i]) continue;
            const unsigned distance = i > p ? i - p : p - i;
            if (pick == 0xFFU || distance < next_new) {
                pick = i;
                next_new = distance;
            }
        }
        if (pick == 0xFFU) {
            /* more players than notes: double the lowest sounding note */
            pick = 0U;
        } else {
            taken[pick] = 1U;
        }
        out->assignment[p] = (uint8_t)pick;
        d->assigned[p] = 1U;
    }

    /* 3. remember what each player holds */
    for (unsigned p = 0; p < players; ++p) {
        const unsigned i = out->assignment[p];
        d->note_of_player[p] = (i < count) ? notes[i] : 0xFFU;
        d->id_of_player[p] = (i < count) ? ids[i] : 0U;
        out->detune_cents[p] = SLOT_DETUNE[p];
        out->pan[p] = SLOT_PAN[p];
    }
    for (unsigned p = players; p < ORCH_DIVISI_MAX; ++p) {
        d->assigned[p] = 0U;
        out->detune_cents[p] = SLOT_DETUNE[p];
        out->pan[p] = SLOT_PAN[p];
    }
    d->players = (uint8_t)players;
    out->players = (uint8_t)players;
    out->active_mask = (uint8_t)((1U << players) - 1U);
}
