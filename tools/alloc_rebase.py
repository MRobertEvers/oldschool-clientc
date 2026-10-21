#!/usr/bin/env python3
"""Move a tree's server-allocated ids above a newer cache's, after a cache upgrade.

A server namespace (`ids = server`: varp, param, enum, ...) is allocated from one
past the cache's high-water mark (tools/ss_allocate.py). A newer cache grows into
that space: rev 241 states varps to 5995, params to 2773 and enums to 6079, and
the rev-239 tree had allocated varps from 5725, params from 2634 and enums from
5995 -- 271, 142 and 85 ids where a cache record and a server record shared one
number. `cachepack unpack` onto such a tree leaves those cache records unnamed,
and the server would read and write one per-player slot for two variables.

ss_allocate.py never renumbers, by design (an assignment once made is kept), so
an upgrade does it here, once, as a constant shift of the whole block:

    new_id = old_id - old_floor + new_floor

A shift, not a re-pack of only the colliding ids, keeps the block's layout and
makes the old->new map one subtraction for anything that stored a number
(player saves store perm varps by id).

Varp names carry their id (`varp5725_mock_zone_clock`, OSRS-Content README "Var
names carry their kind and id"), so a moved varp is also renamed, as a whole
token, in every text file under the given roots. Param and enum names carry no
id and only their ledger lines change.

    tools/alloc_rebase.py --tree OSRS-Content/osrs-content \\
        --shift varp=6400 --shift param=3000 --shift enum=7000 \\
        --rename-root OSRS-Content/osrs-content --rename-root src \\
        --rename-root script --rename-root tools --rename-root test \\
        --map OSRS-Content/osrs-content/port/alloc_rebase_241.map [--write]

Without --write it prints what it would change and writes nothing.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

MARKER = "// --- allocated below this line by tools/ss_allocate.py; do not hand-edit ---"
LOSSLESS = "// @rscache-lossless-v1 "
LINE_RE = re.compile(r"^(\d+)=(\S+)(.*)$")
VARP_TOKEN = re.compile(r"\bvarp(\d+)_[A-Za-z0-9_]+\b")

# Binary asset folders of a content tree; nothing in them is a var reference.
SKIP_DIRS = {"models", "animsets", "synth", "samples", "sprites", "maps", "framemaps",
             "animayas", "songs", "jingles", "patches", "worldmap", "textures", "fonts",
             "binary", "defaults", ".git", "__pycache__"}


def fnv1a64(data: bytes) -> int:
    h = 14695981039346656037
    for b in data:
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def restamp(text: str) -> str:
    """Re-stamp an `@rscache-lossless-v1` fingerprint (tools/var_prefix_names.py)."""
    i = text.find(LOSSLESS)
    if i < 0:
        return text
    j = i + len(LOSSLESS)
    k = text.find(" ", j)
    return text[:j] + f"{fnv1a64(text[:i].encode()):016x}" + text[k:]


def read_alloc(path: Path) -> tuple[list[str], int, dict[int, str]]:
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    marker = next((i for i, ln in enumerate(lines) if ln.rstrip("\n") == MARKER), None)
    assert marker is not None, f"{path}: no allocation marker"
    ids: dict[int, str] = {}
    for ln in lines[marker + 1:]:
        m = LINE_RE.match(ln.rstrip("\n"))
        if m:
            ids[int(m.group(1))] = m.group(2)
    return lines, marker, ids


def shifted_name(ns: str, name: str, old: int, new: int) -> str:
    if ns == "varp" and name.startswith(f"varp{old}_"):
        return f"varp{new}_" + name[len(f"varp{old}_"):]
    return name


def candidate_files(root: Path) -> list[Path]:
    """Text files under root that mention `varp` at all.

    A fixed string and -I (skip binaries) keep this to seconds over a 20 GB tree;
    the token map does the exact matching afterwards. Build output directories
    (`src/build*`, `*/build_*`) hold objects and logs, never sources."""
    cmd = ["grep", "-rlIF", "varp"]
    for d in sorted(SKIP_DIRS):
        cmd.append(f"--exclude-dir={d}")
    cmd += ["--exclude-dir=build*", "--exclude-dir=*.dSYM", str(root)]
    out = subprocess.run(cmd, capture_output=True, text=True)
    return [Path(f) for f in out.stdout.splitlines()]


def skip_file(path: Path) -> bool:
    """The cache layer keeps the cache's names, and these two tools only describe.

    `configs/all.<type>` and its `.compack` are the cache's member index: a
    record there is the cache's even where an old allocation had borrowed its
    number, so a server rename must not reach it. The ledger itself is
    rewritten from its own lines above."""
    parts = path.parts
    if "configs" in parts and path.name.startswith("all."):
        return True
    return path.name in {"alloc_rebase.py", "ss_allocate.py"}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", required=True, type=Path)
    ap.add_argument("--shift", action="append", default=[], help="ns=new_floor")
    ap.add_argument("--rename-root", action="append", default=[], type=Path)
    ap.add_argument("--map", required=True, type=Path)
    ap.add_argument("--cache-max", action="append", default=[],
                    help="ns=id: the new cache's highest id; a lane's own allocation at or below it moves")
    ap.add_argument("--lane-floor", action="append", default=[],
                    help="ns=id: where a lane's own re-minted ids start")
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    args.cache_max = {k: int(v) for k, v in (s.split("=", 1) for s in args.cache_max)}
    args.lane_floor = {k: int(v) for k, v in (s.split("=", 1) for s in args.lane_floor)}

    rows: list[tuple[str, int, str, int, str]] = []
    renames: dict[str, str] = {}
    ledgers: dict[Path, str] = {}

    for spec in args.shift:
        ns, floor = spec.split("=", 1)
        new_floor = int(floor)
        path = args.tree / "pack" / f"{ns}.alloc"
        lines, marker, ids = read_alloc(path)
        assert ids, f"{path}: nothing allocated"
        old_floor = min(ids)
        delta = new_floor - old_floor
        assert delta > 0, f"{ns}: new floor {new_floor} is not above the old {old_floor}"
        out = lines[:marker + 1]
        for ln in lines[marker + 1:]:
            m = LINE_RE.match(ln.rstrip("\n"))
            if not m:
                out.append(ln)
                continue
            old = int(m.group(1))
            name = m.group(2)
            new = old + delta
            new_name = shifted_name(ns, name, old, new)
            rows.append((ns, old, name, new, new_name))
            if new_name != name:
                renames[name] = new_name
            out.append(f"{new}={new_name}{m.group(3)}\n")
        ledgers[path] = "".join(out)
        print(f"{ns}: {len(ids)} ids {old_floor}..{max(ids)} -> {old_floor + delta}..{max(ids) + delta}")

        # A ported lane keeps its own ledger (`ported/<lane>/pack/<ns>.alloc`).
        # A line there that restates a main allocation follows it; a lane's own
        # id that the new cache now reaches is re-minted just above the main
        # block. Two lines for one name at two ids is what cachepack refuses.
        main_new = {old: (name, old + delta, shifted_name(ns, name, old, old + delta)) for old, name in ids.items()}
        cache_max = args.cache_max.get(ns, -1)
        # Lanes get their own floor, so the main block can keep growing.
        nxt = args.lane_floor.get(ns, max(ids) + delta + 1)
        assert nxt > max(ids) + delta, f"{ns}: lane floor {nxt} is inside the main block"
        for lane in sorted((args.tree / "ported").glob(f"*/pack/{ns}.alloc")):
            text = lane.read_text(encoding="utf-8")
            out_lines = []
            for ln in text.splitlines(keepends=True):
                m = LINE_RE.match(ln.rstrip("\n"))
                if not m:
                    out_lines.append(ln)
                    continue
                old, name = int(m.group(1)), m.group(2)
                if old in main_new and main_new[old][0] == name:
                    new, new_name = main_new[old][1], main_new[old][2]
                elif old <= cache_max:
                    new = nxt
                    nxt += 1
                    new_name = shifted_name(ns, name, old, new)
                    rows.append((ns, old, name, new, new_name))
                    if new_name != name:
                        renames[name] = new_name
                    print(f"  {lane.parent.parent.name}: lane {ns} {old} {name} -> {new}")
                else:
                    out_lines.append(ln)
                    continue
                out_lines.append(f"{new}={new_name}{m.group(3)}\n")
            ledgers[lane] = "".join(out_lines)

    # Every text file that names a moved varp, renamed as whole tokens.
    changed: dict[Path, str] = {}
    if renames:
        for root in args.rename_root:
            for f in candidate_files(root):
                if f in ledgers or skip_file(f):
                    continue
                try:
                    text = f.read_text(encoding="utf-8")
                except (UnicodeDecodeError, OSError):
                    continue
                new_text = VARP_TOKEN.sub(lambda m: renames.get(m.group(0), m.group(0)), text)
                if new_text != text:
                    changed[f] = restamp(new_text)
        print(f"varp names: {len(renames)} renamed, {len(changed)} files touched")

    if not args.write:
        for f in sorted(changed)[:20]:
            print("  would edit", f)
        return 0

    for path, text in ledgers.items():
        path.write_text(text, encoding="utf-8")
    for path, text in changed.items():
        path.write_text(text, encoding="utf-8")
    args.map.parent.mkdir(parents=True, exist_ok=True)
    with args.map.open("w", encoding="utf-8") as fh:
        fh.write("# tools/alloc_rebase.py: server ids moved above the newer cache's.\n")
        fh.write("kind\told_id\told_name\tnew_id\tnew_name\n")
        for row in rows:
            fh.write("\t".join(str(x) for x in row) + "\n")
    print(f"wrote {len(ledgers)} ledgers, {len(changed)} files, map {args.map}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
