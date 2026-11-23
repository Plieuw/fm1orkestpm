# Working in parallel on this repository

Several agents work on this repository at the same time. These rules keep the
work mergeable and stop one agent from undoing another's measured fixes.

## Branches

| Branch | Content |
|---|---|
| `beta/0.1.0-poly-modeling` | first waveguide prototype, single generic excitation |
| `beta/0.2.0-divisi-adaptive` | adaptive interpreter, divisi planner, host tests, port docs |
| `beta/0.3.0-instrument-waveguides` | per-channel families, per-instrument profiles, body resonators |
| `beta/0.3.1-build-and-host-checks` | repairs 0.3.0 so it compiles; adds the host tests and CI |

Rules:

- Never force-push a branch another agent is working on. Add commits.
- One topic per commit, and every commit leaves `sh tests/run_tests.sh` green.
- If you must change an interface in `app/*.h`, say so in the commit message:
  the other agents compile against it.
- Record measured numbers in the commit message or in `docs/`. A claim like
  "sounds better" is not reviewable; "DC fell from 2.74 M to 10 k, pitch
  110.2 Hz" is.

## Measured facts (do not re-break these)

These were found by measurement on host renders, not by ear:

1. **A one-pole lowpass inside the loop kills the fundamental.** 0.994 per
   sample over a 200-sample round trip leaves 28 %. The loop uses a two-tap
   smoother; keep it.
2. **Loop loss must be per round trip, not per sample.** A fixed per-sample
   gain makes a bass note lose far more per period than a high note
   (0.9967^401 = 0.26 against 0.9967^67 = 0.80), so register changes the
   volume more than the instrument does.
3. **A loop with exactly zero loss is not stable, it is marginal.** It
   accumulates noise until it clips; the clipped noise then sits on the second
   harmonic (110 Hz measured at 221 Hz). Keep a loss floor of at least one
   step.
4. **A linear friction force fed back from the string output pumps DC.** On
   the 0.3.0 engine it produced DC 2.74 M against a 2.97 M peak and no pitch
   at all. Strings are driven by rosin noise; the Helmholtz friction model is
   still not ported.
5. **The second harmonic being stronger than the fundamental is normal** for
   bowed strings and reeds. Measure pitch with autocorrelation, not by taking
   the spectral maximum, or you will "discover" octave errors that are not
   there.

## Verification expectation

No firmware package has been built or flashed. Every claim in these documents
is a host-render or host-compile result. Do not describe the engine as tested
on the FM1 until someone has measured audio-block time and RAM on hardware.
