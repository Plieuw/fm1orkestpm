# Instrument waveguide models — 0.3.0

This beta replaces the single generic excitation path with instrument-family profiles and distinct excitation/resonator behaviour. The implementation is fixed-point and allocation-free in the audio callback.

## Shared physical-model structure

Each active note owns:
- A tuned circular travelling-wave delay line.
- A one-pole loss/damping state.
- A short secondary resonator for body/bore coloration.
- A fractional-delay pitch modulation path for low-depth vibrato.
- A soft envelope and a per-voice excitation state.

The secondary resonator is deliberately short (maximum 511 samples) to constrain RAM and CPU. All state is allocated statically.

## Family-specific behaviour

| Family | Main resonator | Excitation | Secondary resonance |
|---|---|---|---|
| Violin | Long, high-feedback string loop | Continuous nonlinear bow/friction approximation plus low noise | Body mode around 500 Hz |
| Viola | String loop with slightly darker damping | Bow/friction approximation | Body mode around 350 Hz |
| Cello | Longer/softer string response | Bow/friction approximation | Body mode around 220 Hz |
| Bass | Longer decay and darker response | Bow/friction approximation | Body mode around 110 Hz |
| Woodwind | Half-period air-column delay with inverted reflection | Breath noise plus pitch-locked jet/edge oscillator | Bore overtone resonance |
| Brass | Half-period bore delay with inverted reflection | Pitch-locked lip oscillator coupled to bore pressure | Bell/bore overtone resonance |
| Choir | Sustained resonator | Two phase-related harmonic components and formant-like filtering | Formant/body resonance |
| Percussion | Short, more heavily damped resonator | Shaped noise impulse | Inharmonic membrane-like secondary mode |

The wind-family delay uses a half-period path with inverted feedback as a low-cost approximation of a closed/open air column. It is not a complete multi-bore clarinet/flute model.

## Articulation

ORCH_MODEL_WAVEGUIDE_STRING selects sustained/bowed string excitation. ORCH_MODEL_PLUCKED_STRING selects a shaped one-shot excitation for string-family instruments; the envelope then decays naturally. Percussion also uses a one-shot excitation and natural decay.

The family profile determines loop gain, damping, attack/release scale, vibrato range, bow pressure, breath-noise amount and body mix. MIDI channel-to-family assignment is exposed through orch_set_channel_family(channel, family). All channels still share the same global polyphonic voice pool.

## Adaptive Playing

Phrase timing and density are tracked per MIDI channel. Adaptive values shape attack/release, vibrato depth and tremolo depth. Vibrato is applied as a small fractional-delay change in cents rather than jumping the read pointer by whole samples.

## Divisi and polyphony

The six-player Divisi planner remains a planning layer. It is not yet six simultaneous independent player models. It must be connected to a measured CPU budget before it can allocate extra modeled players. MIDI note polyphony remains the first priority.

## Limitations and validation

These are first-pass physical-model approximations, not finished instrument emulations. In particular:
- The bowed-string friction model is simplified and does not yet model a calibrated nonlinear bow/string contact curve, bridge admittance, string dispersion or independent player bow control.
- Woodwind and brass use compact single-bore approximations; no register holes, tone holes, bell flare mesh or full lip/reed pressure solver yet.
- Choir uses harmonic/formant synthesis rather than a full vocal-tract waveguide.
- Percussion uses a compact resonator pair, not a plate/membrane modal bank.
- The body resonators and all profile constants require listening tests and measurement on the FM1.

Before a flashable release, run the host compile workflow, build with the exact FM1/JieLi toolchain, and measure audio-block execution time and RAM on hardware at 1/4/8/12 active notes. Do not raise the voice ceiling until measured headroom is confirmed.

## Validation on host (0.3.1)

Measured on host renders of this engine, 44.1 kHz, 128-frame blocks, one note
at a time, rendered with the same sources the firmware would use:

| Family | Pitch (autocorrelation) | Peak (Q23) | Note |
|---|---|---|---|
| strings | 110.2 / 262.5 / 668.2 Hz for A2/C4/E5 | 0.6–1.7 M | was broken, see below |
| woodwinds | 110.2 / 262.5 / 668.2 Hz | 3.3–3.7 M | |
| brass | 110.2 / 262.5 / 668.2 Hz | 3.3–3.9 M | |
| mallets | 110.2 / 264.1 / 668.2 Hz | 0.13–0.5 M | decays on its own |

The tuning error is +4 to +23 cents (the loop filter is not phase linear); the
autocorrelation lag resolution at the top note is about 26 cents, so treat the
high-register number as an upper bound.

Two defects were found by measurement and fixed here:

- The bowed string was a DC generator: a linear friction force fed back from
  the string output produced a constant offset of 2.74 M against a 2.97 M peak
  and no fundamental at all. Strings are now driven by rosin noise scaled by
  the profile's bow pressure, and every voice has a DC blocker. The Helmholtz
  friction model remains unported.
- `orch_engine.c` was truncated mid-function in the 0.3.0 commit, and the
  instrument profile struct declared a `uint8_t` for Q15 friction values
  (22000 did not fit). Both are fixed, and the host tests now guard the DC
  case so it cannot come back silently.
