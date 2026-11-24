#!/usr/bin/env python3
"""Stage the torirslive lane's client configs and CS2 for its cache bake.

`make -C src torirsserver-cache-torirslive` bakes the live world's cache in
three passes over a copy of cache.osrs242:

  1. configs  -- every `*.<type>` under ported/torirslive/configs/ (today the
                 Limit Framerate rows, enum_3504), packed with --base so each
                 record the lane does not state keeps the base cache's bytes;
  2. scripts  -- each ported/torirslive/scripts/*.cs2, compiled against the
                 whole content tree's scripts (the procs they call are defined
                 there) and imported with --asset-only --archive-list, so only
                 the lane's own archives are replaced;
  3. assets   -- ported/torirslive/cache/ (the login screen), as before.

This tool writes the tree passes 1 and 2 read: the content tree's support
files (meta.ini, content.ini, fields/, the config member indexes, the
clientscript name table), its scripts/ with the lane's scripts copied over
them, the lane's configs, and `archives.txt` naming the lane's script
archives (`scripts=<id>`, from each file's `// <id>` first line).
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TREE = REPO_ROOT / "OSRS-Content" / "osrs-content"
LANE = Path("ported") / "torirslive"
CONFIG_SUFFIXES = {".enum", ".struct", ".param", ".varbit", ".varp", ".varc", ".inv"}
SCRIPT_ID = re.compile(r"^// (\d+)\s*$")


def fail(message: str) -> SystemExit:
    print(f"stage_torirslive_overlay: error: {message}", file=sys.stderr)
    return SystemExit(1)


def copy_file(source: Path, dest: Path) -> None:
    if not source.is_file():
        raise fail(f"missing {source}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, dest)


def stage(tree: Path, out: Path) -> int:
    tree = tree.resolve()
    out = out.resolve()
    lane = tree / LANE
    if not (lane / "lane.ini").is_file():
        raise fail(f"no lane at {lane}")
    if out == tree or tree in out.parents or out in tree.parents:
        raise fail(f"unsafe output {out}")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    for name in ("meta.ini", "content.ini"):
        copy_file(tree / name, out / name)
    shutil.copytree(tree / "fields", out / "fields")
    # Member indexes: lookup context so a lane block can name records it does
    # not restate. Not records themselves -- nothing here is packed from them.
    compacks = sorted((tree / "configs").glob("*.compack"))
    if not compacks:
        raise fail(f"no config member indexes under {tree / 'configs'}")
    for compack in compacks:
        copy_file(compack, out / "configs" / compack.name)
    copy_file(tree / "pack" / "12_clientscripts.pack", out / "pack" / "12_clientscripts.pack")

    configs = sorted(p for p in (lane / "configs").glob("*") if p.suffix in CONFIG_SUFFIXES)
    for config in configs:
        copy_file(config, out / "configs" / "torirslive" / config.name)

    # The whole script tree is compile context (pass 2 resolves the lane
    # scripts' gosubs against it); archives.txt limits what is imported.
    shutil.copytree(tree / "scripts", out / "scripts")
    picks = []
    for script in sorted((lane / "scripts").glob("*.cs2")):
        first = script.read_text(encoding="utf-8").splitlines()[:1]
        match = SCRIPT_ID.match(first[0]) if first else None
        if not match:
            raise fail(f"{script}: first line must be `// <script id>`")
        if not (tree / "scripts" / script.name).is_file():
            raise fail(f"{script.name}: no script of that name in {tree / 'scripts'} to replace")
        copy_file(script, out / "scripts" / script.name)
        picks.append(f"scripts={match.group(1)}")
    (out / "archives.txt").write_text("".join(p + "\n" for p in picks), encoding="utf-8")

    print(
        f"stage_torirslive_overlay: {len(configs)} config file(s), "
        f"{len(picks)} script archive(s) staged in {out}"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    return stage(args.tree, args.out)


if __name__ == "__main__":
    sys.exit(main())
