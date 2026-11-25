# 0.5.0: how the two lines were merged

Written 25 November 2026. `beta/0.5.0-orchestra-merge` is a real merge commit
(`71807ad`) of `beta/0.4.0-podium-light` into
`feature/shared-eight-voice-allocator`, so the history of both lines is in it.

## What each line brought

| From | Kept |
| --- | --- |
| `feature/shared-eight-voice-allocator` and the `feature/0.3.5`–`0.3.13`, `boot-recovery-protection`, `orchestra-factory-presets` branches below it | the engine (`app/orch_engine.c`): eight parts, one shared pool of eight voices, ADSR, LFO, connected legato, loudness compensation, adaptive sequencer, call/response; `app/orch_performance.c` (ARP/phrase); the factory scenes; `runtime/hawk/` and `tools/prepare_hawk_runtime.py`; `tools/check_boot_safety.py`; the target build in CI |
| `beta/0.4.0-podium-light` (with all of `beta/0.3.1`) | the panel (`app/orch_ui*`), `app/orch_stage.c`, `app/orch_level.*`, the four-parameter FX, `web/emu/`, the bench, the preview, the friction bow |

Not merged, because their content is already in one of the two lines in a
later form: `beta/0.2.0-divisi-adaptive` (its tests were superseded),
`beta/0.3.0-instrument-waveguides` (hand-applied in 0.3.1, see
`PARALLEL_WORK.md`), `beta/0.3.2-roundtrip-loss` (re-integrated as
`beta/0.3.2-roundtrip-loss-integration`, which is in the 0.3.x line).

## Decisions

- **One panel.** The 0.4.0 panel stays; the 3x5-font panel in the 0.3.x
  `app/fm1_app.c` is gone, and `fm1_app.c` is the thin FM1 Core adapter again.
  Its jobs moved: the performance page to the ARP page, the factory scenes to
  the CONSOLE's KNOB4, ADSR and LFO to two new pages behind ENV and LFO.
- **One arpeggiator.** The 0.4.0 render-context arpeggiator (5 modes) is
  dropped for `orch_performance` (31 types, 46 phrases, stream logic, MIDI
  clock). `orch_arp_*()` no longer exists; `tests/orch_arp_test.c` went with it.
- **Eight parts.** The stage shows eight lamps; the level light stays
  calibrated on eight voices = 24, and `LEVEL_TOP` was measured again on the
  merged engine (0.65 M; 0.4.0's engine peaked at 2.1 M).
- **No panic.** The 0.3.x line removed it deliberately; the CONSOLE's KNOB4
  became the scene recall.
- **FX on the FM-1's memory.** The four-parameter chain runs on the 0.3.9
  port's compact buffers, enlarged again once the first 0.5.0 target build
  showed the room (echo 186 ms, combs 512/256). `-DORCH_FX_ROOMY` restores the
  0.4.0 sizes for the host and the browser if wanted.
- **The firmware runs this panel.** Until 0.5.0 the CI-built firmware showed
  HAWK's own screen (editing a synth that no longer sounded). Now
  `runtime/hawk/orch_hawk_glue.c` and `orch_hawk_panel.c` put the orchestral
  panel on the FM-1's screen, keys and encoders; the orchestral sources are
  compiled as their own objects so their static names cannot collide with
  HAWK's.

## Bugs found and fixed on the way

- The shimmer loop grew without bound at SHIM 40 (both buffer sizes,
  measured x50 / x1200 in a second): its feedback is now scaled by
  (1 - comb feedback).
- A decayed reverb sat on a constant offset of 8–16 LSB for ever (arithmetic
  shifts and a low-pass dead band): the loops now round towards zero.
- 64-bit divides in the FX (`lfo_inc`, chorus spread, SYNC) and the panel's
  master curve: the PI32v2 has none; all replaced by multiplies and shifts.
- The 0.3.11 overlay left HAWK's MASTER pot dead (HAWK's synth applied it);
  the bridge applies it again.
- Panel edits wrote engine state the audio interrupt walks (divisi, the ARP's
  held notes, the event ring) without a transaction; they are now wrapped in
  `fm1_engine_pause()`/`fm1_engine_resume()`.

## Open

- Nothing has been heard on an FM-1. Measure the audio block (eight voices,
  four effects) on the PI32v2 before calling this more than a beta.
- The package identity is still `FM-1_007` (HAWK CHUA's). A product identity
  of its own needs its own installer page and loader marker.
- The ARP page shows the type's shape; the phrase generator's density,
  range, cluster, repeat, accent, groove, latch and external clock are kept
  in the record and set by the scenes but have no knob yet.
