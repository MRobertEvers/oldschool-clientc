#!/usr/bin/env python3
"""derive_tob_nylo_pillars — measure which pillar each Nylocas spawn attacks.

    tools/derive_tob_nylo_pillars.py --fetch 100 --write

Resolves [M24]. Every source in `docs/minigames/theater_of_blood/sources/`
states that a wave-spawned nylo always targets the *same* pillar in every
encounter, and none of them publishes *which*. So measure it.

Method
------
blert.io records Old School raids tick by tick and serves them publicly:

    GET https://blert.io/api/v1/challenges?type=1&stage=ge12&limit=100
    GET https://blert.io/api/v1/raids/tob/<uuid>/events?stage=12

The Nylocas-room event stream carries `NPC_SPAWN`(7), `NPC_UPDATE`(8) and
`NPC_DEATH`(9), each with the npc's tile for that tick, and a spawn additionally
carries blert's `nylo` record: wave, lane, big, style. That is enough to follow
every nylo from its spawn tile to wherever it stops walking.

A nylo is scored as attacking pillar P when it spends >= --dwell ticks standing
on a tile orthogonally or diagonally adjacent to P's 2x2 footprint. Two things
are excluded so that a nylo which is merely *near* a pillar cannot be counted:

  * the walk is cut at the tick the npc id changes into the `fighting` range
    (8348-8353 / 10797-10802). That covers both aggros and the pillar-bound
    nylos that retarget players after their own pillar collapses.
  * a nylo that reaches the dwell threshold at two different pillars in one
    raid is recorded as AMBIG and counted nowhere.

Nothing here assumes the assignment is geometric. It is not: the same spawn
tile feeds different pillars in different waves (west (3281, 4249) goes to NE
on wave 2 and NW on wave 4), and nylos routinely cross the room (east
(3310, 4249) attacks the NW pillar on wave 1).

Output
------
`docs/minigames/theater_of_blood/sources/blert_nylo_pillar_assignment.json`,
one record per spawn slot, carrying the raid count behind it so a thin row is
visible as thin rather than passing as fact.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import sys
import tempfile
import time
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(REPO, "docs", "minigames", "theater_of_blood", "sources")
OUT = os.path.join(SRC_DIR, "blert_nylo_pillar_assignment.json")

API = "https://blert.io/api/v1"
TOB_NYLOCAS_STAGE = 12
CHALLENGE_TYPE_TOB = 1

# The four supports. Each is 2x2; the tile named here is its south-west corner.
# Cross-checked against the Support (Theatre of Blood) wiki map pins and against
# `PillarLocation` in the Zenyte/Near-Reality server tree, whose per-pillar
# `PillarCorner` tiles are exactly the tiles nylos are observed standing on.
PILLARS = {
    "NW": (3290, 4253),
    "NE": (3300, 4253),
    "SW": (3290, 4243),
    "SE": (3300, 4243),
}

# `tob_nylocas_fighting_*` — the aggro form. Normal / entry / hard.
FIGHTING = set(range(8348, 8354)) | set(range(10780, 10786)) | set(range(10797, 10803))

LANES = {1: "west", 2: "south", 3: "east"}
STYLES = {0: "melee", 1: "ranged", 2: "magic"}

# blert's per-lane slot index, resolved against the spawn tile by matching
# size+style in the 25 lanes where the two spawns differ; the four lanes where
# they do not (10 south, 11 south, 21 west, 30 south) follow the same order.
# A big occupies both tiles of its lane and is reported on the lower one.
SLOT_TILES = {
    "west": [(3281, 4249), (3281, 4248)],
    "south": [(3296, 4233), (3295, 4233)],
    "east": [(3310, 4249), (3310, 4248)],
}


def get(url: str, retries: int = 6) -> object:
    """blert allows 30 requests a minute; back off rather than hammer it."""
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                body = json.loads(response.read())
            if isinstance(body, dict) and body.get("error") == "rate_limit_exceeded":
                time.sleep(30)
                continue
            return body
        except urllib.error.HTTPError as exc:
            if exc.code == 429:
                time.sleep(30)
                continue
            raise
    raise RuntimeError("gave up on %s" % url)


def fetch(events_dir: str, count: int, before: int | None = None) -> None:
    """Download the `count` most recent recorded ToB raids that reached Nylocas.

    blert caps a page at 100. `before` is an epoch-millisecond start time; pass
    the oldest `startTime` of the previous page to walk further back.
    """
    os.makedirs(events_dir, exist_ok=True)
    url = ("%s/challenges?type=%d&stage=ge%d&limit=%d"
           % (API, CHALLENGE_TYPE_TOB, TOB_NYLOCAS_STAGE, count))
    if before is not None:
        url += "&startTime=lt%d" % before
    raids = get(url)
    modes_path = os.path.join(events_dir, "modes.json")
    modes = json.load(open(modes_path)) if os.path.exists(modes_path) else {}
    for raid in raids:
        modes[raid["uuid"]] = raid["mode"]
        path = os.path.join(events_dir, raid["uuid"] + ".json")
        if os.path.exists(path):
            continue
        try:
            events = get("%s/raids/tob/%s/events?stage=%d"
                         % (API, raid["uuid"], TOB_NYLOCAS_STAGE))
        except Exception as exc:                       # an in-progress raid 404s
            print("  skip %s: %s" % (raid["uuid"], exc), file=sys.stderr)
            continue
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(events, fh)
        time.sleep(2.5)
    with open(modes_path, "w", encoding="utf-8") as fh:
        json.dump(modes, fh)
    oldest = min(r["startTime"] for r in raids) if raids else None
    print("fetched through %s" % oldest, file=sys.stderr)


def adjacent_pillar(x: int, y: int, size: int) -> str | None:
    """The pillar this npc's footprint is touching, if any."""
    for name, (px, py) in PILLARS.items():
        dx = max(px - (x + size - 1), 0, x - (px + 1))
        dy = max(py - (y + size - 1), 0, y - (py + 1))
        if max(dx, dy) <= 1:
            return name
    return None


