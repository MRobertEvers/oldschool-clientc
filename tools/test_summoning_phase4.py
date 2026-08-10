#!/usr/bin/env python3
"""Real-client acceptance for the Summoning skill-guide slice."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=REPO / "src/torirs")
    parser.add_argument("--cache", type=Path, default=REPO / "cache.osrs239.summoning")
    parser.add_argument(
        "--scripts",
        type=Path,
        default=REPO / "OSRS-Content/osrs239-content/server/scripts/build_summoning",
    )
    parser.add_argument("--manifest", type=Path, default=REPO / "manifest_osrs239.ini")
    parser.add_argument("--out", type=Path, default=REPO / "build/summoning-phase4")
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
        ("cache", args.cache / "main_file_cache.dat2"),
        ("script pack", args.scripts / "script.dat"),
        ("manifest", args.manifest),
    ):
        expect(path.is_file() and path.stat().st_size > 0, f"missing {label}: {path}")
    if errors:
        for error in errors:
            print(f"test_summoning_phase4: error: {error}", file=sys.stderr)
        print(f"test_summoning_phase4: {checked} checks, {len(errors)} errors")
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    bmp = args.out / "skill_guide.bmp"
    log_path = args.out / "skill_guide.log"
    with tempfile.TemporaryDirectory(prefix="summoning_phase4_saves_") as saves:
        env = os.environ.copy()
        env.update(
            {
                "MOCK230_SAVES": saves,
                "MOCK230_SCRIPTS": str(args.scripts.resolve()),
                "MOCK230_CACHE": str(args.cache.resolve()),
                "SDL_VIDEODRIVER": "dummy",
                "TORIRS_MAX_FRAMES": "520",
                "TORIRS_EXIT_BMP": str(bmp.resolve()),
                "TORIRS_DUMP_TREE_EXIT": "1",
                # Select Stats, open child 34's real context menu, then select
                # its View Summoning guide row. This exercises the same menu and
                # packet path a player uses rather than dispatching a hook.
                "TORIRS_SIM_CLICK_AT": "150,536,186;230,620,430,1;260,620,445",
                "TORIRS_OBJICON_DEBUG": "1",
                "TORIRS_CLICK_DEBUG": "1",
                "TORIRS_MINIMENU_DEBUG": "1",
            }
        )
        result = subprocess.run(
            [
                str(args.client),
                str(args.cache),
                "--manifest",
                str(args.manifest),
                "--soft3d",
            ],
            cwd=REPO,
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

    expect(result.returncode == 0, f"client exited {result.returncode}")
    expect(bmp.is_file() and bmp.stat().st_size > 54, "client wrote no framebuffer")
    expect("SKIP" not in result.stdout, "client run reported SKIP")
    expect("released 536,186" in result.stdout, "Stats tab click did not execute")
    expect("move 620,430 right=1" in result.stdout, "Summoning menu did not open")
    expect("released 620,445" in result.stdout, "Summoning guide menu row was not selected")
    expect(
        "clickdbg: send op2 target=0x1400022 sub=-1 state=2" in result.stdout,
        "Summoning cell did not send IF_BUTTON2",
    )
    expect("mock230: -> IF_OPENSUB" in result.stdout, "server did not mount the guide")
    expect("mock230: -> RUNCLIENTSCRIPT" in result.stdout, "server did not start guide CS2")
    expect("860<<16" in result.stdout, "skill_guide_v2 is absent from the final UI tree")
    expect('text="Familiars"' in result.stdout, "Familiars subsection did not render")
    expect(
        'text="Spirit wolf - Gold charm, wolf bones, 7 spirit shards"' in result.stdout,
        "live db_find did not render the Spirit wolf row",
    )
    expect(
        "OBJICON: enter" in result.stdout and "obj=40000" in result.stdout,
        "Spirit wolf pouch icon did not enter the object renderer",
    )
    expect("CS2VM2: abort" not in result.stdout, "guide clientscript aborted")

    for error in errors:
        print(f"test_summoning_phase4: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase4: {checked} checks, {len(errors)} errors")
    print(f"test_summoning_phase4: framebuffer and log in {args.out}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
