# Performance: the routine, and how a voice is built

Read this before you change what runs in the audio interrupt, and before you
add an instrument model or an excitation. It records how 0.7.0 halved the
engine's work without changing its sound (`docs/CHANGES_0.6.0.md`, 0.6.9 and
0.7.0), so the next change can be measured and checked the same way.

Nothing here has been timed on the FM-1. Every number is a host measurement.
Host time is a guide to relative cost, not to the PI32v2's cycles. Never write
that something is fast enough on the device.

## 1. The ground rules of the target

- **The PI32v2 has no 64-bit divide** (`__udivdi3` is undefined), and a 64-bit
  multiply is built from several 32-bit ones. In anything that runs per
  sample, prefer 32-bit arithmetic with an overflow budget written next to it
  (see 5.3). A 32-bit divide is allowed, but keep it out of per-sample code.
- **The app is freestanding.** There is no `<string.h>` in `app/`. Zero a
  block with a word loop (`zero_words` in `orch_engine.c`), not with a
  `static const` zero copy, which would cost flash the size of the block.
- **Two cores** (0.6.9). Core 0 runs control. The voices are shared between
  both cores in 32-sample chunks. A voice's per-sample work **may write only
  that voice**, and anything shared that it reads must already be settled
  before the split (section 3).
- **Memory.** Flash has room (the app is about 177 KB of a 0x8F000-byte app
  area). RAM and the `.pool` section do not: about 47 KB of 73 KB RAM, and
  the pool about 346 KB of 368 KB after the hall's 16 KB. Tables that never
  change belong in `static const` (flash). Buffers belong in the pool, but
  only if they are needed.

## 2. The routine

Work in this order. Do not optimise before step 2, and do not commit before
step 6.

1. **Baseline.** `FM1_CORE=../fm1-core sh tests/run_bench.sh` prints ns per
   voice-sample for CINEMA (0), AMBIENT (5) and FULL ORCH (7), with all
   eight parts holding a note. Write the three numbers down.

2. **Profile at line level.** Run callgrind on the benchmark:

   ```sh
   cc -std=c11 -O2 -g -DFM1_HOST_TEST -I../fm1-core/include -Iapp tests/bench.c \
      app/orch_cc.c app/orch_fx.c app/orch_engine.c app/orch_adaptive.c app/orch_divisi.c \
      app/orch_part.c app/orch_stage.c app/orch_performance.c app/orch_resonance.c \
      app/orch_scenes.c -o /tmp/benchg
   valgrind --tool=callgrind --callgrind-out-file=/tmp/cg.out /tmp/benchg 7 3
   callgrind_annotate /tmp/cg.out | head -20                       # by function
   callgrind_annotate --auto=yes /tmp/cg.out | less                 # by line
   ```

   Divide a function's instruction count by its voice-samples (3 s × 44100 ×
   active voices) to get instructions per voice-sample. In 0.6.9 that was
   about 415 for a voice, plus the hall at about 91 per string-sample.

3. **Sort every hot line into one of four kinds.**

   | Kind | Example | Where it belongs |
   | --- | --- | --- |
   | Fixed for the whole note | bow speed and force from the velocity; the profile | `voice_start` → a field of the voice |
   | Moves slowly (below ~100 Hz) | LFO, vibrato, glide, bend, expression, damping, excitation strength, loudness gain | `voice_control`, once a chunk, drawn as a line |
   | Fixed for a block | a string's read length, constants of the hall | at the top of the block function, in locals |
   | The physics itself | the delay-line read and write, the loop filter, friction, the body, DC, followers, the envelope | `voice_sample`, every sample |

   Only the last kind has to run every sample. Before 0.7.0, most of the cost
   was the second kind recomputed 44 100 times a second.

4. **Move it, and keep it smooth.** A value worked out once a chunk must not
   step at the chunk's edges. Draw it as a straight line from where the last
   chunk ended to where this one ends (the `c_len`/`c_amp` pattern, see 5.1).
   A value that only feeds a filter coefficient (damping `b`, excitation
   gain) may be held for the chunk. Its steps are 0.7 ms apart and small.

