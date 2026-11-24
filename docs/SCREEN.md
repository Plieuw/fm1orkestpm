# The screen

What the FM-1's 240 × 240 panel shows, and the files it is drawn from. The
controls are in [`PANEL.md`](PANEL.md); the behaviour behind them, and what is
tested, in [`PANEL-REFERENCE.md`](PANEL-REFERENCE.md).

## One palette, the page's own

The panel and the browser page (`web/emu`, see [`WEB.md`](WEB.md)) are the same
instrument, so they wear the same clothes:

| use | colour | RGB565 |
| --- | --- | --- |
| the case | coffee `#121110` | `0x1082` |
| a card, a step | `#21201e` | `0x2104` |
| a selected row | `#2b2926` | `0x2945` |
| a hairline between things | `#3a352f` | `0x39A5` |
| a value, a choice, the page name | amber `#f5a623` | `0xF524` |
| the value of the knob that just moved | light amber `#ffcf73` | `0xFE6E` |
| a section that is sounding, a step that is playing | green | `0x07E0` |
| recording | wine red | `0xE2A7` |
| the hall | burgundy curtains, wooden floor | `0x8800`, `0x8200` |

Nothing that has to be read is drawn on a wash: text is white or mid grey, and
a tint behind it is only ever a card or a selected row.

## One shape for a row

Every settings page is built from the same pieces, in `orch_ui_view.inc`:

* `kv(y, label, value, colour, bg)` — the label in grey at the left, the value in
  its own colour against the right edge. Right-aligned, so a value that grows a
  digit pushes left instead of running into the label. (The FX page used to put
  four columns of numbers side by side, and they overlapped; that is what this
  shape replaced.)
* `card(x, y, w, h, fill)` — a card with a one pixel hairline, for settings and
  for the selected effect.
* `bar(x, y, w, h, pct, fg, bg)` — a meter with a real trough, so a silent part
  looks silent.

The top of every page is a title bar: the page name in amber, the part and the
tempo at the right, and five segments under them, the open page's lit amber.
The bottom 50 pixels are the knob strip: a hairline, then four knobs with their
labels and values.

The 5x7 font lives in `orch_ui_font.inc` with `text()`, which draws one line
into a small buffer and blits it in one transfer. It has the letters, the
digits and ` -./:+(),<>%#`.

## The five pages

Named for the hall the instrument plays in: **STAGE**, **ARP**, **SEQUENCE**,
**CONSOLE**, **FX**.

*STAGE* — the page the panel opens on. One row per part: family, instrument and
articulation, the player count, and a live meter of the voices it is using; the
selected part's row carries an amber edge. Under the rows is the hall itself:
the orchestra shell, the wooden floor, the curtains at the sides and the
audience row in front, with a block for each part on its section's place (the
selected part in green) and the listener's seat lit amber. `KNOB3` moves the
listener across the stage and `KNOB4` moves it back.

*ARP* — mode, octaves, rate, gate and the number of held keys, with a bar of how
much of the part the arpeggiator has taken, and one hint line per button that
works here.

*SEQUENCE* — sixteen steps as cards: the selected step framed in amber, a step
that holds notes with an amber bar, the step that is playing with a green one.
Below the grid: the step number, its notes (in the font's note names, `C4`,
`F#3`), velocity and gate.

*CONSOLE* — tempo, transpose, scale, volume and the save state inside a card,
with the shortcuts of the buttons that are not page specific.

*FX* — one line per effect: name, on/off and mix, the selected line marked with
an amber edge. The two parameters of the selected effect get the card under the
list, so nothing is squeezed into a column. `ALGORITHM` moves the selection,
`KNOB1` is the mix, `KNOB2` and `KNOB3` are the two parameters, `KNOB4`
switches the effect on or off.

## The files

The panel is one translation unit: `app/orch_ui.c` included from `app/`, so
`-Iapp` is enough to build it and no file runs to a thousand lines.

| file | what it holds |
| --- | --- |
| `app/orch_ui.c` | the palette, the panel's state, the names the engine gives, the include list |
| `app/orch_ui_font.inc` | the 5x7 font and `text()` |
| `app/orch_ui_view.inc` | the string helpers, `kv`, `card`, `bar`, `text_center` |
| `app/orch_ui_strip.inc` | the four knob cells and the bottom strip |
| `app/orch_ui_stage.inc` | the STAGE page and the hall |
| `app/orch_ui_pages.inc` | ARP and SEQUENCE |
| `app/orch_ui_fx.inc` | CONSOLE and FX |
| `app/orch_ui_frame.inc` | the title bar, the page switch, the repaint signature |
| `app/orch_ui_state.inc` | the edits, the save record, the note map, the LEDs, the knob speed |
| `app/orch_ui_api.inc` | the entry points the app calls |

## Looking at it without a board

`tests/panel_preview.c` renders the five pages on the host, with the board
services stubbed. This is the only way to see the screen with no FM-1 attached;
it is a preview of the layout, not evidence that the ST7789 shows it.

```sh
cc -std=c11 -O2 -DFM1_HOST_TEST -I../fm1-core/include -Iapp \
   tests/panel_preview.c app/orch_ui.c app/orch_engine.c app/orch_fx.c \
   app/orch_adaptive.c app/orch_divisi.c app/orch_part.c app/orch_stage.c \
   -o /tmp/panel_preview -lm
/tmp/panel_preview /tmp/p
python3 tools/panel_png.py /tmp/p-parts.ppm /tmp/p-parts.png
```

It writes the STAGE page with an orchestra in the hall and a four-part chord
sounding, the ARP page with three held keys, the SEQUENCE page with three
recorded steps and the playing step marked, the FX page with all four effects
on and the delay row selected, and the CONSOLE page.

The PNGs are not committed: they are output of that command and go stale the
moment the screen changes.
