#!/usr/bin/env python3
"""Does a cache still match the content tree it was baked from?

Two modes, one contract.

**With `--lane`**: a marked-lane overlay.  The `ported/` lanes are excluded from
the ordinary content walk on purpose, so the only thing that puts their records
in front of a client is the lane's own bake (`mock230-cache-rs2012`,
`mock230-cache-summoning`).

**Without `--lane`**: the ordinary content bake — `mock230-cache`, the target
that walks the tree proper and writes `cache.osrs239.baked`.  This mode arrived
late, and its absence was a hole exactly as wide as the one the lane mode was
written to close: nothing watched the ordinary tree at all, so a launcher would
answer "up to date" for a cache whose `configs/`, interfaces, CS2 and sprites
were months behind the tree, and every lane overlay stacked on top inherited
that staleness through its own `--base`.

Both targets are `.PHONY`: asking for one always spends a full copy of a ~440 MB
base cache plus a repack and a verify, which is far too much to pay on every
launch — and not asking at all is how six freshly imported QBD models can sit
committed, staged, and still invisible in game.

So answer the question `make` would answer for itself if the target were a real
file, and let the launchers act on it:

    exit 0   bake — the cache is missing, incomplete, or something it reads is
             newer than the cache itself
    exit 1   skip — every input is older than the cache
    exit 2   error — usage, or an input that should exist does not

Callers must treat anything that is not exactly 1 as "bake".  A predicate that
cannot answer must not be read as "up to date": the failure mode of a needless
bake is two minutes, and the failure mode of a skipped one is a session spent
debugging content that was never in the cache.

The stamp is the OLDEST mtime among the cache's own files, not the newest. A
bake that died between `main_file_cache.dat2` and its idx tables leaves a cache
whose newest file is current and whose contents are a lie.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TREE = REPO_ROOT / "OSRS-Content" / "osrs239-content"

# Both stagers require the same layout: the lane at `ported/<name>`, and its
# binary payloads at `<asset root>/ported/<name>`.  This is the union of the two
# ASSET_ROOTS tuples (stage_rs2012_overlay.py, stage_summoning_overlay.py); a
# root a given lane does not use simply does not exist and is skipped.
ASSET_ROOTS = (
    "models",
    "animsets",
    "framemaps",
    "sprites",
    "textures",
    "synth",
    "samples",
    "patches",
    "songs",
    "maps",
    "interfaces",
    "scripts",
)

# What `cachepack pack --src` reads when `mock230-cache` walks the tree proper,
# named from cachepack itself rather than guessed: `configs/` and
# `server/scripts/` are its two declared config roots (the ROOTS/RANKS pair in
# cp_pack.c), `pack/` holds the name and id packs (cp_names.c), `fields/<type>.ini`
# is the field register (cp_fields.c), and the rest is one entry per asset kind
# in cp_assets.c's `g_assets`.
#
# Over-watching costs a needless bake and under-watching costs a session, so a
# root whose relevance is arguable belongs in this list. What is deliberately
# NOT here is the derivation corpus — `wiki/`, `npc_stats/`, `npc_combat/`,
# `port/`. Those feed the generators that write `configs/`, and `configs/` is
# watched: a corpus edit that has not been regenerated has not changed what a
# bake would produce, and treating it as if it had would rebake 440 MB for a
# file cachepack never opens.
CONTENT_ROOTS = (
    "configs",
    "pack",
    "fields",
    "animsets",
    "framemaps",
    "interfaces",
    "synth",
    "maps",
    "songs",
    "models",
    "sprites",
    "textures",
    "binary",
    "jingles",
    "scripts",
    "fonts",
    "samples",
    "patches",
    "worldmap",
    "dbindex",
    "animayas",
)

# `server/scripts` is a config root AND the ServerScript source tree, and only
# the first half is a cache input: `cp_walk_find` asks it for files whose
# extension names a config type, and a `.rs2` is not one of them.
#
# The distinction is the whole reason this root can be watched at all. Pure
# server work needs no bake (src/makefile, `mock230-cache`: "Pure server work —
# scripts, db tables, params, varps, npc/loc server fields — travels by
# mock230-scripts + mock230-servpack alone"), and a walk that did not filter
# would call the client cache stale on every `.rs2` keystroke and spend two
# minutes rebaking bytes that cannot have moved.
#
# One name per row of cp_types.c's register, plus `constant` (cp_common.c reads
# it to resolve `^names` inside the configs above).
CONFIG_EXTS = frozenset((
    "underlay", "overlay", "idk", "inv", "loc", "enum", "npc", "obj", "param",
    "seq", "spotanim", "varbit", "varp", "varc", "hitsplat", "healthbar",
    "struct", "mapelement", "dbrow", "dbtable", "constant",
))


# sscompile writes its packs into `server/scripts/build*/`, so the config walk
# above must prune them: script.dat is rewritten on every launch, and left in
# the walk it would make the client cache permanently stale by way of an output
# nothing bakes from. By PREFIX, for the reason server_scripts_stale.py records
# — a pack's output directory is a build parameter (MOCK230_SCRIPT_OUT), so a
# list of known names cannot be kept correct.
def is_output_dir(name: str) -> bool:
    return name == "build" or name.startswith("build_")


# The lanes are pruned from the ordinary walk because the ordinary bake does not
# read them (that is what `ported/` means) and because their own mode, above,
# already watches them. Without this, editing a lane would rebake the base cache
# AND the overlay, and only the second one would carry the edit.
def is_lane_dir(name: str) -> bool:
    return name == "ported"


STALE, FRESH, ERROR = 0, 1, 2


def oldest_cache_mtime(cache: Path) -> float | None:
    """The mtime of the cache's least recently written file, or None if absent."""

    if not (cache / "main_file_cache.dat2").is_file():
        return None
    stamps = [
        member.stat().st_mtime
        for member in cache.glob("main_file_cache.*")
        if member.is_file()
    ]
    return min(stamps) if stamps else None


