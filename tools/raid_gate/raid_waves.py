"""raid_report.py --waves: a run's ticklog against a Blert per-wave script.

The script (blert_script.py) says, per wave and per role, where a successful
real team stood, when it first acted after the wave spawned, what it hit in
which order and when it went back to the boss.  This reduces the run's
ticklog.tsv to the same ROOM blert_script.py builds from a Blert stream --
same segments, same anchor, same lane names, same role classifier -- aligns
each wave on its own spawn tick, and names, per role, the FIRST tick where the
run left the script:

    tile      the role stood off the script's tile box (the reference rooms'
              modal-tile range, widened by TILE_SLACK) for TILE_RUN ticks
    late      its first action came after the latest real team's
    none      it did nothing in a wave where most real teams acted
    target    its i-th distinct target is not the one most real teams hit
              (only where at least TARGET_SHARE of them agree)
    return    it went back to the boss later than the latest real team

A role the run has and the script does not (or the other way round) is named.
"""

import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blert_reference as br  # noqa: E402
import blert_script as bs  # noqa: E402

TILE_SLACK = 1
TILE_RUN = 3
TARGET_SHARE = 0.5
BYTE_BUDGET = 4000
NYLO_BOSS_IDS = {8354, 8355, 8356, 8357, 10786, 10787, 10788, 10789, 10807, 10808, 10809, 10810}


