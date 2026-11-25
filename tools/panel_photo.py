#!/usr/bin/env python3
"""Turn photographs or film frames into the panel's own picture format.

The hall on the STAGE page is a photograph, reduced to what the panel can show:
a palette of sixteen colours taken from the images, and per row the runs of one
colour, each run one fm1_screen_fill. A run is a rectangle on the wire, so a
240 x 164 picture costs about 1 500 fills instead of 39 000 pixels.

    python3 tools/panel_photo.py --parts --width 240 --height 164 --block 2 \\
        --colors 16 --dither 0 --merge 24 hall-a.jpg hall-b.jpg hall-c.jpg \\
        -o app/orch_ui_photo.inc

The first image is the rest frame; every later image is an animation frame, and
--parts writes each frame to a file of its own (a photograph is a big table).
--band LO:HI says that the light only changes in those rows: every later frame
keeps the rest frame's rows outside the band, so the panel can repaint just the
band when the animation steps instead of the whole hall.
--preview writes what the panel would show, which is how the palette, the block
size and the merge were chosen.

A menu's picture is a band 240 x 30 instead of the whole body, and a band of a
night photograph needs two more things: --pick-rows N takes it from the window
of N rows with the most light in it rather than the middle of the frame, and
--expose / --contrast lift it enough to read at a fifth of the panel. --name
puts a prefix on the defines and the tables (PIC_ARP_W, pic_arp_palette,
pic_arp_runs0), so five pictures can be included in one translation unit.

Requires Pillow: a build-time tool, the panel itself needs nothing.
"""
import argparse
import sys

from PIL import Image, ImageEnhance


