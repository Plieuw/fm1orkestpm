# HAWK/Felucca native runtime bridge — 0.3.8

Reference runtime: `Plieuw/hawk-chua` stable `main`, commit
`e750df6a2e13490a4f01b84d3f9c553f4612163f`.

This milestone adds a small, isolated audio/event adapter under
`runtime/hawk/`. It does **not** yet replace HAWK's synthesizer, alter
`firmware/src/felucca.c`, or produce a target firmware package.

## Verified runtime facts

- HAWK's `core.h` defines `FS=44100`, `HALF_FRAMES=128`, and `CTL=32`.
- `audio.c` calls `keys_block()` and then `synth_render(out, n)` for each
  32-frame control block.
- HAWK's synth output is interleaved signed Q15 in `int32_t` slots.
- The Orchestral Engine API `orch_render_q23()` produces interleaved signed
  Q23 and accepts an arbitrary positive frame count; its current FM1 Core
  wrapper is not a drop-in HAWK adapter.
- The HAWK panel input is the measured physical mapping in
  `firmware/src/panel.c`; it is not interchangeable with the FM1 Core panel
  API used by `app/fm1_app.c`.

## New bridge

- `runtime/hawk/orch_hawk_bridge.c`
- `runtime/hawk/orch_hawk_bridge.h`

The bridge:
1. initializes the orchestral engine;
2. renders Q23 in bounded chunks of at most 32 frames;
3. converts Q23 to HAWK's Q15 output format with saturation;
4. exposes channel-preserving note events, MIDI realtime and panic.

The scratch buffer is static (256 bytes), so the audio path allocates no heap
memory and uses no variable-size stack buffer. The bridge is intentionally
not wired into the HAWK audio ISR yet: event ownership, note routing, and the
existing `keys_block()`/ARP/KARMA ownership must be changed together to avoid
duplicate notes or leaving HAWK's old synth active.

## Required next integration

1. Add the orchestral engine source files and include paths to the HAWK
   translation unit/build manifest. Preserve the source licence notices.
2. Initialize with `orch_hawk_init()` after platform state is ready and before
   audio starts.
3. Replace the `synth_render()` call in HAWK's audio block with
   `orch_hawk_render_q15()`; keep the existing audio DMA, USB audio tap,
   watchdog, timer nesting and late-half protection.
4. Route note-on/off after HAWK's MIDI channel filtering into
   `orch_hawk_note_event(channel,note,velocity)`. Do not call the old
   `play_on()`/ `play_off()` synth path for the same event.
5. Route panic and MIDI realtime to the bridge. Decide explicitly whether
   HAWK's own ARP/KARMA is disabled or bypassed; the Orchestral Engine already
   has its own ARP/phrase sequencer, so both systems must not consume the same
   held notes accidentally.
6. Port the FM1 Orchestral panel controls to HAWK's real
   `fm1_in.buttons`, `fm1_enc_take()`, screen and LED functions. Verify every
   printed button/encoder mapping on hardware.
7. Run host tests, PI32v2 target compile/link, audio-time and RAM/stack
   measurements, package validation and emulator tests before creating a
   release package.

## Important audio-budget warning

The existing HAWK synth uses four voices and has a measured adaptive load
strategy. The Orchestral Engine has a provisional 12-voice ceiling and
waveguide delay state. The Q23-to-Q15 adapter does not establish that 12
voices fit the FM1's ~2.9 ms audio-half deadline. Start with a conservative
voice cap, measure worst-case render time and RAM on the target, then raise
the cap only from measurements. Never infer performance from host tests.

## Current state

- [x] Stable HAWK runtime API inspected.
- [x] Q23-to-Q15 bridge source added.
- [ ] Bridge wired into HAWK audio ISR and MIDI event path.
- [ ] Physical panel port verified.
- [ ] PI32v2 target build and .fwsc package.
- [ ] HAWK-style Web MIDI installer port and device test.
