#!/bin/sh
# Engine time per voice-sample on the host (not pass/fail; relative only).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CORE=${FM1_CORE:-"$ROOT/../fm1-core"}
OUT=${TMPDIR:-/tmp}/fm1orkestpm-bench
mkdir -p "$OUT"
${CC:-cc} -std=c11 -O2 -DFM1_HOST_TEST -I"$CORE/include" -I"$ROOT/app" "$ROOT/tests/bench.c" \
    "$ROOT/app/orch_cc.c" "$ROOT/app/orch_fx.c" "$ROOT/app/orch_engine.c" "$ROOT/app/orch_adaptive.c" \
    "$ROOT/app/orch_divisi.c" "$ROOT/app/orch_part.c" "$ROOT/app/orch_stage.c" \
    "$ROOT/app/orch_performance.c" "$ROOT/app/orch_resonance.c" "$ROOT/app/orch_scenes.c" -o "$OUT/bench"
for s in ${1:-0 5 7}; do "$OUT/bench" "$s" "${2:-20}"; done
