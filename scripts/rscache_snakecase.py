#!/usr/bin/env python3
"""Rename RSCache regime files from PascalCase to snake_case (keep symbol names)."""

import os
import re
import shutil
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RSCACHE = REPO / "src/osrs/rscache"
REGIME_DIRS = ["shared", "disk", "dat2disk", "dat1disk", "dat2a", "dat1a"]

# Longest / most specific prefixes first.
STEM_PREFIXES = [
    ("RSCacheShared_", "rscache_shared_"),
    ("RSCacheDat2Disk_", "rscache_dat2disk_"),
    ("RSCacheDat1A_", "rscache_dat1a_"),
    ("RSCacheDat2A_", "rscache_dat2a_"),
    ("RSCacheDat1Disk", "rscache_dat1disk"),
    ("RSCacheDat2Disk", "rscache_dat2disk"),
    ("RSCacheDisk", "rscache_disk"),
]


def camel_to_snake(name: str) -> str:
    """Convert CamelCase / mixed case to lower_snake_case."""
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", s)
    return s.lower()


def convert_stem(stem: str) -> str | None:
    for old_prefix, new_prefix in STEM_PREFIXES:
        if stem == old_prefix.rstrip("_") and old_prefix.endswith("_"):
            continue
        if stem.startswith(old_prefix):
            suffix = stem[len(old_prefix) :]
            if suffix:
                return new_prefix + camel_to_snake(suffix)
            return new_prefix
        if stem == old_prefix:
            return new_prefix
    return None


def convert_basename(name: str) -> str | None:
    stem, ext = os.path.splitext(name)
    if ext not in (".c", ".h"):
        return None
    new_stem = convert_stem(stem)
    if new_stem is None or new_stem == stem:
        return None
    return new_stem + ext


def collect_rename_map() -> dict[Path, Path]:
    mapping: dict[Path, Path] = {}
    for regime in REGIME_DIRS:
        regime_path = RSCACHE / regime
        if not regime_path.is_dir():
            continue
        for fpath in sorted(regime_path.iterdir()):
            if not fpath.is_file():
                continue
            new_name = convert_basename(fpath.name)
            if new_name is None:
                continue
            dst = fpath.parent / new_name
            if dst != fpath:
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
    import sys

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
