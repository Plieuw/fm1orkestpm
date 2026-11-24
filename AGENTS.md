# AGENTS.md

Read this first, then `docs/PARALLEL_WORK.md` (branches, measured facts, the
handover section), `docs/PANEL.md` (what every control and page does) and
`docs/NEXT_BETA.md`, the brief for whoever takes the light over the boards into
the next beta.

## Where the work is

The FM-1 panel, the orchestra seating and the movable listener live on
`beta/0.4.0-podium-light`, nowhere else. It was branched from the tip of
`beta/0.3.1-build-and-host-checks` (which still carries the same work up to the
photographed hall); 0.4.0 adds the photograph, the light over the boards and the
panel bench. Other branches in this repository carry an older, independent panel
inside `app/fm1_app.c`; do not merge that one next to this. Never force-push a
branch another agent works on, one topic per commit, and leave
`sh tests/run_tests.sh` green.

## Build and test

```sh
FM1_CORE=../fm1-core sh tests/run_tests.sh          # eight suites
cc -std=c11 -O2 -Wall -Wextra -Werror -DFM1_HOST_TEST \
   -I../fm1-core/include -Iapp -c app/<file>.c -o /tmp/x.o
```

There is no JieLi PI32v2 toolchain and no board in this environment: no
firmware package is built, nothing is flashed, and nothing has been heard.
Never write that the engine or the panel is tested on the FM-1. Everything here
is a host compile and a host render.

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

`git push` does not work here (no credentials: `could not read Username for
'https://github.com'`). Commits are made through the GitHub integration
(`GITHUB_COMMIT_MULTIPLE_FILES`), which writes real commits on the branch. Each
upsert carries the **whole file** and a single upload above roughly 330 lines
gets truncated, which happened twice on this branch (commits `7b4b379` and
`7f2e5dd` hold half a panel; `9fdcb94` repaired it). Keep source files small
and split by section rather than growing one long file. After every publish,
verify with a fresh clone:

```sh
git clone --depth 1 -b <branch> https://github.com/Plieuw/fm1orkestpm /tmp/v
diff -rq --exclude=.git --exclude='*.png' /tmp/v <workspace>
cd /tmp/v && FM1_CORE=<core> sh tests/run_tests.sh
```

## The panel

`app/orch_ui.c` is one translation unit: state, 5x7 font and strings, then four
included parts — `orch_ui_view.inc` (knob cells, strip, stage),
`orch_ui_pages.inc` (the five pages, title, page signature),
`orch_ui_state.inc` (edits, sequencer, flash record, note map, leds, knob
speed) and `orch_ui_api.inc` (entry points). Put new code in the part it
belongs to.

- Pages are **STAGE**, **ARP**, **SEQUENCE**, **CONSOLE**, **FX**.
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
- The listener's seat is appended to the flash record with its own FNV-1a
  checksum, so older records still load. Keep that pattern for new blocks.
