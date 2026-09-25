#!/usr/bin/env python3
"""Check real deck models, animation timing and visual checkpoint restoration."""
import argparse
import json
from pathlib import Path
import time

from sailing_harness import HarnessError, ROOT, Session


def run(session: Session, output: Path) -> dict:
    started = time.perf_counter()
    output.mkdir(parents=True, exist_ok=True)
    events = []

    def call(command):
        result = session.checked(command)
        events.append({"command": command, "response": result})
        return result

    def require(condition, message):
        if not condition:
            raise HarnessError(message)

    state = call("state")
    require(state["sailing"]["vessel"]["config"] == 2, "Use the native skiff fixture")
    # A skiff occupies one native 8x8 deck zone; these offsets come from its
    # actual DB rows, not from its projected position in the root ocean.
    bx = state["sailing"]["player"]["x"] & ~7
    bz = state["sailing"]["player"]["z"] & ~7

    def models(dx, dz):
        return call(f"scenery {bx + dx} {bz + dz}")["items"]

    def cloth():
        items = [item for item in models(4, 5) if item["loc"] == 29516]
        require(len(items) == 1 and items[0]["rig"]["vertices"] > 0,
                "The starter boat is missing its actual linen sailcloth mesh")
        return items[0]

    def shot(name):
        return session.capture(output / f"deck-{name}.png")

    call("pause")
    call("close")
    call("camera 256 256 1000")
    call("save deck_acceptance")
    captures = []
    report = {"ok": False, "events": events, "captures": captures,
              "provenance": session.metadata(), "visual_review_required": True}
    try:
        captures.append(shot("baseline"))
        before = cloth()
        require(before["seq"] == 13884, "Stationary cloth is not using its native down animation")
        # Save after a completed render, so the expected mesh and frame agree.
        call("save deck_acceptance")
        call("cheat vesselsail 0 2")
        call("step 30")
        captures.append(shot("sailing"))
        raised = cloth()
        require(raised["seq"] == 13890, "Raised cloth did not receive its native full animation")
        call("step 5")
        captures.append(shot("sailing-next"))
        advanced = cloth()
        require((advanced["frame"] - raised["frame"]) % 90 == 5,
                "Skeletal sail animation did not advance once per client cycle")
        require(advanced["rig"]["hash"] != raised["rig"]["hash"],
                "Sail animation counters advanced without changing the rendered cloth")
        call("step 185")
        captures.append(shot("sailing-looped"))
        looped = cloth()
        require(looped["seq"] == 13890 and
                (looped["frame"] - advanced["frame"]) % 90 == 185 % 90,
                "The native looping sail terminated or drifted after a complete cycle")
        call("restore deck_acceptance")
        captures.append(shot("restored"))
        restored = cloth()
        for key in ("seq", "frame", "cycle"):
            require(restored[key] == before[key], f"Restore changed cloth {key}")
        require(restored["rig"]["hash"] == before["rig"]["hash"],
                "Restored cloth geometry differs from the saved rendered mesh")
        initial_hotspot = [item["loc"] for item in models(3, 2)]
        call("cheat sailnetfixture 4")
        captures.append(shot("net-installed"))
        require([item["loc"] for item in models(3, 2)] != initial_hotspot,
                "Installing a real net did not change the rendered deck hotspot")
        call("restore deck_acceptance")
        captures.append(shot("net-restored"))
        require([item["loc"] for item in models(3, 2)] == initial_hotspot,
                "Checkpoint left a removed facility model on the boat deck")
        report["ok"] = True
        report["elapsed_ms"] = round((time.perf_counter() - started) * 1000, 3)
        return report
    finally:
        (output / "deck-results.json").write_text(json.dumps(report, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=Path("/tmp/sailing-root-final"))
    parser.add_argument("--output", type=Path, default=ROOT / "docs/sailing_validation")
    args = parser.parse_args()
    session = Session(args.session)
    with session.locked():
        result = run(session, args.output.resolve())
    print(json.dumps({"ok": result["ok"], "elapsed_ms": result["elapsed_ms"]}))


if __name__ == "__main__":
    main()
