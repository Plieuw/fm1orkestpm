# FM1 Orchestral Engine

Polyphonic physical-modeling firmware for the M-VAVE FM1 (JieLi WL82 / AC791N, PI32v2).

## Direction

- One selected instrument shares a dynamic polyphonic voice pool; no fixed voice per instrument.
- MIDI input is channel-aware (1-16) and all accepted channels share the same pool.
- Polyphonic step-pattern API: up to four simultaneous notes per step, with per-note gate percentages and internal tempo.
- Physical-modeling-first synthesis. The first prototype is a digital-waveguide string voice.
- No audio-thread allocation, locks, file access, or display calls.

## Status: 0.3.5-beta, host-validated only

**Nothing here has run on hardware.** Every source compiles with `-Werror`
against the FM1 Core headers and the host tests pass; no `.fwsc` is built or
claimed. 0.3.1 repaired the 0.3.0 sources, which did not compile (a truncated
`orch_engine.c` and a byte-wide field holding Q15 friction values), and added
the host tests and CI run that were missing.

The repository began as a license-only project. The first implementation targets the public FM1 Core platform API in https://github.com/rgrvmusic/fm1-core. That platform provides the FM1 board/audio layer and renderer hook; it is not copied into this repository. Building requires a matching FM1 Core checkout and the JieLi PI32v2 2.5.3 toolchain.

The FM1 uses JieLi WL82/AC791N. Himalia firmware targets a different MCU, so Himalia is a musical/DSP reference, not directly flashable code.

## First beta scope

- 12 provisional waveguide voices (not a measured maximum), shared pool.
- Up to four instrument parts with their own channels, family, instrument, articulation and divisi state.
- 18 instrument profiles (violin, viola, cello, bass, harp, flute, piccolo, clarinet, oboe, bassoon, trumpet, horn, trombone, tuba, marimba, xylophone, timpani, choir) with body/bore resonators.
- MIDI Note On/Off, velocity and sustain pedal.
- 16-step polyphonic sequencer pattern, up to four notes per step, with per-note gate and internal tempo.
- Per-part ARP and generated-phrase engine: 31 ARP patterns, 46 phrase profiles, ten subdivisions, internal/external clock, groove, latch and stream logic. See [docs/HAWK_CHUA_PERFORMANCE_PORT.md](docs/HAWK_CHUA_PERFORMANCE_PORT.md).
- Fixed-point synthesis, one waveguide per player, stereo Q23 output.

The 12-voice ceiling is provisional and must be measured on real FM1 hardware. The goal is the highest stable polyphony that fits the audio interrupt budget. The 0.3.4 branch adds local key input, panel controls and an RGB565 status screen; see [docs/FM1_PANEL.md](docs/FM1_PANEL.md). The 0.3.5 feature branch adds the ARP/phrase performance page and per-part playback controls. The 0.3.6 feature branch adds a second phrase-detail page for density, octave range, cluster, repeat, accent, inverse and root settings. See [docs/FM1_PANEL.md](docs/FM1_PANEL.md) and [docs/BUILD_FIRMWARE.md](docs/BUILD_FIRMWARE.md) for control and target-build details.

## Host tests

No toolchain and no hardware needed:

```sh
FM1_CORE=../fm1-core sh tests/run_tests.sh
```

Covers adaptive classification and velocity reaction, divisi distribution and
stability, channel routing, per-instrument selection, release, sustain, panic,
sequencer gates, the Q23 range, ARP capture/release, phrase generation and a DC regression guard on the bowed strings.
The host workflow also compiles the FM1 app adapter and panel code against the
FM1 Core API headers.
CI compiles every source with `-Werror` and runs the same script.

## Build and web installer

The intended release system is the same as stable HAWK CHUA: JieLi PI32v2
target build, AC79 SDK inputs, .fwsc plus OTA loader, WebMIDI/SysEx browser
installer, GitHub Actions release artifacts and GitHub Pages. The current
repository still contains application modules and a host-tested FM1 Core
adapter only; it does not yet contain the complete target runtime, boot/startup,
hardware layer or OTA loader needed by HAWK's build script. See
[docs/BUILD_FIRMWARE.md](docs/BUILD_FIRMWARE.md) for the integration plan and
exact status. No .fwsc or browser installer is claimed yet.

