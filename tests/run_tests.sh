#!/bin/sh
# Host tests: no toolchain, no hardware. FM1_CORE points at the FM1 Core
# platform checkout (its headers define the application hooks).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CORE=${FM1_CORE:-"$ROOT/../fm1-core"}
CC=${CC:-cc}
CFLAGS="-std=c11 -O2 -Wall -Wextra -Werror -DFM1_HOST_TEST -I$CORE/include -I$ROOT/app"
OUT=${TMPDIR:-/tmp}/fm1orkestpm-tests
mkdir -p "$OUT"

if [ ! -f "$CORE/include/fm1.h" ]; then
    echo "FM1 Core headers not found in $CORE (set FM1_CORE)" >&2
    exit 2
fi

status=0
for test in orch_adaptive orch_divisi orch_stage orch_engine orch_arp orch_ui orch_fx; do
    case "$test" in
        orch_adaptive) src="$ROOT/app/orch_adaptive.c" ;;
        orch_divisi)   src="$ROOT/app/orch_divisi.c $ROOT/app/orch_stage.c" ;;
        orch_stage)    src="$ROOT/app/orch_adaptive.c $ROOT/app/orch_divisi.c $ROOT/app/orch_part.c $ROOT/app/orch_stage.c $ROOT/app/orch_engine.c" ;;
        orch_engine)   src="$ROOT/app/orch_adaptive.c $ROOT/app/orch_divisi.c $ROOT/app/orch_part.c $ROOT/app/orch_stage.c $ROOT/app/orch_engine.c" ;;
        orch_arp)      src="$ROOT/app/orch_adaptive.c $ROOT/app/orch_divisi.c $ROOT/app/orch_part.c $ROOT/app/orch_stage.c $ROOT/app/orch_engine.c" ;;
        orch_ui)       src="$ROOT/app/orch_adaptive.c $ROOT/app/orch_divisi.c $ROOT/app/orch_part.c $ROOT/app/orch_stage.c $ROOT/app/orch_engine.c $ROOT/app/orch_ui.c $ROOT/app/orch_fx.c" ;;
        orch_fx)       src="$ROOT/app/orch_fx.c" ;;
    esac
    # shellcheck disable=SC2086
    if ! $CC $CFLAGS "$ROOT/tests/${test}_test.c" $src -o "$OUT/$test"; then
        echo "build failed: $test" >&2
        status=1
        continue
    fi
    if ! "$OUT/$test"; then status=1; fi
done

exit $status
