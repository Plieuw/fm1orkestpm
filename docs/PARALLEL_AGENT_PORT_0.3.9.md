# Parallel-agent port review — 0.3.9

## Source inspected

Parallel branch: `beta/0.3.1-build-and-host-checks` (latest inspected tree).
Useful additions found:
- `app/orch_fx.c/.h`: fixed-point master insert with chorus, ensemble, ping-pong delay and shimmer reverb.
- `tests/orch_fx_test.c`: bypass, delay timing/feedback, reverb decay, shimmer and bounded-output tests.
- `app/orch_ui.c/.h` and `docs/PANEL.md`: a substantial five-page panel UI (PARTS, ARP, SEQUENCER, GLOBAL, FX), note transpose/scale, save/restore and an FX control page.
- `docs/BRAINSTORM.md`: measured host findings on string intonation, repeatability, mallet inharmonicity and bow position.

## What is taken over in this branch

The FX DSP and its regression test have been copied into the current HAWK runtime bridge line:
- the overlay now copies `orch_fx.c/.h` into the HAWK source tree;
- the amalgamated runtime includes the FX source;
- HAWK initializes the FX state, processes the Q23 stereo block after the orchestra renderer, and flushes tails on panic;
- the host test runner now includes `orch_fx_test.c`.

The effects remain **off by default**. The upstream panel notes estimate roughly 100–116 KiB of static delay/reverb buffers. This is a material RAM cost on the FM1 and is not yet approved for a release until the target link map and runtime RAM budget are measured. CPU cost also needs target measurement.

## Not copied blindly

The 1,300-line panel UI is not yet wired into the current HAWK runtime. It was written against the earlier FM1 Core adapter and the earlier simple ARP API. The current runtime branch has the richer `orch_performance` config interface (31 ARP types, phrase GE, logic, grooves and MIDI clock), so direct copy would either fail to compile or expose misleading controls. The panel layout and printed-label mapping in `docs/PANEL.md` are retained as design input; the UI needs a deliberate adapter to the current performance API and the HAWK panel callbacks.

The parallel branch's latest visible host run failed in its UI tests (22 assertions in `orch_ui_test.c`). The earlier target-build attempt on PR #10 also failed: the overlay applied, but amalgamated HAWK sources collide with orchestral symbols/types (notably `parts`, `voice_t`, and `rng`). Those are integration issues, not evidence that the copied FX module itself is faulty.

## Validation boundary

This is a source port, not an FM1 hardware test. The target RAM/CPU budget, full PI32v2 link, package metadata and physical updater remain release gates.
