#!/usr/bin/env python3
"""The quest ladder: a quest's Quest Helper guide as a small table, cut into legs.

    python3 tools/quest_gate/ladder.py <test_id> [--legs N] [--leg K] [--json] [--write]

WHY. A long quest ran its author out of context: it read the 15 KB guide
Java, then the 10-15 KB .rs2 files it names, over and over. This prints
what the guide asks, one row per guide step, with the content trigger each
step's target already has, so the author reads a table instead.

THE STEPS are exactly the ones helper_coverage.py grades, in its order: the
guide's getPanels() ladder, a panel ConditionalStep expanded into its
leaves (leaves that differ in one name word are one step). This tool asks
helper_coverage's Grader for that list (with an empty test, so the test's
own state never moves it) and reads each step's details from its Guide --
nothing here parses Java.

LEGS. The ordered steps are cut into legs of about --legs steps (default
10); a quest of 30 steps or fewer is one leg unless --legs is given. Each
ideal cut moves to the nearest quest-stage boundary (two different
`steps.put` keys) within three steps of it, else stays where it is.

--leg K prints only leg K after a three-line header (the quest, the stage
the leg starts at, the last step of the previous leg): what a relay author
is handed. --write writes docs/quests/ladders/<test_id>.ladder.tsv.

Exit 2, with a message naming what is missing, for an unknown test_id, a
quest with no guide, or a guide that yields no steps.
"""

import os as _os
import sys as _sys
_HERE = _os.path.dirname(_os.path.abspath(__file__))
# Same sys.path scrub as helper_coverage.py: queue.py here shadows the stdlib.
_sys.path[:] = [_p for _p in _sys.path if _os.path.abspath(_p or ".") != _HERE]
_sys.path.append(_HERE)

import argparse
import json
import os
import sys

import helper_coverage as hc  # noqa: E402

LADDER_DIR = os.path.join(hc.REPO_ROOT, "docs", "quests", "ladders")
DEFAULT_LEG = 10
ONE_LEG_MAX = 30
SNAP = 3
TEXT_CAP = 160
CLASS_OF = {"npc": "NpcID", "loc": "ObjectID", "obj": "ItemID"}


def die(message):
    sys.stderr.write("ladder.py: %s\n" % message)
    sys.exit(2)


def clip(text, cap):
    text = " ".join((text or "").split())
    return text if len(text) <= cap else text[:cap - 1] + "…"


def target_cell(kind, symbol):
    """`npc:klarense` (the guide's NpcID.KLARENSE: the content symbol is the
    constant lowercased) when the content knows the symbol, `npc:klarense?`
    when it does not."""
    known = (kind, symbol) in hc.DISPLAY or bool(hc.symbol_triggers(symbol)) or (kind, symbol) in hc.OPS
    return "%s:%s%s" % (kind, symbol, "" if known else "?")


def quest_triggers(quest_dir, targets, cap=1):
    """`path.rs2:line trigger` for the [op*]/[ap*] triggers on a step's
    targets: the ones in the quest's own directory; when it has none, the
    shared ones elsewhere (an npc's area script). Paths are relative to
    OSRS-Content/osrs239-content/server/scripts/."""
    prefix = os.path.join("quests", quest_dir) + os.sep
    own, other = [], []
    for _, symbol in targets:
        for rel, line, kind in hc.symbol_triggers(symbol):
            cell = "%s:%d %s" % (rel, line, kind)
            bucket = own if rel.startswith(prefix) else other
            if cell not in bucket:
                bucket.append(cell)
    found = own or other
    # op1 (Talk-to / the loc's first op) first: the press a step usually is.
    # Another quest's trigger on a shared loc (a plain `ladder`) is the last resort.
    found.sort(key=lambda cell: (cell.startswith("quests/"), 0 if cell.split(" ")[-1][-1:] == "1" else 1))
    shown = found[:cap]
    if len(found) > cap:
        shown.append("+%d" % (len(found) - cap))
    return shown


