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

- **Violin / viola / cello / double bass:** sustained nonlinear bow-slip/friction approximation, a small rosin-noise component, and a different body-mode resonance for each instrument. Their loop gains and damping differ so the lower instruments sustain more darkly.
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
- Bow/string contact is a simplified friction curve; bridge admittance, string dispersion, coupled strings and measured body impulse responses are not yet modeled.
- Wind and brass use compact single-bore approximations; reed stiffness, lip aperture, tone holes, register holes, bore taper and bell radiation need further work.
- Choir is a small harmonic/formant model, not a full vocal-tract waveguide.
- Percussion uses a compact secondary mode, not a multi-mode membrane/plate solver.
- Profile constants require listening tests and calibration on the actual FM1.
- The 12-voice global ceiling and Divisi player limits must be profiled on the target before increasing them.

## Validation

The host source-check workflow compiles the engine, adaptive interpreter, Divisi planner, part manager and FM1 callback with C11, -Wall, -Wextra and -Werror. A successful host compile does not prove the JieLi target build or audio quality. A flashable release still requires the exact FM1 toolchain, packaging, and hardware measurements at 1/4/8/12 active voices, including CPU-block timing and RAM use.
