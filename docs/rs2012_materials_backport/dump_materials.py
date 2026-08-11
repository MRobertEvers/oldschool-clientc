#!/usr/bin/env python3
"""Render the lane's mask materials, and what the raster makes of them, to PNG.

HISTORICAL: the --alpha-textures flag this needs was removed with the
imported-material kernels, so the first command below no longer runs and the
checked-in bake_report.txt is the last output it produced. The images and
masks.tsv beside it are still the material census they always were. See the
README in this folder.

Regenerates everything under docs/rs2012_materials_backport/images/ plus
masks.tsv. Run from the repository root, after the bake has been applied:

    src/build_win64/rs2012_material_bake.exe --alpha-textures --alpha-report \
        > docs/rs2012_materials_backport/bake_report.txt
    python docs/rs2012_materials_backport/dump_materials.py

The mask sprites are read straight out of the content tree, so the first row of
each sheet is exactly what the cache packer consumes. The rows under it are a
*simulation* of the modulate kernel — mask x tint, tint = the face's chroma at
TORIDRAW_MODULATE_LIGHTNESS — for each face colour the mask is actually used
with. Nothing tinted is baked; the raster does that per face at draw time.
"""

import os
import re
import struct
import sys

from PIL import Image, ImageDraw

TREE = "OSRS-Content/osrs239-content/sprites/ported/rs2012_qbd_td"
OUT = "docs/rs2012_materials_backport/images"
REPORT = "docs/rs2012_materials_backport/bake_report.txt"

# Must match TORIDRAW_MODULATE_LIGHTNESS / the chroma mask in toridraw_types.h.
MODULATE_LIGHTNESS = 64
CHROMA_MASK = 0xFF80

# The neutral mid-grey a body surface reads as, for the "over a surface"
# previews. Not a real face colour - just something to composite against.
BODY = (64, 60, 66)
TILE = 128
PAD = 6
LABEL_H = 16
# Title above a row plus the caption strip under its tiles.
ROW_H = TILE + 2 * LABEL_H + PAD


def load_bmp_rgba(path):
    """The bake writes 32bpp bottom-up BMPs; alpha is the coverage plane."""
    data = open(path, "rb").read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    image = Image.new("RGBA", (width, height))
    pixels = image.load()
    for y in range(height):
        row = offset + (height - 1 - y) * width * 4
        for x in range(width):
            i = row + x * 4
            pixels[x, y] = (data[i + 2], data[i + 1], data[i], data[i + 3])
    return image


def hsl16_to_rgb(hsl):
    """The palette the caches store face colours in."""
    h = ((hsl >> 10) & 0x3F) / 64.0
    s = ((hsl >> 7) & 0x7) / 8.0
    lightness = (hsl & 0x7F) / 128.0
    if s <= 0.0:
        grey = int(lightness * 255)
        return (grey, grey, grey)
    q = lightness * (1.0 + s) if lightness < 0.5 else lightness + s - lightness * s
    p = 2.0 * lightness - q
    out = []
    for offset in (1.0 / 3.0, 0.0, -1.0 / 3.0):
        c = (h + offset) % 1.0
        if c < 1.0 / 6.0:
            v = p + (q - p) * 6.0 * c
        elif c < 0.5:
            v = q
        elif c < 2.0 / 3.0:
            v = p + (q - p) * (2.0 / 3.0 - c) * 6.0
        else:
            v = p
        out.append(int(v * 255))
    return tuple(out)


def tint_for(chroma):
    return hsl16_to_rgb((chroma & CHROMA_MASK) | MODULATE_LIGHTNESS)


