#!/bin/sh
# What the panel costs to draw, per page: fill calls (one window command and one
# transfer each on the board), pixels, text blits, and the host's own time. Not
# pass/fail: it prints the numbers the notes in docs/SCREEN.md are based on.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CORE=${FM1_CORE:-"$ROOT/../fm1-core"}
CC=${CC:-cc}
OUT=${TMPDIR:-/tmp}/fm1orkestpm-bench
mkdir -p "$OUT"

if [ ! -f "$CORE/include/fm1.h" ]; then
    echo "FM1 Core headers not found in $CORE (set FM1_CORE)" >&2
    exit 2
fi

$CC -std=c11 -O2 -DFM1_HOST_TEST -I"$CORE/include" -I"$ROOT/app" \
    "$ROOT/tests/panel_bench.c" "$ROOT/app/orch_adaptive.c" \
    "$ROOT/app/orch_divisi.c" "$ROOT/app/orch_part.c" \
    "$ROOT/app/orch_stage.c" "$ROOT/app/orch_engine.c" \
    "$ROOT/app/orch_ui.c" "$ROOT/app/orch_fx.c" "$ROOT/app/orch_performance.c" \
    "$ROOT/app/orch_scenes.c" "$ROOT/app/orch_resonance.c" -o "$OUT/panel_bench"
exec "$OUT/panel_bench" "$@"
