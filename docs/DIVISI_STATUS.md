# Divisi and Adaptive Playing — implementation status (0.2.0-beta)

Companion to [DIVISI_ADAPTIVE_ARCHITECTURE.md](DIVISI_ADAPTIVE_ARCHITECTURE.md),
which stays the design. This file records what the beta actually implements.

| Design item | Status | Where |
|---|---|---|
| 1. Per-channel instrument-part state and routing | done (4 parts, configurable channel masks) | `app/orch_part.c` |
| 2. Per-part note/phrase state | done (one adaptive state and one note list per part) | `orch_part.c`, `orch_engine.c` |
| 3. Deterministic divisi planner, stable assignments | done (1–4 players, hysteresis, doubling, per-slot detune/seat) | `app/orch_divisi.c` |
| 4. Adaptive interpreter at control rate, host tests | done (fixed-point port; 7 host test groups) | `app/orch_adaptive.c`, `tests/orch_adaptive_test.c` |
| 5. Family-specific adaptive targets in the models | partial: energy, accent, attack, release, vibrato, glide and articulation reach four excitations; friction, body resonances and rosin transients are not there yet | `orch_engine.c`, `HIMALIA_PORT.md` |
| 6. Sequencer events through the same path | done (reserved source, per-note gate, part selectable) | `orch_engine.c` |
| 7. Benchmark notes × players × families on hardware | not done: no firmware package has been built or flashed | — |

Deviations from the design worth naming:

- Divisi density is capped at four players, not six: four players × 12 voices
  would not fit the interrupt budget, and the design already allowed a
  model-specific cap.
- The plan is recomputed at note-on only; held voices are never re-assigned or
  restarted. That keeps the hysteresis guarantee but means a chord tone added
  mid-phrase does not redistribute the players already sounding.
- `phrase_end` and pattern look-ahead stay at their neutral values until the
  sequencer feeds its pattern length into the interpreter (the fields exist).
- The engine does not yet read back a measured CPU load to reduce density by
  itself; the per-part voice ceiling and player cap are static settings.
