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

- **Violin / viola / cello / double bass:** rosin-noise excitation scaled by the instrument's bow pressure and the adaptive energy, a per-voice DC blocker, and a different body-mode resonance for each instrument. Their loop gains and damping differ so the lower instruments sustain more darkly. The scalar loop-gain component is normalized per nominal round trip; the two-tap smoother remains a per-sample, pitch-dependent damping element and still needs calibration. A friction feedback is deliberately not used; see the validation section.
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
- Bow/string contact is not solved: the string is driven by rosin noise, and the Helmholtz friction curve, bridge admittance, string dispersion, coupled strings and measured body impulse responses are still missing.
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
| strings | 110.0 / 262.5 / 668.2 Hz for A2/C4/E5 | 0.8–2.1 M | r 0.83–0.95 |
| woodwinds | 110.2 / 262.5 / 668.2 Hz | 3.3–4.8 M | r 0.99–1.00 |
| brass | 110.2 / 262.5 / 668.2 Hz | 3.3–5.1 M | r 0.99–1.00 |
| mallets | 110.2 / 261.6 / 659.3 Hz | 0.13–0.5 M | decays on its own |

The tuning error is +4 to +23 cents (the loop filter is not phase linear); the
autocorrelation lag resolution at the top note is about 26 cents, so treat the
high-register number as an upper bound.

Two defects were found by measurement and fixed:

- The bowed string was a DC generator. The friction force was computed from the
  string's own output against a constant bow speed, which injects a fixed
  offset: measured DC 2.74 M against a 2.97 M peak, f0/DC = 0, no pitch at all.
  Strings are now driven by rosin noise scaled by the profile's bow pressure,
  and every voice has a DC blocker after the body colouration. After the fix:
  DC 2.4 k–24 k, pitch 110.0/262.5/668.2 Hz. The Helmholtz friction contact is
  still not ported, so this is a working excitation, not a solved bow model.
- `orch_engine.c` was truncated mid-function in the 0.3.0 commit, and the
  instrument profile declared a `uint8_t` for Q15 friction values (22000 did
  not fit). Both are fixed, and `test_strings_are_not_dc` guards the DC case.

Voice stealing, glide state and the vibrato depth width were fixed by the
0.3.0 author in 5f91c1c and 4173e50 and are carried over here.