def walk(events: list[dict]) -> dict[int, tuple]:
    """Group an event stream into one (nylo record, spawn tile, path) per npc."""
    paths = collections.defaultdict(list)
    spawns = {}
    for event in events:
        npc = event.get("npc")
        if npc is None or event["type"] not in (7, 8, 9):
            continue
        paths[npc["roomId"]].append(
            (event["tick"], event["xCoord"], event["yCoord"], npc["id"]))
        if event["type"] == 7 and npc.get("nylo"):
            spawns[npc["roomId"]] = (npc["nylo"], event["xCoord"], event["yCoord"])
    out = {}
    for room_id, path in paths.items():
        if room_id in spawns:
            nylo, x, y = spawns[room_id]
            out[room_id] = (nylo, x, y, sorted(path))
    return out


def derive(events_dir: str, dwell: int) -> dict:
    modes_path = os.path.join(events_dir, "modes.json")
    modes = json.load(open(modes_path)) if os.path.exists(modes_path) else {}

    pillar_votes = collections.defaultdict(collections.Counter)
    by_mode = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
    became_aggro = collections.Counter()
    observed = collections.Counter()
    raids = 0
    uuids = []

    for name in sorted(os.listdir(events_dir)):
        if not name.endswith(".json") or name == "modes.json":
            continue
        events = json.load(open(os.path.join(events_dir, name)))
        if not isinstance(events, list):
            continue
        raids += 1
        uuids.append(name[:-5])
        mode = modes.get(name[:-5])
        for nylo, sx, sy, path in walk(events).values():
            if nylo["spawnType"] not in LANES:           # 0 = split, 4 = unknown
                continue
            size = 2 if nylo["big"] else 1
            key = (nylo["wave"], LANES[nylo["spawnType"]], sx, sy,
                   nylo["big"], STYLES[nylo["style"]])
            observed[key] += 1
            ticks = collections.Counter()
            for _tick, x, y, npc_id in path:
                if npc_id in FIGHTING:
                    became_aggro[key] += 1
                    break
                pillar = adjacent_pillar(x, y, size)
                if pillar:
                    ticks[pillar] += 1
            reached = [p for p, n in ticks.items() if n >= dwell]
            if len(reached) == 1:
                pillar_votes[key][reached[0]] += 1
                by_mode[mode][key][reached[0]] += 1
            elif len(reached) > 1:
                pillar_votes[key]["AMBIG"] += 1

    rows = []
    for key in sorted(observed, key=lambda k: (k[0], k[1], -k[2], -k[3])):
        wave, lane, sx, sy, big, style = key
        votes = {p: n for p, n in pillar_votes[key].most_common() if p != "AMBIG"}
        slot = SLOT_TILES[lane].index((sx, sy)) if (sx, sy) in SLOT_TILES[lane] else 1
        rows.append({
            "wave": wave,
            "lane": lane,
            "slot": slot,
            "spawnTile": [sx, sy],
            "big": big,
            "style": style,
            "pillar": max(votes, key=votes.get) if votes else None,
            "aggro": not votes,
            "raidsObserved": observed[key],
            "raidsReachingAPillar": sum(votes.values()),
            "raidsTurningAggro": became_aggro[key],
            "disagreeing": {p: n for p, n in list(votes.items())[1:]},
            "ambiguous": pillar_votes[key]["AMBIG"],
        })
    return {
        "source": "https://blert.io/api/v1/raids/tob/<uuid>/events?stage=12",
        "method": "tools/derive_tob_nylo_pillars.py",
        "raids": raids,
        "raidUuids": sorted(uuids),
        "dwellTicks": dwell,
        "pillars": {k: list(v) for k, v in PILLARS.items()},
        "spawns": rows,
    }


