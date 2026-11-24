# FM1 Orchestral Engine — architecture 0.3.0

## Core design

Polyphonic MIDI notes are routed through four configurable instrument parts. Each part owns its own Adaptive Playing state, active-note list, Divisi state, articulation, instrument selection and voice ceiling. The parts share one global 12-voice DSP pool. MIDI channels 1–4, 5–8, 9–12 and 13–16 are assigned to the four default parts; the channel masks can be reconfigured.

The audio path is:
MIDI / sequencer -> part routing -> Adaptive Playing -> Divisi planner -> per-player physical waveguide -> instrument body/bore resonator -> stereo voice mix -> master FX (chorus, ensemble, delay, shimmer reverb) -> stereo Q23 output.

The master FX are a single shared insert after the mix (`app/orch_fx.c`), off by
default; the panel's FX page controls them and the settings are part of the
saved record. They are documented with their ranges in [PANEL.md](PANEL.md).

## 0.3.0 instrument models

The instrument layer now distinguishes violin, viola, cello, double bass, harp, flute, piccolo, clarinet, oboe, bassoon, trumpet, French horn, trombone, tuba, marimba, xylophone, timpani and choir. Each profile has its own loop gain, damping, attack/release, body resonance, excitation and vibrato behaviour.

- Bowed strings use a Helmholtz friction law: a falling coefficient `mu(v_rel)` multiplied by the bow force, injected into the string's velocity loop with its DC term removed, scaled by the string length, and started with a short rosin bite.
- Harp and pizzicato use one-shot plucked/struck excitation and natural ring.
- Woodwinds use a half-period inverted-reflection air-column approximation with pitch-locked jet excitation.
- Brass uses a half-period bore approximation with pitch-locked lip-pressure excitation.
- Mallets/percussion use a short strike burst and an inharmonic secondary resonator.
- Choir uses a harmonic source with formant-like smoothing.

Each voice has its own primary delay line and secondary resonator. The model details and acoustic limitations are documented in [INSTRUMENT_WAVEGUIDES.md](INSTRUMENT_WAVEGUIDES.md).

## Adaptive Playing and Divisi

The engine retains the per-part Himalia-inspired phrase interpreter and Divisi planner. The interpreter tracks note spacing, rests, gate duty, intervals, phrase peaks, velocity trends and phrase density. It produces energy, attack/release, vibrato, articulation, glide and Divisi-density intentions.

Divisi is rendered, not merely planned: the planner assigns chord notes to up to four players per part. Every player receives an independent waveguide voice with its own delay state, envelope, vibrato phase, detuning and pan. The player cap and per-part voice ceiling constrain the result.

## Polyphony and CPU budget

All four parts share a fixed 12-voice pool. Each part has a configurable voice ceiling; each Divisi part has a player cap. The allocator prefers releasing/quiet voices and then steals the oldest voice only when necessary. The 12-voice limit is provisional: target-device timing and RAM measurements are still required before raising it.

The per-player model uses one 2048-sample primary delay and a 512-sample secondary resonator. Static allocation avoids heap use in the audio callback.

## Remaining work

- Calibrate instrument profiles by listening and measurement on FM1 hardware.
- Improve string bridge/body coupling, dispersion and independent player bow behaviour, and move from a static friction law with no hysteresis to a contact model with bow position, bow force history and rosin state.
- Improve reed, tone-hole, register and brass bell/lip modelling.
- Expand choir formants and percussion modal banks.
- Add panel/program-change mapping for instrument and articulation selection.
- Measure 1/4/8/12-voice block deadlines and RAM on the JieLi target, and the cost of each FX slot (about 116 KB of buffers in total). Host DSP behaviour tests exist in `tests/` (six suites, run by CI).
- Produce a flashable firmware package only after the exact FM1 target build succeeds.

## Validation status

The host CI compiles the engine, adaptive interpreter, Divisi planner, part manager, panel, FX chain and FM1 callback with C11, -Wall, -Wextra and -Werror, and runs the six host suites. Host compilation is not a target firmware build or hardware test. No flashable package is claimed by any branch.