def modulate(mask, tint):
    """What raster_linear_alpha_modulate_lerp8_v3 produces at full shade."""
    out = mask.copy()
    pixels = out.load()
    scale = [(c * 256 + 127) // 255 for c in tint]
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = pixels[x, y]
            pixels[x, y] = (
                min(255, r * scale[0] >> 8),
                min(255, g * scale[1] >> 8),
                min(255, b * scale[2] >> 8),
                a,
            )
    return out


def over(image, background):
    out = Image.new("RGB", image.size, background)
    out.paste(image, (0, 0), image)
    return out


def parse_report(path):
    """material id -> [(chroma, dest_texture, faces), ...]"""
    usage = {}
    pattern = re.compile(
        r"\s+material (\d+) chroma hsl16 (\d+)\s+-> texture (\d+) \((\d+) faces\)"
    )
    for line in open(path, encoding="utf-8", errors="replace"):
        match = pattern.match(line)
        if match:
            source, chroma, dest, faces = (int(g) for g in match.groups())
            usage.setdefault(source, []).append((chroma, dest, faces))
    return usage


def sheet_for(source, rows):
    """One PNG per material: the mask, then the raster's result per face colour."""
    columns = 3
    height = LABEL_H + (len(rows) + 1) * ROW_H
    sheet = Image.new("RGB", (columns * (TILE + PAD) + PAD, height), (255, 255, 255))
    draw = ImageDraw.Draw(sheet)

    mask = load_bmp_rgba(f"{TREE}/rs2012_material_{source}/0.bmp")
    dest = rows[0][1] if rows else -1
    y = LABEL_H
    draw.text(
        (PAD, y - LABEL_H + 2),
        f"material {source} -> texture {dest}: the mask as baked (alpha=yes modulate=yes)",
        fill=(0, 0, 0),
    )
    sheet.paste(mask.convert("RGB"), (PAD, y))
    sheet.paste(mask.getchannel("A").convert("RGB"), (PAD + TILE + PAD, y))
    sheet.paste(over(mask, BODY), (PAD + 2 * (TILE + PAD), y))
    draw.text((PAD, y + TILE + 2), "rgb (greyscale detail)", fill=(90, 90, 90))
    draw.text((PAD + TILE + PAD, y + TILE + 2), "alpha (coverage)", fill=(90, 90, 90))
    draw.text((PAD + 2 * (TILE + PAD), y + TILE + 2), "untinted over a surface", fill=(90, 90, 90))

    for index, (chroma, _dest, faces) in enumerate(rows):
        y = LABEL_H + (index + 1) * ROW_H
        tint = tint_for(chroma)
        tinted = modulate(mask, tint)
        draw.text(
            (PAD, y - LABEL_H + 2),
            f"face chroma hsl16 {chroma} -> tint rgb{tint}, {faces} faces",
            fill=(0, 0, 0),
        )
        sheet.paste(tinted.convert("RGB"), (PAD, y))
        sheet.paste(over(tinted, BODY), (PAD + TILE + PAD, y))
        swatch = Image.new("RGB", (TILE, TILE), tint)
        sheet.paste(swatch, (PAD + 2 * (TILE + PAD), y))
        draw.text((PAD, y + TILE + 2), "modulated rgb", fill=(90, 90, 90))
        draw.text((PAD + TILE + PAD, y + TILE + 2), "over a surface", fill=(90, 90, 90))
        draw.text((PAD + 2 * (TILE + PAD), y + TILE + 2), "the tint", fill=(90, 90, 90))

    sheet.save(f"{OUT}/material_{source}.png")


def main():
    if not os.path.isdir(TREE):
        sys.exit(f"run from the repository root; {TREE} not found")
    os.makedirs(OUT, exist_ok=True)
    usage = parse_report(REPORT)
    if not usage:
        sys.exit(f"no masks in {REPORT}; run the bake with --alpha-textures --alpha-report")

    for source in sorted(usage):
        sheet_for(source, sorted(usage[source]))

    colours = sum(len(rows) for rows in usage.values())
    print(f"wrote {len(usage)} mask sheets to {OUT}")
    print(f"{len(usage)} modulated textures used with {colours} face colours")

    with open("docs/rs2012_materials_backport/masks.tsv", "w", encoding="utf-8") as out:
        out.write("source_material\tdest_texture\tface_chroma_hsl16\ttint_rgb\tfaces\n")
        for source in sorted(usage):
            for chroma, dest, faces in sorted(usage[source]):
                r, g, b = tint_for(chroma)
                out.write(f"{source}\t{dest}\t{chroma}\t#{r:02X}{g:02X}{b:02X}\t{faces}\n")


if __name__ == "__main__":
    main()