LANE_ORDER = ["east", "south", "west"]


def markdown(result: dict) -> str:
    """The assignment as a per-wave table, cell-for-cell with nylocas_waves.md."""
    waves = collections.defaultdict(lambda: collections.defaultdict(dict))
    for row in result["spawns"]:
        waves[row["wave"]][row["lane"]][row["slot"]] = row
    out = ["| Wave | East lane | South lane | West lane |",
           "|---:|---|---|---|"]
    for wave in sorted(waves):
        cells = []
        for lane in LANE_ORDER:
            slots = waves[wave][lane]
            if not slots:
                cells.append("&mdash;")
                continue
            parts = []
            for slot in sorted(slots):
                row = slots[slot]
                if row["aggro"]:
                    parts.append("`*`")
                elif row["raidsReachingAPillar"] < 5:
                    parts.append("%s?" % row["pillar"])
                else:
                    parts.append(row["pillar"])
            cells.append(" + ".join(parts))
        out.append("| %d | %s |" % (wave, " | ".join(cells)))
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--events-dir",
                        default=os.path.join(tempfile.gettempdir(), "blert_nylo_events"),
                        help="where the raw per-raid dumps are cached; ~300 kB each, "
                             "so they are not committed. The uuids used are recorded "
                             "in the output instead, which is enough to re-fetch them.")
    parser.add_argument("--fetch", type=int, metavar="N",
                        help="download the N most recent ToB raids first (max 100)")
    parser.add_argument("--before", type=int, metavar="EPOCH_MS",
                        help="with --fetch, page further back than this start time")
    parser.add_argument("--dwell", type=int, default=8,
                        help="ticks a nylo must stand beside a pillar to count")
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--markdown", action="store_true",
                        help="also print the per-wave table for nylocas_waves.md")
    args = parser.parse_args()

    if args.fetch:
        fetch(args.events_dir, args.fetch, args.before)

    result = derive(args.events_dir, args.dwell)

    conflicts = [r for r in result["spawns"] if r["disagreeing"]]
    thin = [r for r in result["spawns"] if 0 < r["raidsReachingAPillar"] < 5]
    print("%d raids, %d spawn slots, %d aggro, %d conflicting, %d thin (<5 raids)"
          % (result["raids"], len(result["spawns"]),
             sum(1 for r in result["spawns"] if r["aggro"]), len(conflicts), len(thin)))
    for row in conflicts:
        print("  CONFLICT wave %d %s: %s vs %s"
              % (row["wave"], row["lane"], row["pillar"], row["disagreeing"]))
    for row in thin:
        print("  thin wave %2d %-5s %s -> %s (%d raids)"
              % (row["wave"], row["lane"], row["spawnTile"], row["pillar"],
                 row["raidsReachingAPillar"]))

    if args.markdown:
        print()
        print(markdown(result))

    if args.write:
        with open(OUT, "w", encoding="utf-8") as fh:
            json.dump(result, fh, indent=2)
            fh.write("\n")
        print("wrote %s" % os.path.relpath(OUT, REPO))
    return 0


if __name__ == "__main__":
    sys.exit(main())
