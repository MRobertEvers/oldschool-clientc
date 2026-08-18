#!/usr/bin/env python3
"""Compose a headless BMP frame series into one labelled sprite sheet per encounter.

The client writes `TORIRS_BMP_SERIES=<dir>,<start>,<step>,<count>` as numbered
BMPs of the full framebuffer. This crops each to the world viewport, lays them
out in a grid, and writes a PNG per encounter plus one contact sheet of all of
them.

Cropping matters. The frames are the whole client — inventory, chat, minimap —
and pasted whole they make a sheet of UI with an encounter somewhere in the
middle. The viewport rectangle is a constant of the fixed-mode layout, and it is
stated here rather than detected because a detector that guesses wrong produces a
sheet that looks plausible and is off by a few pixels in every cell.

Usage:
    tools/cox_sprite_sheets.py <frames-dir> <out.png> [--label "Great Olm"]
    tools/cox_sprite_sheets.py --contact <sheet.png> [<sheet.png> ...] -o all.png
"""

import argparse
import glob
import os
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("this needs Pillow: python3 -m pip install --user Pillow")

# Fixed-mode world viewport: 512x334 at the top-left of the 765x503 frame.
VIEWPORT = (0, 0, 512, 334)
COLS = 4
PAD = 8
LABEL_H = 22
BG = (18, 18, 22)
FG = (232, 232, 236)


def load_frames(directory):
    paths = sorted(glob.glob(os.path.join(directory, "*.bmp")))
    if not paths:
        paths = sorted(glob.glob(os.path.join(directory, "*.png")))
    frames = []
    for path in paths:
        try:
            img = Image.open(path).convert("RGB")
        except Exception as exc:  # a truncated final frame is normal on exit
            print(f"  skipping {os.path.basename(path)}: {exc}")
            continue
        # Crop only when the frame is at least as big as the viewport; a
        # differently-sized client (resizable mode) is used whole rather than
        # cropped to a rectangle that means nothing in its layout.
        if img.width >= VIEWPORT[2] and img.height >= VIEWPORT[3]:
            img = img.crop(VIEWPORT)
        frames.append((os.path.basename(path), img))
    return frames


def sheet(frames, label, scale=0.5):
    if not frames:
        return None
    w = int(frames[0][1].width * scale)
    h = int(frames[0][1].height * scale)
    cols = min(COLS, len(frames))
    rows = (len(frames) + cols - 1) // cols
    width = cols * w + (cols + 1) * PAD
    height = rows * (h + LABEL_H) + (rows + 1) * PAD + LABEL_H
    out = Image.new("RGB", (width, height), BG)
    draw = ImageDraw.Draw(out)
    draw.text((PAD, PAD // 2), label, fill=FG)

    for i, (name, img) in enumerate(frames):
        col, row = i % cols, i // cols
        x = PAD + col * (w + PAD)
        y = LABEL_H + PAD + row * (h + LABEL_H + PAD)
        out.paste(img.resize((w, h), Image.LANCZOS), (x, y))
        draw.text((x, y + h + 4), name, fill=FG)
    return out


def contact(sheets, out_path):
    imgs = [Image.open(p).convert("RGB") for p in sheets]
    width = max(i.width for i in imgs)
    height = sum(i.height for i in imgs) + PAD * (len(imgs) + 1)
    out = Image.new("RGB", (width, height), BG)
    y = PAD
    for img in imgs:
        out.paste(img, (0, y))
        y += img.height + PAD
    out.save(out_path)
    print(f"wrote {out_path} ({width}x{height})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frames", nargs="?", help="directory of BMP frames")
    ap.add_argument("out", nargs="?", help="output PNG")
    ap.add_argument("--label", default="")
    ap.add_argument("--contact", nargs="*", help="compose existing sheets")
    ap.add_argument("-o", dest="contact_out", default="build/cox_sprites/all.png")
    args = ap.parse_args()

    if args.contact:
        contact(args.contact, args.contact_out)
        return 0

    if not args.frames or not args.out:
        ap.error("need <frames-dir> <out.png>")

    frames = load_frames(args.frames)
    if not frames:
        print(f"no frames in {args.frames}")
        return 1
    img = sheet(frames, args.label or os.path.basename(args.frames))
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    img.save(args.out)
    print(f"wrote {args.out} ({img.width}x{img.height}, {len(frames)} frames)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
