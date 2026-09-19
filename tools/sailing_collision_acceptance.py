#!/usr/bin/env python3
"""Exercise real-ocean collision in a warm native sailing session.

Build the embedded client and current content before running. This deliberately
uses actual sea/shore geometry and never writes collision flags. Screenshots
must also be visually inspected; assertions alone do not establish appearance.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from sailing_harness import HarnessError, ROOT, Session, parser as harness_parser


def require(condition: bool, message: str) -> None:
    if not condition:
        raise HarnessError(message)


def vessel(state: dict) -> dict:
    require(state.get("sailing", {}).get("aboard", False), "Player must remain aboard")
    return state["sailing"]["vessel"]


def map_index(collision: dict, x: int, z: int) -> int:
    width = collision["width"]
    radius = width // 2
    row = collision["z"] + radius - z
    col = x - collision["x"] + radius
    require(0 <= row < width and 0 <= col < width, "Collision query does not cover target")
    return row * width + col


def pose(boat: dict) -> tuple:
    return tuple(boat[key] for key in ("fine_x", "fine_z", "angle"))


def run(session: Session, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    metadata = session.metadata()
    started = time.perf_counter()
    session.checked("pause")
    initial = session.checked("state")
    # Reruns start from the existing checkpoint, preserving the same warm boat.
    if initial.get("sailing", {}).get("checkpoints", 0):
        restored = session.request("restore collision_ocean")
        if restored.get("ok"):
            initial = session.checked("state")
    before = vessel(initial)
    require((before["fine_x"] // 128, before["fine_z"] // 128) == (3072, 3160),
            "Fixture must start in the surveyed real ocean at 3072,3160")
    require(not before["sails_set"] and not before["reversing"], "Initial boat must have sails down and reverse disengaged")
    session.checked("camera 1024 256 1200")
    initial = session.checked("state")
    before = vessel(initial)
    session.checked("save collision_ocean")
    require(pose(initial["sailing"]["client_vessel"]) == pose(before),
            "Initial client hull has not reached the authoritative ocean pose")
    captures = [session.capture(output / "collision-before.png")]
    ocean = session.checked("collision 20")
    require(ocean["order"] == "north_to_south", "Unknown collision-grid ordering")
    for x in range(3064, 3081):
        for z in range(3152, 3169):
            index = map_index(ocean, x, z)
            require(ocean["boat_open"][index] == "1", f"Actual ocean blocked a boat at {x},{z}")
            require(ocean["player_open"][index] == "0", f"Player could walk on ocean at {x},{z}")

    session.checked("cheat vesselsail 0 2")
    session.checked("step 60")
    underway_state = session.checked("state")
    underway = vessel(underway_state)
    require(underway["fine_x"] == before["fine_x"] and underway["fine_z"] < before["fine_z"],
            "Hull did not move south through real ocean")
    require(underway["state"] == 1, "Clear water prematurely parked the hull")
    captures.append(session.capture(output / "collision-underway.png"))

    # A checkpoint in the middle of interpolation must preserve the paused
    # image and future network-delta target, not merely snap to the server.
    mid_visual = underway_state["sailing"]["client_vessel"]
    session.checked("save collision_underway")
    session.checked("step 15")
    session.checked("restore collision_underway")
    underway_state = session.checked("state")
    restored_visual = underway_state["sailing"]["client_vessel"]
    require(pose(restored_visual) == pose(mid_visual),
            "Mid-motion checkpoint failed to restore the paused client pose")
    require((restored_visual["target_fine_x"], restored_visual["target_fine_z"], restored_visual["target_angle"])
            == (mid_visual["target_fine_x"], mid_visual["target_fine_z"], mid_visual["target_angle"]),
            "Mid-motion restore changed the network-delta target")
    captures.append(session.capture(output / "collision-mid-motion-restored.png"))

    path = [{"tick": underway_state["sailing"]["server_tick"], "pose": pose(underway)}]
    stopped_state = None
    for _ in range(40):
        session.checked("step 30")
        state = session.checked("state")
        boat = vessel(state)
        path.append({"tick": state["sailing"]["server_tick"], "pose": pose(boat)})
        if boat["state"] == 0:
            stopped_state = state
            break
    require(stopped_state is not None, "Hull failed to park before crossing the real southern shore")
    stopped = vessel(stopped_state)
    require(3128 * 128 < stopped["fine_z"] < 3152 * 128,
            "Hull stopped outside the surveyed southern coastline")
    coast = session.checked("collision 20")
    center = map_index(coast, stopped["fine_x"] // 128, stopped["fine_z"] // 128)
    require(coast["boat_open"][center] == "1", "Stopped hull center ended on blocked terrain")
    # The next forward tick must approach an actual blocked boat-map tile.
    require(any(coast["boat_open"][map_index(coast, x, z)] == "0"
                for x in range(stopped["fine_x"] // 128 - 1, stopped["fine_x"] // 128 + 2)
                for z in range(stopped["fine_z"] // 128 - 4, stopped["fine_z"] // 128)),
            "Hull parked with no shore immediately ahead")
    session.checked("step 90")
    stable = vessel(session.checked("state"))
    require(pose(stable) == pose(stopped) and stable["state"] == 0,
            "Parked hull continued drifting into shore")
    captures.append(session.capture(output / "collision-stopped.png"))

    session.checked("restore collision_ocean")
    reset = session.checked("state")
    for key in ("fine_x", "fine_z", "angle", "heading", "state", "speed_tier", "sails_set", "reversing"):
        require(vessel(reset)[key] == before[key], f"Checkpoint failed to restore vessel {key}")
    client = reset["sailing"]["client_vessel"]
    require(client["present"] and pose(client) == pose(before),
            "Restore left the actual rendered client hull at a different pose")
    require((client["target_fine_x"], client["target_fine_z"], client["target_angle"]) == pose(before),
            "Restore did not deliver the saved pose through the world-entity protocol")
    require(reset["sailing"]["player"] == initial["sailing"]["player"],
            "Checkpoint failed to restore aboard player and helm ownership")
    captures.append(session.capture(output / "collision-restored.png"))

    # Choose loaded, walkable land from the real coastline; unknown map and an
    # upper deck plane would be invalid controls for a land-spawn rejection.
    land = None
    width = coast["width"]
    radius = width // 2
    for index, walkable in enumerate(coast["player_open"]):
        if walkable == "1" and coast["boat_open"][index] == "0":
            land = (coast["x"] - radius + index % width,
                    coast["z"] + radius - index // width)
            break
    require(land is not None, "Coast collision query contained no walkable land")
    session.checked(f"cheat vesselgoto {land[0]} {land[1]} 0")
    session.checked(f"cheat vesselspawnat {land[0]} {land[1]} 2 5 2 3840 6448")
    captures.append(session.capture(output / "collision-land-rejected.png"))
    # Restore preflights the full vessel roster and serials: it would reject if
    # the invalid spawn had left even an unboarded hull alive.
    session.checked("restore collision_ocean")
    require(pose(vessel(session.checked("state"))) == pose(before),
            "Land spawn changed the original boat or prevented checkpoint restore")

    result = {"ok": True, "session": str(session.directory),
              "provenance": {key: metadata.get(key) for key in
                             ("binary_sha256", "script_sha256", "script_pack",
                              "allow_stale_scripts")},
              "elapsed_ms": round((time.perf_counter() - started) * 1000, 3),
              "ocean": {"x": 3072, "z": 3160, "tiles": 289},
              "before": before, "underway": underway, "stopped": stopped,
              "land_spawn_rejected_at": land, "path": path,
              "captures": captures, "visual_review_required": True}
    (output / "collision-results.json").write_text(json.dumps(result, indent=2) + "\n")
    (output / "collision-ocean-map.json").write_text(json.dumps(ocean, indent=2) + "\n")
    (output / "collision-shore-map.json").write_text(json.dumps(coast, indent=2) + "\n")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=Path("/tmp/sailing-collision-live"))
    parser.add_argument("--output", type=Path, default=ROOT / "docs/sailing_validation")
    parser.add_argument("--start", action="store_true", help="Start/reuse a headless native client; never builds")
    # A worker cannot run the shared build or the shared script pack, so
    # --start has to be able to name a private client and an isolated pack;
    # without these it could only ever measure src/torirs, which is somebody
    # else's binary and usually somebody else's C.
    parser.add_argument("--binary", type=Path, default=ROOT / "src/torirs",
                        help="Client --start launches (default src/torirs)")
    parser.add_argument("--scripts", type=Path,
                        help="Isolated compiled script-pack directory for --start")
    args = parser.parse_args()
    session = Session(args.session)
    with session.locked():
        if args.start:
            argv = ["start", "--headless", "--binary", str(args.binary)]
            if args.scripts:
                argv += ["--scripts", str(args.scripts)]
            start_args = harness_parser().parse_args(argv)
            session.start(start_args)
        require(session.alive(), "Start the native session first or pass --start")
        result = run(session, args.output.resolve())
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except HarnessError as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        raise SystemExit(1)
