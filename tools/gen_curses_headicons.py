#!/usr/bin/env python3
"""Build the Ancient Curses overhead-icon sprite group for the curses lane.

The problem this solves
-----------------------
Six curses put an icon over the caster's head — the four Deflect curses, Wrath
and Soul Split — and the client draws those from ONE sprite group, resolved by
name: `headicons_prayer`, group 440 in both cache eras. The launch port had no
icons of its own, so `curses.dbrow` pointed each of the six at whichever
osrs239 index looked closest:

    Deflect Melee    -> 0   which is OSRS's *Protect from Melee*
    Deflect Missiles -> 1   Protect from Missiles
    Deflect Magic    -> 2   Protect from Magic
    Wrath            -> 3   Retribution
    Soul Split       -> 5   Redemption
    Deflect Summoning-> 7   a melee+missiles COMBINED icon, not a curse at all

Every one of those is the wrong picture, and the last one is not even a
protection icon. rev558 has the right six; this writes them into the lane.

What it writes
--------------
`ported/rs558_ancient_curses/sprites_override/headicons_prayer/` — the whole
group, 24 osrs239 frames unchanged plus 6 appended:

    24 Deflect Melee      (rev558 frame 12, blue shield + sword)
    25 Deflect Missiles   (rev558 frame 14, blue shield + bow)
    26 Deflect Magic      (rev558 frame 13, blue shield + wizard hat)
    27 Deflect Summoning  (rev558 frame 15, blue shield + beast head)
    28 Wrath              (rev558 frame 19)
    29 Soul Split         (rev558 frame 20)

APPENDED, never substituted: indices 0..23 are what the standard book and every
npc record in the base cache address, and renumbering them to make room would
silently repoint 77 npc overheads and all 29 prayers. The lane's cache is the
only one that carries 24..29, and nothing outside the lane names them.

The group is staged over the base one (`tools/stage_curses_overlay.py`) under
its own name, so `STATIC_SPRITE_HEADICONS_PRAYER` still resolves — renaming it
into the lane's `ported_...` namespace would take every overhead in the game
down with it, player and npc alike.

Usage:
  tools/gen_curses_headicons.py --recon build/rs558-recon
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TREE = ROOT / "OSRS-Content/osrs239-content"
BASE_GROUP = TREE / "sprites/headicons_prayer"
LANE_OUT = TREE / "ported/rs558_ancient_curses/sprites_override/headicons_prayer"

# rev558 group 440 frame -> the curse it belongs to, in the order they are
# appended. Read off the rendered sheet (tools/build_curses_asset_review.py
# renders the same group): 12 carries a sword, 13 a wizard hat, 14 a bow, 15 the
# summoning beast head, 19 the Wrath blast and 20 the Soul Split burst.
#
# rev558 also ships combined icons — 16/17/18 are Deflect Summoning paired with
# each of the other three, because Deflect Summoning is in its own exclusion
# group and can be lit alongside them. This client stacks two icons vertically
# instead, which is the same information in the shape the engine already has, so
# the combined frames are not imported.
APPENDED = (
    (12, "deflect_melee"),
    (14, "deflect_missiles"),
    (13, "deflect_magic"),
    (15, "deflect_summoning"),
    (19, "wrath"),
    (20, "soulsplit"),
)


def read_meta(path: Path) -> dict:
    meta = {"count": 0, "palette": [], "frames": {}}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        key, _, value = line.partition("=")
        if key == "count":
            meta["count"] = int(value)
        elif key.startswith("p") and key[1:].isdigit():
            meta["palette"].append(int(value, 16))
        elif key.startswith("sprite") and key[6:].isdigit():
            meta["frames"][int(key[6:])] = value
    return meta


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--recon", type=Path, default=ROOT / "build/rs558-recon",
                    help="cachepack unpack of cache.rs558_20091209_ancientcurses")
    args = ap.parse_args()

    src558 = args.recon.resolve() / "sprites/sprite_440"
    if not (src558 / "pack.meta").is_file():
        raise SystemExit(
            f"gen_curses_headicons: no rev558 group at {src558}\n"
            "  regenerate it with the unpack in ANCIENT_CURSES.md §9")
    base = read_meta(BASE_GROUP / "pack.meta")
    rs558 = read_meta(src558 / "pack.meta")

    # One palette for the whole group, base first so every existing frame keeps
    # the index it had — the decoder re-derives a pixel's index by nearest
    # colour, but keeping the order stable means the 24 untouched frames
    # round-trip to the same bytes they came from.
    palette = list(base["palette"])
    seen = {c: i for i, c in enumerate(palette)}
    for colour in rs558["palette"]:
        if colour not in seen:
            seen[colour] = len(palette)
            palette.append(colour)
    if len(palette) > 256:
        raise SystemExit(
            f"gen_curses_headicons: merged palette is {len(palette)} entries; "
            "a sprite pack holds at most 256")

    if LANE_OUT.exists():
        shutil.rmtree(LANE_OUT)
    LANE_OUT.mkdir(parents=True)

    frames = []
    for i in range(base["count"]):
        shutil.copy2(BASE_GROUP / f"{i}.bmp", LANE_OUT / f"{i}.bmp")
        frames.append(base["frames"][i])
    for offset, (src_index, name) in enumerate(APPENDED):
        dst = base["count"] + offset
        # Byte copy, not a re-encode. cachepack's BMP reader takes 32-bit BGRA
        # bottom-up and nothing else — it checks `bpp != 32` and returns NULL,
        # which surfaces as "is indexed and in the cache, but no file is on
        # disk" and fails the bake. Pillow writes 24-bit for mode RGB, so
        # loading and saving these frames silently breaks them; both sides of
        # this copy were written by the same `bmp_write_file`, so the bytes are
        # already in the one form that reads back.
        shutil.copy2(src558 / f"{src_index}.bmp", LANE_OUT / f"{dst}.bmp")
        frames.append(rs558["frames"][src_index])
        print(f"  {dst:2d} {name:<18} <- rev558 group 440 frame {src_index}")

    with (LANE_OUT / "pack.meta").open("w") as meta:
        meta.write(f"// Sprite pack 440 — {len(frames)} sprites over a shared palette.\n")
        meta.write("// The BMPs carry the pixels; these are what a bitmap cannot hold.\n")
        meta.write("// Generated by tools/gen_curses_headicons.py — do not hand-edit.\n")
        meta.write(f"count={len(frames)}\n")
        meta.write(f"palette={len(palette)}\n")
        for i, colour in enumerate(palette):
            meta.write(f"p{i}=0x{colour:06X}\n")
        for i, geometry in enumerate(frames):
            meta.write(f"sprite{i}={geometry}\n")

    print(f"wrote {len(frames)} frames and a {len(palette)}-entry palette to {LANE_OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
