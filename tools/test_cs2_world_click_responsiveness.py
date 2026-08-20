#!/usr/bin/env python3
"""World clicks must survive CS2 frame-settlement pauses.

This drives the native C client and embedded revision-239 server.  An active
Spirit wolf keeps the Summoning timer/client-script path busy while ten ordinary
world clicks are released in a short burst. Before the transaction/input fixes,
the deterministic sequence produced ten release lines but only six or seven
``click:`` handlers: early App_RunOnce returns discarded the other mouse-ups.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
CLICK_FRAMES = (148, 156, 164, 172, 180, 188, 196, 204, 212, 220)
CLICK_X = 400
CLICK_Y = 200


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
    parser.add_argument(
        "--out", type=Path, default=REPO / "build/cs2-world-click-responsiveness"
    )
    args = parser.parse_args()

    required = (
        ("client", args.client),
        ("cache", args.cache / "main_file_cache.dat2"),
        ("server scripts", args.scripts / "script.dat"),
        ("manifest", args.manifest),
    )
    missing = [f"missing {label}: {path}" for label, path in required if not valid_file(path)]
    if missing:
        return finish(missing, 0, 0, 0, args.out)

    click_spec = ";".join(f"{frame},{CLICK_X},{CLICK_Y}" for frame in CLICK_FRAMES)
    args.out.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="cs2_world_click_saves_") as saves:
        env = os.environ.copy()
        env.update(
            {
                "TORIRSSERVER_SAVES": saves,
                "TORIRSSERVER_SCRIPTS": str(args.scripts.resolve()),
                "TORIRSSERVER_CACHE": str(args.cache.resolve()),
                "SDL_VIDEODRIVER": "dummy",
                "SDL_AUDIODRIVER": "dummy",
                "TORIRS_MAX_FRAMES": "235",
                "TORIRS_NET_CHEAT": "summoning_unlock;summoning_demo",
                "TORIRS_SIM_CLICK_AT": click_spec,
                "TORIRS_CLICK_DEBUG": "1",
                "TORIRS_NET_DEBUG": "1",
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
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=90,
            check=False,
        )

    log_path = args.out / "world-click.log"
    log_path.write_text(result.stdout, encoding="utf-8")

    errors: list[str] = []
    release_marker = f"sim_click_at: released {CLICK_X},{CLICK_Y}"
    releases = result.stdout.count(release_marker)
    handlers = sum(line.startswith("click: ") for line in result.stdout.splitlines())
    walks = result.stdout.count("minimenu: walk-click")

    if result.returncode != 0:
        errors.append(f"client exited {result.returncode}")
    if "CS2VM2: abort" in result.stdout:
        errors.append("clientscript aborted")
    if "message_game: You summon a Spirit wolf." not in result.stdout:
        errors.append("active Summoning setup did not summon the Spirit wolf")
    if releases != len(CLICK_FRAMES):
        errors.append(f"injected {releases}/{len(CLICK_FRAMES)} mouse releases")
    if handlers != releases:
        errors.append(f"handled {handlers}/{releases} released world clicks")
    if walks < 1:
        errors.append("no released click reached the Walk-here/MOVE_GAMECLICK path")

    # A total can be fooled by one dropped edge plus one duplicated edge.  Pin
    # the stronger invariant: between this release and the next release, the
    # client must handle this click exactly once. The next move begins four
    # loops later and the next release arrives eight loops later: enough for a
    # one-turn async resume, but far below the old wait for a 600 ms world tick.
    per_release = handlers_between_releases(result.stdout.splitlines(), release_marker)
    for index, count in enumerate(per_release, start=1):
        if count != 1:
            errors.append(f"release {index} produced {count} click handlers before the next release")

    return finish(errors, releases, handlers, walks, args.out)


def handlers_between_releases(lines: list[str], release_marker: str) -> list[int]:
    counts: list[int] = []
    current: int | None = None

    for line in lines:
        if line == release_marker:
            if current is not None:
                counts.append(current)
            current = 0
        elif current is not None and line.startswith("click: "):
            current += 1
    if current is not None:
        counts.append(current)
    return counts


def valid_file(path: Path) -> bool:
    return path.is_file() and path.stat().st_size > 0


def finish(
    errors: list[str], releases: int, handlers: int, walks: int, out: Path
) -> int:
    for error in errors:
        print(f"test_cs2_world_click_responsiveness: error: {error}", file=sys.stderr)
    print(
        "test_cs2_world_click_responsiveness: "
        f"releases={releases} handlers={handlers} walks={walks} "
        f"errors={len(errors)} ({out})"
    )
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