def load(path, width, height):
    """Centre-crop to the panel's aspect and scale down to the run grid."""
    image = Image.open(path).convert("RGB")
    want = width / height
    w, h = image.size
    if w / h > want:
        keep = int(round(h * want))
        box = ((w - keep) // 2, 0, (w - keep) // 2 + keep, h)
    else:
        keep = int(round(w / want))
        box = (0, (h - keep) // 2, w, (h - keep) // 2 + keep)
    return image.crop(box).resize((width, height), Image.LANCZOS)


def pick_rows(image, width, height, rows):
    """The window of `rows` rows a band of the picture should be taken from:
    the one with the most light in it. A night picture cropped through its
    middle is its darkest part, which is not what a 240 x 30 strip can show."""
    grey = image.convert("L")
    w, h = image.size
    best, best_at = -1, 0
    for y in range(0, h - rows + 1):
        band = grey.crop((0, y, w, y + rows)).resize((24, 3), Image.BOX)
        light = sum(band.tobytes())
        if light > best:
            best, best_at = light, y
    band = image.crop((0, best_at, w, best_at + rows))
    return band.resize((width, height), Image.LANCZOS)


def exposed(image, brightness, contrast):
    """A band is a fifth of the panel: it has to carry its picture at that size,
    so a dark one is lifted a little rather than left as a black stripe."""
    image = ImageEnhance.Contrast(image).enhance(contrast)
    return ImageEnhance.Brightness(image).enhance(brightness)


def pixels(image, block):
    """The grid of cells: the mean of each block, which is one fill later."""
    w, h = image.size
    grid_w, grid_h = w // block, h // block
    small = image.resize((grid_w, grid_h), Image.BOX)
    return [[small.getpixel((x, y)) for x in range(grid_w)] for y in range(grid_h)]


def median_cut(samples, count):
    """A palette from the image itself: split the box that is widest, twice."""
    boxes = [samples]
    while len(boxes) < count:
        widest, span, channel = None, -1, 0
        for box in boxes:
            if len(box) < 2:
                continue
            for c in range(3):
                values = [p[c] for p in box]
                if max(values) - min(values) > span:
                    widest, span, channel = box, max(values) - min(values), c
        if widest is None or span <= 0:
            break
        boxes.remove(widest)
        widest.sort(key=lambda p: p[channel])
        half = len(widest) // 2
        boxes.append(widest[:half])
        boxes.append(widest[half:])
    palette = []
    for box in boxes:
        n = len(box)
        palette.append(tuple(sum(p[c] for p in box) // n for c in range(3)))
    return palette


def nearest(palette, colour):
    best, best_d = 0, None
    r, g, b = colour
    for i, (pr, pg, pb) in enumerate(palette):
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if best_d is None or d < best_d:
            best, best_d = i, d
    return best


def quantize(grid, palette, strength):
    """Floyd–Steinberg at cell scale, so a gradient reads as a gradient even
    with a palette this small. It is off by default: a dithered photograph is a
    run of one or two cells a colour, and that is a thousand extra fills."""
    height = len(grid)
    error = [[[0.0, 0.0, 0.0] for _ in row] for row in grid]
    out = []
    for y, row in enumerate(grid):
        line = []
        forward = y % 2 == 0
        order = range(len(row)) if forward else range(len(row) - 1, -1, -1)
        step = 1 if forward else -1
        for x in order:
            r, g, b = (row[x][c] + error[y][x][c] * strength for c in range(3))
            r = min(255.0, max(0.0, r))
            g = min(255.0, max(0.0, g))
            b = min(255.0, max(0.0, b))
            i = nearest(palette, (r, g, b))
            line.append((x, i))
            pr, pg, pb = palette[i]
            dr, dg, db = r - pr, g - pg, b - pb
            for dx, dy, weight in ((step, 0, 7 / 16), (-step, 1, 3 / 16),
                                   (0, 1, 5 / 16), (step, 1, 1 / 16)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < len(row) and ny < height:
                    e = error[ny][nx]
                    e[0] += dr * weight
                    e[1] += dg * weight
                    e[2] += db * weight
        line.sort()
        out.append([i for _, i in line])
    return out


def close(a, b, merge):
    return max(abs(a[c] - b[c]) for c in range(3)) <= merge


def merged_row(row, palette, merge):
    """The row as it will be drawn: the colour of each run, over the whole run,
    so a stretch that is one run on the panel is one value in the table."""
    out = list(row)
    start = 0
    for x in range(1, len(row) + 1):
        if x == len(row) or not close(palette[row[start]], palette[row[x]], merge):
            for k in range(start, x):
                out[k] = row[start]
            start = x
    return out


def runs_of(row, palette, merge):
    """One run per stretch of the same colour, and two neighbouring stretches
    whose colours are close enough to be one are one: at 240 pixels wide, a step
    of one palette entry is a seam nobody can see, and a run per cell is what
    turns a photograph into five thousand window commands."""
    out = []
    for x, i in enumerate(row):
        if out:
            last = out[-1]
            a, b = palette[last[2]], palette[i]
            if last[2] == i or max(abs(a[c] - b[c]) for c in range(3)) <= merge:
                last[1] += 1
                continue
        out.append([x, 1, i])
    return out


def preview(frames, rows, palette, width, height, block, path):
    """What the panel will show: the runs, painted at their block size."""
    sheet = Image.new("RGB", (width * len(frames) + 8 * (len(frames) - 1), height))
    for f, frame in enumerate(rows):
        image = Image.new("RGB", (width, height))
        for y, line in enumerate(frame):
            for x, w, i in line:
                image.paste(palette[i], (x * block, y * block,
                                         (x + w) * block, (y + 1) * block))
        sheet.paste(image, (f * (width + 8), 0))
    sheet.save(path)


def rgb565(colour):
    r, g, b = colour
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


HEX = "0123456789abcdef"


def frame_part(out, f, row_runs, counts, name="photo"):
    """One frame, in a file of its own: the runs of each row, three characters
    each, and the row table that says where a row starts in them."""
    offsets, text = [], []
    total = 0
    for line in row_runs:
        offsets.append(total)
        for x, w, i in line:
            text.append("%s%02x" % (HEX[i], w))
        total += len(line)
    offsets.append(total)
    out.extend(["/* frame %d: %d runs */" % (f, total),
                "static const uint16_t %s_rows%d[%s_ROWS + 1U] = {"
                % (name, f, name.upper())])
    numbers = [str(n) for n in offsets]
    for i in range(0, len(numbers), 14):
        out.append("    " + ", ".join(numbers[i:i + 14]) + ",")
    out.append("};")
    joined = "".join(text)
    out.append("static const char %s_runs%d[] =" % (name, f))
    for i in range(0, len(joined), 60):
        out.append('    "%s"' % joined[i:i + 60])
    out.append("    ;")
    out.append("")
    counts.append(total)


def wrap_sources(sources, width=70):
    """The frames this picture came from, in the header, on lines that fit."""
    lines, line = [], " * from"
    for i, name in enumerate(sources):
        piece = ("" if i == 0 else ",") + " " + name
        if len(line) + len(piece) > width and line != " * from":
            lines.append(line)
            line = " *     " + name
        else:
            line += piece
    lines.append(line)
    return "\n".join(lines)


def within_band(frames, band):
    """Later frames keep the first frame outside the band, so that a step of the
    animation is a repaint of the band and nothing else: everything above and
    below it is the same picture it was."""
    if not band:
        return frames
    lo, hi = band
    base = frames[0]
    return [base if f == 0 else
            [row if lo <= y <= hi else base[y] for y, row in enumerate(frame)]
            for f, frame in enumerate(frames)]


def emit_rle(path, frames, palette, width, height, block, strength, merge,
             parts, sources, band=None, name="PHOTO", what=None):
    """The picture as its runs, three hex characters each: the palette index and
    the run's width in cells. A row of the hall is ten runs and thirty
    characters, so the whole hall is a few kilobytes instead of a hundred."""
    up, lo = name.upper(), name.lower()
    what = what or "The hall as a photograph the panel can hold"
    cols, rows = width // block, height // block
    frames = within_band(frames, band)
    band_lo, band_hi = band if band else (0, rows - 1)
    out = ["/* Generated by tools/panel_photo.py — do not edit.",
           " *",
           " * %s: per row, the runs it is" % what,
           " * made of. Three hex characters a run — the palette index, then the run's",
           " * width in cells — one fm1_screen_fill each, so this is not only a",
           " * picture but the list of window commands that draw it.",
           " *",
           wrap_sources(sources),
           " * %d x %d at %dx%d, %d colours, dither %.2f, merge %d" % (
               width, height, block, block, len(palette), strength, merge),
           (" * %d frame(s); the light changes only in rows %d..%d" % (
               len(frames), band_lo, band_hi)) if len(frames) > 1 else
           " * 1 frame",
           " */",
           "#define %s_W %dU" % (up, width),
           "#define %s_H %dU" % (up, height),
           "#define %s_BLOCK %dU" % (up, block),
           "#define %s_COLS %dU" % (up, cols),
           "#define %s_ROWS %dU" % (up, rows),
           "#define %s_COLOURS %dU" % (up, len(palette)),
           "#define %s_FRAMES %dU" % (up, len(frames)),
           "#define %s_BAND_LO %dU" % (up, band_lo),
           "#define %s_BAND_HI %dU" % (up, band_hi),
           "",
           "static const uint16_t %s_palette[%s_COLOURS] = {" % (lo, up)]
    for i in range(0, len(palette), 8):
        out.append("    " + ", ".join("0x%04X" % rgb565(c) for c in palette[i:i + 8]) + ",")
    out.append("};")
    out.append("")
    counts = []
    grids = [[merged_row(row, palette, merge) for row in frame] for frame in frames]
    row_runs = [[runs_of(row, palette, 0) for row in grid] for grid in grids]
    if parts:
        base = path[:-4] if path.endswith(".inc") else path
        for f in range(len(frames)):
            part = []
            frame_part(part, f, row_runs[f], counts, lo)
            open("%s_%d.inc" % (base, f), "w").write("\n".join(part) + "\n")
            out.append('#include "%s_%d.inc"' % (base.split("/")[-1], f))
        out.append("")
    else:
        for f in range(len(frames)):
            frame_part(out, f, row_runs[f], counts, lo)
    out.append("static const char *const %s_frame_runs[%s_FRAMES] = {" % (lo, up))
    out.append("    " + ", ".join("%s_runs%d" % (lo, f) for f in range(len(frames))))
    out.append("};")
    out.append("static const uint16_t *const %s_frame_rows[%s_FRAMES] = {" % (lo, up))
    out.append("    " + ", ".join("%s_rows%d" % (lo, f) for f in range(len(frames))))
    out.append("};")
    out.append("")
    text = "\n".join(out) + "\n"
    open(path, "w").write(text)
    return text, counts, []


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("images", nargs="+")
    ap.add_argument("--width", type=int, default=240)
    ap.add_argument("--height", type=int, default=164)
    ap.add_argument("--block", type=int, default=2)
    ap.add_argument("--colors", type=int, default=16)
    ap.add_argument("--dither", type=float, default=0.0,
                    help="Floyd-Steinberg strength; dithering breaks up runs")
    ap.add_argument("--merge", type=int, default=24,
                    help="merge neighbouring runs whose colours are this close")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--preview", help="also write what the panel would show")
    ap.add_argument("--parts", action="store_true",
                    help="write each frame to its own .inc file")
    ap.add_argument("--pick-rows", type=int,
                    help="take the band from the brightest window of this many "
                         "rows instead of the centre of the image")
    ap.add_argument("--expose", type=float, default=1.0,
                    help="brightness of the result, 1.0 leaves it alone")
    ap.add_argument("--contrast", type=float, default=1.0,
                    help="contrast of the result, 1.0 leaves it alone")
    ap.add_argument("--name", default="PHOTO",
                    help="the C prefix: <NAME>_W, <name>_palette, <name>_runs0")
    ap.add_argument("--what", help="what the picture is, in the header")
    ap.add_argument("--band", metavar="LO:HI",
                    help="rows the animation may change; later frames keep the "
                         "first frame outside them")
    args = ap.parse_args(argv)
    band = None
    if args.band:
        lo, hi = args.band.split(":")
        band = (int(lo), int(hi))

    if args.pick_rows:
        images = [pick_rows(Image.open(p).convert("RGB"), args.width,
                            args.height, args.pick_rows) for p in args.images]
    else:
        images = [load(p, args.width, args.height) for p in args.images]
    if args.expose != 1.0 or args.contrast != 1.0:
        images = [exposed(im, args.expose, args.contrast) for im in images]
    grids = [pixels(image, args.block) for image in images]

    samples = [c for g in grids for row in g for c in row]
    stride = max(1, len(samples) // 6000)
    palette = median_cut(samples[::stride], args.colors)

    frames = [quantize(g, palette, args.dither) for g in grids]
    text, counts, _ = emit_rle(args.out, frames, palette, args.width,
                               args.height, args.block, args.dither,
                               args.merge, args.parts, args.images, band,
                               args.name, args.what)
    print("%s: %d bytes" % (args.out, len(text)))
    for i, n in enumerate(counts):
        print("frame %d: %5d runs" % (i, n))
    if args.preview:
        preview(frames, [[runs_of(merged_row(row, palette, args.merge),
                                  palette, 0) for row in frame]
                         for frame in frames], palette, args.width,
                args.height, args.block, args.preview)
        print("preview:", args.preview)


if __name__ == "__main__":
    main(sys.argv[1:])
