#!/usr/bin/env python3
"""What successful real teams do in one Theatre of Blood room, from Blert.

The owner, 2026-10-06: "Can you just try to make the runs follow what the
Blert successful runs do?"  This harvests N successful rooms of one room, mode
and scale from blert.io (https://blert.io, a volunteer-run recorder: one
request per three seconds, every stream cached and never fetched twice), and
writes one reference file a plan can be held against:

    docs/minigames/theater_of_blood/sources/blert_api/reference/<room>_<mode>_<scale>.json
    docs/minigames/theater_of_blood/sources/blert_api/reference/README.md   (one table per file)

    python3 tools/raid_gate/blert_reference.py maiden --mode normal --scale 3 --rooms 26
    python3 tools/raid_gate/blert_reference.py maiden --mode entry --scale 1
    python3 tools/raid_gate/blert_reference.py nylocas --mode normal --scale 3 --offline

Streams are cached as build/blert/<room>/<uuid>.json, each beside the
challenge-list entry it came from (<uuid>.json.meta).  --offline never touches
the network (it uses what the cache holds).

"Successful room" = the raid got past the room (status COMPLETED, or a later
stage reached) and nobody died in it.  When every candidate room of the mode
has a death, the rooms with deaths are used and the reference says how many.

Every number is a per-room measurement; the reference stores, per number, its
median, min, max and n over the rooms (raid_report.py --against compares our
own run's same number to that range).  The groups:

  outcome.*   room ticks, the boss's death tick, each phase's start and length,
              hitpoints lost per recorder, heals the boss got, leaks, deaths,
              and the boss's hitpoints lost per tick in each phase (output.*)
  role.<R>.*  per role (who casts, who kills: see classify_roles): attacks per
              phase on the boss and on the adds, weapon ids per phase, the gap
              between attacks, the protection prayer against each boss attack
              kind (share right, ticks lit before it was sent), food/drink
              (recorder only), distance to the boss and to the nearest raider;
              positions (offsets from the boss's SW tile) are in "positions"
  react.*     ticks from a room event (a spawn wave = a phase start, a boss
              attack) to each role's first response (attack, attack on a new
              npc, weapon swap, step)
  boss.*      the boss's attacks by kind, cadence, first attack; hits on the
              recorders by kind (max, median) for the spec tables

What Blert cannot see, and the reference therefore never claims: another
player's hitpoints or food (only the recorder's own -- "source 0" -- carry
hitpoints and prayer points); what was eaten (an eat is a hitpoints rise of 3
or more in one tick, a drink a prayer rise of 7 or more); the damage of any
one hit (a hit on a recorder is the hitpoints drop in the six ticks after the
attack was sent, approximate).
"""

import argparse
import collections
import datetime
import glob
import json
import os
import statistics
import sys
import time
import urllib.error
import urllib.request

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REFERENCE_DIR = os.path.join(REPO_ROOT, "docs", "minigames", "theater_of_blood", "sources",
                             "blert_api", "reference")
CACHE_ROOT = os.path.join(REPO_ROOT, "build", "blert")
BASE = "https://blert.io/api/v1"
USER_AGENT = "3draster-tob-research/1.0 (mrobertevers@gmail.com)"
REQUEST_SPACING_S = 3.0

ROOMS = {"maiden": 10, "bloat": 11, "nylocas": 12, "sotetseg": 13, "xarpus": 14, "verzik": 15}
# Blert's ChallengeMode (event.proto): TOB_ENTRY 10, TOB_REGULAR 11, TOB_HARD 12.
MODES = {"entry": 10, "normal": 11, "hard": 12}
# Blert's ChallengeStatus: 1 COMPLETED, 2 RESET, 3 WIPED.
STATUS_COMPLETED = 1

# Event types (event.proto Event.Type).
PLAYER_UPDATE, PLAYER_ATTACK, PLAYER_DEATH = 4, 5, 6
NPC_SPAWN, NPC_UPDATE, NPC_DEATH, NPC_ATTACK = 7, 8, 9, 10
MAIDEN_CRAB_LEAK = 100
BLOAT_DOWN, BLOAT_UP = 110, 111
NYLO_WAVE_SPAWN, NYLO_CLEANUP_END, NYLO_BOSS_SPAWN = 120, 122, 123
SOTE_MAZE_PROC, SOTE_MAZE_END = 130, 132
XARPUS_PHASE, VERZIK_PHASE = 140, 150

# The boss's npc ids per room, every mode (OSRS ids; our osrs239 cache uses
# the same ones).  Maiden: Entry 10814-10819, Normal 8360-8365, Hard
# 10822-10827; her Nylocas Matomenos 10820 / 8366 / 10828.
BOSS_IDS = {
    "maiden": set(range(8360, 8366)) | set(range(10814, 10820)) | set(range(10822, 10828)),
}
ADD_IDS = {
    "maiden": {8366, 10820, 10828},
}
BOSS_SIZE = {"maiden": 6, "bloat": 5}
MAIDEN_WAVES = ("70", "50", "30")

# NpcAttack (event.proto) -> (short name, the protection prayer that answers it
# or None).  Prayer names are Blert's Prayer enum bit numbers below.
NPC_ATTACKS = {
    1: ("maiden_auto", "magic"), 2: ("maiden_blood", None), 3: ("bloat_stomp", None),
    4: ("nylo_melee", "melee"), 5: ("nylo_range", "missiles"), 6: ("nylo_mage", "magic"),
    7: ("sote_melee", "melee"), 8: ("sote_ball", None), 9: ("sote_death_ball", None),
    10: ("xarpus_spit", None), 11: ("xarpus_turn", None), 12: ("verzik_p1_auto", None),
    13: ("verzik_p2_bounce", None), 14: ("verzik_p2_cabbage", None), 15: ("verzik_p2_zap", None),
    16: ("verzik_p2_purple", None), 17: ("verzik_p2_mage", "magic"), 18: ("verzik_p3_auto", None),
    19: ("verzik_p3_melee", "melee"), 20: ("verzik_p3_range", "missiles"),
    21: ("verzik_p3_mage", "magic"), 22: ("verzik_p3_webs", None), 23: ("verzik_p3_yellows", None),
    24: ("verzik_p3_ball", None),
}
# Blert's Prayer enum (common/prayer): the bit of each protection prayer in a
# prayerSet (measured: 0x10000 is lit on 2/3 of a Maiden room's ticks, the
# blackstorm being magic; 0x8000000 rigour, 0x10000000 augury, 0x4000000 piety).
BLERT_PROTECTION_BITS = {"magic": 16, "missiles": 17, "melee": 18}

