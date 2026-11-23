/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Divisi planner: how many modeled players realize a note, and which of the
 * part's sounding notes each player takes.
 *
 * Himalia gives six hardware players fixed roles in one ensemble. On the FM1
 * the notes come from MIDI, so divisi is a planning problem: distribute the
 * part's notes over the players, double when there are more players than
 * notes, and keep an assignment stable while notes are held (hysteresis).
 */
#ifndef ORCH_DIVISI_H
#define ORCH_DIVISI_H

#include <stdint.h>
#include "orch_adaptive.h"

struct orch_divisi_plan {
    uint8_t players;                       /* 1..ORCH_DIVISI_MAX */
    uint8_t active_mask;                   /* bit p set: player p sounds */
    uint8_t assignment[ORCH_DIVISI_MAX];   /* index into the notes[] given, 0xFF = silent */
    int8_t detune_cents[ORCH_DIVISI_MAX];  /* this player's place in the section */
    uint8_t pan[ORCH_DIVISI_MAX];          /* 0..255, 128 = centre */
};

struct orch_divisi_state {
    uint8_t players;
    uint8_t note_of_player[ORCH_DIVISI_MAX];
    uint32_t id_of_player[ORCH_DIVISI_MAX];
    uint8_t assigned[ORCH_DIVISI_MAX];
};

void orch_divisi_init(struct orch_divisi_state *d);

/* notes[] and ids[] are the part's sounding notes (any order); density is
 * the intent's wanted player count, player_cap the part's ceiling. */
void orch_divisi_plan(struct orch_divisi_state *d,
                      const uint8_t *notes, const uint32_t *ids,
                      unsigned count, unsigned density, unsigned player_cap,
                      struct orch_divisi_plan *out);

#endif