def build(test_id):
    row = hc.queue_row(test_id)
    if row is None:
        die("test_id %r is not in %s" % (test_id, hc.QUEUE_PATH))
    if not row.get("helper_dir"):
        die("test_id %r has no helper_dir in QUEUE.tsv: no Quest Helper guide" % test_id)
    path = hc.guide_path(row)
    if not path or not os.path.isfile(path):
        die("no Quest Helper guide for %r: helper_dir=%r helper_file=%r resolved to %s" % (
            test_id, row["helper_dir"], row.get("helper_file", ""), path))
    try:
        grader = hc.Grader(test_id, test_text="")
        graded = grader.grade()
    except Exception as error:  # the parser's own failure, named
        die("guide %s did not parse: %s: %s" % (path, type(error).__name__, error))
    if not graded:
        die("guide %s parsed to no steps (no getPanels() entries and no steps.put ladder)" % path)
    guide = grader.guide
    hc.content_index()
    # A leaf the grade expanded out of a panel ConditionalStep: name its parent.
    parent_of = {}
    for step in guide.ladder():
        if step.is_composite():
            for leaf in guide.leaves(step.name):
                parent_of.setdefault(leaf, step.name)
    steps = []
    for number, result in enumerate(graded, 1):
        step = guide.steps[result["step"]]
        kind = step.kind
        if result["step"] in parent_of and parent_of[result["step"]] != result["step"]:
            kind = "%s<%s" % (kind, parent_of[result["step"]])
        items = []
        for var in step.req_vars:
            name = guide.items.get(var, {}).get("name") or var
            if name not in items:
                items.append(name)
        steps.append({
            "n": number,
            "step": result["step"],
            "kind": kind,
            "stage": result["stage"],
            "panel": step.panel,
            "targets": [target_cell(k, s) for k, s in step.targets],
            "constants": ["%s.%s" % (CLASS_OF.get(k, k), s.upper()) for k, s in step.targets],
            "point": list(step.point) if step.point else None,
            "text": result["text"] or "",
            "substeps": list(step.substeps),
            "items": items,
            "dialog": list(step.dialog),
            "triggers": quest_triggers(row["quest_dir"], step.targets),
            "guide_line": step.line,
        })
    return row, os.path.relpath(path, hc.QUEST_HELPER_ROOT), steps


def cut(steps, size, forced):
    """[(first index, last index)] inclusive, 0-based."""
    count = len(steps)
    if count <= ONE_LEG_MAX and not forced:
        return [(0, count - 1)]
    legs = max(1, int(round(count / float(size))))
    bounds = [0]
    for k in range(1, legs):
        ideal = int(round(k * count / float(legs)))
        best = None
        for b in range(max(bounds[-1] + 1, ideal - SNAP), min(count - 1, ideal + SNAP) + 1):
            if steps[b - 1]["stage"] != steps[b]["stage"]:
                if best is None or abs(b - ideal) < abs(best - ideal):
                    best = b
        cut_at = best if best is not None else ideal
        if cut_at > bounds[-1] and cut_at < count:
            bounds.append(cut_at)
    bounds.append(count)
    return [(bounds[i], bounds[i + 1] - 1) for i in range(len(bounds) - 1)]


def stage_str(stage):
    return "-" if stage is None else str(stage)


def row_cells(step, leg, text_cap=TEXT_CAP):
    point = ",".join(str(v) for v in step["point"]) if step["point"] else ""
    return [str(leg), str(step["n"]), stage_str(step["stage"]), step["step"], step["kind"],
            " ".join(step["targets"]), point, clip(step["text"], text_cap),
            " ".join(step["substeps"]), clip("; ".join(step["items"]), 100),
            clip(" | ".join(step["dialog"]), 60), "; ".join(step["triggers"])]


COLUMNS = ["leg", "n", "stage", "step", "kind", "target", "point", "text",
           "substeps", "items", "dialog", "triggers"]


LEGEND = ("  (leg.n s<stage> step kind target (x,y,z); npc:x = NpcID.X, loc:x = ObjectID.X, obj:x = ItemID.X, "
          "x? = no content symbol; trig = OSRS-Content/osrs239-content/server/scripts/<file>:<line>, op1 first, +N more; "
          "items = means the row above's)")


