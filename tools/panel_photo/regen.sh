#!/bin/sh
# Rebuild the panel's picture from the frames committed beside this script.
# The include is generated, so this is how it is made: the three frames are at
# the panel's own size already, so all that happens here is the palette, the
# runs and the row tables.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"

python3 tools/panel_photo.py --parts --width 240 --height 164 --block 2 \
    --colors 16 --dither 0 --merge 24 \
    tools/panel_photo/hall-a.jpg tools/panel_photo/hall-b.jpg \
    tools/panel_photo/hall-c.jpg \
    -o app/orch_ui_photo.inc
