# FM1 Orchestral Engine

Polyphonic physical-modeling firmware for the M-VAVE FM1 (JieLi WL82 / AC791N, PI32v2).

## Direction

- One selected instrument shares a dynamic polyphonic voice pool; no fixed voice per instrument.
- MIDI input is channel-aware (1-16) and all accepted channels share the same pool.
- Polyphonic step-pattern API: up to four simultaneous notes per step; full gate scheduling and transport are still in development.
- Physical-modeling-first synthesis. The first prototype is a digital-waveguide string voice.
- No audio-thread allocation, locks, file access, or display calls.

## Status

The repository began as a license-only project. The first implementation targets the public FM1 Core platform API in https://github.com/rgrvmusic/fm1-core. That platform provides the FM1 board/audio layer and renderer hook; it is not copied into this repository. Building requires a matching FM1 Core checkout and the JieLi PI32v2 2.5.3 toolchain.

The FM1 uses JieLi WL82/AC791N. Himalia firmware targets a different MCU, so Himalia is a musical/DSP reference, not directly flashable code.

## First beta scope

- 12 provisional waveguide voices (not a measured maximum).
- Shared voice allocation across MIDI channels.
- MIDI Note On/Off, velocity and sustain pedal.
- 16-step polyphonic sequence-pattern API, up to four notes per step (sequencer timing is incomplete).
- Fixed-point mono synthesis rendered to stereo Q23 output.

The 12-voice ceiling is provisional and must be measured on real FM1 hardware. The goal is the highest stable polyphony that fits the audio interrupt budget.

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
