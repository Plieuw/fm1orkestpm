# DSP brainstorm and implementation roadmap — FM1 Orchestral Engine 0.3.3

This is a shared engineering note for every contributor/AI working on
`Plieuw/fm1orkestpm`. It records what the current model actually does, what
has been measured, what is only a hypothesis, and the next safe experiments.
Do not treat roadmap items as implemented until code and tests say so.

## Design target

Build a playable, expressive, polyphonic orchestral physical-model engine for
the M-VAVE FM1. Each MIDI part selects one orchestral instrument; Adaptive
Playing shapes phrase-level timing, articulation, dynamics, glide and vibrato;
Divisi can realize a part with multiple independently modelled players.

The goal is recognisable, musically responsive instrument behaviour, not a
claim that this compact DSP is a full numerical simulation of every acoustic
instrument.

## Current architecture (verified in source)

- Four configurable parts, with default MIDI channel groups 1–4, 5–8, 9–12
  and 13–16.
- Shared fixed pool of 12 voices. This is a provisional software ceiling,
  not a measured FM1 capacity.
- Each voice has a 2048-sample primary delay and a 512-sample secondary body
  resonator, stored as int16 arrays (about 5 KiB per voice for these arrays,
  before state and other globals).
- Fixed-point Q23 stereo render, 44.1 kHz, 128-frame audio blocks.
- 18 instrument identities: violin, viola, cello, double bass, harp, flute,
  piccolo, clarinet, oboe, bassoon, trumpet, French horn, trombone, tuba,
  marimba, xylophone, timpani and choir.
- Existing per-part Adaptive Playing and rendered Divisi are retained.
- No heap allocation in the render path.

## What is measured vs. what is not

Host-render measurements recorded in `INSTRUMENT_WAVEGUIDES.md` show that
the string family produces a pitched output after the DC-pumping friction
experiment was removed. The old linear friction feedback produced a large DC
offset and no valid fundamental; do not restore it without a new measured
model and a passing regression test.

The round-trip gain correction in 0.3.2 normalizes the scalar loop-gain
component: the instrument profile's Q15 gain is interpreted as the desired
gain over one nominal delay traversal and converted to a per-sample Q23 gain.
A regression test checks 400- and 67-sample delays against the same target.

**Important limitation:** this does not make total decay or timbre independent
of pitch. The two-tap loop smoother is still applied per sample, so its
frequency-dependent attenuation accumulates differently for different delay
lengths. Vibrato and glide also move the effective delay around the nominal
length. The 0.3.2 fix corrects one source of register-dependent decay, not
the complete loop response. Do not describe it as a complete acoustic
calibration.

Host CI is source compilation and deterministic host tests. It is not a JieLi
target build, a cycle-accurate FM1 benchmark, or a listening test on hardware.

## Recommended work order

### P0 — preserve correctness and measure the baseline

1. Keep `tests/run_tests.sh` green after every DSP change.
2. Keep regression coverage for DC offset, Q23 clipping, routing, Divisi,
   instrument selection, note release and pitch tracking.
3. Add reusable host metrics: autocorrelation f0, mean/DC, RMS envelope,
   decay per period, spectral centroid and peak level. Use a fixed render
   duration and reset seed/state for repeatability.
4. Capture baseline results for A2, C4 and E5 (or the equivalent practical
   range for each instrument family) before changing loop filters.

### P1 — strings: improve the physical model in small, testable steps

1. **Fractional-delay tuning first.** The current linear interpolation and
   loop smoothing are not phase-linear; measured string pitch errors were
   reported around +4 to +23 cents, with coarse autocorrelation resolution at
   the highest test note. Add a better fractional-delay interpolator only
   after measuring f0 and CPU cost. Keep the interpolation state per voice.
2. **Dispersion as a separate experiment.** Stiff strings have inharmonic
   partials. Prototype a low-order allpass dispersion section outside the
   current feedback path first, compare partial frequencies against a target,
   then move it into the loop only if the fundamental can be retuned and the
   filter remains stable. A generic allpass coefficient must not be guessed
   per instrument.
3. **Bridge/body coupling.** The current secondary resonator colours the
   radiated signal but is not yet a measured bridge-admittance model. Add a
   small, bounded, passive coupling experiment: string excites body; body
   returns a limited load to the string. Sweep coupling at low levels, measure
   decay and DC, and retain a bypass/zero-coupling control for A/B tests.