DEFINITIONS = os.path.join(REFERENCE_DIR, "attack_definitions.json")
EAT_RISE = 3          # hitpoints rise in one tick that is food, not regeneration
DRINK_RISE = 7        # prayer rise in one tick that is a restore, not nothing
HIT_WINDOW = 6        # ticks after a boss attack is sent in which its hit lands
REACTION_WINDOW = 20  # a response later than this is no response to the event


def load_attack_definitions():
    """Blert's attack_definitions.json (copied from its repository): protoId ->
    {name, category, weaponIds, animationIds, cooldown}."""
    with open(DEFINITIONS, "r", encoding="utf-8") as handle:
        return {entry["protoId"]: entry for entry in json.load(handle)}


def is_barrage(definition):
    return definition is not None and "BARRAGE" in definition["name"]


_FAMILY_SUFFIXES = ("_SPEC", "_AUTO", "_BARRAGE", "_BASH", "_SWIPE", "_UNCHARGED", "_SCRATCH", "_SMACK")
_families = {}
# Attack-table entries that are spells cast from any staff, not a weapon.
_SPELLS_NOT_WEAPONS = {"ICE_RUSH", "DARK_DEMONBANE"}


def weapon_family(definitions, weapon):
    """A weapon id's name as Blert's attack table knows it, variants merged
    (22325 / 25736 / 25739 are all SCYTHE; 27624 / 27626 / 25491 SCEPTRE).
    An id the table does not list stays its number."""
    if not _families:
        for definition in definitions.values():
            family = definition["name"]
            if family in _SPELLS_NOT_WEAPONS or family.startswith("UNKNOWN"):
                continue
            for suffix in _FAMILY_SUFFIXES:
                if family.endswith(suffix):
                    family = family[: -len(suffix)]
            for weapon_id in definition.get("weaponIds", []):
                if weapon_id >= 0:
                    _families.setdefault(weapon_id, set()).add(family)
    names = _families.get(weapon)
    return "/".join(sorted(names)) if names else str(weapon)


# ---------------------------------------------------------------- statistics

def summary(values):
    values = [v for v in values if v is not None]
    if not values:
        return None
    return {"median": round(statistics.median(values), 3), "min": min(values), "max": max(values),
            "n": len(values)}


def median_or_none(values):
    values = [v for v in values if v is not None]
    return statistics.median(values) if values else None


def chebyshev_to_footprint(tile, sw, size):
    x, y = tile
    dx = max(sw[0] - x, 0, x - (sw[0] + size - 1))
    dy = max(sw[1] - y, 0, y - (sw[1] + size - 1))
    return max(dx, dy)


# ------------------------------------------------------------------- harvest

_last_request = [0.0]


def fetch_json(url):
    """GET one Blert URL at most once every REQUEST_SPACING_S seconds; None on
    a 404.  Backs off on 429 and network errors (volunteer-run service)."""
    for attempt in range(4):
        wait = _last_request[0] + REQUEST_SPACING_S - time.monotonic()
        if wait > 0:
            time.sleep(wait)
        _last_request[0] = time.monotonic()
        try:
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(request, timeout=90) as response:
                return json.loads(response.read().decode())
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return None
            time.sleep(30 * (attempt + 1))
        except (urllib.error.URLError, TimeoutError, ConnectionError):
            time.sleep(30 * (attempt + 1))
    sys.exit("blert_reference.py: %s failed four times" % url)


def room_deaths(meta, stage):
    return sum(1 for player in meta.get("party", []) for death in player.get("deaths", [])
               if death == stage)


def room_completed(meta, stage):
    return meta.get("status") == STATUS_COMPLETED and meta.get("stage", 0) >= stage \
        or meta.get("stage", 0) > stage


def cached_rooms(cache_dir, mode_code, scale):
    """(meta, stream path) for every cached stream of this mode and scale,
    newest first."""
    found = []
    for meta_path in glob.glob(os.path.join(cache_dir, "*.json.meta")):
        with open(meta_path, "r", encoding="utf-8") as handle:
            meta = json.load(handle)
        stream = meta_path[:-len(".meta")]
        if meta.get("mode") == mode_code and meta.get("scale") == scale and os.path.isfile(stream):
            found.append((meta, stream))
    found.sort(key=lambda item: item[0].get("startTime", ""), reverse=True)
    return found


