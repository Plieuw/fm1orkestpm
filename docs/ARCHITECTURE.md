# FM1 Orchestral Engine — architecture 0.3.0

## Design target

Polyphony remains the first priority. External MIDI channels share one global note allocator, so a channel does not permanently reserve voices. The sequencer is a separate source and uses that same pool. The current ceiling remains 12 voices until target-device profiling proves a higher count safe.

## Audio path

MIDI -> bounded event queue -> shared polyphonic allocator -> instrument-family waveguide/exciter -> secondary body/bore resonator -> stereo Q23 output.

All voice and resonator memory is statically allocated. The render callback performs no heap allocation or blocking I/O.

## 0.3.0 implementation

- Channel-aware MIDI Note On/Off and CC64 sustain.
- Twelve provisional shared voices.
- Per-channel instrument-family assignment API.
- Separate excitation paths for bowed strings, plucked strings, woodwind, brass, choir and percussion.
- Per-family loop gain, damping, attack/release, vibrato, bow/breath and body coloration profiles.
- Fractional-delay vibrato in cents and a short legato delay transition.
- A secondary resonator per voice for string-body, air-column, brass-bore, choir-formant or percussion-mode coloration.
- Adaptive phrase timing/density memory per MIDI channel.
- Six-player Divisi planning API and 16-step internal polyphonic sequencer retained from 0.2.

## Instrument model scope

The models are compact first-pass physical-model approximations. The strings use a delay-line string loop with nonlinear bow/friction excitation or a shaped pluck burst, plus family-specific body resonances. Woodwind and brass use a half-period, inverted-feedback air-column/bore approximation with pitch-locked jet or lip excitation. Choir uses harmonic/formant-like synthesis. Percussion uses a short shaped-noise impulse and a secondary inharmonic resonator.

The model details and limitations are described in [INSTRUMENT_WAVEGUIDES.md](INSTRUMENT_WAVEGUIDES.md).

## Polyphony, Divisi and CPU

All MIDI channels share one global voice pool. Divisi remains a deterministic six-player planning layer, not six additional rendered players. It must be connected to a measured CPU budget before it can allocate more modeled subvoices. This avoids silently multiplying DSP cost and protects note polyphony.

The current 12-voice ceiling is provisional. Each voice has a 2048-sample primary delay and a secondary resonator of up to 512 samples, plus filter/envelope state. RAM and audio-block deadline must be measured on the FM1 before increasing the voice count. Instrument loop loss is specified per round trip and converted once to a Q23 per-sample gain at note start; this prevents pitch register from changing decay time. The conversion has a one-step minimum loss so the delay loop is never marginally lossless.

## Remaining work

- Instrument-family/program changes and UI/controller mappings.
- External MIDI clock/transport, pitch bend, channel pressure and expression.
- More detailed string dispersion, bridge/body coupling and per-player bow control.
- Tone-hole/register modelling for woodwinds and lip/reed nonlinear calibration for brass.
- Full choir vocal-tract formants and richer percussion modal banks.
- Divisi rendering behind a measured CPU budget.
- Automated host DSP tests and actual FM1 hardware listening/measurement.

## Validation status

A host compiler pass is not a target firmware build. A flashable beta requires the exact FM1/JieLi PI32v2 toolchain, packaging, and hardware tests at 1/4/8/12 simultaneous notes. Check pitch, amplitude, transients, sustain/release, RAM and audio-block timing before treating any voice count as supported.
