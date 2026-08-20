#!/usr/bin/env python3
"""Real-client acceptance for the familiar leash.

`playerfollow` walks, and walking is all it does. A player who teleports,
climbs a staircase, or leaves the scene any other way arrives without their
familiar, and nothing used to put it back: the familiar stood where its owner
last was for the rest of its life. `Familiar.update` (2009scape
Familiar.java:271-274) re-calls a familiar left too far behind, and
`~summoning_familiar_leash` (summoning_core.rs2) is that clause, on this
lane's ten-tile familiar distance rather than the source's twelve — the same
ten `~summoning_familiar_assist_allowed` and the engine's
`TORIRSSERVER_FAMILIAR_LEASH` already use.

What is proven live, through the ordinary familiar timer:

  * an owner more than ten tiles from their familiar gets it back, at their
    side, within the tick;
  * the recall is silent. `Familiar.update` does not print "You call your
    familiar." — only the Call Follower operation the player pressed does, and
    a leash that announced itself every time would be a message every teleport.

Where the owner goes is `map_findsquare`'s choice inside the probe, not this
harness's: the assertion is a distance, so it does not need to be a place.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
CONTENT = REPO / "OSRS-Content/osrs239-content"
SCRIPT_LANE = CONTENT / "server/scripts/ported_scape2009_summoning"
# `make -C src` by anyone relinks src/torirs without the embedded server, which
# breaks the runtime half here for a reason unrelated to the leash. Point
# TORIRS_CLIENT at a privately built binary to sidestep a shared tree.
CLIENT = Path(os.environ.get("TORIRS_CLIENT") or REPO / "src/torirs")

# The leash range, and the gap the familiar is allowed to keep afterwards. One
# tile is the answer a footprint-aware `npc_range` gives for a familiar standing
# beside its owner; two is slack for the tick the owner may have moved in.
LEASH_RANGE = 10
AT_HEEL = 2


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=CLIENT)
    parser.add_argument("--cache", type=Path, default=REPO / "cache.osrs239.summoning")
    parser.add_argument(
        "--scripts", type=Path, default=CONTENT / "server/scripts/build_summoning"
    )
    parser.add_argument("--manifest", type=Path, default=REPO / "manifest_osrs239.ini")
    parser.add_argument("--out", type=Path, default=REPO / "build/summoning-leash")
    parser.add_argument(
        "--static-only",
        action="store_true",
        help="skip the client run; check the script source only",
    )
    args = parser.parse_args()

    errors: list[str] = []
    checked = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal checked
        checked += 1
        if not condition:
            errors.append(message)

    # Static half: the range is one number, shared with the two leashes that
    # were already in the tree. A future edit that gives the teleport its own
    # distance would leave a familiar the engine has already stopped fighting
    # with standing wherever it was dropped.
    constants = (SCRIPT_LANE / "configs/summoning.constant").read_text(encoding="utf-8")
    expect(
        f"^summoning_leash_range = {LEASH_RANGE}" in constants,
        f"^summoning_leash_range is not {LEASH_RANGE}",
    )
    combat = (SCRIPT_LANE / "scripts/summoning_combat.rs2").read_text(encoding="utf-8")
    expect(
        f"if (distance(coord, $victim_coord) > {LEASH_RANGE}) return(false);" in combat,
        "the assist leash no longer agrees with the teleport leash",
    )
    engine = (REPO / "src/torirsserver/torirs_server.h").read_text(encoding="utf-8")
    expect(
        f"#define TORIRSSERVER_FAMILIAR_LEASH {LEASH_RANGE}" in engine,
        "the engine's familiar leash no longer agrees with the teleport leash",
    )
    core = (SCRIPT_LANE / "scripts/summoning_core.rs2").read_text(encoding="utf-8")
    tick = core.split("[timer,summoning_tick]")[1].split("\n[")[0]
    leash = core.split("[proc,summoning_familiar_leash]")[-1].split("\n[proc,")[0]
    # A commented-out call still contains the call, so the line has to be a
    # statement of the tick and not merely text inside it.
    expect(
        any(
            line.strip() == "~summoning_familiar_leash;"
            for line in tick.splitlines()
            if not line.strip().startswith("//")
        ),
        "the familiar tick does not run the leash",
    )
    expect(
        "if (npc_getmode ! playerfollow) return;" in leash,
        "the leash no longer waits for a familiar that is actually heeling, so a"
        " targeted special's approach walk can be teleported out from under it",
    )
    expect(
        "~summoning_call_familiar_ex(false);" in leash,
        "the leash does not recall through `call`, so it skips the arrival"
        " graphic and the dropped combat target",
    )

    if args.static_only:
        return finish(errors, checked, args.out)

    for label, path in (
        ("embedded client", args.client),
        ("feature cache", args.cache / "main_file_cache.dat2"),
        ("feature script pack", args.scripts / "script.dat"),
        ("manifest", args.manifest),
    ):
        expect(path.is_file() and path.stat().st_size > 0, f"missing {label}: {path}")
    if errors:
        return finish(errors, checked, args.out)

    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="summoning_leash_saves_") as saves:
        env = os.environ.copy()
        for key in ("TORIRS_NET_CHEAT", "TORIRS_SIM_CLICK_AT", "TORIRS_MAX_FRAMES"):
            env.pop(key, None)
        env.update(
            {
                "TORIRSSERVER_SAVES": saves,
                "TORIRSSERVER_SCRIPTS": str(args.scripts.resolve()),
                "TORIRSSERVER_CACHE": str(args.cache.resolve()),
                "SDL_VIDEODRIVER": "dummy",
                "TORIRS_MAX_FRAMES": "420",
                "TORIRS_NET_DEBUG": "1",
                "TORIRS_NET_CHEAT": (
                    "summoning_unlock;summoning_demo;summoning_leash_probe"
                ),
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
            timeout=120,
            check=False,
        )

    log = result.stdout
    (args.out / "leash.log").write_text(log, encoding="utf-8")
    expect(result.returncode == 0, f"client exited {result.returncode}")
    expect("SKIP" not in log, "client reported SKIP")
    expect("SSVM: abort" not in log, "a server script aborted")

    left = message_number(log, r"leash probe: left (\d+) tiles behind")
    expect(
        left is not None and left > LEASH_RANGE,
        "the probe never got the owner further than the leash range"
        f" (read {left}); nothing was proven",
    )
    now = message_number(log, r"leash probe: familiar is (\d+) tiles away")
    expect(
        now is not None,
        "the familiar never reported back, so the leash tick did not run",
    )
    expect(
        now is not None and now <= AT_HEEL,
        f"the familiar was left {now} tiles behind its owner; the leash did not"
        " bring it back",
    )
    expect(
        "message_game: You call your familiar." not in log,
        "the leash printed the Call Follower message; `Familiar.update` recalls"
        " silently",
    )

    return finish(errors, checked, args.out)


def message_number(log: str, pattern: str) -> int | None:
    match = re.search(r"message_game: " + pattern, log)
    return int(match.group(1)) if match else None


def finish(errors: list[str], checked: int, out: Path) -> int:
    for error in errors:
        print(f"test_summoning_leash: {error}", file=sys.stderr)
    print(f"test_summoning_leash: {checked} checks, {len(errors)} errors (logs in {out})")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
