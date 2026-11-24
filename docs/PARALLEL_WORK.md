# Working in parallel on this repository

Several agents work on this repository at the same time. These rules keep the
work mergeable and stop one agent from undoing another's measured fixes.

## Branches

| Branch | Content |
|---|---|
| `beta/0.1.0-poly-modeling` | first waveguide prototype, single generic excitation |
| `beta/0.2.0-divisi-adaptive` | adaptive interpreter, divisi planner, host tests, port docs |
| `beta/0.3.0-instrument-waveguides` | per-channel families, per-instrument profiles, body resonators |
| `beta/0.3.1-build-and-host-checks` | 0.3.0 content plus the friction bow, host tests and CI; the panel (`app/orch_ui.c`), the master FX chain and the knob cells live here and nowhere else |
| `beta/0.3.2-roundtrip-loss`, `beta/0.3.2-roundtrip-loss-integration` | per-round-trip waveguide loss and the low/high-register regression test |
| `beta/0.3.3-dsp-roadmap` | 0.3.2 plus the 32-bit round-trip gain and the shared DSP brainstorm |
| `beta/0.3.4-panel-integration` | FM1 Core adapter, keys and MIDI, and a small RGB565 status panel drawn inside `app/fm1_app.c` |
| `feature/0.3.5-hawk-chua-arp-karma` | per-part ARP/phrase engine (`app/orch_performance.c`) on the 0.3.4 panel, with a performance page in `app/fm1_app.c` |
| `feature/0.3.6-performance-controls` | 0.3.5 plus a phrase-detail page and `docs/BUILD_FIRMWARE.md` |
| `feature/0.3.7-hawk-build-and-web-installer` | build and web-installer alignment |
| `feature/0.3.8-hawk-runtime-bridge` | `runtime/hawk/orch_hawk_bridge.c` and the HAWK runtime overlay scripts |

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

- `app/orch_engine.c` — the 0.3.0 engine plus the friction bow. Every other
  line, including the voice-stealing comparison, the `glide_left` reset and the
  32-bit `vib_depth`, comes from the 0.3.0 line.
- `docs/ARCHITECTURE.md` and `docs/INSTRUMENT_WAVEGUIDES.md` — the 0.3.0 text,
  with the bow sections corrected and the measurements added.
- The added tests, `tests/run_tests.sh`, the workflow that runs them and the
  README status section exist only here; the workflow compiles every `app/*.c`
  and then runs the suites, so it is a superset of the 0.3.0 workflow.

Re-run that diff before claiming this branch matches 0.3.0. If the 0.3.0 tip
moves again, diff against it rather than assuming.

## 0.3.1 panel: knobs, cells and the second panel

This branch carries the FM-1 panel in `app/orch_ui.c` (5x7 font, five pages,
the flash record, the FX page). Four things about its knobs changed and the
next agent needs to know them:

1. **The four knobs have a cell at the bottom of the screen.** Each cell is a
   small drawn knob, the parameter name under it and its value under that; the
   value lights for a second after its knob moves. The cell contents change
   with the page (players/voices/tempo/transpose on PARTS, rate/octaves/gate/
   mode on ARP, the selected step's velocity/gate/tempo/clear on SEQUENCER,
   scale/transpose/tempo/panic on GLOBAL, the selected effect's mix/two
   parameters/power on FX). It replaces the old "K1 ... K2 ..." help lines.
2. **A fast turn goes faster: x1, x2, x4, x8.** `enc_accel()` gives the factor
   from the detents a second in the same direction (x1 under 12 a second, x2
   under 24, x4 under 40, x8 above), lets it fall by halves as the hand slows,
   and starts again at x1 when the direction changes. Two calls in the same
   millisecond are taken unscaled, so the host tests stay deterministic.
3. **One detent is one menu step.** A choice (part, family, instrument, arp
   mode, effect, scale, sequencer step) moves exactly one place per detent, so
   a quick turn cannot skip an entry. The ranges take the accelerated count,
   except the percentages (FX mix and its two parameters, arp gate, step
   velocity and gate): those keep the 5 % a detent they always had and a fast
   turn only doubles that step. The tempo and the transpose take the full x8.