def harvest(room, mode_code, scale, want, offline):
    """Fill build/blert/<room>/ until it holds `want` completed rooms of this
    mode and scale with no death in the room (or the list runs out).  Returns
    the cache directory and what the listing found."""
    stage = ROOMS[room]
    cache_dir = os.path.join(CACHE_ROOT, room)
    os.makedirs(cache_dir, exist_ok=True)
    have = [m for m, _ in cached_rooms(cache_dir, mode_code, scale)
            if room_completed(m, stage) and room_deaths(m, stage) == 0]
    listed = {"pages": 0, "challenges": 0, "completed_room": 0}
    if offline or len(have) >= want:
        return cache_dir, listed
    cursor = None
    clean = len(have)
    while clean < want:
        url = "%s/challenges?limit=50&type=1&mode=%d&scale=eq%d" % (BASE, mode_code, scale)
        if cursor:
            url += "&startTime=lt%d" % cursor
        page = fetch_json(url)
        listed["pages"] += 1
        if not page:
            break
        listed["challenges"] += len(page)
        last = page[-1]["startTime"].replace("Z", "+00:00")
        cursor = int(datetime.datetime.fromisoformat(last).timestamp() * 1000)
        for challenge in page:
            if not room_completed(challenge, stage):
                continue
            listed["completed_room"] += 1
            stream = os.path.join(cache_dir, challenge["uuid"] + ".json")
            empty = stream + ".empty"  # Blert recorded nothing of this room: never ask again
            if os.path.isfile(empty):
                continue
            if not os.path.isfile(stream):
                events = fetch_json("%s/raids/tob/%s/events?stage=%d" % (BASE, challenge["uuid"], stage))
                if not events:
                    with open(empty, "w", encoding="utf-8") as handle:
                        json.dump(challenge, handle)
                    continue
                with open(stream, "w", encoding="utf-8") as handle:
                    json.dump(events, handle)
                print("blert_reference.py: fetched %s (%d events)" % (challenge["uuid"][:8], len(events)),
                      flush=True)
            with open(stream + ".meta", "w", encoding="utf-8") as handle:
                json.dump(challenge, handle)
            if room_deaths(challenge, stage) == 0:
                clean += 1
                if clean >= want:
                    break
        if len(page) < 50:
            break
    return cache_dir, listed


def select_rooms(cache_dir, room, mode_code, scale, want):
    """The rooms the reference is built from: completed, no death in the room,
    newest first, at most `want`; when NO candidate is death-free, the rooms
    with deaths instead (the reference says so)."""
    stage = ROOMS[room]
    candidates = [(m, s) for m, s in cached_rooms(cache_dir, mode_code, scale) if room_completed(m, stage)]
    clean = [(m, s) for m, s in candidates if room_deaths(m, stage) == 0]
    chosen = clean[:want] if clean else candidates[:want]
    selection = {"candidates": len(candidates), "death_free": len(clean), "used": len(chosen),
                 "used_with_deaths": sum(1 for m, _ in chosen if room_deaths(m, stage) > 0),
                 "uuids": [m["uuid"] for m, _ in chosen]}
    return chosen, selection


# --------------------------------------------------------------------- trace
#
# Both sides -- a Blert stream here, our ticklog.tsv in raid_report.py -- are
# first reduced to one TRACE, and every number is computed from a trace by
# room_numbers(), so "ours" and "theirs" are the same arithmetic:
#
#   start, end, boss_death        ticks (start = the room's tick 0)
#   boss_base, boss_size, boss_tile   base hitpoints, footprint, SW tile
#   boss_damage [(tick, hp)]      hitpoints the boss lost (Blert: drops in her
#                                 hitpoints series; ours: hit_npc rows)
#   boss_heals [(tick, hp)]       hitpoints she gained
#   boss_attacks [(tick, npc attack id, target player or None)]
#   phases [(name, start tick)]   in order
#   adds {key: spawn tick}        the room's other npcs (crabs, nylos)
#   leaks [tick]
#   players {name: {tiles {t:(x,y)}, prayers {t:set}, hp {t:(cur,base)} or None,
#            prayer_points {t:cur} or None, weapon {t:id},
#            attacks [(t, protoId or None, weapon, "boss"|"add"|"other")],
#            taken [(t, hp)] or None, deaths [t], consumes [(t, "eat"|"drink", hp pct)]}}


def new_player():
    return {"tiles": {}, "prayers": {}, "hp": None, "prayer_points": None, "weapon": {},
            "attacks": [], "taken": None, "deaths": [], "consumes": []}


