#!/usr/bin/env python3
"""
Zip Lua scripts, one baked cullmap, cache254, the osx executable, and optional Win32 DLLs.
Loads repo-root .env for defaults; CLI overrides .env. Paths in .env may be relative to repo root.
"""
from __future__ import annotations

import argparse
import shlex
import subprocess
import sys
import zipfile
from pathlib import Path

CULLMAP_BIN = "painters_cullmap_baked_r25_f50_w600_h400.bin"

DEFAULTS = {
    "SCRIPTS_DIR": "src/osrs/scripts",
    "CULLMAPS_DIR": "src/osrs/revconfig/configs/cullmaps",
    "CACHE_DIR": "../cache254",
    "BUILD_DIR": "build-mingw",
    "PACKAGE_OUTPUT": "package_build.zip",
    "SDL2_DLL_PATH": "",
    "LIBWINPTHREAD_DLL_PATH": "",
    "COMPILE_FLAGS": "",
}


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def parse_dotenv(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    if not path.is_file():
        return out
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        k, _, v = line.partition("=")
        key = k.strip()
        val = v.strip().strip('"').strip("'")
        if key:
            out[key] = val
    return out


def resolve_path(repo: Path, p: str) -> Path:
    path = Path(p)
    if path.is_absolute():
        return path.resolve()
    return (repo / path).resolve()


def find_executable(build_dir: Path) -> Path:
    for name in ("osx.exe", "osx"):
        cand = build_dir / name
        if cand.is_file():
            return cand
    raise FileNotFoundError(
        f"No osx.exe or osx found in {build_dir}"
    )


def add_tree_lua(z: zipfile.ZipFile, src_dir: Path, arc_prefix: str) -> None:
    for path in sorted(src_dir.rglob("*.lua")):
        rel = path.relative_to(src_dir)
        arc = f"{arc_prefix}/{rel.as_posix()}"
        z.write(path, arcname=arc)


def add_tree_all(z: zipfile.ZipFile, src_dir: Path, arc_prefix: str) -> None:
    for path in sorted(src_dir.rglob("*")):
        if path.is_file():
            rel = path.relative_to(src_dir)
            arc = f"{arc_prefix}/{rel.as_posix()}"
            z.write(path, arcname=arc)


def main() -> int:
    repo = repo_root()
    env_path = repo / ".env"
    env = {**DEFAULTS, **parse_dotenv(env_path)}

    parser = argparse.ArgumentParser(description="Build a distributable zip for osx + assets.")
    parser.add_argument(
        "--scripts-dir",
        default=env.get("SCRIPTS_DIR", DEFAULTS["SCRIPTS_DIR"]),
        help="Lua scripts directory (default: env SCRIPTS_DIR or repo default)",
    )
    parser.add_argument(
        "--cullmaps-dir",
        default=env.get("CULLMAPS_DIR", DEFAULTS["CULLMAPS_DIR"]),
        help="Directory containing cullmap .bin files",
    )
    parser.add_argument(
        "--cache-dir",
        default=env.get("CACHE_DIR", DEFAULTS["CACHE_DIR"]),
        help="Cache directory to pack as cache254/",
    )
    parser.add_argument(
        "--build-dir",
        default=env.get("BUILD_DIR", DEFAULTS["BUILD_DIR"]),
        help="CMake build dir with osx executable",
    )
    parser.add_argument(
        "-o",
        "--output",
        default=env.get("PACKAGE_OUTPUT", DEFAULTS["PACKAGE_OUTPUT"]),
        help="Output .zip path",
    )
    parser.add_argument(
        "--sdl2-dll",
        default=env.get("SDL2_DLL_PATH", "").strip(),
        help="Optional path to SDL2.dll for zip root",
    )
    parser.add_argument(
        "--libwinpthread-dll",
        default=env.get("LIBWINPTHREAD_DLL_PATH", "").strip(),
        help="Optional path to libwinpthread-1.dll for zip root",
    )
    parser.add_argument(
        "--compile-flags",
        default=env.get("COMPILE_FLAGS", DEFAULTS["COMPILE_FLAGS"]).strip(),
        help="Additional arguments passed to make as overrides (e.g. CFLAGS=\"-O2\" CXXFLAGS=\"-O2\"); empty skips build",
    )
    args = parser.parse_args()

    scripts_dir = resolve_path(repo, args.scripts_dir)
    cullmaps_dir = resolve_path(repo, args.cullmaps_dir)
    cache_dir = resolve_path(repo, args.cache_dir)
    build_dir = resolve_path(repo, args.build_dir)
    out_zip = resolve_path(repo, args.output)

    compile_flags = (args.compile_flags or "").strip()
    if compile_flags:
        if not build_dir.is_dir():
            print(f"package_build: BUILD_DIR is not a directory: {build_dir}", file=sys.stderr)
            return 1
        cmd = ["make", "-C", str(build_dir)] + shlex.split(compile_flags)
        print(f"package_build: running {' '.join(cmd)}")
        result = subprocess.run(cmd)
        if result.returncode != 0:
            print(f"package_build: make failed (exit {result.returncode})", file=sys.stderr)
            return 1

    cullmap_path = cullmaps_dir / CULLMAP_BIN

    missing: list[str] = []
    if not scripts_dir.is_dir():
        missing.append(str(scripts_dir))
    if not cullmap_path.is_file():
        missing.append(str(cullmap_path))
    if not cache_dir.is_dir():
        missing.append(str(cache_dir))

    try:
        exe_path = find_executable(build_dir)
    except FileNotFoundError:
        missing.append(f"{build_dir} (osx.exe or osx)")

    if missing:
        print("package_build: missing required paths:", file=sys.stderr)
        for m in missing:
            print(f"  {m}", file=sys.stderr)
        return 1

    sdl2 = Path(args.sdl2_dll).expanduser() if args.sdl2_dll else None
    if sdl2 and str(sdl2):
        sdl2 = sdl2.resolve() if sdl2.is_absolute() else resolve_path(repo, str(sdl2))
        if not sdl2.is_file():
            print(f"package_build: SDL2 dll not found: {sdl2}", file=sys.stderr)
            return 1

    pthr = Path(args.libwinpthread_dll).expanduser() if args.libwinpthread_dll else None
    if pthr and str(pthr):
        pthr = pthr.resolve() if pthr.is_absolute() else resolve_path(repo, str(pthr))
        if not pthr.is_file():
            print(f"package_build: libwinpthread dll not found: {pthr}", file=sys.stderr)
            return 1

    out_zip.parent.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(out_zip, "w", compression=zipfile.ZIP_DEFLATED) as z:
        add_tree_lua(z, scripts_dir, "scripts")
        z.write(cullmap_path, arcname=f"configs/cullmaps/{CULLMAP_BIN}")
        add_tree_all(z, cache_dir, "cache254")
        z.write(exe_path, arcname=exe_path.name)
        if sdl2 and sdl2.is_file():
            z.write(sdl2, arcname=sdl2.name)
        if pthr and pthr.is_file():
            z.write(pthr, arcname=pthr.name)

    print(f"package_build: wrote {out_zip}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
