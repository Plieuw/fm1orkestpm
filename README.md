# FM1 Orchestral Engine

Polyphonic physical-modeling firmware for the M-VAVE FM1 (JieLi WL82 / AC791N, PI32v2).

## Status: 0.6.4-beta

0.6.0 tunes the waveguides (up to +26 cents off in the high register before),
shares the mix evenly over the voices (1/n, no limiter), reworks the adaptive playing, the ARP and the sequencer (a lane per
part), and draws the panel with anti-aliased vectors, the stage as a seating
plan. 0.6.3 adds a shared sympathetic resonance: eight open strings the
whole orchestra rings, set by RESON on the CONSOLE page. 0.6.4 answers MIDI as a Yamaha XG part
([docs/MIDI.md](docs/MIDI.md)) and spreads velocity evenly over 32 dB. See [docs/CHANGES_0.6.0.md](docs/CHANGES_0.6.0.md). Below: how 0.5.0
brought the two lines together.

### 0.5.0 — the two lines merged

`beta/0.5.0-orchestra-merge` is the first branch that carries all the work of
this repository at once:

- the **engine of the 0.3.x feature line** (`feature/shared-eight-voice-allocator`
  and everything below it): eight parts on one shared pool of eight waveguide
  voices, per-part ADSR and LFO, connected legato, register-aware loudness, the
  ARP/phrase engine (31 ARP types, 46 generated phrases, stream logic, MIDI
  clock), the adaptive 16-step sequencer and call/response, eight factory
  scenes, the HAWK/Felucca runtime overlay and the boot/flash safety checks;
- the **panel of the 0.4.0 line** (`beta/0.4.0-podium-light`): the photographed
  hall, the orchestra seating and the movable listener, the light over the
  boards, the four-parameter FX chain and the browser build;
- the **Helmholtz friction bow** of 0.3.1/0.4.0, ported into the 0.3.x engine.

The panel has seven pages: **STAGE**, **ARP**, **SEQUENCE**, **CONSOLE**,
**FX**, **ENVELOPE** and **LFO**. See [docs/PANEL.md](docs/PANEL.md) and
[docs/MERGE_0.5.0.md](docs/MERGE_0.5.0.md).

**Nothing here has been heard on an FM-1.** The host suites pass and CI builds
a PI32v2 package through the pinned HAWK CHUA runtime (see below); that proves
it compiles, links and fits, not that it plays. Flash it only with a recovery
route at hand (OCT- + OCT+ held 5 s enters UBOOT, as in HAWK CHUA).

## Direction

- Eight parts, one MIDI channel each (1-8), sharing one pool of eight voices.
- Physical-modeling-first synthesis: one digital waveguide per player.
- No audio-thread allocation, locks, file access or display calls.
- Fixed-point only; no 64-bit divides (the PI32v2 has none).

## Host tests

No toolchain and no hardware needed:

```sh
FM1_CORE=../fm1-core sh tests/run_tests.sh          # eight suites
FM1_CORE=../fm1-core sh tests/run_panel_bench.sh    # the panel's drawing cost
FM1_CORE=../fm1-core sh tests/run_measure.sh        # pitch, DC, harmonics
```

Suites: `orch_adaptive`, `orch_divisi`, `orch_performance`, `orch_stage`,
`orch_engine`, `orch_ui`, `orch_fx`, `orch_level`. CI compiles every source
with `-Werror` and runs them.

## The firmware build

The FM-1 firmware is built from a pinned HAWK CHUA / Felucca checkout
(`Plieuw/hawk-chua` at `e750df6`) with this repository overlaid on it:

```sh
python3 tools/prepare_hawk_runtime.py /path/to/clean/hawk-chua
cd /path/to/clean/hawk-chua && ./build.sh      # JieLi toolchain + AC79 SDK
```

The overlay replaces HAWK's synth, keyboard path and screen with the orchestral
engine (`runtime/hawk/orch_hawk_bridge.c`) and this panel
(`runtime/hawk/orch_hawk_panel.c`), and keeps HAWK's boot, watchdog, USB/TRS
MIDI, flash storage, OTA update entry and UBOOT combination. The GitHub Actions
job `hawk-target-build` does all of this and uploads `felucca.bin`, the `.fwsc`
package and their checksums as an artifact. See
[docs/BUILD_FIRMWARE.md](docs/BUILD_FIRMWARE.md).

## The browser build

`web/emu/` compiles the same app, engine and panel to WebAssembly; see
[docs/WEB.md](docs/WEB.md).

The FM1 Core platform, HAWK CHUA and the JieLi SDK retain their own licenses.
This project's GPL-3.0 license remains in force for original application code.
