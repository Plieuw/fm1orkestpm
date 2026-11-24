# The engine in a browser

The whole instrument — engine, FX, panel, save record — runs in a page, so it
can be played and looked at without an FM-1. This is the same idea as HAWK
CHUA's browser emulator (`Plieuw/hawk-chua`, `web/emu`, newest on `main`:
649102d, 17 November 2026, which also carries the `vibe/` build for the VIBE FM
VST and Playground): the firmware's own sources compile to WebAssembly and the
board is replaced by a handful of buffers. Nothing of the panel is
reimplemented, so the page shows what the board would show.

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
committed (`.gitignore`).

## What the page does

- the panel on a canvas, redrawn every frame from the framebuffer;
- the panel's 14 buttons under the mouse, the four knobs and the volume pot as
  sliders (one slider step is one detent), the keys on `a w s e d f t g y h u j k`;
- the engine is rendered whether or not there is an audio device, so a headless
  browser still shows live voices; with audio on, the same blocks go to an
  `AudioWorklet` (`orch-worklet.js`) that plays the queue.

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

The page was checked in a cloud browser on 24 November 2026: the wasm loaded,
the STAGE page with the hall drew, `audio on · 48000 Hz · running` appeared, and
three held keys gave three voices on part 1. No console errors other than a
missing `favicon.ico`.
