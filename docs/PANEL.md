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

Two more documents go with this one: [`SCREEN.md`](SCREEN.md) is the screen
itself — palette, the shape of a row, the five pages and the files they are
drawn from — and [`PANEL-REFERENCE.md`](PANEL-REFERENCE.md) is how the panel
behaves: the knob speed, the stage and the listener, the arpeggiator, the
sequencer, the effects, the note map, the save record and what is tested.

## What each control does (0.5.0)

Seven pages: **STAGE**, **ARP**, **SEQUENCE**, **CONSOLE**, **FX**,
**ENVELOPE** and **LFO**. Eight parts, one MIDI channel each (1..8).

| Control | Job |
| --- | --- |
| `SELECT` | choose the part (1..8, wraps) |
| `ALGORITHM` | family on STAGE, ENVELOPE and LFO; stream logic (OR, AND, XOR, NOR, ALT, S/R) on ARP; step on SEQUENCE; scale on CONSOLE; effect on FX |
| `PRESETS` | next/previous instrument inside the current family, wrapping; on FX it turns the strip over to the effect's other two parameters |
| `KNOB1` | divisi players 1..4 / ARP type (31) / step velocity / scale / FX mix / attack / LFO depth |
| `KNOB2` | articulation / ARP rate (10 divisions) / step gate / semitone transpose / the effect's first (or third) parameter / decay / LFO rate |
| `KNOB3` | listener across the stage / ARP gate / tempo on SEQUENCE and CONSOLE / the effect's second (or fourth) parameter / sustain / LFO wave |
| `KNOB4` | listener depth / phrase (OFF, GE 1..46) / clear step / **factory scene** on CONSOLE (recalled as it is chosen) / FX on-off / release / LFO destination |
| `OCT-` `OCT+` | transpose an octave down or up (clamped to ±24) |
| `FX` | open the FX page; pressed again there, a chord across all eight parts, released after 900 ms |
| `SCL` | cycle the note map: OFF, MAJOR, MINOR, PENTA |
| `ENV` | the ENVELOPE page: the part's ADSR, drawn as its shape |
| `LFO` | the LFO page: depth, rate, wave, destinations (pitch, tremolo, brightness) |
| `EDIT` `HOME` | back to the STAGE page |
| `GLO` | CONSOLE page |
| `SAVE` | write the settings record ("OR50") to flash; the LED shows it was done |
| `ARP` | open the ARP page; pressed on that page, this part's ARP on/off |
| `SEQ` | open the SEQUENCE page |
| `PLAY` | start/stop the sequencer on the selected part |
| `REC` | arm the keys for step recording (SEQUENCE page only) |
| panel knob | master level. On the HAWK firmware build the MASTER pot is applied after the engine and the panel's own level stays at unity |

There is no panic control (the 0.3.x line removed it on purpose). A MIDI
all-notes-off (CC 120/123) or a scene recall clears the voices.

On the FM-1 firmware, holding OCT- and OCT+ together is still HAWK's update
combination: after 2 s the countdown takes the screen, at 5 s the FM-1 enters
UBOOT, and letting go cancels it and repaints the panel. Those two presses also
transpose (down, then up again), as on every other build.

The master gain (`orch_ui_master_q23`) and one byte per FX setting are the only
words the control task shares with the audio interrupt. The panel never starts
a voice directly; it asks the engine, and the engine starts it in the render
context.

The panel is laid out as HAWK CHUA's is: the four knob cells along the bottom
are the page's menu — a small knob, its label under it and the value under that,
the value lighting for a second after its knob moves — so no page needs help
lines that have to be read. That layout, and the speed of the encoders, are in
[`PANEL-REFERENCE.md`](PANEL-REFERENCE.md#knobs).
