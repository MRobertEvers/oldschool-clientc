#!/usr/bin/env python3
"""Real-client acceptance for the Summoning skill-guide slice."""

from __future__ import annotations

import argparse
import os
import re
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
                # Finish by selecting the Summoning Scrolls subsection. This
                # makes the final tree and object-icon trace prove the packed
                # scroll records, rather than only the Overview fallback icon.
                "TORIRS_SIM_CLICK_AT": "150,536,186;230,525,460,1;260,525,475;360,80,92",
                "TORIRS_OBJICON_DEBUG": "1",
                "TORIRS_CLICK_DEBUG": "1",
                "TORIRS_MINIMENU_DEBUG": "1",
                "TORIRS_NET_CHEAT": "summoning_unlock",
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
    expect(
        "this build has no embedded server" not in result.stdout,
        "client was built without the embedded mock server",
    )
    expect("released 536,186" in result.stdout, "Stats tab click did not execute")
    expect("move 525,460 right=1" in result.stdout, "Summoning menu did not open")
    expect("released 525,475" in result.stdout, "Summoning guide menu row was not selected")
    expect(
        "clickdbg: send op2 target=0x1400022 sub=-1 state=2" in result.stdout,
        "Summoning cell did not send IF_BUTTON2",
    )
    expect("860<<16|0" in result.stdout, "server did not mount the guide")
    # The final tree is dumped after the Summoning Scrolls subsection click.
    expect("OBJICON: enter com=0x035c" in result.stdout, "Summoning guide CS2 built no rows")
    expect("860<<16" in result.stdout, "skill_guide_v2 is absent from the final UI tree")
    expect('text="Familiars"' in result.stdout, "Familiars subsection did not render")
    expect(
        'text="Summoning - Summoning Scrolls (Members Only) "' in result.stdout,
        "Summoning Scrolls did not become the active guide subsection",
    )
    expect(
        'text="Howl"' in result.stdout and 'text="Steel of Legends"' in result.stdout,
        "live db_find did not render the first and last Summoning scroll rows",
    )
    rendered_scrolls = {
        int(obj_id)
        for obj_id in re.findall(r"OBJICON: enter[^\n]* obj=(474\d+) ", result.stdout)
    }
    expect(
        rendered_scrolls == set(range(47400, 47468)),
        f"Summoning scroll renderer saw {len(rendered_scrolls)}/68 packed object icons",
    )
    expect("CS2VM2: abort" not in result.stdout, "guide clientscript aborted")

    for error in errors:
        print(f"test_summoning_phase4: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase4: {checked} checks, {len(errors)} errors")
    print(f"test_summoning_phase4: framebuffer and log in {args.out}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
