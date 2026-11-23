# Himalia → FM1: what the port keeps, changes and leaves out

Reference: `Plieuw/himalias`, branch `himalia-strings-v1.58.0-beta`,
`IOCore/HimaliaSketch/strings_engine.h` (124 KB) and `STRINGS_BETA.md`, GPL-3.0.
Himalia runs on an ATSAMD51J20A (Cortex-M4F, 120 MHz, 192 KB RAM, float DSP,
one voice, six fixed players, no MIDI). The FM1 is a JieLi AC791N / WL82
(pi32v2, 240 MHz, no external RAM, 96 KB application RAM, MIDI polyphonic).
Nothing from Himalia compiles here; the port is a re-implementation of its
*musical decisions* in integer arithmetic.

## What is kept

| Himalia | FM1 port | Where |
|---|---|---|
| Phrase interpreter (`StrBrain`) | `struct orch_adaptive`, one instance per instrument part | `app/orch_adaptive.c` |
| Note history: IOI, gap, duty, velocity trend, phrase index/peak | same fields, in samples and Q16 | `orch_adaptive.c` |
| Phrase continuity: rest detection, new phrase, tied notes | same rules, same thresholds, converted to samples | `orch_adaptive_note()` |
| Gate length → articulation (AUTO) | length classes + `orch_adaptive_resolve()` | `orch_adaptive.c` |
| Energy, attack, accent, vibrato intention, glide | bounded Q16 intents, deterministic | `struct orch_intent` |
| Eight-phrase dynamic arc | `ORCH_ARC[]`, same values | `orch_adaptive.c` |
| Divisi density following the phrase | `intent.density`, expanded per phrase and per velocity | `orch_adaptive.c` |
| Six players with individual timing, tuning, seating | up to four players per part, per-slot detune/pan, stable assignment | `app/orch_divisi.c` |
| Bow / breath / lip / strike excitation families | four excitation modes on one waveguide core | `app/orch_engine.c` |
| Articulation table (portamento … col legno) | `artic_profile()`: damping, attack, release, stroke, accent, pitch offset | `orch_engine.c` |
| CPU guard idea: fewer players before stealing a held note | divisi density reduced by the intent, per-part voice ceiling | `orch_divisi.c`, `pick_voice()` |

## What is deliberately different

- **Fixed point.** Himalia is float on an FPU. The pi32v2 has no fast float
  path (soft-float lives in the SDK's libm), and 12 voices × float would not
  fit the interrupt. Every control value is Q16, every audio path value is
  integer.
- **DSP cost per voice** is structurally different: Himalia computes six rich
  players per *note stream*; the FM1 computes one waveguide per *player*, and
  polyphony is bounded by the audio block (128 frames ≈ 2.9 ms), not by a
  fixed ensemble.
- **No audio-rate modulation input.** Himalia's `StrFm` reads a CV every
  sample. The FM1 has no CV; MIDI at 31.25 kbaud cannot carry it, so that path
  is dropped rather than faked.
- **No 24 KB float buffers per voice.** Himalia's `StrVoice` holds
  `float neck[4096] + bridge[2048]` ≈ 24 KB. Four such voices would be the
  whole FM1 application RAM. The FM1 voice uses a single int16 delay line
  (`DELAY_SIZE` 2048 → 4 KB), which is why the model is a single waveguide
  string/bore rather than Himalia's split neck/bridge pair.
- **Chords come from MIDI, not from one pitch CV.** Divisi therefore becomes
  an assignment problem (which player takes which sounding note) instead of a
  layout lookup. The FM1 port assigns deterministically with hysteresis, so a
  held note keeps its desk when a new note arrives.

## What is not ported yet

- The nonlinear bow/friction model (Helmholtz motion, bow pressure vs. contact
  point). The FM1 excitation is noise + loop feedback with articulation-dependent
  damping; it is a physical model, not yet a friction model.
- Body/air resonances (`StrMode body[...]`, `StrAir`), the presence peak,
  stereo seating delay, sizzle/rosin transient shaping, limiter.
- Look-ahead from a known pattern (`predIoi`/`predGate` from `patLen`), phrase
  ends, down-bow planning across a bar. The sequencer knows its pattern, so
  these can be driven from it later; the interpreter already keeps the fields.
- Per-player bow length/bow changes, con sordino, flageolet node selection.

## Resource reasoning (estimates, not measurements)

At 44.1 kHz a 128-frame block is 2.9 ms. A voice in the render loop does one
fractional delay read, two damping taps, one excitation term, an envelope and
a pan multiply: roughly 40–60 pi32v2 cycles, so 12 voices × 128 frames ≈
60–90 kcycles ≈ 0.25–0.4 ms per block, before vibrato (`ORCH_MAX_VOICES`).
That leaves most of the block for the rest of the firmware, but **it is an
estimate from the source, not a measurement**: the FM1's cache, XIP flash
waits and the volatile-RAM code section all change it. The engine exposes
`orch_active_voices()`, `orch_voices_of_part()` and the platform's own audio
statistics so the first hardware run can measure overruns and peak render
time. Raise `ORCH_MAX_VOICES` only with that number in hand.

Memory: 12 voices × 4 KB delay = 48 KB of the 96 KB application RAM, plus the
per-part state (a few hundred bytes). The platform's own cost (display, USB,
MIDI, mixer) must be added; if it does not fit, the first lever is
`DELAY_SIZE` 2048 → 1024 (lowest note ≈ 43 Hz), the second is ten voices.
