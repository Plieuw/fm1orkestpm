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
# darkest part, so the band is cut from the window with the most light in it and
# lifted enough to carry the picture at a fifth of the panel. That is done once
# with --pick-rows / --expose on the photograph itself, and band/ holds what came
# out of it: the band at the panel's own size, in the sixteen colours it will
# be drawn in, a kilobyte or two — so the picture can be committed beside the
# tool and this script builds the same runs from it every time.
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
        "tools/panel_photo/band/$pic.png" -o "app/orch_ui_pic_$pic.inc"
done
