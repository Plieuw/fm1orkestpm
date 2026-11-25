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

`orch_ui_draw.inc` sits on top: `mix565`, `disc`, `pen`, `arc_soft`, `curve`.
Changing a page is now changing its geometry — a knob is a circle, a stadium and
an arc, and a bigger knob is a bigger number — instead of rewriting a loop over
pixels. Five pictures are not drawn at all: the hall on the stage page, and the
band under the work on each of the four menus, are photographs reduced to runs
of colour (see below).

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

On the stage page the photograph is the still part: it is drawn when the page is
entered, a step of the lights repaints only the band of rows they change in, and
a knob turn there repaints the four lamps, the players and the marks over it,
not the hall.

Each menu has a band of its own under the work and above the knobs, and it is
drawn the same way: once, when the page is entered. A knob turn on a menu
repaints the values over it, not the picture, so the turn still costs what it
did before the band existed — 507 fills on FX.

## What drawing costs

Measured on the host with `tests/run_panel_bench.sh`, which drives the same five
pages through the same controls as the preview and counts the calls:

| what | fills before | fills now |
| --- | --- | --- |
| STAGE, entered | 63 106 | 2 683 |
| ARP | 9 370 | 1 150 |
| SEQUENCE | 61 723 | 2 598 |
| FX | 87 262 | 6 858 |
| CONSOLE | 4 366 | 1 689 |
| one knob turn | — | 507 |
| its highlight fading | 819 | 197 |
| **five pages** | **225 827** | **23 415** |

The STAGE figure is entering the hall: the picture, the title, the marks and
the strip. The lights are counted apart, because they are the part that runs on
its own; the bench prints them as `lights 1` and `lights 2`, one step each:

| the hall's lights | before | now |
| --- | --- | --- |
| one step | 776 / 2 364 | 581 / 1 206 |
| a step every | 1.2 s | 2.4 s |
| fills in a second of playing | 647 – 1 970 | 242 – 503 |

That is where the animation was going: up to 1 970 fills a second, twice what a
knob turn costs, for a picture that had not moved. A step now repaints the band
the lights are in rather than the hall (see below), there are two frames
instead of three, and the cycle is slower — so the worst second of the
animation went from 1 970 fills to 503, and the whole five-page visit from
23 220 to 20 653. The menus' bands bring that visit back to 23 415: 2 762 fills
for four pictures, each of them paid once per visit, not once per frame. These
are host numbers for the number of calls; they are not a measurement of the
FM-1's own frame time, which needs the board.

The one thing on the screen that follows the sound is counted apart, as
`sound 0.5s` and `orchestra` in the same bench: a note held for half a second,
then a full orchestra, with the panel scanned every 25 ms. The light over the
boards costs five fills an update and updates at most once every 50 ms, so it
can never cost more than 100 fills a second; the bench's half second of one note
is 268 fills including the lamps' own overlays.

Its scale is the orchestra's, nought to 24: a voice is three steps, so eight
voices — the four parts at two each — are 24 of 24, and the peak of the block
moves the light inside that size, so an attack widens the cone and a ringing
note narrows it again. `level` prints it: 6 for one held note (two players in
this engine), 24 for the eight-voice orchestra, 0 once the notes are let go and
their tails rendered. The count is the part the renderer cannot give: it keeps
its mix at one level whatever the polyphony — measured on the host, one voice
peaks 4.5 M of Q23's 8.39 M and eight peak 3.7 M — so the output says how loud
the music is and the voices say how big the orchestra is.

## The hall is a photograph

`tools/panel_photo.py` turns photographs — or stills from a film — into the
panel's own picture format. The two frames it was used on are in
`tools/panel_photo/` (240 × 164, a few kilobytes each, committed beside the
tool), so the picture in `app/orch_ui_photo.inc` can be rebuilt from the
repository alone:

```sh
sh tools/panel_photo/regen.sh   # same bytes, every time
```

The tool centre-crops each image to 240 × 164, mixes it down to sixteen colours
taken from the images themselves, averages every 2 × 2 block to one cell, and
writes, for every row, the runs that row is made of:

