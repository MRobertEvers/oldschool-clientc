#!/usr/bin/env python3
"""Is a compiled server script pack still current against its sources?

`mock230-scripts`/`mock230-scripts-summoning` have no output file matching
their own target name, so `make` reruns their recipe -- a ~15k-script
sscompile pass -- on every single launch, `.PHONY` or not. That is safe but
slow, so the launchers ask this predicate first and skip the make invocation
when nothing that feeds sscompile has changed since script.dat/script.idx
were last written.

    exit 0   bake  -- missing/incomplete output, or an input is newer than it
    exit 1   skip  -- every input is older than the output
    exit 2   error -- usage, or a --tree whose server/scripts is missing

Callers must treat anything that is not exactly 1 as "bake" -- see
cache_overlay_stale.py for the same contract and the reasoning behind it.

The stamp is the OLDEST mtime of script.dat/script.idx, not the newest: a
compile that died between the two leaves one current and one stale, and the
pair must be read as a matched set or not at all.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TREE = REPO_ROOT / "OSRS-Content" / "osrs239-content"

# server/scripts holds both the .rs2 sources sscompile reads (--src) and the
# build*/ directories sscompile writes into. Walking the source tree for
# freshness must prune the output dirs, or the plain build would always look
# stale right after a lane build runs, and vice versa.
#
# By PREFIX, not by a list of names. The list was ("build", "build_summoning"),
# from when those were the only two packs anyone compiled; `build_curses` and
# `build_summoning_curses` arrived later and were never added, so each of them
# made every other pack look stale forever. Now that a pack's output directory
# is a build parameter (MOCK230_SCRIPT_OUT) rather than one of a fixed few, a
# list of known names cannot be kept correct at all.
def is_output_dir(name: str) -> bool:
    return name == "build" or name.startswith("build_")

STALE, FRESH, ERROR = 0, 1, 2


def oldest_output_mtime(out_dir: Path) -> float | None:
    """The mtime of script.dat/script.idx's older half, or None if either is missing."""

    dat = out_dir / "script.dat"
    idx = out_dir / "script.idx"
    if not dat.is_file() or not idx.is_file():
        return None
    return min(dat.stat().st_mtime, idx.stat().st_mtime)


def newest_under(path: Path, prune=None) -> tuple[float, Path] | None:
    """The newest (mtime, file) at or below `path`, or None if it has no files."""

    if path.is_file():
        return (path.stat().st_mtime, path)
    if not path.is_dir():
        return None
    newest: tuple[float, Path] | None = None
    for parent, dirs, files in os.walk(path):
        if prune:
            dirs[:] = [d for d in dirs if not prune(d)]
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


def inputs_for(tree: Path, extra: list[Path]) -> list[tuple[Path, object]]:
    # The lane component-roots are walked whole rather than limited to their
    # `pack`/`configs`/`interface_overlays` subdirectories: sscompile only
    # reads a subset of each lane, but a broader freshness check only ever
    # costs an unnecessary rebuild, never a missed one.
    paths = [
        (tree / "server" / "scripts", is_output_dir),
        (tree / "pack", None),
        (tree / "configs", None),
        # Every lane, rather than the two that existed when this was written:
        # a lane is discovered from `ported/<lane>/lane.ini` now, so a list of
        # lane names here would have to be kept in step with the tree by hand,
        # and the failure mode of forgetting is a lane whose edits never
        # trigger a recompile.
        (tree / "ported", None),
    ]
    paths += [(p, None) for p in extra]
    return paths


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True,
                        help="server/scripts/build or .../build_summoning")
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    parser.add_argument("--input", type=Path, action="append", default=[],
                        dest="extra",
                        help="additional input (sscompile source, Makefile, "
                             "staging tool); repeatable, missing paths are "
                             "skipped")
    parser.add_argument("--force", action="store_true",
                        help="report stale without looking (also "
                             "TORIRS_FORCE_SCRIPT_BUILD=1)")
    args = parser.parse_args()

    if args.force or os.environ.get("TORIRS_FORCE_SCRIPT_BUILD") == "1":
        print("server_scripts_stale: forced")
        return STALE

    src_dir = args.tree / "server" / "scripts"
    if not src_dir.is_dir():
        print(f"server_scripts_stale: no sources at {src_dir}", file=sys.stderr)
        return ERROR

    stamp = oldest_output_mtime(args.out)
    if stamp is None:
        print(f"server_scripts_stale: no script pack at {args.out}")
        return STALE

    for path, prune in inputs_for(args.tree, args.extra):
        newest = newest_under(path, prune)
        if newest is None:
            continue
        when, member = newest
        if when > stamp:
            print(f"server_scripts_stale: {member} is newer than {args.out}")
            return STALE

    print(f"server_scripts_stale: {args.out} is up to date")
    return FRESH


if __name__ == "__main__":
    raise SystemExit(main())