def blert_trace(events, room):
    """A trace from one Blert stage stream."""
    events = sorted(events, key=lambda e: e["tick"])
    boss_ids = BOSS_IDS.get(room)
    add_ids = ADD_IDS.get(room, set())
    if boss_ids is None:
        # Any other room: the boss is the npc with the largest base hitpoints.
        best = {}
        for e in events:
            if e["type"] in (NPC_SPAWN, NPC_UPDATE):
                best[e["npc"]["id"]] = max(best.get(e["npc"]["id"], 0), e["npc"]["hitpoints"] & 0xFFFF)
        top = max(best.values()) if best else 0
        boss_ids = {npc for npc, hp in best.items() if hp == top}
        add_ids = set(best) - boss_ids
    trace = {"start": 0, "end": events[-1]["tick"] if events else 0, "boss_death": None,
             "boss_base": None, "boss_size": BOSS_SIZE.get(room, 1), "boss_tile": None,
             "boss_damage": [], "boss_heals": [], "boss_attacks": [], "phases": [],
             "adds": {}, "leaks": [], "players": collections.defaultdict(new_player)}
    boss_rooms = set()
    previous_boss_hp = None
    waves = {}
    for e in events:
        t = e["tick"]
        kind = e["type"]
        npc = e.get("npc")
        if npc and npc["id"] in boss_ids and kind in (NPC_SPAWN, NPC_UPDATE, NPC_DEATH):
            boss_rooms.add(npc["roomId"])
            current, base = npc["hitpoints"] >> 16, npc["hitpoints"] & 0xFFFF
            if base:
                trace["boss_base"] = max(trace["boss_base"] or 0, base)
            if trace["boss_tile"] is None:
                trace["boss_tile"] = (e["xCoord"], e["yCoord"])
            if kind != NPC_DEATH and previous_boss_hp is not None and base:
                if current < previous_boss_hp:
                    trace["boss_damage"].append((t, previous_boss_hp - current))
                elif current > previous_boss_hp:
                    trace["boss_heals"].append((t, current - previous_boss_hp))
            if kind != NPC_DEATH and base:
                previous_boss_hp = current
            if kind == NPC_DEATH and trace["boss_death"] is None:
                trace["boss_death"] = t
        elif npc and kind == NPC_SPAWN and (npc["id"] in add_ids or npc["id"] not in boss_ids):
            trace["adds"].setdefault(npc["roomId"], t)
            crab = npc.get("maidenCrab")
            if crab is not None:
                waves.setdefault(crab["spawn"], t)
        if kind == NPC_ATTACK and npc and npc["id"] in boss_ids:
            trace["boss_attacks"].append((t, e["npcAttack"]["attack"], e["npcAttack"].get("target")))
        elif kind == MAIDEN_CRAB_LEAK:
            trace["leaks"].append(t)
        elif kind == PLAYER_UPDATE:
            p = e["player"]
            player = trace["players"][p["name"]]
            player["tiles"][t] = (e["xCoord"], e["yCoord"])
            bits = p.get("prayerSet", 0)
            player["prayers"][t] = {name for name, bit in BLERT_PROTECTION_BITS.items() if (bits >> bit) & 1}
            if "hitpoints" in p:
                if player["hp"] is None:
                    player["hp"], player["prayer_points"], player["taken"] = {}, {}, []
                player["hp"][t] = (p["hitpoints"] >> 16, p["hitpoints"] & 0xFFFF)
                if "prayer" in p:
                    player["prayer_points"][t] = p["prayer"] >> 16
            for delta in p.get("equipmentDeltas", []):
                if (delta >> 48) == 4 and (delta >> 31) & 1:
                    player["weapon"][t] = (delta >> 32) & 0xFFFF
        elif kind == PLAYER_ATTACK:
            attack = e["attack"]
            weapon = (attack.get("weapon") or {}).get("id")
            trace["players"][e["player"]["name"]]["attacks"].append(
                (t, attack.get("type"), weapon, attack.get("target") or {}))
        elif kind == PLAYER_DEATH:
            trace["players"][e["player"]["name"]]["deaths"].append(t)
    # Targets resolve after the pass: a barrage pre-aimed at a spawn tile is
    # logged on the tick its crab spawns, sometimes before the spawn event.
    for player in trace["players"].values():
        resolved = []
        for t, proto, weapon, target in player["attacks"]:
            where = "other"
            if target.get("id") in boss_ids or target.get("roomId") in boss_rooms:
                where = "boss"
            elif target.get("roomId") in trace["adds"]:
                where = "add"
            resolved.append((t, proto, weapon, where))
        player["attacks"] = resolved
    if trace["boss_death"] is None and trace["boss_damage"]:
        trace["boss_death"] = trace["end"]
    # Phases: Maiden by her crab waves (Blert's maidenCrab.spawn 0/1/2 =
    # 70/50/30 percent), the other rooms by their own phase events.
    if room == "maiden":
        trace["phases"] = [("100", 0)] + [(MAIDEN_WAVES[w], waves[w]) for w in sorted(waves) if w < 3]
    else:
        trace["phases"] = blert_phases(events, room)
    for player in trace["players"].values():
        derive_recorder(player)
    trace["players"] = dict(trace["players"])
    return trace


def blert_phases(events, room):
    """Phase starts for the rooms other than Maiden, from Blert's room events."""
    phases = [("start", 0)]
    if room == "bloat":
        for e in events:
            if e["type"] == BLOAT_DOWN:
                phases.append(("down%d" % (sum(1 for n, _ in phases if n.startswith("down")) + 1), e["tick"]))
            elif e["type"] == BLOAT_UP:
                phases.append(("walk%d" % (sum(1 for n, _ in phases if n.startswith("walk")) + 2), e["tick"]))
    elif room == "nylocas":
        for e in events:
            if e["type"] == NYLO_WAVE_SPAWN:
                wave = (e.get("nyloWave") or {}).get("wave")
                phases.append(("wave%s" % wave, e["tick"]))
            elif e["type"] == NYLO_CLEANUP_END:
                phases.append(("cleanup_end", e["tick"]))
            elif e["type"] == NYLO_BOSS_SPAWN:
                phases.append(("boss", e["tick"]))
    elif room == "sotetseg":
        for e in events:
            if e["type"] == SOTE_MAZE_PROC:
                phases.append(("maze%d" % (sum(1 for n, _ in phases if n.startswith("maze")) + 1), e["tick"]))
            elif e["type"] == SOTE_MAZE_END:
                phases.append(("after_maze%d" % sum(1 for n, _ in phases if n.startswith("maze")), e["tick"]))
    elif room in ("xarpus", "verzik"):
        code = XARPUS_PHASE if room == "xarpus" else VERZIK_PHASE
        for e in events:
            if e["type"] == code:
                value = e.get("xarpusPhase", e.get("verzikPhase"))
                phases.append(("phase%s" % value, e["tick"]))
    return phases


def derive_recorder(player):
    """A recorder's hitpoints series -> damage taken and eats; its prayer
    series -> drinks."""
    if player["hp"] is None:
        return
    ticks = sorted(player["hp"])
    for earlier, later in zip(ticks, ticks[1:]):
        before, base = player["hp"][earlier]
        after = player["hp"][later][0]
        if after < before:
            player["taken"].append((later, before - after))
        elif after - before >= EAT_RISE:
            player["consumes"].append((later, "eat", round(100.0 * before / max(1, base))))
    ticks = sorted(player["prayer_points"])
    for earlier, later in zip(ticks, ticks[1:]):
        if player["prayer_points"][later] - player["prayer_points"][earlier] >= DRINK_RISE:
            current, base = player["hp"].get(later, (0, 1))
            player["consumes"].append((later, "drink", round(100.0 * current / max(1, base))))


# -------------------------------------------------------------------- roles

