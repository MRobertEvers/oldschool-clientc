#!/usr/bin/env python3
"""Author the Summoning orb's art from the cache's own orb sprites.

The Summoning orb is the fifth member of a set the cache already ships — the
hitpoints, prayer, run-energy and special-attack orbs of interface 160, and the
same four the `minimap_orbs` plugin carries as PNGs. It has to look like one of
them, and the only way that happens without hand-tuned offsets is to *be* one
of them: same 57x34 stone plate, same 26x26 gauge disc, same icon box.

So nothing here draws pixels. Every sprite this writes is a transform of a
sprite in `sprites/`, and the three transforms are the three things that make
this orb the fifth one rather than a copy of the fourth:

  mirror   The other four hang off the minimap's bottom-LEFT arc, so their
           plates put the number panel on the left and the gauge on the right,
           facing the map. This one sits on the bottom-RIGHT, where that plate
           is backwards: the gauge points away from the map and the panel runs
           off the edge. Mirroring the CHROME — and only the chrome — turns it
           round. The discs and the icon are deliberately not mirrored: they
           are lit from the upper left like every other piece of RS interface
           art, and flipping them would light this one orb from the other side.

  hue      The gauge is the special-attack orb's disc rotated -24 degrees, into
           the teal-green Summoning already uses for its own points orb
           (0x13A48B, rev-530 sprite 1244). Rotating hue and keeping saturation
           and value is what preserves the sphere: the highlight, the terminator
           and the rim stay exactly where 1607 put them, so the disc still
           reads as the same ball of glass, in a different colour.

  rebox    The imported rev-530 wolf head is 16x17 of art in a 20x20 box; every
           orb icon in this cache is its art centred in a 26x26 box (the heart
           is 15x14, the boot 15x18, the crossed swords 16x16). Same pixels,
           bigger box, so the icon can be placed at the disc's own x/y like the
           other four instead of at an offset of its own.

The output is `sprites/ported/scape2009_summoning/<name>/` — a BMP plus the
`pack.meta` sidecar cachepack reads back, with the palette rewritten to match
the pixels, because sprite_read resolves each pixel through the RECORDED
palette and a colour that is not in it lands on the nearest one that is.

    python3 tools/make_summoning_orb_sprites.py
"""

from __future__ import annotations

import argparse
import colorsys
import struct
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SPRITES = REPO_ROOT / "OSRS-Content" / "osrs239-content" / "sprites"
LANE = SPRITES / "ported" / "scape2009_summoning"

# Cyan (192 deg) to teal-green (168 deg). Small enough that the orb still reads
# as a member of the set beside the blue special-attack one, far enough that
# nobody has to compare them side by side to tell which is which.
HUE_DEGREES = -24.0


