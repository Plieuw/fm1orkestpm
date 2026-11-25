# AGENTS.md

Read this first, then `docs/PARALLEL_WORK.md` (branches, measured facts, the
handover section), `docs/PANEL.md` (what every control and page does) and
`docs/NEXT_BETA.md`, the brief for whoever takes the light over the boards into
the next beta.

## Where the work is

The newest work is on `beta/0.6.0-refine` (`docs/CHANGES_0.6.0.md`), which
continues `beta/0.5.0-orchestra-merge`. Everything lives there since 25 November 2026: the
0.3.x feature line's engine, performance engine, scenes and HAWK overlay, and
the 0.4.0 panel, stage, level light, FX and browser build, merged
(`docs/MERGE_0.5.0.md`). Start new work there, not on 0.4.0 or on a
`feature/0.3.x` branch. There is one panel (`app/orch_ui.c`); do not bring
back the 3x5-font panel of the 0.3.x `app/fm1_app.c`. Never force-push a
branch another agent works on, one topic per commit, and leave
`sh tests/run_tests.sh` green.

## Build and test

```sh
FM1_CORE=../fm1-core sh tests/run_tests.sh          # eight suites
cc -std=c11 -O2 -Wall -Wextra -Werror -DFM1_HOST_TEST \
   -I../fm1-core/include -Iapp -c app/<file>.c -o /tmp/x.o
```

There is no JieLi PI32v2 toolchain and no board in this environment. The
firmware package is built by CI (`hawk-target-build`, see
`docs/BUILD_FIRMWARE.md`); nothing has been flashed or heard. Never write that
the engine or the panel is tested on the FM-1.

The firmware build is freestanding: no `<string.h>`, no 64-bit divides (the
PI32v2 has none), no `uint64_t /` anywhere in `app/`. Every `app/orch_*` file
is copied into the HAWK tree by `tools/prepare_hawk_runtime.py`.

## The browser build

`web/emu/` compiles the app and the engine to WebAssembly behind a hardware
shim, so the instrument can be played and looked at without a board — see
`docs/WEB.md`. It is modelled on HAWK CHUA's `web/emu` (newest on `main`,
649102d), not copied: same idea, our SDK API.

```sh
pip install ziglang==0.13.0            # or have clang + wasm-ld
FM1_CORE=../fm1-core sh web/emu/build.sh
node web/emu/smoke.mjs                 # screen, audio, flash record
python3 -m http.server 3000            # repo root, then web/emu/index.html
```

`build/emu/orch.wasm` is build output and stays out of git. The browser proves
the code runs and sounds somewhere; it says nothing about the FM-1's codec or
about audio-block time, so it is not a hardware test either.

## Publishing

Where `git push` works, push. Where it does not and commits go through the
GitHub integration (`GITHUB_COMMIT_MULTIPLE_FILES`), each upsert carries the
**whole file** and a single upload above roughly 330 lines gets truncated,
which happened twice on 0.4.0 (commits `7b4b379` and `7f2e5dd` hold half a
panel; `9fdcb94` repaired it). Keep source files small and split by section.
After every publish, verify with a fresh clone:

```sh
git clone --depth 1 -b <branch> https://github.com/Plieuw/fm1orkestpm /tmp/v
diff -rq --exclude=.git --exclude='*.png' /tmp/v <workspace>
cd /tmp/v && FM1_CORE=<core> sh tests/run_tests.sh
```

## The panel

`app/orch_ui.c` is one translation unit: state, 5x7 font and strings, then
included parts — `orch_ui_view.inc` (knob cells, strip, stage),
`orch_ui_pages.inc` (the knobs' jobs, ARP and SEQUENCE), `orch_ui_shape.inc`
(ENVELOPE and LFO),
`orch_ui_state.inc` (edits, sequencer, flash record, note map, leds, knob
speed) and `orch_ui_api.inc` (entry points). Put new code in the part it
belongs to.

- Pages are **STAGE**, **ARP**, **SEQUENCE**, **CONSOLE**, **FX**,
  **ENVELOPE**, **LFO**; eight parts.
- Edits that write engine state go inside `fm1_engine_pause()` /
  `fm1_engine_resume()` (on the firmware: interrupts off); drawing and the
  flash write stay outside.
- The STAGE page is a vector seating plan (`orch_ui_stage.inc`), drawn by the
  anti-aliased rasterizer in `orch_ui_path.inc` (1/16 px, four sub-rows a row).
  Count fill calls with `tests/run_panel_bench.sh` when you change it.
- Each of the four menus has a band of its own above the knobs — the harp, the
  score, the console, the rack — a 240 x 30 photograph in
  `app/orch_ui_pic_*.inc`, drawn once when the page is entered through
  `picture()` in `app/orch_ui_picture.inc`. A knob turn repaints the values
  over the band, not the band. `sh tools/panel_photo/regen.sh` rebuilds all
  five pictures from the images in `tools/panel_photo/` and its `band/`.
- The palette is the hall's: curtain burgundy in the title bar, wood for rows,
  steps and knob discs, warm stone for the shell. Keep the contrast rule: the
  readings stay white, green or orange; a tint sits behind a header band or a
  selected row, never behind a value field.
- The pan comes from `app/orch_stage.c` (instrument seat, section spread,
  listener). Do not reintroduce a fixed pan per part number.
- Visual checks: `tests/panel_preview.c` renders five pages to PPM, plus a
  sixth frame of the full orchestra at the top of the light's scale, and
  `tools/panel_png.py` turns them into PNGs; `web/emu/` renders the same panel
  in a browser. Look at them; the theme cannot be checked any other way without
  a board.
- The flash record is "OR60" (`struct save_record` in `orch_ui.c`; a
  compile-time check keeps it within the firmware's 3 840-byte object). Append new
  blocks after `check` with their own FNV-1a checksum, so this record still
  loads.
