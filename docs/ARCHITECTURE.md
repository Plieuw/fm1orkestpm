# FM1 Orchestral Engine — architecture 0.2.0

## Design target

Polyphony remains the first priority. External MIDI channels share one global note allocator, so a channel does not permanently reserve voices. The sequencer is a separate source and also uses that pool. The current ceiling remains 12 voices until target-device profiling proves a higher count safe.

## Audio path and constraints

MIDI -> bounded event queue -> global allocator -> fixed-point digital waveguide -> stereo Q23 output. Rendering uses fixed-size state and no heap allocation, blocking calls, display work or file access. The FM1 Core callback is 128 frames at approximately 44.1 kHz.

## Implemented in this beta

- Channel-aware Note On/Off and CC64 sustain.
- Twelve provisional shared waveguide voices.
- Velocity-sensitive, softened envelope onset and instrument-family release shaping.
- Context-sensitive legato glide over a short transition; detached notes do not glide.
- Gesture-derived vibrato modulation and phrase/density-derived tremolo depth. These are preliminary, low-cost approximations and need listening calibration.
- Per-channel adaptive phrase memory: inter-onset estimate, phrase density proxy, note count and velocity trend.
- Instrument-family selection API for violin, viola, cello, bass, woodwind, brass, choir and percussion.
- Divisi planning API with up to six section players, stable chord-part mapping, and extra doublers for sparse phrases.
- 16-step internal sequencer, up to four notes per step, internal tempo and gate percentages.

## Divisi boundary

The six-player Divisi planner is implemented as a deterministic planning layer, not yet as six independent rendered physical player models. The plan returns active players, chord-part assignment and doubling offsets. Rendering those players as independent waveguides would multiply DSP cost and reduce available MIDI-note polyphony. It must therefore be integrated behind a measured global CPU budget rather than silently consuming several slots for every note. This keeps the polyphonic note engine separate from ensemble orchestration.

The orch_set_divisi() switch enables/disables sparse-phrase doubling in the planner. The current MIDI front end does not yet expose a UI or controller mapping for instrument family or Divisi mode.

## Adaptive Playing boundary

Adaptive values are calculated on note events, not through expensive per-sample analysis. The current implementation provides first-pass phrase-density estimates, softer attack/release, a short legato glide, and low-cost modulation. It is not yet the full Himalia interpreter: phrase peak detection, articulation classification, bow pressure/speed, independent player personalities, true nonlinear bow/friction interaction, body/bridge coupling, and adaptive player reassignment remain future work.

## Voice allocation and memory

Each voice owns a 2048-sample delay buffer, damping filter, envelope, glide state and modulation phase. All channels share this pool. Voice stealing prefers the quietest releasing voice, then the oldest held voice. The 12-voice ceiling is provisional and should not be raised before timing and memory measurements on the FM1 target.

## Sequencer and MIDI gaps

External MIDI clock/transport, pitch bend, channel pressure, expression, program changes, per-channel instrument presets, pattern length, swing, ratchets and panel editing remain outstanding. The current 16-step sequencer runs from internal tempo.

## Validation status

The host workflow checks C compilation with warnings treated as errors. That is not a JieLi target build, firmware package, or hardware test. Before a flashable beta is claimed:
1. Add host tests for allocator, note lifecycle, adaptive timing and Divisi mapping.
2. Build/package using the matching FM1 Core/JieLi PI32v2 toolchain.
3. Measure 1/4/8/12-note loads on device, including sequencer and modulation.
4. Verify pitch tracking, tuning, output level, transient quality and block deadline headroom.
5. Increase the voice ceiling only after measured margin is available.

## Reference

The Divisi/adaptive design rationale is in [DIVISI_ADAPTIVE_ARCHITECTURE.md](DIVISI_ADAPTIVE_ARCHITECTURE.md). The source architecture is inspired by the user's Himalia strings-engine exploration; this FM1 implementation is separately adapted for the FM1 target and remains GPL-3.0-or-later.