def ticklog_room(rows, room, definitions, report):
    """The ROOM (blert_script.py) of one run's ticklog rows."""
    unpack = report.unpack
    marks = [row["tick"] for row in rows if row["kind"] == "mark" and row.get("label") == "room start"]
    start = marks[0] if marks else rows[0]["tick"]
    boss_ids = br.BOSS_IDS.get(room, NYLO_BOSS_IDS if room == "nylocas" else set())
    adds, boss_keys, slot_key = {}, set(), {}
    anchor = None
    lane_ticks = []
    boss_spawn = None
    end = rows[-1]["tick"]
    for row in rows:
        if row["kind"] != "npc_spawn":
            continue
        tile = unpack(row["coord"])
        key = (row["slot"], row["tick"])
        if row.get("type") in boss_ids:
            if anchor is None and room != "nylocas":
                anchor = tile
            boss_keys.add(key)
            slot_key[row["slot"]] = key
            if boss_spawn is None and row["tick"] >= start:
                boss_spawn = row["tick"]
            continue
        if room == "nylocas" and row.get("type") in bs.NYLO_STYLE:
            if anchor is None:
                anchor = (tile[0] // 64 * 64 + bs.NYLO_BOSS_LOCAL[0], tile[1] // 64 * 64 + bs.NYLO_BOSS_LOCAL[1])
        if anchor is None or max(abs(tile[0] - anchor[0]), abs(tile[1] - anchor[1])) > 40:
            continue
        if room == "maiden" and row.get("type") not in br.ADD_IDS["maiden"]:
            continue
        if room == "nylocas" and row.get("type") not in bs.NYLO_STYLE:
            continue
        lane = bs.lane_of(room, bs.local(tile), row.get("type"))
        adds[key] = {"npc": row.get("type"), "lane": lane, "spawn": row["tick"], "tile": tile}
        slot_key[row["slot"]] = key
        if row["tick"] >= start and not lane.startswith("split"):
            lane_ticks.append(row["tick"])
    assert anchor is not None, "no %s anchor (boss or lane spawn) in the ticklog" % room
    deaths = [row["tick"] for row in rows if row["kind"] == "npc_death" and
              slot_key.get(row.get("slot")) in boss_keys and row["tick"] >= start]
    if room == "maiden":
        end = deaths[0] if deaths else end
        waves = []
        for t in sorted(set(lane_ticks)):
            if not waves or t - waves[-1][-1] > report.ADD_WAVE_GAP:
                waves.append([t])
            else:
                waves[-1].append(t)
        starts = [("100", start)] + [(br.MAIDEN_WAVES[i], w[0]) for i, w in enumerate(waves[:3])]
    elif room == "nylocas":
        starts = [("w%d" % (i + 1), t) for i, t in enumerate(sorted(set(lane_ticks))[:31])]
        if len(starts) == 31:
            starts.append(("cleanup", starts[-1][1] + 4))
        if boss_spawn is not None:
            starts.append(("boss", boss_spawn))
        end = deaths[-1] if deaths else end
    else:
        starts = [("start", start)]
    segments = bs.segments_from(starts, end)
    players = collections.defaultdict(lambda: {"tiles": {}, "prayers": {}, "weapon": {}, "attacks": []})
    bits = report.read_protection_bits()
    by_animation = report.attack_by_animation(definitions)
    weapon_now, target_now = {}, {}
    alive = {}
    for row in rows:
        t, kind = row["tick"], row["kind"]
        if kind == "npc_spawn":
            alive[row["slot"]] = (row["slot"], t)
        elif kind == "raider":
            name = "p%d" % row["pid"]
            p = players[name]
            p["prayers"][t] = frozenset(n.replace("protectfrom", "") for n in
                                        report.lit_protections(row.get("prayers", -1), bits))
            if weapon_now.get(name) != row.get("weapon"):
                p["weapon"][t] = row.get("weapon")
                weapon_now[name] = row.get("weapon")
            target_now[name] = row.get("tgt", -1)
        elif kind == "player_tile":
            players["p%d" % row["pid"]]["tiles"][t] = (row.get("x"), row.get("z"))
        elif kind == "player_anim" and t >= start:
            name = "p%d" % row["pid"]
            seq = report.ANIMATION_ALIASES.get(row.get("seq"), row.get("seq"))
            if seq == report.DEATH_ANIM:
                continue
            proto = report.pick_attack(by_animation.get(seq, []), weapon_now.get(name))
            if proto is None:
                continue
            key = alive.get(target_now.get(name, -1))
            if key not in adds and key not in boss_keys:
                key = None
            players[name]["attacks"].append((t, proto, weapon_now.get(name), key))
    # Leaks: the content's own row (tob_maiden.rs2 [proc,tob_maiden_leak_heal]:
    # an npc_heal on her, amount = twice the crab's hitpoints unless her
    # maximum clamped it).  A log written before that row existed labels the
    # heal [proc,tob_maiden_heal_found], shared with the blood splats; there a
    # heal on the tick a crab died is counted as the leak.
    crab_death_ticks = collections.Counter(row["tick"] for row in rows if row["kind"] == "npc_death"
                                           and row.get("type") in br.ADD_IDS.get("maiden", set()))
    leaks = []
    for row in rows:
        if row["kind"] != "npc_heal" or slot_key.get(row.get("slot")) not in boss_keys:
            continue
        label = row.get("label", "")
        if "leak" in label or ("heal_found" in label and crab_death_ticks.get(row["tick"])):
            clamped = row.get("hp") == row.get("base")
            leaks.append((row["tick"], None if clamped else row.get("amount", 0) // 2, None))
    result = {"anchor": anchor, "segments": segments, "adds": adds, "boss_keys": boss_keys,
              "leaks": leaks, "players": dict(players)}
    result["roles"] = bs.roles_of(result, definitions, room)
    bs.index_adds(result)
    return result


def in_box(value, entry):
    return entry is None or entry["min"] - TILE_SLACK <= value <= entry["max"] + TILE_SLACK


def first_deviation(ours, ref, player, room_data, segment):
    """[(tick, kind, text)] for one role in one wave, earliest first."""
    name, start, stop = segment
    found = []
    tile = ref["tile"]
    if tile["dx"] and player is not None:
        ax, ay = room_data["anchor"]
        run = 0
        for t in range(start, stop):
            where = player["tiles"].get(t)
            if where is None:
                where = br.series_at(player["tiles"], t)
            if where is None:
                continue
            dx, dy = where[0] - ax, where[1] - ay
            run = run + 1 if not (in_box(dx, tile["dx"]) and in_box(dy, tile["dy"])) else 0
            if run >= TILE_RUN:
                found.append((t - TILE_RUN + 1, "tile", "at %d,%d, script %s dx %d..%d dy %d..%d" % (
                    dx, dy, tile["mode"], tile["dx"]["min"], tile["dx"]["max"], tile["dy"]["min"], tile["dy"]["max"])))
                break
    first = ref["first_action"]
    if first:
        if ours["first_action"] is None and ref["no_action_share"] < 0.5:
            found.append((start + first["max"] + 1, "none", "no action, script first +%g [%d-%d]" % (
                first["median"], first["min"], first["max"])))
        elif ours["first_action"] is not None and ours["first_action"] > first["max"]:
            found.append((start + first["max"] + 1, "late", "first +%d, script +%g [%d-%d]" % (
                ours["first_action"], first["median"], first["min"], first["max"])))
    for i, target in enumerate(ref["targets"]):
        if i >= len(ours["targets"]):
            break
        if target["share"] >= TARGET_SHARE and ours["targets"][i] != target["mode"]:
            casts = [a for a in player["attacks"] if start <= a[0] < stop] if player else []
            labels, tick = [], start
            for a in casts:
                label = bs.target_label(room_data, a[3])
                if not labels or labels[-1] != label:
                    labels.append(label)
                    if len(labels) == i + 1:
                        tick = a[0]
                        break
            found.append((tick, "target", "#%d %s, script %s (%d%%)" % (
                i + 1, ours["targets"][i], target["mode"], round(100 * target["share"]))))
            break
    back = ref["return_to_boss"]
    if back and ours["return_to_boss"] is not None and ours["return_to_boss"] > back["max"]:
        found.append((start + back["max"] + 1, "return", "back +%d, script +%g [%d-%d]" % (
            ours["return_to_boss"], back["median"], back["min"], back["max"])))
    elif back and ours["return_to_boss"] is None and ours["first_action"] is not None \
            and any(t != "boss" for t in ours["targets"]):
        found.append((start + back["max"] + 1, "return", "never back on the boss, script +%g" % back["median"]))
    return sorted(found)


def fmt_summary(entry):
    if not entry:
        return "-"
    return "%g[%g-%g]" % (entry["median"], entry["min"], entry["max"])


def map_roles(room_data, script, overrides):
    """Our players onto the script's roles: --roles first (p0=mage), then the
    same role name, then the same style with the number dropped (mage1 ->
    mage), then whoever is left by attack count, most first."""
    wanted = sorted({role for s in script["segments"] for role in s["roles"]})
    mapping = {}
    for item in filter(None, (overrides or "").split(",")):
        player, _, role = item.partition("=")
        assert role in wanted, "--roles %s: the script has no role %s (it has %s)" % (item, role, wanted)
        mapping[player] = role
    ours = {name: role for name, role in room_data["roles"].items() if name not in mapping}
    free = [r for r in wanted if r not in mapping.values()]
    for name, role in sorted(ours.items()):
        if role in free:
            mapping[name] = role
            free.remove(role)
    for name, role in sorted(ours.items()):
        base = role.rstrip("0123456789")
        if name not in mapping:
            same = [r for r in free if r.rstrip("0123456789") == base]
            if same:
                mapping[name] = same[0]
                free.remove(same[0])
    rest = sorted((n for n in ours if n not in mapping),
                  key=lambda n: -len(room_data["players"].get(n, {}).get("attacks", [])))
    for name in rest:
        mapping[name] = free.pop(0) if free else room_data["roles"][name]
    return mapping


def wave_lines(run_directory, script, report, only_wave=None, overrides=None):
    rows = report.read_ticklog(run_directory)
    room = script["room"]
    found = report.run_room(rows)
    assert found == room, "the run is in %s, the script is %s (--room to say which)" % (found, room)
    definitions = br.load_attack_definitions()
    room_data = ticklog_room(rows, room, definitions, report)
    classified = dict(room_data["roles"])
    room_data["roles"] = map_roles(room_data, script, overrides)
    ours = bs.room_records(room_data, definitions)
    by_role = {role: name for name, role in room_data["roles"].items()}
    lines = ["== waves %s vs %s (%d rooms); roles %s" % (
        os.path.basename(os.path.normpath(run_directory)), os.path.basename(script.get("_path", "script")),
        script["rooms"], " ".join("%s=%s%s" % (r, n, "" if classified.get(n) == r else "(played %s)" % classified.get(n))
                                  for r, n in sorted(by_role.items())))]
    segments = {s[0]: s for s in room_data["segments"]}
    reference = {s["name"]: s for s in script["segments"]}
    for name in [s["name"] for s in script["segments"]] + [n for n in ours if n not in reference]:
        if only_wave is not None and name != only_wave and name != "w%s" % only_wave:
            continue
        ref = reference.get(name)
        mine = ours.get(name)
        if mine is None:
            lines.append("%-7s MISSING in the run (script start +%s)" % (name, fmt_summary(ref["start"])))
            continue
        if ref is None:
            lines.append("%-7s t%d not in the script" % (name, mine["start"]))
            continue
        segment = segments[name]
        head = "%-5s t%d len %d/%s adds %d/%s" % (
            name, mine["start"], mine["length"], fmt_summary(ref["length"]), len(mine["adds"]),
            fmt_summary(ref["adds"]))
        if room == "maiden":
            head += " leaks %d/%s" % (len(mine["leaks"]), fmt_summary(ref["leaks"]))
        verdicts = []
        detail = []
        for role in sorted(set(ref["roles"]) | set(mine["roles"])):
            r, o = ref["roles"].get(role), mine["roles"].get(role)
            if r is None:
                verdicts.append("%s:not-in-script" % role)
                continue
            if o is None:
                verdicts.append("%s:ABSENT" % role)
                continue
            player = room_data["players"].get(by_role.get(role))
            found = first_deviation(o, r, player, room_data, segment)
            verdicts.append("%s:%s" % (role, ("t%d %s" % (found[0][0], found[0][1])) if found else "ok"))
            if only_wave is not None:
                detail.append("  %s (%s)" % (role, by_role.get(role)))
                detail.append("    tile   ours %s  script %s share %.2f dx %s dy %s" % (
                    o["tile"], r["tile"]["mode"], r["tile"]["share"], fmt_summary(r["tile"]["dx"]),
                    fmt_summary(r["tile"]["dy"])))
                detail.append("    first  ours %s  script %s  attacks ours %d script %s" % (
                    o["first_action"], fmt_summary(r["first_action"]), o["attacks"], fmt_summary(r["attacks"])))
                detail.append("    casts  ours %s  script %s" % (
                    o["casts"], " ".join(fmt_summary(c) for c in r["casts"])))
                detail.append("    tgts   ours %s  script %s" % (
                    o["targets"], " ".join("%s(%d%%)" % (t["mode"], round(100 * t["share"])) for t in r["targets"])))
                detail.append("    return ours %s  script %s  weapon ours %s script %s  prayer ours %s script %s" % (
                    o["return_to_boss"], fmt_summary(r["return_to_boss"]), o["weapon"], r["weapon"],
                    o["prayer"], r["prayer"]))
                if r.get("wave_adds_hit"):
                    detail.append("    lanes  ours %s  script %s" % (
                        " ".join("%s+%d" % (k, v) for k, v in sorted(o["wave_adds_hit"].items(), key=lambda i: i[1])),
                        " ".join("%s:%d%%+%s" % (k, round(100 * v["share"]), fmt_summary(v["first"]))
                                 for k, v in sorted(r["wave_adds_hit"].items(), key=lambda i: -i[1]["share"])[:5])))
                for tick, kind, text in found:
                    detail.append("    DEVIATES t%d %s: %s" % (tick, kind, text))
        lines.append(head + " | " + " ".join(verdicts))
        if only_wave is not None:
            lines.append("  leaks ours %s" % mine["leaks"])
            lines += detail
    firsts = []
    for name in ours:
        ref = reference.get(name)
        if not ref:
            continue
        for role, r in ref["roles"].items():
            o = ours[name]["roles"].get(role)
            if o is None:
                continue
            found = first_deviation(o, r, room_data["players"].get(by_role.get(role)), room_data, segments[name])
            if found:
                firsts.append((found[0][0], name, role, found[0][1], found[0][2]))
    if firsts:
        t, name, role, kind, text = min(firsts)
        lines.insert(1, "FIRST DEVIATION t%d wave %s %s %s: %s" % (t, name, role, kind, text))
    return lines


def budget(lines, limit=BYTE_BUDGET):
    out, used = [], 0
    for index, line in enumerate(lines):
        if used + len(line) + 1 > limit:
            out.append("... %d more lines (--wave N prints one wave in full)" % (len(lines) - index))
            break
        out.append(line)
        used += len(line) + 1
    return out