5. **Make 64-bit 32-bit where the range allows**, with the budget in a
   comment (5.3). Where it does not, as in a resonator's feedback, split the
   product into a high and a low half so it stays exact.

6. **Verify, all of it:**
   - `FM1_CORE=../fm1-core sh tests/run_tests.sh`: nine suites, all green.
     `test_gain_staging` and `test_scenes_stay_in_range` hold full scale with
     no limiter. `test_no_zipper` holds the lines. `orch_cores` holds one
     core and two to the same output, bit for bit.
   - ThreadSanitizer on the cores test:

     ```sh
     cc -std=c11 -O1 -g -fsanitize=thread -DFM1_HOST_TEST -I../fm1-core/include -Iapp \
        tests/orch_cores_test.c app/orch_cc.c app/orch_fx.c app/orch_engine.c app/orch_adaptive.c \
        app/orch_divisi.c app/orch_part.c app/orch_stage.c app/orch_performance.c \
        app/orch_resonance.c -pthread -o /tmp/tsan && /tmp/tsan
     ```

   - `sh tests/run_measure.sh`: pitch per family at A2, C4 and E5. Compare
     before and after with `git stash`. A change in cents is a change in
     sound.
   - If a model's level against velocity moved:
     `sh tests/run_velocity_model.sh` re-measures and regenerates
     `app/orch_cc_tables.inc`. Then run the suites again: the level light's
     `LEVEL_TOP` in `app/orch_level.inc` may need recalibrating, as it
     already has been several times.
   - `sh web/emu/build.sh && node web/emu/smoke.mjs`.
   - `sh tests/run_bench.sh` again, and record before and after in
     `docs/CHANGES_*.md`.

7. **CI builds for the device** (`hawk-target-build`). That is the only
   place a 64-bit divide or a missing symbol shows up. Push, wait for green,
   and check that the pre-release has the `.fwsc`.

## 3. How the render is built

```
orch_render_q23(out, frames)            core 0, in the audio interrupt
  drain_events()                        MIDI in
  for each chunk of ORCH_CHUNK = 32:
    render_chunk()
      1. control, per sample            core 0
           ARP, controller lines (orch_cc_step), tempo, performance events,
           call and response, sequencer;
           chunk_gain / chunk_pan_l / chunk_pan_r [part][f] written down
      2. voices, voice-major            every other sounding voice on core 1
           render_voices(mask, n, side)
             per voice: voice_control(v, n)      once
                        voice_sample(v) x n      per sample
             each core sums into sum_l/sum_r/sum_n[side]
      3. mix, per sample                core 0
           1.36 / n share (MIX_GAIN_Q12), clamp guard
  orch_resonance_process()              the hall's eight strings, per block
```

Rules that keep the two cores identical, bit for bit:

- `voice_control` and `voice_sample` write only `*v`. Each voice has its own
  noise generator (`v->rng`, `noise_v`). The global `noise16()` is only for
  `voice_start`, which runs in control.
- Part settings (`orch_part_get`), the controller arrays (`orch_cc_*`) and
  the chunk arrays are read-only during phase 2. A per-sample value that a
  part has (gain, pan) goes through the chunk arrays. Slower ones (bend,
  brightness, excitation, mod) are read at the chunk's end.
- The sums are integers, so the order of addition does not matter.
- `orch_init` clears every voice and part and reseeds the generators, so two
  runs from a fresh start are identical. The cores test relies on this.

## 4. Adding a model or an excitation

1. **Profile** (`INST_PROFILE` in `orch_engine.c`): add a row and an enum
   entry in `orch_part.h`. Give the family in `orch_instrument_family` and
   default ADSR in `orch_instrument_adsr` (`orch_part.c`).
2. **Note-on** (`voice_start`): everything fixed for the note. That means the
   loop length and its loss (`len_q16`, `loop_gain_q23`), any oscillator
   increment (with `div_pow2`, not a 64-bit divide), and constants such as
   `bow_speed`/`bow_fmax`. Reset the lines on a new note (`c_len = 0`,
   `c_amp = 0`) but not on a legato handover.
