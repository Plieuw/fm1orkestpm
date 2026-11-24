# FM-1 panel

The panel map below follows the labels **printed on the FM-1 itself** and the
matrix numbering the board uses, both taken from the M-VAVE reference code
(`Plieuw/hawk-chua`, `open/app/hc_panel.h` and `firmware/src/panel.c`). Where a
button has no obvious job, the job chosen here is written down so nothing is
left dead.

```
buttons   0 OCT-   1 OCT+   2 FX     3 SCL    4 ENV    5 LFO    6 EDIT
          7 GLO    8 HOME   9 SAVE  10 ARP   11 SEQ   12 PLAY  13 REC
encoders  0 SELECT  1 ALGORITHM  2 KNOB1  3 KNOB2  4 KNOB3  5 KNOB4  6 PRESETS
```

## What each control does

| Control | Job |
| --- | --- |
| `SELECT` | choose the part (1..4, wraps); the LED of the part lights |
| `ALGORITHM` | family on PARTS; mode on ARP; step on SEQUENCER; scale on GLOBAL; effect on FX |
| `PRESETS` | next/previous instrument inside the current family, wrapping |
| `KNOB1` | divisi players 1..4 / arp rate / step velocity / FX mix |
| `KNOB2` | voice ceiling / octaves / step gate / semitone transpose / FX parameter A |
| `KNOB3` | listener across the stage / arp gate / step tempo / tempo on GLOBAL and SEQUENCER / FX parameter B |
| `KNOB4` | listener depth (how far back) / arp mode / clear step / FX on-off / **panic** on GLOBAL |
| `OCT-` `OCT+` | transpose the part an octave down or up (clamped to ±24) |
| `FX` | open the FX page (chorus, ensemble, delay, shimmer reverb). Pressed again on that page it is the old sound check: a four-part chord, released after 900 ms |
| `SCL` | cycle the note map: OFF, MAJOR, MINOR, PENTA |
| `ENV` `LFO` | articulation step down / up |
| `EDIT` `HOME` | back to the PARTS page |
| `GLO` | GLOBAL page |
| `SAVE` | write the settings record to flash; the LED shows it was done |
| `ARP` | open the ARP page and toggle this part's arpeggiator |
| `SEQ` | open the SEQUENCER page |
| `PLAY` | start/stop the sequencer on the selected part |
| `REC` | arm the keys for step recording (SEQUENCER page only) |
| panel knob | master level, quadratic, 0..unity; independent of the page |

The master gain (`orch_ui_master_q23`) and one byte per FX setting are the only
words the control task shares with the audio interrupt. The panel never starts
a voice directly; it asks the engine, and the engine starts it in the render
context.

## Knobs

The four knob cells at the bottom of the screen are the page's menu: each cell
is a small knob with its label under it and the value under that, and the value
lights for a second after its knob moves, so the eye finds the one that changed.
The cells change with the page: players, voices, listener and depth on PARTS;
rate, octaves, gate and mode on ARP; the selected step's velocity, gate and
tempo plus clear on SEQUENCER; scale, transpose, tempo and panic on GLOBAL; and
the selected effect's mix, its two parameters and its power on FX. This is HAWK
CHUA's layout (`firmware/src/ui.c`, `ui_cell`): the knob, its name and its value
in one cell, instead of help lines that have to be read.

Turning behaves as it does in HAWK CHUA (`knob_accel` in the same file):

* a fast turn goes faster — the detents a second in the same direction give
  x1 (under 12 a second), x2, x4, then x8 (from 40 a second), so a range
  crosses in one flick;
* slowing down brings the factor back by halves, never in a jump;
* one detent is still one step: a menu entry (part, family, instrument, arp
  mode, effect, scale, step) moves exactly one place a detent, so a quick turn
  cannot skip an entry;
* turning the other way starts again at x1;
* the percentages — the FX mix and its two parameters, the arp gate, the step's
  velocity and gate — keep the 5 % a detent the panel always had, so slow
  turning is as fine as it was, and a fast turn only doubles that step: a range
  never jumps the values the fine step exists for. The tempo and the transpose
  do take the full x8.

Two calls in the same millisecond are taken unscaled: a hand cannot do that,
and it keeps the host tests deterministic. The board's knob (the volume pot) is
not an encoder and is not accelerated.

The encoder numbering above is the one measured on an FM-1 by HAWK CHUA's
`panel.c`; the SDK's `fm1_controls.encoders` uses the same matrix order, so
PRESETS is encoder 6 and KNOB1..4 are 2..5.

## Pages

There are five pages: **PARTS**, **ARP**, **SEQUENCER**, **GLOBAL**, **FX**.

