/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Adaptive playing, control rate, fixed point.
 *
 * A port of the phrase interpreter in Himalia's strings engine (StrBrain,
 * v1.58.0-beta, GPL-3.0) to integer arithmetic for the FM1. It is shared by
 * every instrument part: the interpreter decides *how* a note is played, the
 * waveguide in orch_engine.c decides how that sounds for a family.
 *
 * Nothing here runs per sample; one call per note-on, one for the velocity
 * (a few milliseconds later, when the host has settled), one at note-off.
 */
#ifndef ORCH_ADAPTIVE_H
#define ORCH_ADAPTIVE_H

#include <stdint.h>

#define ORCH_Q16 65536
#define ORCH_ADAPTIVE_SR 44100U
#define ORCH_DIVISI_MAX 4U

enum orch_artic {
    ORCH_ART_AUTO = 0,
    ORCH_ART_PORTAMENTO,
    ORCH_ART_LEGATO,
    ORCH_ART_DETACHE,
    ORCH_ART_MARTELE,
    ORCH_ART_STACCATO,
    ORCH_ART_SPICCATO,
    ORCH_ART_PIZZICATO,
    ORCH_ART_FLAGEOLET,
    ORCH_ART_TASTO,
    ORCH_ART_PONTICELLO,
    ORCH_ART_COL_LEGNO,
    ORCH_ART_COUNT
};

/* Length classes, as in Himalia: the gate against the note spacing. */
enum orch_len_class {
    ORCH_LEN_TIED = 0,     /* legato / tied */
    ORCH_LEN_NORMAL = 1,   /* detache */
    ORCH_LEN_SHORT = 2,    /* staccato */
    ORCH_LEN_FAST = 3,     /* spiccato / sautille */
    ORCH_LEN_ACCENTED = 4  /* martele */
};

/* Bounded targets for one note. All Q16 except glide_ms and the flags. */
struct orch_intent {
    uint32_t energy;       /* bow / breath / lip energy, 1.0 neutral */
    uint32_t attack_mul;   /* attack time, > 1 = more gentle */
    uint32_t release_mul;  /* release time, > 1 = longer */
    uint32_t vib_intent;   /* vibrato wanted, 1.0 neutral */
    uint32_t accent;       /* accent strength, 1.0 neutral */
    uint16_t glide_ms;     /* pitch travel for a connected note */
    uint8_t articulation;  /* enum orch_artic, resolved (never AUTO) */
    uint8_t length_class;  /* enum orch_len_class */
    uint8_t density;       /* divisi players wanted, 1..ORCH_DIVISI_MAX */
    uint8_t phrase_start;
    uint8_t phrase_end;    /* known only with a pattern (see docs) */
    uint8_t connected;     /* tied to the previous note */
    uint8_t down_bow;      /* heavy beat */
    uint8_t new_high;      /* the phrase reached a new peak */
};

struct orch_adaptive {
    int16_t prev_note;        /* -1 until the first note */
    uint32_t ioi_avg;         /* samples between note starts */
    uint32_t gap_avg;         /* samples of silence between notes */
    uint32_t last_on;         /* last note-on, samples */
    uint32_t off_now;         /* last note-off, samples */
    uint16_t peak_note;       /* phrase peak, note*256 */
    uint16_t duty_avg;        /* Q16 gate / interval */
    uint16_t vel_avg;         /* Q8 velocity average */
    uint8_t have_prev;
    uint8_t have_off;
    uint8_t idx;              /* note number in the phrase */
    uint8_t n_hist;           /* notes with a known gate length */
    uint8_t gap_n;
    uint8_t phrase_n;         /* phrase counter, 8-phrase arc */
    uint8_t vel_n;
    uint8_t density;
    uint8_t held_notes;       /* overlapping gates allow true legato connection */
};

void orch_adaptive_init(struct orch_adaptive *a);
void orch_adaptive_reset_phrase(struct orch_adaptive *a);

/* One note-on. now is the engine's sample clock; articulation_fixed is
 * ORCH_ART_AUTO or a pinned articulation for the part. */
void orch_adaptive_note(struct orch_adaptive *a, struct orch_intent *out,
                        unsigned note, unsigned velocity, uint32_t now,
                        unsigned articulation_fixed);

/* The velocity half of the interpretation; call it shortly after the note-on
 * (MIDI velocity can arrive a few milliseconds after the note). */
void orch_adaptive_velocity(struct orch_adaptive *a, struct orch_intent *io,
                            unsigned velocity);

void orch_adaptive_off(struct orch_adaptive *a, uint32_t now);

/* Resolve AUTO from the length class. */
unsigned orch_adaptive_resolve(unsigned articulation, unsigned length_class);

#endif
