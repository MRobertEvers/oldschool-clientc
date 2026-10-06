#!/usr/bin/env python3
"""The PER-WAVE SCRIPT of one Theatre of Blood room: what each role of a
successful real team did in every wave, from the cached Blert streams.

The owner, 2026-10-06: "Blert literally tells you what you need to do."  Every
room fixer re-derived the raiders' per-wave play from the raw streams by hand.
This writes it once, as a table a plan can be filled from and raid_report.py
--waves can diff a run against:

    python3 tools/raid_gate/blert_script.py maiden --mode normal --scale 3
    python3 tools/raid_gate/blert_script.py nylocas --mode normal --scale 3 [--out X.json]

    -> docs/minigames/theater_of_blood/sources/blert_api/reference/<room>_<mode>_<scale>.script.json

It never touches the network: it reads the streams blert_reference.py cached
(build/blert/<room>/, and build/blert_maiden/ for Maiden) and uses the rooms
the room's reference (<room>_<mode>_<scale>.json) was built from when that file
lists them, so the script and the statistical reference describe the same
teams.  A missing stream is skipped and counted (fetch it with
blert_reference.py, one request per three seconds).

SEGMENTS ("waves").  Maiden: "100" (room start to the 70% crabs), "70", "50",
"30" (each from its crab spawn to the next spawn or her death).  Nylocas:
"w1".."w31" (each from its wave spawn to the next wave), "cleanup" (wave 31 to
the cleanup end), "boss" (the boss spawn to the room end).  The other rooms:
Blert's own phase events (bloat downN/walkN, sotetseg mazeN/after_mazeN,
xarpus/verzik phaseN), each to the next.

Every tick offset is from the SEGMENT's start tick (a wave's spawn tick).
Every tile is relative to the room ANCHOR (see ANCHORS): Maiden = her
south-west tile; Nylocas = the Vasilias' spawn tile (a fixed square of the
room, the same in every instance because instances keep region-local tiles);
the other rooms = the boss's first tile.

The whole record is per segment, per role (classify_roles in
blert_reference.py: dps1 / dps2 / freezer at Maiden, mage / range / melee ...
elsewhere), each number a {median, min, max, n} over the rooms; see the README
section "Per-wave scripts" in the reference directory for every field.
"""

import argparse
import collections
import glob
import json
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blert_reference as br  # noqa: E402

REFERENCE_DIR = br.REFERENCE_DIR
EXTRA_CACHES = {"maiden": [os.path.join(br.REPO_ROOT, "build", "blert_maiden")]}

# Region-local (x % 64, y % 64) tiles, measured off the streams.  Maiden's crab
# lanes are Blert's MaidenCrabPosition 0-9 (0 S1, 1 N1, 2 S2, 3 N2, 4 S3, 5 N3,
# 6 S4 inner, 7 S4 outer, 8 N4 inner, 9 N4 outer); a scuffed spawn is a step
# east or out, so a tile within 2 of a lane is that lane.
MAIDEN_LANES = {"S1": (37, 20), "N1": (37, 40), "S2": (41, 20), "N2": (41, 40), "S3": (45, 20),
                "N3": (45, 40), "S4in": (49, 22), "S4out": (49, 20), "N4in": (49, 38), "N4out": (49, 40)}
MAIDEN_POSITION_NAMES = ["S1", "N1", "S2", "N2", "S3", "N3", "S4in", "S4out", "N4in", "N4out"]
# Nylocas: Blert's spawnType 1 west, 2 south, 3 east.
NYLO_LANES = {"W": (17, 24), "S": (32, 9), "E": (46, 25)}
NYLO_SPAWN_TYPE = {1: "W", 2: "S", 3: "E"}
NYLO_STYLE = {8342: "melee", 8343: "range", 8344: "mage", 8345: "melee", 8346: "range", 8347: "mage",
              10774: "melee", 10775: "range", 10776: "mage", 10777: "melee", 10778: "range", 10779: "mage",
              10791: "melee", 10792: "range", 10793: "mage", 10794: "melee", 10795: "range", 10796: "mage"}
NYLO_BIG = {8345, 8346, 8347, 10777, 10778, 10779, 10794, 10795, 10796}
NYLO_BOSS_LOCAL = (30, 23)
LANE_REACH = {"maiden": 2, "nylocas": 3}
MAX_ACTIONS = 8   # casts / targets kept per role per segment


