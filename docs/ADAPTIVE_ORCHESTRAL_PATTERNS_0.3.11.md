# FM1 Orchestral Engine 0.3.11 — Adaptive orchestral patterns

## Goal

Make the existing ARP and 16-step sequencer feel like an orchestral arranger rather than a conventional electronic step sequencer. Patterns should respond to the player's actual phrasing, register, harmony, velocity, note density and rests. Keep the FM1's bounded real-time constraints and preserve the existing ARP/phrase engine, 31 ARP types, 46 phrase slots, MIDI clock, groove controls and 16-step polyphonic pattern API.

## Current architecture findings

- `orch_performance.c` owns the 31 ARP types, 46 phrase rhythm masks, velocity/gate grooves, phrase density/range/cluster/repeat, and MIDI clock.
- `orch_engine.c` owns the independent 16-step polyphonic pattern storage and playback.
- Generated performance notes are passed to `engine_note_on_part()`, where `orch_adaptive_note()` and `orch_adaptive_velocity()` already interpret note timing, interval, phrase position, velocity, articulation, energy, vibrato intent and divisi density.
- The adaptive interpreter currently reacts to generated notes after they have been selected. The pattern generators themselves do not yet use that interpretation to decide what comes next.
- The 16-step pattern API is currently a low-level note/gate store; it needs an optional orchestral pattern layer rather than replacing the existing API.

## Musical direction

Use a two-layer design:

1. **Pattern grammar** chooses the musical role of each event: pulse, ostinato, arpeggiated figure, answer, sustained chord, passing motion, rest, cadence or pickup.
2. **Adaptive arranger** changes the grammar's event density, register, articulation intent, note selection and phrase contour in response to recent playing.

The result should remain deterministic and repeatable for a given seed and input phrase. Avoid unconstrained random note generation.

## Adaptive input state

Maintain a small per-part snapshot, updated only on note events / control ticks (never per audio sample):

- Recent inter-onset interval average and variability.
- Recent gate/duty ratio and rest length.
- Pitch centre, range, direction and largest recent leap.
- Velocity average and accent trend.
- Chord pitch-class mask and detected root/quality when enough notes are held.
- Phrase age, phrase-start / phrase-peak / phrase-release state.
- Recent event density and whether the player is accelerating or relaxing.

Prefer reusing the existing `orch_adaptive` state and its phrase interpretation. Do not duplicate its timing history unless a specific field is missing. Keep a clear ownership boundary so ARP/SEQ and adaptive state cannot recursively drive each other.

## Adaptive behaviours

- **Tempo feel:** derive a smoothed local pulse from note-on intervals. External MIDI clock remains authoritative when enabled; adaptive feel may influence subdivision choice and humanized microtiming only within a bounded range.
- **Density:** increase activity when the player becomes more active or plays stronger accents; thin out during sparse input, long notes or rests.
- **Contour:** follow the player's melodic direction for a while, then create a restrained answer or return toward the tonal centre. Avoid repeated large leaps.
- **Harmony:** prefer held chord tones; use scale tones as passing notes; resolve tension at phrase ends. Do not change the user's root or scale setting silently.
- **Orchestration:** distribute chord tones / divisi across parts and registers; avoid every part attacking together. Use call-and-response, staggered entries and sustained support.
- **Articulation:** align note lengths with the adaptive length class; short, quick figures use crisp articulation, connected phrases use legato, and phrase starts / accents receive suitable emphasis.
- **Phrase arc:** build toward a peak, then relax; use the existing eight-phrase arc and peak detection where possible.
- **Rests:** treat silence as musical information. A long input rest should close the current phrase and allow a new pickup or entrance rather than continuing an endless loop.

## Pattern families

Provide named orchestral families, not just numbered masks:

