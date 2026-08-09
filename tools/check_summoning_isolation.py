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

    # The protocol roster is deliberately contiguous through stat 24.  Also
    # pin the only two names this slice adds: Sailing is the retained stat 23,
    # and the bare skill spelling `summoning` may occur only here (ported config
    # records use summoning_* so trigger subjects cannot capture this symbol).
    stat_rows: dict[int, str] = {}
    stat_pack = tree / "pack" / "stat.pack"
    if stat_pack.is_file():
        for line_no, line in data_lines(stat_pack):
            checked += 1
            if "=" not in line:
                errors.append(f"{stat_pack}:{line_no}: expected id=name")
                continue
            raw_id, name = line.split("=", 1)
            try:
                stat_id = int(raw_id)
            except ValueError:
                errors.append(f"{stat_pack}:{line_no}: invalid stat id {raw_id!r}")
                continue
            if stat_id in stat_rows:
                errors.append(f"{stat_pack}:{line_no}: duplicate stat id {stat_id}")
            stat_rows[stat_id] = name
        if sorted(stat_rows) != list(range(25)):
            errors.append(f"{stat_pack}: stat ids must be contiguous 0..24")
        if stat_rows.get(23) != "sailing" or stat_rows.get(24) != "summoning":
            errors.append(f"{stat_pack}: expected 23=sailing and 24=summoning")
    else:
        errors.append(f"missing stat pack: {stat_pack}")

    summoning_bindings: list[str] = []
    for root_name in ("pack", "configs"):
        root = tree / root_name
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in {".pack", ".alloc", ".compack"}:
                continue
            for line_no, line in data_lines(path):
                checked += 1
                if line.split("=", 1)[-1].strip() == "summoning":
                    summoning_bindings.append(f"{path}:{line_no}")
    if len(summoning_bindings) != 1 or not summoning_bindings[0].startswith(f"{stat_pack}:"):
        errors.append(
            "bare name 'summoning' must bind only stat 24; found " +
            (", ".join(summoning_bindings) if summoning_bindings else "none")
        )

    # 1198 is a hole in the base clientscript index.  The flag-on lane claims
    # that hole for its dedicated stats cell script; pin both sides so a future
    # cache refresh cannot turn the add into an accidental overwrite.
    base_scripts = tree / "pack" / "12_clientscripts.pack"
    lane_scripts = lane / "pack" / "12_clientscripts.pack"
    base_script_ids: set[int] = set()
    if base_scripts.is_file():
        for line_no, line in data_lines(base_scripts):
            checked += 1
            try:
                base_script_ids.add(int(line.split("=", 1)[0]))
            except ValueError:
                errors.append(f"{base_scripts}:{line_no}: invalid clientscript id")
    else:
        errors.append(f"missing base clientscript pack: {base_scripts}")
    if 1198 in base_script_ids:
        errors.append(f"{base_scripts}: clientscript 1198 is no longer vacant")

    lane_script_rows: dict[int, str] = {}
    if lane_scripts.is_file():
        for line_no, line in data_lines(lane_scripts):
            checked += 1
            if "=" not in line:
                errors.append(f"{lane_scripts}:{line_no}: expected id=name")
                continue
            raw_id, name = line.split("=", 1)
            try:
                lane_script_rows[int(raw_id)] = name
            except ValueError:
                errors.append(f"{lane_scripts}:{line_no}: invalid clientscript id")
        expected_scripts = {393: "script_393", 1198: "summoning_stats_init", 8950: "script_8950"}
        if lane_script_rows != expected_scripts:
            errors.append(f"{lane_scripts}: expected {expected_scripts}, got {lane_script_rows}")
        for name in lane_script_rows.values():
            checked += 1
            if not (lane / "scripts" / f"{name}.cs2").is_file():
                errors.append(f"{lane_scripts}: no scripts/{name}.cs2")
    else:
        errors.append(f"missing Summoning clientscript pack: {lane_scripts}")

    # The stat symbol is the one deliberate bare spelling.  No ported record
    # in any other namespace may use it: trigger subjects resolve unknown kinds
    # first-match-wins, so a component/config/asset called exactly `summoning`
    # can silently capture a later script trigger.
    for path in lane.rglob("*"):
        if not path.is_file() or path.suffix not in TEXT_SUFFIXES | {".pack"}:
            continue
        for line_no, line in data_lines(path):
            checked += 1
            value = line.split("=", 1)[-1].strip() if "=" in line else ""
            if line == "[summoning]" or value == "summoning":
                errors.append(f"{path}:{line_no}: ported record must use the summoning_ prefix")

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