def lane_of(room, local, npc):
    """The lane label of a spawn at a region-local tile, or None."""
    lanes = MAIDEN_LANES if room == "maiden" else NYLO_LANES if room == "nylocas" else {}
    best, best_d = None, None
    for name, tile in lanes.items():
        d = max(abs(tile[0] - local[0]), abs(tile[1] - local[1]))
        if best_d is None or d < best_d:
            best, best_d = name, d
    if best is None:
        return str(npc)
    if best_d > LANE_REACH.get(room, 2):
        best = "split" if room == "nylocas" else "off"
    if room == "nylocas":
        return "%s-%s%s" % (best, NYLO_STYLE.get(npc, str(npc)), "-big" if npc in NYLO_BIG else "")
    return best


def local(tile):
    return (tile[0] % 64, tile[1] % 64)


# ---------------------------------------------------------------- the ROOM
#
# Both sides reduce to one ROOM (this file reads Blert, raid_report.py reads a
# ticklog), so the script and the diff use the same arithmetic:
#   anchor (x, y)                 world tile every tile is made relative to
#   segments [(name, start, end)] end exclusive
#   adds {key: {npc, lane, index, segment, spawn}}   the room's other npcs
#   boss_keys set                 the keys that are the boss
#   leaks [(tick, crab hp or None, lane)]
#   players {name: {tiles {t: (x, y)}, prayers {t: frozenset}, weapon {t: id},
#            attacks [(t, proto, weapon, target key or None)]}}
#   roles {name: role}


