#!/usr/bin/env python3
"""The quest ladder: a quest's Quest Helper guide as a small table, cut into legs.

    python3 tools/quest_gate/ladder.py <test_id> [--legs N] [--leg K] [--json] [--write] [--guide-order]

WHY. A long quest ran its author out of context: it read the 15 KB guide
Java, then the 10-15 KB .rs2 files it names, over and over. This prints
what the guide asks, one row per guide step, with the content trigger each
step's target already has, so the author reads a table instead.

THE STEPS are exactly the ones helper_coverage.py grades, in its order: the
guide's getPanels() ladder, a panel ConditionalStep expanded into its
leaves (leaves that differ in one name word are one step), and since seam36
every step the steps.put ConditionalStep tree shows that no panel lists,
placed before the state it leads to with kind `<Kind><<composite>[<condition>]`
(Monkey Madness's `ObjectStep<bringMonkey[onApeAtollSouth]` enterGate, the
Bamboo Gate). This tool asks
helper_coverage's Grader for that list (with an empty test, so the test's
own state never moves it) and reads each step's details from its Guide --
nothing here parses Java.

LEGS. The ordered steps are cut into legs of about --legs steps (default
10); a quest of 30 steps or fewer is one leg unless --legs is given. Each
ideal cut moves to the nearest quest-stage boundary (two different
`steps.put` keys) within three steps of it, else stays where it is.

ROUTE CUT. When docs/quests/ladders/<test_id>.legs exists, the legs are
not cut by count: each non-comment line of that file is one leg, a stage
range in walking order (`0-60`, `90-150`, `300-` = 300 and up; `#` starts a
comment). Every step goes to the leg whose range holds its stage (a step
with no stage keeps the stage of the guide step before it), the steps are
listed in leg order and, inside a leg, by stage (guide order breaks ties),
and `n` is renumbered in that route order (`guide_n` in --json, `g<k>` in
the text keeps the guide's number). Ranges must ascend without overlap,
every step's stage must fall in one, and no range may be empty; anything
else exits 2 naming the line. Use it when the guide's order is not the
route (The Fremennik Isles files stage 90 before the steps that reach it);
a quest without the file is cut exactly as before. --guide-order ignores
the file; --legs N with a route cut exits 2. fail.py --leg K follows the
same cut, through build() and cut().

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
import re
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


def build(test_id, route=True):
    """(QUEUE row, guide path, steps). The steps are in the guide's order, or
    in route order when the quest has a .legs file and route is true."""
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
        elif guide.branch_only.get(result["step"]):
            # A step only the ConditionalStep tree shows (seam36): its
            # ConditionalStep and the condition QH shows it on.
            kind = "%s<%s" % (kind, guide.branch_label(result["step"]))
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
    if route:
        steps = route_order(test_id, steps)
    return row, os.path.relpath(path, hc.QUEST_HELPER_ROOT), steps


def legs_path(test_id):
    return os.path.join(LADDER_DIR, test_id + ".legs")


def read_route_legs(test_id):
    """(path, [(lo, hi or None, line number)]) from docs/quests/ladders/<test_id>.legs,
    or (None, None) when the quest has no route cut file."""
    path = legs_path(test_id)
    if not os.path.isfile(path):
        return None, None
    rel = os.path.relpath(path, hc.REPO_ROOT)
    ranges = []
    with open(path, encoding="utf-8") as handle:
        for number, raw in enumerate(handle, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            match = re.match(r"^(\d+)\s*-\s*(\d*)$", line)
            if not match:
                die("%s:%d: %r is not a stage range (`lo-hi`, or `lo-` for lo and up)" % (rel, number, line))
            lo = int(match.group(1))
            hi = int(match.group(2)) if match.group(2) else None
            if hi is not None and hi < lo:
                die("%s:%d: range %d-%d runs backwards" % (rel, number, lo, hi))
            if ranges:
                previous_hi = ranges[-1][1]
                if previous_hi is None or lo <= previous_hi:
                    die("%s:%d: range %s does not start after the line before it ends (%d-%s): "
                        "ranges ascend in walking order without overlap" % (
                            rel, number, line, ranges[-1][0], "" if previous_hi is None else previous_hi))
            ranges.append((lo, hi, number))
    if not ranges:
        die("%s holds no stage range: delete it or write one range per leg" % rel)
    return path, ranges


def route_order(test_id, steps):
    """The steps re-ordered by the quest's .legs file, each stamped with
    `route_leg` and `guide_n` and renumbered; the steps unchanged when the
    quest has no such file."""
    path, ranges = read_route_legs(test_id)
    if path is None:
        return steps
    rel = os.path.relpath(path, hc.REPO_ROOT)
    keyed = []
    effective = ranges[0][0]
    for step in steps:
        if step["stage"] is not None:
            effective = step["stage"]
        leg = None
        for index, (lo, hi, _) in enumerate(ranges):
            if effective >= lo and (hi is None or effective <= hi):
                leg = index + 1
                break
        if leg is None:
            die("%s: guide step %d %s (stage %s) falls in none of its ranges" % (
                rel, step["n"], step["step"], stage_str(step["stage"])))
        keyed.append((leg, effective, step["n"], step))
    keyed.sort(key=lambda entry: entry[:3])
    used = set(entry[0] for entry in keyed)
    for index, (lo, hi, number) in enumerate(ranges):
        if index + 1 not in used:
            die("%s:%d: range %d-%s holds no guide step" % (rel, number, lo, "" if hi is None else hi))
    ordered = []
    for route_n, (leg, _, guide_n, step) in enumerate(keyed, 1):
        step = dict(step, n=route_n, guide_n=guide_n, route_leg=leg)
        ordered.append(step)
    return ordered


def cut(steps, size, forced):
    """[(first index, last index)] inclusive, 0-based. Steps that route_order
    stamped carry their own legs (the .legs file); size and forced are then
    not used."""
    count = len(steps)
    if count and "route_leg" in steps[0]:
        bounds = [0] + [i for i in range(1, count) if steps[i]["route_leg"] != steps[i - 1]["route_leg"]]
        bounds.append(count)
        return [(bounds[i], bounds[i + 1] - 1) for i in range(len(bounds) - 1)]
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
        lines.append("%s.%-3s s%-5s %s  %s  %s  %s%s" % (
            cells[0], cells[1], cells[2], cells[3], cells[4], cells[5] or "-",
            "(" + cells[6] + ")" if cells[6] else "",
            "  g%d" % step["guide_n"] if "guide_n" in step else ""))
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
    parser.add_argument("--guide-order", action="store_true",
                        help="ignore docs/quests/ladders/<test_id>.legs: cut in the guide's order")
    args = parser.parse_args()
    if args.legs is not None and args.legs < 1:
        die("--legs must be at least 1, got %d" % args.legs)

    row, guide, steps = build(args.test_id, route=not args.guide_order)
    routed = bool(steps) and "route_leg" in steps[0]
    if routed and args.legs is not None:
        die("--legs %d: %s is cut by route in %s; edit that file, or add --guide-order" % (
            args.legs, args.test_id, os.path.relpath(legs_path(args.test_id), hc.REPO_ROOT)))
    cut_note = ("route order, %s (%s)" % (
        os.path.relpath(legs_path(args.test_id), hc.REPO_ROOT),
        " ".join("%d-%s" % (lo, "" if hi is None else hi) for lo, hi, _ in read_route_legs(args.test_id)[1]))
        if routed else "guide order")
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
            handle.write("# ladder %s quest_dir=%s guide=%s steps=%d legs=%s%s\n" % (
                args.test_id, row["quest_dir"], guide, len(steps),
                " ".join("%d:%d-%d@s%s" % (i["leg"], i["steps"][0], i["steps"][1], stage_str(i["start_stage"]))
                         for i in leg_info),
                (" cut=route:%s.legs (n is the route order; guide_n the guide's)" % args.test_id) if routed else ""))
            handle.write("#" + LEGEND.strip() + "\n")
            handle.write("\t".join(COLUMNS + (["guide_n"] if routed else [])) + "\n")
            previous_items = None
            for step in steps:
                cells = row_cells(step, leg_of[step["n"]])
                if routed:
                    cells.append(str(step["guide_n"]))
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
        print("starts at stage: %s (%s)%s" % (stage_str(info["start_stage"]), info["first"],
                                            "; cut in " + cut_note if routed else ""))
        print("previous leg ended with: %s" % previous)
    else:
        print("quest: %s (%s, guide %s) -- %d steps, %d leg%s" % (
            args.test_id, row["quest_dir"], guide, len(steps), len(legs), "" if len(legs) == 1 else "s"))
        if routed:
            print("  cut in %s" % cut_note)
        for info in leg_info:
            print("  leg %d: steps %d-%d, stage %s..%s, %s .. %s" % (
                info["leg"], info["steps"][0], info["steps"][1], stage_str(info["start_stage"]),
                stage_str(info["end_stage"]), info["first"], info["last"]))
    print(LEGEND)
    print("\n".join(text_rows(shown, leg_of)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
