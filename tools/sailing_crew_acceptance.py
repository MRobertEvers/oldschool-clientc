#!/usr/bin/env python3
"""Exercise native crew assignments and real work in one paused ocean fixture."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from sailing_harness import HarnessError, ROOT, Session, parser as harness_parser


def run(session: Session, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    report = {"ok": False, "events": [], "captures": [],
              "provenance": session.metadata(), "visual_review_required": True}

    def call(command):
        response = session.checked(command)
        report["events"].append({"command": command, "response": response})
        return response

    def require(condition, message):
        if not condition:
            raise HarnessError(message)

    def ticks(count):
        call(f"step {count * 30}")

    def duty(position):
        call(f"cheat sailcrewduty 0 {position}")

    def activity(slot=4):
        return call(f"activity {slot}")

    def inventory(native_id):
        return call(f"inventory {native_id}")["server"]

    def amount(native_id, obj=None):
        return sum(count for _, item, count in inventory(native_id)
                   if obj is None or item == obj)

    def shot(name):
        call("hover 806 502")
        report["captures"].append(session.capture(output / f"crew-{name}.png"))

    def reset():
        duty(0)
        call("restore crew_acceptance")
        call("cheat sailactivitycleanup")

    try:
        state = call("state")
        vessel = state["sailing"]["vessel"]
        require(vessel["config"] == 2 and state["sailing"]["aboard"],
                "Use the native skiff ocean fixture")
        require((vessel["fine_x"] // 128, vessel["fine_z"] // 128) == (3072, 3160),
                "Crew acceptance must begin in the actual surveyed ocean")
        call("pause")
        call("cheat setlevel fishing 99")
        call("cheat setlevel ranged 99")
        call("cheat sailrecruit")
        # Actual native938 Assign button, not a synthetic IF_BUTTON request.
        call("click 255 225 left")
        require(call("varbit sailing_crew_slot_1")["server"] == 1,
                "The visible native Assign button did not recruit Jobless Jim")
        call("close")
        duty(0)
        call("camera 512 340 1100")
        ticks(1)
        call("save crew_acceptance")

        call("cheat sailsalvagefixture 4")
        before = activity()
        duty(10)
        ticks(50)
        gathered = activity()
        require(gathered["registers"][0] == 1 and gathered["registers"][5] == 0,
                "Assigned crewmate never operated the hook")
        products = amount(963, 32847)
        require(products > 0 and gathered["sailing_xp_tenths"] - before["sailing_xp_tenths"] == products * 30,
                "Crew salvage did not conserve products and native D3 rewards")
        require(call("state")["sailing"]["player"]["navigating"] == 1,
                "Crew work removed the captain from the helm")
        duration = gathered["registers"][4]
        ticks(5)
        require(activity()["registers"][4] == duration - 5,
                "Hook gathering did not run on an actual five-tick cycle")
        shot("salvaging")
        duty(0)
        stopped = activity()
        ticks(10)
        require(activity()["registers"][0] == 0 and
                activity()["sailing_xp_tenths"] == stopped["sailing_xp_tenths"],
                "Unassigned crewmate continued salvaging")

        reset()
        call("cheat sailnetfixture 4")
        call("cheat sailshoal")
        before = activity()
        duty(10)
        ticks(25)
        after = activity()
        require(after["registers"][0] == 1 and amount(968) > 0,
                "Crew trawling did not retain real fish")
        require(after["sailing_xp_tenths"] > before["sailing_xp_tenths"] and
                after["fishing_xp_tenths"] == before["fishing_xp_tenths"],
                "Crew trawling awarded the wrong skill experience")
        shot("trawling")

        reset()
        call("cheat sailcrewrepairfixture")
        require(call("state")["sailing"]["vessel"]["hp"] == vessel["hp_max"] - 10,
                "Repair fixture did not create real hull damage")
        duty(5)
        ticks(15)
        require(call("state")["sailing"]["vessel"]["hp"] == vessel["hp_max"] and
                amount(93, 31964) == 0, "Crew repair failed to consume kits and heal the hull")
        shot("repairing")

        reset()
        call("cheat sailcannonfixture 1")
        before = activity(1)
        duty(7)
        for _ in range(30):
            ticks(1)
            after = activity(1)
            if after["sailing_xp_tenths"] > before["sailing_xp_tenths"]:
                break
        require(after["registers"][1] < 12 and after["registers"][5] == 0 and
                after["sailing_xp_tenths"] > before["sailing_xp_tenths"] and
                after["ranged_xp_tenths"] == before["ranged_xp_tenths"],
                "Crew cannon did not spend ammunition and award Sailing-only damage XP")
        shot("cannon")

        reset()
        duty(4)
        ticks(5)
        before = activity()
        # A gentle circle stays inside the surveyed17x17 open ocean.
        for heading in range(80):
            call(f"cheat vesselsail {heading % 16} 1")
            ticks(1)
            after = activity()
            if after["wind"][2] > 0:
                break
        require(after["wind"][2] > 0 and
                after["sailing_xp_tenths"] - before["sailing_xp_tenths"] == 52,
                "Crew helm did not automatically trim for native H2 reward")
        shot("trimming")

        reset()
        duty(4)
        ticks(5)
        call("cheat helm")
        ticks(1)
        call("camera 512 340 900")
        state = call("state")
        require(state["sailing"]["player"]["navigating"] == 0,
                "Captain remains physically locked to the helm")
        old_position = (state["server_x"], state["server_z"])
        call("click 343 265 left")
        ticks(2)
        state = call("state")
        require((state["server_x"], state["server_z"]) != old_position and
                state["sailing"]["vessel"]["heading"] == 0,
                "Deck clicking under crew navigation did not walk independently")
        call("click 100 100 left")
        ticks(1)
        state = call("state")
        require(state["sailing"]["vessel"]["heading"] != 0 and
                state["sailing"]["player"]["navigating"] == 0,
                "Water clicking under crew navigation did not steer")
        shot("helm-walking")
        report["ok"] = True
    finally:
        report["elapsed_ms"] = round((time.perf_counter() - started) * 1000, 3)
        (output / "crew-actions.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=Path("/tmp/sailing-crew-acceptance"))
    parser.add_argument("--output", type=Path, default=ROOT / "docs/sailing_validation")
    parser.add_argument("--start", action="store_true")
    args = parser.parse_args()
    session = Session(args.session)
    with session.locked():
        if args.start:
            session.start(harness_parser().parse_args(["start", "--headless", "--user", "crewcheck"]))
        if not session.alive():
            raise HarnessError("Start a fresh skiff session or use --start")
        report = run(session, args.output.resolve())
    print(json.dumps({"ok": report["ok"], "elapsed_ms": report["elapsed_ms"],
                      "captures": report["captures"]}))


if __name__ == "__main__":
    try:
        main()
    except HarnessError as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        raise SystemExit(1)
