`orch_ui_state.inc` (edits, sequencer, flash record, note map, leds, knob
speed) and `orch_ui_api.inc` (entry points). Put new code in the part it
belongs to.

- Pages are **STAGE**, **ARP**, **SEQUENCE**, **CONSOLE**, **FX**.
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
