#!/usr/bin/env python3
"""Real-client acceptance for the runtime Summoning obelisk and Renew-points."""

from __future__ import annotations

import argparse
import os
import struct
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
    parser.add_argument("--out", type=Path, default=REPO / "build/summoning-phase4e")
    args = parser.parse_args()

    errors: list[str] = []
    checked = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal checked
        checked += 1
        if not condition:
            errors.append(message)

    for label, path in (
        ("embedded client", args.client),
        ("feature cache", args.cache / "main_file_cache.dat2"),
        ("feature script pack", args.scripts / "script.dat"),
        ("manifest", args.manifest),
    ):
        expect(path.is_file() and path.stat().st_size > 0, f"missing {label}: {path}")

    wire = (REPO / "src/net/mock/mock230_wire.c").read_text(encoding="utf-8")
    expect(
        "case PKT_NAME_LOC_ADD_CHANGE:" in wire and "rsab_p2_alt3(buf, id);" in wire,
        "rev239 LOC_ADD_CHANGE no longer writes the loc config as an exact 16-bit p2Alt3",
    )
    bridge = (REPO / "src/net/mock/mock230_encode.c").read_text(encoding="utf-8")
    expect(
        "player->masks & MOCK230_PMASK_SPOTANIM" in bridge
        and "ext.has_spotanim = 1" in bridge
        and "ext.spotanim_slot = 0" in bridge,
        "rev239 player-info bridge does not forward SPOTANIM_PL",
    )
    script = (
        REPO
        / "OSRS-Content/osrs239-content/server/scripts/ported_scape2009_summoning"
        / "scripts/summoning_points.rs2"
    ).read_text(encoding="utf-8")
    expect("[oploc2,summoning_obelisk]" in script, "Renew-points is not bound to loc op2")
    expect(
        "spotanim_pl(summoning_renew_points_gfx, 0, 0);" in script,
        "Renew-points does not request its imported graphic",
    )

    if errors:
        return finish(errors, checked, args.out)

    args.out.mkdir(parents=True, exist_ok=True)
    bmp = (args.out / "renew-points.bmp").resolve()
    with tempfile.TemporaryDirectory(prefix="summoning_phase4e_saves_") as saves:
        env = os.environ.copy()
        env.update(
            {
                "MOCK230_SAVES": saves,
                "MOCK230_SCRIPTS": str(args.scripts.resolve()),
                "MOCK230_CACHE": str(args.cache.resolve()),
                "MOCK230_EXT_DEBUG": "1",
                "SDL_VIDEODRIVER": "dummy",
                "TORIRS_MAX_FRAMES": "390",
                "TORIRS_NET_CHEAT": "summoning_unlock;summoning_demo;drain summoning 1 0",
                # Right-click the projected imported obelisk, then choose its
                # real second menu entry. These are ordinary input events, not
                # a direct server-script or component-hook dispatch.
                "TORIRS_SIM_CLICK_AT": "240,465,205,1;270,445,236",
                "TORIRS_CLICK_DEBUG": "1",
                "TORIRS_NET_DEBUG": "1",
                "TORIRS_ANIM_DEBUG": "1",
                "TORIRS_PICK_DEBUG": "all",
                "TORIRS_WORLD_PICK_DEBUG": "1",
                "TORIRS_LOC_CFG": "62201",
                "TORIRS_EMIT_LOC": "62201",
                "TORIRS_EXIT_BMP": str(bmp),
            }
        )
        result = subprocess.run(
            [
                str(args.client.resolve()),
                str(args.cache.resolve()),
                "--manifest",
                str(args.manifest.resolve()),
                "--soft3d",
            ],
            cwd=REPO,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=90,
            check=False,
        )

    (args.out / "renew-points.log").write_text(result.stdout, encoding="utf-8")
    expect(result.returncode == 0, f"client exited {result.returncode}")
    expect("SKIP" not in result.stdout, "client reported SKIP")
    expect("CS2VM2: abort" not in result.stdout, "clientscript aborted")
    expect(
        "loc_cfg 62201: name='Obelisk' size=2x2 seq=20002" in result.stdout,
        "client did not decode the imported Obelisk config",
    )
    expect("group 0: shape=-1 models(1): 100005" in result.stdout,
           "Obelisk did not resolve imported model 100005")
    expect("emit_loc 62201" in result.stdout, "Obelisk never reached the scene draw list")
    expect("loc=62201 size=2x2" in result.stdout,
           "real mouse picking did not hit the runtime Obelisk")
    expect("loc=4464" not in result.stdout, "loc id 62201 was truncated to 4464")
    expect("sim_click_at: frame=240 move 465,205 right=1" in result.stdout,
           "real right-click was not injected")
    expect("sim_click_at: frame=270 move 445,236 right=0" in result.stdout,
           "real Renew-points menu click was not injected")
    expect("Summoning points: 0/1" in result.stdout, "test never established drained points")
    expect("Summoning points: 1/1" in result.stdout, "Renew-points did not restore the stat")
    expect("message_game: You renew your summoning points." in result.stdout,
           "loc op2 did not execute Renew-points")
    expect("spotanim=20000/0" in result.stdout and "seq=20003/0" in result.stdout,
           "server did not place both Renew masks in one rev239 player update")
    expect("player_info: spotanim idx=0 id=20000 height=0 delay=0" in result.stdout,
           "client did not decode Renew spotanim 20000")
    expect("player_info: sequence idx=0 id=20003 delay=0" in result.stdout,
           "client did not decode Renew animation 20003")
    expect("entity_spotanim: combine id=20000" in result.stdout
           and "seq=20004 frame=0" in result.stdout,
           "client did not load and combine the imported Renew model/sequence chain")
    expect(bmp.is_file() and bmp.stat().st_size > 54, "active-effect framebuffer is absent")
    if bmp.is_file() and bmp.stat().st_size > 54:
        expect(
            green_effect_pixels(bmp, 300, 140, 390, 240) >= 100,
            "Renew graphic is absent from the player-area framebuffer",
        )

    return finish(errors, checked, args.out)


def green_effect_pixels(path: Path, x0: int, y0: int, x1: int, y1: int) -> int:
    data = path.read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    bits = struct.unpack_from("<H", data, 28)[0]
    if width <= 0 or height <= 0 or bits != 32:
        return 0
    stride = width * 4
    count = 0
    for y in range(max(0, y0), min(height, y1)):
        row = offset + (height - 1 - y) * stride
        for x in range(max(0, x0), min(width, x1)):
            blue, green, red, _ = data[row + x * 4 : row + x * 4 + 4]
            if green >= 120 and green >= red + 25 and green >= blue + 15:
                count += 1
    return count


def finish(errors: list[str], checked: int, out: Path) -> int:
    for error in errors:
        print(f"test_summoning_phase4e: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase4e: {checked} checks, {len(errors)} errors ({out})")
    if checked == 0:
        return 1
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
