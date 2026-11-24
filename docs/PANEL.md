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

## What each control does

| Control | Job |
| --- | --- |
| `SELECT` | choose the part (1..4, wraps); the LED of the part lights |
| `ALGORITHM` | family on STAGE; mode on ARP; step on SEQUENCE; scale on CONSOLE; effect on FX |
| `PRESETS` | next/previous instrument inside the current family, wrapping |
| `KNOB1` | divisi players 1..4 / arp rate / step velocity / FX mix |
| `KNOB2` | voice ceiling / octaves / step gate / semitone transpose / FX parameter A |
| `KNOB3` | listener across the stage / arp gate / step tempo / tempo on CONSOLE and SEQUENCE / FX parameter B |
| `KNOB4` | listener depth (how far back) / arp mode / clear step / FX on-off / **panic** on CONSOLE |
| `OCT-` `OCT+` | transpose the part an octave down or up (clamped to ±24) |
| `FX` | open the FX page (chorus, ensemble, delay, shimmer reverb). Pressed again on that page it is the old sound check: a four-part chord, released after 900 ms |
| `SCL` | cycle the note map: OFF, MAJOR, MINOR, PENTA |
| `ENV` `LFO` | articulation step down / up |
| `EDIT` `HOME` | back to the STAGE page |
| `GLO` | CONSOLE page |
| `SAVE` | write the settings record to flash; the LED shows it was done |
| `ARP` | open the ARP page and toggle this part's arpeggiator |
| `SEQ` | open the SEQUENCE page |
| `PLAY` | start/stop the sequencer on the selected part |
| `REC` | arm the keys for step recording (SEQUENCE page only) |
| panel knob | master level, quadratic, 0..unity; independent of the page |

The master gain (`orch_ui_master_q23`) and one byte per FX setting are the only
words the control task shares with the audio interrupt. The panel never starts
a voice directly; it asks the engine, and the engine starts it in the render
context.

The panel is laid out as HAWK CHUA's is: the four knob cells along the bottom
are the page's menu — a small knob, its label under it and the value under that,
the value lighting for a second after its knob moves — so no page needs help
lines that have to be read. That layout, and the speed of the encoders, are in
[`PANEL-REFERENCE.md`](PANEL-REFERENCE.md#knobs).
