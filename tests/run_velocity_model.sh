#!/bin/sh
# Re-measure the models' level per velocity and regenerate the velocity tables
# (app/orch_cc_tables.inc). Run after any change that makes a model louder or
# softer with velocity; then run the suites (the level light may need
# app/orch_level.inc LEVEL_TOP again). Takes a minute or two.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CORE=${FM1_CORE:-"$ROOT/../fm1-core"}
OUT=${TMPDIR:-/tmp}/fm1orkestpm-velocity
mkdir -p "$OUT"
${CC:-cc} -std=c11 -O2 -DFM1_HOST_TEST -I"$CORE/include" -I"$ROOT/app" "$ROOT/tests/velocity_model.c" \
    "$ROOT/app/orch_cc.c" "$ROOT/app/orch_fx.c" "$ROOT/app/orch_engine.c" "$ROOT/app/orch_adaptive.c" \
    "$ROOT/app/orch_divisi.c" "$ROOT/app/orch_part.c" "$ROOT/app/orch_stage.c" \
    "$ROOT/app/orch_performance.c" "$ROOT/app/orch_resonance.c" -lm -o "$OUT/velocity_model"
"$OUT/velocity_model" > "$ROOT/tests/velocity_model.txt"
python3 "$ROOT/tools/gen_cc_tables.py"
echo "tests/velocity_model.txt and app/orch_cc_tables.inc regenerated"
