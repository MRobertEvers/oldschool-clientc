#!/usr/bin/env python3
"""Carry a content tree's names onto a newer cache: a three-way merge per id.

`cachepack unpack` onto an existing tree keeps the tree's name for every id it
already names. That protects an authored name, and it is wrong across a cache
upgrade wherever Jagex moved or reused ids: rev 241 inserted `chatbox:crmclose`
at 565, so 565..568 each shifted down a slot and the tree went on calling 567
`chatmodal` (241: `chatoverlay`; `chatmodal` is 568) -- every dialogue opened
into a component the client does not have. Varbit 4398 (`ge_newoffer_price` ->
`pvptut_loadout_a_inv_00`), dbrow 11239 and seq 14172 are the same failure on
reused ids.

So, per id, with the raw unpacks of both caches as base and theirs:

    ours == base           the tree never renamed it      -> theirs (241's name)
    theirs == base         Jagex never renamed it          -> ours   (authored kept)
    both changed           a real conflict                 -> theirs, and logged

"Equal" compares var names without their `varp<id>_`/`varb<id>_`/`varc<id>_`
prefix (OSRS-Content README "Var names carry their kind and id"); a var name
taken from 241 gets the prefix back. An id 241 no longer has keeps the tree's
line (unpack drops the record; the name is then an orphan the compiler reports).

Run it on a CLONE of the tree, before `cachepack unpack --cache <new>`:

    tools/cache_upgrade_names.py --tree X --base raw239 --theirs raw241 \\
        --log X/port/cache_upgrade_names_241.tsv [--write]

raw239 / raw241 are fresh `cachepack unpack --assets` trees of each cache.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

LINE_RE = re.compile(r"^(-?\d+)=([^\s]+)(.*)$")
VAR_PREFIX = re.compile(r"^(varb|varp|varc)\d+_")
VAR_KIND = {"all.varbit.compack": "varb", "all.varp.compack": "varp", "all.varc.compack": "varc"}


def read_names(path: Path) -> dict[int, str]:
    out: dict[int, str] = {}
    if not path.exists():
        return out
    for ln in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = LINE_RE.match(ln)
        if m:
            out[int(m.group(1))] = m.group(2)
    return out


def norm(name: str) -> str:
    return VAR_PREFIX.sub("", name)


def name_files(tree: Path) -> list[Path]:
    """Every id=name table the cache names: config member indexes, interface
    member indexes, and the asset archive packs (`pack/<idx>_<name>.pack`)."""
    rels = []
    for d, pattern in (("configs", re.compile(r"^all\..+\.compack$")),
                       ("interfaces", re.compile(r".+\.compack$")),
                       ("pack", re.compile(r"^\d+_.+\.pack$"))):
        root = tree / d
        if not root.is_dir():
            continue
        for f in sorted(os.listdir(root)):
            if pattern.match(f):
                rels.append(Path(d) / f)
    return rels


def merge_file(rel: Path, tree: Path, base: Path, theirs: Path, log: list) -> str | None:
    ours_path = tree / rel
    B = read_names(base / rel)
    T = read_names(theirs / rel)
    if not T:
        return None
    kind = VAR_KIND.get(rel.name)
    lines = ours_path.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)
    changed = False
    out = []
    for ln in lines:
        m = LINE_RE.match(ln.rstrip("\n"))
        if not m:
            out.append(ln)
            continue
        ident = int(m.group(1))
        ours = m.group(2)
        rest = m.group(3)
        if ident not in T:
            out.append(ln)
            continue
        theirs_name = T[ident]
        base_name = B.get(ident)
        if norm(theirs_name) == norm(ours):
            out.append(ln)
            continue
        if base_name is not None and norm(theirs_name) == norm(base_name):
            out.append(ln)  # Jagex kept the name; the tree's own spelling stands
            continue
        new = f"{kind}{ident}_{theirs_name}" if kind else theirs_name
        if base_name is None or norm(ours) == norm(base_name):
            log.append((str(rel), ident, ours, new, "upstream"))
        else:
            log.append((str(rel), ident, ours, new, "conflict"))
        # The pack line's trailing fields (a script's hashcode) are re-derived by
        # unpack; only the name is ours to set.
        out.append(f"{ident}={new}{rest}\n")
        changed = True
    return "".join(out) if changed else None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", required=True, type=Path)
    ap.add_argument("--base", required=True, type=Path)
    ap.add_argument("--theirs", required=True, type=Path)
    ap.add_argument("--log", required=True, type=Path)
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()

    log: list = []
    edits: dict[Path, str] = {}
    for rel in name_files(args.tree):
        text = merge_file(rel, args.tree, args.base, args.theirs, log)
        if text is not None:
            edits[rel] = text

    by_file: dict[str, list[int]] = {}
    for f, _, _, _, how in log:
        c = by_file.setdefault(f, [0, 0])
        c[0 if how == "upstream" else 1] += 1
    for f, (up, conf) in sorted(by_file.items(), key=lambda kv: -sum(kv[1]))[:40]:
        print(f"{f:48s} upstream={up:5d} conflict={conf:4d}")
    print(f"{len(log)} names in {len(edits)} files"
          + ("" if args.write else " (dry run; --write to apply)"))
    if not args.write:
        return 0
    for rel, text in edits.items():
        (args.tree / rel).write_text(text, encoding="utf-8")
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w", encoding="utf-8") as fh:
        fh.write("# tools/cache_upgrade_names.py: names changed by the cache upgrade.\n")
        fh.write("file\tid\ttree_name\tnew_name\twhy\n")
        for row in log:
            fh.write("\t".join(str(x) for x in row) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