def read_bmp(path: Path) -> tuple[list[int], int, int]:
    """32-bit BGRA, bottom-up, exactly as cachepack's bmp_read_file expects."""
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise SystemExit(f"{path}: not a BMP")
    offset = struct.unpack_from("<i", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
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
    """Byte-for-byte what 3rd/bmp's bmp_write_file writes, biSizeImage 0 included.

    Not a nicety: matching it is what lets a sprite this tool only reboxes come
    out identical to the cachepack decode it came from, so `cmp` still proves the
    pixels were untouched.
    """
    body = bytearray()
    for y in range(height - 1, -1, -1):
        for x in range(width):
            p = pixels[y * width + x]
            body += bytes(((p >> 0) & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF, (p >> 24) & 0xFF))
    header = struct.pack(
        "<2sIHHIIiiHHIIiiII",
        b"BM",
        54 + len(body),
        0,
        0,
        54,
        40,
        width,
        height,
        1,
        32,
        0,
        0,
        0,
        0,
        0,
        0,
    )
    path.write_bytes(header + body)


def read_meta(path: Path) -> tuple[list[int], tuple[int, int, int, int, int, int]]:
    """Parse a one-sprite pack.meta into its palette and that sprite's geometry."""
    palette: dict[int, int] = {}
    geometry: tuple[int, ...] | None = None
    count = None
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        key, _, value = line.partition("=")
        if key == "count":
            count = int(value)
        elif key.startswith("p") and key[1:].isdigit():
            palette[int(key[1:])] = int(value, 16)
        elif key == "sprite0":
            geometry = tuple(int(field) for field in value.split(","))
    if count != 1:
        raise SystemExit(f"{path}: this tool only handles one-sprite packs, not {count}")
    if geometry is None or len(geometry) != 6:
        raise SystemExit(f"{path}: no sprite0 geometry")
    return [palette[i] for i in range(len(palette))], geometry  # type: ignore[return-value]


def write_group(
    out: Path,
    provenance: str,
    palette: list[int],
    geometry: tuple[int, int, int, int, int, int],
    pixels: list[int],
) -> None:
    out.mkdir(parents=True, exist_ok=True)
    write_bmp(out / "0.bmp", pixels, geometry[2], geometry[3])
    lines = [f"// {provenance}", "// The BMPs carry the pixels; these are what a bitmap cannot hold.", "count=1", f"palette={len(palette)}"]
    lines += [f"p{i}=0x{colour:06X}" for i, colour in enumerate(palette)]
    lines.append("sprite{}={}".format(0, ",".join(str(field) for field in geometry)))
    (out / "pack.meta").write_text("\n".join(lines) + "\n", encoding="utf-8")


def mirror(pixels: list[int], width: int, height: int) -> list[int]:
    out = [0] * (width * height)
    for y in range(height):
        row = y * width
        for x in range(width):
            out[row + x] = pixels[row + (width - 1 - x)]
    return out


def hue_rotate(rgb: int, degrees: float) -> int:
    """Rotate hue, keep saturation and value — the shading survives untouched."""
    r, g, b = ((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    r, g, b = colorsys.hsv_to_rgb((h + degrees / 360.0) % 1.0, s, v)
    return (round(r * 255) << 16) | (round(g * 255) << 8) | round(b * 255)


def source(name: str) -> tuple[list[int], tuple[int, int, int, int, int, int], list[int]]:
    directory = SPRITES / name
    palette, geometry = read_meta(directory / "pack.meta")
    pixels, width, height = read_bmp(directory / "0.bmp")
    if (width, height) != (geometry[2], geometry[3]):
        raise SystemExit(
            f"{directory}: pack.meta stores {geometry[2]}x{geometry[3]}, the BMP is {width}x{height}"
        )
    return palette, geometry, pixels


def make_mirrored_chrome(src_name: str, out_name: str, provenance: str) -> None:
    palette, geometry, pixels = source(src_name)
    width, height, crop_w, crop_h, off_x, off_y = geometry
    write_group(
        LANE / out_name,
        provenance,
        palette,
        (width, height, crop_w, crop_h, width - crop_w - off_x, off_y),
        mirror(pixels, crop_w, crop_h),
    )


def make_copy(src_name: str, out_name: str, provenance: str) -> None:
    palette, geometry, pixels = source(src_name)
    write_group(LANE / out_name, provenance, palette, geometry, pixels)


def make_gauge(src_name: str, out_name: str, provenance: str) -> None:
    palette, geometry, pixels = source(src_name)
    # Index 0 is the pack's transparent slot. It is not a colour anyone sees, and
    # rotating it would move it off 0x000000 and stop sprite_read recognising it.
    rotated = [palette[0]] + [hue_rotate(colour, HUE_DEGREES) for colour in palette[1:]]
    remap = {palette[i]: rotated[i] for i in range(1, len(palette))}
    out = []
    for pixel in pixels:
        rgb, alpha = pixel & 0xFFFFFF, pixel & 0xFF000000
        out.append(alpha | remap.get(rgb, rgb))
    write_group(LANE / out_name, provenance, rotated, geometry, out)


def make_reboxed_icon(src_name: str, out_name: str, box: int, provenance: str) -> None:
    palette, geometry, pixels = source(src_name)
    crop_w, crop_h = geometry[2], geometry[3]
    if crop_w > box or crop_h > box:
        raise SystemExit(f"{src_name}: {crop_w}x{crop_h} of art does not fit a {box}x{box} box")
    write_group(
        LANE / out_name,
        provenance,
        palette,
        (box, box, crop_w, crop_h, (box - crop_w) // 2, (box - crop_h) // 2),
        pixels,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()

    make_mirrored_chrome(
        "orb_frame_0",
        "summoning_orb_backing",
        "Sprite 1071 (orb_frame,0) mirrored — the orb plate, gauge socket on the left.",
    )
    make_mirrored_chrome(
        "orb_frame_1",
        "summoning_orb_backing_hover",
        "Sprite 1072 (orb_frame,1) mirrored — the same plate, hovered.",
    )
    make_gauge(
        "orb_filler_9",
        "summoning_orb_indicator",
        f"Sprite 1607 (the special-attack gauge) hue-rotated {HUE_DEGREES:+.0f} degrees to Summoning teal.",
    )
    make_copy(
        "orb_filler_0",
        "summoning_orb_empty",
        "Sprite 1059 (orb_filler,0) — the unlit gauge every orb covers its disc with.",
    )
    make_reboxed_icon(
        "ported/scape2009_summoning/summoning_orb_icon",
        "summoning_orb_icon",
        26,
        "The rev-530 orb wolf head, reboxed 20x20 -> 26x26 to match orb_icon,0..3.\n"
        "// Rewritten in place, and idempotent: the art is untouched, only the box "
        "around it and\n// the offset that centres it in the box are recomputed.",
    )
    for name in (
        "summoning_orb_backing",
        "summoning_orb_backing_hover",
        "summoning_orb_indicator",
        "summoning_orb_empty",
        "summoning_orb_icon",
    ):
        print(f"wrote {(LANE / name).relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