# How the raiders who are not the freezer are named, per room.  "dps": by
# attacks on the boss, most first (dps1, dps2) -- Maiden, where 18 of 24 real
# trios put two scythes on her and 6 a scythe and a bow, so naming by style
# would split one job in two; "style": by the most used attack category
# (melee / range / mage, numbered by attack count when two share one).
ROLE_SCHEME = {"maiden": "dps"}


def classify_roles(trace, definitions, room=None):
    """name -> role, by what each raider DID (not who they are):
      solo     a party of one;
      freezer  the raider whose attacks are most often a barrage, when at least
               a third of them are (Maiden's crab freezer);
      the rest by ROLE_SCHEME (dps1, dps2 / melee, range1, range2 ...).
    Who TANKS is a number, not a role: role.R.boss_targeted_pct.  A raider with
    no attack at all is 'idle'."""
    names = sorted(trace["players"])
    if len(names) == 1:
        return {names[0]: "solo"}
    roles = {}
    counts = {}
    for name in names:
        attacks = trace["players"][name]["attacks"]
        categories = collections.Counter()
        barrage = 0
        for _, proto, _, _ in attacks:
            definition = definitions.get(proto)
            if definition:
                categories[definition["category"]] += 1
            barrage += 1 if is_barrage(definition) else 0
        counts[name] = (len(attacks), categories, barrage)
    freezers = [n for n in names if counts[n][0] and counts[n][2] * 3 >= counts[n][0] and counts[n][2] >= 3]
    if freezers:
        roles[max(freezers, key=lambda n: counts[n][2])] = "freezer"
    by_style = collections.defaultdict(list)
    for name in names:
        if name in roles:
            continue
        total, categories, _ = counts[name]
        if not total or not categories:
            roles[name] = "idle"
            continue
        if ROLE_SCHEME.get(room) == "dps":
            by_style["dps"].append(name)
            continue
        style = {"MELEE": "melee", "RANGED": "range", "MAGIC": "mage"}[categories.most_common(1)[0][0]]
        by_style[style].append(name)
    on_boss = {n: sum(1 for a in trace["players"][n]["attacks"] if a[3] == "boss") for n in names}
    if by_style.get("dps"):
        by_style["dps"].sort(key=lambda n: -on_boss[n])
        for index, name in enumerate(by_style.pop("dps")):
            roles[name] = "dps%d" % (index + 1)
    for style, members in by_style.items():
        members.sort(key=lambda n: -counts[n][0])
        for index, name in enumerate(members):
            roles[name] = style if len(members) == 1 else "%s%d" % (style, index + 1)
    return roles


# ------------------------------------------------------------------ numbers

def series_at(series, tick):
    """The value of a {tick: value} series at `tick` (the last one at or before)."""
    best = None
    for t in series:
        if t <= tick and (best is None or t > best):
            best = t
    return series[best] if best is not None else None


def phase_of(phases, tick):
    current = phases[0][0] if phases else "room"
    for name, start in phases:
        if tick >= start:
            current = name
    return current


def phase_spans(trace):
    """[(name, start, end)] with end exclusive (the next start, or her death)."""
    end = trace["boss_death"] if trace["boss_death"] is not None else trace["end"]
    spans = []
    for index, (name, start) in enumerate(trace["phases"]):
        stop = trace["phases"][index + 1][1] if index + 1 < len(trace["phases"]) else end
        spans.append((name, start, max(start, stop)))
    return spans


def first_after(ticks, tick, window=REACTION_WINDOW):
    later = [t for t in ticks if tick <= t <= tick + window]
    return (min(later) - tick) if later else None


def changes(series):
    """Ticks on which a {tick: value} series changed value."""
    ticks = sorted(series)
    return [later for earlier, later in zip(ticks, ticks[1:]) if series[later] != series[earlier]]


