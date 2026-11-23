# Instrument-specific waveguide models — 0.3.0

The FM1 renderer uses the existing per-part Adaptive Playing and Divisi pipeline. Every active Divisi player receives its own waveguide voice; the instrument profile changes the resonator, excitation, damping, envelope and body response. The models are computed in fixed point, with static memory and no allocation in the audio callback.

## Instrument catalogue

The part layer now exposes these instrument identities:

| Family | Instruments |
|---|---|
| Strings | Violin, viola, cello, double bass, harp |
| Woodwinds | Flute, piccolo, clarinet, oboe, bassoon |
| Brass | Trumpet, French horn, trombone, tuba |
| Mallets / percussion | Marimba, xylophone, timpani |
| Voice | Choir |

Each instrument has its own profile: loop gain, damping, attack, release, body resonance frequency and level, bow pressure or breath-noise amount, vibrato range, and (where appropriate) plucked/struck or half-period behaviour. A part selects its instrument with orch_engine_set_instrument(part, instrument). Selecting an instrument also sets its compatible family.

## Physical-model structure

Each note/player owns:
- A fractional-length digital waveguide delay line tuned from MIDI pitch.
- A family-specific loop gain and damping response.
- A short secondary delay resonator for body, bore, bell, bar or vocal-formant coloration.
- An excitation oscillator for pitch-locked air/lip/voice behaviour where needed.
- The existing adaptive envelope, articulation, glide, vibrato and Divisi player placement.

## Instrument-specific behaviour

- **Violin / viola / cello / double bass:** Helmholtz friction excitation. The bow force is a function of the relative velocity at the contact, `v_rel = v_bow - v_string`, through a falling coefficient `mu(v) = mu_s / (1 + |v|/v0)`. The falling slope is the negative damping that makes the string speak; the flat tail limits the amplitude. Only the fluctuation of the force enters the loop (its running mean is removed, because a mean is a DC term), the force is scaled by the loop length because a short string has a lower impedance, and the note starts with a 10 ms rosin bite.
- **Harp:** one-shot plucked excitation with a long natural ring; no continuous bow drive.
- **Flute / piccolo:** half-period, inverted-reflection air-column approximation, pitch-locked jet/edge excitation, and a higher body/bore resonance. Piccolo has a shorter, brighter profile.
- **Clarinet / oboe / bassoon:** the same compact air-column topology but with different damping, breath-noise, attack and body resonance profiles. These are not yet separate full reed/tone-hole solvers.
- **Trumpet / French horn / trombone / tuba:** half-period bore path with inverted reflection and a pitch-locked lip-pressure oscillator. Each instrument has its own body/bell resonance and envelope profile.
- **Marimba / xylophone / timpani:** one-shot strike excitation, short attack, stronger damping and a secondary inharmonic resonator. These are compact modal approximations, not full 2-D plate or membrane models.
- **Choir:** pitch-locked fundamental plus overtone excitation, formant-like smoothing and a sustained envelope.

The wind/brass half-period path is a low-cost closed/open air-column approximation. It is not a complete multi-section bore with tone holes, register vents, bell flare or measured reflection coefficients.

## Adaptive Playing and Divisi

The engine retains the per-part Himalia-inspired phrase interpreter and Divisi planner:
- Phrase timing, note intervals, gate duty, velocity trends, phrase peaks and density are tracked per part.
- Articulation resolves to legato, portamento, détaché, martelé, staccato, spiccato, pizzicato, flageolet, sul tasto, sul ponticello or col legno.
- Glide is applied only to connected notes and follows interval/duration intent.
- Vibrato begins after a delay and grows over the note; its depth is expressed as fractional delay rather than integer sample jumps.
- Divisi distributes a chord among up to four modeled players per part, with stable assignment, small detuning and pan differences.
- Every player has its own delay, envelope, vibrato phase, excitation and pan. The global pool is capped at 12 voices; each part has its own lower voice ceiling.

## Limitations

These are first-pass physical-model approximations, not finished acoustic instrument emulations. In particular:
- The bow/string contact uses a static friction law `mu(v_rel)`. It has no stick-slip hysteresis, no dependence on rosin state, temperature or contact pressure history, no bow position along the string, no bridge admittance and no string dispersion or coupling between strings.
- Wind and brass use compact single-bore approximations; reed stiffness, lip aperture, tone holes, register holes, bore taper and bell radiation need further work.
- Choir is a small harmonic/formant model, not a full vocal-tract waveguide.
- Percussion uses a compact secondary mode, not a multi-mode membrane/plate solver.
- Profile constants require listening tests and calibration on the actual FM1.
- The 12-voice global ceiling and Divisi player limits must be profiled on the target before increasing them.

## Validation

The host source-check workflow compiles the engine, adaptive interpreter, Divisi planner, part manager and FM1 callback with C11, -Wall, -Wextra and -Werror, and runs three host test suites. A successful host compile does not prove the JieLi target build or audio quality. A flashable release still requires the exact FM1 toolchain, packaging, and hardware measurements at 1/4/8/12 active voices, including CPU-block timing and RAM use.

## Validation on host (0.3.1)

Measured on host renders of this engine, 44.1 kHz, 128-frame blocks, one note at
a time, rendered with the same sources the firmware would use:

| Family | Pitch (autocorrelation) | Peak (Q23) | Note |
|---|---|---|---|
| strings | 110.0 / 262.5 / 668.2 Hz for A2/C4/E5 | 2.7–3.6 M | r 0.95–0.99 |
| strings, 2 players | 110.2 / 262.5 / 668.2 Hz | 1.9–2.7 M | r 0.94–0.99 |
| woodwinds | 110.2 / 262.5 / 668.2 Hz | 3.3–4.8 M | r 0.99–1.00 |
| brass | 110.2 / 262.5 / 668.2 Hz | 3.3–5.1 M | r 0.99–1.00 |
| mallets | 110.2 / 261.6 / 659.3 Hz | 0.13–0.6 M | decays on its own |

The tuning error is +4 to +23 cents (the loop filter is not phase linear); the
autocorrelation lag resolution at the top note is about 26 cents, so treat the
high-register number as an upper bound.

A register/velocity sweep is the test that matters for a friction model,
because the failure mode is not a wrong frequency but a string that stops
oscillating. C2, A2, C4, E5 and C6 at velocity 20, 40, 80 and 124 (20 points):
normalized autocorrelation at the expected period is 0.89 or better in every
case, and the peak grows 20–29× from pp to ff. DC sits at 3–28 k against peaks
of 0.1–2.8 M.

Three defects were found by measurement and fixed here:

- The first friction attempt injected the force with its DC term into a
  near-lossless loop: DC 2.74 M against a 2.97 M peak, f0/DC = 0, no pitch.
  The mean of the force is now removed before injection; the loop is not
  filtered, since that would shift the pitch.
- With a force that ignored string length, the high notes broke into noise:
  a short string has a lower impedance and was over-driven. The force is now
  scaled with the loop length.
- With a force that ignored bow speed behaviour, pp on the low strings was
  surface noise instead of a tone. Soft playing is now a lighter, slightly
  faster stroke, and the note is seeded with a short rosin bite.
- `orch_engine.c` was truncated mid-function in the 0.3.0 commit, and the
  instrument profile declared a `uint8_t` for Q15 friction values (22000 did
  not fit). Both are fixed, and `test_strings_are_not_dc` guards the DC case.

Voice stealing, glide state and the vibrato depth width were fixed by the
0.3.0 author in 5f91c1c and 4173e50 and are carried over here.
