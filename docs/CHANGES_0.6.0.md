# 0.6.0 – 0.7.1: tuning, mix, adaptive playing, ARP, sequencer, vector panel

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

## 0.6.5: the even line through adaptive playing, ARP and sequencer

- The adaptive layer, the ARP's ear, the sequencer's ear and call-and-response
  hear the **played dynamic**: the velocity under CC2 breath, CC11 expression
  and the soft pedal, in steps on the even dB line (`orch_cc_dynamic`). A breath
  crescendo now thickens the divisi and the ARP's clusters, as a harder touch
  does. The voice keeps its velocity, so nothing is taken off twice.
- The phrase arc and the echoes are offsets in dB, the same at every dynamic.
  Before this they scaled the velocity, so the arc was 3 dB deep at pp and
  10 dB at ff.
- Every ARP setting and the sequencer's transport has a controller on the
  free XG numbers (CC16..19, 80..89, 102..116), using all 128 values. The
  panel follows MIDI tempo and run. See [MIDI.md](MIDI.md).

## 0.6.6: the tempo comes from the clock, or from the playing

- The **MIDI clock always takes priority**: it sets the tempo of the
  sequencer, of every ARP (until now only the ARPs switched to MIDI clock
  followed it) and of the delay, and the panel shows it. The panel cannot
  override it.
- **Without a clock** the tempo follows the playing a little: it moves a
  quarter of the way towards a pulse when two intervals in a row agree. The
  details are in [MIDI.md](MIDI.md#tempo).
- **Power on is 128 BPM** (was 120).
- CC89 (tempo, 0.6.5) is gone: the clock carries the tempo.

## 0.6.7: the presets made for this firmware

- **Velocity per family.** The models get louder with velocity by very
  different amounts: brass hardly at all, because its level compensation
  flattens it; woodwinds and choir only below 16; strings 22 dB; mallets
  45 dB. With one shared table, a woodwind at velocity 8 was only 13 dB under
  velocity 100, and a mallet 38 dB. Now each family has its own table, and
  every family lands on the same even 32 dB line: measured within 0.6 dB from
  8 to 127 (`tests/velocity_model.txt`, `tools/gen_cc_tables.py`).
- **The eight factory scenes:**
  - The motifs' velocities are moved onto the even line, so they sound as
    loud as their 0.6.3 levels intended. So are call-and-response's answers
    (54 and 72 became 81 and 94).
  - Each scene sets the shared resonance (BRASS 15, BAROQUE 20, CINEMA 35,
    PIZZ 45, AMBIENT 60, …).
  - Each scene sets the adaptive sequencer (on where a motif should follow
    the harmony) and the ARP's ear (adaptive span, as played, strum).
  - Articulations come from all twelve: AMBIENT's strings play sul tasto,
    OSTINATO's détaché. The rest stay AUTO, so adaptive playing chooses.
  - Each scene sets all four settings of every effect, so nothing of the
    previous scene remains. Before, the 3rd and 4th settings carried over.
  - Chorus only in AMBIENT, and shimmer only in the open scenes.
  - The delay is locked to the tempo (SYNC), which now comes from the MIDI
    clock or the playing.
- A test plays every scene: the whole orchestra at velocity 127, CC7 fully
  open, through the scene's own effects. It stays under full scale.

## 0.6.8: no zipper

- **Controllers interpolate between their values.** Each new CC value (and
  pitch bend) is reached along a line as long as the gap since the previous
  message (3..50 ms). Until now a 3 ms one-pole took each of the 128 steps
  almost at once, and fast or slow sweeps were heard as steps. The gain moves
  at most 1/378 of a message's step per sample (`test_no_zipper`). The lines
  are computed in 1/256 units, so a 50 ms line lands where it was going.
- **Effects:** a mix moves along a line, 20 ms from dry to full wet. Switching
  an effect on or off fades it in or out, and an effect switched off keeps
  running until it is silent. Chorus and ensemble used to click when switched
  on: they started from an empty modulation line. That line is now kept
  written while the chain runs, and after a silence they wait until it is full
  before fading in. A tone through the chorus switched on mid-note moves no
  faster than the tone itself (1.0×). With a step instead, it jumped 4×
  (`test_mix_fades`).
- **Resonance** (CC71, RESON) moves along a 20 ms line. **MASTER** moves along
  a line across each block, both in the app and on the HAWK runtime, where the
  pot's own jitter was a step every callback.

## 0.6.9: both cores

The AC79 in the FM-1 has two cores. HAWK CHUA starts the second one at boot,
but until now the orchestra replaced HAWK's synth, which was the only thing
that gave it work, so it sat idle. Now the engine renders on both:

- **The render is split.** `orch_render_q23` works in chunks of 32 samples.
  First comes **control**, one sample at a time on core 0: events, ARP,
  sequencer, call and response, tempo, and the controllers' lines. The gain
  and balance each part has at each sample are written down. Then come the
  **voices**, each through the whole chunk, with every other sounding voice on
  the second core. Each core sums into its own buffers, and core 0 adds them,
  applies the 1.36 / n share and the guard, and the hall's strings.
- **One core or two sound the same, bit for bit.** A voice's render writes
  only itself; each voice now has its own noise generator in place of the
  shared one; and the sums are integers. The new suite
  `tests/orch_cores_test.c` plays a busy orchestra and compares the output
  bit for bit against a single core. It covers a second core that runs at
  once, a real thread, one that refuses jobs, and one that takes a job and
  never answers. It is clean under ThreadSanitizer as well.
- **On the device** the hand-over is HAWK's own (`fm1_cpu1.h`, from X0X,
  Melodee and Jangada): a check word, a 10 ms watchdog, and a hold for good
  on units that misread, after which core 0 renders everything. The glue
  hands the core to the engine (`orch_hawk_set_second_core`). Without a
  second core (host, browser, 2ND CORE off) everything runs on one.
