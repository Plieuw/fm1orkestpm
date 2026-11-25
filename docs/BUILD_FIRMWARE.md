# HAWK CHUA-compatible FM1 build and browser installer

## Decision

The Orchestral Engine will use the same kind of release pipeline as stable
HAWK CHUA: native JieLi PI32v2 firmware build, .fwsc package, browser installer
over Web MIDI/SysEx, GitHub Actions artifacts/releases, and GitHub Pages.
Reference: HAWK CHUA main at e750df6a2e13490a4f01b84d3f9c553f4612163f
(not a beta branch).

Reference implementation:
- build.sh, tools/build.py, tools/fm1pkg_make.py
- tools/get_toolchain.sh
- .github/workflows/firmware.yml
- web/index_pkg.html, web/fm1pkg.js, web/fm1ota.js, web/make_site.py

## Important difference in the current Orchestral Engine repository

The current repository contains app/*.c application modules and an adapter
that includes fm1.h, fm1_app.h, and fm1_engine_control.h. Its current CI checks
these against the public FM1 Core API headers and runs host tests.

It does NOT currently contain the HAWK/Felucca firmware tree:
firmware/crt0.S, firmware/app.ld, firmware/hal/, firmware/loader/, or the
complete firmware/src/ runtime. Therefore the HAWK tools/build.py cannot yet
build this repository simply by pointing it at app/. The earlier FM1 Core
command shown in older project notes is not a verified target-build route and
must not be treated as producing a valid FM1 image.

## Required integration work

1. Choose the runtime base. Port the Orchestral Engine into the actual FM1
   firmware runtime that supplies the FM1 hardware abstraction, audio
   interrupt, MIDI, display, storage, and update entry. Keep the current app/
   engine modules isolated and connect them through a thin adapter. The stable
   HAWK/Felucca runtime is the target reference, not a drop-in source dependency.
2. Audio and control integration. Map the engine render callback to the real
   FM1 audio block format and sample clock; map MIDI and physical controls to
   actual runtime callbacks. Verify stack/RAM use and worst-case audio time on
   the PI32v2 target.
3. Preserve update support. The running firmware must implement the M-UPGRADE
   entry expected by the HAWK browser updater and include a matching update
   loader in the .fwsc. A web page alone cannot install a package.
4. Build the package. Use the same pinned JieLi toolchain and AC79 SDK inputs
   as HAWK CHUA, with reproducible hashes and licence notices. Assign product
   identity and package metadata deliberately; do not copy HAWK's FM-1_007
   identity blindly.
5. Port the web installer. Adapt the GPL-3.0 HAWK installer page and its
   package parser / WebMIDI updater. Preserve the two-stage request/response
   update, device identity check, progress/error handling, and resume support.
   Replace HAWK branding, version, package URL and product identity. Keep the
   official V15 recovery option only if its exact package validation remains
   intact and tested.
6. Automate releases. GitHub Actions should build the target package, run host
   and package/update tests, create the installer site, upload the .fwsc
   artifact, and deploy Pages from the release branch/tag. PR builds must not
   publish Pages.

## HAWK CHUA build characteristics to match

- JieLi PI32v2 clang toolchain (Linux x86-64).
- AC79NN SDK version AC79NN_SDK_V1.2.1_2023-12-13; only specific required
  binary resources are staged, with their hashes checked.
- build.sh invokes the target build/package script and generates the
  installable .fwsc containing app plus OTA loader.
- CI runs host tests and target build, then assembles a self-contained browser
  installer.
- The installer uses Web MIDI with SysEx permission in Chrome/Edge. It detects
  the FM-1, transfers the package in bounded chunks in response to device read
  requests, waits for the update loader, writes the image, and verifies the
  device returns with the expected identity.
- GitHub Pages hosts the installer; GitHub Releases host versioned packages
  and checksums.

## Safety / release gates

Do not publish an Install button pointing at a placeholder or host-only build.
Do not call the package firmware-ready until:
- PI32v2 compile and link succeeds;
- package structure, product identity, loader marker and checksums validate;
- updater protocol tests pass against a simulated device;
- the first physical install and reboot are confirmed on an FM1 with a known
  recovery route available.

## Current status (0.5.0)

- Orchestral DSP and panel modules: host-compiled and host-tested (eight
  suites).
- HAWK-style target build: **integrated**. `tools/prepare_hawk_runtime.py`
  overlays the pinned HAWK checkout; CI's `hawk-target-build` job links it.
  Measured on the 0.5.0 build: image 171 456 B, RAM .data+.bss 40 756 of
  73 728 B, pool 329 444 of 368 640 B, `.ram_text` without calls, one core.
- What runs on the FM-1 in that image: the orchestral engine, its FX and
  the orchestral panel (`app/orch_ui.c` through `runtime/hawk/`); HAWK's
  boot, watchdog, boot-loop guard, USB/TRS MIDI, storage, M-UPGRADE update
  entry and the OCT- + OCT+ UBOOT combination. HAWK's synth, ARP/KARMA and
  menus are no longer called (the linker drops them).
- .fwsc package: **produced by CI** (artifact `fm1-orchestral-0.5.0-beta`,
  and on a push to a `beta/0.5*` branch the pre-release `v0.5.0-beta`).
  The package identity is still HAWK CHUA's `FM-1_007`, so HAWK's installer
  accepts it and a HAWK CHUA release can be installed over it again.
- Browser installer: not ported. Install with HAWK CHUA's command-line
  installer (below).
- Physical FM-1 update: **not tested.** Audio-block time on the PI32v2 with
  eight voices, the FX and the panel drawing is unmeasured.

## Installing the beta (at your own risk)

1. Have a way back: HAWK CHUA's release or M-VAVE's V15 `FM-1.fwsc`, and know
   the UBOOT route — hold OCT- and OCT+ for 5 s (the screen counts down), or
   use FM-1-transporter over UBOOT if an install is cut off.
2. On a computer with Python 3: `pip3 install mido python-rtmidi`, then from a
   HAWK CHUA checkout
   `python3 tools/fm1_install.py fm1-orchestral-0.5.0-beta.fwsc`
   with the FM-1 on a USB data cable. The same tool installs a HAWK CHUA or
   the stock V15 file to go back.
3. First things to check on the device: the splash, then the STAGE page with
   the hall; a key sounds on part 1 (violin); the MASTER pot sets the level;
   SAVE, power off and on, and the settings come back; OCT- + OCT+ held shows
   the countdown and letting go repaints the panel.
