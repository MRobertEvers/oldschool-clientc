#!/usr/bin/env python3
"""Turn the three kernel renders of each subject into labelled comparison sheets.

Called by tools/hd_kernels_shots.sh; usable on its own once the BMPs exist.

Produces, per subject:
  <subject>_sheet.png      legacy | alpha-only | both, side by side, labelled
  <subject>_diff.png       per-pixel difference masks for the two steps

The diffs matter more than the renders. Two frames that look similar at a
glance can differ across a third of the model, and a reader should not have to
take "these are different" on trust — the mask says where, and the caption says
how much.
"""

import argparse
import os
import struct
import sys

from PIL import Image, ImageDraw

VARIANTS = [
    ("legacy", "both kernels off (before)"),
    ("alpha", "coverage only, no tint"),
    ("both", "coverage + modulate (shipping)"),
]
LABEL_H = 18
PAD = 6


def load_bmp(path):
    """32bpp bottom-up BMP, what rs2012_model_view writes."""
    data = open(path, "rb").read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    image = Image.new("RGB", (width, height))
    pixels = image.load()
    for y in range(height):
        row = offset + (height - 1 - y) * width * 4
        for x in range(width):
            i = row + x * 4
            pixels[x, y] = (data[i + 2], data[i + 1], data[i])
    return image


def diff_mask(a, b):
    """Red where the two differ, dimmed original underneath for context."""
    out = Image.new("RGB", a.size)
    pa, pb, po = a.load(), b.load(), out.load()
    changed = 0
    for y in range(a.height):
        for x in range(a.width):
            ra, ga, ba = pa[x, y]
            rb, gb, bb = pb[x, y]
            delta = abs(ra - rb) + abs(ga - gb) + abs(ba - bb)
            if delta > 12:
                changed += 1
                po[x, y] = (255, 40, 40)
            else:
                po[x, y] = (ra // 4, ga // 4, ba // 4)
    return out, changed


def label_row(images, captions, width_each, height):
    sheet = Image.new(
        "RGB", (len(images) * (width_each + PAD) + PAD, height + LABEL_H + PAD), (250, 250, 250)
    )
    draw = ImageDraw.Draw(sheet)
    for i, (image, caption) in enumerate(zip(images, captions)):
        x = PAD + i * (width_each + PAD)
        sheet.paste(image, (x, LABEL_H))
        draw.text((x, 3), caption, fill=(0, 0, 0))
    return sheet


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", required=True)
    parser.add_argument("--subjects", required=True)
    args = parser.parse_args()

    for subject in args.subjects.split():
        paths = {v: os.path.join(args.dir, f"{subject}_{v}.bmp") for v, _ in VARIANTS}
        if not all(os.path.exists(p) for p in paths.values()):
            print(f"  {subject}: missing renders, skipped")
            continue

        images = {v: load_bmp(p) for v, p in paths.items()}
        w, h = images["legacy"].size

        sheet = label_row(
            [images[v] for v, _ in VARIANTS],
            [f"{v}: {caption}" for v, caption in VARIANTS],
            w,
            h,
        )
        sheet.save(os.path.join(args.dir, f"{subject}_sheet.png"))

        total = w * h
        alpha_mask, alpha_changed = diff_mask(images["legacy"], images["alpha"])
        both_mask, both_changed = diff_mask(images["alpha"], images["both"])
        net_mask, net_changed = diff_mask(images["legacy"], images["both"])
        diff = label_row(
            [alpha_mask, both_mask, net_mask],
            [
                f"coverage changed {100.0 * alpha_changed / total:.1f}% of pixels",
                f"modulate changed {100.0 * both_changed / total:.1f}%",
                f"net legacy -> shipping {100.0 * net_changed / total:.1f}%",
            ],
            w,
            h,
        )
        diff.save(os.path.join(args.dir, f"{subject}_diff.png"))
        print(
            f"  {subject}: coverage {100.0 * alpha_changed / total:.1f}%  "
            f"modulate {100.0 * both_changed / total:.1f}%  "
            f"net {100.0 * net_changed / total:.1f}%"
        )


if __name__ == "__main__":
    sys.exit(main())
