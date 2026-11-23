# FM1 Orchestral Engine — architecture 0.2

## Core design

The instrument is a polyphonic physical model, not a sample player. There are
two kinds of polyphony and they are kept apart:

1. **Note polyphony** — how many MIDI notes sound at once (shared pool of
   `ORCH_MAX_VOICES` = 12, provisional).
2. **Divisi polyphony** — how many modeled players realize one note
   (`ORCH_DIVISI_MAX` = 4 per note, two by default).

A MIDI note belongs to an **instrument part** (`app/orch_part.c`). A part owns
its MIDI channels, its family, its adaptive state and its divisi state. The
part decides how the note is played; the engine decides how that sounds.

```
MIDI / sequencer
      │ bounded event queue (no allocation)
      ▼
orch_part_for_channel()  →  part (family, channels, articulation, ceilings)
      │
      ├─ orch_adaptive_note()      how: energy, attack, accent, vibrato, glide,
      │                            articulation, density, phrase flags
      ├─ orch_divisi_plan()        who: 1..4 players, stable assignment,
      │                            per-slot detune and pan
      └─ voice_start() × players   what: waveguide + family excitation
      ▼
stereo Q23 block (FM1_BLOCK_FRAMES = 128)
```

## Audio path

`fm1_app_render()` is called by the FM1 Core platform from the ALNK DMA
interrupt once per 128 frames at 44.1 kHz (2.9 ms). It calls
`orch_render_q23()`, which drains the event queue, runs the sequencer, then
renders every active voice sample by sample. No heap, no locks, no filesystem,
no display, no libc in this path.

Output is signed Q23 stereo. The FM1 Core mixer (`fm1_output_mix`) applies
MASTER and mixes USB/Bluetooth audio; that stays platform code.

## Parts and families

| Part | Default channels | Family | Excitation |
|---|---|---|---|
| 1 | 1–4 | strings | bowed: rosin noise + loop feedback |
| 2 | 5–8 | woodwinds | breath column: filtered noise, stronger damping |
| 3 | 9–12 | brass | lip buzz + a nonlinear term in the loop |
| 4 | 13–16 | mallets | struck: noise burst, then ring-out |

Defaults are a starting point, not a claim: only the string family has been
tuned by ear on a host render. `orch_engine_configure_part()` remaps families
and channels; `orch_engine_set_articulation()` pins an articulation for a part
(useful for a panel page); `orch_engine_set_player_cap()` and
`orch_engine_set_voice_ceiling()` bound the cost per part.

## Adaptive playing

`orch_adaptive.c` is the fixed-point port of Himalia's `StrBrain` (see
[HIMALIA_PORT.md](HIMALIA_PORT.md)). Per note it produces a bounded
`struct orch_intent`:

- `energy` — bow / breath / lip level (drives loop gain and excitation);
- `attack_mul` — how gently the note begins;
- `accent` — extra attack weight;
- `vib_intent` — vibrato wanted (delayed and grown inside the voice);
- `glide_ms` — pitch travel for a connected note;
- `articulation` + `length_class` — resolved from the gate history when AUTO;
- `density` — how many players this note wants;
- phrase flags (`phrase_start`, `connected`, `down_bow`, `new_high`).

The velocity half is applied separately (`orch_adaptive_velocity`), because
MIDI velocity can arrive a few milliseconds after the note; it compares the
note with the part's recent average, which is what makes a hard note in a soft
phrase an accent rather than simply louder.

Rules kept from Himalia: rest vs. gap detection, tied notes, gate/interval
duty, very fast notes becoming spiccato, phrase opening gently and building,
new phrase peaks leaning in, the eight-phrase dynamic arc, and divisi opening
with the phrase.

## Divisi

`orch_divisi_plan()` takes the part's sounding notes and the intent's density
and returns, per player slot: which note it plays, its intonation offset and
its seat. Assignment is deterministic with hysteresis: a held note stays on
its player, new notes take the free players, extra players double existing
notes, and each slot keeps its own detune and pan so a line does not change
colour when the chord changes.

The plan is recomputed at each note-on; existing voices are not restarted, so
the plan never interrupts a sounding note.

## The physical model

One voice is a digital waveguide:

- a fractional delay line (int16, `DELAY_SIZE` 2048) holds travelling energy;
- loop damping is a two-tap smoother inside the loop — a one-pole applied every
  sample would attenuate the fundamental by ~70 % per round trip and the voice
  would never resonate (this was a real bug, fixed and re-measured);
- loop gain sits just under one and rises with the intent's energy;
- excitation depends on the family and the articulation;
- the articulation profile sets attack, release, stroke length, damping, an
  accent and a pitch offset (flageolet), and whether the note is plucked or
  struck (pizzicato, col legno).

Vibrato modulates the delay length with a triangle LFO, delayed ~120 ms and
growing over ~350 ms; the rate differs per player slot, which is what makes a
section sound like a section. Glide ramps the delay length to the new pitch
over `glide_ms`, and only for connected notes.

A pluck or a strike closes the gate immediately and runs its own attack, so it
rings out naturally instead of being cut by note-off.

## Voice allocation

Per part, `voice_ceiling` (default 4) is enforced first; the global pool is 12.
A stealing voice prefers a releasing voice, then the oldest held note. Divisi
density is reduced by the adaptive layer before the pool is exhausted — the
Himalia guard principle, applied at planning time rather than by degrading a
player.

## Validation

Host (`tests/`): adaptive classification (legato / détaché / staccato /
spiccato), velocity reaction, density bounds, divisi distribution, cap,
stability across plans, routing per channel, sustain, panic, sequencer gates,
Q23 range, and non-silence.

On hardware (not done yet): build and package with FM1 Core + the JieLi
PI32v2 2.5.3 toolchain, then play 1, 4, 8 and 12 notes, read overruns and peak
render time from the platform statistics, and only then raise
`ORCH_MAX_VOICES` or the default `player_cap`.

Implementation status of the port: [DIVISI_STATUS.md](DIVISI_STATUS.md).
