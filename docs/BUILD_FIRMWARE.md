# Building an FM1 firmware package

This repository targets the M-VAVE FM1 JieLi WL82 / AC791N PI32v2. It is
**not** an RP2040 build. The XIAO RP2040 used by the recovery workflow is not
the synth target.

## Required components

- Python 3.10 or newer.
- The matching FM1 Core checkout from https://github.com/rgrvmusic/fm1-core.
- The JieLi PI32v2 toolchain 2.5.3 (clang 4.0.1, target `pi32v2`, `-mcpu=r3`).
- The FM1 Core binary SDK and boot resources. Their redistribution terms are
  documented in that repository's `THIRD_PARTY.md`.

The toolchain and binary SDK are not vendored by this project. Do not replace
them with a generic ARM compiler or the RP2040 toolchain.

## Checkout layout

Keep the two repositories next to each other:

```text
workspace/
  fm1-core/
  fm1orkestpm/
```

Use the exact feature branch or PR head that you intend to build. For the
current panel/performance work that is
`feature/0.3.6-performance-controls`.

## Build and package

From `workspace/fm1-core`:

```sh
python3 tools/build.py --app ../fm1orkestpm/app --clean
python3 tools/build.py --app ../fm1orkestpm/app --package
```

If the toolchain is not under `~/.jieli/toolchain`, set
`FM1_PI32_TOOLCHAIN` to the toolchain directory containing `bin/clang` and
`bin/ld`. The package command writes `build/fm1.fwsc`.

## Before any device update

1. Confirm the exact FM1 Core commit, toolchain version and FM1 product identity.
2. Confirm the build completed and record the generated package SHA-256.
3. Keep a known-good stock Felucca recovery image and the recovery procedure
   available.
4. Do not flash a package built only on the host. The PI32v2 target build must
   succeed first, then the resulting image needs a controlled hardware test.

## Current validation boundary

The feature branch's GitHub Actions workflow compiles the application sources
against the FM1 Core API headers with strict host warnings and runs the host
tests. That does **not** execute the PI32v2 compiler, link the binary SDK,
produce a validated `.fwsc`, or test audio/panel timing on the physical FM1.
