#!/usr/bin/env python3
"""Derive a badge-sized copy of a Summoning panel sprite.

The familiar special badge draws the claw at 18x25, but the art's nominal box is
32x45. An IF3 graphic scales its nominal box into the component rect, and the
selection outline (`if_setoutline(2)`) is computed on the *source* raster before
that scale — so a 1px ring loses roughly half its pixels on the way down and
lands as speckle instead of an outline.

The reference never hits this: RS sprites are authored at the size they are
drawn, so nothing scales an outlined icon. Do the same — downsize first, then
let the outline happen at the badge's own resolution.

The resample is nearest with the renderer's own mapping (`sx = dx*sw/dw`,
`ToriDraw2D_BlitArgbScaledAlpha`), so the pixels this writes are the pixels the
scaled draw was already putting on screen. No colour is invented, which also
keeps every pixel inside the pack's recorded palette.

    python3 tools/make_summoning_badge_sprite.py \
        --source OSRS-Content/osrs239-content/sprites/ported/scape2009_summoning/summoning_familiar_special_overlay \
        --out    OSRS-Content/osrs239-content/sprites/ported/scape2009_summoning/summoning_familiar_special_overlay_badge \
        --size 18x25
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def read_bmp(path: Path) -> tuple[list[int], int, int]:
    """32-bit BGRA, bottom-up, as cachepack's bmp_read_file expects."""
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise SystemExit(f"{path}: not a BMP")
    offset, width, height = (
        struct.unpack_from("<i", data, 10)[0],
        struct.unpack_from("<i", data, 18)[0],
        struct.unpack_from("<i", data, 22)[0],
    )
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp != 32:
        raise SystemExit(f"{path}: expected 32bpp, got {bpp}")
    pixels = [0] * (width * height)
    src = offset
    for y in range(height - 1, -1, -1):
        for x in range(width):
            b, g, r, a = data[src : src + 4]
            pixels[y * width + x] = (a << 24) | (r << 16) | (g << 8) | b
            src += 4
    return pixels, width, height


def write_bmp(path: Path, pixels: list[int], width: int, height: int) -> None:
    body = bytearray()
    for y in range(height - 1, -1, -1):
        for x in range(width):
            p = pixels[y * width + x]
            body += bytes((p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF, (p >> 24) & 0xFF))
    header = struct.pack(
        "<2sIHHIIiiHHIIiiII",
        b"BM", 54 + len(body), 0, 0, 54,
        40, width, height, 1, 32, 0, len(body), 0, 0, 0, 0,
    )
    path.write_bytes(header + bytes(body))


def read_meta(path: Path) -> tuple[list[str], tuple[int, ...]]:
    palette: list[str] = []
    geometry: tuple[int, ...] | None = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("p") and "=" in line and line[1].isdigit():
            palette.append(line)
        elif line.startswith("sprite0="):
            geometry = tuple(int(v) for v in line.split("=", 1)[1].split(","))
    if geometry is None or len(geometry) != 6:
        raise SystemExit(f"{path}: no sprite0=w,h,cw,ch,cx,cy line")
    return palette, geometry


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--size", required=True, help="destination nominal box, e.g. 18x25")
    args = parser.parse_args()

    dst_w, dst_h = (int(v) for v in args.size.lower().split("x"))
    crop, crop_w, crop_h = read_bmp(args.source / "0.bmp")
    palette, (nom_w, nom_h, meta_cw, meta_ch, crop_x, crop_y) = read_meta(args.source / "pack.meta")
    if (meta_cw, meta_ch) != (crop_w, crop_h):
        raise SystemExit(
            f"pack.meta says the stored part is {meta_cw}x{meta_ch}, the BMP is {crop_w}x{crop_h}"
        )

    # The scale the renderer performs is nominal -> rect, so the crop has to be
    # seated in its nominal box before resampling or the offsets scale wrong.
    nominal = [0] * (nom_w * nom_h)
    for y in range(crop_h):
        for x in range(crop_w):
            ny, nx = y + crop_y, x + crop_x
            if 0 <= nx < nom_w and 0 <= ny < nom_h:
                nominal[ny * nom_w + nx] = crop[y * crop_w + x]

    scaled = [0] * (dst_w * dst_h)
    for y in range(dst_h):
        sy = y * nom_h // dst_h
        for x in range(dst_w):
            scaled[y * dst_w + x] = nominal[sy * nom_w + (x * nom_w // dst_w)]

    # Store the whole nominal box rather than the tight art box. The outline is
    # painted into the stored raster without growing it, so a silhouette flush
    # against that raster's edge gets a ring on three sides and a flat cut on
    # the fourth. Keeping the transparent margin gives the ring somewhere to go,
    # and an offset of 0,0 also keeps the draw on the renderer's unscaled path.
    opaque = [(x, y) for y in range(dst_h) for x in range(dst_w) if scaled[y * dst_w + x] >> 24]
    if not opaque:
        raise SystemExit("the resample produced no opaque pixels")
    margins = (
        min(x for x, _ in opaque),
        dst_w - 1 - max(x for x, _ in opaque),
        min(y for _, y in opaque),
        dst_h - 1 - max(y for _, y in opaque),
    )
    if min(margins) < 1:
        print(
            f"warning: art touches the {args.size} box (margins l,r,t,b = {margins}); "
            "the selection ring will be cut on that side",
            file=sys.stderr,
        )
    out_w, out_h, x0, y0 = dst_w, dst_h, 0, 0
    stored = scaled

    args.out.mkdir(parents=True, exist_ok=True)
    write_bmp(args.out / "0.bmp", stored, out_w, out_h)
    meta = [
        f"// Generated by tools/{Path(__file__).name} from {args.source.name}.",
        "// Badge-sized so the draw is 1:1 and if_setoutline's ring is one pixel",
        "// at the size it is seen. Do not hand-edit; re-run the generator.",
        "count=1",
        f"palette={len(palette)}",
        *palette,
        f"sprite0={dst_w},{dst_h},{out_w},{out_h},{x0},{y0}",
    ]
    (args.out / "pack.meta").write_text("\n".join(meta) + "\n", encoding="utf-8")
    print(f"{args.out}: {dst_w}x{dst_h} nominal, {out_w}x{out_h} stored at {x0},{y0}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