*PARTS* — one row per part: family, instrument and articulation, the player
count, and a live meter of the voices it is using. Under the rows is the stage
picture: a bar with a square for each part at its section's place and a yellow
mark for the listener's seat, plus the shortcuts. `KNOB3` moves the listener
across the stage and `KNOB4` moves it back.

*ARP* — mode, octaves, rate, gate and the number of held keys, plus what the
encoders do on this page.

*SEQUENCER* — sixteen steps. The selected step is highlighted, a step that holds
notes has an orange mark, and the step that is playing has a green bar under it
while the pattern runs. Below the grid: the step number, its notes, velocity and
gate.

*GLOBAL* — tempo, transpose, scale, volume and the save state, with the shortcuts
of the buttons that are not page specific.

*FX* — one row per effect: name, on/off, mix and its two parameters. The
selected row is highlighted. `ALGORITHM` moves the selection, `KNOB1` is the
mix, `KNOB2` and `KNOB3` are the two parameters of the selected effect, and
`KNOB4` switches it on or off. The parameter names change with the effect
(rate/depth for the chorus, rate/width for the ensemble, time/feedback for the
delay, decay/shimmer for the reverb).

To look at a page without a board, render it on the host:

```sh
cc -std=c11 -O2 -DFM1_HOST_TEST -I../fm1-core/include -Iapp \
   tests/panel_preview.c app/orch_ui.c app/orch_engine.c app/orch_fx.c \
   app/orch_adaptive.c app/orch_divisi.c app/orch_part.c app/orch_stage.c \
   -o /tmp/panel_preview -lm
/tmp/panel_preview /tmp/p
python3 tools/panel_png.py /tmp/p-parts.ppm /tmp/p-parts.png
python3 tools/panel_png.py /tmp/p-arp.ppm   /tmp/p-arp.png
python3 tools/panel_png.py /tmp/p-seq.ppm   /tmp/p-seq.png
python3 tools/panel_png.py /tmp/p-fx.ppm    /tmp/p-fx.png
```

`panel_preview` writes four pages as PPM: the PARTS page with an orchestra on
the stage and a four-part chord sounding, the ARP page with three held keys,
the SEQUENCER page with three recorded steps and the playing step marked, and
the FX page with all four effects on and the delay row selected. The PNGs are
not committed, because they are output of that command and would go stale.

## Stage and listener

The pan of a voice is not a fixed pattern for every part: it follows the seat
of the instrument's section, seen from the audience.

| Section | Instruments | Pan |
| --- | --- | --- |
| strings, left | violins, harp | 40, 56 |
| strings, middle | violas | 100 |
| strings, right | cellos, double basses | 180, 216 |
| woodwinds | clarinet, flute, piccolo, oboe, bassoon | 92..158 |
| brass | French horn, trumpet, trombone, tuba | 168..232 |
| percussion | marimba, xylophone, timpani | 176..240 |
| choir | choir | 128 (back centre) |

The players of one part spread evenly over the width of their own section
(`orch_divisi.c`): one player stands in the middle, two at ±48, three at
-48/0/+48 and four at -48/-16/+16/+48, relative to the section's centre. The
seats are in `app/orch_stage.c`, and the seat of a part comes from its
instrument, so the sequencer, the arpeggiator and MIDI all put a note in the
same place.

The listener is the one control over that image (`KNOB3` and `KNOB4` on the
PARTS page, and in the save record):

* **left/right** (-100..100) shifts the whole orchestra; 5 a detent, at most 10
  when flicked;
* **depth** (0..100) moves the seat back, which narrows the stage to 40 % of its
  width, and forward again to open it up. Depth changes no level: it moves the
  image, it is not a room simulation, so a part that was audible stays audible.

Both are clamped at the edges. Moving either redraws the stage picture and the
two cells under the knobs, so the seat is visible while it moves. The listener
block is appended after the FX block with its own checksum, so a record written
before it existed still loads its parts and its FX.

Not measured: that the seating sounds like an orchestra. The pans are numbers
in the render; the host test checks their order and the listener's clamping,
not the sound.

## Arpeggiator

Per part, in the engine (`app/orch_engine.c`), not in the panel: the held-key
list and the clock have to live in the same context as the voices, because the
control task can interrupt the render half way through a note.

* five modes: up, down, up-down, random (deterministic), as played;
* one to three octaves;
* five rates: 1/8, 1/8 triplet, 1/16, 1/16 triplet, 1/32 of the shared tempo;
* gate 5..100 % of the step;
* up to eight held keys per part.

Turning the arp on for a part collects its keys instead of sounding them; the
first key sounds at once, and letting go of the last key stops the pattern.
`orch_panic()` clears the held lists as well.

## Sequencer

Sixteen steps, four notes per step, per-note velocity and gate, one part at a
time (`orch_seq_set_part`). Notes go through the same part path as MIDI, so the
divisi planner and the adaptive interpreter see them like any other note.