def text_rows(steps, leg_of):
    lines = []
    for step in steps:
        cells = row_cells(step, leg_of[step["n"]])
        lines.append("%s.%-3s s%-5s %s  %s  %s  %s" % (
            cells[0], cells[1], cells[2], cells[3], cells[4], cells[5] or "-",
            "(" + cells[6] + ")" if cells[6] else ""))
        lines.append("      \"%s\"" % cells[7])
        for label, index in (("subs", 8), ("items", 9), ("dialog", 10), ("trig", 11)):
            if cells[index]:
                lines.append("      %s: %s" % (label, cells[index]))
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("test_id")
    parser.add_argument("--legs", type=int, default=None, help="target steps per leg (default 10)")
    parser.add_argument("--leg", type=int, default=None, help="print only leg K (1-based)")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--write", action="store_true",
                        help="write docs/quests/ladders/<test_id>.ladder.tsv")
    args = parser.parse_args()
    if args.legs is not None and args.legs < 1:
        die("--legs must be at least 1, got %d" % args.legs)

    row, guide, steps = build(args.test_id)
    legs = cut(steps, args.legs or DEFAULT_LEG, args.legs is not None)
    leg_of = {}
    for number, (first, last) in enumerate(legs, 1):
        for index in range(first, last + 1):
            leg_of[steps[index]["n"]] = number
    if args.leg is not None and not 1 <= args.leg <= len(legs):
        die("--leg %d: %s has %d leg%s" % (args.leg, args.test_id, len(legs), "" if len(legs) == 1 else "s"))

    leg_info = [{"leg": n, "first": steps[a]["step"], "last": steps[b]["step"],
                 "steps": [a + 1, b + 1], "start_stage": steps[a]["stage"], "end_stage": steps[b]["stage"]}
                for n, (a, b) in enumerate(legs, 1)]
    shown = steps
    if args.leg is not None:
        a, b = legs[args.leg - 1]
        shown = steps[a:b + 1]

    if args.write:
        os.makedirs(LADDER_DIR, exist_ok=True)
        out_path = os.path.join(LADDER_DIR, args.test_id + ".ladder.tsv")
        with open(out_path, "w", encoding="utf-8") as handle:
            handle.write("# ladder %s quest_dir=%s guide=%s steps=%d legs=%s\n" % (
                args.test_id, row["quest_dir"], guide, len(steps),
                " ".join("%d:%d-%d@s%s" % (i["leg"], i["steps"][0], i["steps"][1], stage_str(i["start_stage"]))
                         for i in leg_info)))
            handle.write("#" + LEGEND.strip() + "\n")
            handle.write("\t".join(COLUMNS) + "\n")
            previous_items = None
            for step in steps:
                cells = row_cells(step, leg_of[step["n"]])
                # A kit carried across many steps is written once: "=" is the row above's items.
                if cells[9] and cells[9] == previous_items:
                    cells[9] = "="
                else:
                    previous_items = cells[9]
                handle.write("\t".join(c.replace("\t", " ") for c in cells) + "\n")
        sys.stderr.write("wrote %s (%d bytes)\n" % (os.path.relpath(out_path, hc.REPO_ROOT),
                                                   os.path.getsize(out_path)))

    if args.json:
        print(json.dumps({"test_id": args.test_id, "quest_dir": row["quest_dir"], "guide": guide,
                          "step_count": len(steps), "legs": leg_info,
                          "steps": [dict(s, leg=leg_of[s["n"]]) for s in shown]}, indent=1))
        return 0

    if args.leg is not None:
        info = leg_info[args.leg - 1]
        previous = leg_info[args.leg - 2]["last"] if args.leg > 1 else "(none: this is the first leg)"
        print("quest: %s (%s, guide %s) -- leg %d of %d, steps %d-%d of %d" % (
            args.test_id, row["quest_dir"], guide, args.leg, len(legs), info["steps"][0], info["steps"][1],
            len(steps)))
        print("starts at stage: %s (%s)" % (stage_str(info["start_stage"]), info["first"]))
        print("previous leg ended with: %s" % previous)
    else:
        print("quest: %s (%s, guide %s) -- %d steps, %d leg%s" % (
            args.test_id, row["quest_dir"], guide, len(steps), len(legs), "" if len(legs) == 1 else "s"))
        for info in leg_info:
            print("  leg %d: steps %d-%d, stage %s..%s, %s .. %s" % (
                info["leg"], info["steps"][0], info["steps"][1], stage_str(info["start_stage"]),
                stage_str(info["end_stage"]), info["first"], info["last"]))
    print(LEGEND)
    print("\n".join(text_rows(shown, leg_of)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
