# Panel: buttons, encoders, keys and screen

`app/orch_ui.c` turns the FM-1 panel into a control surface for the orchestral
engine. Everything in this file ran as a host build; **nothing has run on an
FM-1 yet**, and this machine has no PI32 toolchain, so no `.fwsc` image was
produced here either. Read the last section before you claim it works.

## Panel map

| control | does |
|---|---|
| buttons 1–4 | select part 1–4 |
| buttons 5–9 | family: strings, woodwinds, brass, mallets, choir |
| button 10 / 11 | articulation down / up (wraps around `ORCH_ART_COUNT`) |
| button 12 | sequencer run / stop |
| button 13 | panic (all notes off) |
| button 14 | play a four-part C major chord, released after 900 ms |
| encoder 1 | instrument inside the selected family (wraps) |
| encoder 2 | divisi players, 1–4 |
| encoder 3 | voices this part may hold, 1–4 |
| encoder 4 | tempo, 30–300 in steps of 4 |
| encoder 5 | select part (wraps) |
| encoder 6 | family (wraps) |
| encoder 7 | spare |
| knob | master volume |
| LEDs 1–4 | selected part |
| LEDs 5–9 | family of the selected part |
| LEDs 10–14 | pressed buttons |
| key LEDs | pressed keys |

Keys do **not** generate notes here. The platform maps the key matrix to tracks
and calls `groove_midi_note()`, which forwards to `orch_note_event()`; posting
notes from the panel scan as well would retrigger every key twice.

## How it is wired

- `groove_screen_init()` starts the engine once (`orch_init()`) and then
  `orch_ui_init()`: `fm1_controls_init()`, `fm1_screen_init()`, first draw.
- `groove_screen_control()` calls `orch_ui_control()`, which reads the panel at
  most every 25 ms (`fm1_millis()`), applies the edits and draws only what
  changed.
- Edits use the public engine API only: `orch_engine_configure_part()`,
  `orch_engine_set_instrument()`, `orch_engine_set_articulation()`,
  `orch_engine_set_player_cap()`, `orch_engine_set_voice_ceiling()`,
  `orch_seq_set_tempo()`, `orch_seq_set_enabled()`, `orch_panic()`.
  `orch_part_configure()` resets a part's instrument to the family default,
  which is why the family buttons are the only place that calls it.
- `fm1_app_render()` replaces the SDK's block renderer, so it also owns the
  master level: `orch_ui_master_q23` is a single volatile word written by the
  control task and read by the audio interrupt. At unity the multiply is
  skipped.

## Graphics

The SDK exposes `fm1_screen_fill()` and `fm1_screen_blit()` and no text at all,
so the file carries its own 5×7 font (41 glyphs) and draws one 8-pixel text
line into a 240×8 buffer, then blits it in one transfer. The voice meters are
redrawn as a 228×5 strip when the count changes, so a sounding note stays
visible without re-sending any text.

Layout: a title bar, four part rows (family, players, instrument, articulation,
voice meter) and a footer with part, tempo, run state, volume, articulation and
the button hints.

The layout, as rendered from this drawing code into `docs/panel.png` locally
(not committed: the publishing path here is text-only, so regenerate it with
the commands below):

```
FM1 ORCHESTRA
P1 STRINGS            2PL      <- part, family, players
VIOLIN               AUTO      <- instrument, articulation
[##########----------------]    <- voices / ceiling
...
PART 3  TEMPO 120  STOP
VOL 80  ART AUTO
```

To look at the screen without hardware:

```sh
cc -std=c11 -O2 -DFM1_HOST_TEST -I../fm1-core/include -Iapp \
   tests/panel_preview.c app/orch_ui.c app/orch_engine.c app/orch_adaptive.c \
   app/orch_divisi.c app/orch_part.c -o /tmp/panel_preview -lm
/tmp/panel_preview /tmp/panel.ppm
python3 tools/panel_png.py /tmp/panel.ppm docs/panel.png
```

`tests/panel_preview.c` stubs the board services and paints into a 240×240
RGB565 array. It is a preview of the layout, not evidence that the ST7789 shows
it.

## Checks that were run

- `tests/run_tests.sh`: four suites, all green, including the new
  `tests/orch_ui_test.c`, which drives button presses and encoder detents
  through `orch_ui_handle()` with the board stubbed, and asserts the resulting
  part state, clamping, knob gain, panic and chord release.
- Every `app/*.c` compiles with `-std=c11 -Wall -Wextra -Werror
  -DFM1_HOST_TEST`.
- The preview above was rendered from the real drawing code.

## Not verified, and why

- **No firmware image.** `tools/build.py` needs the JieLi PI32v2 toolchain
  (`~/.jieli/toolchain` or `FM1_PI32_TOOLCHAIN`); it is absent here, so the
  build stops at `cannot find the JieLi PI32v2 toolchain`. On a machine with the
  toolchain: `python3 tools/build.py --app path/to/fm1orkestpm/app`, then
  `--package`. Keep a stock recovery image and know how to reach the
  bootloader.
- **Nothing seen on the display.** No panel, no keys, no knob.
- **Encoder direction and detent rate on real hardware** are untested; the
  panel layer reports detents, the sign is assumed to be positive clockwise.
- **CPU and RAM on the target.** 12 voices of 2048 + 512 int16 samples is about
  60 KiB of BSS before state, plus 3.8 KiB for the text buffer. The master gain
  uses a 64-bit multiply per sample when it is below unity; that cost is
  unmeasured. The engine is also not in `.volatile_ram_code`, so audio code is
  not safe while user Flash is being programmed.

## Coordination with the 0.3.3 DSP work

The 0.3.3 brainstorm says the old linear friction feedback produced a large DC
offset and must not return without a measured model and a passing regression
test. The friction bow on this branch is that model: `test_strings_are_not_dc`
guards the DC, the pitch and level numbers in `docs/BRAINSTORM.md` are measured
on the host, and `tests/run_measure.sh` reproduces them.

The round-trip gain normalisation from 0.3.2/0.3.3 is **not** in this branch. It
is compatible: its `loop_gain_q23` field and `loop_gain_per_sample_q23()` helper
do not collide with the friction fields `exc_dc` and `bow_gain`, so it can be
ported on top. Port it before tuning decay, and re-run the pitch and level
measurements afterwards, because the sustain of a bowed note depends on the loop
loss.
