#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The browser build: app/fm1_app.c, the engine and web/emu/emu.c (the board's
# place) to build/emu/orch.wasm. No SDK and no JieLi toolchain are involved:
# the FM-1 Core headers are the only thing it needs.
#
#   FM1_CORE=../fm1-core sh web/emu/build.sh
#
# A compiler for wasm32 is needed: clang with wasm-ld, or `pip install
# ziglang==0.13.0` (Zig ships the same clang front end and lld).
set -e
cd "$(dirname "$0")/../.."
FM1_CORE=${FM1_CORE:-../fm1-core}
if [ ! -f "$FM1_CORE/include/fm1.h" ]; then
    echo "web/emu/build.sh: set FM1_CORE to an fm1-core checkout (no fm1.h in $FM1_CORE/include)" >&2
    exit 1
fi

if [ -n "$CC" ]; then
    :
elif command -v clang >/dev/null 2>&1; then
    CC=clang
elif python3 -c 'import ziglang' >/dev/null 2>&1; then
    CC="python3 -m ziglang cc"
else
    echo "web/emu/build.sh: no wasm compiler. Install lld+clang, or: pip install ziglang==0.13.0" >&2
    exit 1
fi

SRC="web/emu/emu.c web/emu/string.c app/fm1_app.c app/orch_engine.c app/orch_fx.c
     app/orch_adaptive.c app/orch_divisi.c app/orch_part.c app/orch_stage.c app/orch_ui.c"

EXPORTS="emu_init emu_tick emu_render emu_button emu_key emu_turn emu_knob
         emu_battery emu_note emu_fb emu_audio emu_flash emu_leds emu_frames
         emu_fb_width emu_fb_height emu_voices emu_gain"
LINK=""
for e in $EXPORTS; do LINK="$LINK -Wl,--export=$e"; done

mkdir -p build/emu
# shellcheck disable=SC2086
$CC --target=wasm32-freestanding -O2 -nostdlib -ffreestanding -fno-builtin \
    -DFM1_HOST_TEST -Iweb/emu/include -I"$FM1_CORE/include" -Iapp \
    $SRC -Wl,--no-entry $LINK \
    -o build/emu/orch.wasm
printf 'web/emu: build/emu/orch.wasm (%s B)\n' "$(wc -c < build/emu/orch.wasm)"
