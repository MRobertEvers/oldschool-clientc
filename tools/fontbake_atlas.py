#!/usr/bin/env python3
"""
Turn a fontbake C bake into a glyph ATLAS a plugin can ship as its own asset.

fontbake (3rd/rscache/tools/fontbake) decodes a cache font with this tree's own
decoder and writes a `struct ToriDraw_Font` for the client to compile in. That
is the right artefact for the chrome's own faces; it is the wrong one for a
PLUGIN, which ships files and loads them at runtime, and which has no way to
name a scene font id in the first place.

So this reads the bake back and writes two files:

  <name>.png   one row of glyphs, RGBA, already in the colour and with the
               drop shadow the text is meant to have.
  <name>.ini   where each glyph is in that row, and how far the pen moves.

Baking the COLOUR in is deliberate. A plugin blits its atlas with the ordinary
image verb, which has no tint -- and it does not need one, because a caption's
colour is part of how the caption is authored, exactly like the plate it sits
on. The shadow is baked for the same reason: it is a second blit at +1,+1 in
the reference, and a glyph that carries it is one blit instead of two.

Usage:
  fontbake_atlas.py <bake.c> <out_dir> <name> <chars> [rgb|ramp:N] [shadow_rgb]

`chars` is the literal set to bake, e.g. 0123456789. `rgb`/`shadow_rgb` are
hex, defaulting to the interfaces' own yellow on black.

`ramp:N` bakes N ROWS of the same glyphs instead of one, stepping the
reference's own orb colour ramp from empty to full: red at 0, yellow at half,
green at full (clientscript 449). A meter's number is coloured by how full the
meter is, and a plugin blitting a fixed-colour atlas cannot tint one -- so the
colours are baked, one row per step, and the caller picks the row. N is a
sampling of a continuous ramp: 21 steps is 5% apart, which is finer than the
eye separates at this size.
"""

import os
import re
import struct
import sys
import zlib

# The 94-glyph RS charset, in the order a baked font's slots are indexed. Same
# table fontbake writes from; restated here because the bake does not carry it.
CHARSET = (
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "!\"£$%^&*()-_=+[{]};:'@#~,<.>/?\\| "
)


def parse_bake(text):
    """The tables one baked face needs: masks, geometry, advances, ascent."""

    def ints(field):
        m = re.search(r"\.%s = \{(.*?)\},\s*\n" % field, text, re.S)
        if not m:
            raise SystemExit("no .%s table in the bake" % field)
        return [int(v) for v in re.findall(r"-?\d+", m.group(1))]

    blob = re.search(r"static const unsigned char \w+_glyph_bits\[\d+\] = \{(.*?)\};", text, re.S)
    if not blob:
        raise SystemExit("no glyph blob in the bake")
    bits = [int(v) for v in re.findall(r"\d+", blob.group(1))]

    # Each glyph's mask starts at its own offset into that blob; the bake
    # states those as pointer arithmetic, which is the only place they appear.
    offsets = [int(v) for v in re.findall(r"_glyph_bits \+ (\d+)", text)]

    # `ascent` is not a field of the struct -- it is stated in the bake's own
    # header comment, which is the only place the number survives.
    ascent_m = re.search(r"ascent (\d+)", text)
    ascent = int(ascent_m.group(1)) if ascent_m else 0
    return {
        "bits": bits,
        "offsets": offsets,
        "w": ints("glyph_width"),
        "h": ints("glyph_height"),
        "ox": ints("offset_x"),
        "oy": ints("offset_y"),
        "advance": ints("advance"),
        "ascent": ascent,
    }