def room_numbers(trace, room, definitions):
    """Every number of one room: {key: value or [samples]}, plus the weapon and
    position tallies and the role map."""
    numbers = {}
    weapons = collections.defaultdict(collections.Counter)    # "role|phase" -> weapon id -> count
    positions = collections.defaultdict(collections.Counter)  # "role|phase" -> "dx,dy" -> ticks
    roles = classify_roles(trace, definitions, room)
    start = trace["start"]
    spans = phase_spans(trace)
    base = trace["boss_base"] or 0
    numbers["outcome.room_ticks"] = trace["end"] - start
    if trace["boss_death"] is not None:
        numbers["outcome.boss_death_tick"] = trace["boss_death"] - start
    numbers["outcome.boss_heal"] = sum(hp for _, hp in trace["boss_heals"])
    numbers["outcome.leaks"] = len(trace["leaks"])
    numbers["outcome.deaths"] = sum(len(p["deaths"]) for p in trace["players"].values())
    for name, first, stop in spans:
        ticks = stop - first
        key = "outcome.phase.%s." % name
        numbers[key + "start"] = first - start
        numbers[key + "ticks"] = ticks
        numbers[key + "leaks"] = sum(1 for t in trace["leaks"] if first <= t < stop)
        numbers[key + "boss_heal"] = sum(hp for t, hp in trace["boss_heals"] if first <= t < stop)
        dealt = sum(hp for t, hp in trace["boss_damage"] if first <= t < stop)
        if ticks > 0:
            numbers["output.phase.%s.boss_hp_per_tick" % name] = round(dealt / ticks, 2)
            if base:
                numbers["output.phase.%s.boss_pct_per_tick" % name] = round(100.0 * dealt / base / ticks, 3)
    attacks_by_tick = {}
    for name, player in trace["players"].items():
        role = roles[name]
        if player["taken"] is not None:
            numbers["outcome.hp_lost.%s" % role] = sum(hp for _, hp in player["taken"])
        attack_ticks = [a[0] for a in player["attacks"]]
        attacks_by_tick[name] = attack_ticks
        styles = collections.Counter((definitions.get(a[1]) or {}).get("category", "UNKNOWN")
                                     for a in player["attacks"])
        if player["attacks"]:
            for category in ("MELEE", "RANGED", "MAGIC"):
                numbers["role.%s.%s_pct" % (role, category.lower())] = round(
                    100.0 * styles[category] / len(player["attacks"]), 1)
            numbers["role.%s.barrage_pct" % role] = round(100.0 * sum(
                1 for a in player["attacks"] if is_barrage(definitions.get(a[1]))) / len(player["attacks"]), 1)
        targeted = [a for a in trace["boss_attacks"] if a[2] is not None]
        if targeted and len(trace["players"]) > 1:
            numbers["role.%s.boss_targeted_pct" % role] = round(
                100.0 * sum(1 for a in targeted if a[2] == name) / len(targeted), 1)
        gaps = [b - a for a, b in zip(attack_ticks, attack_ticks[1:]) if b - a <= 12]
        if gaps:
            numbers["role.%s.cadence" % role] = statistics.median(gaps)
        per_weapon = collections.defaultdict(list)
        for (t0, _, w0, _), (t1, _, w1, _) in zip(player["attacks"], player["attacks"][1:]):
            if w0 == w1 and t1 - t0 <= 12:
                per_weapon[w0].append(t1 - t0)
        for weapon, values in per_weapon.items():
            numbers["role.%s.cadence.%s" % (role, weapon)] = statistics.median(values)
        for phase, first, stop in spans:
            key = "role.%s.phase.%s." % (role, phase)
            inside = [a for a in player["attacks"] if first <= a[0] < stop]
            numbers[key + "attacks_boss"] = sum(1 for a in inside if a[3] == "boss")
            numbers[key + "attacks_add"] = sum(1 for a in inside if a[3] == "add")
            for _, _, weapon, _ in inside:
                weapons["%s|%s" % (role, phase)][weapon_family(definitions, weapon)] += 1
            if player["hp"] is not None:
                numbers[key + "eats"] = sum(1 for c in player["consumes"] if c[1] == "eat" and first <= c[0] < stop)
                numbers[key + "drinks"] = sum(1 for c in player["consumes"] if c[1] == "drink" and first <= c[0] < stop)
            boss_distance, raider_distance = [], []
            for t, tile in player["tiles"].items():
                if not first <= t < stop:
                    continue
                if trace["boss_tile"] is not None:
                    sw = trace["boss_tile"]
                    positions["%s|%s" % (role, phase)]["%d,%d" % (tile[0] - sw[0], tile[1] - sw[1])] += 1
                    boss_distance.append(chebyshev_to_footprint(tile, sw, trace["boss_size"]))
                others = [o["tiles"].get(t) for n, o in trace["players"].items() if n != name]
                others = [o for o in others if o is not None]
                if others:
                    raider_distance.append(min(max(abs(o[0] - tile[0]), abs(o[1] - tile[1])) for o in others))
            if boss_distance:
                numbers[key + "dist_boss"] = statistics.median(boss_distance)
            if raider_distance:
                numbers[key + "dist_raider"] = statistics.median(raider_distance)
        eat_hp = [c[2] for c in player["consumes"] if c[1] == "eat"]
        if eat_hp:
            numbers["role.%s.eat_at_hp_pct" % role] = eat_hp
    # The boss: attacks by kind, cadence, the protection prayer against each.
    attack_ticks = [t for t, _, _ in trace["boss_attacks"]]
    if attack_ticks:
        numbers["boss.first_attack"] = attack_ticks[0] - start
        gaps = [b - a for a, b in zip(attack_ticks, attack_ticks[1:])]
        if gaps:
            numbers["boss.cadence"] = statistics.median(gaps)
    by_kind = collections.Counter(NPC_ATTACKS.get(k, ("attack%d" % k, None))[0] for _, k, _ in trace["boss_attacks"])
    for kind, count in by_kind.items():
        numbers["boss.attacks.%s" % kind] = count
    prayer = collections.defaultdict(lambda: [0, 0, []])  # "role|kind" -> right, total, lit
    hits = collections.defaultdict(list)
    for index, (t, kind_id, target) in enumerate(trace["boss_attacks"]):
        kind, protection = NPC_ATTACKS.get(kind_id, ("attack%d" % kind_id, None))
        player = trace["players"].get(target)
        if player is None:
            continue
        role = roles[target]
        if protection:
            entry = prayer["%s|%s" % (role, kind)]
            entry[1] += 1
            lit = series_at(player["prayers"], t) or set()
            if protection in lit:
                entry[0] += 1
                run = 0
                while run < 50 and protection in (player["prayers"].get(t - run - 1) or set()):
                    run += 1
                entry[2].append(run)
        if player["taken"] is not None:
            nxt = [u for u, _, who in trace["boss_attacks"][index + 1:] if who == target]
            stop = min([t + HIT_WINDOW] + [u for u in nxt])
            hits[kind].append(sum(hp for u, hp in player["taken"] if t < u <= stop))
        # Ticks after the attack was sent (1 = the next tick) to the target's
        # first step and first prayer change.
        step = first_after(changes(player["tiles"]), t + 1)
        if step is not None:
            numbers.setdefault("react.boss.%s.%s.step" % (kind, role), []).append(step + 1)
        swap = first_after(changes({u: frozenset(v) for u, v in player["prayers"].items()}), t + 1)
        if swap is not None:
            numbers.setdefault("react.boss.%s.%s.prayer" % (kind, role), []).append(swap + 1)
    for key, (right, total, lit) in prayer.items():
        role, kind = key.split("|")
        numbers["role.%s.prayer.%s.right_pct" % (role, kind)] = round(100.0 * right / total, 1)
        if lit:
            numbers["role.%s.prayer.%s.lit_ticks" % (role, kind)] = statistics.median(lit)
    for kind, values in hits.items():
        numbers["boss.hit_on_recorder.%s" % kind] = values
    # Reactions to each phase start (Maiden: a crab wave spawning).
    for phase, first, _ in spans[1:]:
        new_adds = [t for t in trace["adds"].values() if first - 1 <= t <= first + 2]
        for name, player in trace["players"].items():
            role = roles[name]
            key = "react.phase.%s.%s." % (phase, role)
            values = {
                "attack": first_after([a[0] for a in player["attacks"]], first),
                "attack_add": first_after([a[0] for a in player["attacks"] if a[3] == "add"], first)
                if new_adds else None,
                "swap": first_after(changes(player["weapon"]), first),
                "step": first_after(changes(player["tiles"]), first),
            }
            for response, value in values.items():
                if value is not None:
                    numbers[key + response] = value
    # Per-room medians for the sample lists that are per-event, not per-hit.
    for key in list(numbers):
        if key.startswith("react.boss.") and isinstance(numbers[key], list):
            numbers[key] = statistics.median(numbers[key])
    return numbers, weapons, positions, roles