def blert_room(events, room, definitions):
    events = sorted(events, key=lambda e: e["tick"])
    trace = br.blert_trace(events, room)
    boss_ids = br.BOSS_IDS.get(room)
    if boss_ids is None:
        best = {}
        for e in events:
            if e["type"] in (br.NPC_SPAWN, br.NPC_UPDATE):
                best[e["npc"]["id"]] = max(best.get(e["npc"]["id"], 0), e["npc"]["hitpoints"] & 0xFFFF)
        top = max(best.values()) if best else 0
        boss_ids = {npc for npc, hp in best.items() if hp == top}
    adds, boss_keys = {}, set()
    first_spawn = None
    segments_raw = []
    wave_of_spawn = {}
    for e in events:
        npc = e.get("npc")
        if e["type"] != br.NPC_SPAWN or not npc:
            continue
        tile = (e["xCoord"], e["yCoord"])
        if first_spawn is None:
            first_spawn = tile
        if npc["id"] in boss_ids:
            boss_keys.add(npc["roomId"])
            continue
        crab = npc.get("maidenCrab")
        if crab is not None:
            lane = MAIDEN_POSITION_NAMES[crab["position"]] if 0 <= crab["position"] < 10 else "off"
            wave_of_spawn.setdefault(crab["spawn"], e["tick"])
        else:
            lane = lane_of(room, local(tile), npc["id"])
            if room == "nylocas" and (npc.get("nylo") or {}).get("parentRoomId"):
                lane = "split-%s%s" % (NYLO_STYLE.get(npc["id"], npc["id"]), "-big" if npc["id"] in NYLO_BIG else "")
        adds[npc["roomId"]] = {"npc": npc["id"], "lane": lane, "spawn": e["tick"], "tile": tile}
    end = trace["boss_death"] if trace["boss_death"] is not None else trace["end"]
    if room == "maiden":
        boss_tile = trace["boss_tile"]
        anchor = boss_tile
        starts = [("100", 0)] + [(br.MAIDEN_WAVES[w], wave_of_spawn[w]) for w in sorted(wave_of_spawn) if w < 3]
    elif room == "nylocas":
        ref = first_spawn or (0, 0)
        anchor = (ref[0] // 64 * 64 + NYLO_BOSS_LOCAL[0], ref[1] // 64 * 64 + NYLO_BOSS_LOCAL[1])
        starts = []
        for e in events:
            if e["type"] == br.NYLO_WAVE_SPAWN:
                starts.append(("w%d" % e["nyloWave"]["wave"], e["tick"]))
                if e["nyloWave"]["wave"] == 31:
                    starts.append(("cleanup", e["tick"] + 4))
            elif e["type"] == br.NYLO_BOSS_SPAWN:
                starts.append(("boss", e["tick"]))
        end = trace["end"]
    else:
        anchor = trace["boss_tile"] or (0, 0)
        starts = trace["phases"]
    segments = segments_from(starts, end)
    players = {}
    for name, p in trace["players"].items():
        players[name] = {"tiles": p["tiles"], "prayers": {t: frozenset(v) for t, v in p["prayers"].items()},
                         "weapon": p["weapon"], "attacks": []}
    for e in events:
        if e["type"] != br.PLAYER_ATTACK:
            continue
        attack = e["attack"]
        target = attack.get("target") or {}
        key = target.get("roomId")
        if key not in adds and key not in boss_keys:
            key = None
        players[e["player"]["name"]]["attacks"].append(
            (e["tick"], attack.get("type"), (attack.get("weapon") or {}).get("id"), key))
    leaks = []
    for e in events:
        if e["type"] == br.MAIDEN_CRAB_LEAK:
            key = e["npc"]["roomId"]
            leaks.append((e["tick"], e["npc"]["hitpoints"] >> 16, adds.get(key, {}).get("lane")))
    result = {"anchor": anchor, "segments": segments, "adds": adds, "boss_keys": boss_keys,
              "leaks": leaks, "players": players}
    result["roles"] = roles_of(result, definitions, room)
    index_adds(result)
    return result


def segments_from(starts, end):
    starts = sorted(starts, key=lambda s: s[1])
    segments = []
    for index, (name, start) in enumerate(starts):
        stop = starts[index + 1][1] if index + 1 < len(starts) else end
        segments.append((name, start, max(start + 1, stop)))
    return segments


def segment_of(segments, tick):
    current = None
    for name, start, _ in segments:
        if tick >= start:
            current = name
    return current


def index_adds(room_data):
    """Each add's segment and its spawn index inside it (spawn order, then lane)."""
    per = collections.defaultdict(list)
    for key, add in room_data["adds"].items():
        add["segment"] = segment_of(room_data["segments"], add["spawn"])
        per[add["segment"]].append(key)
    for keys in per.values():
        keys.sort(key=lambda k: (room_data["adds"][k]["spawn"], room_data["adds"][k]["lane"]))
        for index, key in enumerate(keys):
            room_data["adds"][key]["index"] = index


def roles_of(room_data, definitions, room):
    """classify_roles over a minimal trace of this ROOM."""
    trace = {"players": {}}
    for name, p in room_data["players"].items():
        trace["players"][name] = {"attacks": [
            (t, proto, weapon, "boss" if key in room_data["boss_keys"] else ("add" if key is not None else "other"))
            for t, proto, weapon, key in p["attacks"]]}
    return br.classify_roles(trace, definitions, room)


# ------------------------------------------------------------- per segment

def target_label(room_data, key):
    if key is None:
        return "other"
    if key in room_data["boss_keys"]:
        return "boss"
    add = room_data["adds"][key]
    return add["lane"]


def modal(values):
    values = [v for v in values if v is not None]
    if not values:
        return None, 0.0
    value, count = collections.Counter(values).most_common(1)[0]
    return value, count / len(values)


def segment_record(room_data, player, segment, definitions):
    """One role's play in one segment of one room."""
    name, start, stop = segment
    ax, ay = room_data["anchor"]
    tiles = {t: (x - ax, y - ay) for t, (x, y) in player["tiles"].items() if start <= t < stop}
    spawn_tile = None
    for t in range(start, start + 3):
        if t in player["tiles"]:
            x, y = player["tiles"][t]
            spawn_tile = (x - ax, y - ay)
            break
    attacks = [a for a in player["attacks"] if start <= a[0] < stop]
    labels = [target_label(room_data, a[3]) for a in attacks]
    targets = []
    for label in labels:
        if not targets or targets[-1] != label:
            targets.append(label)
    first_add = next((a[0] for a in attacks if a[3] is not None and a[3] not in room_data["boss_keys"]), None)
    back = None
    if first_add is not None:
        back = next((a[0] - start for a in attacks if a[0] > first_add and a[3] in room_data["boss_keys"]), None)
    weapons = [br.weapon_family(definitions, a[2]) for a in attacks if a[2] is not None]
    if not weapons:
        held = br.series_at(player["weapon"], stop - 1) if player["weapon"] else None
        weapons = [br.weapon_family(definitions, held)] if held is not None else []
    prayers = ["+".join(sorted(v)) or "none" for t, v in player["prayers"].items() if start <= t < stop]
    tile, tile_share = modal(list(tiles.values()))
    spells = [(definitions.get(a[1]) or {}).get("name", str(a[1])) for a in attacks]
    return {"tile": tile, "tile_share": tile_share, "spawn_tile": spawn_tile,
            "first_action": (attacks[0][0] - start) if attacks else None,
            "casts": [a[0] - start for a in attacks[:MAX_ACTIONS]],
            "attacks": len(attacks), "targets": targets[:MAX_ACTIONS],
            "attack_names": spells[:MAX_ACTIONS],
            "return_to_boss": back, "weapon": modal(weapons)[0], "prayer": modal(prayers)[0],
            "first_tick": attacks[0][0] if attacks else None}


def room_records(room_data, definitions):
    """{segment name: {role: record}} plus per-segment add/leak facts."""
    out = collections.OrderedDict()
    for segment in room_data["segments"]:
        name, start, stop = segment
        roles = {}
        for player_name, player in room_data["players"].items():
            role = room_data["roles"].get(player_name, "idle")
            roles[role] = segment_record(room_data, player, segment, definitions)
        adds = sorted((a for a in room_data["adds"].values() if a.get("segment") == name),
                      key=lambda a: a["index"])
        leaks = [l for l in room_data["leaks"] if start <= l[0] < stop]
        out[name] = {"start": start, "length": stop - start, "roles": roles,
                     "adds": [(a["index"], a["npc"], a["lane"], a["spawn"] - start) for a in adds],
                     "leaks": [(l[0] - start, l[1], l[2]) for l in leaks]}
    return out


# ------------------------------------------------------------- aggregation

def aggregate_segment(records):
    """records: one room's record per room for one segment -> the script entry."""
    entry = {"n": len(records),
             "start": br.summary([r["start"] for r in records]),
             "length": br.summary([r["length"] for r in records]),
             "adds": br.summary([len(r["adds"]) for r in records]),
             "lanes": dict(collections.Counter(lane for r in records for _, _, lane, _ in r["adds"]).most_common()),
             "leaks": br.summary([len(r["leaks"]) for r in records]),
             "leak_hp": br.summary([hp for r in records for _, hp, _ in r["leaks"]]),
             "roles": {}}
    roles = sorted({role for r in records for role in r["roles"]})
    for role in roles:
        rows = [r["roles"][role] for r in records if role in r["roles"]]
        tiles = [tuple(x["tile"]) for x in rows if x["tile"] is not None]
        tile_mode, tile_share = modal(tiles)
        spawn_tiles = [tuple(x["spawn_tile"]) for x in rows if x["spawn_tile"] is not None]
        spawn_mode, spawn_share = modal(spawn_tiles)
        targets = []
        for i in range(MAX_ACTIONS):
            column = [x["targets"][i] for x in rows if len(x["targets"]) > i]
            if not column:
                break
            value, share = modal(column)
            targets.append({"i": i, "mode": value, "share": round(share, 2), "n": len(column)})
        casts = []
        for i in range(MAX_ACTIONS):
            column = [x["casts"][i] for x in rows if len(x["casts"]) > i]
            if len(column) * 2 < len(rows):
                break
            casts.append(br.summary(column))
        entry["roles"][role] = {
            "n": len(rows),
            "tile": {"mode": list(tile_mode) if tile_mode else None, "share": round(tile_share, 2),
                     "dx": br.summary([t[0] for t in tiles]), "dy": br.summary([t[1] for t in tiles])},
            "spawn_tile": {"mode": list(spawn_mode) if spawn_mode else None, "share": round(spawn_share, 2)},
            "first_action": br.summary([x["first_action"] for x in rows]),
            "no_action_share": round(sum(1 for x in rows if x["first_action"] is None) / len(rows), 2),
            "attacks": br.summary([x["attacks"] for x in rows]),
            "casts": casts,
            "targets": targets,
            "attack_names": dict(collections.Counter(n for x in rows for n in x["attack_names"]).most_common(4)),
            "return_to_boss": br.summary([x["return_to_boss"] for x in rows]),
            "weapon": dict(collections.Counter(x["weapon"] for x in rows if x["weapon"]).most_common(3)),
            "prayer": dict(collections.Counter(x["prayer"] for x in rows if x["prayer"]).most_common(3)),
        }
    return entry


def stream_paths(room, mode_code, scale, uuids):
    dirs = [os.path.join(br.CACHE_ROOT, room)] + EXTRA_CACHES.get(room, [])
    found, missing = [], []
    by_uuid = {}
    for directory in dirs:
        for meta_path in glob.glob(os.path.join(directory, "*.json.meta")):
            with open(meta_path, "r", encoding="utf-8") as handle:
                meta = json.load(handle)
            stream = meta_path[:-len(".meta")]
            if meta.get("mode") == mode_code and meta.get("scale") == scale and os.path.isfile(stream):
                by_uuid.setdefault(meta["uuid"], (meta, stream))
    if uuids:
        for uuid in uuids:
            (found if uuid in by_uuid else missing).append(uuid)
        return [by_uuid[u] for u in found], missing
    stage = br.ROOMS[room]
    chosen = [v for v in by_uuid.values() if br.room_completed(v[0], stage) and br.room_deaths(v[0], stage) == 0]
    return chosen, missing


def script_path(room, mode, scale):
    return os.path.join(REFERENCE_DIR, "%s_%s_%d.script.json" % (room, mode, scale))


def build_script(room, mode, scale):
    definitions = br.load_attack_definitions()
    reference = br.reference_path(room, mode, scale)
    uuids = None
    if os.path.isfile(reference):
        with open(reference, "r", encoding="utf-8") as handle:
            uuids = json.load(handle).get("selection", {}).get("uuids")
    chosen, missing = stream_paths(room, br.MODES[mode], scale, uuids)
    assert chosen, "no cached %s %s scale %d streams (run blert_reference.py first)" % (room, mode, scale)
    per_segment = collections.OrderedDict()
    anchors = []
    for meta, stream in chosen:
        with open(stream, "r", encoding="utf-8") as handle:
            events = json.load(handle)
        room_data = blert_room(events, room, definitions)
        anchors.append(room_data["anchor"])
        for name, record in room_records(room_data, definitions).items():
            per_segment.setdefault(name, []).append(record)
    order = sorted(per_segment, key=lambda n: statistics.median(r["start"] for r in per_segment[n]))
    lanes = MAIDEN_LANES if room == "maiden" else NYLO_LANES if room == "nylocas" else {}
    ax, ay = local(anchors[0])
    return {"kind": "blert-script-v1", "room": room, "mode": mode, "scale": scale,
            "anchor": {"what": ANCHORS.get(room, "the boss's first tile"), "region_local": [ax, ay]},
            "lanes": {name: [x - ax, y - ay] for name, (x, y) in lanes.items()},
            "rooms": len(chosen), "missing_streams": missing, "uuids": [m["uuid"] for m, _ in chosen],
            "segments": [dict(name=name, **aggregate_segment(per_segment[name])) for name in order]}


ANCHORS = {"maiden": "Maiden's south-west tile",
           "nylocas": "the Nylocas Vasilias' spawn tile (region-local 30,23)"}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("room", choices=sorted(br.ROOMS))
    parser.add_argument("--mode", required=True, choices=sorted(br.MODES))
    parser.add_argument("--scale", required=True, type=int, choices=range(1, 6))
    parser.add_argument("--out", default=None, help="where to write (default: the reference directory)")
    parser.add_argument("--offline", action="store_true", help="accepted for symmetry; this tool never fetches")
    args = parser.parse_args()
    script = build_script(args.room, args.mode, args.scale)
    out = args.out or script_path(args.room, args.mode, args.scale)
    with open(out, "w", encoding="utf-8") as handle:
        json.dump(script, handle, indent=1, sort_keys=False)
        handle.write("\n")
    print("blert_script.py: %s -- %d rooms, %d segments, %d streams missing"
          % (os.path.relpath(out, br.REPO_ROOT), script["rooms"], len(script["segments"]),
             len(script["missing_streams"])))


if __name__ == "__main__":
    main()
