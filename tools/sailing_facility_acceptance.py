#!/usr/bin/env python3
"""Verify real sailing activities in one warm native skiff session.

The fixtures install cache facilities and place real resources/NPCs. They do
not award activity XP, catch fish, damage targets, or supply wind motes. Images
must be inspected separately after the executable assertions pass.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import time
from sailing_harness import HarnessError, ROOT, Session, parser as harness_parser


def require(ok: bool, message: str):
    if not ok:
        raise HarnessError(message)


def run(s: Session, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    result = {"ok": False, "session": str(s.directory), "events": [], "captures": [],
              "visual_review_required": True}
    started = time.perf_counter()

    def call(command):
        data = s.checked(command)
        result["events"].append({"command": command, "response": data})
        return data

    def shot(name):
        result["captures"].append(s.capture(output / f"activity-{name}.png"))

    def activity(slot):
        return call(f"activity {slot}")

    def ticks(count):
        call(f"step {30 * count}")

    def inventory(inv):
        return call(f"inventory {inv}")["server"]

    def amount(entries):
        return sum(item[2] for item in entries)

    def contents(*inventories):
        totals = {}
        for inv in inventories:
            for _, item, count in inventory(inv):
                totals[item] = totals.get(item, 0) + count
        return totals

    def delta(after, before):
        return {item: after.get(item, 0) - before.get(item, 0)
                for item in after.keys() | before.keys()
                if after.get(item, 0) != before.get(item, 0)}

    try:
        call("pause")
        state = call("state")
        if state.get("sailing", {}).get("checkpoints", 0):
            call("restore activity_ocean")
            state = call("state")
        require(state.get("sailing", {}).get("aboard"), "Board the ocean fixture first")
        require(state["sailing"]["vessel"]["config"] == 2, "This acceptance uses native skiff hotspot positions")
        require((state["sailing"]["vessel"]["fine_x"] // 128, state["sailing"]["vessel"]["fine_z"] // 128) == (3072, 3160),
                "Activity fixture must start in the surveyed actual ocean")
        call("camera 1152 300 1600")
        # Player-operated activities have their own acceptance; retain the
        # roster but clear inherited duties so crew cannot consume fixtures.
        for crew_slot in range(5):
            call(f"cheat sailcrewduty {crew_slot} 0")
        ticks(1)
        call("cheat setlevel fishing 99")
        call("cheat setlevel ranged 99")
        call("cheat sailactivitycleanup")
        ticks(1)
        call("save activity_ocean")
        shot("baseline")

        base_tier = call("varbit sailing_sidepanel_boat_basespeed")["server"] // 64
        cap_tier = call("varbit sailing_sidepanel_boat_speedcap")["server"] // 64
        call("cheat sailwindfixture 0")
        call("cheat sailwind 0")
        wind_before = activity(0)
        require(wind_before["wind"][0] == 0, "Wind fixture supplied an unearned mote")
        require(wind_before["wind"][1] == 1, "Wind catcher did not activate")
        # A gentle circle stays inside the independently surveyed17x17 sea.
        # A straight49-tick course reaches nearby islands before the gust.
        for heading in range(65):
            call(f"cheat vesselsail {heading % 16} 1")
            ticks(1)
            gust = activity(0)
            if gust["wind"][6] > 0:
                break
        require(gust["wind"][6] > 0, "Sailing never produced a natural trim window")
        require(call("state")["sailing"]["vessel"]["state"] == 1, "Gust test ran aground before trimming")
        shot("wind-gust")
        call("cheat sailtrim")
        caught = activity(0)
        require(caught["wind"][0] == 1, "Real trimming did not store a wind mote")
        require(caught["sailing_xp_tenths"] > wind_before["sailing_xp_tenths"], "Trimming awarded no Sailing XP")
        # Bank another real gust, then verify successive motes add one tier.
        for _ in range(65):
            heading += 1
            call(f"cheat vesselsail {heading % 16} 1")
            ticks(1)
            if activity(0)["wind"][6] > 0:
                break
        call("cheat sailtrim")
        caught = activity(0)
        require(caught["wind"][0] == 2, "Second real trim did not retain a second mote")
        call("cheat sailwind 0")
        released = activity(0)
        require(released["wind"][0] == 1 and released["wind"][2] > 0, "Release did not consume the mote and start a boost")
        require(released["sailing_xp_tenths"] - caught["sailing_xp_tenths"] == 1500, "Mote release XP differs from150")
        require(call("state")["sailing"]["vessel"]["speed_tier"] == min(cap_tier, base_tier + 1),
                "First mote skipped its half-tile speed increment")
        call("cheat sailwind 0")
        released = activity(0)
        require(released["wind"][0] == 0, "Second release failed to consume its stored mote")
        require(call("state")["sailing"]["vessel"]["speed_tier"] == min(cap_tier, base_tier + 2),
                "Successive motes failed to stack half-tile speed increments")
        shot("wind-released")
        for remaining in range(released["wind"][2]):
            call(f"cheat vesselsail {(heading + remaining + 1) % 16} 2")
            ticks(1)
        require(activity(0)["wind"][2] == 0, "Wind boost failed to expire")
        require(call("state")["sailing"]["vessel"]["speed_tier"] == base_tier, "Expired boost failed to restore the native hull base speed")
        call("restore activity_ocean")

        call("cheat sailsalvagefixture 4")
        hook_before = activity(4)
        hook_storage_before = contents(93, 963)
        call("cheat sailsalvage 4")
        ticks(3)
        deployed = activity(4)
        require(deployed["registers"][0] == 1, "Hook did not deploy into the nearby native wreck")
        shot("hook-deployed")
        ticks(50)
        gathered = activity(4)
        require(gathered["sailing_xp_tenths"] > hook_before["sailing_xp_tenths"], "Hook gathered no salvage XP")
        hook_products = delta(contents(93, 963), hook_storage_before)
        require(len(hook_products) == 1 and min(hook_products.values()) > 0, "Hook did not retain a new salvage product")
        require(sum(hook_products.values()) * 100 == gathered["sailing_xp_tenths"] - hook_before["sailing_xp_tenths"],
                "Small-wreck salvage quantity differs from its earned10XP per product")
        call("cheat sailsalvage 4")
        stopped = activity(4)
        require(stopped["registers"][0] == 0, "Hook could not be stopped")
        ticks(8)
        require(activity(4)["sailing_xp_tenths"] == stopped["sailing_xp_tenths"], "Stopped hook kept gathering")
        shot("hook-stopped")
        call("restore activity_ocean")

        call("cheat sailnetfixture 4")
        net_before = activity(4)
        net_inventory_before = inventory(968)
        net_contents_before = contents(968)
        call("cheat sailnet 4")
        ticks(12)
        require(inventory(968) == net_inventory_before, "Net invented fish without a shoal")
        require(activity(4)["fishing_xp_tenths"] == net_before["fishing_xp_tenths"], "Net awarded Fishing XP without a shoal")
        call("cheat sailshoal")
        ticks(1)
        require(call("npc sailing_shoal_ripples")["count"] > 0, "Shoal never reached the actual client NPC pool")
        shot("net-shoal")
        ticks(36)
        fishing = activity(4)
        caught_fish = inventory(968)
        caught_contents = contents(968)
        require(sum(delta(caught_contents, net_contents_before).values()) > 0, "Operating net caught nothing new from the native shoal")
        require(fishing["fishing_xp_tenths"] > net_before["fishing_xp_tenths"], "Caught fish awarded no Fishing XP")
        require(fishing["sailing_xp_tenths"] > net_before["sailing_xp_tenths"], "Working a shoal awarded no Sailing XP")
        call("cheat sailnetraise 4")
        raised = activity(4)
        require(raised["registers"][0] == 0 and raised["registers"][2] == 0, "Raising the net failed to stop fishing")
        ticks(6)
        require(inventory(968) == caught_fish, "Raised net kept fishing")
        fish_storage_before = contents(93, 963)
        call("cheat sailnetcollect 4")
        require(amount(inventory(968)) == 0, "Collect left caught fish in the net")
        require(delta(contents(93, 963), fish_storage_before) == caught_contents, "Collect did not conserve every retained fish by type")
        shot("net-collected")
        call("restore activity_ocean")

        ammo_before = {entry[1]: entry[2] for entry in inventory(93)}
        call("cheat sailcannonfixture 1")
        ticks(1)  # Admit the real NPC to zones and NPC_INFO before operating.
        ammo_after = {entry[1]: entry[2] for entry in inventory(93)}
        added = [item for item, count in ammo_after.items() if count - ammo_before.get(item, 0) == 12]
        require(len(added) == 1, "Cannon fixture did not supply exactly12 real compatible rounds")
        loaded_rounds = ammo_after[added[0]]
        cannon_before = activity(1)
        call("cheat sailcannon 1")
        for _ in range(6):
            ticks(1)
            first = activity(1)
            if first["registers"][4] > 0 and first["registers"][1] < loaded_rounds:
                break
        call("step 8")
        require(0 < first["registers"][1] < loaded_rounds, "Cannon did not consume real loaded ammunition")
        require(first["registers"][4] > 0, "Cannon acquired no real NPC target")
        require(call("npc sailing_bull_shark")["count"] > 0, "Cannon target never reached the actual client NPC pool")
        shot("cannon-firing")
        ticks(40)
        damaged = activity(1)
        require(damaged["ranged_xp_tenths"] > cannon_before["ranged_xp_tenths"], "Cannon inflicted no XP-earning damage")
        if first["target_hp"] > 0 and damaged["target_hp"] >= 0 and first["registers"][4] == damaged["registers"][4]:
            require(damaged["ranged_xp_tenths"] - first["ranged_xp_tenths"] == (first["target_hp"] - damaged["target_hp"]) * 20,
                    "Cannon damage did not award2 Ranged XP per actual hitpoint")
        require(damaged["registers"][1] <= first["registers"][1], "Cannon firing created ammunition")
        require(damaged["registers"][1] < first["registers"][1] or damaged["target_hp"] <= 0,
                "Cannon stopped spending ammunition while its target remained alive")
        ammo_pre_unload = contents(93).get(added[0], 0)
        call("cheat sailcannonunload 1")
        unloaded = activity(1)
        require(unloaded["registers"][0] == 0 and unloaded["registers"][1] == 0, "Cannon unload failed to stop and empty the magazine")
        require(contents(93).get(added[0], 0) - ammo_pre_unload == damaged["registers"][1], "Unload failed to conserve the unspent ammunition")
        ticks(5)
        quiet = activity(1)
        ticks(8)
        require(activity(1)["ranged_xp_tenths"] == quiet["ranged_xp_tenths"], "Stopped cannon kept firing after projectiles landed")
        shot("cannon-stopped")
        call("cheat sailactivitycleanup")
        ticks(1)
        result["ok"] = True
        result["elapsed_ms"] = round((time.perf_counter() - started) * 1000, 3)
        result["loaded_script_sha256"] = s.metadata().get("script_sha256")
        result["binary_sha256"] = s.metadata().get("binary_sha256")
        result["script_pack_on_disk_sha256"] = hashlib.sha256((ROOT / "OSRS-Content/osrs239-content/server/scripts/build/script.dat").read_bytes()).hexdigest()
        return result
    finally:
        (output / "activity-results.json").write_text(json.dumps(result, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=Path("/tmp/sailing-facility-live"))
    parser.add_argument("--output", type=Path, default=ROOT / "docs/sailing_validation")
    parser.add_argument("--start", action="store_true")
    args = parser.parse_args()
    session = Session(args.session)
    with session.locked():
        if args.start:
            session.start(harness_parser().parse_args(["start", "--headless", "--user", "sailacts"]))
        require(session.alive(), "Start a fresh native skiff session or pass --start")
        result = run(session, args.output.resolve())
    print(json.dumps({"ok": result["ok"], "elapsed_ms": result["elapsed_ms"], "captures": result["captures"]}))


if __name__ == "__main__":
    try:
        main()
    except HarnessError as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        raise SystemExit(1)
