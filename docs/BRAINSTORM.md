# Brainstorm and open work

For the agents working on this repository in parallel. Everything here is a
host-render or host-compile result. Nothing has run on an FM1.

Re-derive any number before you rely on it:

```sh
FM1_CORE=../fm1-core sh tests/run_tests.sh      # pass/fail, must be green
FM1_CORE=../fm1-core sh tests/run_measure.sh    # print the measurements
FM1_CORE=../fm1-core sh tests/run_measure.sh repeat
```

## Where the engine stands

The audio path is MIDI or sequencer -> part routing -> Adaptive Playing ->
Divisi planner -> per-player waveguide -> body/bore resonator -> stereo Q23.
Strings are bowed with a Helmholtz friction curve (`mu(v_rel)` with the mean of
the force removed), winds and brass with a half-period air column and a
pitch-locked jet/lip oscillator, mallets with a strike and a secondary
resonator, choir with harmonic excitation and formant smoothing.

Green: all `app/*.c` compile with `-Werror`, three host suites pass, the
strings hold pitch from C2 to C6 at velocity 20 to 124 (`r >= 0.89`, level
20–29× from pp to ff).

## Findings from the friction round (0.3.1)

### 1. The intonation error is frequency dependent, not a constant offset

Measured with `tests/run_measure.sh`, compression of the loop by the two-tap
smoother is the obvious suspect, but a constant compensation per filter stage
does not fit:

| compensation per stage | strings A2 | C4 | E5 |
|---|---|---|---|
| none | +2.3 c | +4.2 c | +13.9 c |
| 0.20 sample | +0.6 c | 0.0 c | −12.8 c |
| 0.30 sample | −0.8 c | −3.3 c | −9.1 c |
| 0.40 sample | −1.1 c | −4.1 c | −6.6 c |

So the error is roughly 0.4–0.5 samples at A2/C4 but less at E5, and winds and
brass (whose damping skips the smoother entirely) show +4.3…+23.5 c that no
compensation touches. A proper fix has to model the phase delay of the
fractional-delay interpolation at the actual frequency (first-order allpass, or
a per-note correction from measurement), not a constant.

### 2. A friction note has no reproducible timbre

`run_measure.sh repeat` plays C4 at velocity 100 six times. Two runs of the
same command, same build, one of them on a fresh clone:

```
run A: h3/h1  4.91  0.68  3.66  3.49  6.14  3.52
run B: h3/h1  0.42  0.55  0.11  0.71  0.47  0.12
```

The pitch is stable in both (r 0.98–0.99) and the harmonic balance is not:
within one run h3/h1 moves by a factor of five, and between runs the same take
index is not reproducible at all. The takes themselves are therefore not
quotable — only the spread is. The limit cycle depends on the initial transient,
and that transient includes the 10 ms bite noise, seeded from a generator that
`orch_init()` never resets. Suspects, in order:

1. the bite noise differs on every note, so every note starts in a different
   phase relationship;
2. the growth rate is low (that is why the bite exists), so the model spends a
   long time in a transient that carries audible timbre;
3. the oscillation may be bimodal (a period-doubled state at high bow force is
   the classic rauschig/"surface sound" regime).

Acceptance test for a fix: across repeated runs of the repeat mode, h2/h1 and
h3/h1 vary by less than ~20 %, with pitch and level unchanged. Note that
tightening the measurement window (a longer analysis after the attack) helps
the numbers, but if the timbre really is take-dependent it is audible too, so
fix the engine rather than the window.

### 3. High mallet notes are a semitone sharp

`run_measure.sh` on the mallets: A2 +5.0 c, C4 +19.9 c, E5 **+117.6 c**. A real
bar has inharmonic partials, so a single waveguide tuned to the strike note is
the wrong model above the low register; the fix is a two- or three-mode bar
(k0, 2.76 k0 for a free-free bar), not a longer loop.

### 4. Bow position was tried and not kept

Reading the string velocity at a second tap, a fraction of the loop away from
the bridge, and using it for the friction velocity is cheap. It did not produce
a usable result: over six identical takes the h3/h1 spread got *worse* (up to
30), and no brightness ordering between "near the bridge" and "over the
fingerboard" survived that noise. Prototype discarded; `ORCH_ART_TASTO` and
`ORCH_ART_PONTICELLO` still only change damping. Given finding 2, any future
attempt has to average over takes before it can claim anything.

To do it properly you need a two-segment loop (a bow junction at `beta*L` with
the reflections of both segments) and an averaged measurement, not a single
take. That is a real structural change to `voice_tick`, so state it in the
commit message.

## Suggested next tasks

Ordered by value per risk. Take one per branch and say so here.

1. **Deterministic note start.** Reset or seed the noise generator from
   `note`, `velocity` and a note counter so a note sounds the same every time.
   Small, testable with the repeat mode. (Fix for finding 2.)
2. **Allpass fractional delay.** Replace linear interpolation with a
   first-order allpass and re-measure the cents table; it should make the error
   flat across the register. (Fix for finding 1.)
3. **Modal bars for the mallets.** Two or three tuned modes per voice for
   marimba/xylophone/timpani; acceptance is pitch within 10 cents at C4 and E5
   and a decay that is shorter for the higher modes. (Fix for finding 3.)
4. **Bow force hysteresis / rosin state.** A first-order relaxation on the
   friction coefficient (stick time, slip time) would give real bow changes and
   make `martele`, `detache` and `spiccato` physically different, not just
   different envelopes.
5. **Two-segment bow junction** for bow position, with an averaged
   measurement (see finding 4).
6. **Hardware budget.** Port the block loop and measure cycles and RAM on the
   target at 1/4/8/12 voices. Everything above is host-only until this exists.
7. **Profile calibration.** The `INST_PROFILE` constants are plausible, not
   measured. A listening pass with the owner beats guessing.

## Process rules

See `docs/PARALLEL_WORK.md`: one topic per commit, green tests, measured
numbers in the message, no force-push, and describe the lineage you actually
have rather than the one you wish you had.
