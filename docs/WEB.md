# The engine in a browser

The whole instrument — engine, FX, panel, save record — runs in a page, so it
can be played and looked at without an FM-1. This is the same idea as HAWK
CHUA's browser emulator (`Plieuw/hawk-chua`, `web/emu`, newest on `main`:
649102d, 17 November 2026, which also carries the `vibe/` build for the VIBE FM
VST and Playground): the firmware's own sources compile to WebAssembly and the
board is replaced by a handful of buffers. Nothing of the panel is
reimplemented, so the page shows what the board would show.

The page takes that emulator's interface, too: the same flat case, the screen on
the left with the seven encoders beside it, the fourteen buttons in one row and
a piano keyboard under them, and the same "start sound" overlay, because a
browser only opens an audio device after a click. What is ours is what the
instrument is: four desks under the keyboard, one per part, lit by the voices
each section is sounding, so the panel's STAGE page can be read from the page as
well as from the screen.

## What replaces what

`web/emu/emu.c` implements the FM-1 Core API that `app/fm1_app.c` calls:

| on the board | in the browser |
| --- | --- |
| ST7789 LCD | a 240 × 240 RGB565 framebuffer (`emu_fb`) |
| 14 buttons, 27 keys, 7 encoders, the pot | `emu_button`, `emu_key`, `emu_turn`, `emu_knob` |
| MIDI in | `emu_note`, which goes through `groove_midi_note` like the keys do |
| the audio codec | blocks of `FM1_BLOCK_FRAMES` stereo frames, Q23 in int32 (`emu_render`, `emu_audio`) |
| the flash chip | 4 KiB of settings in the module, so `SAVE` works and reloads |

`fm1_app.c` is the same adapter the firmware uses — audio, MIDI and a call into
`orch_ui_*` — so the browser also compiles a file the host tests never touch.

## Build and run

A wasm compiler is needed: clang with `wasm-ld`, or `pip install
ziglang==0.13.0` (Zig ships the same clang front end and lld). No SDK, no JieLi
toolchain and no board are involved.

```sh
FM1_CORE=../fm1-core sh web/emu/build.sh     # -> build/emu/orch.wasm (~229 KiB)
node web/emu/smoke.mjs                       # headless check, see below
python3 -m http.server 3000                  # from the repository root
# then open http://localhost:3000/web/emu/index.html
```

The page needs `build/emu/orch.wasm`, which is build output and is not
committed (`.gitignore`). Three files make the page: `index.html` for the
markup, `hardware.css` for the look and `ui.js` for the wiring.

## The page

`index.html` is a case, not a photograph of one. The screen is the firmware's
240 × 240 framebuffer on a black bezel, and everything around it is a control
that goes to the same place the board's control goes: the buttons to
`emu_button` (held, not clicked — the panel reads the mask), the seven encoders
to `emu_turn`, the big knob to `emu_knob` (0–1023), the keys to `emu_key` and
`emu_note`.

- an encoder turns by dragging it up or down (12 px per detent), by scrolling
  over it, or with the arrow keys when it has focus;
- the keys are on `a w s e d f t g y h u j k o l p ; '` (F3 upward, as on the
  board) and on the drawn keyboard;
- the engine is rendered whether or not there is an audio device, so a headless
  browser still shows a live panel and live desks; with audio on, the same
  blocks go to an `AudioWorklet` (`orch-worklet.js`) that plays the queue;
- the four desks are `emu_voices(part)` per part: the name, a meter of four
  voices, and "resting" when the section is silent. They are the one thing on
  the page the board does not draw.

The hall drawing sits behind it all at 16 % opacity, blurred, so the page has
the room in it without pretending to be a photograph of an instrument.

## The artwork

One drawing, made for this project and under the project's own licence — there
is no photograph and no third-party image on the page. `assets/hall.svg` is the
Musikverein's Great Hall: gilded coffered ceiling, an arcade of lit windows,
panelled dado, the organ's pipes on the stage, rows of red seats with aisles and
brass rails, chandeliers and a warm spotlight.

It is built for how the page shows it: `hardware.css` blurs the hall and dims it
behind the case, and the case covers the middle of the picture, so the drawing
is large shapes and warm colour rather than detail. Open it on its own in a
browser to see it whole. The history of the photographs that came before it,
and how their licences were checked, is in `assets/CREDITS.md`.

## What the headless check proves

`node web/emu/smoke.mjs` loads the module with no browser and checks three
things, each of which has failed at least once during development:

```
lit pixels      19408 ok     the panel drew
peak audio      199368 ok    a note sounds (Q23 peak, after one block)
flash magic     0x4f524348 ok SAVE wrote the record
```

Two bugs it caught, worth knowing if the page is changed: the pot has to be set
**after** `orch_ui_init()` (the panel's own init clears the controls, and a
zero pot means a zero master gain), and the engine must not depend on the
worklet reporting played frames, or nothing renders on a machine without an
audio device.

## What is not proven

Heard on real hardware: no. The browser renders the same fixed-point code, but
a browser's audio stack is not the FM-1's codec, and nothing here says anything
about audio-block time on the JieLi. That still needs the board.

The page was checked in a cloud browser on 24 November 2026, after the interface
was rebuilt: the wasm loaded, the panel and the desks drew, `audio on · 48000 Hz
· running` appeared, held keys lit the keyboard and gave three voices on P1, and
there were no console errors. The layout was looked at at 1100 px wide; the
narrow layout (seven buttons per row, two desks per row) is written but was not
looked at after the rebuild, and nothing was tried on a phone or in Safari.
