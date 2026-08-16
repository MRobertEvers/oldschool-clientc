#!/usr/bin/env python3
"""Strip redundant rscache_ filename prefix from src/osrs/rscache files."""

import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RSCACHE = REPO / "src/osrs/rscache"
PREFIX = "rscache_"


def collect_rename_map() -> dict[Path, Path]:
    mapping: dict[Path, Path] = {}
    for fpath in sorted(RSCACHE.rglob("*")):
        if not fpath.is_file():
            continue
        if fpath.suffix not in (".c", ".h"):
            continue
        if not fpath.name.startswith(PREFIX):
            continue
        dst = fpath.parent / fpath.name[len(PREFIX) :]
        if dst == fpath:
            continue
        if dst in mapping.values():
            raise FileExistsError(f"collision: multiple sources map to {dst}")
        mapping[fpath] = dst
    return mapping


def git_mv(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists():
        raise FileExistsError(f"destination already exists: {dst}")
    try:
        subprocess.run(["git", "mv", str(src), str(dst)], check=True, cwd=REPO, capture_output=True)
    except subprocess.CalledProcessError:
        shutil.move(str(src), str(dst))
    print(f"RENAMED: {src.relative_to(REPO)} -> {dst.relative_to(REPO)}")


def rename_files(mapping: dict[Path, Path]) -> None:
    for src, dst in mapping.items():
        git_mv(src, dst)


def apply_basename_renames(text: str, basename_map: dict[str, str]) -> str:
    for old, new in sorted(basename_map.items(), key=lambda x: -len(x[0])):
        text = text.replace(old, new)
    return text


def rewrite_includes(basename_map: dict[str, str]) -> None:
    skip_dirs = {".git", "build", "build_release", "build.em", "build_sse1", "node_modules", ".cursor"}
    extensions = {".c", ".h", ".cpp", ".hpp", ".u.c", ".md", "CMakeLists.txt", "Makefile"}

    for root, dirs, files in os.walk(REPO):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for fname in files:
            fpath = Path(root) / fname
            if fpath.suffix not in extensions and fname not in ("CMakeLists.txt", "Makefile"):
                continue
            try:
                content = fpath.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            new_content = apply_basename_renames(content, basename_map)
            if new_content != content:
                fpath.write_text(new_content, encoding="utf-8")
                print(f"UPDATED: {fpath.relative_to(REPO)}")


def main() -> None:
    step = sys.argv[1] if len(sys.argv) > 1 else "all"
    mapping = collect_rename_map()
    basename_map = {src.name: dst.name for src, dst in mapping.items()}

    if not basename_map:
        print("No files to rename.")
        return

    print(f"Planned renames ({len(basename_map)}):")
    for old, new in sorted(basename_map.items()):
        print(f"  {old} -> {new}")

    if step in ("rename", "all"):
        rename_files(mapping)
    if step in ("includes", "all"):
        rewrite_includes(basename_map)


if __name__ == "__main__":
    main()
