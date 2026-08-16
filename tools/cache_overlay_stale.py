#!/usr/bin/env python3
"""Does a marked-lane cache still match the content tree it was baked from?

The `ported/` lanes are excluded from the ordinary content walk on purpose, so
the only thing that puts their records in front of a client is the lane's own
bake (`mock230-cache-rs2012`, `mock230-cache-summoning`).  Those targets are
`.PHONY`: asking for one always spends a full copy of a ~440 MB base cache plus
a repack and a verify, which is far too much to pay on every launch — and not
asking at all is how six freshly imported QBD models can sit committed, staged,
and still invisible in game.

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


def newest_under(path: Path) -> tuple[float, Path] | None:
    """The newest (mtime, file) at or below `path`, or None if it has no files."""

    if path.is_file():
        return (path.stat().st_mtime, path)
    if not path.is_dir():
        return None
    newest: tuple[float, Path] | None = None
    for parent, _dirs, files in os.walk(path):
        for name in files:
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
               extra: list[Path]) -> list[Path]:
    paths = [tree / "ported" / lane]
    paths += [tree / root / "ported" / lane for root in ASSET_ROOTS]
    if base is not None:
        paths.append(base)
    paths += extra
    return paths


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True,
                        help="the composed cache the bake writes")
    parser.add_argument("--lane", required=True,
                        help="lane directory name below ported/")
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

    lane_dir = args.tree / "ported" / args.lane
    if not lane_dir.is_dir():
        print(f"cache_overlay_stale: no lane at {lane_dir}", file=sys.stderr)
        return ERROR

    stamp = oldest_cache_mtime(args.cache)
    if stamp is None:
        print(f"cache_overlay_stale: no cache at {args.cache}")
        return STALE

    for path in inputs_for(args.tree, args.lane, args.base, args.extra):
        newest = newest_under(path)
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