3. **Slow modulation** goes into `voice_control`. Write it as "n samples
   on": advance phases by `inc * n`, and count down counters by `n`. If it
   moves pitch or loudness, fold it into the end of `len` or into the gain
   before the line is drawn.
4. **The excitation** goes into the `switch (v->excite)` in `voice_sample`.
   It is per sample, and uses `noise_v(&v->rng)` only, `c_exc_gain` for
   strength, and `delayed` for the string's state.
5. **Level.** The loudness compensation evens instruments out across the
   register. After adding a model:
   - run `tests/run_velocity_model.sh` (a new family needs a row in the
     generator and in `VEL_OUT_Q12`);
   - check that `test_gain_staging` (every instrument, six octaves, velocity
     127, CC7 fully open) stays under full scale;
   - check that `test_high_register_tuning` passes.
6. **Run the routine's step 6 in full.**

## 5. Patterns, with the traps found on the way

### 5.1 A line across a chunk

```c
/* voice_control: where this chunk ends */
if (!v->c_len) v->c_len = len;                   /* new note: no line from nowhere */
v->c_len_step = ((int32_t)len - (int32_t)v->c_len) / (int32_t)n;
/* voice_sample: every sample */
v->c_len += (uint32_t)v->c_len_step;
```

The remainder of the division is carried into the next chunk, because the
next line starts from where this one actually got to. Gains use the same
pattern in Q15 × 2^8 (`c_amp`), so a slow change still moves every sample.

### 5.2 Controller ramps need fractional precision

`orch_cc.c` draws each controller as a line across the gap between messages
(3..50 ms). With whole units per sample, the truncation added up to almost
one unit per sample over a 2205-sample line, and the line jumped by that much
at its end. That is a zipper of its own (`test_no_zipper` caught 430 where 3
was expected). The accumulators are in 1/256 units (`ramp_acc`). Any long
line needs the same.

### 5.3 32-bit with a budget

`orch_resonance.c` after 0.7.0, for example:

- **The read:** `a + ((((b - a) >> 2) * frac) >> 6)`. `b - a` is at most
  2^25, shifted to 2^23, times 255, which stays under 2^31.
- **The feedback:** `(s >> 15) * g + (((s & 0x7FFF) * g) >> 15)`. This is
  exactly `floor(s * g / 2^15)` without 64 bits.
- **The pan sum:** `(y >> 4) * pan`, eight strings, at most 2^30.

Write the budget in the comment. The next change that widens a range has to
see it.

### 5.4 Effects that switch

An effect that is switched on reads its buffers. If those were not written
while it was off, it fades in over silence and clicks. The chorus did this
until 0.6.8. `orch_fx_dsp.inc` keeps the modulation line written while the
chain runs, and holds the mix at 0 until the line is full (`mod_fill`). The
mixes themselves move along 20 ms lines (`mix_glide`).

### 5.5 What not to pre-render

- **Noise.** It is about 2.5 % of a voice. A table would loop audibly, and
  every voice reading the same table would correlate them.
- **Attacks as samples.** The waveguide has to run from the first sample, or
  its state does not match when the sample hands over. It saves no work. It
  could be a sound choice, but that is not this routine.
- **One body for a whole part.** It would save the per-voice body filter,
  but voices would no longer be independent, which the two-core split and its
  bit-for-bit test rely on.

## 6. The numbers so far (host, FULL ORCH, eight voices)

| Version | ns per voice-sample | Instructions (3 s) | Notes |
| --- | --- | --- | --- |
| 0.6.9 | 39.4 | 634 M | everything per sample; the hall in 64-bit |
| 0.7.0, voices at two rates | 27.7 | 415 M | `voice_control` / `voice_sample`, bow per note |
| 0.7.0, the hall in 32-bit | 19.7 | 386 M | hall 97 M → 68 M |

CINEMA went from 40.9 to 24.9 ns, and AMBIENT from 52.4 to 34.7 ns.