* three hex characters a run: the palette index, then the run's width in cells;
* a row table per frame, so `draw_photo_rows()` starts at the right run;
* so the file is not only a picture but the list of window commands that draw
  it — `fm1_screen_fill` per run, about 1 600 for the whole hall.

Neighbouring runs whose colours are close enough to be one are merged
(`--merge 24`), which is what keeps a row at ten fills instead of a hundred and
twenty: the seams it removes are one palette step apart and cannot be seen.

Dithering is off for the same reason: a dithered photograph is a run of one or
two cells a colour, and that is a thousand extra fills. The palette is small
exactly where the picture is dark, which is most of a hall at night.

The first image is the rest frame; every later image is an animation frame, and
`--band LO:HI` is what makes a step cheap: outside those rows the later frames
keep the first frame's picture, so the panel can repaint the band the lights
change in instead of the whole hall.

The hall in the panel has two: the house as it is, and the same hall with the
lights down over the stage. `--band 0:46` is everything from the top of the
picture to the boards — the arch and the curtains, where the light changes; the
seats, the lamps and the front of the hall are the same picture in both, so a
step never touches them. While a part is sounding the page steps every 2.4 s;
when the hall is quiet it stays on the first frame.

The nights start in the same tool. Each of the four menus has a picture of its
own — `tools/panel_photo/band/arp.png`, `seq.png`, `glo.png`, `fx.png` — and
`tools/panel_photo/regen.sh` builds all five includes with it. Those four are
bands: 240 × 30, the strip a page has free between its work and its knobs. A
band is a fifth of the panel, so two things happen to the photograph before it
becomes one, and both are options of the tool:

* `--pick-rows 30` takes the band from the brightest window of thirty rows
  instead of the centre of the image, because cropped through the middle a night
  picture is its darkest part;
* `--expose 1.35 --contrast 1.15` lifts it enough to carry the picture at that
  size, so the band is a scene and not a black stripe.

That is done once, on the photograph. What `band/` holds is the result: the band
at the panel's own size, already in the sixteen colours it will be drawn in, a
kilobyte or two a picture. `regen.sh` reads those, so the repository rebuilds
the four bands as they are without carrying the photographs themselves, and a
band is the same picture every time it is built.

`--name PIC_ARP` is what keeps five pictures in one translation unit apart: the
defines and the tables carry that prefix (`PIC_ARP_W`, `pic_arp_palette`,
`pic_arp_runs0`), so `orch_ui_pictures.inc` can hold them all. The pictures
themselves are the harp the arp plays, the score the sequencer follows, the
console at the back of the hall, and the rack the effects live in — the same
hall, seen from the four places the four menus are about.

`orch_ui_picture.inc` is the one place that turns runs into fills: a
`struct panel_picture` holds a palette, its frames and their row tables, and
`picture()` / `picture_rows()` draw them at a corner. The hall has been drawn
through it since the menus got their bands, so there is one drawing path for
five pictures rather than two.

To change the picture, replace the frames in `tools/panel_photo/` and run the
same script — it is this command, with those two files:

```sh
python3 tools/panel_photo.py --parts --width 240 --height 164 --block 2 \
    --colors 16 --dither 0 --merge 24 --band 0:46 \
    tools/panel_photo/hall-a.jpg tools/panel_photo/hall-c.jpg \
    -o app/orch_ui_photo.inc
```

`--parts` writes each frame to `app/orch_ui_photo_<n>.inc`, included by
`orch_ui_photo.inc`; `--preview out.png` writes what the panel will show, which
is how the palette and the merge were chosen. Any PNG or JPEG works — a
photograph of a real hall, a frame from a concert film, a drawing; give it at
the panel's size and it stays a few kilobytes, so the source travels with the
code. The generated include names the frames it came from in its header.

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
| the hall | sixteen colours, out of the photograph | — |

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

*STAGE* — a photograph of a hall, and the instrument's life drawn over it: four
narrow lamps in the dark rows of seats fill from the bottom up with the voices
each part is sounding (the selected one amber-capped), the players stand on the
boards where their sections sit and light up when they sound, the listener's
seat is amber among the seats, and the selected part's instrument is the one
word on the page. While a part sounds the hall's own lights step, in the band
above the boards and nowhere else. `KNOB3` moves the listener across, `KNOB4`
back.

