#!/usr/bin/env python3
"""Real-client acceptance for Summoning sidebar access in every gameframe."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
WOLF_ICON_SHA256 = "89726834d13ce73b8fff38eb34567ed2e52c7757b2d8405577e801979e4178cd"


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
    parser.add_argument("--out", type=Path, default=REPO / "build/summoning-phase4d")
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
    icon = (
        REPO
        / "OSRS-Content/osrs239-content/sprites/ported/scape2009_summoning"
        / "summoning_staticon/0.bmp"
    )
    expect(
        icon.is_file() and hashlib.sha256(icon.read_bytes()).hexdigest() == WOLF_ICON_SHA256,
        "sidebar icon pixels are not the exact rev-530 sprite-222 wolf head",
    )
    if errors:
        return finish(errors, checked, args.out)

    args.out.mkdir(parents=True, exist_ok=True)

    cases = (
        {
            "label": "classic",
            "click": "220,707,484",
            "target": "0xa10063",
            "bounds": "161",
        },
        {
            "label": "fixed",
            "click": "240,744,484",
            "target": "0x2240060",
            "bounds": "548",
            "runscript": "100,3998,0",
            "root_switch": "if-opentop: switching root 161 -> 548",
        },
        {
            "label": "modern",
            "click": "380,766,750",
            "target": "0xa40062",
            "bounds": "164",
            "root_size": "1024x768",
            "save_mode": 2,
            "root_switch": "if-opentop: switching root 161 -> 164",
        },
    )

    for case in cases:
        label = str(case["label"])
        with tempfile.TemporaryDirectory(prefix=f"summoning_phase4d_{label}_saves_") as saves:
            if "save_mode" in case:
                write_save(Path(saves), int(case["save_mode"]))
            env = base_env(args, saves)
            env.update(
                {
                    "TORIRS_MAX_FRAMES": "700" if label == "modern" else "430",
                    "TORIRS_SIM_CLICK_AT": str(case["click"]),
                    "TORIRS_CLICK_DEBUG": "1",
                    "TORIRS_DUMP_BOUNDS": str(case["bounds"]),
                    "TORIRS_DUMP_EMIT_EXIT": "969",
                    "TORIRS_NPC_HEAD_DEBUG": "1",
                    "TORIRS_DUMP_COM": str((969 << 16) | 3),
                    "TORIRS_NET_DEBUG": "1",
                    "TORIRS_EXIT_BMP": str((args.out / f"{label}.bmp").resolve()),
                }
            )
            if "runscript" in case:
                env["TORIRS_SIM_RUNSCRIPT"] = str(case["runscript"])
            if "root_size" in case:
                env["TORIRS_ROOT_SIZE"] = str(case["root_size"])
            result = run_client(args, env)
        log_path = args.out / f"{label}.log"
        log_path.write_text(result.stdout, encoding="utf-8")
        bmp = args.out / f"{label}.bmp"

        expect(result.returncode == 0, f"{label}: client exited {result.returncode}")
        expect(bmp.is_file() and bmp.stat().st_size > 54, f"{label}: no framebuffer")
        expect("SKIP" not in result.stdout, f"{label}: client reported SKIP")
        expect("CS2VM2: abort" not in result.stdout, f"{label}: clientscript aborted")
        expect(
            f"clickdbg: send op1 target={case['target']} sub=-1 state=2" in result.stdout,
            f"{label}: wolf tab did not send its real IF_BUTTON1 packet",
        )
        if "root_switch" in case:
            expect(str(case["root_switch"]) in result.stdout, f"{label}: wrong toplevel")
        expect(
            "EMIT_EXIT" in result.stdout and "(969|2)" in result.stdout,
            f"{label}: familiar model did not reach the final draw list",
        )
        model_emit = next(
            (line for line in result.stdout.splitlines()
             if "EMIT_EXIT" in line and "(969|2)" in line),
            "",
        )
        expect(
            "kind=5" in model_emit and "model=1342197280" in model_emit,
            f"{label}: familiar widget did not emit the composed NPC 20000 head "
            f"(got {model_emit or 'no model command'})",
        )
        expect(
            "npc_head: npc=20000 component=0x03c90002 scene=1342197280 applied=1"
            in result.stdout,
            f"{label}: NPC head compositor did not bind the packed Spirit wolf head",
        )
        expect(
            "Summoning points: 0/1" in result.stdout,
            f"{label}: live Summoning point count did not render",
        )
        for component, caption in ((5, "Call"), (7, "Dismiss")):
            rect = emitted_rect(result.stdout, component)
            expect(rect is not None, f"{label}: {caption} label did not reach the draw list")
            if rect is not None:
                orange = bmp_orange_count(bmp, rect)
                expect(
                    orange >= 8,
                    f"{label}: {caption} label command was covered or invisible "
                    f"({orange} orange pixels)",
                )
        expect(
            "message_game: You summon a Spirit wolf." in result.stdout,
            f"{label}: setup did not summon the familiar",
        )
        if label == "modern":
            expect(
                "(164|99) type=15 graphic=229 hidden=0 abs=755,742 25x25" in result.stdout,
                "modern: exact wolf icon is absent from its unclipped fifteenth cell",
            )

    with tempfile.TemporaryDirectory(prefix="summoning_phase4d_buttons_saves_") as saves:
        env = base_env(args, saves)
        env.update(
            {
                "TORIRS_MAX_FRAMES": "460",
                "TORIRS_SIM_CLICK_AT": "190,707,484;260,602,403;330,602,439",
                "TORIRS_CLICK_DEBUG": "1",
                "TORIRS_DUMP_BOUNDS": "969",
                "TORIRS_EXIT_BMP": str((args.out / "buttons.bmp").resolve()),
            }
        )
        result = run_client(args, env)
    (args.out / "buttons.log").write_text(result.stdout, encoding="utf-8")
    expect(result.returncode == 0, f"buttons: client exited {result.returncode}")
    expect("clickdbg: send op1 target=0x3c90005 sub=-1 state=2" in result.stdout,
           "Call button did not send IF_BUTTON1")
    expect("message_game: You call your familiar." in result.stdout,
           "Call button did not execute familiar logic")
    expect("clickdbg: send op1 target=0x3c90007 sub=-1 state=2" in result.stdout,
           "Dismiss button did not send IF_BUTTON1")
    expect("message_game: You dismiss your familiar." in result.stdout,
           "Dismiss button did not execute familiar logic")
    expect("hidden=1" in bounds_line(result.stdout, "(969|2)"),
           "Dismiss did not hide the familiar model")

    return finish(errors, checked, args.out)


def base_env(args: argparse.Namespace, saves: str) -> dict[str, str]:
    env = os.environ.copy()
    env.update(
        {
            "MOCK230_SAVES": saves,
            "MOCK230_SCRIPTS": str(args.scripts.resolve()),
            "MOCK230_CACHE": str(args.cache.resolve()),
            "TORIRS_NET_CHEAT": "summoning_unlock;summoning_demo",
            "SDL_VIDEODRIVER": "dummy",
        }
    )
    return env


def write_save(root: Path, mode: int) -> None:
    (root / "guest.ini").write_text(
        "[player]\nversion = 1\nname = guest\nclient_layout_mode = "
        f"{mode}\n\n[stats]\n3 = 10 11540\n",
        encoding="utf-8",
    )


def run_client(args: argparse.Namespace, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(args.client), str(args.cache), "--manifest", str(args.manifest), "--soft3d"],
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


def bounds_line(log: str, marker: str) -> str:
    return next((line for line in log.splitlines() if line.startswith("BOUNDS ") and marker in line), "")


def emitted_rect(log: str, component: int) -> tuple[int, int, int, int] | None:
    pattern = re.compile(
        rf"EMIT_EXIT.*\(969\|{component}\).* x=(\d+) y=(\d+) w=(\d+) h=(\d+)"
    )
    matches = [pattern.search(line) for line in log.splitlines()]
    match = next((item for item in reversed(matches) if item is not None), None)
    return tuple(map(int, match.groups())) if match else None


def bmp_orange_count(path: Path, rect: tuple[int, int, int, int]) -> int:
    """Count visible Summoning-gold pixels in the central area of a 32-bit BMP."""
    data = path.read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits != 32 or width <= 0 or height == 0:
        return 0
    top_down = height < 0
    height = abs(height)
    x, y, w, h = rect
    count = 0
    for py in range(max(0, y + 3), min(height, y + h - 3)):
        row = py if top_down else height - 1 - py
        for px in range(max(0, x + 8), min(width, x + w - 8)):
            pos = offset + (row * width + px) * 4
            blue, green, red = data[pos : pos + 3]
            if red >= 170 and 65 <= green <= 210 and blue <= 100 and red >= green + 35:
                count += 1
    return count


def finish(errors: list[str], checked: int, out: Path) -> int:
    for error in errors:
        print(f"test_summoning_phase4d: error: {error}", file=sys.stderr)
    print(f"test_summoning_phase4d: {checked} checks, {len(errors)} errors")
    print(f"test_summoning_phase4d: framebuffers and logs in {out}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
