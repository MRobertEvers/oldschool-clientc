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
  spritebake_png.py <bake.c> <out_dir> [--strip <name>]

Each `<Prefix>_<symbol>_argb[N]` array in the file becomes `<symbol>.png`, sized
from the geometry table spritebake writes beside it.

`--strip` writes ONE `<name>.png` instead: every sprite in the bake laid out
left to right in a cell the size of the largest of them, in the order the
recipe asked for them. A set that is indexed rather than named -- the skill
icons, one per skill id -- is one asset that way instead of twenty-three, and
the plugin holding it addresses a member by multiplying, with nothing to keep
in step but the cell size. Twenty-three files would each be a load, an image
handle and a name to get right, and the host caps both tables.
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


def write_strip(path, sprites):
    """Every sprite in one row of equal cells, each centred in its own."""
    cell_w = max(s[1] for s in sprites)
    cell_h = max(s[2] for s in sprites)
    width = cell_w * len(sprites)
    pixels = [0] * (width * cell_h)
    for index, (_symbol, w, h, src) in enumerate(sprites):
        # Centred, not corner-aligned: the cell is as wide as the widest
        # member, so a narrower one corner-aligned would sit off to one side of
        # a caller that draws the whole cell.
        ox = index * cell_w + (cell_w - w) // 2
        oy = (cell_h - h) // 2
        for y in range(h):
            row = (oy + y) * width + ox
            pixels[row : row + w] = src[y * w : y * w + w]
    write_png(path, width, cell_h, pixels)
    return width, cell_h, cell_w


def main(argv):
    strip = None
    if len(argv) == 5 and argv[3] == "--strip":
        strip = argv[4]
    elif len(argv) != 3:
        raise SystemExit(__doc__.strip())
    text = open(argv[1]).read()
    os.makedirs(argv[2], exist_ok=True)
    sprites = list(parse_bake(text))
    if strip:
        path = os.path.join(argv[2], strip + ".png")
        width, height, cell = write_strip(path, sprites)
        print(
            "wrote %s (%dx%d, %d sprites in %dpx cells)"
            % (path, width, height, len(sprites), cell)
        )
        return
    for symbol, width, height, pixels in sprites:
        path = os.path.join(argv[2], symbol + ".png")
        write_png(path, width, height, pixels)
        print("wrote %s (%dx%d)" % (path, width, height))


if __name__ == "__main__":
    main(sys.argv)
