# The screen

What the FM-1's 240 × 240 panel shows, and the files it is drawn from. The
controls are in [`PANEL.md`](PANEL.md); the behaviour behind them, and what is
tested, in [`PANEL-REFERENCE.md`](PANEL-REFERENCE.md).

## Vectors, not pixels

The pages are built from paths. A path is a list of points; circles, arcs and
the bows between two points are flattened into that list when the path is built,
so the rasterizer only ever sees straight edges. It then fills a path one span
per scanline: a disc of radius 24 costs 48 fills, not the 1 800 single-pixel
fills it used to.

That matters because the panel has no framebuffer: every `fm1_screen_fill` is a
window command plus a transfer over the display bus, and the cost of a page is
the number of calls, not the number of pixels. `orch_ui_path.inc` is the whole
geometry layer:

* `vp_move`, `vp_line`, `vp_quad`, `vp_arc`, `vp_arc_rev`, `vp_circle` — build
  the path; a curve is flattened with as many steps as it bends;
* `vp_fill`, `vp_fill_shaded`, `vp_stroke` — rasterize it: even-odd crossings per
  row, sorted, filled as spans, with the end pixels of each span mixed towards
  the colour the shape sits on, which is where the round edge comes from.

`orch_ui_draw.inc` sits on top: `mix565`, `grad`, `disc`, `pen`, `arc_soft`,
`curve`. Changing a page is now changing its geometry — a knob is a circle, a
stadium and an arc, and a bigger knob is a bigger number — instead of rewriting
a loop over pixels.

## Only what moves is repainted

The control task compares three signatures per scan: the page's own settings (a
mode, a mix, the open step), the values that change on their own (the running
sequencer step, the arp's held keys, a part starting or stopping), and the knob
strip. Each has its own cost:

* a setting redraws the page body;
* a live value repaints only the pixels it moves — two step cells, the row of
  key dots, or the players on the stage;
* a turned knob patches its own cell: its cap, its arc, its name and its value,
  and the cell that lost the highlight before it.

On the stage page the hall is two pictures: the art (wall, opening with its
arch, boards, pool of light, curtains, audience) is drawn when the page is
entered, and the life (players, the listener's seat, the instrument's name) is
drawn on its own afterwards. A knob turn there no longer repaints the wall.

## What drawing costs

Measured on the host with `tests/run_panel_bench.sh`, which drives the same five
pages through the same controls as the preview and counts the calls:

| what | fills before | fills now |
| --- | --- | --- |
| STAGE | 63 106 | 4 740 |
| ARP | 9 370 | 1 013 |
| SEQUENCE | 61 723 | 2 150 |
| FX | 87 262 | 7 152 |
| CONSOLE | 4 366 | 1 241 |
| one knob turn | — | 554 |
| its highlight fading | 819 | 197 |
| **five pages** | **225 827** | **17 047** |

The pixels are the same (949 k before, 462 k now — the desks and step bars are
flat bands instead of gradients, and the hall's art is only painted when the
page is entered), so this is the same screen for 7.5% of the fills. What is left
is the page body a setting repaints, not the shapes and not the strip. These are
host numbers for the number of calls; they are not a measurement of the FM-1's
own frame time, which needs the board.

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
| `app/orch_ui_path.inc` | paths, the span rasterizer, strokes |
| `app/orch_ui_draw.inc` | `mix565`, `isqrt`, gradients, discs, pens, arcs, curves |
| `app/orch_ui_view.inc` | the string helpers, `kv`, `card`, `bar`, `text_center` |
| `app/orch_ui_strip.inc` | the four knobs, their value arcs, the strip and its per-cell patch |
| `app/orch_ui_stage.inc` | the STAGE page, the hall's art and its life |
| `app/orch_ui_pages.inc` | ARP and SEQUENCE |
| `app/orch_ui_fx.inc` | CONSOLE and FX |
| `app/orch_ui_frame.inc` | the title bar, the body, the two repaint signatures, the live repaint |
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

`tests/run_panel_bench.sh` runs the same five pages with the fill and text-blit
calls counted, and prints them per page, plus what one knob turn and its fade
cost:

```sh
FM1_CORE=../fm1-core sh tests/run_panel_bench.sh /tmp/bench
```

The PNGs are not committed: they are output of that command and go stale the
moment the screen changes.