# --------------------------------------------------------------- aggregate

def aggregate(per_room):
    """per_room: [(numbers, weapons, positions, roles)] -> the reference body."""
    values = collections.defaultdict(list)
    for numbers, _, _, _ in per_room:
        for key, value in numbers.items():
            values[key].extend(value if isinstance(value, list) else [value])
    numbers = {key: summary(v) for key, v in sorted(values.items())}
    weapons = {}
    rooms_with = collections.defaultdict(collections.Counter)
    counts = collections.defaultdict(lambda: collections.defaultdict(list))
    for _, room_weapons, _, _ in per_room:
        for slot, tally in room_weapons.items():
            for weapon, count in tally.items():
                rooms_with[slot][weapon] += 1
                counts[slot][weapon].append(count)
    for slot in sorted(rooms_with):
        weapons[slot] = {w: {"rooms": rooms_with[slot][w], "count": summary(counts[slot][w])}
                         for w, _ in rooms_with[slot].most_common()}
    positions = {}
    merged = collections.defaultdict(collections.Counter)
    for _, _, room_positions, _ in per_room:
        for slot, tally in room_positions.items():
            merged[slot].update(tally)
    for slot in sorted(merged):
        total = sum(merged[slot].values())
        positions[slot] = [{"offset": o, "ticks": c, "share": round(c / total, 3)}
                           for o, c in merged[slot].most_common(8)]
    role_counts = collections.Counter(tuple(sorted(r.values())) for _, _, _, r in per_room)
    return numbers, weapons, positions, [{"roles": list(k), "rooms": c} for k, c in role_counts.most_common()]


# ------------------------------------------------------------------ output

def reference_path(room, mode, scale):
    return os.path.join(REFERENCE_DIR, "%s_%s_%d.json" % (room, mode, scale))


def fmt(entry):
    if entry is None:
        return "-"
    return "%g [%g-%g] n=%d" % (entry["median"], entry["min"], entry["max"], entry["n"])


def readme_section(reference, definitions):
    """The README.md table for one reference file."""
    numbers = reference["numbers"]
    lines = ["## %s, %s, scale %d" % (reference["room"].capitalize(), reference["mode"], reference["scale"]), ""]
    sel = reference["selection"]
    lines.append("%d rooms (%d candidates, %d death-free, %d used with a death in the room); harvested %s; "
                 "file `%s`." % (sel["used"], sel["candidates"], sel["death_free"], sel["used_with_deaths"],
                                 reference["harvested"], os.path.basename(reference_path(
                                     reference["room"], reference["mode"], reference["scale"]))))
    lines.append("Roles seen (by what each raider did): " + "; ".join(
        "%s x%d" % ("+".join(r["roles"]), r["rooms"]) for r in reference["role_sets"]) + ".")
    lines += ["", "| Number | median [min-max] n |", "|---|---|"]
    shown = [k for k in numbers if k.startswith(("outcome.", "output.", "boss."))]
    shown += [k for k in numbers if k.startswith("role.") and (".prayer." in k or k.count(".") == 2
                                                                or k.endswith(("attacks_boss", "attacks_add",
                                                                               "dist_boss", "eats")))]
    shown += [k for k in numbers if k.startswith("react.phase.")]
    for key in shown:
        lines.append("| `%s` | %s |" % (key, fmt(numbers[key])))
    lines += ["", "Weapons per role and phase (Blert's weapon name, variants merged, or the id it does not know: "
              "rooms using it / median attacks in those rooms):", ""]
    for slot, tally in reference["weapons"].items():
        top = list(tally.items())[:4]
        lines.append("- `%s`: %s" % (slot, ", ".join("%s: %d / %g" % (w, v["rooms"], v["count"]["median"])
                                                    for w, v in top)))
    lines += ["", "Where each role stands (offset from the boss's SW tile, share of the phase's ticks, top 3):", ""]
    for slot, offsets in reference["positions"].items():
        lines.append("- `%s`: %s" % (slot, ", ".join("(%s) %.0f%%" % (o["offset"], 100 * o["share"])
                                                    for o in offsets[:3])))
    return "\n".join(lines) + "\n"


