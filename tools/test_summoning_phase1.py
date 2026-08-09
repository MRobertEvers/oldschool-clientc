#!/usr/bin/env python3
"""Headless acceptance for the feature-flagged Summoning stat slice."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=REPO_ROOT / "src" / "torirs")
    parser.add_argument("--flag-on", type=Path, default=REPO_ROOT / "cache.osrs239.summoning")
    parser.add_argument("--flag-off", type=Path, default=REPO_ROOT / "cache.osrs239")
    parser.add_argument("--manifest", type=Path, default=REPO_ROOT / "manifest_osrs239.ini")
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "build" / "summoning-phase1")
    args = parser.parse_args()

    errors: list[str] = []
    checked = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal checked
        checked += 1
        if not condition:
            errors.append(message)

    for label, path in (
        ("client", args.client),
        ("flag-on cache", args.flag_on / "main_file_cache.dat2"),
        ("flag-off cache", args.flag_off / "main_file_cache.dat2"),
        ("manifest", args.manifest),
    ):
        expect(path.is_file(), f"missing {label}: {path}")
    if errors:
        for error in errors:
            print(f"test_summoning_phase1: error: {error}", file=sys.stderr)
        print(f"test_summoning_phase1: {checked} checks, {len(errors)} errors")
        return 1

    args.out.mkdir(parents=True, exist_ok=True)

    def run(name: str, cache: Path, saves: Path, cheat: str | None = None) -> str:
        bmp = args.out / f"{name}.bmp"
        log_path = args.out / f"{name}.log"
        env = os.environ.copy()
        env.update(
            {
                "MOCK230_SAVES": str(saves),
                "SDL_VIDEODRIVER": "dummy",
                "TORIRS_MAX_FRAMES": "430",
                "TORIRS_EXIT_BMP": str(bmp),
                "TORIRS_DUMP_TREE_EXIT": "1",
                "TORIRS_SIM_CLICK_AT": "200,535,186",
            }
        )
        if cheat is not None:
            env["TORIRS_NET_CHEAT"] = cheat
        else:
            env.pop("TORIRS_NET_CHEAT", None)
        command = [
            str(args.client),
            str(cache),
            "--manifest",
            str(args.manifest),
            "--soft3d",
        ]
        result = subprocess.run(
            command,
            cwd=REPO_ROOT,
            env=env,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=180,
            check=False,
        )
        log_path.write_text(result.stdout, encoding="utf-8")
        expect(result.returncode == 0, f"{name}: client exited {result.returncode}")
        expect(bmp.is_file() and bmp.stat().st_size > 54, f"{name}: no non-empty BMP")
        expect("SKIP" not in result.stdout, f"{name}: headless run reported SKIP")
        return result.stdout

    with tempfile.TemporaryDirectory(prefix="summoning_phase1_") as temporary:
        root = Path(temporary)
        level1 = run("flag_on_level1", args.flag_on, Path(tempfile.mkdtemp(dir=root)))
        persistent_saves = Path(tempfile.mkdtemp(dir=root))
        level20 = run(
            "flag_on_level20",
            args.flag_on,
            persistent_saves,
            "setlevel summoning 20",
        )
        persisted = run("flag_on_persisted", args.flag_on, persistent_saves)
        flagoff = run("flag_off", args.flag_off, Path(tempfile.mkdtemp(dir=root)))

        expect("320<<16|34" in level1, "flag_on_level1: Summoning cell is absent")
        expect('text="Total level: 34"' in level1, "flag_on_level1: total is not 34")
        expect(
            "cheat 'setlevel summoning 20' -> debugproc not found" in level20
            and "Set stat 24 to 20." in level20,
            "flag_on_level20: named setlevel did not resolve to stat 24",
        )
        expect("320<<16|34" in level20, "flag_on_level20: Summoning cell is absent")
        expect('text="Total level: 53"' in level20, "flag_on_level20: total is not 53")
        expect('text="20"' in level20, "flag_on_level20: level 20 text is absent")
        save_text = (persistent_saves / "guest.ini").read_text(encoding="utf-8")
        expect("24 = 20 44700" in save_text, "save: stat 24 level/XP did not persist")
        expect("320<<16|34" in persisted, "flag_on_persisted: Summoning cell is absent")
        expect('text="Total level: 53"' in persisted, "flag_on_persisted: total is not 53")
        expect('text="20"' in persisted, "flag_on_persisted: level 20 text is absent")
        expect("320<<16|34" not in flagoff, "flag_off: Summoning cell leaked")
        expect('text="Total level: 33"' in flagoff, "flag_off: total is not 33")

    for error in errors:
        print(f"test_summoning_phase1: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase1: {checked} checks, {len(errors)} errors")
    print(f"test_summoning_phase1: four BMPs and logs in {args.out}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
