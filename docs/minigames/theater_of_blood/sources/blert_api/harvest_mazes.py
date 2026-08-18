#!/usr/bin/env python3
"""Harvest Sotetseg maze paths (M11), Verzik phase timings (M20) and
Nylocas lineage/positions (M37) from blert.

Throttled to 3 s/request; blert is a volunteer-run service and rate-limits
bulk crawls (HTTP 429).  See harvest3.py for the earlier fetcher.

    python3 harvest_mazes.py /tmp/blertmaze
"""
import json, os, sys, time, urllib.request

API = "https://blert.io/api/v1"
DELAY = 3.0


def get(url):
    with urllib.request.urlopen(url, timeout=60) as fh:
        return json.load(fh)


def challenge_uuids():
    """Completed raids that reached Verzik, spread over every mode and scale."""
    out = []
    for mode in (10, 11, 12):
        for scale in range(1, 6):
            url = (f"{API}/challenges?type=1&status=eq1&stage=eq15"
                   f"&mode={mode}&scale=eq{scale}&limit=50")
            for c in get(url):
                out.append((c["uuid"], mode, scale))
            time.sleep(DELAY)
    return sorted(set(out))


def main(outdir):
    for sub in ("sote", "verzik", "nylo"):
        os.makedirs(os.path.join(outdir, sub), exist_ok=True)
    uuids = challenge_uuids()
    print(f"{len(uuids)} raids")

    for uuid, mode, scale in uuids:
        # stage 13 = Sotetseg: keep the maze proc (130) and maze path (131) events
        path = os.path.join(outdir, "sote", f"{mode}_{scale}_{uuid}.json")
        if not os.path.exists(path):
            ev = get(f"{API}/raids/tob/{uuid}/events?stage=13")
            keep = [e for e in ev if e["type"] in (130, 131)]
            json.dump(keep, open(path, "w"))
            time.sleep(DELAY)

    for uuid, mode, scale in uuids[:130]:
        # stage 15 = Verzik: phase events (150), npc attacks (10), npc updates (8)
        path = os.path.join(outdir, "verzik", f"{mode}_{scale}_{uuid}.json")
        if not os.path.exists(path):
            ev = get(f"{API}/raids/tob/{uuid}/events?stage=15")
            keep = [{"type": e["type"], "tick": e["tick"],
                     "id": (e.get("npc") or {}).get("id"),
                     "ph": e.get("verzikPhase"),
                     "atk": (e.get("npcAttack") or {}).get("attack")}
                    for e in ev if e["type"] in (8, 10, 150)]
            json.dump(keep, open(path, "w"))
            time.sleep(DELAY)


    for uuid, mode, scale in uuids[:70]:
        # stage 12 = Nylocas: spawns carry lineage, updates carry per-tick position
        path = os.path.join(outdir, "nylo", f"{mode}_{scale}_{uuid}.json")
        if not os.path.exists(path):
            ev = get(f"{API}/raids/tob/{uuid}/events?stage=12")
            keep = [{"type": e["type"], "tick": e["tick"],
                     "x": e["xCoord"], "y": e["yCoord"],
                     "id": (e.get("npc") or {}).get("id"),
                     "rid": (e.get("npc") or {}).get("roomId"),
                     "nylo": (e.get("npc") or {}).get("nylo")}
                    for e in ev if e["type"] in (7, 8, 9)]
            json.dump(keep, open(path, "w"))
            time.sleep(DELAY)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "/tmp/blertmaze")
