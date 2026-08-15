#!/usr/bin/env python3
"""Stage the Ancient Curses lane into a disposable tree for the flag-on bake.

`ported/` is deliberately outside the ordinary content walk, so a lane only
reaches a cache through a stage like this one. The output is thrown away after
`cachepack pack` reads it; nothing here is authored, and nothing writes back
into the content tree.

This is much smaller than tools/stage_summoning_overlay.py because the lane is
much smaller: no npcs, no locs, no map squares, no database rows, no interface
overlays. It contributes twenty objs, one enum, two varps, two varbits, six
patched clientscripts, and the assets those reference.

  tools/stage_curses_overlay.py --tree OSRS-Content/osrs239-content \
                               --out build/curses-overlay
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

LANE = Path("ported") / "rs558_ancient_curses"

# Asset trees the lane owns a subdirectory of. Each mirrors its destination
# path, so `models/ported/rs558_ancient_curses/...` stages to the same place.
ASSET_ROOTS = ("models", "animsets", "framemaps", "synth", "sprites")

# Config records the lane adds to the cache. Named one by one rather than
# globbed: a file appearing here by accident would silently enter the flag-on
# cache, and the whole point of the lane is that nothing does so unintentionally.
LANE_CONFIGS = ("curses.obj", "curses.enum", "curses.varp", "curses.varbit")

# The lane's overriding member indexes. These carry the base tree's ids plus the
# lane's minted ones (varp 5705/5706, varbit 20412/20413) and must replace the
# base copies rather than sit beside them.
LANE_COMPACKS = ("all.varp.compack", "all.varbit.compack")

# Asset groups the lane REPLACES rather than adds to, staged under the base
# tree's own name instead of under `ported/rs558_ancient_curses/`.
#
# There is exactly one, and it has to work this way. Six curses draw an overhead
# icon, the client finds that art by resolving the name `headicons_prayer`
# (static_sprites.c), and a name is a whole group — there is no way to add a
# frame to a group from beside it. So the lane ships the group entire: the base
# cache's 24 frames plus its own six appended, written by
# tools/gen_curses_headicons.py. Staged here rather than through ASSET_ROOTS
# because the destination is `sprites/headicons_prayer`, NOT
# `sprites/ported/rs558_ancient_curses/...` — the packed gameval name has to stay
# byte-identical or the client's lookup fails and every overhead in the game,
# player and npc, stops drawing.
LANE_SPRITE_OVERRIDES = ("headicons_prayer",)


def fail(message: str) -> SystemExit:
    return SystemExit(f"stage_curses_overlay: {message}")


def copy_file(source: Path, target: Path) -> None:
    if source.is_symlink():
        raise fail(f"refusing a symlink: {source}")
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def copy_tree(source: Path, target: Path) -> int:
    if not source.is_dir():
        return 0
    count = 0
    for path in sorted(source.rglob("*")):
        if path.is_dir():
            continue
        copy_file(path, target / path.relative_to(source))
        count += 1
    return count


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--tree", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    tree = args.tree.resolve()
    out = args.out.resolve()
    if tree == out or tree in out.parents:
        raise fail("output must not be the content tree or one of its parents")

    lane = tree / LANE
    if not lane.is_dir():
        raise fail(f"missing lane: {lane}")
    # The marker the Summoning lane also insists on: a lane without recorded
    # provenance must not be able to enter a cache.
    if not (lane / "ANCIENT_CURSES.md").is_file():
        raise fail(f"missing provenance: {lane / 'ANCIENT_CURSES.md'}")

    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    copied = 0

    # --- lookup context ---------------------------------------------------
    # The packer needs to resolve names the lane references but does not own.
    for required in ("meta.ini", "content.ini"):
        source = tree / required
        if not source.is_file():
            raise fail(f"missing support file: {source}")
        copy_file(source, out / required)
        copied += 1
    copied += copy_tree(tree / "fields", out / "fields")

    for compack in sorted((tree / "configs").glob("*.compack")):
        copy_file(compack, out / "configs" / compack.name)
        copied += 1
    if copied < 3:
        raise fail("no config member indexes staged; refusing a vacuous stage")

    # --- the lane's own member indexes, over the base ones -----------------
    for name in LANE_COMPACKS:
        source = lane / "configs" / name
        if not source.is_file():
            raise fail(f"missing lane member index: {source}")
        copy_file(source, out / "configs" / name)

    # --- the lane's records ------------------------------------------------
    for name in LANE_CONFIGS:
        source = lane / "configs" / name
        if not source.is_file():
            raise fail(f"missing lane config: {source}")
        copy_file(source, out / "configs" / LANE / name)
        copied += 1

    # Imported spotanim and seq records, written by `cachepack import`.
    for name in ("curses.spotanim", "curses.seq"):
        source = lane / "configs" / name
        if not source.is_file():
            raise fail(f"missing imported config: {source}")
        copy_file(source, out / "configs" / LANE / name)
        copied += 1

    # --- packs, scripts and assets ----------------------------------------
    staged_packs = copy_tree(lane / "pack", out / "pack")
    if staged_packs == 0:
        raise fail("no pack files staged")
    copied += staged_packs

    scripts = copy_tree(lane / "scripts", out / "scripts")
    if scripts == 0:
        raise fail("no clientscripts staged")
    copied += scripts

    assets = 0
    for root in ASSET_ROOTS:
        assets += copy_tree(tree / root / LANE, out / root / LANE)
    if assets == 0:
        raise fail("no assets staged; the lane cannot be asset-free")
    copied += assets

    for name in LANE_SPRITE_OVERRIDES:
        source = lane / "sprites_override" / name
        if not source.is_dir():
            raise fail(
                f"missing sprite override: {source}\n"
                "  regenerate with tools/gen_curses_headicons.py")
        staged = copy_tree(source, out / "sprites" / name)
        if staged == 0:
            raise fail(f"empty sprite override: {source}")
        assets += staged
        copied += staged

    print(f"staged {copied} file(s) into {out}")
    print(f"  configs {len(LANE_CONFIGS) + 2}  packs {staged_packs}  "
          f"scripts {scripts}  assets {assets}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
