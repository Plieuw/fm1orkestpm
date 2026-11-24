#!/bin/sh
# Rebuild the panel's pictures from the images committed beside this script.
# The includes are generated, so this is how they are made: the images are at
# the panel's own size already, so all that happens here is the palette, the
# runs and the row tables.
#
# The hall has two frames and --band says the light only changes in those rows,
# so a step of the animation repaints the band and not the hall.
#
# Each menu has one picture: a band 240 x 30, the strip a page has free between
# its work and its knobs. A night picture cropped through the middle is its
# darkest part, so --pick-rows takes the window with the most light in it, and
# --expose lifts it enough to carry the picture at a fifth of the panel.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"

python3 tools/panel_photo.py --parts --width 240 --height 164 --block 2 \
    --colors 16 --dither 0 --merge 24 --band 0:46 \
    tools/panel_photo/hall-a.jpg tools/panel_photo/hall-c.jpg \
    -o app/orch_ui_photo.inc

for pic in arp seq glo fx; do
    case "$pic" in
    arp) name=PIC_ARP; what="the harp the arp plays" ;;
    seq) name=PIC_SEQ; what="the score the sequencer follows" ;;
    glo) name=PIC_GLO; what="the console at the back of the hall" ;;
    fx)  name=PIC_FX;  what="the rack the effects live in" ;;
    esac
    python3 tools/panel_photo.py --width 240 --height 30 --block 2 --colors 16 \
        --dither 0 --merge 24 --name "$name" --what "The band: $what" \
        --pick-rows 30 --expose 1.35 --contrast 1.15 \
        "tools/panel_photo/$pic.png" -o "app/orch_ui_pic_$pic.inc"
done
