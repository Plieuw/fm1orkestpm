# Divisi and Adaptive Playing — FM1 port design

Reference inspected: Plieuw/himalias, branch himalia-strings-v1.58.0-beta, commit 88b0a3bdfa1b265143ba2235ba2ef0fdaa703604.

This document describes a hardware-aware adaptation of the Himalia Strings design. The FM1 is a different platform and a MIDI-polyphonic instrument, so the six-player Himalia engine is a musical/DSP reference, not code that can be compiled unchanged.

## 1. Keep two kinds of polyphony separate

The FM1 engine must distinguish:

1. MIDI note polyphony — how many independent notes are sounding at once.
2. Instrument divisi polyphony — how many modeled players/parts are used to realize one note or a chord as an ensemble.

A MIDI note is a note event owned by one MIDI channel/instrument part. That event may drive one solo model or a small group of divisi players. The global DSP budget is shared, but each instrument part has its own voice quota, phrase state and adaptive-playing state.

This avoids treating a six-player string section as six unrelated MIDI notes.

## 2. Instrument-part routing

- MIDI channels 1–16 can select independent instrument parts through a channel-to-part map.
- Each part owns its own instrument model, active note list, phrase interpreter, sustain state, expression state and divisi configuration.
- The selected instrument remains polyphonic: multiple notes can sound together on that part.
- Different parts can be enabled simultaneously when the total DSP budget permits.
- A global allocator arbitrates the actual FM1 CPU/memory budget. Per-part voice limits are ceilings, not guaranteed reservations.

The initial default is one active orchestral part with full polyphony. Multi-part playback is enabled progressively after CPU measurements.

## 3. Divisi model

Himalia's strings engine uses six modeled players. They are not six identical copies:
- full strings map players to violin I, violin II, viola, cello, cello and bass families;
- low strings map players to viola, viola, cello, cello, bass and bass;
- section models use a common family but assign different chord tones;
- each player has its own tuning drift, vibrato phase/rate/depth, bow speed/pressure, onset/release timing, level, pan/seating and timbral variation.

The Himalia phrase-density value controls how many independent parts are exposed. Additional players double an existing part when the phrase is sparse instead of inventing unnecessary chord tones. The generative divisi register chooses which players participate and which chord-tone offsets they receive.

### FM1 adaptation

Each instrument part gets a DivisiPlan:
- player_count: 1 for solo, up to a model-specific cap for sections;
- active_mask: which ensemble players currently sound;
- part_assignment[]: which chord voice each player doubles or carries;
- intervals[]: instrument-specific chord-tone offsets;
- density: current phrase density (1..player_count);
- style: traditional / open / warm / tight;
- per-player deterministic personality seed.

For a single note, section divisi can play tutti (all section players reinforce the line). For chords, notes are distributed between divisi parts; when the chord has fewer notes than players, extra players double assigned chord tones with small independent timing, tuning, pan and timbral differences. The assignment should remain stable across connected/legato notes so a melodic line does not jump between section desks.

For the first implementation, use deterministic nearest-voice assignment with hysteresis:
1. preserve existing note-to-part assignments for connected notes;
2. assign new chord tones to the nearest available part/register;
3. double existing chord tones if there are more players than chord notes;
4. reduce density for sparse/soft phrases and expand it for stronger, denser phrases;
5. never exceed the part's voice quota or the global DSP budget.

## 4. Adaptive Playing (HAESE-inspired)

Himalia separates phrase interpretation from the physical string model. Its StrBrain tracks note starts/ends, pitch intervals, average inter-onset interval, gate duty, rests, phrase position/peak, velocity trend, beat accents and phrase density. It produces intentions for the next note; the physical model consumes those intentions.

The FM1 port should keep that separation. Each instrument part owns an AdaptiveState, not one global state shared by all MIDI channels.

### Per-part context

Track:
- previous pitch and previous note start/end;
- average inter-onset interval and average release gap;
- note duration / gate ratio;
- recent velocity average and velocity change;
- interval direction and size;
- phrase index, phrase peak and phrase-start / phrase-end flags;
- connected/legato state and note density.

MIDI Note Off supplies the gate end. The sequencer supplies an exact programmed gate. If a new note arrives before the previous one ends, classify the transition as connected according to a short, tempo-relative gap threshold.

### Per-note intentions

