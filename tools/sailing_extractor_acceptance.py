#!/usr/bin/env python3
"""Verify extractor charging across a real ocean-to-shore transition."""
import argparse
import json
from pathlib import Path

from sailing_harness import HarnessError, Session, parser as harness_parser


def run(session: Session, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    commands = []

    def request(command):
        result = session.checked(command)
        commands.append({"command": command, "elapsed_ms": result["elapsed_ms"]})
        return result

    def charge(handle):
        return request(f"activity 2 {handle}")["registers"][2]

    request("close")
    initial = request("state")
    boat = initial["sailing"]["vessel"]
    if boat["config"] != 2 or (boat["fine_x"] // 128, boat["fine_z"] // 128) != (3072, 3160):
        raise HarnessError("Use the default fresh skiff fixture at 3072,3160")
    handle = boat["id"]
    request("save extractor_acceptance")
    try:
        request("camera 256 256 1000")
        request("cheat sailutilityfixture 2 12")
        # Actual native model hit on this fixed fixture, followed by normal
        # approach/activation; no charge is supplied by the fixture command.
        request("hover 350 229")
        request("click 350 229 left")
        request("step 30")
        active = request("activity 2")
        assert active["registers"][4] == 1 and active["registers"][2] == 99, active
        session.capture(output / "extractor-grace-active.png")
        request("cheat vesselsail 0 2")
        for _ in range(30):
            request("step 30")
            if request("state")["sailing"]["vessel"]["state"] == 0:
                break
        else:
            raise AssertionError("Boat did not stop against the real coast")
        before = charge(handle)
        request("cheat sailplank")
        ashore = request("state")
        assert not ashore["sailing"]["aboard"], ashore
        assert charge(handle) == before
        samples = []
        for _ in range(22):
            request("step 30")
            samples.append(charge(handle))
        assert samples == [before - min(i, 17) for i in range(1, 23)], samples
        session.capture(output / "extractor-grace-ashore.png")
        request("cheat sailplank")
        assert request("state")["sailing"]["aboard"]
        reboard = charge(handle)
        request("step 30")
        assert charge(handle) == reboard - 1
        # Reboard within the grace window, then depart again before the old
        # callback expires. Exactly one owner may decrement on each tick.
        request("cheat sailplank")
        request("step 90")
        request("cheat sailplank")
        quick = charge(handle)
        request("step 30")
        assert charge(handle) == quick - 1
        request("cheat sailplank")
        again = charge(handle)
        request("step 60")
        assert charge(handle) == again - 2
        request("cheat sailplank")
        session.capture(output / "extractor-grace-returned.png")
        report = {"ok": True, "before_shore": before, "shore_samples": samples,
                  "ashore": ashore, "commands": commands,
                  "provenance": session.metadata(), "visual_review_required": True,
                  "checks": ["natural ocean/coast with real collision", "native mouse activation",
                             "17 charging ticks after disembarking, then freeze",
                             "charging resumes on return", "quick reboarding never double-ticks"]}
    finally:
        state = request("state")
        if not state.get("sailing", {}).get("aboard"):
            request(f"cheat vesselboard {handle}")
        request("restore extractor_acceptance")
    (output / "extractor-grace-results.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=Path("/tmp/sailing-extractor"))
    parser.add_argument("--output", type=Path, default=Path("docs/sailing_validation"))
    parser.add_argument("--start", action="store_true")
    args = parser.parse_args()
    session = Session(args.session)
    with session.locked():
        if args.start:
            session.start(harness_parser().parse_args(["start", "--headless"]))
        result = run(session, args.output.resolve())
    print(json.dumps({"ok": result["ok"], "checks": len(result["checks"])}))


if __name__ == "__main__":
    main()
