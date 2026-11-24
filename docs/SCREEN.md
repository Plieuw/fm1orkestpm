# The screen

What the FM-1's 240 × 240 panel shows, and the files it is drawn from. The
controls are in [`PANEL.md`](PANEL.md); the behaviour behind them, and what is
tested, in [`PANEL-REFERENCE.md`](PANEL-REFERENCE.md).

## Drawn, not printed

The screen is a picture of an instrument, not a page of text. There is no
framebuffer to read back, so nothing blends against what is already there:
every shape takes the colour it is drawn on and mixes towards it at its edges.
That is what makes a disc look round, a knob look turned, and a curve look
drawn rather than stepped — `orch_ui_draw.inc`:

* `mix565(a, b, t)` — a colour part-way between two, in RGB565;
* `grad(x, y, w, h, top, bottom)` — one colour a row, so a field is not a flat
  block;
* `disc(cx, cy, r, core, edge, bg)` — a shaded disc with a soft rim: knobs,
  players, the listener's seat, the lamps on the FX page;
* `pen(x0, y0, x1, y1, w, fg, bg)` — a capsule, the pen the arcs and the curves
  are drawn with;
* `arc_soft(cx, cy, r, a0, a1, w, fg, bg)` — an arc in 256ths of a turn from
  twelve o'clock: the proscenium, the shell, a knob's value;
* `curve(v, n, x, y, w, h, fg, bg)` — a line through values: the sequencer's
  pitches and each effect's own shape.

`isqrt()` is a bit-by-bit square root, because the board has no maths library.

## One palette, the page's own

| use | colour | RGB565 |
| --- | --- | --- |
| the case | coffee `#121110` | `0x1082` |
| a card, a track | `#21201e` | `0x2104` |
| a selected row, a knob cap | `#2b2926` | `0x2945` |
| a hairline | `#3a352f` | `0x39A5` |
| a value, a choice, light | amber `#f5a623` | `0xF524` |
| the value of the knob that just moved | light amber `#ffcf73` | `0xFE6E` |
| a section that is sounding | green | `0x07E0` |
| recording | wine red | `0xE2A7` |
| the hall | burgundy curtains, wooden floor | `0x8800`, `0x8200` |

## The frame

The top bar carries the page name in amber, the part and the tempo at the
right, and five segments under them, the open page's amber. The bottom 50
pixels are the knob strip: four knobs, each with its value drawn as an arc
around it, its name under it and the number under that. The arc is the readout
a player sees from a metre away; the number is there for the values that are
not percentages.

The 5x7 font (`orch_ui_font.inc`) is still the instrument's own: it is used for
numbers and for one word where a word is shorter than a picture. It has the
letters, the digits and ` -./:+(),<>%#`.

## What each page draws

*STAGE* — four columns on the left, one per part, each filling from the floor
up with the voices that part is sounding; the selected one wears an amber edge.
Beside them the hall: a warm wall, the arch of the stage with its curtains, the
boards running towards you with a pool of light, the players standing where
their sections sit (lit when they sound) and two rows of the audience, the
listener's seat in amber. The selected part's instrument is the one word on the
page. `KNOB3` moves the listener across, `KNOB4` back.

*ARP* — the pattern itself: the eight notes of one cycle rising across the
octaves, joined by a line, each with a bar as long as its gate, the held keys as
dots under the axis, and the rate and the mode as the only two words.

*SEQUENCE* — sixteen steps as cards whose height is the velocity, the open step
framed in amber, the playing one underlined in green; under them the phrase as
a line through the open steps' pitches, with the open step as an amber dot, its
number and its note names in the corners.

*CONSOLE* — three faders (tempo, transpose, volume) with their numbers under
them, the note map as seven degrees, and the save state as one word.

*FX* — one line per effect: its name, its mix as a level bar and a lamp for on,
the open effect marked with an amber edge. Under the list, the shape that
effect gives a note, drawn from its own two parameters: a wobble for the chorus
and the ensemble, echoes that decay for the delay, a long ringing tail for the
shimmer. Its two parameter numbers are the only text on the card.

## The files

The panel is one translation unit: `app/orch_ui.c` included from `app/`, so
`-Iapp` is enough to build it and no file runs to a thousand lines.

| file | what it holds |
| --- | --- |
| `app/orch_ui.c` | the palette, the panel's state, the names the engine gives, the include list |
| `app/orch_ui_font.inc` | the 5x7 font and `text()` |
| `app/orch_ui_draw.inc` | `mix565`, gradients, discs, pens, arcs, curves |
| `app/orch_ui_view.inc` | the string helpers, `kv`, `card`, `bar`, `text_center` |
| `app/orch_ui_strip.inc` | the four knobs, their value arcs and the strip |
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
