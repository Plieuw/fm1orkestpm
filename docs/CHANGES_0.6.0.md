# 0.6.0: tuning, mix, adaptive playing, ARP, sequencer, vector panel

Written 25 November 2026 on `beta/0.6.0-refine`, branched from
`beta/0.5.0-orchestra-merge`. One commit a topic; every number below is a host
measurement (`tests/run_measure.sh`, the suites, `tests/run_panel_bench.sh`).
Nothing has been heard on an FM-1.

## Sound

- **Tuning.** The loop's fractional read went the wrong way and the
  jet/lip/voice oscillators divided by whole samples, so every note sounded at
  SR/whole. Cents at A2 / C4 / E5, before -> after: woodwinds +4.5/+6.0/+23.5
  -> +0.4/+0.6/+0.4; brass +6.6/+6.0/+23.4 -> +0.3/+0.6/+0.2; choir
  +3.6/+5.7/+24.7 -> -0.1/+0.5/0.0; strings +2.5/+4.3/+4.1 -> +1.4/+2.0/-2.6.
  Check: `test_high_register_tuning`.
- **Loop filter.** A first-order FIR whose coefficient follows the damping
  continuously (0.5.0: three fixed shapes in six steps); its delay is taken off
  the read, so brightness no longer moves the pitch.
- **Mix.** Gain 2.5/sqrt(n) that glides (~6 ms) instead of dividing by the
  voice count, which stepped every sounding note when a voice started and left
  one note at -16 dBFS. Four-piece soft knee from -6 dBFS; measured worst case
  (eight parts, one octave, velocity 127) 18.3 M -> 7.2 M. One voice now peaks
  2.1 M, eight parts at velocity 100 6.0 M. Check: `test_gain_staging`.

## Adaptive playing

- Keys of a chord (within 35 ms of a held note) share the chord's intent: no
  glide from the root, no jump to four desks, and no 10 ms "interval" in the
  tempo estimate (one chord a second used to read as 227 ms).
- The divisi follows the line's weight, one desk at a time (it used to add a
  desk every note and sit at four from the third note).
- The absolute velocity scales the energy (x0.85..x1.15), not only its change.
- How a note ends: the held last note of a line rings out (release x1.5), a
  short detached note is stopped sooner (x0.75).
- Checks: `test_chord`, `test_density_follows_weight`,
  `test_release_follows_ending`.

## ARP

- The octave span no longer follows the player's activity unless SPAN is set
  to ADAPT; UP 2 is two octaves.
- Order by pitch or as played; ratchet 1..4; strum 0..40 ms.
- Five knob banks behind PRESETS: PLAY, FEEL, PHRASE, ORDER, GROOVE.
- Checks: `test_arp_as_played`, `test_arp_ratchet`, `test_arp_fixed_span`.

## Sequencer

- A lane per part (16 steps x 4 notes each), all lanes playing at once; mute
  per lane; pattern length 1..16; per-step chance; the step's note has a knob.
- Two knob banks behind PRESETS: STEP (NOTE, VEL, GATE, CHANCE) and PATTERN
  (LENGTH, TEMPO, LANE, CLEAR).
- Check: `test_sequencer_lanes`.

## Panel

- Anti-aliased vector rasterizer (1/16 px, four sub-rows, per-pixel coverage).
- The STAGE page is a vector seating plan: eight desks in the rings of the
  orchestra at their instruments' seats, lit by their voices, the podium's light
  ring, the listener in front. The hall photograph is gone (~20 KB of flash).
- Bench: stage page 5 136 fills, half a second of one note 1 177, a knob turn
  937 (0.4.0: 2 683 / 268 / 554). More calls for smooth edges; still to be timed
  on the ST7789.

## Record and interfaces

- Flash record "OR60" (all eight lanes, the new ARP fields); "OR50" is not read.
- `orch_adaptive_off()` returns a release multiplier; `orch_perf_config` has
  four new fields; `struct orch_step_note` has `chance`; `orch_seq_set_part()`
  chooses the lane being edited; new `orch_seq_clear_all`, `orch_seq_set_length`,
  `orch_seq_set_lane_muted`.

## Not done, still open

- Bow position (two loop segments), reed/lip nonlinearities, modal body
  resonators: the next modelling step, after the audio block has been timed
  on the PI32v2.
- Audio-block time and panel drawing time on the FM-1 itself.
