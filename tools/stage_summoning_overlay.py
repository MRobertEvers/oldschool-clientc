#!/usr/bin/env python3
"""Stage the client half of the feature-flagged Summoning overlay.

The ordinary content tree remains the flag-off input.  Client-visible Summoning
files live below ``ported/scape2009_summoning`` (or in the matching asset
subtrees) where the ordinary cachepack config walk cannot see them.  This tool
builds the small, explicit second-pass tree used only by the flag-on bake.

It also provides ``--compare-cache`` as the byte-identity harness.  The command
compares every regular file in two cache directories and refuses a vacuous
zero-file pass.
"""

from __future__ import annotations

import argparse
import filecmp
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TREE = REPO_ROOT / "OSRS-Content" / "osrs239-content"
LANE = Path("ported") / "scape2009_summoning"
ASSET_ROOTS = (
    "models",
    "animsets",
    "framemaps",
    "sprites",
    "synth",
    "interfaces",
    "scripts",
)
CONFIG_SUFFIXES = {
    ".enum",
    ".npc",
    ".obj",
    ".loc",
    ".seq",
    ".spotanim",
    ".varbit",
    ".varp",
    ".constant",
}


def fail(message: str) -> ValueError:
    return ValueError(f"stage_summoning_overlay: {message}")


def ensure_plain_tree(root: Path, label: str) -> list[Path]:
    if not root.is_dir():
        raise fail(f"{label} directory does not exist: {root}")
    files: list[Path] = []
    for path in root.rglob("*"):
        if path.is_symlink():
            raise fail(f"{label} contains a symlink: {path}")
        if path.is_file():
            files.append(path)
    return files


def copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def copy_tree(source: Path, destination: Path) -> int:
    if not source.exists():
        return 0
    files = ensure_plain_tree(source, "Summoning source")
    for source_file in files:
        copy_file(source_file, destination / source_file.relative_to(source))
    return len(files)


def reset_output(tree: Path, out: Path) -> None:
    tree = tree.resolve()
    out = out.resolve()
    if out == tree or tree in out.parents:
        raise fail("output must not be the content tree or a child of it")
    if out == REPO_ROOT or out == REPO_ROOT.parent or out == Path(out.anchor):
        raise fail(f"refusing broad output path: {out}")
    if out.exists():
        if out.is_symlink() or not out.is_dir():
            raise fail(f"existing output is not a plain directory: {out}")
        shutil.rmtree(out)
    out.mkdir(parents=True)


