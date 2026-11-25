# 0.6.0 – 0.6.4: tuning, mix, adaptive playing, ARP, sequencer, vector panel

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
- **Mix (0.6.1).** No limiter and no knee: every sounding voice gets an
  equal share of full scale, 1/n, so n voices — each at most full scale on its
  own — can never clip together. The share glides when the count changes
  (down in ~1.5 ms, back up over ~25 ms) instead of stepping as 0.5.0's
  division did. A larger share (1.5/n) was measured and rejected: a lone
  double bass C2 at velocity 127 bowed itself to full scale. One voice peaks
  around 0.78 M (-21 dBFS), eight parts 0.97 M: there is headroom by design,
  and MASTER sets the level. (0.6.0 had 2.5/sqrt(n) with a soft knee.)
- **MASTER up to +12 dB (0.6.2).** The knob's middle is unity, fully open x4,
  so the even mix's headroom can be turned into level. Over full scale the
  output saturates: the player decides, at the top of the knob.
- **Even across instruments (0.6.1).** The mallets' strike burst now scales
  with the loop length as the bow force does; high mallet notes were far
  louder than everything else (timpani C7 13.6 M, marimba C6 6.8 M against
  3–5 M for the rest, measured at gain 1). Now every instrument alone at
  velocity 127, C2..C7, peaks at most 5.2 M. Check: `test_gain_staging`
  (all 18 instruments x 6 octaves, and the full orchestra, under full scale).

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

## 0.6.3: shared resonance

The orchestra now shares one sympathetic body (`app/orch_resonance.c`): eight
"open strings" at G2 C3 D3 G3 A3 D4 A4 E5 (the strings' open strings), each a
tuned loop (0 to +2 cents on a sine sweep) with a T60 of 2.5 s at G2 falling to
1.6 s at E5, fed by the whole mix and panned as the orchestra sits (low strings
right, high left). A note on one of their pitches leaves them ringing after it;
a note between them barely does: at resonance 100, the tail after a 1 s G3
carries about twice the energy it has without (+98 %), after a G#3 only +11 %. The ring is added on top of the dry sound (never a
crossfade), inside the headroom of the even 1/n mix: the full orchestra at
velocity 127 with resonance 100 peaks near 3 M of the 8.4 M full scale. Still no
limiter.

- Panel: CONSOLE `KNOB1` is now RESON, 0..100, default 25 (0 bypasses it).
  The scale stays on `ALGORITHM` and the `SCL` button.
- Record: the amount is appended to the OR60 record with its own checksum, so
  0.6.2 saves still load (at 25).
- Cost: eight interpolated reads per sample, 16 KB of pool.

## 0.6.4: MIDI as Yamaha does it, and the headroom used

- **Velocity** is now an even line in dB: the line runs over 32 dB (velocity 1 measures −34.5 dB from 127) with the same
  step everywhere (measured within 0.2 dB from 8 to 127, averaged over all 18
  instruments). In 0.6.3 the velocities from 1 to 16 spanned 35 dB and were
  nearly silent, while the top half spanned only 10 dB.
- **Controllers** follow Yamaha XG: CC1 vibrato, CC2 breath, CC7 and CC11 on
  the GM2 curve, CC10, CC67, CC74, pitch bend with RPN 0, bank select and
  program change, the relative sound controllers CC71..78, the sends CC91,
  CC93 and CC94, and CC121 with XG's reset. The whole map is in
  [MIDI.md](MIDI.md). Setting controllers change the same settings the panel
  turns, so the panel shows them and SAVE keeps them.
- **Mix**: the share is now 1.36 / n (a lone voice stays at unity). Each share
  is sized by what one voice measurably reaches (5.6 M at most), no longer by
  full scale. Eight voices in phase stay below 7.6 M. The full orchestra at
  127 with CC7 fully open peaks at 3.7 M (2.7 M in 0.6.3). Eight double
  basses on one C2 at 127 stay under full scale; that is a test. Still no
  limiter.
- XG powers on at volume 100 (−4.2 dB), and so does this engine now. With
  CC7 at 127 it is as loud as before or louder. MASTER has +12 dB besides.

## Not done, still open

- Bow position (two loop segments), reed/lip nonlinearities, modal body
  resonators: the next modelling step, after the audio block has been timed
  on the PI32v2.
- Audio-block time and panel drawing time on the FM-1 itself.
