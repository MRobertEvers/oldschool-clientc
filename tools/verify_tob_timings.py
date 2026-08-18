#!/usr/bin/env python3
"""verify_tob_timings — check the Theatre implementation against recorded raids.

    tools/verify_tob_timings.py --fetch 25
    tools/verify_tob_timings.py                 # re-analyse the cache

Every tick constant in `minigame_tob/configs/tob.constant` came from a plugin's
source, a wiki sentence, or somebody's guide. This measures them against what
actually happened in real raids, which is the only source that cannot be out of
date or misread.

blert.io records Old School raids tick by tick and serves them publicly:

    GET /api/v1/challenges?type=1&stage=ge<n>&limit=100
    GET /api/v1/raids/tob/<uuid>/events?stage=<n>

The event stream carries, per tick, `NPC_ATTACK`(10) with the boss's attack id
and target, plus per-room events: Maiden's crab spawns(100) and blood
splats(101), and Bloat's down(110)/up(111)/hand-spawn(112)/hand-land(113).
Nothing here is inferred from an animation - blert already resolved the attack.

The event type numbers are not published in anything archived under
`sources/`, so they are recovered from the data itself and pinned in
`EVENT` below. A stream whose types stop matching these is a stream this tool
should refuse to read rather than silently mis-measure, which is what
`--strict` is for.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import statistics
import sys
import tempfile
import time
import urllib.error
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONSTANTS = os.path.join(REPO, "OSRS-Content", "osrs239-content", "server", "scripts",
                         "minigames", "minigame_tob", "configs", "tob.constant")
API = "https://blert.io/api/v1"
CHALLENGE_TYPE_TOB = 1

EVENT = {
    "PLAYER_UPDATE": 4,
    "PLAYER_ATTACK": 5,
    "NPC_SPAWN": 7,
    "NPC_UPDATE": 8,
    "NPC_DEATH": 9,
    "NPC_ATTACK": 10,
    "PLAYER_SPELL": 11,
    "MAIDEN_CRAB_SPAWN": 100,
    "MAIDEN_BLOOD_SPLATS": 101,
    "BLOAT_DOWN": 110,
    "BLOAT_UP": 111,
    "BLOAT_HANDS_SPAWN": 112,
    "BLOAT_HANDS_DROP": 113,
}

MAIDEN_BLOOD_SPAWN = 8367

STAGE = {"maiden": 10, "bloat": 11, "nylocas": 12, "sotetseg": 13,
         "xarpus": 14, "verzik": 15}

# Blert's raid `mode`: only regular raids are measured, because hard mode
# changes several of the numbers under test (Bloat's turn cooldown halves, the
# Nylocas cap rises) and mixing the two would widen every distribution for a
# reason that has nothing to do with the implementation being wrong.
#
# Recovered from the listing rather than assumed: the live feed carries modes
# 11 and 12 in roughly a 3:1 ratio, which is regular and hard. There is no
# published enum for this either.
MODE_REGULAR = 11

# `status` 1 is a finished raid. An in-progress one (0) has a truncated event
# stream, and reading a half-recorded room as if it were whole is how a cadence
# measurement quietly acquires a wrong tail.
STATUS_COMPLETED = 1


def get(url: str, retries: int = 6) -> object:
    """blert allows 30 requests a minute; back off rather than hammer it."""
    for _ in range(retries):
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
            if exc.code == 404:
                return None
            raise
    raise RuntimeError("gave up on %s" % url)


def fetch(cache: str, count: int) -> None:
    os.makedirs(cache, exist_ok=True)
    raids = get("%s/challenges?type=%d&stage=ge%d&limit=%d"
                % (API, CHALLENGE_TYPE_TOB, STAGE["verzik"], count))
    kept = [r for r in raids
            if r.get("mode") == MODE_REGULAR and r.get("status") == STATUS_COMPLETED]
    print("%d raids listed, %d finished and in regular mode" % (len(raids), len(kept)))
    for i, raid in enumerate(kept):
        for room, stage in STAGE.items():
            path = os.path.join(cache, "%s.%s.json" % (raid["uuid"], room))
            if os.path.exists(path):
                continue
            events = get("%s/raids/tob/%s/events?stage=%d" % (API, raid["uuid"], stage))
            if events is None:
                continue
            with open(path, "w", encoding="utf-8") as fh:
                json.dump(events, fh)
            time.sleep(2.2)
        print("  %d/%d %s" % (i + 1, len(kept), raid["uuid"]))


def load(cache: str, room: str) -> list[list[dict]]:
    out = []
    for name in sorted(os.listdir(cache)):
        if not name.endswith(".%s.json" % room):
            continue
        with open(os.path.join(cache, name), encoding="utf-8") as fh:
            events = json.load(fh)
        if isinstance(events, list) and events:
            out.append(events)
    return out


def constants() -> dict[str, int]:
    out = {}
    with open(CONSTANTS, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("//")[0].strip()
            if not line.startswith("^") or "=" not in line:
                continue
            name, _, value = line.partition("=")
            value = value.strip()
            if value.lstrip("-").isdigit():
                out[name.strip().lstrip("^")] = int(value)
    return out


def wave_table() -> dict[int, int]:
    """The generated `tob_nylo_wave` rows: wave -> natural stall."""
    path = os.path.join(REPO, "OSRS-Content", "osrs239-content", "server", "scripts",
                        "minigames", "minigame_tob", "configs", "tob_nylo.dbrow")
    out, wave = {}, None
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line.startswith("data=wave,"):
                wave = int(line.split(",")[1])
            elif line.startswith("data=stall,") and wave is not None:
                out[wave] = int(line.split(",")[1])
                wave = None
    return out


def intervals(events: list[dict], type_id: int) -> list[int]:
    ticks = sorted({e["tick"] for e in events if e["type"] == type_id})
    return [b - a for a, b in zip(ticks, ticks[1:])]


class Report:
    def __init__(self) -> None:
        self.rows: list[tuple[str, str, str, str, bool]] = []

    def check(self, name: str, measured, expected, ok: bool, note: str = "") -> None:
        self.rows.append((name, str(measured), str(expected), note, ok))

    def note(self, name: str, measured, note: str = "") -> None:
        """An observation with no constant to fail against. Reported rather
        than dropped: a number nobody printed is a number nobody checked."""
        self.rows.append((name, str(measured), "-", note, None))

    def render(self) -> int:
        bad = 0
        width = max(len(r[0]) for r in self.rows) if self.rows else 10
        for name, measured, expected, note, ok in self.rows:
            if ok is None:
                flag = "note"
            elif ok:
                flag = "ok"
            else:
                flag = "MISMATCH"
                bad += 1
            print("%-8s %-*s measured %-22s constant %-10s %s"
                  % (flag, width, name, measured, expected, note))
        return bad


def mode_of(values: list[int]) -> int | None:
    if not values:
        return None
    return collections.Counter(values).most_common(1)[0][0]


def summary(values: list[int]) -> str:
    if not values:
        return "no data"
    counts = collections.Counter(values).most_common(3)
    spread = " ".join("%d x%d" % (v, n) for v, n in counts)
    return "n=%d median=%d [%s]" % (len(values), int(statistics.median(values)), spread)


def analyse_maiden(raids, k, rep) -> None:
    gaps = []
    for events in raids:
        gaps += intervals(events, EVENT["NPC_ATTACK"])
    common = mode_of(gaps)
    rep.check("maiden attack period", summary(gaps), k["tob_maiden_attack_ticks"],
              common == k["tob_maiden_attack_ticks"],
              "the most common gap is the cadence; longer gaps are phase transitions")

    # The blood splat.
    #
    # NOT asserted against `^tob_maiden_blood_splat_ticks`, because the two are
    # different quantities and comparing them was this tool's own bug. That
    # constant is TobMistakeTracker's `MAIDEN_BLOOD_GAME_TICK_LENGTH = 11`,
    # which is how long a tile still COSTS you - it sets a `deactivationTick`
    # for the mistake detector. What a recording shows is how long the tile is
    # still DRAWN, and blert merges the splat graphic (1579) with the blood
    # trail objects into one event, so a tile near a blood spawn is a trail
    # rather than a splat.
    #
    # Measured on the only clean window there is: ticks before the first blood
    # spawn npc (8367) exists in the room, where every tile must be a splat
    # from one of Maiden's own attacks.
    lifetimes = []
    for events in raids:
        slugs = [e["tick"] for e in events
                 if e["type"] == EVENT["NPC_SPAWN"] and (e.get("npc") or {}).get("id") == MAIDEN_BLOOD_SPAWN]
        cut = min(slugs) if slugs else None
        if cut is None:
            continue
        seen: dict[tuple[int, int], list[int]] = {}
        for e in events:
            if e["type"] != EVENT["MAIDEN_BLOOD_SPLATS"] or e["tick"] >= cut:
                continue
            for tile in e.get("maidenBloodSplats", []):
                seen.setdefault((tile["x"], tile["y"]), []).append(e["tick"])
        for ticks in seen.values():
            run = 1
            for a, b in zip(ticks, ticks[1:]):
                if b - a == 1:
                    run += 1
                else:
                    lifetimes.append(run)
                    run = 1
            lifetimes.append(run)
    if lifetimes:
        drawn = k["tob_maiden_blood_splat_drawn"]
        common = mode_of(lifetimes)
        rep.check("blood splat drawn for",
                  "%s min=%d" % (summary(lifetimes), min(lifetimes)), drawn,
                  common == drawn,
                  "drawn lifetime, NOT the %d-tick damage window; longer runs are "
                  "two splats overlapping on one tile"
                  % k["tob_maiden_blood_splat_ticks"])

    # The blood spawn's trail. Measured on the clean tail - runs still ending
    # after the last slug is dead, where nothing new is being laid.
    tails = []
    for events in raids:
        deaths = [e["tick"] for e in events
                  if e["type"] == EVENT["NPC_DEATH"] and (e.get("npc") or {}).get("id") == MAIDEN_BLOOD_SPAWN]
        if not deaths:
            continue
        last = max(deaths)
        seen: dict[tuple[int, int], list[int]] = {}
        for e in events:
            if e["type"] != EVENT["MAIDEN_BLOOD_SPLATS"]:
                continue
            for tile in e.get("maidenBloodSplats", []):
                seen.setdefault((tile["x"], tile["y"]), []).append(e["tick"])
        for ticks in seen.values():
            run = [ticks[0]]
            for a, b in zip(ticks, ticks[1:]):
                if b - a == 1:
                    run.append(b)
                else:
                    if run[-1] > last:
                        tails.append(len(run))
                    run = [b]
            if run[-1] > last:
                tails.append(len(run))
    if tails:
        trail = k["tob_maiden_blood_trail_ticks"]
        rep.check("blood trail lifetime", summary(tails), trail,
                  mode_of(tails) == trail,
                  "runs outliving the last blood spawn; shorter ones are cut by the room ending")


def analyse_bloat(raids, k, rep) -> None:
    down_to_up, down_to_stomp, walks, first_walks = [], [], [], []
    for events in raids:
        downs = [e["tick"] for e in events if e["type"] == EVENT["BLOAT_DOWN"]]
        ups = [e["tick"] for e in events if e["type"] == EVENT["BLOAT_UP"]]
        stomps = [e["tick"] for e in events if e["type"] == EVENT["NPC_ATTACK"]]
        for d in downs:
            after = [u for u in ups if u > d]
            if after:
                down_to_up.append(after[0] - d)
            hit = [s for s in stomps if d < s < d + 40]
            if hit:
                down_to_stomp.append(hit[0] - d)
        # blert states the walk length on the down event itself, in TWO fields:
        # `walkTime` is the walk, and `upTicks` is `walkTime + 1`. Reading
        # `upTicks` shifts every measurement up by one and makes the Wiki's
        # 34..42 look like it is off by one when it is exact.
        for e in events:
            if e["type"] != EVENT["BLOAT_DOWN"]:
                continue
            info = e.get("bloatDown", {})
            if "walkTime" not in info:
                continue
            (first_walks if info.get("downNumber") == 1 else walks).append(info["walkTime"])

    rep.check("bloat down -> up", summary(down_to_up), k["tob_bloat_up_offset"],
              mode_of(down_to_up) == k["tob_bloat_up_offset"])
    rep.check("bloat down -> stomp", summary(down_to_stomp), k["tob_bloat_stomp_offset"],
              mode_of(down_to_stomp) == k["tob_bloat_stomp_offset"])

    # The bonus is not the first walk's alone. The Wiki gives it to a Bloat
    # that was not attacked during its down, and a team that is repositioning
    # or waiting for a stomp leaves it alone on later downs too - so the
    # envelope for EVERY walk is min .. max+bonus.
    #
    # That envelope is what the recordings draw exactly: 34 to 46 over 42
    # walks, with 34 the most common and 46 reached. Both ends of both
    # constants are therefore confirmed rather than merely not-contradicted.
    lo, hi = k["tob_bloat_walk_min"], k["tob_bloat_walk_max"]
    top = hi + k["tob_bloat_walk_unattacked_bonus"]
    inrange = [w for w in walks if lo <= w <= top]
    rep.check("bloat walk (later)", "%s min=%d max=%d" % (summary(walks), min(walks), max(walks)),
              "%d..%d" % (lo, top),
              len(walks) > 0 and len(inrange) == len(walks),
              "min..max+bonus; an unattacked down earns the bonus on any walk")
    # The FIRST walk, now asserted. It was previously left as a note on the
    # theory that blert's tick 0 being room entry made it a measurement of the
    # team rather than of the boss. It is not: the first-walk distribution is
    # the later one shifted +5 at BOTH ends, which a variable human delay could
    # not produce. So it is a constant, and it is checked like one.
    delay = k["tob_bloat_first_walk_delay"]
    lo1, hi1 = lo + delay, top + delay
    inrange1 = [w for w in first_walks if lo1 <= w <= hi1]
    rep.check("bloat walk (first)",
              "%s min=%d max=%d" % (summary(first_walks), min(first_walks), max(first_walks)),
              "%d..%d" % (lo1, hi1),
              len(first_walks) > 0 and len(inrange1) == len(first_walks),
              "the later envelope plus the %d-tick startup" % delay)
    shifted = [w - delay for w in first_walks]
    rep.check("bloat first-walk shift",
              "shifted min=%d max=%d" % (min(shifted), max(shifted)),
              "%d..%d" % (lo, top),
              min(shifted) >= lo and max(shifted) <= top,
              "the shift is exact: subtract it and the first walks are ordinary ones")


def analyse_nylocas(raids, k, rep, waves) -> None:
    """Check the generated stall table against the recordings.

    A wave spawns `naturalStall` ticks after the previous one *if nobody
    stalled*; a team over the cap makes the gap longer, never shorter. So the
    table's value has to be the MINIMUM observed gap - a stall table that is
    too low would show up as gaps below it, which is the failure worth
    catching.

    Splits are excluded (`parentRoomId != 0`): they spawn when their parent
    dies, on whatever tick that was, and counting them as wave spawns is what
    made 1217 of 1790 gaps look like they were off-cycle.
    """
    per_wave = collections.defaultdict(list)
    for events in raids:
        first = {}
        for e in events:
            nylo = (e.get("npc") or {}).get("nylo")
            if e["type"] != EVENT["NPC_SPAWN"] or not nylo:
                continue
            if nylo.get("parentRoomId"):
                continue
            w = nylo["wave"]
            first[w] = min(first.get(w, e["tick"]), e["tick"])
        for w in sorted(first):
            if w + 1 in first:
                per_wave[w].append(first[w + 1] - first[w])

    below, checked, offcycle = [], 0, 0
    for w, gaps in sorted(per_wave.items()):
        stall = waves.get(w)
        if stall is None:
            continue
        checked += 1
        offcycle += sum(1 for g in gaps if g % k["tob_nylo_cycle_ticks"] != 0)
        if min(gaps) < stall:
            below.append("w%d min %d < %d" % (w, min(gaps), stall))
    rep.check("nylo stall table", "%d waves checked, %d under table" % (checked, len(below)),
              "no gap below its stall", not below,
              "; ".join(below[:4]))
    rep.check("nylo wave cycle", "%d off-cycle gaps" % offcycle,
              k["tob_nylo_cycle_ticks"], offcycle == 0,
              "every wave-to-wave gap is a multiple of the cycle")


def analyse_simple(raids, k, rep, label: str, key: str, note: str = "") -> None:
    gaps = []
    for events in raids:
        gaps += intervals(events, EVENT["NPC_ATTACK"])
    rep.check(label, summary(gaps), k[key], mode_of(gaps) == k[key], note)


def analyse_verzik(raids, k, rep) -> None:
    """Verzik's three phases have three different periods, so a single gap
    histogram would blend them. Split on the npc id changing, which is what a
    phase change is."""
    per_phase = {1: [], 2: [], 3: []}
    # From the cache's own gameval table (`sources/cache_npc_verzik.txt`), not
    # from the order the ids happen to appear in: 8371 and 8373 are the two
    # TRANSITION forms and attack in neither phase, so a map that counted up
    # from 8370 would file every phase-2 attack under phase 3. It did.
    ids = {8370: 1, 8372: 2, 8374: 3}
    for events in raids:
        buckets = collections.defaultdict(list)
        for e in events:
            if e["type"] != EVENT["NPC_ATTACK"]:
                continue
            phase = ids.get((e.get("npc") or {}).get("id"))
            if phase:
                buckets[phase].append(e["tick"])
        for phase, ticks in buckets.items():
            ticks = sorted(set(ticks))
            per_phase[phase] += [b - a for a, b in zip(ticks, ticks[1:])]
    for phase, key in ((1, "tob_verzik_p1_attack_ticks"),
                       (2, "tob_verzik_p2_attack_ticks"),
                       (3, "tob_verzik_p3_attack_ticks")):
        gaps = per_phase[phase]
        rep.check("verzik p%d period" % phase, summary(gaps), k[key],
                  mode_of(gaps) == k[key],
                  "p3 also runs at 5 once enraged" if phase == 3 else "")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cache", default=os.path.join(tempfile.gettempdir(), "blert_tob_events"))
    ap.add_argument("--fetch", type=int, metavar="N", help="download N recent raids first")
    args = ap.parse_args()

    if args.fetch:
        fetch(args.cache, args.fetch)
    if not os.path.isdir(args.cache):
        print("no cache; run with --fetch N", file=sys.stderr)
        return 2

    k = constants()
    rep = Report()
    rooms = {r: load(args.cache, r) for r in STAGE}
    print("raids per room: " + "  ".join("%s=%d" % (r, len(v)) for r, v in rooms.items()))
    print()

    if rooms["maiden"]:
        analyse_maiden(rooms["maiden"], k, rep)
    if rooms["bloat"]:
        analyse_bloat(rooms["bloat"], k, rep)
    if rooms["nylocas"]:
        analyse_nylocas(rooms["nylocas"], k, rep, wave_table())
    if rooms["sotetseg"]:
        analyse_simple(rooms["sotetseg"], k, rep, "sotetseg period", "tob_sote_attack_ticks")
    if rooms["xarpus"]:
        analyse_simple(rooms["xarpus"], k, rep, "xarpus spit period", "tob_xarpus_spit_ticks",
                       "p3's stare is 8; the most common gap is p2")
    if rooms["verzik"]:
        analyse_verzik(rooms["verzik"], k, rep)

    print()
    bad = rep.render()
    print()
    print("%d mismatch(es)" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
