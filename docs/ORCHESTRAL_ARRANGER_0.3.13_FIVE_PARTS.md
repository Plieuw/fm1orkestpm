# FM1 Orchestral 0.3.13 — five-part orchestration pass

This pass extends the optional call/response prototype without replacing the existing HAWK-derived ARP, phrase engine, 16-step sequencer, adaptive interpreter, or divisi planner. All orchestration behaviours remain opt-in through the existing call/response enable switch.

## 1. Harmony-aware answers

Incoming MIDI is the sole source of the held pitch-class mask. A response first applies its configured transposition and small deterministic motif variation, then chooses the nearest available note in the leader's held chord mask within MIDI 36–96. If no chord context is held, it falls back to the configured interval. Generated response notes are not fed back into the harmony learner.

## 2. Motif variation by section

The response line now uses a small repeating contour variation rather than copying every leader note at one fixed interval. It remains deterministic and bounded: the configured interval is the anchor, and the variation is only a few semitones. This is an initial motif vocabulary, not a full phrase composer. The next layer can give each family a larger motif bank while retaining a common phrase identity.

## 3. Strings as harmonic support

When the selected follower is a string part, the response is softer and longer than a wind/brass answer. This creates a sustained support tone under the caller rather than another short foreground phrase. It still uses the normal instrument, adaptive articulation, divisi, and voice allocation path. This is a single-note support layer, not yet automatic four-part chord voicing.

## 4. Woodwind / brass phrase exchange

When the configured follower is a woodwind or brass part and an enabled complementary section exists, the answer is routed to that complementary family. Thus a woodwind-led response can be answered by brass and vice versa, while a configuration without both families continues using its configured follower. The configured leader remains the incoming MIDI part; generated replies never recursively trigger another reply.

## 5. Adaptive eight-voice allocation

The eight-voice ceiling remains hard. When a part reaches its own ceiling, its own releasing/oldest voice is reused as before. When the global pool is full, the engine first reclaims the quietest release tail; otherwise it steals the oldest voice from the most represented part. This discourages a single section from monopolising the entire modeled-voice pool and improves section balance under dense passages.

## Boundaries and validation

- No new heap allocation, floating point, locks, or file I/O in the render path.
- Harmony and routing decisions happen on incoming MIDI events; the response clock is sample-counter based.
- Tempo is bounded by the existing 30–300 BPM setter.
- The response scheduler allows one pending answer at a time.
- The existing 8-voice ceiling and per-part ceilings remain in force.
- All new response behaviour is disabled by default.
- Host regression tests, PI32v2 target build, static RAM/pool validation, and physical FM1 audio testing are still required before a flashable beta can be claimed.

## UI integration contract

Expose the existing `orch_call_response_configure(enabled, leader, follower, interval)` as the master switch and core routing controls. The selected follower is a seed role: when it is woodwinds/brass the engine may exchange the reply to the complementary configured family. The UI should explain that strings are used for a longer, softer support answer when selected as follower. Do not add a second independent MIDI router in the UI.
