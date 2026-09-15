#!/usr/bin/env python3
"""Native three-boat client acceptance; uses one warm actual-ocean session.

No build or timers are hidden here. Each command and rendered capture is
recorded with the loaded executable/script provenance for visual review.
"""
import argparse
import json
from pathlib import Path
import time
from sailing_harness import HarnessError, Session


def run(session, output):
    output.mkdir(parents=True, exist_ok=True)
    report = {"ok": False, "provenance": session.metadata(), "events": [],
              "captures": [], "visual_review_required": True}
    def call(command):
        result = session.checked(command)
        report["events"].append({"command": command, "response": result})
        return result
    def require(condition, message):
        if not condition: raise HarnessError(message)
    def state():
        return {v["id"]: v for v in call("wev")["views"]}
    def capture(name):
        call("hover 806 502")
        result = session.capture(output / f"client-{name}.png")
        result["name"] = name
        report["captures"].append(result)
        return state()
    started = time.perf_counter()
    try:
        require(len(state()) == 1, "Start a fresh skiff fixture for three-boat acceptance")
        call("step 30")
        call("cheat vesselgoto 3072 3160 0")
        call("step 30")
        call("cheat vesselspawnat 3077 3160 2 5 2 3840 6448 0 1")
        call("step 30")
        call("cheat vesselspawnat 3078 3161 2 5 2 3840 6448 256 1")
        call("step 30")
        call("cheat vesselboard 1")
        call("cheat helm")
        call("step 30")
        call("camera 1024 320 1200")
        boats = capture("three-overlap")
        require(len(boats) == 3, "Three native WEV rebuilds must be visible in the ocean")
        require(boats[1]["visible"] and not boats[1]["flat"] and boats[1]["actor_commands"] > 0,
                "Aboard boat must remain full and contain the actual player")
        require(not boats[2]["flat"] and boats[3]["flat"], "Earlier group1 hull must win overlap")
        require(boats[2]["model_commands"] >= 4 and boats[3]["model_commands"] >= 4,
                "Other visible boats must receive mast, helm and cargo scenery")
        require(boats[3]["actor_commands"] == boats[3]["picked"] == 0,
                "Flat view must contain no actors or picks")
        # Change native priority ownership without rebuilding any cache scene.
        call("wevgroup 3 2")
        boats = capture("priority-two")
        require(boats[3]["visible"] and not boats[3]["flat"] and boats[2]["flat"],
                "Group2 must stamp before group1 and win the same overlap")
        call("wevgroup 2 0")
        call("wevgroup 3 0")
        call("wevlimit 1")
        boats = capture("budget-one")
        require(boats[1]["visible"] and boats[2]["visible"] and not boats[3]["visible"],
                "Aboard is uncapped, and group0 gets its own one-boat allowance")
        require(boats[3]["markers"] == boats[3]["model_commands"] == 0,
                "Over-budget boat is omitted from the native painter stream")
        call("wevlimit 0")
        boats = capture("budget-zero")
        require(boats[1]["visible"] and not boats[1]["flat"] and boats[1]["actor_commands"] > 0,
                "Zero budget still renders the player aboard")
        require(all(not boats[i]["visible"] and boats[i]["markers"] == 0 for i in (2, 3)),
                "Zero budget omits every non-aboard view")
        call("wevlimit 30")
        boats = capture("full-restored")
        require(all(b["visible"] and not b["flat"] for b in boats.values()),
                "Changing back to the native default restores full boats on the next frame")
        require(all(b["model_commands"] >= 4 for b in boats.values()),
                "Every restored hull retains its live facilities")
        # A second real server player exercises the actual player-info codec,
        # deck membership and render suppression without injecting client actors.
        call("peer create HarnessMate")
        call("step 30")
        peer = call("peer")
        require(peer["client"]["present"] and peer["client"]["view"] == 1,
                "The real peer must decode onto the captain's deck")
        call("peer board 2")
        call("step 30")
        call("wevlimit 1")
        peer = call("peer")
        require(peer["client"]["present"] and peer["client"]["home_view"] == 2,
                "Remote boarding must update native wire membership")
        call("peer walk 6403 132 0")
        call("step 90")
        peer = call("peer")
        require(peer["server"]["x"] == 6403 and peer["server"]["z"] == 132,
                "The server peer must walk the real deck collision map")
        require(peer["client"]["view"] == 2 and
                peer["client"]["fine_x"] == 3 * 128 + 64 and
                peer["client"]["fine_z"] == 4 * 128 + 64,
                "Remote walking must decode to the same deck-local fine position")
        boats = capture("peer-visible")
        require(boats[2]["actor_commands"] > 0, "Full peer deck must emit its player model")
        call("wevgroup 3 2")
        call("wevgroup 2 1")
        call("wevlimit 30")
        boats = capture("peer-flat")
        require(boats[2]["flat"] and boats[2]["actor_commands"] == 0,
                "Flattening a populated peer deck must omit the actor")
        require(call("peer")["client"]["present"],
                "Flat suppression must retain the actual player-info record")
        call("wevgroup 2 0")
        call("wevgroup 3 1")
        boats = capture("peer-restored")
        require(not boats[2]["flat"] and boats[2]["actor_commands"] > 0,
                "The retained peer must render again immediately when full")
        call("peer remove")
        call("step 30")
        require(not call("peer")["present"], "Peer removal must clean up the server fixture")
        call("wevgroup 3 0")
        # Real collision stops the bow beside a raised island. The painter
        # dependency must retain the cliff's higher terrain in its own order.
        call("save client_coast")
        call("wevlimit 0")
        call("cheat vesselsail 15 3 1")
        call("step 600")
        coast = call("state")["sailing"]["vessel"]
        require(coast["state"] == 0 and coast["hp"] < coast["hp_max"],
                "The real boat collision map must stop the hull beside the coast")
        call("camera 1024 320 1200")
        capture("raised-coast")
        call("restore client_coast")
        call("wevlimit 30")
        require(call("state")["sailing"]["vessel"]["hp"] == 80,
                "Checkpoint restore must return the actual ocean fixture after coast proof")
        report["ok"] = True
    finally:
        report["elapsed_ms"] = round((time.perf_counter() - started) * 1000, 3)
        (output / "client-results.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("docs/sailing_validation/client"))
    args = parser.parse_args()
    session = Session(args.session)
    with session.locked():
        result = run(session, args.output.resolve())
    print(json.dumps({"ok": result["ok"], "captures": len(result["captures"]),
                      "elapsed_ms": result["elapsed_ms"]}))


if __name__ == "__main__":
    main()
