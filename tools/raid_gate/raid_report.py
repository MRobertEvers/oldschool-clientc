#!/usr/bin/env python3
"""Read a raid run's tick log after the fact and say where the play went wrong.

The owner, 2026-10-05: "Perhaps instead of watching, you can use something like
what blert does and just look at the log and see where you went wrong."  A
finished run already wrote every tick's events to <run dir>/ticklog.tsv
(script/plugins/quest_driver/ticklog.lua names the columns).  This reads that
file -- it never runs the client -- and prints, per run:

  * the fight's length, the hits the raiders landed and took, by npc type;
  * the attack cadence: the usual gap between landed hits and every stretch
    much longer than it (ticks in which nobody attacked);
  * the damage taken per fifth of the run, and the last hits before the end;
  * with --timeline A-B, one line per tick for that range;
  * the MISTAKES, each with its tick and raider (seam29): a missed attack, a
    hit the right protection prayer would have stopped, a tick on a hazard,
    food eaten that no hit could have needed, a stall, a death with its last
    ten ticks -- read off the raider rows (raider / input / consume, file-only
    kinds the server writes to ticklog.tsv; see find_mistakes below).

Several run directories print one after another, so a run that died can be
read against one that did not.

    python3 tools/raid_gate/raid_report.py build/quest_gate/tob_bloat
    python3 tools/raid_gate/raid_report.py RUN_A RUN_B --timeline 380-420

Output is short by design (it is read inside an editor): --max-lines bounds it.
"""

import argparse
import bisect
import collections
import csv
import os
import statistics
import sys

FIELDS = {
    "hit_player": ["pid", "npc_slot", "damage", "hitsplat", "dealer_pid", "npc_type", "raw"],
    "hit_npc": ["slot", "type", "damage", "hitsplat", "raw"],
    "npc_spawn": ["slot", "type", "coord"],
    "npc_death": ["slot", "type", "coord"],
    "npc_free": ["slot", "type", "coord"],
    "npc_anim": ["slot", "type", "seq", "delay"],
    "npc_retype": ["slot", "from_type", "to_type", "duration"],
    "player_tile": ["pid", "x", "z", "level"],
    "player_anim": ["pid", "seq", "delay"],
    "npc_tile": ["slot", "x", "z", "level", "type", "size"],
    "map_spotanim": ["coord", "spotanim", "height", "delay"],
    "projectile": ["src", "dst", "target", "spotanim", "start_cycle", "end_cycle"],
    # The raider's side (seam29; torirs_server.h TORIRSSERVER_TICKLOG_RAIDER):
    # file-only rows, every logged-in player every tick.
    "raider": ["pid", "hp", "prayer", "prayers", "weapon", "style", "spec"],
    "input": ["pid", "trigger", "subject", "npc_slot"],
    "consume": ["pid", "obj", "op", "hp_before", "hp_after", "prayer_before", "prayer_after"],
}


def read_ticklog(run_directory):
    path = os.path.join(run_directory, "ticklog.tsv")
    assert os.path.isfile(path), "no ticklog.tsv in %s" % run_directory
    rows = []
    with open(path, newline="") as handle:
        reader = csv.reader(handle, delimiter="\t")
        header = next(reader)
        assert header and header[0] == "ticklog-v1", "not a ticklog-v1 file: %s" % path
        for record in reader:
            if len(record) < 9:
                continue
            kind = record[2]
            row = {"tick": int(record[1]), "kind": kind, "label": record[9] if len(record) > 9 else ""}
            values = record[3:9] + (record[10:11] if len(record) > 10 else [])
            for name, value in zip(FIELDS.get(kind, []), values):
                try:
                    row[name] = int(value)
                except ValueError:
                    row[name] = value
            if kind == "raider":
                # "hpmax H prmax P head I input N tgt S"
                words = row["label"].split()
                for key, value in zip(words[0::2], words[1::2]):
                    try:
                        row[key] = int(value)
                    except ValueError:
                        pass
            rows.append(row)
    return rows


