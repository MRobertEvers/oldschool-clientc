#!/usr/bin/env python3
"""Real-client acceptance for the active familiar special-points orb."""

from __future__ import annotations

import argparse
import hashlib
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
    parser.add_argument("--out", type=Path, default=REPO / "build/summoning-phase4c")
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
            print(f"test_summoning_phase4c: error: {error}", file=sys.stderr)
        print(f"test_summoning_phase4c: {checked} checks, {len(errors)} errors")
        return 1

    sprite_root = (
        REPO
        / "OSRS-Content/osrs239-content/sprites/ported/scape2009_summoning"
    )
    expected_sprites = {
        "summoning_orb_icon": "ab53c1293c23ee39e056f4b04a394ccb17cbf87346d4f2ca4fc36a76a1360220",
        "summoning_orb_backing": "764e90013ff6a9076e6643af308081178834c365211eb9325fe0f6bb661c0833",
        "summoning_orb_indicator": "9489b4c50b01cd619ab2cdff0f069e79a6286a88e105501d9e8669681ef3e932",
        "summoning_orb_empty": "233ea60b31223e8224fb66d16e723166f8788ae92169aa74ea0ae9945a715a24",
    }
    for name, expected in expected_sprites.items():
        path = sprite_root / name / "0.bmp"
        expect(
            path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == expected,
            f"{name}: pixels are not the exact rev-530 source sprite",
        )

    args.out.mkdir(parents=True, exist_ok=True)
    bmp = args.out / "summoning_orb.bmp"
    log_path = args.out / "summoning_orb.log"
    with tempfile.TemporaryDirectory(prefix="summoning_phase4c_saves_") as saves:
        env = os.environ.copy()
        env.update(
            {
                "TORIRSSERVER_SAVES": saves,
                "TORIRSSERVER_SCRIPTS": str(args.scripts.resolve()),
                "TORIRSSERVER_CACHE": str(args.cache.resolve()),
                "TORIRS_NET_CHEAT": "summoning_unlock;summoning_demo",
                "SDL_VIDEODRIVER": "dummy",
                "TORIRS_MAX_FRAMES": "380",
                "TORIRS_EXIT_BMP": str(bmp.resolve()),
                "TORIRS_DUMP_TREE_EXIT": "1",
                "TORIRS_SIM_CLICK_AT": "210,617,154",
                "TORIRS_CLICK_DEBUG": "1",
                "TORIRS_MINIMENU_DEBUG": "1",
                "TORIRS_SOUND_DEBUG": "1",
                "TORIRS_ANIM_DEBUG": "1",
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
    expect(
        "static graphic=1072 abs=601,138 57x34 hidden=0" in result.stdout,
        "modern hovered orb backing did not render in the visible minimap arc",
    )
    expect(
        "static graphic=20000 abs=631,145 20x20 hidden=0" in result.stdout,
        "source-authentic wolf orb icon did not render",
    )
    expect(
        '(160<<16|62) static font=494 color=0xff00 text="60"' in result.stdout,
        "active familiar orb did not show its 60 special-move points",
    )
    expect("message_game: You summon a Spirit wolf." in result.stdout, "setup did not summon")
    expect(
        "entity_spotanim: combine id=20002" in result.stdout,
        "Spirit wolf summon/call did not render the large familiar-arrival graphic",
    )
    expect(
        "clickdbg: send op1 target=0xa00040 sub=-1 state=2" in result.stdout,
        "orb did not send its real IF_BUTTON1 packet",
    )
    expect("message_game: You call your familiar." in result.stdout, "orb click did not call")
    expect(
        "sound: synth=188 loops=1 delay=0" in result.stdout,
        "familiar call did not emit the verified shared SYNTH_SOUND packet",
    )
    expect("CS2VM2: abort" not in result.stdout, "orb clientscript aborted")

    for error in errors:
        print(f"test_summoning_phase4c: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase4c: {checked} checks, {len(errors)} errors")
    print(f"test_summoning_phase4c: framebuffer and log in {args.out}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
