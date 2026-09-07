#!/usr/bin/env python3
"""Exercise real native cargo widgets in an existing, isolated warm fixture.

Start tools/sailing_harness.py --session /tmp/sailing-ui start --headless first.
The checkpoint is restored even when a check fails. No client restart or
server tick is required for the actual widget interactions.

Every check below raises rather than asserting: `python3 -O` strips `assert`,
and an acceptance tool that reports ok:true because its statements were
compiled out is worse than no tool at all.
"""
import argparse
import json
from pathlib import Path

from sailing_harness import HarnessError, ROOT, Session


def require(condition, message: str) -> None:
    if not condition:
        raise HarnessError(message)


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
        require(widget["exists"] and not widget["hidden"], f"widget missing or hidden: {(name, widget)}")
        require(widget["w"] > 0 and widget["h"] > 0, f"widget has no area: {(name, widget)}")
        request(f"click {widget['x'] + widget['w'] // 2} {widget['y'] + widget['h'] // 2} left")

    def count(container, item):
        return sum(row[2] for row in container["server"] if row[1] == item)

    request("close")
    before = request("state")
    require(before["sailing"]["aboard"], f"player is not aboard: {before}")
    initial_backpack = request("inventory 93")["server"]
    request("save cargo_acceptance")
    try:
        request("cheat sailverify")
        # The server acceptance routine leaves precisely thirteen bronze cannonballs in the
        # captain's hold and an empty backpack after testing capacity/repairs.
        require(request("inventory 93")["server"] == [], "backpack is not empty after sailverify")
        request("cheat sailcargo")
        hold_id = request("varp 5204")["server"]
        require(963 <= hold_id <= 967, f"hold inventory id out of range: {hold_id}")
        hold = request(f"inventory {hold_id}")
        require(count(hold, 31906) == 13, f"hold does not hold thirteen bronze cannonballs: {hold}")
        require(hold["other"] == hold["server"], f"native namespace disagrees with captain hold: {hold}")
        if request("varbit sailing_boat_cargohold_warning_dismissed")["client"] == 0:
            click_widget("sailing_boat_cargohold_side:dismiss")
            dismissed = request("varbit sailing_boat_cargohold_warning_dismissed")
            require(dismissed["client"] == dismissed["server"] == 1, f"dismissal did not transmit: {dismissed}")

        click_widget("sailing_boat_cargohold:all")
        click_widget("sailing_boat_cargohold:items", 0)
        require(count(request(f"inventory {hold_id}"), 31906) == 0, "All did not empty the hold")
        require(count(request("inventory 93"), 31906) == 13, "All did not deliver thirteen cannonballs")

        click_widget("sailing_boat_cargohold:quantity_10")
        click_widget("sailing_boat_cargohold_side:items", 0)
        hold = request(f"inventory {hold_id}")
        require(count(hold, 31906) == 10, f"quantity 10 did not deposit ten cannonballs: {hold}")
        require(hold["other"] == hold["server"], f"native namespace disagrees with captain hold: {hold}")
        require(count(request("inventory 93"), 31906) == 3, "backpack does not retain the remaining three")
        session.capture(output / "native-cargo-roundtrip.png")
        after = request("state")
        require(after["server_tick"] == before["server_tick"], f"server time advanced: {(before, after)}")
        request("close")
        request("cheat sailcargopolicy")
        # Slots 3/4 are eligible cannonballs/kits, 5 is a shared tool. Coins,
        # logs, noted kits and a quest-locked crowbar are never depositable.
        mask = request("varp 5205")
        require(mask["server"] == mask["client"] == 56, f"highlight bitmask is not 56: {mask}")
        seeded = request("inventory 93")["server"]
        request("button sailing_boat_cargohold_side:items 0 6")
        request("button sailing_boat_cargohold_side:items 2 6")
        require(request("inventory 93")["server"] == seeded, "forged item buttons moved a prohibited item")
        click_widget("sailing_boat_cargohold:depositall_inventory")
        backpack = request("inventory 93")
        hold = request(f"inventory {hold_id}")
        for item, amount in ((995, 20), (1511, 1), (31965, 10), (31807, 1)):
            require(count(backpack, item) == amount, f"prohibited item {item} left the backpack: {backpack}")
            require(count(hold, item) == 0, f"prohibited item {item} entered the hold: {hold}")
        require(count(hold, 31906) == 13 and count(hold, 31964) == 1,
                f"permitted items did not deposit: {hold}")
        require(count(backpack, 31986) == count(hold, 31986) == 0,
                "shared tool is neither in the backpack nor consuming hold capacity")
        require(sum(row[2] for row in hold["server"]) == 14, f"hold total is not fourteen: {hold}")
        require(request("varp 5205")["client"] == 0, "highlight bitmask did not clear")
        click_widget("sailing_boat_cargohold:tools", 0)
        require(count(request("inventory 93"), 31986) == 1, "native Tools did not retrieve the shared tool")
        require(request(f"inventory {hold_id}")["server"] == hold["server"],
                "retrieving the shared tool changed the hold")
        session.capture(output / "native-cargo-whitelist.png")
        require(request("state")["server_tick"] == before["server_tick"],
                "server time advanced during the policy checks")
        report = {"ok": True, "provenance": {key: metadata.get(key) for key in
                  ("binary_sha256", "script_sha256", "script_pack", "allow_stale_scripts")}, "checks": [
            "native whitelist highlights only permitted unnoted items and retrievable tools",
            "forged item buttons cannot deposit coins or noted repair kits",
            "Deposit inventory leaves prohibited items and quest-locked tools untouched",
            "shared tool storage consumes no hold capacity and native Tools retrieves it",

            "native inventory namespace matches captain inventory",
            "dismissal transmits and removes the blocking warning",
            "native All button and item click withdraw all thirteen bronze cannonballs",
            "native 10 button and inventory click deposit exactly ten bronze cannonballs",
            "inventory and hold conserve the total item count",
            "all interactions finish while server time remains paused",
        ], "timings": timings}
    finally:
        request("close")
        request("restore cargo_acceptance")
        require(request("inventory 93")["server"] == initial_backpack,
                "restore did not return the original backpack")
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=Path("/tmp/sailing-ui"))
    parser.add_argument("--output", type=Path, default=ROOT / "docs/sailing_validation/cargo")
    args = parser.parse_args()
    session = Session(args.session)
    output = args.output.resolve()
    with session.locked():
        result = run(session, output)
    print(json.dumps({"ok": result["ok"], "checks": len(result["checks"]), "output": str(output)}))


if __name__ == "__main__":
    main()
