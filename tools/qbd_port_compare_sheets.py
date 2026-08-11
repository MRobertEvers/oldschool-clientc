#!/usr/bin/env python3
"""Label and diff the renders tools/qbd_port_compare.sh produces.

Per form:
  <form>_sheet.png   the three renders side by side, baseline first
  <form>_diff.png    what each of the other two changed against the baseline

The baseline is the priorities-authored port. The mask is the point: the depth
test changes only which face WINS a pixel, and on a dark spiky model that is
almost invisible side by side. A mask does not care how busy the picture is.
"""

import argparse
import os
import struct
import sys

from PIL import Image, ImageDraw

BASELINE = "priorities"
VARIANTS = [
    ("priorities", "1. priorities port (baseline: authored face order)"),
    ("zbuffer", "2. z-buffer (same model, priorities ignored)"),
]
LABEL_H = 18
PAD = 6


def load_bmp(path):
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
    out = Image.new("RGB", a.size)
    pa, pb, po = a.load(), b.load(), out.load()
    changed = 0
    for y in range(a.height):
        for x in range(a.width):
            ra, ga, ba = pa[x, y]
            rb, gb, bb = pb[x, y]
            if abs(ra - rb) + abs(ga - gb) + abs(ba - bb) > 12:
                changed += 1
                po[x, y] = (255, 40, 40)
            else:
                po[x, y] = (ra // 4, ga // 4, ba // 4)
    return out, changed


def row(images, captions, w, h):
    sheet = Image.new(
        "RGB", (len(images) * (w + PAD) + PAD, h + LABEL_H + PAD), (250, 250, 250)
    )
    draw = ImageDraw.Draw(sheet)
    for i, (image, caption) in enumerate(zip(images, captions)):
        x = PAD + i * (w + PAD)
        sheet.paste(image, (x, LABEL_H))
        draw.text((x, 3), caption, fill=(0, 0, 0))
    return sheet


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", required=True)
    parser.add_argument("--forms", required=True)
    args = parser.parse_args()

    for form in args.forms.split():
        have = {}
        for key, _ in VARIANTS:
            path = os.path.join(args.dir, "%s_%s.bmp" % (form, key))
            if os.path.exists(path):
                have[key] = load_bmp(path)
        if BASELINE not in have or len(have) < 2:
            print("  %s: not enough renders, skipped" % form)
            continue

        keys = [k for k, _ in VARIANTS if k in have]
        w, h = have[BASELINE].size
        row([have[k] for k in keys],
            [dict(VARIANTS)[k] for k in keys], w, h).save(
            os.path.join(args.dir, "%s_sheet.png" % form))

        masks, captions = [], []
        total = w * h
        summary = []
        for key in keys:
            if key == BASELINE:
                continue
            mask, changed = diff_mask(have[BASELINE], have[key])
            masks.append(mask)
            captions.append(
                "%s vs baseline: %.1f%% of pixels" % (key, 100.0 * changed / total))
            summary.append("%s %.1f%%" % (key, 100.0 * changed / total))
        if masks:
            row(masks, captions, w, h).save(os.path.join(args.dir, "%s_diff.png" % form))
        print("  %s: %s" % (form, "  ".join(summary)))


if __name__ == "__main__":
    sys.exit(main())
