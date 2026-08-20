#!/usr/bin/env python3
"""Real-client acceptance for followers crossing into and out of map instances.

A map instance is a private copy of a piece of map at coordinates nobody else is
using — the Inferno, the Gauntlet, a quest arena. Nobody *walks* there, so
`playerfollow` cannot carry a follower across: the owner is teleported in, and
whatever is at their heel is left standing on the ordinary map for the rest of
the session unless something puts it back.

Two crossings are proven here, for two kinds of follower:

  * a summoned familiar, which had the leash (`~summoning_familiar_leash`) and
    caught up on the ten-tile rule alone;
  * a released pet, which did not. `[timer,summoning_tick]` read
    `%summoning_familiar_active = 0`, cleared state the pet does not own and
    stopped its own timer on the first tick after release, so a Clockwork cat
    never leashed at all — measured at 3,208 tiles behind and staying there.

The exit is proven as well as the entry, and the exit deliberately releases the
instance while the follower may still be inside it: the pool re-issues a freed
square immediately, so a follower left behind by one tick is a follower standing
in the next session's arena.

The gap is read through `npc_range`, which measures the follower's FOOTPRINT and
answers `max_32bit_int` across a plane change, so a two-tile familiar beside its
owner reads 1 rather than 2.
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
# breaks the runtime half here for a reason unrelated to instances. Point
# TORIRS_CLIENT at a privately built binary to sidestep a shared tree.
CLIENT = Path(os.environ.get("TORIRS_CLIENT") or REPO / "src/torirs")

# The leash range, and the gap a follower is allowed to keep after a recall. One
# tile is the answer a footprint-aware `npc_range` gives for a follower standing
# beside its owner; two is slack for the tick the owner may have moved in.
LEASH_RANGE = 10
AT_HEEL = 2

# The two followers a player can have out, and the cheat that provides each.
# `summoning_demo` summons the Spirit wolf (size 2), `summoning_pet_release`
# releases the Clockwork cat (size 1, and no familiar state behind it).
FOLLOWERS = (
    ("familiar", "summoning_demo"),
    ("pet", "summoning_pet_release"),
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=CLIENT)
    parser.add_argument("--cache", type=Path, default=REPO / "cache.osrs239.summoning")
    parser.add_argument(
        "--scripts", type=Path, default=CONTENT / "server/scripts/build_summoning"
    )
    parser.add_argument("--manifest", type=Path, default=REPO / "manifest_osrs239.ini")
    parser.add_argument("--out", type=Path, default=REPO / "build/summoning-instance")
    parser.add_argument(
        "--static-only",
        action="store_true",
        help="skip the client runs; check the script source only",
    )
    args = parser.parse_args()

    errors: list[str] = []
    checked = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal checked
        checked += 1
        if not condition:
            errors.append(message)

    core = (SCRIPT_LANE / "scripts/summoning_core.rs2").read_text(encoding="utf-8")
    pet = (SCRIPT_LANE / "scripts/summoning_pet_clockwork_cat.rs2").read_text(
        encoding="utf-8"
    )
    leash = core.split("[proc,summoning_familiar_leash]")[-1].split("\n[proc,")[0]
    tick = core.split("[timer,summoning_tick]")[1].split("\n[")[0]

    # Static half. The runtime half below can only prove the crossing it runs,
    # and the two structural properties it cannot see are the ones a later edit
    # is most likely to undo.
    instance_clause = "if (map_instance_find(npc_coord) ! map_instance_find(coord)) {"
    expect(
        instance_clause in leash,
        "the leash no longer recalls on an instance mismatch, so it is back to"
        " relying on the pool happening to be thousands of tiles away",
    )
    # Order is the point, not presence: below the mode gate, a follower that is
    # mid-errand when its owner crosses waits for the errand to end — and the
    # errand's own end condition is a distance to a target in another world.
    expect(
        instance_clause in leash
        and leash.index(instance_clause) < leash.index("if (npc_getmode ! playerfollow)"),
        "the instance clause is below the `playerfollow` gate, so a follower"
        " mid-errand no longer crosses with its owner",
    )
    expect(
        "~summoning_familiar_leash;" in tick,
        "the follower tick no longer runs the leash",
    )
    expect(
        "if (%summoning_pet_active = 1 & %summoning_familiar_active = 0) {" in tick,
        "the follower tick has no pet branch, so a pet's timer is stopped by the"
        " familiar-state guard on its first tick and it never leashes again",
    )
    expect(
        "settimer(summoning_tick, 1);" in pet,
        "releasing a pet no longer arms the follower tick, so nothing runs its"
        " leash",
    )
    expect(
        "cleartimer(summoning_tick);" in pet,
        "clearing pet state no longer stops the follower tick",
    )
    expect(
        "if (%summoning_pet_active = 1) {" in core.split("[proc,summoning_call_familiar_ex]")[-1].split("\n[proc,")[0],
        "the recall no longer splits the pet case out, so a Clockwork cat"
        " materialises inside the blue summoning graphic every time it catches up"
        " (Familiar.call guards both on `!(this instanceof Pet)`)",
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
    for kind, provision in FOLLOWERS:
        log = run_probe(args, provision)
        (args.out / f"{kind}.log").write_text(log, encoding="utf-8")

        expect("SKIP" not in log, f"{kind}: client reported SKIP")
        expect("SSVM: abort" not in log, f"{kind}: a server script aborted")
        expect(
            "instance probe: no free instance" not in log,
            f"{kind}: the pool was exhausted, so nothing was proven",
        )
        expect(
            "instance probe: no follower" not in log,
            f"{kind}: the probe ran with nothing following the player",
        )

        # Vacuity guard: an entry that did not leave the follower behind proves
        # nothing about bringing it along.
        left = number(log, r"instance probe: entered \d+, left (\d+) tiles behind")
        expect(
            left is not None and left > LEASH_RANGE,
            f"{kind}: the crossing left the follower {left} tiles behind, which is"
            " inside the leash range; nothing was proven",
        )

        inside = number(log, r"instance probe: inside, follower is (\d+) tiles away")
        expect(
            inside is not None,
            f"{kind}: the follower never reported from inside the instance",
        )
        expect(
            inside is not None and inside <= AT_HEEL,
            f"{kind}: the follower was left {inside} tiles behind on the way IN;"
            " it did not cross into the instance",
        )
        pair = re.search(
            r"instance probe: inside, follower in instance (\d+) of (\d+)", log
        )
        expect(
            pair is not None and pair.group(1) == pair.group(2) and pair.group(2) != "0",
            f"{kind}: inside the instance the follower was in a different one"
            f" ({pair.groups() if pair else None})",
        )

        outside = number(log, r"instance probe: outside, follower is (\d+) tiles away")
        expect(
            outside is not None,
            f"{kind}: the follower never reported from outside; the release may"
            " have taken it with the instance",
        )
        expect(
            outside is not None and outside <= AT_HEEL,
            f"{kind}: the follower was left {outside} tiles behind on the way OUT,"
            " standing in a reservation the pool has already re-issued",
        )
        expect(
            "message_game: You call your familiar." not in log,
            f"{kind}: the crossing printed the Call Follower message; a recall the"
            " player did not ask for is silent",
        )

    return finish(errors, checked, args.out)


def run_probe(args: argparse.Namespace, provision: str) -> str:
    with tempfile.TemporaryDirectory(prefix="summoning_instance_saves_") as saves:
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
                    f"summoning_unlock;{provision};summoning_instance_probe"
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
    if result.returncode != 0:
        return result.stdout + f"\nclient exited {result.returncode}\nSKIP\n"
    return result.stdout


def number(log: str, pattern: str) -> int | None:
    match = re.search(r"message_game: " + pattern, log)
    return int(match.group(1)) if match else None


def finish(errors: list[str], checked: int, out: Path) -> int:
    for error in errors:
        print(f"test_summoning_instance: {error}", file=sys.stderr)
    print(
        f"test_summoning_instance: {checked} checks, {len(errors)} errors"
        f" (logs in {out})"
    )
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