The interpreter outputs bounded values:
- energy: phrase and dynamic intensity;
- attack_scale: how gently the note begins;
- release_scale: how the note leaves;
- vibrato_intent: how much vibrato this note wants;
- glide_time: based on interval size, instrument family and connection;
- accent: phrase/beat/velocity emphasis;
- articulation: legato, portamento, détaché, staccato, spiccato, pizzicato or instrument-specific equivalent;
- divisi_density: number of active section parts.

These are targets smoothed by each voice; they must not cause discontinuities.

## 5. Adaptive rules to preserve

### Phrase and articulation
- A long connected note becomes legato; normal separated notes become détaché; short gates become staccato; very short/fast notes become spiccato.
- A real rest or long pause begins a new phrase.
- Phrase starts are placed gently; a rising line and a new high point can lean into the note.
- Phrase endings relax the energy and allow a more natural release.
- Repeated notes alternate articulation/attack character rather than retriggering identically.

### Dynamics and attack/release
- Compare each new velocity with that part's recent average. A stronger note gets more energy and accent; a softer note backs away.
- Use the peak of the initial velocity ramp where the controller/host delivers velocity slightly after Note On.
- Attack and release are instrument-family dependent and velocity dependent. Keep transients soft; do not use one hard, fixed attack/release envelope for every instrument.
- For strings, release can stop the bow while allowing the string/body to ring; for winds, close the air column/reed excitation smoothly; for brass, relax lip excitation and preserve a short resonant tail.

### Vibrato, tremolo and glide
- Vibrato begins after a short straight-tone interval, then grows naturally during a sustained note.
- Vibrato intention depends on note length, interval, phrase shape, dynamic level and instrument character.
- Tremolo rate and depth follow the playing gesture and note spacing, not a fixed BPM subdivision. This preserves the requested Adaptive Playing behaviour.
- Glide depends on interval distance, note duration, legato connection and instrument family. Small connected intervals can be quick finger changes; larger shifts take longer. Detached notes do not inherit an unwanted glide.
- Pizzicato has its own natural decay and should ring longer than a short bow stroke.

## 6. Instrument-specific adaptive profiles

The adaptive interpreter is shared, but the physical response is not:

| Family | Adaptive controls |
|---|---|
| Strings | bow energy, bow speed/pressure, bow changes, finger shifts, vibrato growth, pizzicato decay |
| Woodwinds | breath pressure, air noise, tongue attack, breath phrase and vibrato |
| Brass | lip/breath pressure, embouchure brightness, tongued attack, note transition and vibrato |
| Choir/voice | vowel/formant transition, breath onset, consonant-like articulation, phrase swell |
| Percussion/mallets | strike energy, hardness, decay and roll density; no string-style legato/vibrato by default |

## 7. CPU-aware voice allocation

Himalia's v1.47 guard reduces the number of fully computed players when the audio interrupt is too expensive; remaining section players can be represented as cheaper delayed doublers. The FM1 should use the same principle only after measuring the FM1 renderer:
- reserve the highest-quality DSP for the lead/most audible parts;
- lower-priority ensemble doublers may use cheaper resonators or delayed/filtered copies;
- reduce divisi density before stealing a held MIDI note;
- steal only as a last resort, preferring quiet/releasing voices;
- maintain a measured safety margin in every audio block.

A fixed claim such as "12 voices" is not a final specification. The final note polyphony and divisi-player count must be benchmarked together on the actual FM1.

## 8. Sequencer integration

Sequencer notes enter the same event and allocation system as external MIDI. Each pattern track maps to an instrument part/MIDI channel and can contain chords. Sequencer timing supplies note duration and gate ratio to Adaptive Playing, but does not force vibrato or tremolo to lock to BPM.

## 9. Implementation order

1. Add per-channel instrument-part state and routing.
2. Replace the single global note state with per-part note/phrase state.
3. Add a deterministic divisi planner and stable note-to-part assignments.
4. Port the adaptive interpreter as a control-rate module with host tests.
5. Add family-specific adaptive targets to the physical models.
6. Integrate polyphonic sequencer events and per-note gate durations.
7. Benchmark combinations of MIDI notes × divisi players × instrument families on hardware.

## 10. Reference and licensing

Reference source: Plieuw/himalias/IOCore/HimaliaSketch/strings_engine.h and STRINGS_BETA.md, branch himalia-strings-v1.58.0-beta. The reference repository is GPL-3.0. Keep required notices and license compatibility if porting or adapting source implementation. This document describes the architecture and does not copy the reference engine implementation.