def stage(tree: Path, out: Path) -> int:
    tree = tree.resolve()
    out = out.resolve()
    lane = tree / LANE
    if not (lane / "PROVENANCE.md").is_file():
        raise fail(f"missing lane marker {lane / 'PROVENANCE.md'}")
    ensure_plain_tree(lane, "Summoning lane")
    reset_output(tree, out)

    copied = 0
    for required in (tree / "meta.ini", tree / "content.ini"):
        if not required.is_file() or required.is_symlink():
            raise fail(f"missing plain support file: {required}")
        copy_file(required, out / required.name)
        copied += 1
    copied += copy_tree(tree / "fields", out / "fields")

    # Full member indexes are lookup context only.  They allow imported blocks
    # to name existing records without placing those base records in this tree.
    compacks = sorted((tree / "configs").glob("*.compack"))
    if not compacks:
        raise fail(f"no config member indexes under {tree / 'configs'}")
    for compack in compacks:
        if compack.is_symlink():
            raise fail(f"config member index is a symlink: {compack}")
        copy_file(compack, out / "configs" / compack.name)
        copied += 1

    # Client dbrows and DB_FIND indexes are one derived unit. The feature tree
    # adds rows to cache tables 212/213, so the stage needs the base row/schema
    # text and index templates as lookup context before gen_dbindex appends the
    # authored rows. These copies exist only in the disposable flag-on stage.
    for name in ("all.dbrow", "all.dbtable"):
        source = tree / "configs" / name
        if not source.is_file() or source.is_symlink():
            raise fail(f"missing plain database source: {source}")
        copy_file(source, out / "configs" / name)
        copied += 1
    copied += copy_tree(tree / "dbindex", out / "dbindex")

    dbrow_alloc = tree / "pack" / "dbrow.alloc"
    if not dbrow_alloc.is_file() or dbrow_alloc.is_symlink():
        raise fail(f"missing plain dbrow allocation ledger: {dbrow_alloc}")
    copy_file(dbrow_alloc, out / "pack" / "dbrow.alloc")
    copied += 1
    dbindex_pack = tree / "pack" / "21_dbtableindex.pack"
    if not dbindex_pack.is_file() or dbindex_pack.is_symlink():
        raise fail(f"missing plain dbindex archive pack: {dbindex_pack}")
    copy_file(dbindex_pack, out / "pack" / "21_dbtableindex.pack")
    copied += 1

    # Lane directories mirror their destination in the staged root.  Config
    # records may also be placed directly under the lane; keep those in a
    # provenance-named subdirectory of the config walk.
    for child in sorted(lane.iterdir()):
        if child.name == "PROVENANCE.md":
            continue
        if child.is_dir() and child.name in {"configs", "interfaces", "scripts", "pack", *ASSET_ROOTS}:
            copied += copy_tree(child, out / child.name)
        elif child.is_file() and child.suffix in CONFIG_SUFFIXES:
            copy_file(child, out / "configs" / LANE / child.name)
            copied += 1
        else:
            raise fail(f"unclassified lane entry: {child}")

    # Large binary assets stay in their native roots in the source tree, but
    # only the provenance-marked subtree is copied into the second-pass view.
    for root_name in ASSET_ROOTS:
        source = tree / root_name / LANE
        copied += copy_tree(source, out / root_name / LANE)

    generated = subprocess.run(
        [sys.executable, str(REPO_ROOT / "tools/gen_dbindex.py"),
         "--content", str(out), "--write"],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if generated.returncode != 0:
        raise fail(f"dbindex regeneration failed:\n{generated.stdout}")
    print(generated.stdout, end="")

    if copied == 0:
        raise fail("staged zero files")
    print(f"stage_summoning_overlay: staged {copied} files in {out}")
    return 0


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def cache_files(root: Path) -> dict[Path, Path]:
    files = ensure_plain_tree(root.resolve(), "cache")
    return {path.relative_to(root.resolve()): path for path in files}


def compare_cache(before: Path, after: Path) -> int:
    left = cache_files(before)
    right = cache_files(after)
    errors: list[str] = []
    left_names = set(left)
    right_names = set(right)
    for missing in sorted(left_names - right_names):
        errors.append(f"missing after: {missing}")
    for added in sorted(right_names - left_names):
        errors.append(f"added after: {added}")
    checked = 0
    for relative in sorted(left_names & right_names):
        checked += 1
        if not filecmp.cmp(left[relative], right[relative], shallow=False):
            errors.append(
                f"bytes differ: {relative} "
                f"({digest(left[relative])} != {digest(right[relative])})"
            )
    if checked == 0:
        errors.append("cache comparison executed zero file checks")
    for error in errors:
        print(f"stage_summoning_overlay: error: {error}", file=sys.stderr)
    print(f"stage_summoning_overlay: compared {checked} cache files, {len(errors)} errors")
    return 1 if errors else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    parser.add_argument("--out", type=Path, help="directory to replace with the staged overlay")
    parser.add_argument(
        "--compare-cache",
        nargs=2,
        metavar=("BEFORE", "AFTER"),
        type=Path,
        help="assert two cache directories are byte-identical",
    )
    args = parser.parse_args()
    try:
        if args.compare_cache:
            if args.out is not None:
                parser.error("--out and --compare-cache are mutually exclusive")
            return compare_cache(*args.compare_cache)
        if args.out is None:
            parser.error("--out is required when staging")
        return stage(args.tree, args.out)
    except ValueError as exc:
        print(exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
