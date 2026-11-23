# FM1 Orchestral Engine

Polyphonic **physical-modeling** firmware for the M-VAVE FM1 (JieLi WL82 /
AC791N, PI32v2) — an orchestra of modeled instruments, not a sample player.

## Direction

- One selected instrument per part shares a dynamic polyphonic voice pool; no fixed voice per instrument.
- Up to four instrument parts, each with its own MIDI channels, family, phrase state and divisi.
- Physical modeling first: bowed strings, breath columns, lip-driven bores and struck bars, computed sample by sample.
- Himalia's phrase interpretation (energy, accents, articulation, vibrato, glide, divisi density) is ported to fixed point and drives the models. See [docs/HIMALIA_PORT.md](docs/HIMALIA_PORT.md).
- No audio-thread allocation, locks, file access, or display calls.

## Status: 0.2.0-beta, host-validated only

The repository began as a license-only project. 0.1.0 added a waveguide
prototype; this beta adds the layer the design called for:

| Module | What it does |
|---|---|
| `app/orch_adaptive.c` | fixed-point port of Himalia's `StrBrain`: phrase/gate/velocity interpretation into bounded intents |
| `app/orch_divisi.c` | divisi planner: 1–4 players per note, stable assignment, per-slot detune and seat |
| `app/orch_part.c` | instrument parts: channel routing, family, articulation, ceilings |
| `app/orch_engine.c` | the model: one waveguide per player, four family excitations, articulation profiles, glide, vibrato, Q23 stereo out |

**Nothing here has run on hardware.** It compiles with `-Werror` against the
FM1 Core headers and the host tests pass; no `.fwsc` is claimed.

## First beta scope

- 12-voice provisional polyphony (`ORCH_MAX_VOICES`), not a measured maximum.
- 4 parts × up to 4 modeled players per note (2 by default).
- MIDI Note On/Off, velocity, CC64 sustain, panic; channel-aware routing.
- 16-step polyphonic sequencer, up to four notes per step, per-note gate percentages, internal tempo.
- Fixed-point mono-per-player synthesis to stereo Q23.

The 12-voice ceiling and the per-part player cap are provisional and must be
measured on real hardware. The aim is the highest stable polyphony that fits
the 2.9 ms audio block.

## Build prerequisites

- FM1 Core: https://github.com/rgrvmusic/fm1-core (platform, board and renderer hook; not copied here)
- JieLi PI32v2 toolchain 2.5.3 (`-mcpu=r3`)
- Python 3.10+

From the FM1 Core checkout, with this repository beside it:

```sh
python3 fm1-core/tools/build.py --app fm1orkestpm/app --clean
python3 fm1-core/tools/build.py --app fm1orkestpm/app --package
```

## Host tests

No toolchain or hardware needed:

```sh
FM1_CORE=../fm1-core sh tests/run_tests.sh
```

Covers adaptive classification and velocity reaction, divisi distribution and
stability, channel routing, sustain, panic, sequencer gates, and the Q23
output range. CI runs the same script on every push.

## Licences

This project is GPL-3.0. The FM1 Core platform is MIT; its binary SDK, boot
resources and third-party parts keep their own terms (see its THIRD_PARTY.md).
Himalia is GPL-3.0 and is the musical/DSP reference for the adaptive layer;
`docs/HIMALIA_PORT.md` records what was re-implemented and what was left out.
