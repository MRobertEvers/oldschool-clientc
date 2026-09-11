#!/usr/bin/env python3
"""Prove that GWD private-room state cannot survive a server restart."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def run_phase(server: Path, scripts: Path, cache: Path, saves: Path, phase: str) -> None:
    env = os.environ.copy()
    env.update(
        {
            "TORIRSSERVER_CACHE": str(cache.resolve()),
            "TORIRSSERVER_SAVES": str(saves.resolve()),
            "TORIRSSERVER_SCRIPTS": str(scripts.resolve()),
            "TORIRSSERVER_SELFTEST_GWD_RESTART_PHASE": phase,
        }
    )
    result = subprocess.run(
        [str(server.resolve()), "--selftest"],
        cwd=server.resolve().parents[2],
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    marker = f"ToriRSServer God Wars restart {phase}: 0 failure(s)"
    if result.returncode != 0 or marker not in result.stdout:
        raise SystemExit(
            f"God Wars restart phase {phase!r} failed\n{result.stdout}"
        )
    print(marker)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, default=Path("src/build_opt/torirsserver"))
    parser.add_argument("--scripts", type=Path, required=True)
    parser.add_argument("--cache", type=Path, default=Path("cache.osrs239"))
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="godwars_restart_saves_") as temp:
        saves = Path(temp)
        run_phase(args.server, args.scripts, args.cache, saves, "arm")
        run_phase(args.server, args.scripts, args.cache, saves, "assert")

    print("God Wars two-process restart acceptance: PASS")


if __name__ == "__main__":
    main()
