#!/usr/bin/env python3
"""Assert that the Summoning client lane is unreachable in a flag-off bake."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TREE = REPO_ROOT / "OSRS-Content" / "osrs239-content"
LANE = Path("ported") / "scape2009_summoning"
PREFIX = "summoning_"
PREFIX_TOKEN = re.compile(r"(?<![A-Za-z0-9_])summoning_")
TEXT_SUFFIXES = {
    ".compack",
    ".enum",
    ".if",
    ".loc",
    ".npc",
    ".obj",
    ".seq",
    ".spotanim",
    ".varbit",
    ".varp",
    ".cs2",
}


def data_lines(path: Path):
    for line_no, raw in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
        stripped = raw.strip()
        if stripped and not stripped.startswith(("#", ";", "//")):
            yield line_no, stripped


def check(tree: Path) -> int:
    tree = tree.resolve()
    lane = tree / LANE
    errors: list[str] = []
    checked = 0

    required = (
        lane / "PROVENANCE.md",
        tree / "port" / "summoning_530.map",
        REPO_ROOT / "tools" / "stage_summoning_overlay.py",
    )
    for path in required:
        checked += 1
        if not path.is_file() or path.is_symlink():
            errors.append(f"missing plain isolation artifact: {path}")

    # A symlink can make a directory outside cachepack's roots reachable from
    # one inside them (or vice versa), invalidating the path-based flag.
    for path in tree.rglob("*"):
        checked += 1
        if not path.is_symlink():
            continue
        try:
            target = path.resolve(strict=True)
        except OSError as exc:
            errors.append(f"broken symlink {path}: {exc}")
            continue
        allowed_roots = [lane]
        allowed_roots.extend(tree / root / LANE for root in ("models", "animsets", "framemaps", "sprites", "synth"))
        if any(target == root or root in target.parents for root in allowed_roots):
            errors.append(f"symlink reaches the Summoning client lane: {path} -> {target}")
        if any(path == root or root in path.parents for root in allowed_roots):
            errors.append(f"Summoning client lane contains a symlink: {path} -> {target}")

    # Base membership and asset packs are the only way otherwise-inert authored
    # records/assets become part of the ordinary bake.  Ported claims belong in
    # the staged lane's pack directory, never here.
    pack_dir = tree / "pack"
    for path in sorted(pack_dir.glob("*.client")) + sorted(pack_dir.glob("[0-9]*.pack")):
        for line_no, line in data_lines(path):
            checked += 1
            value = line.split("=", 1)[-1].strip()
            if value.startswith(PREFIX) or f"/{LANE.as_posix()}/" in f"/{value}/":
                errors.append(f"{path}:{line_no}: flag-off pack exposes {value!r}")

    # Configs, interfaces and clientscripts are direct cachepack walk roots.
    # The prefix is intentionally stronger than the generic English word: the
    # base cache has unrelated POH/quest records containing "summoning".
    for root_name in ("configs", "interfaces", "scripts"):
        root = tree / root_name
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in TEXT_SUFFIXES:
                continue
            checked += 1
            relative = path.relative_to(tree).as_posix()
            if any(part.startswith(PREFIX) for part in Path(relative).parts):
                errors.append(f"flag-off client root contains a ported path: {relative}")
            for line_no, line in data_lines(path):
                if PREFIX_TOKEN.search(line):
                    errors.append(f"{path}:{line_no}: flag-off client root names {PREFIX!r}")

    if checked == 0:
        errors.append("summoning isolation executed zero checks")
    for error in errors:
        print(f"check_summoning_isolation: error: {error}", file=sys.stderr)
    print(f"check_summoning_isolation: {checked} checks, {len(errors)} errors")
    return 1 if errors else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    parser.add_argument("--check", action="store_true", required=True)
    args = parser.parse_args()
    return check(args.tree)


if __name__ == "__main__":
    raise SystemExit(main())