README_HEAD = """# Blert references: what successful real teams do, per room, mode and scale

Written by `tools/raid_gate/blert_reference.py` (do not edit by hand: rerun it).  One JSON
per room/mode/scale beside this file; `tools/raid_gate/raid_report.py RUN --against
<file>` prints a run's same numbers against the reference's median and range and flags
every number outside the range.  Streams are cached under `build/blert/<room>/` (not
committed).  `attack_definitions.json` and `spell_definitions.json` are Blert's own
(its repository), the table the attack and weapon names come from.

Key: `outcome.*` the room; `output.phase.P.boss_pct_per_tick` the boss's hitpoints lost
per tick in phase P as a percent of her base (the damage output); `role.R.*` per role
(`solo`; `freezer`; Maiden's others `dps1`, `dps2` by attacks on her, other rooms' `melee`/`range`/`mage` numbered by attack count); `react.*` ticks from
an event to a role's first response; `boss.*` the boss's attacks.  What Blert cannot see:
another raider's hitpoints, what was eaten, the damage of one hit (a hit on a recorder is
its hitpoints drop in the six ticks after the attack, approximate); see the tool's
docstring.

## Per-wave scripts (`<room>_<mode>_<scale>.script.json`)

Written by `tools/raid_gate/blert_script.py <room> --mode M --scale S` from the same cached
streams and the same rooms (the reference's `selection.uuids`); read by
`tools/raid_gate/raid_report.py RUN --waves <file> [--wave N] [--roles p0=ROLE,...]`, which
aligns each of a run's waves on its own spawn tick and names, per role, the first tick
the run left the script (tile / late / none / target / return).  Every number is
`{median, min, max, n}` over the rooms.

- `anchor`: what tiles are relative to (Maiden: her SW tile; Nylocas: the Vasilias' spawn
  tile, region-local 30,23; other rooms: the boss's first tile). `lanes`: each spawn lane's
  tile relative to the anchor (Maiden N1..N4out / S1..S4out, Blert's crab positions;
  Nylocas W / S / E).
- `segments[]` in room order. Maiden `100`, `70`, `50`, `30`; Nylocas `w1`..`w31`,
  `cleanup`, `boss`; other rooms Blert's phase events. `start` (ticks from the room's
  tick 0), `length`, `adds` (spawns in the segment), `lanes` (spawns per lane over all
  rooms; a Nylocas lane is `<lane>-<style>[-big]`, `split-*` a big one's split), `leaks`
  and `leak_hp` (Maiden: crabs reaching her, the crab's hitpoints then).
- `segments[].roles.<role>` (classify_roles: Maiden `dps1`/`dps2`/`freezer`, others
  `mage`/`range`/`melee`): `tile` (modal tile over the segment relative to the anchor:
  `mode` across rooms, its `share`, and `dx`/`dy` ranges of each room's modal tile);
  `spawn_tile` (where the role stood on the spawn tick); `first_action` (ticks from the
  spawn to its first attack) and `no_action_share`; `attacks`; `casts[i]` (the i-th
  attack's offset, kept while half the rooms have one); `targets[i]` (the i-th DISTINCT
  target in order: `boss`, a lane, or `other`, with its `share`); `attack_names`;
  `return_to_boss` (offset of the first boss attack after its first add attack);
  `weapon` and `prayer` (modal per room, counted over rooms).

"""


def write_readme(definitions):
    sections = []
    for path in sorted(glob.glob(os.path.join(REFERENCE_DIR, "*_*_*.json"))):
        with open(path, "r", encoding="utf-8") as handle:
            reference = json.load(handle)
        if isinstance(reference, dict) and reference.get("kind") == "blert-reference-v1":
            sections.append(readme_section(reference, definitions))
    with open(os.path.join(REFERENCE_DIR, "README.md"), "w", encoding="utf-8") as handle:
        handle.write(README_HEAD + "\n".join(sections))


def build_reference(room, mode, scale, want, offline):
    definitions = load_attack_definitions()
    cache_dir, listed = harvest(room, MODES[mode], scale, want, offline)
    chosen, selection = select_rooms(cache_dir, room, MODES[mode], scale, want)
    assert chosen, "no completed %s %s scale-%d room in %s" % (room, mode, scale, cache_dir)
    per_room = []
    for meta, stream in chosen:
        with open(stream, "r", encoding="utf-8") as handle:
            events = json.load(handle)
        trace = blert_trace(events, room)
        per_room.append(room_numbers(trace, room, definitions))
    numbers, weapons, positions, role_sets = aggregate(per_room)
    selection["listing"] = listed
    reference = {"kind": "blert-reference-v1", "room": room, "mode": mode, "mode_code": MODES[mode],
                 "scale": scale, "stage": ROOMS[room],
                 "harvested": datetime.date.today().isoformat(), "selection": selection,
                 "role_sets": role_sets, "numbers": numbers, "weapons": weapons, "positions": positions}
    os.makedirs(REFERENCE_DIR, exist_ok=True)
    with open(reference_path(room, mode, scale), "w", encoding="utf-8") as handle:
        json.dump(reference, handle, indent=1, sort_keys=False)
    write_readme(definitions)
    return reference


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("room", choices=sorted(ROOMS))
    parser.add_argument("--mode", required=True, choices=sorted(MODES))
    parser.add_argument("--scale", required=True, type=int, choices=range(1, 6))
    parser.add_argument("--rooms", type=int, default=26, help="successful rooms wanted (default 26)")
    parser.add_argument("--offline", action="store_true", help="use the cache only, never fetch")
    arguments = parser.parse_args()
    reference = build_reference(arguments.room, arguments.mode, arguments.scale, arguments.rooms,
                                arguments.offline)
    sel = reference["selection"]
    numbers = reference["numbers"]
    print("blert_reference.py: %s %s scale %d: %d rooms used (%d candidates, %d death-free, %d with deaths)"
          % (arguments.room, arguments.mode, arguments.scale, sel["used"], sel["candidates"],
             sel["death_free"], sel["used_with_deaths"]))
    for key in ("outcome.room_ticks", "outcome.boss_death_tick", "outcome.leaks", "outcome.deaths"):
        print("  %-28s %s" % (key, fmt(numbers.get(key))))
    print("  roles: %s" % "; ".join("%s x%d" % ("+".join(r["roles"]), r["rooms"]) for r in reference["role_sets"]))
    print("  wrote %s (%d numbers)" % (os.path.relpath(reference_path(arguments.room, arguments.mode,
                                                                      arguments.scale), REPO_ROOT), len(numbers)))


if __name__ == "__main__":
    main()
