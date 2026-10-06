#!/usr/bin/env python3
"""verify_tob_timings — check the Theatre implementation against recorded raids.

    tools/verify_tob_timings.py --fetch 25
    tools/verify_tob_timings.py                 # re-analyse the cache
    tools/verify_tob_timings.py --ticklog build/quest_gate/<run>/ticklog.tsv \
        [--start-tick N | --start-mark REGEX [--anchor-offset -1]] [--mode hard] [--scale N]

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

`--ticklog` measures a run on OUR server the same way: the quest driver's
`t.ticklog` writes `<run>/ticklog.tsv` (header `ticklog-v1`), and the same
checks run over its npc_anim, npc_tile, hit_player, map_spotanim and loc_set
rows against the same constants. The log has no room-start row, so a
measurement that counts from room tick 0 (Maiden's first attack, Bloat's first
walk, Xarpus' first exhumed) needs an anchor: `--start-tick`, or a mark the
test wrote; without one those rows are printed as notes, never checked.
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

import config_text

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
    # Recovered from the cached Xarpus streams the same way: 140 carries
    # `xarpusPhase`, 141 `xarpusExhumed` ({spawnTick, healAmount, healTicks},
    # stamped on the tick the exhumed is gone), 142 `xarpusSplat`.
    "XARPUS_PHASE": 140,
    "XARPUS_EXHUMED": 141,
    "XARPUS_SPLATS": 142,
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
        for line in config_text.filter_lines(fh):
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


def runs_of(ticks: list[int]) -> list[list[int]]:
    """Split sorted ticks into runs of consecutive ticks."""
    out: list[list[int]] = []
    for t in ticks:
        if out and t - out[-1][-1] == 1:
            out[-1].append(t)
        elif not out or t != out[-1][-1]:
            out.append([t])
    return out


def first_check(rep: "Report", name: str, firsts: list, expected: int, note: str) -> None:
    """A first-attack (or first-spawn) offset from the room start: +-1, the
    tolerance RAID_ORCHESTRATOR.md section 6 gives a first-attack offset for
    recorder alignment (TOB_RESEARCH.md M16). `None` entries are segments of a
    tick log with no room-start anchor; they are reported, never checked."""
    known = [f for f in firsts if f is not None]
    if not firsts:
        return
    if not known:
        rep.note(name, "no room-start anchor",
                 "pass --start-tick or --start-mark to put room tick 0 somewhere")
        return
    rep.check(name, "%s min=%d max=%d" % (summary(known), min(known), max(known)),
              "%d +-1" % expected, all(abs(f - expected) <= 1 for f in known), note)


def analyse_maiden(raids, k, rep) -> None:
    gaps, firsts = [], []
    for events in raids:
        gaps += intervals(events, EVENT["NPC_ATTACK"])
        ticks = [e["tick"] for e in events if e["type"] == EVENT["NPC_ATTACK"]]
        if ticks:
            firsts.append(min(ticks))
    common = mode_of(gaps)
    rep.check("maiden attack period", summary(gaps), k["tob_maiden_attack_ticks"],
              common == k["tob_maiden_attack_ticks"],
              "the most common gap is the cadence; longer gaps are phase transitions")
    first_check(rep, "maiden first attack", firsts, k["tob_maiden_first_attack_ticks"],
                "blert tick 0 is the room start")

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
    # What it is compared against. This read `^tob_maiden_blood_splat_drawn`
    # (13), which tob.constant deleted: that number was fed to `spotanim_map`'s
    # fourth argument as a lifetime and is a DELAY, so it never drew anything
    # (tob.constant, the comment above `^tob_maiden_blood_splat_ticks`). The
    # pool is a loc now, `loc_add(..., tob_maiden_blood, ..., grounddecor,
    # ^tob_maiden_blood_splat_ticks)` (tob_maiden.rs2), so the damage window IS
    # how long our pool is drawn, and that is the implementation figure a
    # recording's drawn run measures. If the two disagree, the mismatch is the
    # finding: either blert's drawn run includes something our loc does not, or
    # the pool should outlive its damage window.
    # The recorder sees a despawn one tick early (M5, below), so a recorded
    # run of N is a pool that was drawn N + 1, the same correction the trail
    # check makes.
    if lifetimes:
        drawn = k["tob_maiden_blood_splat_ticks"]
        common = mode_of(lifetimes)
        rep.check("blood splat drawn for",
                  "%s min=%d (+1 = %d)" % (summary(lifetimes), min(lifetimes), common + 1), drawn,
                  common + 1 == drawn,
                  "our pool loc is drawn for ^tob_maiden_blood_splat_ticks (its damage "
                  "window); longer runs are two splats overlapping on one tile")

    # The blood spawn's trail. A run is a trail when a blood spawn stood on
    # that tile on the tick it appeared (or one or two before: the spawn's
    # NPC_UPDATE and the splat list are separate events); every other run is
    # one of Maiden's own splats. This used to take "runs still ending after
    # the last slug is dead" as its clean window, but Maiden keeps throwing
    # after the last slug dies, so on a short cache that window held her
    # splats (13 x5 of 6 runs over the three cached rooms) and no trail.
    # Runs still open on the room's last event are cut by the room ending and
    # are left out. The recorder sees a loc's despawn one tick early (M5,
    # closed: measured 29, Zenyte `BloodTrail` 30), so a recorded run of N is
    # a loc that lived N + 1.
    trails = []
    for events in raids:
        slug_at = {(e["tick"], e["xCoord"], e["yCoord"]) for e in events
                   if e["type"] in (EVENT["NPC_SPAWN"], EVENT["NPC_UPDATE"])
                   and (e.get("npc") or {}).get("id") == MAIDEN_BLOOD_SPAWN}
        if not slug_at:
            continue
        end = max(e["tick"] for e in events)
        seen: dict[tuple[int, int], list[int]] = {}
        for e in events:
            if e["type"] != EVENT["MAIDEN_BLOOD_SPLATS"]:
                continue
            for tile in e.get("maidenBloodSplats", []):
                seen.setdefault((tile["x"], tile["y"]), []).append(e["tick"])
        for (x, y), ticks in seen.items():
            for run in runs_of(ticks):
                if run[-1] >= end:
                    continue
                if any((run[0] - back, x, y) in slug_at for back in (0, 1, 2)):
                    trails.append(len(run))
    if trails:
        trail = k["tob_maiden_blood_trail_ticks"]
        rep.check("blood trail lifetime", "%s (+1 = %d)" % (summary(trails), mode_of(trails) + 1),
                  trail, mode_of(trails) + 1 == trail,
                  "runs a blood spawn laid; the recorder sees the despawn a tick early (M5), "
                  "longer runs are re-covered tiles")


BLOAT_IDS = (8359, 10812, 10813)          # tob_bloat, _story, _hard (cache_npc_bloat.txt)


def bloat_hand_checks(rep: "Report", k: dict, gaps: list[int], delays: list[int]) -> None:
    """The falling hands, from either source. A gap shorter than a down is
    inside one walk and must be the healthy or the hurt cadence; a longer one
    spans a down, when nothing falls in regular mode."""
    if not gaps and not delays:
        rep.note("bloat hand drops", "none", "no hands fell (above the hands threshold, or none logged)")
        return
    cadence = (k["tob_bloat_hand_ticks"], k["tob_bloat_hand_ticks_hurt"])
    inside = [g for g in gaps if g < k["tob_bloat_down_ticks"]]
    rep.check("bloat hand drop gaps", "%s (%d inside a walk)" % (summary(gaps), len(inside)),
              "%d or %d" % cadence, bool(inside) and all(g in cadence for g in inside),
              "gaps shorter than a down; longer ones span the down")
    rep.check("bloat hand shadow -> drop", summary(delays), k["tob_bloat_hand_delay"],
              bool(delays) and mode_of(delays) == k["tob_bloat_hand_delay"])


def bloat_hp_pct(events: list[dict], tick: int) -> int | None:
    """Bloat's hitpoints at `tick`, from its last NPC_UPDATE. blert packs them
    as current << 16 | max (the room's first update reads 1750 of 1750)."""
    best = None
    for e in events:
        if e["type"] != EVENT["NPC_UPDATE"] or e["tick"] > tick:
            continue
        npc = e.get("npc") or {}
        if npc.get("id") in BLOAT_IDS and npc.get("hitpoints"):
            best = npc["hitpoints"]
    if not best or not best & 0xFFFF:
        return None
    return 100 * (best >> 16) // (best & 0xFFFF)


def analyse_bloat(raids, k, rep) -> None:
    down_to_up, down_to_stomp, walks, first_walks = [], [], [], []
    hand_gaps, hand_delays = [], []
    outside: list[str] = []
    lo, hi = k["tob_bloat_walk_min"], k["tob_bloat_walk_max"]
    top = hi + k["tob_bloat_walk_unattacked_bonus"]
    for events in raids:
        spawns = sorted({e["tick"] for e in events if e["type"] == EVENT["BLOAT_HANDS_SPAWN"]})
        drops = sorted({e["tick"] for e in events if e["type"] == EVENT["BLOAT_HANDS_DROP"]})
        hand_gaps += [b - a for a, b in zip(drops, drops[1:])]
        for t in spawns:
            later = [d for d in drops if d >= t]
            if later:
                hand_delays.append(later[0] - t)
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
            if info.get("downNumber") != 1 and not lo <= info["walkTime"] <= top:
                pct = bloat_hp_pct(events, e["tick"])
                outside.append("%d (down %s at tick %d, Bloat %s%%)"
                               % (info["walkTime"], info.get("downNumber"), e["tick"],
                                  "?" if pct is None else pct))

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
    inrange = [w for w in walks if lo <= w <= top]
    rep.check("bloat walk (later)", "%s min=%d max=%d" % (summary(walks), min(walks), max(walks)),
              "%d..%d" % (lo, top),
              len(walks) > 0 and len(inrange) == len(walks),
              "min..max+bonus; an unattacked down earns the bonus on any walk"
              + ("; OUTSIDE: " + ", ".join(outside[:4]) if outside else ""))
    bloat_hand_checks(rep, k, hand_gaps, hand_delays)
    # The FIRST walk, now asserted. It was previously left as a note on the
    # theory that blert's tick 0 being room entry made it a measurement of the
    # team rather than of the boss. It is not: the first-walk distribution is
    # the later one shifted +5 at BOTH ends, which a variable human delay could
    # not produce. So it is a constant, and it is checked like one.
    # `^tob_bloat_walk_first_bonus` (tob.constant, [M17 closed]: +5 at both ends
    # over 98 445 first downs). This read `^tob_bloat_first_walk_delay`, the
    # name it had before the Bloat block was rewritten.
    delay = k["tob_bloat_walk_first_bonus"]
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


def analyse_xarpus(raids, k, rep) -> None:
    """The spit period, as before, plus the exhumeds (event 141), which carry
    their own spawn tick and are stamped on the tick they are gone."""
    analyse_simple(raids, k, rep, "xarpus spit period", "tob_xarpus_spit_ticks",
                   "p3's stare is 8; the most common gap is p2")
    lifetimes, gaps, firsts = [], [], []
    for events in raids:
        ex = [e for e in events if e["type"] == EVENT["XARPUS_EXHUMED"]
              and "spawnTick" in (e.get("xarpusExhumed") or {})]
        lifetimes += [e["tick"] - e["xarpusExhumed"]["spawnTick"] for e in ex]
        born = sorted(e["xarpusExhumed"]["spawnTick"] for e in ex)
        gaps += [b - a for a, b in zip(born, born[1:])]
        if born:
            firsts.append(born[0])
    xarpus_exhumed_checks(rep, k, lifetimes, gaps, firsts, hard=False, scale=None)


def xarpus_exhumed_checks(rep, k, lifetimes, gaps, firsts, hard: bool, scale) -> None:
    if not lifetimes:
        rep.note("xarpus exhumed", "none", "no exhumed in the stream")
        return
    life = k["tob_xarpus_exhumed_open_ticks_hard" if hard else "tob_xarpus_exhumed_open_ticks"]
    rep.check("xarpus exhumed lifetime", summary(lifetimes), life, mode_of(lifetimes) == life,
              "spawn to gone")
    # The cadence moves with party size (^tob_xarpus_spawn_ticks_<scale>) and a
    # blert stream does not carry the scale, so a recording's gaps are printed,
    # not checked; a tick log names its scale with --scale.
    if scale is None:
        rep.note("xarpus exhumed spawn gap", summary(gaps),
                 "12/8/8/4/4 by scale 1-5 (^tob_xarpus_spawn_ticks_N); mixed scales here")
    else:
        # Hard's 4 is the trio-to-five-man figure; a Hard solo spawns every 12
        # like a regular one (encounters/xarpus.tsv xarpus.p1.spawn_gap.hard_solo,
        # blert), and a Hard duo has no recording, so it reads the regular table.
        key = "tob_xarpus_spawn_ticks_hard" if hard and scale >= 3 else "tob_xarpus_spawn_ticks_%d" % scale
        rep.check("xarpus exhumed spawn gap", summary(gaps), k[key],
                  bool(gaps) and mode_of(gaps) == k[key], "^" + key)
    known = [f for f in firsts if f is not None]
    rep.note("xarpus first exhumed", summary(known) if known else "no room-start anchor",
             "room tick; constant %d, spec 8-12 (xarpus.p1.first_exhumed_tick)"
             % k["tob_xarpus_exhumed_first_ticks"])


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


# ---------------------------------------------------------------------------
# Our server's tick log (`--ticklog`).
#
# The same measurements, read from a quest-driver run's `ticklog.tsv`
# (src/torirsserver/torirs_server_ticklog.c; the kinds and their fields are
# QD.ticklog.FIELDS in script/plugins/quest_driver/ticklog.lua and
# docs/minigames/raid_loop/DRIVER_NOTES.md "The tick log"), so a run on our
# server and a blert recording are compared against the same constants by the
# same checks. blert reports an attack; the log reports the animation the
# client was sent (npc_anim, after the priority gate), so each room names the
# attack seqs it counts, by their cache names (sources/cache_seq.txt).
# ---------------------------------------------------------------------------

TICKLOG_HEADER = "ticklog-v1"
TICKLOG_FIELDS = {
    "start": ("start_tick",),
    "mark": (),
    "npc_anim": ("slot", "type", "seq", "delay"),
    "npc_spotanim": ("slot", "type", "spotanim", "height", "delay"),
    "projectile": ("src", "dst", "target", "spotanim", "start_cycle", "end_cycle"),
    "map_spotanim": ("coord", "spotanim", "height", "delay"),
    "hit_player": ("pid", "npc_slot", "damage", "hitsplat", "dealer_pid", "npc_type"),
    "hit_npc": ("slot", "type", "damage", "hitsplat"),
    "npc_spawn": ("slot", "type", "coord"),
    "npc_death": ("slot", "type", "coord"),
    "npc_free": ("slot", "type", "coord"),
    "npc_retype": ("slot", "from_type", "to_type", "duration"),
    "loc_set": ("coord", "loc", "shape", "angle", "loc_kind"),
    "obj_add": ("coord", "obj", "count", "receiver_pid"),
    "player_tile": ("pid", "x", "z", "level"),
    "npc_tile": ("slot", "x", "z", "level", "type"),
}

MAIDEN_ATTACK_SEQS = {8091, 8092}            # maiden_attack_blood, maiden_attack_special
MAIDEN_POOL_LOC = 32984                      # tob_maiden_blood (cache_locs.txt)
MAIDEN_BLOOD_SPAWN_IDS = {8367, 10821, 10829}  # maiden_blood_slug, _story, _hard
BLOAT_DOWN_SEQ = 8082                        # tob_bloat_sleep
BLOAT_SHADOW_GFX = {1570, 1571, 1572, 1573}  # tob_bloat_falling_flesh1..4
BLOAT_SPLAT_GFX = 1576                       # tob_bloat_blood_splat
SOTE_ATTACK_SEQS = {8138, 8139}              # tob_sotetseg_attack_melee, _ranged
XARPUS_SPIT_SEQ = 8059                       # tob_xarpus_attack_ranged
XARPUS_EXHUMED_LOC = 32743                   # tob_xarpus_exhumed
# Verzik's phase is the npc type, as in `analyse_verzik`, with the Entry
# (_story) and Hard ids beside the regular ones (sources/cache_npc_verzik.txt),
# and only her attack seqs counted: a spawn or death animation on the same
# type is not an attack.
VERZIK_PHASE_TYPES = {8370: 1, 10831: 1, 10848: 1,
                      8372: 2, 10833: 2, 10850: 2,
                      8374: 3, 10835: 3, 10852: 3}
NYLOCAS_IDS = set(range(8342, 8354))      # the twelve wave forms, 8342..8353 (cache_npc_nylocas.txt)
VERZIK_ATTACK_SEQS = {
    1: {8109},                                   # verzik_phase1_attack_magic
    2: {8114, 8116},                             # phase2 magic, melee
    3: {8123, 8124, 8125, 8126, 8127, 14406},    # phase3 melee magic ranged powerblast webspin summon
}


def unpack_coord(coord: int) -> tuple[int, int, int]:
    """ToriRSServer_CoordPack: level << 28 | x << 14 | z."""
    return (coord >> 14) & 0x3FFF, coord & 0x3FFF, (coord >> 28) & 0x3


def read_ticklog(path: str) -> list[dict]:
    rows = []
    with open(path, encoding="utf-8", errors="replace") as fh:
        header = fh.readline().rstrip("\n").split("\t")
        if not header or header[0] != TICKLOG_HEADER:
            raise SystemExit("%s: not a tick log (first field %r, want %r)"
                             % (path, header[0] if header else "", TICKLOG_HEADER))
        for line in fh:
            cols = line.rstrip("\n").split("\t")
            if len(cols) < 9:
                continue
            row = {"serial": int(cols[0]), "tick": int(cols[1]), "kind": cols[2],
                   "label": cols[9] if len(cols) > 9 else ""}
            raw = [int(c) for c in cols[3:9]]
            for name, value in zip(TICKLOG_FIELDS.get(row["kind"], ()), raw):
                row[name] = value
            for packed, prefix in (("coord", ""), ("src", "src_"), ("dst", "dst_")):
                if packed in row:
                    row[prefix + "x"], row[prefix + "z"], row[prefix + "level"] = unpack_coord(row[packed])
            rows.append(row)
    return rows


class Segment:
    """One stretch of a tick log measured as one room: `anchor` is its room
    tick 0 (blert's tick 0, the room start), or None when nothing said."""

    def __init__(self, label: str, anchor: int | None, rows: list[dict]) -> None:
        self.label, self.anchor, self.rows = label, anchor, rows

    def kind(self, *kinds: str) -> list[dict]:
        return [r for r in self.rows if r["kind"] in kinds]

    def room_tick(self, tick: int) -> int | None:
        return None if self.anchor is None else tick - self.anchor


def segments(rows: list[dict], start_ticks: list[int], mark_re: str | None,
             offset: int) -> list[Segment]:
    """Cut the log into rooms. A scratch run that re-enters a room many times
    (spec_bloat_walks_*: sixty rooms in one log, a mark after each start) is
    many segments; a room test's log is usually one."""
    import re
    cuts: list[tuple[int, str]] = []
    if start_ticks:
        cuts = [(t, "start-tick %d" % t) for t in sorted(start_ticks)]
    elif mark_re is not None:
        pattern = re.compile(mark_re)
        cuts = [(r["tick"], "mark %r at tick %d" % (r["label"], r["tick"]))
                for r in rows if r["kind"] == "mark" and pattern.search(r["label"])]
        if not cuts:
            raise SystemExit("no mark row matches %r" % mark_re)
    if not cuts:
        return [Segment("whole log, no room-start anchor", None, rows)]
    out = []
    for i, (tick, label) in enumerate(cuts):
        end = cuts[i + 1][0] if i + 1 < len(cuts) else None
        body = [r for r in rows if r["tick"] >= tick and (end is None or r["tick"] < end)]
        out.append(Segment(label + ("" if not offset else " %+d" % offset), tick + offset, body))
    return out


def anim_ticks(seg: Segment, seqs: set[int]) -> tuple[int | None, list[int]]:
    """The slot with the most rows of these seqs, and its distinct ticks."""
    by_slot: dict[int, list[int]] = collections.defaultdict(list)
    for r in seg.kind("npc_anim"):
        if r["seq"] in seqs:
            by_slot[r["slot"]].append(r["tick"])
    if not by_slot:
        return None, []
    slot = max(by_slot, key=lambda s: len(by_slot[s]))
    return slot, sorted(set(by_slot[slot]))


def gaps_of(ticks: list[int]) -> list[int]:
    return [b - a for a, b in zip(ticks, ticks[1:])]


def loc_lifetimes(seg: Segment, loc_id: int) -> list[tuple[int, int, int, int]]:
    """(x, z, first tick, lifetime) per presence of `loc_id` on a tile: from
    the add that put it there to the `-1` that took it away. A re-add on an
    occupied tile (a re-covered trail) restarts the server's revert but not
    the presence, which is the same thing a recording's run measures."""
    open_at: dict[int, int] = {}
    out = []
    for r in seg.kind("loc_set"):
        if r["loc"] == loc_id:
            open_at.setdefault(r["coord"], r["tick"])
        elif r["loc"] == -1 and r["coord"] in open_at:
            born = open_at.pop(r["coord"])
            out.append((r["x"], r["z"], born, r["tick"] - born))
    return out


def tl_maiden(segs: list[Segment], k: dict, rep: Report) -> None:
    gaps, firsts, splats, trails = [], [], [], []
    for seg in segs:
        _, ticks = anim_ticks(seg, MAIDEN_ATTACK_SEQS)
        if not ticks:
            continue
        gaps += gaps_of(ticks)
        firsts.append(seg.room_tick(ticks[0]))
        slug_at = set()
        for r in seg.kind("npc_spawn", "npc_tile"):
            if r["type"] in MAIDEN_BLOOD_SPAWN_IDS:
                slug_at.add((r["tick"], r["x"], r["z"]))
        for x, z, born, life in loc_lifetimes(seg, MAIDEN_POOL_LOC):
            if any((born - back, x, z) in slug_at for back in (0, 1)):
                trails.append(life)
            else:
                splats.append(life)
    if not gaps and not firsts:
        return
    rep.check("maiden attack period", summary(gaps), k["tob_maiden_attack_ticks"],
              bool(gaps) and mode_of(gaps) == k["tob_maiden_attack_ticks"],
              "npc_anim %s on the Maiden's slot" % "/".join(map(str, sorted(MAIDEN_ATTACK_SEQS))))
    first_check(rep, "maiden first attack", firsts, k["tob_maiden_first_attack_ticks"],
                "room tick of her first attack animation")
    if splats:
        rep.check("blood splat drawn for", summary(splats), k["tob_maiden_blood_splat_ticks"],
                  mode_of(splats) == k["tob_maiden_blood_splat_ticks"],
                  "loc %d add -> remove, no blood spawn on the tile" % MAIDEN_POOL_LOC)
    if trails:
        rep.check("blood trail lifetime", summary(trails), k["tob_maiden_blood_trail_ticks"],
                  mode_of(trails) == k["tob_maiden_blood_trail_ticks"],
                  "loc %d laid under a blood spawn; longer = re-covered" % MAIDEN_POOL_LOC)


def tl_bloat(segs: list[Segment], k: dict, rep: Report) -> None:
    down_to_up, down_to_stomp, walks, first_walks = [], [], [], []
    hand_gaps, hand_delays, downs_seen = [], [], 0
    for seg in segs:
        slot, downs = anim_ticks(seg, {BLOAT_DOWN_SEQ})
        if slot is None:
            continue
        downs_seen += len(downs)
        moves = sorted({r["tick"] for r in seg.kind("npc_tile") if r["slot"] == slot})
        shadows = sorted({r["tick"] for r in seg.kind("map_spotanim") if r["spotanim"] in BLOAT_SHADOW_GFX})
        drops = sorted({r["tick"] for r in seg.kind("map_spotanim") if r["spotanim"] == BLOAT_SPLAT_GFX})
        # Every hit the Bloat deals carries its slot: the flies, the hands and
        # the stomp. A down stops the flies, but the ones already in the air
        # land after it (a projectile launched on tick p lands on p +
        # ceil(end_cycle / 30)), and a hand hits on its drop tick (Hard drops
        # through downs); what is left inside the down is the stomp.
        flies = [(r["tick"], r["tick"] + -(-r["end_cycle"] // 30)) for r in seg.kind("projectile")]
        hand_ticks = set(drops) | {t + 1 for t in drops}
        hits = sorted({r["tick"] for r in seg.kind("hit_player") if r["npc_slot"] == slot})
        up_prev = None
        for i, d in enumerate(downs):
            # blert's walkTime is down - up - 1 (its up event is the first
            # step); the first walk counts from room tick 0.
            if i == 0:
                if seg.anchor is not None:
                    first_walks.append(d - seg.anchor - 1)
            elif up_prev is not None:
                walks.append(d - up_prev - 1)
            nxt = downs[i + 1] if i + 1 < len(downs) else None
            up = next((m for m in moves if m > d and (nxt is None or m < nxt)), None)
            if up is not None:
                down_to_up.append(up - d)
            up_prev = up
            in_air = max([land for launch, land in flies if launch <= d] or [d])
            hit = [t for t in hits if max(d, in_air) < t < d + k["tob_bloat_up_offset"]
                   and t not in hand_ticks]
            if hit:
                down_to_stomp.append(hit[0] - d)
        hand_gaps += gaps_of(drops)
        for t in shadows:
            later = [x for x in drops if x >= t]
            if later:
                hand_delays.append(later[0] - t)
    if not downs_seen:
        return
    rep.check("bloat down -> up", summary(down_to_up), k["tob_bloat_up_offset"],
              bool(down_to_up) and mode_of(down_to_up) == k["tob_bloat_up_offset"],
              "npc_anim %d to the slot's next npc_tile" % BLOAT_DOWN_SEQ)
    if down_to_stomp:
        rep.check("bloat down -> stomp", summary(down_to_stomp), k["tob_bloat_stomp_offset"],
                  mode_of(down_to_stomp) == k["tob_bloat_stomp_offset"],
                  "first hit_player from the Bloat inside the down")
    else:
        rep.note("bloat down -> stomp", "no stomp hit", "a stomp shows only as a hit on a player in range")
    lo, hi = k["tob_bloat_walk_min"], k["tob_bloat_walk_max"]
    top = hi + k["tob_bloat_walk_unattacked_bonus"]
    if walks:
        out = [w for w in walks if not lo <= w <= top]
        rep.check("bloat walk (later)", "%s min=%d max=%d" % (summary(walks), min(walks), max(walks)),
                  "%d..%d" % (lo, top), not out,
                  "up -> next down; outside: %s" % (out[:6] or "none"))
    delay = k["tob_bloat_walk_first_bonus"]
    if first_walks:
        out = [w for w in first_walks if not lo + delay <= w <= top + delay]
        rep.check("bloat walk (first)",
                  "%s min=%d max=%d" % (summary(first_walks), min(first_walks), max(first_walks)),
                  "%d..%d" % (lo + delay, top + delay), not out,
                  "room tick 0 -> first down; outside: %s" % (out[:6] or "none"))
    else:
        rep.note("bloat walk (first)", "no room-start anchor",
                 "pass --start-tick or --start-mark")
    bloat_hand_checks(rep, k, hand_gaps, hand_delays)


def tl_sotetseg(segs: list[Segment], k: dict, rep: Report) -> None:
    gaps = []
    for seg in segs:
        _, ticks = anim_ticks(seg, SOTE_ATTACK_SEQS)
        if ticks:
            gaps += gaps_of(ticks)
    if gaps:
        rep.check("sotetseg period", summary(gaps), k["tob_sote_attack_ticks"],
                  mode_of(gaps) == k["tob_sote_attack_ticks"],
                  "npc_anim %s" % "/".join(map(str, sorted(SOTE_ATTACK_SEQS))))


def tl_xarpus(segs: list[Segment], k: dict, rep: Report, hard: bool, scale: int) -> None:
    spit, lifetimes, spawn_gaps, firsts, seen = [], [], [], [], False
    for seg in segs:
        _, ticks = anim_ticks(seg, {XARPUS_SPIT_SEQ})
        spit += gaps_of(ticks)
        ex = loc_lifetimes(seg, XARPUS_EXHUMED_LOC)
        born = sorted({r["tick"] for r in seg.kind("loc_set") if r["loc"] == XARPUS_EXHUMED_LOC})
        if ticks or born:
            seen = True
        lifetimes += [life for _, _, _, life in ex]
        spawn_gaps += gaps_of(born)
        if born:
            firsts.append(seg.room_tick(born[0]))
    if not seen:
        return
    if spit:
        rep.check("xarpus spit period", summary(spit), k["tob_xarpus_spit_ticks"],
                  mode_of(spit) == k["tob_xarpus_spit_ticks"], "npc_anim %d" % XARPUS_SPIT_SEQ)
    xarpus_exhumed_checks(rep, k, lifetimes, spawn_gaps, firsts, hard=hard, scale=scale)


def tl_verzik(segs: list[Segment], k: dict, rep: Report) -> None:
    per_phase: dict[int, list[int]] = {1: [], 2: [], 3: []}
    seen = False
    for seg in segs:
        buckets: dict[int, set[int]] = collections.defaultdict(set)
        for r in seg.kind("npc_anim"):
            phase = VERZIK_PHASE_TYPES.get(r["type"])
            if phase and r["seq"] in VERZIK_ATTACK_SEQS[phase]:
                buckets[phase].add(r["tick"])
        for phase, ticks in buckets.items():
            seen = True
            per_phase[phase] += gaps_of(sorted(ticks))
    if not seen:
        return
    for phase, key in ((1, "tob_verzik_p1_attack_ticks"),
                       (2, "tob_verzik_p2_attack_ticks"),
                       (3, "tob_verzik_p3_attack_ticks")):
        gaps = per_phase[phase]
        if not gaps:
            rep.note("verzik p%d period" % phase, "no attacks", "phase not reached in this log")
            continue
        rep.check("verzik p%d period" % phase, summary(gaps), k[key], mode_of(gaps) == k[key],
                  "attack seqs on the phase's npc type")


def ticklog_main(args, k: dict) -> int:
    rows = read_ticklog(args.ticklog)
    segs = segments(rows, args.start_tick or [], args.start_mark, args.anchor_offset)
    ticks = [r["tick"] for r in rows]
    print("ticklog %s: %d rows, ticks %s..%s, %d segment(s)"
          % (args.ticklog, len(rows), min(ticks) if ticks else "-", max(ticks) if ticks else "-",
             len(segs)))
    for seg in segs[:6]:
        print("  %s: room tick 0 = %s, %d rows" % (seg.label, seg.anchor, len(seg.rows)))
    if len(segs) > 6:
        print("  ... %d more" % (len(segs) - 6))
    print()
    rep = Report()
    tl_maiden(segs, k, rep)
    tl_bloat(segs, k, rep)
    tl_sotetseg(segs, k, rep)
    tl_xarpus(segs, k, rep, hard=args.mode == "hard", scale=args.scale)
    tl_verzik(segs, k, rep)
    nylo = sum(1 for r in rows if r["kind"] == "npc_spawn" and r["type"] in NYLOCAS_IDS)
    if nylo:
        rep.note("nylocas waves", "%d nylocas spawn row(s)" % nylo,
                 "not measured from a tick log: a spawn row carries no wave number")
    if not rep.rows:
        print("no Theatre room found in this log (no Maiden/Bloat/Sotetseg/Xarpus/Verzik rows)")
        return 2
    bad = rep.render()
    print()
    print("%d mismatch(es)" % bad)
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cache", default=os.path.join(tempfile.gettempdir(), "blert_tob_events"))
    ap.add_argument("--fetch", type=int, metavar="N", help="download N recent raids first")
    ap.add_argument("--ticklog", metavar="PATH",
                    help="measure a quest-driver run's ticklog.tsv instead of the blert cache")
    ap.add_argument("--start-tick", type=int, action="append", metavar="N",
                    help="--ticklog: server tick of a room start (room tick 0); repeat for several rooms")
    ap.add_argument("--start-mark", metavar="REGEX",
                    help="--ticklog: every mark row whose label matches starts a room at its tick")
    ap.add_argument("--anchor-offset", type=int, default=0, metavar="N",
                    help="--ticklog: add N to each anchor (a mark written after chat.play returns "
                         "lands one tick after the server's room start: -1)")
    ap.add_argument("--mode", choices=("entry", "normal", "hard"), default="normal",
                    help="--ticklog: the room's mode, for the constants that have a Hard figure")
    ap.add_argument("--scale", type=int, default=1, choices=range(1, 6), metavar="1-5",
                    help="--ticklog: party size, for the constants that move with it")
    args = ap.parse_args()

    if args.ticklog:
        return ticklog_main(args, constants())

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
        analyse_xarpus(rooms["xarpus"], k, rep)
    if rooms["verzik"]:
        analyse_verzik(rooms["verzik"], k, rep)

    print()
    bad = rep.render()
    print()
    print("%d mismatch(es)" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
