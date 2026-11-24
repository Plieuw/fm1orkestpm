# Next beta: the light over the boards

Written 24 November 2026, at the end of the 0.4.0 work, for the agent who picks
up the next beta. Read it together with `AGENTS.md` and
`docs/PARALLEL_WORK.md`; this file is only about the podium light and the level
meter — what it is, what is proven, and what is still open.

Start the next branch from `beta/0.4.0-podium-light`, not from 0.3.1: the light,
the photograph and the bench live only there.

## What it does

A light lies on the floor in front of the boards on the STAGE page. Its width is
the size of the orchestra and the loudness of the music at once:

- **size** comes from the voices that are sounding: three steps a voice, so eight
  voices — the four parts at two each — are the top of the scale, `ORCH_LEVEL_MAX`
  (`app/orch_level.h`). A part at one note is two players, six steps.
- **inside that size** the peak of the finished block moves the light: env is
  `peak * 24 / LEVEL_TOP` with `LEVEL_TOP 2100000`, clamped to 24. An attack
  widens the cone; a note left ringing narrows it again while the key stays down.
- the fall is slow (`(gap + 63) >> 6` a block) and the size is held while the tail
  rings, so a release is not a blackout.

Why the voices are counted at all: the renderer keeps its mix at one level
whatever the polyphony. Measured on the host, one voice peaks 4.5 M of Q23's
8.39 M and eight voices peak 3.7 M — output alone cannot tell you how big the
orchestra is. The output says how loud, the voices say how many.

## The files

| file | what it holds |
| --- | --- |
| `app/orch_level.h` | `ORCH_LEVEL_MAX 24`, `LEVEL_EIGHT 8`, `LEVEL_TOP 2100000`, the two entry points |
| `app/orch_level.inc` | `orch_level_take()` (called once per block) and `orch_output_level()` |
| `app/fm1_app.c` | calls `orch_level_take(output, 2U * frames)` after the FX and the master gain |
| `app/orch_ui.c` | includes `orch_level.h` + `orch_level.inc` before `orch_ui_stage.inc` |
| `app/orch_ui_stage.inc` | `draw_stage_light()`, the `LIP_*` colours, the trough on the photograph's own dark floor |
| `app/orch_ui_frame.inc` | the level in the HOME live signature: `(orch_output_level() & 0x1F) << 24` |
| `tests/orch_level_test.c` | the eighth suite: the scale, and a level that falls with the keys held |
| `tests/panel_bench.c` | `sound 0.5s`, `orchestra`, and the `level` printouts |
| `tests/panel_preview.c` | six frames, the last one the full orchestra at 24 of 24 |
| `docs/SCREEN.md` | what the light looks like and what a repaint costs |

## Keep these rules

1. **Five fills an update, at most every 50 ms.** The trough and four bars are
   drawn over the photograph's own dark colour (`LIP_DARK 0x1860`, y 145..151,
   x 68..172), so nothing behind the light is ever repainted. Do not widen the
   area or raise the throttle without measuring it in the bench: that is the
   whole point of the design.
2. **The level is not in the page signature; it is in the live signature.** The
   page body is not redrawn when the music moves, only the light and the parts'
   overlays.
3. **Calibration stays on eight voices = 24.** If a later beta raises
   `ORCH_MAX_VOICES` or lets a part sound more than two players, rescale here
   deliberately and update `docs/SCREEN.md`, the bench and the test together.
4. **The test must keep the keys down.** `tests/orch_level_test.c` proves the
   point by never releasing the note: the part's note count is read before and
   after the level falls. Do not "fix" it by adding a note-off.
5. **Nothing here is a hardware claim.** There is no PI32v2 toolchain and no
   board in this environment; every number is a host render.

## How to check it

```sh
FM1_CORE=../fm1-core sh tests/run_tests.sh          # eight suites
FM1_CORE=../fm1-core sh tests/run_panel_bench.sh /tmp/bench
```

What the bench must still print:

```
level      6 of 24 while one note sounds
level      24 of 24 for a full orchestra (8 voices)
level      0 of 24 after the notes are off
sound 0.5s fills    268 (  6894 px)  blits   3 ( 2880 px)
orchestra  fills   1818 ( 49728 px)  blits  16 (15360 px)
```

The preview, for the picture of the top of the scale:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -DFM1_HOST_TEST -I../fm1-core/include -Iapp \
   tests/panel_preview.c app/orch_ui.c app/orch_engine.c app/orch_fx.c \
   app/orch_adaptive.c app/orch_divisi.c app/orch_part.c app/orch_stage.c \
   -o /tmp/panel_preview -lm
/tmp/panel_preview /tmp/p        # prints: orchestra: level 24 of 24, 8 voices
python3 tools/panel_png.py /tmp/p-orchestra.ppm /tmp/p-orchestra.png
```

## Measured, on the host

| what | number |
| --- | --- |
| one held string (two players) | level 6 of 24 |
| eight voices, four parts | level 24 of 24 |
| after the notes are off and the tails rendered | 0 |
| one voice / eight voices, Q23 peak | 4.5 M / 3.7 M |
| a repaint of the light | 5 fills, ≤ 20 a second |
| the light's half second of one note | 268 fills |
| the full orchestral page | 1 818 fills, 16 blits |

## Open, and honestly unfinished

- **Struck instruments barely light it.** Measured peak of one note at velocity
  127: marimba 0.70 M, timpani 0.73 M, xylophone 0.49 M, against a bowed string's
  1.8 M early and 8.4 M once it swells — and `LEVEL_TOP` is 2.1 M. A mallet solo
  therefore moves the light a step or two inside its size and then lets it fall,
  which is why the falling test uses a timpani: it is audible enough to prove the
  property. If mallets should light the cone like the strings, that is a decision
  about a per-family reference level in `orch_level.inc` — measure it, do not
  guess, and keep the eight-voice calibration intact.
- **The browser build does not show it yet.** `web/emu/` renders the same panel,
  but nothing there has been checked against the light; the wasm build and its
  smoke test pass (284 933 B, 29 906 lit pixels, magic `0x4f524348`).
- **Nobody has heard or seen any of this on an FM-1.** The audio-block cost of
  `orch_level_take()` and the real ST7789 timing of five fills are unmeasured.
- The light's floor is the photograph's own dark band. A different hall picture
  needs the trough and `LIP_DARK` re-measured against it.

## Publishing from here

No git credentials in this environment: commits go through the GitHub
integration (`GITHUB_COMMIT_MULTIPLE_FILES`), one whole file per upsert, and a
single upload above roughly 330 lines gets truncated. After every upload, take a
fresh clone of the branch, `diff -rq` it against the working tree, run the suites
and compare `git hash-object <file>` with the blob sha the commit reports. That
is how the half-transferred panels (`7b4b379`, `7f2e5dd`) were caught.

Commits of the 0.4.0 round, in order: `b7956b4` `5cc013b` (branch and docs),
`25f7216` `60f31ed` `e09d061` `9ad7151` (light, level, bench), `56a8ad3` (the
level test), `1773efa` (the orchestra frame), `11b2a09` `6d0aff3` `e906ea0`
(the documents).