- **Ostinato:** stable repeated pulse with subtle velocity and articulation variation.
- **Arpeggio:** chord-tone traversal with up/down/pendulum/inside-out motion.
- **Legato line:** connected melodic movement with restrained note density.
- **Call & response:** one part states a motif; another answers in a complementary register.
- **Divisi chordal:** chord tones distributed among available players / parts, with staggered attacks.
- **Tremolo / pulse bed:** sustained harmonic support with adaptive pulse rate; do not force vibrato or tremolo solely from BPM.
- **Cadence / transition:** reduce density, guide voices toward stable chord tones and prepare the next phrase.
- **Percussive / mallet ostinato:** instrument-family-aware note lengths and accents.

## Sequencer behaviour

Keep the existing 16-step storage and public API intact. Add a separate pattern-generator layer that can fill or transform a pattern:

- Each step carries a role/weight (rest, anchor, passing, accent, chord, pickup) in addition to note/velocity/gate.
- Pattern generation is seeded and bounded.
- Regeneration occurs at phrase boundaries or explicit user action, never unpredictably every step.
- Adaptive changes are smoothed and quantized to safe boundaries.
- Manual step edits take priority and are not overwritten by adaptive regeneration.

## ARP behaviour

Keep all 31 existing ARP types. Add adaptive modifiers rather than replacing the type catalogue:

- Register follows the current chord and instrument range.
- Direction may continue, reverse or answer the player's recent contour.
- Skip/repeat probability responds to density and phrase energy.
- Chord tones remain preferred; passing tones are used sparingly.
- Phrase endings favour resolution and may shorten the final figure.
- Swing and external clock retain their existing meaning and are never double-applied.

## Safety / real-time constraints

- No allocation, locks, floating point, file I/O or UI calls in the audio callback.
- No per-sample adaptive calculations; update on existing note/control events.
- Use fixed-size arrays and bounded loops.
- Use fixed-point arithmetic and avoid expensive 64-bit division in hot paths.
- Keep output note range 0..127 and cap generated simultaneous notes to the existing event/voice limits.
- Do not change FX processing or the physical panel UI in this workstream.
- Do not replace the user's ARP, phrase engine or sequencer with the parallel agent's legacy implementation.

## Implementation sequence

1. Add host tests for adaptive snapshot extraction (timing, contour, velocity, chord mask, rests).
2. Add a small `orch_pattern_adapt` module that consumes a read-only adaptive snapshot and produces bounded pattern modifiers.
3. Connect the modifiers to ARP selection and phrase selection without changing the existing public configuration structure.
4. Add optional pattern generation for the 16-step sequencer, preserving manual edits and existing API.
5. Add tests for deterministic output, phrase-boundary regeneration, harmony, rest handling, clock authority and bounded note counts.
6. Only then expose the new controls through the physical FM1 UI.

## Acceptance criteria

- Existing 31 ARP types and 46 phrase slots continue to work when adaptive mode is off.
- Adaptive mode changes musical behaviour in response to note timing, contour, velocity, harmony and rests.
- Same input and seed produce the same pattern.
- External clock is not overridden.
- Manual sequence edits are preserved.
- Host regression tests pass; PI32v2 target build, RAM/CPU measurement and package validation are required before a flashable beta is claimed.


## First implementation pass (0.3.11)

Implemented on the feature branch:

- The performance engine now tracks player-input contour separately from generated ARP notes, so the pattern does not learn recursively from its own output.
- ARP octave span responds to recent input activity.
- Random/contour ARP variants can follow ascending or descending player motion.
- Generated phrase density and register respond to activity; cluster size and peak accents respond to recent dynamics.
- The 16-step sequencer keeps the stored note, velocity and gate values intact, but gently blends its gate length with the adaptive articulation class (tied, normal, short, fast or accented).
- Added a regression test proving that the same held chord produces a different first note in the contour-following ARP when the player approaches it with an ascending versus descending line.

Host CI on the implementation commit: application sources and native bridge compile; HAWK overlay validation and host tests pass. PI32v2 target build is still a separate validation gate. This is not yet a flashable or physically tested firmware release.