- **A note started by the ARP or the sequencer** inside a chunk starts at the
  chunk's beginning: at most 0.7 ms early.
- `orch_init` now clears every voice and part and reseeds the generators, so
  a fresh start sounds the same every time.

Not measured: how much time this saves per block on the PI32v2. The split
is HAWK's proven pattern, but nothing has run on a board.

## 0.7.0: half the work, the same sound

The question was what could be pre-rendered to save CPU, such as the
transients. The measurement (callgrind, eight voices of FULL ORCH) answered
first. The transients were cheap: the strike's noise burst and the bow's bite
together took about 2 %. What cost was recomputing every sample what hardly
changes from one sample to the next. So:

- **Two rates for a voice.** Once every 32-sample chunk (1378 times a
  second), `voice_control` works out the glide, bend, vibrato, LFO, phrase
  expression, tremolo, the excitation's strength, the damping and the
  loudness compensation. The loop length and the output gain are drawn as
  straight lines across the chunk, so nothing steps. The waveguide, the
  excitation, the body, the followers and the envelope stay per sample
  (`voice_sample`).
- **Pre-computed per note:** the bow's speed and force, which until now were
  worked out every sample from the velocity.
- **The hall's strings in 32-bit arithmetic.** Before, each string did five
  64-bit products per sample, which the PI32v2 has to build in software. The
  feedback is split into high and low halves so it stays exact. Their
  constants are worked out once per block, and the panel's ring meter is
  updated once per block.
- **Result on the host:** 39.4 → 19.7 ns per voice-sample in FULL ORCH,
  40.9 → 24.9 in CINEMA, 52.4 → 34.7 in AMBIENT (`tests/run_bench.sh`). The
  instruction count is 634 → 386 M; the hall's share is 97 → 68 M. On the
  PI32v2, where a 64-bit product costs more, the hall should gain more. Not
  measured on a board.
- Pitch is unchanged: every family stays within 3 cents at A2, C4 and E5,
  except the bowed string at E5. That was already 15 cents flat (bow pressure
  pulls it down) and is now 10 cents flat. One core or two remain the same
  bit for bit, and still clean under ThreadSanitizer.
- **Not done:** pre-rendered noise tables. Noise is 2.5 % of the work, and a
  table would loop audibly. The body resonator is kept per voice so voices
  stay independent of each other.

## 0.7.1: thirty instruments, sixteen voices

- **Twelve new instruments**, 30 in all. Each family is listed in score order;
  PRESETS and program change walk it in that order.
  - Strings: violin, viola, cello, double bass, harp, **harpsichord** (a
    bright quill pluck).
  - Woodwinds: piccolo, flute, **alto flute**, oboe, **English horn**,
    clarinet, **bass clarinet**, bassoon, **contrabassoon**.
  - Brass: trumpet, horn, trombone, **bass trombone**, tuba.
  - Mallets: **glockenspiel**, **celesta**, xylophone, **vibraphone**,
    marimba, **tubular bells**, timpani.
  - Choir: choir, **women's choir**, **men's choir**.

  They are appended to the numbering, so saved parts and scenes keep their
  instruments. Each has a profile, default ADSR, a seat on the stage and a
  short name. Tested: in tune at a note of their range (0.4 %), the struck
  ones still ring a second after the hit, and every instrument at velocity
  127 across C2..C7 stays under full scale. The velocity tables were
  re-measured (`tests/run_velocity_model.sh`).
- **Scenes:** CINEMA has the celesta, BAROQUE the harpsichord as continuo,
  WIND CHOIR the English horn, BRASS FAN the bass trombone, and AMBIENT the
  vibraphone.
- **Sixteen voices** (was eight). At 2048 samples each, sixteen lines would
  not fit the pool, and only low notes need that length. So voices 0..7 have
  2048-sample lines (down to 21.5 Hz) and voices 8..15 have 1024 (down to
  43 Hz, with a whole tone of bend to spare). A note takes a voice whose line
  holds it, and higher notes try the short ones first, so the long ones stay
  free for the bass. Bodies are 416 samples (the lowest is 401). The hall's
  string lines are int16 (−8 KB). Tested: eight parts of double basses on E1
  get the eight long voices and stay in tune; with violins above, all sixteen
  sound. The share is 1.36 / n up to 16. The second core's job carries a
  16-bit voice mask.
- **Memory**, from the build's symbol sizes: voices and lines 66.9 KB (was
  about 43 KB), the hall 8 KB (was 16). That puts the pool about 15.5 KB
  higher, near 362 KB of 368 KB, which the CI build prints.
- **CPU:** with sixteen voices sounding the voice work doubles. Per voice it is
  unchanged (`tests/run_bench.sh`: 20.0 ns in FULL ORCH, 21.2 ns in CINEMA,
  30.2 ns in AMBIENT). Not measured on the FM-1.

## Not done, still open

- Bow position (two loop segments), reed/lip nonlinearities, modal body
  resonators: the next modelling step, after the audio block has been timed
  on the PI32v2.
- Audio-block time and panel drawing time on the FM-1 itself.
