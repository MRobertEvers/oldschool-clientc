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
  * with --timeline A-B, one line per tick for that range.

Several run directories print one after another, so a run that died can be
read against one that did not.

    python3 tools/raid_gate/raid_report.py build/quest_gate/tob_bloat
    python3 tools/raid_gate/raid_report.py RUN_A RUN_B --timeline 380-420

Output is short by design (it is read inside an editor): --max-lines bounds it.
"""

import argparse
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
    return report, rows


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
    arguments = parser.parse_args()

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
