#!/bin/sh
# Host measurements (not pass/fail). FM1_CORE points at the FM1 Core checkout.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CORE=${FM1_CORE:-"$ROOT/../fm1-core"}
CC=${CC:-cc}
OUT=${TMPDIR:-/tmp}/fm1orkestpm-measure
mkdir -p "$OUT"

if [ ! -f "$CORE/include/fm1.h" ]; then
    echo "FM1 Core headers not found in $CORE (set FM1_CORE)" >&2
    exit 2
fi

$CC -std=c11 -O2 -DFM1_HOST_TEST -I"$CORE/include" -I"$ROOT/app" \
    "$ROOT/tests/measure.c" "$ROOT/app/orch_engine.c" "$ROOT/app/orch_adaptive.c" \
    "$ROOT/app/orch_divisi.c" "$ROOT/app/orch_part.c" "$ROOT/app/orch_stage.c" \
    "$ROOT/app/orch_performance.c" "$ROOT/app/orch_resonance.c" "$ROOT/app/orch_cc.c" "$ROOT/app/orch_fx.c" -o "$OUT/measure" -lm
exec "$OUT/measure" "$@"
