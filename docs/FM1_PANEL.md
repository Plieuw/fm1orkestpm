# FM1 panel and input map — 0.3.4

This adapter connects the existing orchestral engine to the FM1 Core hooks.
It uses only documented FM1 Core interfaces from `fm1.h`, `fm1_app.h` and
`fm1_engine_control.h`.

## Runtime split

- `fm1_app_render()` is the audio-interrupt entry point. It validates the
  fixed 128-frame block and calls `orch_render_q23()`. It does not read panel
  controls or draw pixels.
- `fm1_app_midi_received()` forwards complete MIDI 1.0 short messages to the
  engine's bounded event queue.
- `groove_screen_init()` initializes the panel and draws the first screen.
- `groove_screen_control()` polls panel state at 100 Hz, translates key
  edges into note events, and redraws only when the UI state changes.
- Instrument, articulation and player-cap edits use the FM1 Core
  `fm1_engine_pause()/resume()` control-task transaction so the audio
  renderer does not observe a partially edited part.

## Physical controls

| Control | Action |
|---|---|
| 27 note keys (F3–G5) | Play/release notes on the selected part |
| Knob | Sets local keyboard velocity (48–127) |
| Encoder 1 | Previous/next instrument (all 18 catalogue entries) |
| Encoder 2 | Divisi player cap (1–4) |
| Encoder 3 | Articulation: Auto, Legato, Detache, Staccato, Pizzicato |
| Encoder 4 | Select part 1–4 |
| Buttons 1–4 | Select part 1–4 |
| Button 5 | Panic |
| Button 6 | Advance articulation |
| Button 7 | Cycle Divisi cap |

Local keyboard channels are the first channel of each default group:
part 1 = MIDI channel 1, part 2 = 5, part 3 = 9, part 4 = 13.
External MIDI continues to use the configured channel groups 1–4, 5–8,
9–12 and 13–16.

## Display

The 240×240 RGB565 display shows selected part/channel group, instrument,
articulation and Divisi cap. The UI uses the documented rectangle-fill
primitive and a small built-in 3×5 glyph set; no undocumented font or display
symbol is assumed. Drawing is performed only from the control task.

## Validation status and limitations

The source is checked by the repository's host compiler workflow against the
FM1 Core headers. This is not a JieLi PI32v2 target build and does not prove
that the fixed runtime invokes every weak hook in the expected order on the
device. Confirm the hook call order in the exact FM1 Core revision used for
the build, then test keys, MIDI, screen refresh and audio on hardware.

The FM1 Core API exposes no named button labels in `fm1_controls`; this
mapping intentionally uses stable numeric button indices 0–6. Verify the
physical button numbering against the actual FM1 panel before relying on it.
