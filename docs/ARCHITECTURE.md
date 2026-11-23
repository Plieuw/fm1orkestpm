# FM1 Orchestral Engine — architecture 0.1

## Core design

The instrument is polyphonic first. One global voice allocator serves all accepted MIDI channels and the internal sequencer. MIDI channel identity is retained per voice so Note Off and sustain can be channel-aware. No voice is permanently reserved for a channel.

## Audio path

MIDI input -> bounded event queue -> voice allocator -> digital waveguide -> stereo Q23 block.

The FM1 Core renderer receives 128 frames per block at approximately 44.1 kHz. The audio callback uses fixed-size buffers and fixed-point arithmetic. No heap allocation, blocking calls, filesystem access or display operations belong in the render path.

## Voice allocation

The first implementation has a provisional ceiling of 12 voices. It reuses an idle voice, otherwise steals the quietest releasing voice, otherwise the oldest held voice. All MIDI channels share this pool. Twelve is not a measured hardware maximum; benchmark on device before raising it.

Each voice owns a 2048-sample delay buffer, loop filter, envelope and MIDI note/channel identity.

## First physical model

The first prototype is a simplified digital-waveguide string:
- delay line models travelling energy on a string;
- one-pole damping models frequency-dependent loss;
- noise excitation seeds the resonator and sustains a rough bowed-like tone while held;
- release closes excitation and allows the resonator to decay.

This is an initial physical-modeling scaffold, not yet a finished violin/bowed-string model. The next DSP pass must add a nonlinear bow/friction interaction, more accurate bridge/body coupling, and measured tuning across the MIDI range.

## MIDI

The raw FM1 MIDI callback parses channel-voice Note On, Note Off and CC64 sustain messages. All 16 channels feed the same polyphonic pool. MIDI program changes, pitch bend, channel pressure, per-channel expression and independent channel presets are not implemented yet.

## Sequencer

The source exposes a 16-step pattern API with up to four notes per step. Sequencer playback is disabled by default. The first timing implementation is deliberately basic: gate-duration scheduling, external MIDI clock, per-pattern length, swing, ratchets, transport and panel editing are still outstanding. The next pass must complete note-off timing before calling this a finished polyphonic sequencer.

## Validation plan

1. Host compile and tests for allocator, MIDI parser and sequence timing.
2. Build/package with the matching FM1 Core platform and JieLi PI32v2 toolchain.
3. Hardware tests at 1, 4, 8 and 12 held notes.
4. Measure audio-block overruns and peak render time.
5. Increase the voice ceiling only while preserving interrupt headroom.
