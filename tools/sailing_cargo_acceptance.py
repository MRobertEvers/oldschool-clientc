#!/usr/bin/env python3
"""Exercise real native cargo widgets in an existing, isolated warm fixture.

Start tools/sailing_harness.py --session /tmp/sailing-ui start --headless first.
The checkpoint is restored even when an assertion fails. No client restart or
server tick is required for the actual widget interactions.
"""
import argparse
import json
from pathlib import Path

from sailing_harness import HarnessError, Session


def run(session: Session, output: Path) -> dict:
    metadata = session.metadata()
    if metadata.get("user") != "sailingtest" or not metadata.get("headless"):
        raise HarnessError("This test requires an isolated headless sailingtest fixture")
    output.mkdir(parents=True, exist_ok=True)
    timings = []

    def request(command):
        result = session.checked(command)
        timings.append({"command": command, "elapsed_ms": result["elapsed_ms"]})
        return result

    def click_widget(name, sub=-1):
        widget = request(f"widget {name} {sub}")
        assert widget["exists"] and not widget["hidden"], (name, widget)
        assert widget["w"] > 0 and widget["h"] > 0, (name, widget)
        request(f"click {widget['x'] + widget['w'] // 2} {widget['y'] + widget['h'] // 2} left")

    def count(container, item):
        return sum(row[2] for row in container["server"] if row[1] == item)

    request("close")
    before = request("state")
    assert before["sailing"]["aboard"], before
    initial_backpack = request("inventory 93")["server"]
    request("save cargo_acceptance")
    try:
        request("cheat sailverify")
        # The server acceptance routine leaves precisely thirteen coins in the
        # captain's hold and an empty backpack after testing capacity/repairs.
        assert request("inventory 93")["server"] == []
        request("cheat sailcargo")
        hold_id = request("varp 5204")["server"]
        assert 963 <= hold_id <= 967, hold_id
        hold = request(f"inventory {hold_id}")
        assert count(hold, 995) == 13, hold
        assert hold["other"] == hold["server"], hold
        if request("varbit sailing_boat_cargohold_warning_dismissed")["client"] == 0:
            click_widget("sailing_boat_cargohold_side:dismiss")
            dismissed = request("varbit sailing_boat_cargohold_warning_dismissed")
            assert dismissed["client"] == dismissed["server"] == 1, dismissed

        click_widget("sailing_boat_cargohold:all")
        click_widget("sailing_boat_cargohold:items", 0)
        assert count(request(f"inventory {hold_id}"), 995) == 0
        assert count(request("inventory 93"), 995) == 13

        click_widget("sailing_boat_cargohold:quantity_10")
        click_widget("sailing_boat_cargohold_side:items", 0)
        hold = request(f"inventory {hold_id}")
        assert count(hold, 995) == 10, hold
        assert hold["other"] == hold["server"], hold
        assert count(request("inventory 93"), 995) == 3
        session.capture(output / "native-cargo-roundtrip.png")
        after = request("state")
        assert after["server_tick"] == before["server_tick"], (before, after)
        report = {"ok": True, "checks": [
            "native inventory namespace matches captain inventory",
            "dismissal transmits and removes the blocking warning",
            "native All button and item click withdraw all thirteen coins",
            "native 10 button and inventory click deposit exactly ten coins",
            "inventory and hold conserve the total item count",
            "all interactions finish while server time remains paused",
        ], "timings": timings}
    finally:
        request("close")
        request("restore cargo_acceptance")
        assert request("inventory 93")["server"] == initial_backpack
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=Path("/tmp/sailing-ui"))
    parser.add_argument("--output", type=Path, default=Path("docs/sailing_validation/cargo"))
    args = parser.parse_args()
    session = Session(args.session)
    with session.locked():
        result = run(session, args.output)
    print(json.dumps({"ok": result["ok"], "checks": len(result["checks"]), "output": str(args.output)}))


if __name__ == "__main__":
    main()