Recording: `SEQ`, then `REC`, then play the keys. Each key press writes a note
into the current step and moves to the next step, so a phrase is entered left to
right. `KNOB1` and `KNOB2` change the velocity and the gate of the selected
step's first note, `KNOB4` clears the step, `KNOB3` is the tempo.

## Effects

The FX page drives a master effect chain that runs in the audio callback after
the voice mix (`fm1_app_render()` calls `orch_fx_process()`; the DSP is
`app/orch_fx.c`, one stereo pass):

```
chorus -> ensemble -> echo -> shimmer reverb
```

All four are fixed point, allocation-free and off by default. They are a master
insert, so **one setting applies to all four parts**; there are no per-part
sends. Each effect has a dry/wet mix and two parameters, all in percent:

| Effect | Parameter A | Parameter B | What it does |
| --- | --- | --- | --- |
| CHORUS | RATE 0.15..5 Hz | DEPTH 2..8 ms | one modulated tap per channel, opposite LFO phase |
| ENSEMBLE | RATE 0.09..1.8 Hz | WIDTH 1..10 ms | three taps at 18/23/29 ms with their own LFO phases |
| DELAY | TIME 60..260 ms | FEEDBACK 0..90 % | ping-pong stereo echo, time glides instead of clicking |
| SHIMMER | DECAY 0.70..0.92 | SHIM 0..70 % | four damped combs and two allpasses per channel, with an octave-up grain shifter in the feedback path |

The delay and reverb tails are flushed by the panic move (`KNOB4` on GLOBAL)
and by `orch_panic()`, so a stuck tail cannot outlive a panic. The reverb is an
insert, not a send: at 100 % mix only the reverb is heard.

## Note map

`orch_ui_map_note()` adds the transpose and then snaps the note **down** to the
next note of the scale. Snapping down (and not to the nearest note) is a
deliberate choice: it never raises a note the player did not play. In MAJOR, C#
and D# become C and D; in MINOR, D# stays D# because it is the minor third. Both
keys and MIDI pass through this function (`groove_midi_note` in
`app/fm1_app.c`), so transposition behaves the same wherever the note came from.

## Save

`SAVE` writes one record at offset 0 of the first flash sector: a magic
(`0x4F524348`), the tempo, transpose, scale, sequencer state and selected part,
and for each part the family, instrument, articulation, players, voice ceiling
and the five arp settings, followed by an FNV-1a checksum. On start-up the panel
reads it back and applies it only when the magic and the checksum both match, so
a blank or half-written sector is ignored rather than loaded.

The FX block (on/off, mix, both parameters per effect) is appended **after** the
original checksum with its own FNV-1a checksum, and the listener's seat
(left/right and depth) is appended after that with a checksum of its own. That
keeps the old record layout intact: a record written before the FX or the
listener existed still loads its parts, and only the block whose checksum
cannot match erased flash is skipped.

The sequencer's note pattern is **not** in the record yet: the panel mirrors what
it wrote, and a power cycle leaves the pattern empty. That is a known gap, not a
bug that is hidden.

## What is tested, and what is not

`tests/orch_ui_test.c` drives the panel with stubbed board services and checks
every button, every encoder role on every page, the note map, the flash save and
reload (including the FX and listener blocks) and that a recorded step sounds
after `PLAY`. It also checks the knob speed: one detent alone, x2 at 20 a
second, x8 at 40, back by halves when slowing and one detent after turning back,
and that a percentage reaches only x2 (the delay mix going 5 then 10 a detent).
`tests/orch_stage_test.c` checks the seating order, the section spread and that
the listener clamps at both ends. `tests/orch_arp_test.c` covers the arp.
`tests/orch_fx_test.c` covers the effect chain: bypass is bit exact, the echo
arrives at the set time and its feedback decays, the reverb tail decays,
shimmer changes that tail, hard driving stays inside Q23, and panic empties the
buffers. `tests/panel_preview.c` renders the four pages above.

Not tested, because there is no hardware in this environment:

* that the ST7789 shows what is drawn (colour order, orientation, timing);
* the real button and encoder scan (the SDK's `fm1_controls_read`);
* flash wear and a real power cycle after `SAVE`;
* that the audio output sounds like an orchestra, or how the effects sound —
  the chain is checked on the host, not heard;
* the FX cost in the audio interrupt (CPU and about 116 KB of int16 buffers).
  Every effect starts off, so the default path is unchanged; the measured
  caveats are in `docs/BRAINSTORM.md`.

The firmware image itself cannot be built here either: the JieLi PI32v2
toolchain is not available, so `build` stops with
`cannot find the JieLi PI32v2 toolchain`. Everything above is host-side code
plus host tests.
