#!/usr/bin/env python3
"""
Turn a spritebake C bake into PNG files.

spritebake (3rd/rscache/tools/spritebake) decodes a cache's sprites table with
this tree's own decoder and writes `static const uint32_t` ARGB arrays. That is
the right artefact for art the client compiles in; it is the wrong one for art a
PLUGIN ships, because a plugin's assets are files it loads at runtime and not
code it is linked against.

So this reads the bake back and writes one PNG per symbol. The decode is still
spritebake's -- the alpha, the crop and the palette are the cache's, not this
script's -- and what changes is only the container.

Usage:
  spritebake_png.py <bake.c> <out_dir>

Each `<Prefix>_<symbol>_argb[N]` array in the file becomes `<symbol>.png`, sized
from the geometry table spritebake writes beside it.
"""

import re
import sys
import zlib
import struct
import os


def parse_bake(text):
    """Yield (symbol, width, height, [argb...]) for every sprite in a bake."""
    # The geometry lives in the accessor table: `{ 57, 34, Prefix_frame_argb }`.
    geometry = dict(
        (m.group(3), (int(m.group(1)), int(m.group(2))))
        for m in re.finditer(
            r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*([A-Za-z0-9_]+)\s*\}", text
        )
    )
    for m in re.finditer(
        r"static const uint32_t ([A-Za-z0-9_]+_argb)\[(\d+)\]\s*=\s*\{(.*?)\};",
        text,
        re.S,
    ):
        array, count, body = m.group(1), int(m.group(2)), m.group(3)
        if array not in geometry:
            raise SystemExit("no geometry row for %s" % array)
        width, height = geometry[array]
        pixels = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{8})", body)]
        if len(pixels) != count or count != width * height:
            raise SystemExit(
                "%s: %d values for %dx%d" % (array, len(pixels), width, height)
            )
        # `Prefix_frame_argb` -> `frame`; the prefix is spritebake's, not ours.
        symbol = array[: -len("_argb")]
        symbol = symbol.split("_", 1)[1] if "_" in symbol else symbol
        yield symbol, width, height, pixels


def write_png(path, width, height, pixels):
    """8-bit RGBA, no interlace -- what src/engine/png_decode.c reads."""
    raw = bytearray()
    for row in range(height):
        raw.append(0)  # filter: none
        for col in range(width):
            argb = pixels[row * width + col]
            raw += bytes(
                ((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF)
            )

    def chunk(tag, payload):
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    with open(path, "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n")
        out.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
        out.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        out.write(chunk(b"IEND", b""))


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__.strip())
    text = open(argv[1]).read()
    os.makedirs(argv[2], exist_ok=True)
    for symbol, width, height, pixels in parse_bake(text):
        path = os.path.join(argv[2], symbol + ".png")
        write_png(path, width, height, pixels)
        print("wrote %s (%dx%d)" % (path, width, height))


if __name__ == "__main__":
    main(sys.argv)
