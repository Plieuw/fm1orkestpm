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

## Current status

- Orchestral DSP and panel modules: host-compiled and host-tested.
- HAWK-style target build: not integrated yet.
- .fwsc package: not produced.
- Browser installer: not ported yet.
- Physical FM1 update: not tested.

The next engineering milestone is the runtime integration needed to make the
HAWK/Felucca build script compile the Orchestral Engine, before publishing the
web installer.