def write_png(path, width, height, pixels):
    raw = bytearray()
    for row in range(height):
        raw.append(0)
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
    if len(argv) < 5:
        raise SystemExit(__doc__.strip())
    bake, out_dir, name, chars = argv[1], argv[2], argv[3], argv[4]
    # The ascent lives in the generated HEADER's comment, not in the .c.
    text = open(bake).read()
    header = os.path.splitext(bake)[0] + ".h"
    if os.path.exists(header):
        text += open(header).read()
    spec = argv[5] if len(argv) > 5 else "FFFF00"
    ramp = int(spec.split(":", 1)[1]) if spec.startswith("ramp:") else 0
    rgb = 0 if ramp else int(spec, 16)
    shadow = int(argv[6], 16) if len(argv) > 6 else 0x000000

    font = parse_bake(text)
    os.makedirs(out_dir, exist_ok=True)

    slots = []
    for ch in chars:
        if ch not in CHARSET:
            raise SystemExit("'%s' is not in the RS charset" % ch)
        slots.append((ch, CHARSET.index(ch)))

    # Each cell is a pixel wider and taller than the glyph so the shadow at
    # +1,+1 has somewhere to land.
    cells = [(ch, s, font["w"][s] + 1, font["h"][s] + 1) for ch, s in slots]
    atlas_w = sum(c[2] for c in cells)
    row_h = max(c[3] for c in cells)
    steps = ramp if ramp > 0 else 1
    pixels = [0] * (atlas_w * row_h * steps)

    def ramp_colour(step):
        """clientscript 449, evaluated at `step`/(steps-1) full."""
        if steps == 1:
            return rgb
        total = 2 * (steps - 1)          # so `half` is exactly steps-1
        value = 2 * step
        half = total // 2
        if value > half:
            return (((255 - 255 * (value - half) // half) & 0xFF) << 16) | (255 << 8)
        return (255 << 16) | (((value * 255 // half) & 0xFF) << 8)

    rows = []
    for step in range(steps):
        colour = ramp_colour(step)
        top = step * row_h
        pen = 0
        for ch, slot, cell_w, cell_h in cells:
            gw, gh = font["w"][slot], font["h"][slot]
            base = font["offsets"][slot]
            for gy in range(gh):
                for gx in range(gw):
                    if not font["bits"][base + gy * gw + gx]:
                        continue
                    # Shadow first, so the glyph covers it where they meet.
                    pixels[(top + gy + 1) * atlas_w + pen + gx + 1] = 0xFF000000 | shadow
            for gy in range(gh):
                for gx in range(gw):
                    if font["bits"][base + gy * gw + gx]:
                        pixels[(top + gy) * atlas_w + pen + gx] = 0xFF000000 | colour
            if step == 0:
                rows.append(
                    (ch, pen, 0, cell_w, cell_h, font["ox"][slot], font["oy"][slot],
                     font["advance"][slot])
                )
            pen += cell_w

    write_png(os.path.join(out_dir, name + ".png"), atlas_w, row_h * steps, pixels)

    with open(os.path.join(out_dir, name + ".ini"), "w") as out:
        out.write(
            "; GENERATED by tools/fontbake_atlas.py -- do not edit.\n"
            ";\n"
            "; One row per glyph in %s.png:\n"
            ";   <char> = <atlas x> <atlas y> <w> <h> <offset x> <offset y> <advance>\n"
            ";\n"
            "; The offsets are the glyph's place inside the LINE BOX and the advance is\n"
            "; how far the pen moves after it, both exactly as the cache states them --\n"
            "; so text laid out from this file sits where the same string would sit if the\n"
            "; client had drawn it with the cache's own face.\n"
            "\n" % name
        )
        out.write("ascent=%d\n" % font["ascent"])
        # The colour ramp, when there is one: `steps` rows of the same glyphs,
        # `row_height` apart, from empty at row 0 to full at the last.
        out.write("steps=%d\nrow_height=%d\n\n" % (steps, row_h))
        for ch, x, y, w, h, ox, oy, adv in rows:
            out.write("%s=%d %d %d %d %d %d %d\n" % (ch, x, y, w, h, ox, oy, adv))

    print("wrote %s.png (%dx%d, %d colour step(s)) and %s.ini (%d glyphs)"
          % (name, atlas_w, row_h * steps, steps, name, len(rows)))


if __name__ == "__main__":
    main(sys.argv)