def read_summary(run_directory):
    path = os.path.join(run_directory, "ledger.tsv")
    if not os.path.isfile(path):
        return "no ledger", []
    summary = "no SUMMARY row"
    failing = []
    with open(path, newline="") as handle:
        for record in csv.reader(handle, delimiter="\t"):
            if record and record[0] == "SUMMARY":
                summary = " ".join(record[1:4])
            elif len(record) > 2 and record[2] in ("FAIL", "BLOCKED"):
                failing.append(record[1])
    return summary, failing


def analyse(run_directory):
    rows = read_ticklog(run_directory)
    summary, failing = read_summary(run_directory)
    report = {"name": os.path.basename(os.path.normpath(run_directory)), "summary": summary, "failing": failing}
    if not rows:
        report["empty"] = True
        return report, rows
    report["first_tick"] = rows[0]["tick"]
    report["last_tick"] = rows[-1]["tick"]
    dealt = [row for row in rows if row["kind"] == "hit_npc"]
    taken = [row for row in rows if row["kind"] == "hit_player"]
    report["hits_dealt"] = len(dealt)
    report["damage_dealt"] = sum(row.get("damage", 0) for row in dealt)
    report["zero_hits_dealt"] = sum(1 for row in dealt if row.get("damage", 0) == 0)
    report["hits_taken"] = sum(1 for row in taken if row.get("damage", 0) > 0)
    report["damage_taken"] = sum(row.get("damage", 0) for row in taken)

    by_type = collections.defaultdict(lambda: [0, 0, 0])
    for row in taken:
        entry = by_type[row.get("npc_type", -1)]
        entry[0] += 1
        entry[1] += row.get("damage", 0)
        entry[2] = max(entry[2], row.get("damage", 0))
    report["taken_by_type"] = sorted(by_type.items(), key=lambda item: -item[1][1])
    dealt_by_type = collections.defaultdict(lambda: [0, 0])
    for row in dealt:
        dealt_by_type[row.get("type", -1)][0] += 1
        dealt_by_type[row.get("type", -1)][1] += row.get("damage", 0)
    report["dealt_by_type"] = sorted(dealt_by_type.items(), key=lambda item: -item[1][1])

    hit_ticks = sorted(set(row["tick"] for row in dealt))
    if len(hit_ticks) > 2:
        gaps = [later - earlier for earlier, later in zip(hit_ticks, hit_ticks[1:])]
        usual = int(statistics.median(gaps))
        floor = max(usual * 2, usual + 3)
        stretches = [(a, b, b - a) for a, b in zip(hit_ticks, hit_ticks[1:]) if b - a > floor]
        report["usual_gap"] = usual
        report["first_hit"] = hit_ticks[0]
        report["last_hit"] = hit_ticks[-1]
        report["long_gaps"] = sorted(stretches, key=lambda gap: -gap[2])
        report["ticks_lost"] = sum(gap[2] - usual for gap in stretches)

    report["deaths"] = [(row["tick"], row.get("type", -1)) for row in rows if row["kind"] == "npc_death"]
    tiles = [row for row in rows if row["kind"] == "player_tile"]
    moved = 0
    for earlier, later in zip(tiles, tiles[1:]):
        if earlier.get("pid") == later.get("pid") and (earlier.get("x"), earlier.get("z")) != (later.get("x"), later.get("z")):
            moved += 1
    report["ticks_moving"] = moved
    span = max(1, report["last_tick"] - report["first_tick"] + 1)
    fifths = [0] * 5
    for row in taken:
        fifths[min(4, (row["tick"] - report["first_tick"]) * 5 // span)] += row.get("damage", 0)
    report["taken_by_fifth"] = fifths
    report["last_hits_taken"] = [
        (row["tick"], row.get("npc_type", -1), row.get("damage", 0)) for row in taken if row.get("damage", 0) > 0
    ][-6:]
    (report["mistakes"], report["raider_deaths"], report["has_raider"],
     report["mistake_notes"]) = find_mistakes(rows, HAZARDS_IN_USE)
    return report, rows




def mistake_lines(report, limit):
    """The MISTAKES block: a count by kind, then the first `limit`, each
    "p<pid> <text>"; a death adds its raider's last ten ticks."""
    lines = []
    mistakes = report.get("mistakes", [])
    counts = collections.Counter(kind for _, _, kind, _ in mistakes)
    lines.append("   mistakes: %d (%s)%s" % (
        len(mistakes), ", ".join("%s %d" % item for item in sorted(counts.items())) or "none",
        "" if report.get("has_raider") else "  [no raider rows: a log from before seam29; "
        "prayer and food need them; a death is read from the death animation, a stall from "
        "swings and walks]"))
    for tick, pid, kind, text in mistakes[:limit]:
        lines.append("      p%d %-13s %s" % (pid, kind, text))
    lines.extend("      " + note for note in report.get("mistake_notes", []))
    for pid, detail in sorted(report.get("raider_deaths", {}).items()):
        lines.append("   p%d death, the last ten ticks:" % pid)
        lines.extend("      " + line for line in detail)
    return lines


def print_report(report, lines):
    lines.append("== %s: %s" % (report["name"], report["summary"]))
    if report.get("empty"):
        lines.append("   empty tick log")
        return
    if report["failing"]:
        more = len(report["failing"]) - 6
        lines.append(
            "   failing rows: %s%s" % (", ".join(report["failing"][:6]), (" (+%d more)" % more) if more > 0 else "")
        )
    lines.append(
        "   ticks %d..%d (%d)   dealt %d damage in %d hits (%d zeros)   took %d damage in %d hits"
        % (
            report["first_tick"],
            report["last_tick"],
            report["last_tick"] - report["first_tick"] + 1,
            report["damage_dealt"],
            report["hits_dealt"],
            report["zero_hits_dealt"],
            report["damage_taken"],
            report["hits_taken"],
        )
    )
    if "usual_gap" in report:
        lines.append(
            "   attack cadence: a hit lands every %d ticks usually; first hit tick %d, last %d; %d long gaps, about %d ticks with no attack"
            % (report["usual_gap"], report["first_hit"], report["last_hit"], len(report["long_gaps"]), report["ticks_lost"])
        )
        for earlier, later, length in report["long_gaps"][:5]:
            lines.append("      no hit landed from tick %d to %d (%d ticks)" % (earlier, later, length))
    dealt_text = ", ".join("npc %s: %d in %d" % (kind, entry[1], entry[0]) for kind, entry in report["dealt_by_type"][:4])
    lines.append("   dealt by npc type: %s" % (dealt_text or "nothing"))
    taken_text = ", ".join(
        "npc %s: %d in %d (largest %d)" % (kind, entry[1], entry[0], entry[2]) for kind, entry in report["taken_by_type"][:4]
    )
    lines.append("   taken by npc type: %s" % (taken_text or "nothing"))
    lines.append(
        "   damage taken by fifth of the run: %s   ticks spent moving: %d" % (report["taken_by_fifth"], report["ticks_moving"])
    )
    if report["deaths"]:
        lines.append("   npc deaths: %s" % ", ".join("tick %d npc %s" % death for death in report["deaths"][:6]))
    else:
        lines.append("   npc deaths: NONE")
    lines.append("   last hits taken: %s" % ", ".join("t%d npc %s %d" % hit for hit in report["last_hits_taken"]))
    lines.extend(mistake_lines(report, MISTAKES_SHOWN[0]))


MISTAKES_SHOWN = [12]


# ---------------------------------------------------------------- mistakes
#
# Raid seam29 (raid_log_raider_state): the report NAMES the play's mistakes,
# each with its tick and raider (pid), the way Blert's analysis reads a
# recording.  Every rule reads the server's own rows; none replays the run.
#
#   missed_attack  the raider's attack cooldown was over (the weapon's
#                  cadence = the shortest gap between two of its swings), an
#                  npc it hits was alive and within the reach its own swings
#                  showed (Chebyshev, from its tile at the end of the tick
#                  before: ENCOUNTER_TIMING.md 1.1), and no swing was sent.
#   prayer         a hit taken (damage > 0) from an npc with no protection
#                  prayer lit -- or through the wrong one -- on the tick the
#                  attack was SENT (its npc_anim, or its projectile launch, in
#                  the ten ticks before the hit; else the tick before the hit).
#                  The owner's rule: prayer is checked on the animation tick
#                  (memory: prayer-checked-on-the-animation-tick); the pinned
#                  exceptions that check on landing are PRAYER_AT_LANDING.
#   hazard         the raider stood on a hazard tile on the tick it was live
#                  (HAZARD_SPOTANIMS, or --hazard ID[:TICKS]).
#   food           food (not a potion) eaten when the most any raider took in
#                  one tick of the run could not have killed (hitpoints before
#                  > that total).
#   stall          no input for STALL_TICKS ticks while an npc it hits lived.
#                  An input is the raider row's `input 1` (any client packet
#                  since the row before, walks included), a swing (an attack
#                  that is on repeats with no packet) or a food; a log
#                  written before seam29
#                  has none, and there an input is a swing, a landed hit, a
#                  food or the first step of a walk.
#   death          the raider's hitpoints reached 0, with its last ten ticks.

STALL_TICKS = 10
# spotanim -> (name, ticks live from its row's tick + delay / 30).  The Bloat
# hand's landing (1576; tob_bloat.rs2, PLAY_NOTES.md: "every 1576 landing on a
# raider" is a hand hit).  Add a room's hazard here with its source line.
HAZARD_SPOTANIMS = {1576: ("bloat hand", 1)}
# --hazard adds to it for one invocation (main()).
HAZARDS_IN_USE = dict(HAZARD_SPOTANIMS)
# The owner's pinned exceptions to "the prayer lit on the tick the attack was
# sent": npc type -> (the protection, ticks before the hit the prayer may be
# read on, whether it only REDUCES the hit, source).  A hit from a pinned type
# is a mistake only when that protection was lit on none of those ticks.
PINNED_PRAYER = {
    10812: ("protectfrommissiles", 6, True,
            "Bloat (Entry) flies: tob_bloat.rs2 ~tob_bloat_fly_hit queues the damage for the "
            "flight (at most six ticks) and ~tob_bloat_fly_damage reads the prayer at launch; "
            "Protect from Missiles cuts it by 25%, so a hit through it is not a mistake"),
}
PROTECTION_NAMES = ("protectfrommagic", "protectfrommissiles", "protectfrommelee")
# varp83_prayer0's bits, OSRS's layout; read_protection_bits() replaces them
# with configs/all.varbit's own startbits when the content tree is present.
PROTECTION_BITS = {"protectfrommagic": 12, "protectfrommissiles": 13, "protectfrommelee": 14}


def read_protection_bits():
    path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                        "OSRS-Content", "osrs239-content", "configs", "all.varbit")
    bits = dict(PROTECTION_BITS)
    if not os.path.isfile(path):
        return bits
    current = None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if line.startswith("["):
                current = None
                for name in PROTECTION_NAMES:
                    if line.rstrip().endswith("_prayer_" + name + "]"):
                        current = name
            elif current and line.startswith("startbit="):
                bits[current] = int(line.split("=", 1)[1])
                current = None
    return bits


def lit_protections(prayers, bits):
    return [name for name in PROTECTION_NAMES if prayers >= 0 and (prayers >> bits[name]) & 1]


def unpack(coord):
    return (coord >> 14) & 0x3FFF, coord & 0x3FFF


# A log written before seam29 has no footprint on its npc_tile rows (f was 0).
# type -> size, each read off a seam29 log's npc_tile f: 10812 (Bloat, Entry)
# from build/quest_gate/s29rl1.
NPC_SIZE_BEFORE_SEAM29 = {10812: 5}


def footprint_gaps(tile, npc):
    x, z = tile
    size = npc.get("size") or NPC_SIZE_BEFORE_SEAM29.get(npc.get("type"), 1)
    dx = max(npc["x"] - x, 0, x - (npc["x"] + size - 1))
    dz = max(npc["z"] - z, 0, z - (npc["z"] + size - 1))
    return dx, dz


def footprint_distance(tile, npc):
    """Chebyshev distance from a tile to an npc's footprint (0 = under it)."""
    return max(footprint_gaps(tile, npc))


def in_reach(tile, npc, reach):
    """Could a weapon of this reach hit the npc from `tile`?  Never from under
    it; melee (reach 1) never across a diagonal."""
    dx, dz = footprint_gaps(tile, npc)
    if max(dx, dz) == 0 or max(dx, dz) > reach:
        return False
    return reach > 1 or min(dx, dz) == 0


# Never a swing: the eat/drink animation (OSRS 829, in a log with no consume
# rows to say so) and the death animation (836, which is also how a log from
# before seam29 shows a death).
DEATH_ANIM = 836
NOT_SWINGS = {-1, 829, DEATH_ANIM}
# Eating food delays the next attack by three ticks; drinking a potion does
# not (OSRS wiki, Food: "eating food will add 3 ticks to the attack delay").
FOOD_ATTACK_DELAY = 3
POTION_WORDS = ("dose", "potion", "brew")


def runs_of(ticks):
    """Consecutive ticks grouped: [(first, last), ...]."""
    groups = []
    for tick in sorted(ticks):
        if groups and tick == groups[-1][1] + 1:
            groups[-1][1] = tick
        else:
            groups.append([tick, tick])
    return [tuple(group) for group in groups]


class NpcTrack:
    """Where every npc stood, and whether it was alive, on any tick."""

    def __init__(self, rows):
        self.tiles = collections.defaultdict(list)  # slot -> [(tick, row)]
        self.ends = collections.defaultdict(list)  # slot -> [tick of death/free]
        self.keys = {}
        for row in rows:
            if row["kind"] == "npc_tile":
                self.tiles[row["slot"]].append((row["tick"], row))
            elif row["kind"] in ("npc_death", "npc_free"):
                self.ends[row["slot"]].append(row["tick"])

    def at(self, slot, tick):
        """The slot's npc_tile row in force on `tick`, or None (absent/dead)."""
        moves = self.tiles[slot]
        if slot not in self.keys:
            self.keys[slot] = [when for when, _ in moves]
        index = bisect.bisect_right(self.keys[slot], tick) - 1
        if index < 0:
            return None
        when, row = moves[index]
        if any(when <= end <= tick for end in self.ends[slot]):
            return None
        return row

    def alive(self, tick, types):
        found = []
        for slot in self.tiles:
            row = self.at(slot, tick)
            if row is not None and row.get("type") in types:
                found.append(row)
        return found


def find_mistakes(rows, hazards=None):
    """[(tick, pid, kind, text)], sorted; plus {pid: [death detail lines]}."""
    hazards = dict(HAZARD_SPOTANIMS if hazards is None else hazards)
    bits = read_protection_bits()
    mistakes = []
    deaths = {}
    unattributed = {}
    pids = sorted(set(row["pid"] for row in rows if row["kind"] == "player_tile"))
    tiles = collections.defaultdict(dict)
    raider = collections.defaultdict(dict)
    for row in rows:
        if row["kind"] == "player_tile":
            tiles[row["pid"]][row["tick"]] = (row["x"], row["z"])
        elif row["kind"] == "raider":
            raider[row["pid"]][row["tick"]] = row
    has_raider = bool(raider)
    dealt = [row for row in rows if row["kind"] == "hit_npc"]
    if not dealt:
        return mistakes, deaths, has_raider, []
    targets = set(row["type"] for row in dealt)
    fight_start = dealt[0]["tick"]
    log_end = rows[-1]["tick"]
    npcs = NpcTrack(rows)
    alive_cache = {}

    def alive(tick):
        if tick not in alive_cache:
            alive_cache[tick] = npcs.alive(tick, targets)
        return alive_cache[tick]

    consumes = [row for row in rows if row["kind"] == "consume"]
    taken = [row for row in rows if row["kind"] == "hit_player" and row.get("damage", 0) > 0]

    # hazard tiles live per tick
    hazard_at = collections.defaultdict(dict)
    for row in rows:
        if row["kind"] == "map_spotanim" and row.get("spotanim") in hazards:
            name, live = hazards[row["spotanim"]]
            first = row["tick"] + max(0, row.get("delay", 0)) // 30
            for tick in range(first, first + live):
                hazard_at[tick][unpack(row["coord"])] = name

    for pid in pids:
        consume_ticks = set(row["tick"] for row in consumes if row["pid"] == pid)
        swings = sorted(set(
            row["tick"] for row in rows
            if row["kind"] == "player_anim" and row["pid"] == pid and row.get("seq") not in NOT_SWINGS
            and row["tick"] not in consume_ticks))
        # ---- missed attacks
        gaps = [later - earlier for earlier, later in zip(swings, swings[1:]) if later - earlier >= 2]
        if gaps:
            cadence = min(gaps)
            # The weapon's reach: the distance most of its swings were sent
            # from (a swing sent on the step that closed the distance reads
            # long, so the mode, not the max).
            distances = collections.Counter()
            for swing in swings:
                here = tiles[pid].get(swing)
                near = [footprint_distance(here, npc) for npc in alive(swing)] if here else []
                if near:
                    distances[min(near)] += 1
            reach = max(1, min(distances, key=lambda d: (-distances[d], d))) if distances else 1
            # Food holds the next attack back three ticks (a potion does not).
            fed = set()
            for row in consumes:
                if row["pid"] == pid and not any(word in row["label"] for word in POTION_WORDS):
                    fed.update(range(row["tick"], row["tick"] + FOOD_ATTACK_DELAY))
            missed = []
            bounds = list(zip(swings, swings[1:] + [log_end + 1]))
            for previous, following in bounds:
                for tick in range(previous + cadence, following):
                    before = tiles[pid].get(tick - 1)
                    if before is None or tick < fight_start or tick in fed:
                        continue
                    # A step sent instead is a choice (the hazard and stall
                    # rules judge it); a missed attack is standing in reach.
                    if tiles[pid].get(tick) != before:
                        continue
                    # npcs move before players (ENCOUNTER_TIMING.md 1.1): the
                    # npc where it stands on `tick`, the raider where it stood.
                    # In reach on the tick before too: the play sees the end
                    # of t-1, and an npc that walks past for one tick could
                    # not have been answered.
                    seen = set(npc["slot"] for npc in alive(tick - 1) if in_reach(before, npc, reach))
                    if any(in_reach(before, npc, reach) and npc["slot"] in seen for npc in alive(tick)):
                        missed.append(tick)
            for first, last in runs_of(missed):
                npc = alive(first)
                mistakes.append((first, pid, "missed_attack",
                                 "t%d-%d attack not sent (%d ticks; cadence %d, reach %d, npc %s in reach)"
                                 % (first, last, last - first + 1, cadence, reach,
                                    npc[0]["type"] if npc else "?")))
        # ---- hazards
        stood = [(tick, tile, hazard_at[tick][tile]) for tick, tile in sorted(tiles[pid].items())
                 if tile in hazard_at.get(tick, {})]
        for tick, tile, name in stood:
            mistakes.append((tick, pid, "hazard", "t%d stood on a %s at %d,%d" % (tick, name, tile[0], tile[1])))
        hazard_ticks = set(tick for tick, _, _ in stood)
        # ---- prayer
        if has_raider:
            anims = rows_by_slot_anim(rows)
            for hit in taken:
                if hit["pid"] != pid or hit.get("npc_slot", -1) < 0 or hit["tick"] in hazard_ticks:
                    continue
                pinned = PINNED_PRAYER.get(hit.get("npc_type"))
                if pinned:
                    prayer, window, reduces, _ = pinned
                    lit_ticks = [tick for tick in range(hit["tick"] - window, hit["tick"])
                                 if prayer in lit_protections(raider[pid].get(tick, {}).get("prayers", -1), bits)]
                    if not lit_ticks:
                        mistakes.append((hit["tick"], pid, "prayer",
                                         "t%d took %d from npc %s with no %s lit in t%d-%d (pinned: read at launch)"
                                         % (hit["tick"], hit["damage"], hit.get("npc_type"), prayer,
                                            hit["tick"] - window, hit["tick"] - 1)))
                    elif not reduces:
                        mistakes.append((hit["tick"], pid, "prayer",
                                         "t%d took %d from npc %s through %s" % (
                                             hit["tick"], hit["damage"], hit.get("npc_type"), prayer)))
                    continue
                send = hit_send_tick(hit, anims)
                if send is None:
                    unattributed[pid] = unattributed.get(pid, 0) + 1
                    continue
                state = raider[pid].get(send)
                if state is None:
                    continue
                lit = lit_protections(state["prayers"], bits)
                if not lit:
                    text = "t%d took %d from npc %s (attack sent t%d) with no protection prayer lit" % (
                        hit["tick"], hit["damage"], hit.get("npc_type"), send)
                else:
                    text = "t%d took %d from npc %s (attack sent t%d) through %s" % (
                        hit["tick"], hit["damage"], hit.get("npc_type"), send, "+".join(lit))
                mistakes.append((hit["tick"], pid, "prayer", text))
        # ---- food
        # The largest that could land: the most any raider took in one tick.
        per_tick = collections.Counter()
        for row in taken:
            per_tick[(row["pid"], row["tick"])] += row["damage"]
        largest = max(per_tick.values() or [0])
        for row in consumes:
            if (row["pid"] == pid and row["hp_after"] > row["hp_before"] and row["hp_before"] > largest
                    and not any(word in row["label"] for word in POTION_WORDS)):
                mistakes.append((row["tick"], pid, "food",
                                 "t%d ate %s at %d hitpoints (the most taken in one tick: %d)"
                                 % (row["tick"], row["label"], row["hp_before"], largest)))
        # ---- stall
        if has_raider:
            # An attack that is already on repeats with no new input: its
            # swings are play too.
            inputs = set(tick for tick, state in raider[pid].items() if state.get("input") == 1)
            inputs |= set(swings) | consume_ticks
        else:
            inputs = set(swings) | consume_ticks | set(row["tick"] for row in dealt)
            for tick in sorted(tiles[pid]):
                if (tiles[pid].get(tick) != tiles[pid].get(tick - 1) and tiles[pid].get(tick - 1) is not None
                        and tiles[pid].get(tick - 1) == tiles[pid].get(tick - 2)):
                    inputs.add(tick)
        marks = sorted(tick for tick in inputs if tick >= fight_start) + [log_end + 1]
        for earlier, later in zip(marks, marks[1:]):
            if later - earlier - 1 >= STALL_TICKS and alive(earlier + 1):
                mistakes.append((earlier, pid, "stall",
                                 "t%d-%d stall: no input for %d ticks while npc %s lived"
                                 % (earlier, later - 1, later - earlier - 1, alive(earlier + 1)[0]["type"])))
        # ---- death: hitpoints 0 on the raider row, or (no raider rows) the
        # death animation
        if has_raider:
            died, previous = [], None
            for tick in sorted(raider[pid]):
                if raider[pid][tick]["hp"] == 0 and (previous is None or previous > 0):
                    died.append(tick)
                previous = raider[pid][tick]["hp"]
        else:
            died = sorted(set(row["tick"] for row in rows if row["kind"] == "player_anim"
                              and row["pid"] == pid and row.get("seq") == DEATH_ANIM))
        for tick in died:
            mistakes.append((tick, pid, "death", "t%d died" % tick))
            deaths.setdefault(pid, death_detail(pid, tick, raider[pid], tiles[pid], taken, swings, bits))
    mistakes.sort(key=lambda item: (item[0], item[1]))
    notes = ["p%d: %d hits taken with no attack row to say when they were sent (not judged for prayer)"
             % (pid, count) for pid, count in sorted(unattributed.items())]
    return mistakes, deaths, has_raider, notes


_ANIM_INDEX = {}


def rows_by_slot_anim(rows):
    key = id(rows)
    if key not in _ANIM_INDEX:
        index = collections.defaultdict(list)
        for row in rows:
            if row["kind"] == "npc_anim":
                index[row["slot"]].append(row["tick"])
        _ANIM_INDEX.clear()
        _ANIM_INDEX[key] = index
    return _ANIM_INDEX[key]


def hit_send_tick(hit, anims):
    """The tick the attack behind `hit` was sent: its npc's last animation in
    the ten ticks before it, or None (the hit is not judged: no attack row
    says when it was sent, and a guess would invent a prayer mistake)."""
    sends = [tick for tick in anims.get(hit["npc_slot"], []) if hit["tick"] - 10 <= tick <= hit["tick"]]
    return sends[-1] if sends else None


def death_detail(pid, tick, states, tiles, taken, swings, bits):
    lines = []
    for when in range(tick - 9, tick + 1):
        state = states.get(when)
        took = sum(hit["damage"] for hit in taken if hit["pid"] == pid and hit["tick"] == when)
        tile = tiles.get(when, ("?", "?"))
        if state is not None:
            head = "hp %d pr %d [%s]" % (state["hp"], state["prayer"],
                                         "+".join(lit_protections(state["prayers"], bits)) or "-")
        else:
            head = "hp ?"
        lines.append("t%d %s at %s,%s%s%s" % (
            when, head, tile[0], tile[1], (" took %d" % took) if took else "", " SWING" if when in swings else ""))
    return lines


def print_timeline(name, rows, first, last, lines):
    lines.append("-- %s ticks %d..%d" % (name, first, last))
    by_tick = collections.defaultdict(list)
    for row in rows:
        if first <= row["tick"] <= last:
            by_tick[row["tick"]].append(row)
    for tick in range(first, last + 1):
        parts = []
        for row in by_tick.get(tick, []):
            kind = row["kind"]
            if kind == "player_tile":
                parts.append("at %s,%s" % (row.get("x"), row.get("z")))
            elif kind == "hit_npc":
                parts.append("HIT npc %s for %s" % (row.get("type"), row.get("damage")))
            elif kind == "hit_player":
                parts.append("TOOK %s from npc %s" % (row.get("damage"), row.get("npc_type")))
            elif kind == "npc_anim":
                parts.append("npc %s anim %s" % (row.get("type"), row.get("seq")))
            elif kind == "player_anim":
                parts.append("player anim %s" % row.get("seq"))
            elif kind in ("npc_death", "npc_spawn", "npc_retype"):
                parts.append("%s %s" % (kind, row.get("type", row.get("to_type"))))
            elif kind == "mark":
                parts.append("mark %s" % row.get("label"))
        lines.append("  t%d: %s" % (tick, "; ".join(parts)[:150]))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run_directories", nargs="+", help="directories holding ticklog.tsv (and ledger.tsv)")
    parser.add_argument("--timeline", default=None, help="A-B: print one line per tick for this range")
    parser.add_argument("--max-lines", type=int, default=60, help="stop printing after this many lines")
    parser.add_argument("--mistakes", type=int, default=12, help="how many mistakes to list per run")
    parser.add_argument("--hazard", action="append", default=[], metavar="SPOTANIM[:TICKS]",
                        help="also treat this map spotanim as a hazard tile for TICKS ticks (default 1)")
    arguments = parser.parse_args()
    MISTAKES_SHOWN[0] = arguments.mistakes
    for entry in arguments.hazard:
        spotanim, _, ticks = entry.partition(":")
        HAZARDS_IN_USE[int(spotanim)] = ("spotanim %s" % spotanim, int(ticks or 1))

    lines = []
    for run_directory in arguments.run_directories:
        report, rows = analyse(run_directory)
        print_report(report, lines)
        if arguments.timeline:
            first, last = (int(part) for part in arguments.timeline.split("-"))
            print_timeline(report["name"], rows, first, last, lines)
    for line in lines[: arguments.max_lines]:
        print(line)
    if len(lines) > arguments.max_lines:
        print("... %d more lines (raise --max-lines)" % (len(lines) - arguments.max_lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
