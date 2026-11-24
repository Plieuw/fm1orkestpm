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

The initial code change replaces the old part-ceiling/fair-share allocator with
this global eight-slot policy. The engine now exposes eight parts, each mapped
to one default MIDI channel (1-8), and the ARP/phrase state is expanded to eight
independent parts. The panel's fourth encoder cycles all eight parts; B1-B4
remain shortcuts for parts 1-4.

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
created for that note must release together. Repeated instances of the same pitch are tracked as separate note instances
(FIFO note IDs), so an older note-off cannot terminate a newer retrigger.
Sequencer note-offs retain their own note IDs and cannot accidentally release
a manually played note of the same pitch. Sustain-pedal release must apply
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

This branch contains the first allocator step only. Complete target validation and physical FM1 testing are still required before this can be
called a complete eight-part implementation or flashed as a test build.


## Adaptive phrase-shaped ADSR refinement (0.3.14 work)

The authored ADSR remains the base contour, but adaptive intent now shapes its
Decay and Sustain stages in a bounded, tempo-independent way. Connected notes
receive a longer decay and slightly higher sustain for legato continuity;
phrase openings ease the decay in; down-bows and new phrase peaks speak more
firmly; phrase endings lower sustain and shorten decay to create a natural
release into the next phrase. The sustain adjustment also follows the adaptive
energy estimate within a narrow +/-4 percentage-point range for bowed strings,
winds, brass and choir. Percussive instruments retain their automatic decay
and are not given a synthetic held sustain.

The slow bow/breath expression contour also follows articulation intent:
connected phrases breathe more slowly, while down-bow accents increase its
rate slightly. It remains free-running and is not locked to sequencer BPM.
ARP, phrase sequencer and live MIDI share the same note-start path and therefore
receive identical adaptive envelope behavior. Existing ADSR controls, panel
layout and preset workflow are unchanged.

This is fixed-point note-on work; no additional per-sample modulation branch
or new memory allocation is introduced. CI must still confirm compilation and
tests. Physical FM1 listening and CPU-headroom checks remain outstanding.


## Connected legato transitions (next-step implementation)

For connected notes in the same part and divisi player, the engine now reuses
the existing waveguide voice instead of starting a fresh delay line. It keeps
the resonator delay/body state and current envelope level, then retunes from
the current loop length toward the new pitch using the adaptive glide time.
The envelope receives a short 12 ms transition ramp rather than restarting
from zero. A recently released source voice can be reused while its envelope
is still above 20%, which covers the short articulation gap common in played
legato lines. Old note-off events cannot release the new note because the
voice is rebound to the new note ID.

This behavior is restricted to connected, non-percussive notes of the same
part, family and divisi-player slot. It does not borrow a voice from another
part or turn a mallet strike into a sustained transition. Live MIDI, ARP and
sequencer inherit it through the shared note-start path. A host regression test
checks that a connected transition preserves audible energy and does not add a
second voice. Full target-build, CPU and FM1 listening validation remain
required.


## Overlapping legato and section-transition refinement

The adaptive interpreter now tracks the number of currently held notes. A new
note arriving while another note in the same part is still held is treated as
part of the current phrase rather than a phrase restart, and can request a
connected transition even when there is no gap to measure. Note-off decrements
the held-note count; phrase reset clears it.

For non-mallet families, connected notes reuse the existing player waveguide
and retune it rather than starting from silence. The new bow/breath excitation
is blended in from 72% to full level over a short family-sensitive transition
(roughly 18-33 ms), while the resonator continues ringing. Pitch glide and the
existing envelope handover remain active. This avoids a hard re-trigger edge
and preserves the older note's resonant body during the change.

A regression test covers overlapping notes with a one-player part, confirms
that the same voice is reused, and checks that releasing the older note does
not release the reassigned voice. ARP and sequencer notes use this same path.
Target audio quality and CPU headroom still require listening on physical FM1.


### Connected-note priority in divisi
The divisi planner normally preserves held chord assignments. For a connected
melodic note, the engine now promotes the newest note onto the final active
desk when the planner's hysteresis would otherwise leave all desks on older
held notes. Other desks retain their chord tones. The MIDI and sequencer paths
use the same promotion helper, allowing a true overlapping legato line without
breaking the shared voice pool or note-ID release isolation.


## Instrument loudness matching

A per-voice output-level follower now applies gentle automatic loudness
compensation after the physical model and before the final expression/VCA mix.
It estimates the rectified acoustic level and slowly steers each instrument
toward a shared target. The target is proportional to incoming MIDI velocity,
so quieter and louder playing dynamics are retained rather than flattened.
Compensation is bounded to approximately +/-6 dB, uses a faster rise and slower
fall to avoid abrupt gain pumping, and leaves the model's spectrum, damping,
articulation and spatial placement unchanged. It is reset on a fresh note and
retained across a connected legato handover. Host checks cover the gain limits
and velocity scaling. Final matching still needs listening across all factory
instruments and registers on the FM1; this is approximate balancing, not a
claim of laboratory-calibrated LUFS equality.


### Register-aware loudness refinement

The velocity-aware output follower now uses a restrained, continuous pitch-register
target contour as well as its per-note level estimate. The lowest register receives
up to +8% target level, the central register remains the reference, and the extreme
upper register receives up to -6%. Linear ramps avoid abrupt register boundaries.
The existing gain safety range remains unchanged, and MIDI velocity still scales
the target. A connected-note fallback also ensures that a connected lead note is
realized if divisi hysteresis temporarily leaves the new note unassigned; it uses
the last active desk rather than silently dropping the melody. Host tests check
the register contour and preserve the existing overlapping-note ownership test.


### Note-off and short-gap legato ownership

A Note Off first marks the oldest held instance of that pitch as released. The
voice remains eligible as a short-gap legato source while the adaptive phrase
interpreter considers the next note connected. Handover changes the voice's
note ID to the new note, so a later Note Off for the old note ID cannot release
the reassigned voice. Candidate selection no longer uses a fixed envelope
threshold: a quiet released note can still carry useful resonator state.
Mallets remain excluded from waveguide legato reuse. Host coverage checks both
short-gap reuse and overlapping-note old-Note-Off isolation.
