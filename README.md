# FM1 Orchestral Engine

Polyphonic physical-modeling firmware for the M-VAVE FM1 (JieLi WL82 / AC791N, PI32v2).

## Direction

- One selected instrument shares a dynamic polyphonic voice pool; no fixed voice per instrument.
- MIDI input is channel-aware (1-16) and all accepted channels share the same pool.
- Polyphonic step-pattern API: up to four simultaneous notes per step, with per-note gate percentages and internal tempo.
- Physical-modeling-first synthesis. The first prototype is a digital-waveguide string voice.
- No audio-thread allocation, locks, file access, or display calls.

## Status: 0.4.0-beta, host-validated only

**Nothing here has run on hardware.** Every source compiles with `-Werror`
against the FM1 Core headers and the host tests pass; no `.fwsc` is built or
claimed. 0.4.0 is the 0.3.1 work with a photographed hall on the STAGE page and
a light in front of the boards whose width is what is coming out of the
instrument — calibrated so the full orchestra of eight voices is 24 of 24 —
with its cost counted by `tests/run_panel_bench.sh`. 0.3.1 repaired the 0.3.0
sources, which did not compile (a truncated `orch_engine.c` and a byte-wide
field holding Q15 friction values), and added the host tests and CI run that
were missing.

The repository began as a license-only project. The first implementation targets the public FM1 Core platform API in https://github.com/rgrvmusic/fm1-core. That platform provides the FM1 board/audio layer and renderer hook; it is not copied into this repository. Building requires a matching FM1 Core checkout and the JieLi PI32v2 2.5.3 toolchain.

The FM1 uses JieLi WL82/AC791N. Himalia firmware targets a different MCU, so Himalia is a musical/DSP reference, not directly flashable code.

## First beta scope

- 12 provisional waveguide voices (not a measured maximum), shared pool.
- Up to four instrument parts with their own channels, family, instrument, articulation and divisi state.
- 18 instrument profiles (violin, viola, cello, bass, harp, flute, piccolo, clarinet, oboe, bassoon, trumpet, horn, trombone, tuba, marimba, xylophone, timpani, choir) with body/bore resonators.
- MIDI Note On/Off, velocity and sustain pedal.
- 16-step polyphonic sequencer pattern, up to four notes per step, with per-note gate and internal tempo.
- Master FX after the voice mix: chorus, ensemble, ping-pong delay and shimmer reverb, controlled from the FX page and kept in the flash record.
- Knob cells under the page body (label and value under each of the four knobs) and HAWK CHUA's knob speed: a fast turn goes up to x8, one detent is still one menu step, and the percentage ranges keep their 5 % a detent (a fast turn doubles it).
- Orchestra seating: each part is panned to its instrument's place in the orchestra (strings left and right, woodwinds centre, brass and percussion right, choir back centre) with the section's players spread over its width, instead of one fixed pattern for every part. The listener can be moved across the stage and back on the STAGE page (KNOB3 left/right, KNOB4 depth), which draws the hall around it — shell, wooden floor, curtains and the audience row with the listener's lit seat — and the seat is kept in the flash record.
- Fixed-point synthesis, one waveguide per player, stereo Q23 output.

The 12-voice ceiling is provisional and must be measured on real FM1 hardware. The goal is the highest stable polyphony that fits the audio interrupt budget.

## Host tests

No toolchain and no hardware needed:

```sh
FM1_CORE=../fm1-core sh tests/run_tests.sh
```

Covers adaptive classification and velocity reaction, divisi distribution and
stability, channel routing, per-instrument selection, release, sustain, panic,
sequencer gates, the panel and its flash save, the orchestra seating and the
listener, the arpeggiator, the FX chain
(bypass, echo timing, reverb decay, shimmer, range), the Q23 range and a DC
regression guard on the bowed strings.
CI compiles every source with `-Werror` and runs the same script.

## Build prerequisites

- FM1 Core: https://github.com/rgrvmusic/fm1-core
- JieLi PI32v2 toolchain 2.5.3 (`-mcpu=r3`)
- Python 3.10+

From the FM1 Core checkout, with this repository beside it:

```sh
python3 fm1-core/tools/build.py --app fm1orkestpm/app --clean
python3 fm1-core/tools/build.py --app fm1orkestpm/app --package
```

The FM1 Core platform and binary SDK retain their own licenses and redistribution terms. See its THIRD_PARTY.md. This project's GPL-3.0 license remains in force for original application code.

This is source code, not a verified firmware image. Do not flash until it builds cleanly and the generated package is checked. No .fwsc or .uf2 is claimed by this initial source commit.
