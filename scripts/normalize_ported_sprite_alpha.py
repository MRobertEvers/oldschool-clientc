#!/usr/bin/env python3
"""Flatten a pre-232 sprite's alpha plane into the index-0 transparency that
OldSchool 232+ can actually express.

A dat2 sprite carries transparency two ways: palette **index 0**, and a per-pixel
alpha plane behind `FLAG_ALPHA`. From OldSchool revision 232 the client stopped
honouring the plane -- `RSCACHE_SPRITELOAD_FLAG_OPAQUE_INDEX` in
`3rd/rscache/src/datatypes/dat2_sprites.c` forces every pixel whose palette index
is non-zero to alpha 0xFF *after* the plane is read. Native osrs239 content is
built for that rule (33 packs rely on it; only `hd_water_normal` and three
stray-pixel packs carry any partial alpha at all), so the rule is correct and
stays.

The RS2012 assets ported into the osrs239 cache are not. They came from an era
where the plane was authoritative, so their fully-clear pixels sit at palette
index 1 (colour 0x000001, the "black but not transparent" slot) with alpha 0.
Under the 232+ rule those are forced opaque and paint solid black -- the black
boxes framing the Queen Black Dragon HUD's healthbar.

A 239 cache has no way to say "honour my alpha plane", so the port has to be
re-baked down to what the era can express:

    alpha <  THRESHOLD  ->  (0,0,0,0)      => palette index 0, transparent
    alpha >= THRESHOLD  ->  rgb, alpha 255 => opaque

Writing an exactly-zero BGRA is what makes this work: `sprite_read` in
`3rd/rscache/tools/cachepack/cp_decode.c` only skips its nearest-palette match
when both alpha and rgb are zero, so a clear pixel that kept a colour would be
matched straight back to a non-zero index. Once every pixel is either
(index 0, alpha 0) or (index != 0, alpha 255), the encoder finds the alphas
derivable, drops `FLAG_ALPHA` entirely, and the sprite renders identically under
both eras.

This is lossy by construction -- the soft edges become 1-bit -- and it must be
re-run after any re-port of the RS2012 assets.

Usage:
    python scripts/normalize_ported_sprite_alpha.py [--check] [PACK_GLOB ...]

    --check  report what would change and exit non-zero if anything would,
             without writing. Everything else rewrites the BMPs in place.
"""

import glob
import os
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# Default scope: the RS2012 Queen Black Dragon / Tormented Demon UI sprites.
# `rs2012_material_*` in the same directory are 3D texture sources that go
# through the procedural-texture bake, not the 2D blitter, and are left alone.
DEFAULT_GLOBS = [
    "OSRS-Content/osrs239-content/sprites/ported/rs2012_qbd_td/rs2012_qbd_sprite_*",
]

# Alpha at or above this becomes opaque, below it becomes clear. 128 is the
# half-coverage boundary, which keeps an anti-aliased shape closest to its
# original extent; the ported ramps are weighted low (9122 of 12910 partial
# pixels fall under 128), so a lower cut would fatten every soft edge.
THRESHOLD = 128

# Palette index 0 is the transparent slot, so an opaque pixel must never carry a
# colour that could be matched back to it. Pure black moves to the cache's usual
# "black but not transparent" value. (No pixel in the current port needs this --
# the guard is here so a future re-port cannot silently punch holes.)
OPAQUE_BLACK = (0, 0, 1)  # b, g, r


def read_bmp(path):
    """Parse the 32-bit BGRA BMP that `sprite_write` emits. Returns (data, offset, count)."""
    data = bytearray(Path(path).read_bytes())
    if len(data) < 54 or data[0:2] != b"BM":
        return None
    offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp != 32 or width <= 0 or height <= 0:
        return None
    count = width * height
    if offset + count * 4 > len(data):
        return None
    return data, offset, count


def normalize(path):
    """Rewrite one BMP in place. Returns (cleared, opaqued, blackened) pixel counts."""
    parsed = read_bmp(path)
    if parsed is None:
        return None
    data, offset, count = parsed

    cleared = opaqued = blackened = 0
    for i in range(count):
        p = offset + i * 4
        b, g, r, a = data[p], data[p + 1], data[p + 2], data[p + 3]

        if a < THRESHOLD:
            if (b, g, r, a) != (0, 0, 0, 0):
                cleared += 1
            data[p : p + 4] = b"\x00\x00\x00\x00"
            continue

        if a != 255:
            opaqued += 1
        if (r, g, b) == (0, 0, 0):
            b, g, r = OPAQUE_BLACK
            blackened += 1
        data[p : p + 4] = bytes((b, g, r, 255))

    return data, cleared, opaqued, blackened


def main(argv):
    check_only = "--check" in argv
    patterns = [a for a in argv if not a.startswith("--")] or DEFAULT_GLOBS

    packs = []
    for pattern in patterns:
        for d in sorted(glob.glob(str(REPO / pattern))):
            if os.path.isdir(d) and os.path.exists(os.path.join(d, "pack.meta")):
                packs.append(d)

    if not packs:
        print("no sprite packs matched", file=sys.stderr)
        return 2

    touched = 0
    total_cleared = total_opaqued = total_blackened = 0

    for pack in packs:
        pack_cleared = pack_opaqued = pack_blackened = 0
        for bmp in sorted(glob.glob(os.path.join(pack, "*.bmp"))):
            result = normalize(bmp)
            if result is None:
                print(f"skipped (not 32-bit BMP): {bmp}", file=sys.stderr)
                continue
            data, cleared, opaqued, blackened = result
            pack_cleared += cleared
            pack_opaqued += opaqued
            pack_blackened += blackened
            if (cleared or opaqued or blackened) and not check_only:
                Path(bmp).write_bytes(bytes(data))

        if pack_cleared or pack_opaqued or pack_blackened:
            touched += 1
            total_cleared += pack_cleared
            total_opaqued += pack_opaqued
            total_blackened += pack_blackened
            print(
                f"{os.path.relpath(pack, REPO)}: "
                f"{pack_cleared} -> clear, {pack_opaqued} -> opaque"
                + (f", {pack_blackened} black nudged" if pack_blackened else "")
            )

    verb = "would change" if check_only else "rewrote"
    print(
        f"\n{verb} {touched} of {len(packs)} packs: "
        f"{total_cleared} pixels to clear, {total_opaqued} to opaque, "
        f"{total_blackened} black nudged off index 0"
    )
    return 1 if (check_only and touched) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