# Last-dot equality, the rule cp_walk.h states rather than a suffix test:
# `all.npc` is a `.npc` file, `all.npc.compack` is a `.compack` one, and a name
# with no dot at all has no extension (`npc` is not a `.npc` file).
def extension_of(name: str) -> str:
    _, sep, ext = name.rpartition(".")
    return ext if sep else ""


def newest_under(path: Path, prune=None, exts=None) -> tuple[float, Path] | None:
    """The newest (mtime, file) at or below `path`, or None if it has no files.

    `prune` is called with each directory name and drops the ones it accepts;
    `exts`, when given, limits the walk to files whose last-dot extension is in
    it. Both are None for an ordinary whole-tree input.
    """

    if path.is_file():
        return (path.stat().st_mtime, path)
    if not path.is_dir():
        return None
    newest: tuple[float, Path] | None = None
    for parent, dirs, files in os.walk(path):
        if prune:
            dirs[:] = [d for d in dirs if not prune(d)]
        for name in files:
            if exts is not None and extension_of(name) not in exts:
                continue
            member = Path(parent) / name
            try:
                stamp = member.stat().st_mtime
            except OSError:
                # A file that vanished mid-walk is a tree being written right
                # now, which is the definition of stale.
                return (float("inf"), member)
            if newest is None or stamp > newest[0]:
                newest = (stamp, member)
    return newest


def inputs_for(tree: Path, lane: str, base: Path | None,
               extra: list[Path]) -> list[tuple[Path, object, object]]:
    paths = [tree / "ported" / lane]
    paths += [tree / root / "ported" / lane for root in ASSET_ROOTS]
    if base is not None:
        paths.append(base)
    paths += extra
    return [(path, None, None) for path in paths]


def content_inputs_for(tree: Path, base: Path | None,
                       extra: list[Path]) -> list[tuple[Path, object, object]]:
    """The ordinary bake's inputs: the tree proper, minus the marked lanes."""

    paths: list[tuple[Path, object, object]] = [
        (tree / root, is_lane_dir, None) for root in CONTENT_ROOTS
    ]
    paths.append((tree / "server" / "scripts", is_output_dir, CONFIG_EXTS))
    if base is not None:
        paths.append((base, None, None))
    paths += [(path, None, None) for path in extra]
    return paths


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True,
                        help="the composed cache the bake writes")
    parser.add_argument("--lane", default=None,
                        help="lane directory name below ported/; omit to ask "
                             "about the ordinary content bake instead")
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    parser.add_argument("--base", type=Path, default=None,
                        help="pristine cache the bake copies first")
    parser.add_argument("--input", type=Path, action="append", default=[],
                        dest="extra",
                        help="additional input (stager, Makefile, packer); "
                             "repeatable, missing paths are skipped")
    parser.add_argument("--force", action="store_true",
                        help="report stale without looking (also "
                             "TORIRS_FORCE_CACHE_BAKE=1)")
    args = parser.parse_args()

    if args.force or os.environ.get("TORIRS_FORCE_CACHE_BAKE") == "1":
        print("cache_overlay_stale: forced")
        return STALE

    if args.lane is not None:
        lane_dir = args.tree / "ported" / args.lane
        if not lane_dir.is_dir():
            print(f"cache_overlay_stale: no lane at {lane_dir}", file=sys.stderr)
            return ERROR
    elif not (args.tree / "configs").is_dir():
        # The same argument as the misspelled lane: a --tree that is not a
        # content tree is a caller that cannot be answered, and "nothing has
        # changed" is the one answer that must never be invented.
        print(f"cache_overlay_stale: no content tree at {args.tree}",
              file=sys.stderr)
        return ERROR

    stamp = oldest_cache_mtime(args.cache)
    if stamp is None:
        print(f"cache_overlay_stale: no cache at {args.cache}")
        return STALE

    if args.lane is not None:
        inputs = inputs_for(args.tree, args.lane, args.base, args.extra)
    else:
        inputs = content_inputs_for(args.tree, args.base, args.extra)

    for path, prune, exts in inputs:
        newest = newest_under(path, prune, exts)
        if newest is None:
            continue
        when, member = newest
        if when > stamp:
            print(f"cache_overlay_stale: {member} is newer than {args.cache}")
            return STALE

    print(f"cache_overlay_stale: {args.cache} is up to date")
    return FRESH


if __name__ == "__main__":
    raise SystemExit(main())