4. **The encoder order in the docs was wrong and is corrected.** The board's
   matrix is `0 SELECT, 1 ALGORITHM, 2 KNOB1, 3 KNOB2, 4 KNOB3, 5 KNOB4,
   6 PRESETS` (measured on hardware by HAWK CHUA's `firmware/src/panel.c`, and
   the order `app/orch_ui.c` already used). `docs/PANEL.md` had PRESETS at 2
   and KNOB1..4 at 3..6; do not restore that.

### The second panel in 0.3.5 and 0.3.6

`feature/0.3.5-hawk-chua-arp-karma` and `feature/0.3.6-performance-controls`
forked from `beta/0.3.4-panel-integration`, not from this branch, so they carry
a **second, independent panel**: a 3x5 glyph font and `panel_*()` drawing
functions inside `app/fm1_app.c`, with the page cycle on button bit 13 and a
performance and a phrase-detail page. Their tree has no `app/orch_ui.c`, no
`app/orch_fx.c`, no `tests/orch_ui_test.c`, no `tests/orch_fx_test.c`, no
`tests/panel_preview.c` and no `tools/panel_png.py`; a diff against 0.3.1 shows
them as deletions only because 0.3.4 predates them. They add
`tests/orch_engine_test.c` and `tests/orch_performance_test.c` instead.

Whoever merges them should:

- **Keep one panel, not two.** `app/orch_ui.c` is the one with the five pages,
  the flash record, the FX page and the knob cells; add the performance and
  phrase-detail pages to it as two more `PAGE_*` values. Do not leave the 3x5
  font and the `panel_*()` drawing in `app/fm1_app.c` beside it.
- **Keep the measured control map.** The performance pages in their
  `docs/FM1_PANEL.md` are written against raw button bits (0-3 parts, 4 panic,
  13 pages) and encoders 1-4 (instrument, divisi, articulation, part), which
  does not match the measured panel; 0 OCT-, 1 OCT+, 2 FX, 3 SCL, 4 ENV,
  5 LFO, 6 EDIT, 7 GLO, 8 HOME, 9 SAVE, 10 ARP, 11 SEQ, 12 PLAY, 13 REC, and
  the encoders above. Put their arp and phrase controls on these labels before
  merging, and write down any job that has no label.
- **Do not lose the FX.** Their `app/fm1_app.c` has no `orch_fx_init()` or
  `orch_fx_process()` call; taking that file wholesale silently drops the
  master FX chain and its flash block. `fm1_app.c` on this branch is a 60-line
  adapter and should stay that way: audio, MIDI and a call into `orch_ui_*`.
- **Keep both test sets.** `tests/run_tests.sh` lists the six suites here;
  after the merge it must also run `orch_engine_test` and
  `orch_performance_test`, and still run `orch_ui`, `orch_fx` and
  `orch_arp`.
- **Check the save record.** The performance settings are not in this
  branch's flash record; either append them with their own checksum (as the FX
  block does) or keep them out and say so in `docs/PANEL.md`.

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
4. **A friction force fed back into a near-lossless loop pumps DC unless its
   mean is removed.** Measured without that: DC 2.74 M against a 2.97 M peak
   and no pitch at all. The bowed strings therefore subtract a running mean of
   the force before injecting it, and the loop itself is never filtered,
   because a filter inside the loop shifts the pitch. With the mean removed
   the same model measures 110.0/262.5/668.2 Hz at r 0.95–0.99 and DC 3–28 k.
5. **Bow force must scale with the string.** A short string has a lower
   mechanical impedance, so a force that is right for A2 turns E5 into noise.
   The force is scaled by the loop length (a 400-sample A2 string is the
   reference).
6. **Soft playing is a light, faster stroke, not a slow drag.** Below the
   minimum bow force the string stops oscillating and only hisses; that is why
   pp on the low strings was periodic only after the bow speed was raised for
   low velocities. The note also starts with a 10 ms rosin bite, without which
   the model swells for nearly a second.
7. **The second harmonic being stronger than the fundamental is normal** for
   bowed strings and reeds. Measure pitch with autocorrelation, not by taking
   the spectral maximum, or you will "discover" octave errors that are not
   there.

## Verification expectation

No firmware package has been built or flashed. Every claim in these documents
is a host-render or host-compile result. Do not describe the engine as tested
on the FM1 until someone has measured audio-block time and RAM on hardware.
