# Factory preset design — orchestral FM1

These eight factory scenes are complete musical starting points for the FM1
Orchestral Engine. They are designed around modeled orchestral sections,
adaptive response to the player's live MIDI, divisi, and phrase-aware motion.
They are not generic synth patches and they do not replace live playing.

## Recall and control contract

- Turn the dedicated HAWK-CHUA PRESETS encoder (matrix encoder 6).
- Each detent immediately recalls the next/previous scene.
- Recall works from every existing page. No new page, confirmation step,
  or remapped performance control is introduced.
- The existing four encoders and button workflow remain unchanged.
- Preset recall is a control-task transaction with the engine paused.

## What a scene recalls

| Layer | Recalled state |
|---|---|
| Orchestra | Four part instrument assignments, articulations, divisi/player caps |
| ARP / phrase engine | Enable states, ARP type, phrase profile, tempo, division, density, range, cluster size, accent, quantize, gate, swing, logic, inverse, root, repeats, velocity/gate grooves, latch and clock source |
| Step sequencer | 16-step motif, rests, note anchors, velocity accents, gate lengths, selected part, tempo, adaptive revoicing and enable state |
| Ensemble interaction | Call/response enable, leader/follower parts and interval |
| Master FX | Four fixed effect slots, enable, mix, parameter A and parameter B |

The four FX slots are currently fixed by the engine architecture:
**Chorus → Ensemble → Delay → Reverb**. Presets vary each slot's enable,
mix and two parameters. They do not pretend to change the effect algorithm
or reorder the chain; that would require a separate engine feature.

## Scene intent

### 1. Cinema Strings
Wide string-section foundation with violin, viola, cello and bass. Long,
connected phrase behavior, restrained step motion and spacious chorus/ensemble
color. Intended for sustained chords, slow harmonic movement and adaptive
voice-leading rather than a rigid ostinato.

### 2. Baroque
Strings with harp and flute color. Measured pulse, light ornamentation,
shorter articulation and a clear repeating motif. The ARP supplies controlled
note-order movement while the player determines harmony and phrase direction.

### 3. Wind Choir
Violin/string support with flute, clarinet and oboe colors. Sparse,
breath-shaped phrase entries, longer gates and soft ensemble/reverb support.
The phrase engine leaves room between entries so the live line can breathe.

### 4. Brass Fan
Trumpet, horn and trombone/tuba family. Stronger accents, compact ceremonial
calls and restrained spatial effects. The call/response layer creates a
leader/follower relationship rather than simply doubling every note.

### 5. Pizz Motion
Pizzicato strings with a percussive color part. Syncopated short notes,
repeating phrase motion, more pronounced velocity/gate grooves and a clear
rhythmic FX profile. Adaptive revoicing follows the player's current notes;
the stored motif supplies rhythmic identity.

### 6. Ambient
Low strings, bass, winds and choir. Slow tempo, sparse anchors, long gates,
wide phrase range and the broadest spatial FX blend. Designed for evolving
harmonies and sustained player-led input, not a fixed melody.

### 7. Ostinato
Interlocking strings, winds and brass. A repeating 16-step contour, moderate
swing, XOR phrase logic and a call/response interval. Intended as a moving
orchestral bed that remains harmonically responsive to live MIDI.

### 8. Full Orchestra
Balanced string, wind and brass sections with generous divisi caps. A broad
phrase foundation and moderate FX. This is the neutral starting point for
building an arrangement from the FM1 controls.

## Adaptive-play principle

A preset sets the *musical context*, not a frozen performance. Incoming MIDI
continues to drive the adaptive learner and phrase response after recall.
The sequencer's stored notes are neutral pitch anchors that can be revoiced
against the player's live chord/contour when adaptive mode is enabled. This
keeps the concept expressive and player-led instead of making every preset
a fixed backing track.

## Scope and validation

These are factory scenes, not user-save slots. The current FM1 engine exposes
four master FX slots with fixed algorithms and two percentage parameters per
slot. Per-part ARP/phrase settings are initialized from the same scene profile;
the sequencer currently recalls one active lane/part per scene, matching the
current engine API.

Source compilation is not hardware validation. Confirm encoder direction,
physical button mapping, MIDI response, effect balance and the perceived
orchestral voicing on the FM1 before treating the scenes as final.
