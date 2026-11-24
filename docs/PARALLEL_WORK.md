# Working in parallel on this repository

Several agents work on this repository at the same time. These rules keep the
work mergeable and stop one agent from undoing another's measured fixes.

## Branches

| Branch | Content |
|---|---|
| `beta/0.1.0-poly-modeling` | first waveguide prototype, single generic excitation |
| `beta/0.2.0-divisi-adaptive` | adaptive interpreter, divisi planner, host tests, port docs |
| `beta/0.3.0-instrument-waveguides` | per-channel families, per-instrument profiles, body resonators |
| `beta/0.3.1-build-and-host-checks` | 0.3.0 content plus the DC fix, host tests and CI |
| `beta/0.3.2-roundtrip-loss-integration` | per-round-trip waveguide loss and low/high-register regression test, layered on the latest 0.3.1 branch tip |
| `beta/0.3.3-dsp-roadmap` | 0.3.2 plus 32-bit round-trip gain conversion and shared DSP brainstorm/implementation roadmap |
| `beta/0.3.4-panel-integration` | FM1 Core app adapter, local key-to-note input, MIDI callback, RGB565 panel UI and safe control-task part edits |
| `feature/0.3.5-hawk-chua-arp-karma` | New per-part ARP/phrase scheduler based on the stable HAWK CHUA main branch; panel page, MIDI clock handling and host tests |

Rules:

- Never force-push a branch another agent is working on. Add commits.
- One topic per commit, and every commit leaves `sh tests/run_tests.sh` green.
- If you must change an interface in `app/*.h`, say so in the commit message:
  the other agents compile against it.
- Record measured numbers in the commit message or in `docs/`. A claim like
  "sounds better" is not reviewable; "DC fell from 2.74 M to 10 k, pitch
  110.2 Hz" is.
- **Describe the lineage you actually have.** If a branch carries another
  branch's content because it was transcribed or re-applied by hand, say so;
  do not write "rebased on X" unless X really is an ancestor. A tree can be
  identical while the history is not, and the next agent will trust the
  history.

## What 0.3.1 is, precisely

`beta/0.3.1-build-and-host-checks` was created from commit `4173e50` of the
0.3.0 line, not from its tip. There is no merge commit. The later 0.3.0 work
(`eeda2ab`, `3fa6354`) and the repairs were applied to the files by hand and
then verified with `git diff` against `origin/beta/0.3.0-instrument-waveguides`:

- `app/orch_engine.c` — identical except the rosin excitation and the DC
  blocker. Every other line, including the voice-stealing comparison, the
  `glide_left` reset and the 32-bit `vib_depth`, comes from the 0.3.0 line.
- `docs/ARCHITECTURE.md` and `docs/INSTRUMENT_WAVEGUIDES.md` — the 0.3.0 text,
  with the bow bullet corrected and the measurements added.
- The added tests, `tests/run_tests.sh`, the workflow that runs them and the
  README status section exist only here; the workflow compiles every `app/*.c`
  and then runs the suites, so it is a superset of the 0.3.0 workflow.

Re-run that diff before claiming this branch matches 0.3.0. If the 0.3.0 tip
moves again, diff against it rather than assuming.

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
   this engine it produced DC 2.74 M against a 2.97 M peak and no pitch at
   all. Strings are driven by rosin noise; the Helmholtz friction model is
   still not ported. Do not restore the feedback without a measurement that
   shows f0/DC well above one.
5. **The second harmonic being stronger than the fundamental is normal** for
   bowed strings and reeds. Measure pitch with autocorrelation, not by taking
   the spectral maximum, or you will "discover" octave errors that are not
   there.

## Verification expectation

No firmware package has been built or flashed. Every claim in these documents
is a host-render or host-compile result. Do not describe the engine as tested
on the FM1 until someone has measured audio-block time and RAM on hardware.


## 0.3.4 panel integration notes

The FM1 Core API provides `fm1_app_render`, `fm1_app_midi_received`, `groove_screen_init` and `groove_screen_control` hooks. The previous `fm1_app.c` already routed audio and external MIDI, but its panel-control hook was empty. The 0.3.4 branch now adds local key edge detection, part/instrument/articulation/Divisi controls and a small RGB565 status display. PR #6: https://github.com/Plieuw/fm1orkestpm/pull/6. Host source check passed on commit `52f13e34ab035563d1bc75e6116046fcb02da47f` (run `45286429574`). See `docs/FM1_PANEL.md` for the numeric control map and validation caveats. Do not claim a hardware-tested panel mapping until checked against the physical button numbering. The JieLi target build and physical FM1 test are still outstanding.


## 0.3.5 performance engine notes

Source reference is pinned to HAWK CHUA `main` commit `e750df6a2e13490a4f01b84d3f9c553f4612163f`, not a beta. The FM1 port is a new implementation with 31 ARP patterns and 46 authored phrase rhythm profiles. See `docs/HAWK_CHUA_PERFORMANCE_PORT.md` for exact behavior, differences and outstanding target checks. Generated notes must continue to route through `engine_note_on_part()` / `engine_note_off_part()` so instrument profiles, Adaptive Playing and Divisi remain in the signal path.


Host CI for the 0.3.5 ARP/phrase implementation passed on code commit `1f9cf1f087c82b8762e97fa71e26cf78b2461c33` (run `45388838986`): every application C source compiled with `-Wall -Wextra -Werror`, and all host test suites passed, including all 31 ARP slots, all 46 phrase slots, external clock routing and engine voice-path integration. The later commit `5fc94dbeef25595eeca0c0e649fc354bf1f90b9d` only corrects source attribution metadata. This is still host-only validation, not a JieLi target build or a physical FM1 test.
