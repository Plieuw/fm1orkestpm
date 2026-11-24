# Orchestral voice control: ADSR, VCA and LFO

This is the implementation contract for the next Orchestral Engine firmware
revision. It preserves the existing FM1 control workflow and does not add a
new panel page or remap a physical control.

## Firmware and control compatibility

- Keep the HAWK-CHUA-style PRESETS encoder on matrix encoder 6: one detent
  selects and immediately recalls a complete factory scene.
- Keep the existing four performance encoders, buttons, page cycle and MIDI
  channel mapping.
- Keep the M-Flasher-compatible firmware packaging/install path. Do not fork
  or replace M-Flasher.
- Add synthesis controls through the existing preset model and existing
  control assignments only; no surprise control remapping.

## Current engine facts

The orchestral voice now has a four-stage per-voice ADSR contour: attack,
decay, sustain and release. Note-off enters release from the current level;
release duration is derived from that level. Pluck/strike voices complete
their attack and decay before automatically entering release, preserving a
natural modeled tail. Instrument-specific defaults are stored per part, and
`orch_engine_set_adsr()` allows scene/preset code to override attack, decay,
sustain and release independently. Adaptive intent remains a bounded
modifier for attack/release timing.

A general-purpose per-part LFO is now implemented alongside the existing
expressive vibrato. It offers sine, triangle, saw, square and sample-and-hold
shapes; free rates (0.1–20 Hz) or tempo divisions (quarter, eighth, sixteenth,
dotted eighth); delay and fade-in; and independent pitch, tremolo and
brightness destinations. Each voice has its own phase. Pitch depth is capped
at 50 cents, tremolo at a musically bounded attenuation, and brightness is
limited to a small damping shift. The existing adaptive vibrato remains
active and has not been replaced.

The amplitude envelope currently multiplies the physical-model output in the
voice mixer. That is already a VCA-like gain stage, but it is not exposed as
a separately configurable VCA module.

## Target voice signal path

`physical instrument model → per-voice VCA → pan/ensemble mix → master FX`

The VCA is a normal linear amplitude stage controlled by the envelope and
velocity/dynamics. It replaces any LPG-style coupling: no low-pass response
is tied to the amplitude control. Instrument damping remains part of the
physical model and articulation, independently of the VCA.

## ADSR design

Each part receives a default ADSR profile, with per-instrument defaults:
- Attack: shaped onset; bowed strings and winds can swell, brass can speak
  more promptly, plucked instruments retain a short excitation transient.
- Decay: transition from peak to sustain.
- Sustain: held-note level.
- Release: independent tail after note-off.

Values are stored in milliseconds/percent in control-task configuration and
converted to fixed-point per-sample increments outside the render hot path.
The render path remains allocation-free and fixed-point. Note-off starts the
release from the current envelope level, not from a hard-coded maximum.
Plucks and struck instruments remain naturally decaying even when the MIDI
gate closes; the ADSR/VCA must not truncate their modeled resonances abruptly.

Adaptive playing may scale attack/release intent within bounded limits, but
must not overwrite the user's preset values. The preset is the base; adaptive
performance is a bounded modifier.

## General-purpose LFO

One phase-accumulator LFO per part (or a shared phase source with per-part
depth, if memory/CPU measurements require it) with:
- Shapes: sine, triangle, rising/falling saw, square, sample-and-hold.
- Rate: free-running Hz and tempo divisions when external/internal sync is
  selected.
- Fade-in/delay and depth.
- Destinations: vibrato/pitch, tremolo/VCA, model damping/brightness and
  slow expression. Destination depth is independently adjustable.
- Adaptive mode can gently vary rate/depth from phrase activity and intent;
  it must remain musically bounded and deterministic.

Existing vibrato behavior should be migrated into this LFO system only after
A/B tests demonstrate equivalent or better natural onset and tuning stability.
Do not abruptly remove the current per-voice vibrato path before migration.

## Factory preset coverage

Every factory scene should define:
- Four part ADSR profiles (instrument-appropriate).
- LFO shape/rate/sync/depth/delay and destination amounts.
- VCA response/velocity curve.
- Existing instrument, articulation, divisi, ARP, step motif, call/response
  and FX configuration.

Scene intent:
- Cinema Strings: slow attack, long release, gentle delayed vibrato, subtle
  expression movement.
- Baroque: moderate attack, short controlled release, light pitch modulation.
- Wind Choir: breath-shaped attack/release and slow expression/tremolo.
- Brass Fan: faster attack, firm sustain, restrained pitch movement.
- Pizz Motion: percussive excitation, short VCA contour; preserve natural
  model ring after excitation.
- Ambient: slow envelopes, long release, slow evolving modulation.
- Ostinato: quick controlled envelopes, tempo-related tremolo/groove.
- Full Orchestra: balanced instrument-specific defaults, subtle modulation.

## Acceptance tests

1. ADSR stage transitions and note-off from mid-attack/decay/sustain.
2. No clicks when changing preset or releasing a held note.
3. Pluck/strike tails ring naturally after gate close.
4. VCA does not alter model brightness or damping.
5. LFO waveforms/rates and tempo sync verified numerically.
6. CPU and RAM budget measured on target FM1; no floating point or dynamic
   allocation in the audio ISR.
7. Existing panel controls, PRESETS encoder, MIDI routing, sequencer and
   M-Flasher package path remain unchanged.
8. Test both known flash variants and preserve boot-area safety checks.

The ADSR voice contour and per-part configuration API are implemented in the
current feature branch. The eight factory scenes assign per-section ADSR and
LFO values during preset recall. The LFO is additive to the legacy vibrato.
Separate VCA response/velocity curves, host regression tests for modulation,
and FM1 hardware verification remain future work.