4. **Bowed-string contact.** The next physically meaningful step is a bounded
   Helmholtz stick/slip contact model using relative bow/string velocity,
   bow pressure and bow position. Do not feed a raw linear friction force back
   into the delay. Use a bounded nonlinear curve or a small lookup table,
   clamp state, include DC monitoring, and test onset/release and pitch
   stability across bow pressure.
5. **Player individuality.** Derive small, deterministic per-player
   variations in bow pressure, bow position, vibrato onset/depth and pan from
   Divisi player identity. Keep the variation subtle and stable for a note;
   do not use unrelated random changes every sample.

### P2 — wind and brass realism

- Clarinet/reed family: add a bounded nonlinear reed reflection/flow relation
  before adding a large bore solver. Clarinet and oboe should not remain merely
  differently coloured copies if CPU allows.
- Flute: model jet delay/edge interaction separately from reed excitation.
- Brass: separate lip oscillator/pressure from the bore; add a simple
  frequency-dependent bell radiation stage only after pitch and stability
  tests.
- Tone holes, register vents and bore taper should be incremental sections,
  not one expensive monolithic model.

### P3 — mallets, timpani and choir

- Replace the single secondary mode with a small modal bank for marimba and
  xylophone. Keep modes inharmonic and instrument-specific; decay rates should
  vary by mode.
- Timpani needs a membrane-like modal ratio set and a controllable pitch bend
  at the strike, not just a short noise burst.
- Choir needs a small set of independently controlled formant bands and
  vowel selection. Keep it a stylised choir pad until measured/validated.

## Adaptive Playing / Divisi integration

- Adaptive phrase intent should control bow pressure/energy and articulation
  transitions, not just amplitude.
- Vibrato should retain delayed onset and phrase-sensitive growth; player
  offsets should be independent but coherent.
- Glide should remain connected-note dependent and use pitch-continuous
  fractional delay. Avoid fixed equal-time portamento for every interval.
- Divisi voices need independent excitation and small, bounded physical
  differences, but all players of a section should share the same phrase
  gesture. Do not make each player behave like an unrelated soloist.

## Guardrails for all contributors

- Do not reintroduce a one-pole lowpass in the delay loop: host measurement
  showed it killed the fundamental too quickly.
- Do not restore the old linear bow-friction feedback: it pumped DC and lost
  the pitch. Any new friction model must pass DC/f0 tests.
- Do not claim the 12-voice ceiling is safe until RAM and 128-frame deadline
  are measured on the actual JieLi FM1.
- Do not increase delay sizes, voice count or modal count without a RAM/CPU
  estimate. Current primary+body arrays alone are roughly 60 KiB for 12 voices.
- Keep instrument enum values stable. Add new IDs only at the end and update
  family mapping/tests/docs together.
- Avoid concurrent edits to the same C file. If two agents need
  `orch_engine.c`, split work into separate branches and cherry-pick/review
  deliberately; never force-push a shared branch.
- State clearly whether a result is analytical, host-measured, target-built or
  hardware-listened.

## Suggested parallel task split

| Workstream | Files to own | Deliverable |
|---|---|---|
| String DSP | `orch_engine.c` on a dedicated branch | one isolated dispersion or bridge-coupling experiment plus A/B tests |
| Bow model | separate helper file if feasible, plus engine integration | bounded contact curve, DC/f0 tests |
| Wind/brass | separate model helper/header | reed/lip/bore experiment with stability tests |
| Test/analysis | `tests/`, analysis script/docs | repeatable f0, DC, RMS/decay and spectral metrics |
| Integration | `docs/`, workflow, review | reconcile branches, retain passing tests, no unmeasured claims |

## Immediate 0.3.3 implementation in this branch

The 0.3.2 round-trip gain conversion used a 64-bit division during note
initialization, which is called from the audio/event path. The Q15-to-Q23
conversion simplifies algebraically to:

`loss_q23 = ((32768 - trip_gain_q15) * 256) / samples`

The numerator fits in 32 bits for the clamped input range. This branch uses
that 32-bit expression to avoid a 64-bit divide. It still performs one integer
division at note start; profile it on target before making timing claims.
The 0.3.2 approximation and minimum-loss floor are unchanged.

## Definition of done for the next string-model increment

- Host compile and all tests pass.
- A/B renders use identical notes, velocity, adaptive intent and random seed.
- Report f0 error, DC/peak ratio, RMS decay per period, spectral partial ratios,
  clipping count and per-block CPU time.
- The change can be disabled for direct comparison.
- No target/hardware claims without target evidence.
