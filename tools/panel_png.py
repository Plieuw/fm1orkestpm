#!/usr/bin/env python3
"""Convert the PPM written by tests/panel_preview.c into a PNG.

Standard library only, so it runs where Pillow is not installed. The preview
is a layout check, not a picture of the real display.

    python3 tools/panel_png.py panel.ppm panel.png
"""
import struct
import sys
import zlib


def read_ppm(path):
    data = open(path, "rb").read()
    fields, index = [], 0
    while len(fields) < 4:
        while data[index:index + 1].isspace():
            index += 1
        if data[index:index + 1] == b"#":
            while data[index:index + 1] != b"\n":
                index += 1
            continue
        start = index
        while not data[index:index + 1].isspace():
            index += 1
        fields.append(data[start:index])
    index += 1
    if fields[0] != b"P6":
        raise SystemExit("expected a binary P6 PPM")
    width, height = int(fields[1]), int(fields[2])
    return width, height, data[index:index + width * height * 3]


def chunk(kind, payload):
    body = struct.pack(">I", len(payload)) + kind + payload
    return body + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    width, height, pixels = read_ppm(argv[1])
    stride = width * 3
    raw = b"".join(b"\x00" + pixels[y * stride:(y + 1) * stride]
                   for y in range(height))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    open(argv[2], "wb").write(png)
    print(f"{argv[2]}: {width}x{height}, {len(png)} bytes")


if __name__ == "__main__":
    main(sys.argv)
