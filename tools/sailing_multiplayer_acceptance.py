#!/usr/bin/env python3
"""Observe a real server passenger through the native v5 packet/render pipeline.

The peer has no second external transport. Both players live in the same server;
production PLAYER_INFO creates/moves/removes the passenger on the real client.
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


def actor_count(session: Session, view: int) -> int:
    state = session.checked("wev")
    return next(row["actor_commands"] for row in state["views"] if row["id"] == view)


def run(session: Session, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    session.checked("pause")
    if session.checked("peer").get("present"):
        session.checked("peer remove")
    initial = session.checked("state")
    if initial.get("sailing", {}).get("checkpoints", 0):
        restored = session.request("restore multiplayer_ocean")
        if restored.get("ok"):
            initial = session.checked("state")
    require(initial.get("sailing", {}).get("aboard"), "Start aboard an actual ocean skiff")
    boat = initial["sailing"]["vessel"]
    require(boat["config"] == 2 and not boat["sails_set"], "Fixture needs a stopped native skiff")
    require((boat["fine_x"] // 128, boat["fine_z"] // 128) == (3072, 3160),
            "Fixture must occupy actual surveyed ocean")
    session.checked("camera 1024 256 1200")
    session.checked("save multiplayer_ocean")
    captures = [session.capture(output / "multiplayer-primary.png")]
    base_actors = actor_count(session, boat["view"])
    peer = session.checked("peer create HarnessMate")
    require(peer["present"] and peer["client"]["present"],
            "Real server passenger did not reach the actual client decoder")
    bx, bz = peer["server"]["x"] & ~63, peer["server"]["z"] & ~63
    level = peer["server"]["level"]
    session.checked(f"peer place {level} {bx + 3} {bz + 5}")
    # Placement travels normally, then enough client cycles let its model and
    # deck routing settle without starting the hull.
    session.checked("step 30")
    aboard = session.checked("peer")
    require(aboard["client"]["present"] and aboard["client"]["view"] == boat["view"],
            "Passenger was not routed to the actual hull view")
    captures.append(session.capture(output / "multiplayer-aboard.png"))
    require(actor_count(session, boat["view"]) > base_actors,
            "Published passenger exists in state but is absent from actual draw commands")
    session.checked(f"peer walk {bx + 3} {bz + 3} 1")
    before = session.checked("state")["sailing"]["vessel"]
    session.checked("cheat vesselsail 0 2")
    session.checked("step 30")
    moving = session.checked("peer")
    moved_boat = session.checked("state")["sailing"]["vessel"]
    require((moving["server"]["x"], moving["server"]["z"]) == (bx + 3, bz + 3),
            "Passenger failed to RUN two legal native deck tiles")
    require(moved_boat["fine_z"] == before["fine_z"] - 128,
            "Hull did not move concurrently through actual sea")
    require(moving["client"]["element"] == aboard["client"]["element"],
            "Movement unnecessarily destroyed and recreated the passenger's model")
    require(moving["client"]["route_length"] > 0 and moving["client"]["route_run"] == 1,
            "Two deck steps plus hull displacement snapped instead of queuing RUN")
    session.checked("step 15")
    middle = session.checked("peer")
    captures.append(session.capture(output / "multiplayer-running.png"))
    session.checked("peer remove")
    require(not session.checked("peer")["present"], "Server passenger did not retire")
    session.checked("step 1")
    captures.append(session.capture(output / "multiplayer-removed.png"))
    require(actor_count(session, boat["view"]) == base_actors,
            "Removed passenger remains in actual draw commands")
    session.checked("restore multiplayer_ocean")
    captures.append(session.capture(output / "multiplayer-restored.png"))
    result = {
        "ok": True, "elapsed_ms": round((time.perf_counter() - started) * 1000, 3),
        "session": session.metadata(), "before": initial, "aboard": aboard,
        "running": moving, "mid_run": middle, "captures": captures,
        "scope": "two actual server players, production v5 packets, one native rendering client",
    }
    (output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=Path("/tmp/sailing-multiplayer-live"))
    parser.add_argument("--output", type=Path, default=ROOT / "docs/sailing_validation/multiplayer")
    parser.add_argument("--start", action="store_true")
    parser.add_argument("--headless", action="store_true")
    args = parser.parse_args()
    session = Session(args.session)
    with session.locked():
        if args.start and not session.alive():
            launch = ["--session", str(args.session), "start", "--user", "peerproof"]
            if args.headless:
                launch.append("--headless")
            session.start(harness_parser().parse_args(launch))
        require(session.alive(), "Start the native sailing harness first or pass --start")
        result = run(session, args.output)
        print(json.dumps({"ok": result["ok"], "elapsed_ms": result["elapsed_ms"],
                          "results": str(args.output / "results.json")}, indent=2))


if __name__ == "__main__":
    main()
