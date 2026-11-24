# Shared eight-voice allocation and eight-part expansion

## Agreed target

The FM1 Orchestral Engine should expose eight independent instrument parts while
retaining exactly eight physical-model mono voices. The voices are a global
resource, not eight voices per part and not a fixed voice allotment per part.
Every active voice is panned independently in the final stereo mixer.

## Allocation policy

1. Allocate an inactive voice first.
2. If all voices are active, steal the quietest voice already in its release
   stage. This minimizes discontinuities and preserves held notes.
3. If no release tail exists, use last-note priority: steal the oldest held
   note, preserving the newest musical information.
4. A part must not reserve voices or impose a private voice ceiling.
5. A part's divisi/player cap remains a musical arrangement limit for one
   incoming note; it is not a reservation of hardware voices.

The initial code change in this branch replaces the old part-ceiling/fair-share
allocator with this global eight-slot policy.

## Eight-part MIDI/control mapping

- Eight parts, one MIDI channel per part, using MIDI channels 1 through 8 by
  default. User configuration can later remap the 16 MIDI channels.
- Each part retains independent instrument, articulation, adaptive state,
  divisi settings, ADSR/VCA, LFO, ARP and sequencer configuration.
- The existing FM1 panel workflow is preserved. The part-selection encoder
  cycles through all eight parts; existing B1-B4 shortcuts continue selecting
  parts 1-4. No new panic control or new top-level page is introduced.
- Factory scenes must define valid instrument/articulation/divisi/envelope/LFO
  data for all eight parts.

## Note-off correctness

A note-off must release only the matching part/channel and pitch. Divisi voices
created for that note must release together. Repeated instances of the same
pitch must be tracked as separate note instances (FIFO note IDs) so an older
note-off cannot terminate a newer retrigger. Sustain-pedal release must apply
only to the corresponding MIDI channel/part.

## Performance and acceptance checks

- The hard voice count remains exactly eight; no dynamic allocation.
- No floating point or new work in the audio ISR.
- Test 1, 4 and 8 simultaneously active parts, with overlapping chords.
- Test voice stealing while all eight voices are held, and while release tails
  are present.
- Test repeated same-pitch Note On/Off, sustain pedal, divisi and call/response.
- Verify panning is retained when a voice is reassigned.
- Measure CPU/RAM and verify audio output on the FM1 hardware.
- Preserve M-Flasher packaging and boot-area protections.

## Scope status

This branch contains the first allocator step only. The public part count,
one-channel-per-part routing, eight-part panel/preset data, repeated-note ID
tracking, and target regression tests are still required before this can be
called a complete eight-part implementation or flashed as a test build.
