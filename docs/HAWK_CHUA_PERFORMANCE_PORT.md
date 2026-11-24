# HAWK CHUA ARP / phrase sequencer reference and FM1 port

## Stable source selected

The source was read from the repository's **`main` branch**, not from any
`beta/*` branch.

- Repository: https://github.com/Plieuw/hawk-chua
- Stable source commit: `e750df6a2e13490a4f01b84d3f9c553f4612163f`
- Commit date: 2026-11-17
- `firmware/src/arp.c` blob: `bf3cae6d503dacd0d354bb8539568160a7e07147`
- `firmware/src/karma.c` blob: `1dc46323d069596d284c85ed1752c5f48c97bc67`
- `firmware/src/keys.c` blob: `587b320190166718f565b8643aec0c2341d37556`
- `firmware/src/sys.h` blob: `b21477ae87960eb5f00b4329fa3aaebdd91219f`

The selected stable branch and commit are deliberately pinned in this note.
The beta branches are not used as a source.

## Behavior studied

The HAWK CHUA implementation combines:

- Held-note collection and latch/hold behavior.
- An arpeggiator with ordered and step-pattern algorithms.
- Ten note subdivisions, internal tempo or MIDI realtime clock, gate and swing.
- Velocity and gate grooves.
- A phrase generator whose Generated Effects combine rhythm, note-series
  selection, index movement, density, octave range, clusters, repeats,
  accents and duration.
- ARP/phrase interlocking: OR, AND, XOR, NOR, ALT and S/R.
- Chord and scale note quantization.
- MIDI START / CONTINUE / STOP / CLOCK transport behavior.

## FM1 Orchestral Engine implementation

The new FM1 implementation is in:

- `app/orch_performance.h`
- `app/orch_performance.c`
- Integration in `app/orch_engine.c`
- FM1 panel page and controls in `app/fm1_app.c`
- Host tests in `tests/orch_performance_test.c` and
  `tests/orch_engine_test.c`

It is a new bounded implementation of the musical behavior, not a copy of the
HAWK CHUA source tables or implementation. Its patterns are authored for this
orchestral engine.

### Implemented in this first port

- Per-part held-note capture (up to 16 notes) and optional latch.
- 31 ARP types: 15 order/range combinations plus 16 compact step-pattern
  families.
- Ten subdivisions, 40–250 BPM, internal sample clock and MIDI realtime
  START / CONTINUE / STOP / CLOCK input.
- Gate, swing, eight velocity grooves and eight gate grooves.
- 46 generated-phrase slots. Each has an authored rhythm mask and uses
  parameterized note-series/index generation, density, range, cluster, accent,
  repeat and gate controls.
- OR / AND / XOR / NOR / ALT / S-R stream logic.
- Chord, inferred major/minor and scale quantization.
- Generated note events enter the same instrument, Adaptive Playing and
  Divisi realization path as normal MIDI input.
- Fixed arrays and bounded event queues; no allocation in the audio renderer.

### Differences and next work

This is a functional first implementation, not a line-for-line reproduction
of every HAWK CHUA ARP/KARMA detail. The 46 phrase slots use this project's
own rhythm masks and parameterized generator; they are not the original
hand-authored GE definitions. This first pass also does not yet include
sample-spread strumming, KARMA-style parameter envelopes, per-note pan
automation, generated MIDI output, or the original ARP's complete pattern
catalogue.

External MIDI realtime bytes are accepted by the application's short-message
path. The FM1 Core `groove_midi_transport()` and `groove_midi_clock()` hooks
are also connected to the scheduler's transport and tempo queues. Confirm on
the device that the runtime delivers every DIN F8 pulse and START/CONTINUE/STOP
through those hooks; host compilation cannot prove the physical TRS route.

## FM1 panel controls

Open the performance page with button bit 13.

- Button bits 0–3: select part.
- Button bit 4: panic.
- On the performance page, bits 5–6 cycle velocity and gate groove.
- Bits 7–8 toggle ARP and phrase playback.
- Bit 9 toggles latch; bit 10 cycles logic; bit 11 toggles external clock;
  bit 12 cycles note quantization.
- Encoder 1 selects the ARP type when ARP is on, otherwise the phrase slot.
- Encoder 2 changes BPM; encoder 3 changes subdivision; encoder 4 changes swing.
- The main knob changes gate length on the performance page.

The physical button-bit labels still require verification on an FM1.

## Validation

Host source compilation and host regression tests are required by CI.
Passing host tests do not prove the JieLi PI32v2 target build, audio deadline,
RAM headroom, MIDI clock routing on the physical device, or panel numbering.
No flashable firmware package is claimed by this port.