In front of the boards, on the dark floor between the hall and the listener,
lies a light that is the orchestra's size and the music's loudness at once:
three bands of amber falling away from the stage — a cone narrow at the boards
and wider as it reaches the floor — with a brighter line where it lands. Its
width is calibrated on the hall's own full orchestra, eight voices, which fills
it; the peak of the output moves it inside that width, so an attack widens it
and a note left ringing narrows it while the key is still down. The lamps above
keep following which parts sound, because that is a different question and the
panel answers both. The floor it lies on is the photograph's own dark colour,
kept as `LIP_DARK`: an update is a trough and four bars, so nothing behind the
light is ever repainted.

*ARP* — the pattern itself: the eight notes of one cycle rising across the
octaves, joined by a line, each with a bar as long as its gate, the held keys as
dots under the axis, and the rate and the mode as the only two words. Under the
card, the band: the harp the arp plays.

*SEQUENCE* — sixteen steps as cards whose height is the velocity, the open step
framed in amber, the playing one underlined in green; under them the phrase as
a line through the open steps' pitches, with the open step as an amber dot, its
number and its note names in the corners. Under the card, the band: the score,
under its one lamp.

*CONSOLE* — three faders (tempo, transpose, volume) with their numbers under
them, the note map as seven degrees, and the save state as one word. Under it
all, the band: the console at the back of the hall, its own faders lit.

*FX* — one line per effect: its name, its mix as a level bar and a lamp for on,
the open effect marked with an amber edge. Under the list, the shape that
effect gives a note, drawn from its own first two parameters: a wobble for the
chorus and the ensemble, echoes that decay for the delay, a long ringing tail
for the shimmer. Under the curve, the effect's four parameters, two to a line:
the pair the strip is on in white, the other two dim, so the card and the knobs
agree on where the next turn goes. Only the delay's SYNC reads as a word rather
than a number, because it is a division of the beat. Under the card, the band:
the rack the effects live in.

## The files

The panel is one translation unit: `app/orch_ui.c` included from `app/`, so
`-Iapp` is enough to build it and no file runs to a thousand lines.

| file | what it holds |
| --- | --- |
| `app/orch_ui.c` | the palette, the panel's state, the names the engine gives, the include list |
| `app/orch_ui_font.inc` | the 5x7 font and `text()` |
| `app/orch_ui_path.inc` | paths, the span rasterizer, strokes |
| `app/orch_ui_draw.inc` | `mix565`, `isqrt`, discs, pens, arcs, curves |
| `app/orch_ui_view.inc` | the string helpers, `kv`, `card`, `bar`, `text_center` |
| `app/orch_ui_strip.inc` | the four knobs, their value arcs, the strip and its per-cell patch |
| `app/orch_ui_photo.inc` | the hall as a photograph: the palette, and the runs of each frame |
| `app/orch_ui_picture.inc` | `struct panel_picture`, `picture()`, `picture_rows()`: runs to fills |
| `app/orch_ui_pic_arp.inc` `_seq` `_glo` `_fx` | the four menus' bands, one picture each |
| `app/orch_ui_pictures.inc` | which band belongs to which page, and where it hangs |
| `app/orch_ui_stage.inc` | the STAGE page: the photograph, the lamps, the players, the light over the boards |
| `app/orch_level.inc` | the light's level: the orchestra's scale (24 at eight voices) and the output's peak inside it |
| `app/orch_ui_pages.inc` | ARP and SEQUENCE |
| `app/orch_ui_fx.inc` | CONSOLE and FX |
| `app/orch_ui_frame.inc` | the title bar, the body, the three repaint signatures, the live repaint |
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
on and the delay row selected, the CONSOLE page, and last the STAGE page again
with a full orchestra of eight voices, taken at the moment the level is 24 of
24 — that frame is what the light looks like when it is as wide as it goes
(`/tmp/p-orchestra.ppm`).

`tests/run_panel_bench.sh` runs the same five pages with the fill and text-blit
calls counted, and prints them per page, plus what one knob turn and its fade
cost:

```sh
FM1_CORE=../fm1-core sh tests/run_panel_bench.sh /tmp/bench
```

The PNGs are not committed: they are output of that command and go stale the
moment the screen changes.
