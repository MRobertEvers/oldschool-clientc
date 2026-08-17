#!/usr/bin/env python3
"""Author a `category=` on every ladder and staircase this cache states a climb verb on.

Why this exists
---------------
`interaction_engine_loc` in `src/net/mock/mock230_world.c` answered a ladder by
`strcmp`ing the loc's *cache menu verb* — "Climb-up", "Climb-down", or a bare
"Climb" — and moving the player a plane. That is content's job (LostCity puts it
in `content/scripts/ladders+stairs/scripts/`), but content cannot say
"whichever loc's menu says Climb-up": there is no opcode that exposes a loc's
verb, and a trigger's subject is a loc *name* or a *category*.

There are 1,445 such records in cache.osrs239, which rules out a name list kept
by hand. So the verb becomes a category — the record grouped by what it *is* —
and the grouping is derived from the cache rather than chosen, which is what this
script does. The reference does the same thing in the other direction: a
LostCity category is crawled out of the `.loc` blocks that state it, never
looked up (`CategoryType.ts`).

The grouping
------------
A record is grouped by the SET OF DIRECTIONS its climb ops name, not by which op
slot they sit on: a "Climb-up" on op1 and a "Climb-up" on op2 mean the same
thing, and the script binds both slots. Measured on cache.osrs239 there are
exactly 11 distinct (slot, verb) layouts and they collapse to four groups plus
two records that stand alone.

Records carrying two DIFFERENT climb verbs whose layout is not the spiral
staircase's are NOT given a category — one category cannot say "op2 goes up and
op3 goes down" for a group of one. They are reported and bound by name in
`ladders_stairs/scripts/ladders.rs2`.

The two conflict rules
----------------------
**A record another authored `.loc` already categorises is not categorised here.**
A record carries one category and `cachepack` refuses a second, so this is a hard
error rather than a preference. It is not hypothetical: 15 open trapdoors are
`category=door_opened` in `doors/configs/doors.loc` *and* say "Climb-down" on
op1. They are emitted as **name** bindings into a generated
`ladders_stairs/scripts/climb_shared.rs2` instead — the trigger table's name rung
beats its category rung, so `[oploc1,trapdoor_open]` wins over
`[oploc1,_door_opened]` on the same click and the door's own "Close" (op2 on 13
of the 15) stays with the door script.

**A record already carrying a cache category which `pack/category.pack` *names*
is skipped and reported, never overwritten.** The authored overlay wins at load
time, so silently overwriting would unbind whatever that name is for. Measured
today, 194 of the 1,445 carry a cache category across 47 ids and not one of the
47 is named, so nothing is skipped — the rule exists to make the first future
collision loud instead of silent.

Usage
-----
    python3 tools/ladder_import.py [--content <dir>] [--check] [--report]

`--check` writes nothing and exits non-zero if the file on disk differs, which
is what makes "generated, do not edit" enforceable.
"""

import argparse
import collections
import os
import re
import sys

CLIMB_VERBS = ("Climb-up", "Climb-down", "Climb")

# group name -> the set of distinct climb verbs that defines it
SINGLE_VERB_GROUP = {
    "Climb-up": "climb_up",
    "Climb-down": "climb_down",
    "Climb": "climb_unqualified",
}

# The spiral staircase's middle floor: one unqualified op and both directions
# beside it. 32 records in cache.osrs239 and the only multi-verb layout that is
# a group rather than a one-off.
SPIRAL = {1: "Climb", 2: "Climb-up", 3: "Climb-down"}
SPIRAL_GROUP = "climb_spiral_middle"

# The second axis: is this thing a LADDER, or is it a staircase / rock / vine?
#
# It matters because a ladder animates and a staircase does not. The reference
# says so directly — `ladders+stairs/scripts/ladders.rs2` runs
# `anim(human_reachforladder)` going up and `anim(human_pickupfloor)` going down
# (its `~climb_ladder` proc, and `manhole.rs2` for the trapdoor case), while
# every one of `stairs.rs2`'s several hundred staircases is a bare
# `p_telejump` with no anim at all.
#
# The reference can key that off the loc NAME because it binds each loc by name;
# this tree binds by category, so the name test happens here and the answer is
# carried as the category's `_ladder` suffix.
#
# Measured on cache.osrs239: 'ladder' hits 485 of the 1,443 climb records,
# 'stair' hits 457, and the two overlap exactly once —
# `elem2_stairs_door_open_no_hatch`, a staircase whose name only says what it
# does NOT have. That is what the `stair` veto below is for. 'trapdoor' (33),
# 'manhole' (6) and 'hatch' (3) join the ladder side: you climb down through a
# hole the same way, and the reference's own manhole script plays exactly the
# same `human_pickupfloor`.
# `trap_door` is not a spelling variant worth being clever about — it is one
# record, `champions_trap_door_open`, and it climbed down un-animated for exactly
# as long as the keyword list said `trapdoor` and the cache said `trap_door`.
#
# The veto reads the cache's `name=` as well as the symbol, and that is the half
# that was missing
# ------------------------------------------------------------------------
# A symbol is this port's name for a record, not the cache's: `port/names.map`
# resolves what it can and the rest is inherited from whichever tree the name
# came from. The cache's own `name=` — the words the player reads in the menu —
# is the only statement in the file about what the thing IS.
#
# They disagree, and they disagree in exactly the direction that matters. Five
# records carry `ladder` in the symbol and say **Stairs** in the cache
# (`domeladderdown`, `obs_dungeonladderdown` and the three
# `slp_church_crypt_south_ladder_*`), so a symbol-only test animated five
# staircases. It runs the other way too, and more often — 44 records the symbol
# calls a staircase, a slope or nothing in particular say **Ladder**,
# **Rope ladder**, **Vine ladder** or **Trapdoor** in the cache
# (`dorgesh_cavewall_slope_steps` is the clearest: a `steps` symbol on a record
# whose menu says Ladder).
#
# So the display name joins the test on the veto side only, which is the scoped
# half: a record whose menu says Stairs is not a ladder whatever its symbol
# claims. Adding the display name to the KEYWORDS side as well would animate
# those 44, which is a behaviour change nobody has asked for — it is written up
# in `ladders_stairs/README.md` instead.
#
# Only `stair` is read out of the display name. `steps` is not a reliable token
# on either side: `dorgesh_cavewall_slope_steps` displays Ladder, and
# `agility_pyramid_steps1` displays Stairs — the word says nothing on its own.
LADDER_KEYWORDS = ("ladder", "trapdoor", "trap_door", "manhole", "hatch")
LADDER_VETO = ("stair",)
DISPLAY_VETO = ("stair",)


def is_ladder(name, record):
    if any(veto in name for veto in LADDER_VETO):
        return False
    display = record.get("name", "").lower()
    if any(veto in display for veto in DISPLAY_VETO):
        return False
    return any(keyword in name for keyword in LADDER_KEYWORDS)


# Emission order, and the whole set of categories this importer authors.
GROUPS = tuple(
    group + suffix
    for group in ("climb_up", "climb_down", "climb_unqualified", SPIRAL_GROUP)
    for suffix in ("", "_ladder")
)

HEADER = """\
// Ladders, staircases and every other loc whose menu says Climb.
//
// Generated by tools/ladder_import.py — do not edit, re-run the importer.
// `python3 tools/ladder_import.py --check` fails when this file and the cache
// disagree.
//
// One key per block and it is the whole point: the cache says a loc's climb
// *verb*, and no opcode exposes a verb to a script, so the verb is restated as a
// category — which a trigger CAN be bound to. `ladders_stairs/scripts/
// ladders.rs2` binds all eight of them and covers {covered} of the {total}
// records cache.osrs239 states a climb verb on.
//
// The category states TWO things, because a script needs two: which way the
// climb goes (the record's own menu verb), and whether the thing is a LADDER —
// the `_ladder` suffix. A ladder animates the climb the way the reference's
// `~climb_ladder` does; a staircase, a rock and a vine do not. The suffix is
// derived from the loc's name, see `LADDER_KEYWORDS` in the importer.
//
// The eight groups, and what puts a record in one (see the importer's docstring
// for why the direction grouping is by verb set and not by op slot):
//
{summary}//
// {exceptions_note}
"""


SHARED_HEADER = """\
// Locs that climb but belong to somebody else's category.
//
// Generated by tools/ladder_import.py — do not edit, re-run the importer.
//
// A loc record carries ONE category and `cachepack` refuses a second, so a loc
// that is already `category=door_opened` in `doors/configs/doors.loc` cannot
// also be `category=climb_down` here. All {count} of these are open trapdoors:
// the cache says "Climb-down" on op1 and "Close" on op2, so they are a door and
// a ladder at the same time and each op belongs to a different script.
//
// Bound by NAME, which is what makes it work: the trigger table's type rung
// beats its category rung, so `[oploc1,trapdoor_open]` below wins over
// `[oploc1,_door_opened]` in doors.rs2 for the same click, and the door script
// keeps the "Close" it is actually for.
//
// This is also the first thing that showed `doors.loc`'s op assumption is not
// universal. `[oploc1,_door_closed]` is right 387 times out of 388; the *opened*
// half is "Close" on op1 only 231 times of 388 — 144 records state no Close op
// at all, 13 put it on op2 (which doors.rs2 binds), and the rest say
// Climb-down, Open, Pass-through, Go-down or Peek. The engine's C door swap
// never noticed because it ignored the op number entirely.
//
// A handful of these NAME bindings would themselves collide with a
// hand-authored `.rs2` script that already owns the same `[oploc<N>,name]`
// trigger (usually a quest's own state-gated trapdoor body). The compiler
// only keeps one body for a duplicate header and silently drops the other,
// so those names are excluded here rather than emitted — see
// `read_other_rs2_triggers` in tools/ladder_import.py.
"""


def read_pack(path):
    out = {}
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("//"):
                continue
            key, _, value = line.partition("=")
            try:
                out[value] = int(key)
            except ValueError:
                continue
    return out


def read_other_overlays(content, mine):
    """Every loc name some OTHER authored `.loc` states a `category=` on."""
    claimed = {}
    root = os.path.join(content, "server", "scripts")
    for base, _dirs, files in os.walk(root):
        for name in files:
            path = os.path.join(base, name)
            if not name.endswith(".loc") or os.path.abspath(path) == os.path.abspath(mine):
                continue
            block = None
            with open(path) as handle:
                for line in handle:
                    line = line.strip()
                    match = re.match(r"^\[([^\]]+)\]$", line)
                    if match:
                        block = match.group(1)
                    elif block and line.startswith("category="):
                        claimed[block] = (os.path.relpath(path, content), line.split("=", 1)[1])
    return claimed


def read_other_rs2_triggers(content, mine):
    """Every `[oploc<N>,<name>]` header some OTHER hand-authored `.rs2` file
    declares. A generated NAME binding into `climb_shared.rs2` must not
    collide with a hand-authored trigger for the same loc — the compiler
    resolves same-name headers by silently keeping one body and dropping the
    other, so a quest's own gated trapdoor script must always win over the
    generic `~climb(-1)` fallback. Returns {(slot, name): relpath}."""
    claimed = {}
    root = os.path.join(content, "server", "scripts")
    mine_abs = os.path.abspath(mine)
    for base, _dirs, files in os.walk(root):
        for name in files:
            if not name.endswith(".rs2"):
                continue
            path = os.path.join(base, name)
            if os.path.abspath(path) == mine_abs:
                continue
            with open(path) as handle:
                for line in handle:
                    line = line.strip()
                    match = re.match(r"^\[oploc(\d+),([^\]]+)\]", line)
                    if match:
                        key = (int(match.group(1)), match.group(2))
                        claimed.setdefault(key, os.path.relpath(path, content))
    return claimed


def read_configs(path):
    records = {}
    current = None
    with open(path) as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            match = re.match(r"^\[([^\]]+)\]$", line)
            if match:
                current = match.group(1)
                records[current] = {}
                continue
            if current is None or "=" not in line or line.startswith("//"):
                continue
            key, _, value = line.partition("=")
            records[current][key] = value
    return records


def classify(records):
    """name -> (group | None, slot->verb map)"""
    grouped = {}
    exceptions = {}
    for name, record in records.items():
        ops = {}
        for slot in range(1, 6):
            verb = record.get("op%d" % slot)
            if verb in CLIMB_VERBS:
                ops[slot] = verb
        if not ops:
            continue
        verbs = set(ops.values())
        suffix = "_ladder" if is_ladder(name, record) else ""
        if len(verbs) == 1:
            grouped[name] = (SINGLE_VERB_GROUP[next(iter(verbs))] + suffix, ops)
        elif ops == SPIRAL:
            grouped[name] = (SPIRAL_GROUP + suffix, ops)
        else:
            exceptions[name] = ops
    return grouped, exceptions


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser()
    parser.add_argument("--content", default=os.path.join(here, "OSRS-Content", "osrs239-content"))
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--report", action="store_true")
    args = parser.parse_args()

    content = args.content
    records = read_configs(os.path.join(content, "configs", "all.loc"))
    named_categories = read_pack(os.path.join(content, "pack", "category.pack"))
    category_names = {v: k for k, v in named_categories.items()}

    grouped, exceptions = classify(records)

    out_path = os.path.join(content, "server", "scripts", "ladders_stairs", "configs", "ladders.loc")
    script_path = os.path.join(
        content, "server", "scripts", "ladders_stairs", "scripts", "climb_shared.rs2"
    )

    # Conflict rule 1: a record another authored `.loc` categorises keeps that
    # category and gets a NAME binding here instead. `cachepack` refuses a second
    # `category=` on one record, so this is an error and not a preference.
    claimed = read_other_overlays(content, out_path)
    shared = {}
    for name in sorted(grouped):
        if name in claimed:
            shared[name] = (grouped[name][1], claimed[name])
            del grouped[name]

    # Conflict rule 3: a NAME binding this would emit into climb_shared.rs2
    # must not collide with a trigger some hand-authored `.rs2` file already
    # declares for the same (op slot, loc name) — that script's body is
    # quest-state-gated (or otherwise bespoke) and would silently lose to
    # the generic `~climb` fallback if both declared the same header.
    rs2_claimed = read_other_rs2_triggers(content, script_path)
    rs2_shadowed = {}
    for name in sorted(shared):
        ops, _cat_info = shared[name]
        hit = {slot: rs2_claimed[(slot, name)] for slot in ops if (slot, name) in rs2_claimed}
        if hit:
            rs2_shadowed[name] = hit
            del shared[name]

    # Conflict rule 2: never overwrite a cache category somebody has named.
    skipped = {}
    for name in sorted(grouped):
        cache_category = records[name].get("category")
        if cache_category and int(cache_category) in category_names:
            skipped[name] = (int(cache_category), category_names[int(cache_category)])
    for name in skipped:
        del grouped[name]

    by_group = collections.defaultdict(list)
    for name, (group, _ops) in grouped.items():
        by_group[group].append(name)

    slots = collections.defaultdict(set)
    for name, (group, ops) in grouped.items():
        slots[group].update(ops)

    total = len(grouped) + len(exceptions) + len(skipped) + len(shared) + len(rs2_shadowed)
    summary_lines = []
    for group in GROUPS:
        members = by_group.get(group, [])
        summary_lines.append(
            "//   %-28s %5d records, ops %s\n"
            % (group, len(members), ",".join(str(s) for s in sorted(slots[group])))
        )
    exceptions_note = (
        "%d records carry two different climb verbs in a layout no group "
        "describes; they are bound by name in ladders.rs2: %s"
        % (len(exceptions), ", ".join(sorted(exceptions)))
    )
    if shared:
        exceptions_note += (
            "\n// %d more are categorised by another authored .loc and are bound by NAME in "
            "scripts/climb_shared.rs2 instead." % len(shared)
        )
    if skipped:
        exceptions_note += "\n// SKIPPED (already carry a NAMED cache category): " + ", ".join(
            "%s -> %d %s" % (n, c, cn) for n, (c, cn) in sorted(skipped.items())
        )
    if rs2_shadowed:
        exceptions_note += (
            "\n// SKIPPED (a hand-authored .rs2 script already declares this trigger): "
            + ", ".join(
                "%s -> %s" % (n, ", ".join("op%d in %s" % (s, p) for s, p in sorted(hit.items())))
                for n, hit in sorted(rs2_shadowed.items())
            )
        )

    body = [
        HEADER.format(
            covered=len(grouped),
            total=total,
            summary="".join(summary_lines),
            exceptions_note=exceptions_note,
        )
    ]
    for group in GROUPS:
        body.append("\n// ---- %s ----\n" % group)
        for name in sorted(by_group.get(group, [])):
            body.append("\n[%s]\ncategory=%s\n" % (name, group))
    text = "".join(body)

    script = [SHARED_HEADER.format(count=len(shared))]
    for name in sorted(shared):
        ops, (where, other) = shared[name]
        script.append(
            "\n// %s — `%s` in %s\n" % (", ".join("op%d %s" % kv for kv in sorted(ops.items())), other, where)
        )
        # Same ladder/stairs axis the categories carry, spelled as the proc name
        # instead: a name binding has no category to hang a suffix on.
        ladder = "_ladder" if is_ladder(name, records[name]) else ""
        for slot, verb in sorted(ops.items()):
            script.append(
                "[oploc%d,%s] %s\n"
                % (slot, name, "~climb%s(-1);" % ladder if verb == "Climb-down"
                   else "~climb%s(1);" % ladder if verb == "Climb-up"
                   else "~climb%s_unqualified;" % ladder)
            )
    script_text = "".join(script)

    if args.report:
        sys.stdout.write("".join(summary_lines))
        sys.stdout.write("// exceptions: %s\n" % exceptions_note)
        sys.stdout.write("// shared with another overlay: %s\n" % ", ".join(sorted(shared)))
        sys.stdout.write(
            "// shadowed by hand-authored .rs2 triggers: %s\n" % ", ".join(sorted(rs2_shadowed))
        )
        # The symbol-vs-display disagreement, both ways. The veto half is
        # already acted on (those records are in the un-animated groups above);
        # the keyword half is NOT, and is the standing work queue that
        # `ladders_stairs/README.md` points at. Printing both is what makes the
        # README's "one command" true, and what makes the queue shrink
        # visibly if anyone works it.
        vetoed = sorted(
            (name, records[name].get("name", ""))
            for name in grouped
            if any(k in name for k in LADDER_KEYWORDS)
            and not any(v in name for v in LADDER_VETO)
            and any(v in records[name].get("name", "").lower() for v in DISPLAY_VETO)
        )
        unclaimed = sorted(
            (name, records[name].get("name", ""))
            for name in grouped
            if not is_ladder(name, records[name])
            and any(k in records[name].get("name", "").lower() for k in LADDER_KEYWORDS)
        )
        sys.stdout.write(
            "\n// display says stairs, symbol says ladder — %d, ANIMATION REMOVED "
            "by DISPLAY_VETO:\n" % len(vetoed)
        )
        for name, display in vetoed:
            sys.stdout.write("//   %-48s %s\n" % (name, display))
        sys.stdout.write(
            "\n// display says ladder, symbol does not — %d, still UN-ANIMATED "
            "(open queue, see README):\n" % len(unclaimed)
        )
        for name, display in unclaimed:
            sys.stdout.write("//   %-48s %s\n" % (name, display))
        return 0
    if args.check:
        for path, want in ((out_path, text), (script_path, script_text)):
            try:
                with open(path) as handle:
                    on_disk = handle.read()
            except OSError:
                print("ladder_import: %s is missing" % path, file=sys.stderr)
                return 1
            if on_disk != want:
                print(
                    "ladder_import: %s does not match the cache — re-run tools/ladder_import.py"
                    % path,
                    file=sys.stderr,
                )
                return 1
        return 0
    for path, want in ((out_path, text), (script_path, script_text)):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as handle:
            handle.write(want)
    print("ladder_import: wrote %d blocks to %s" % (len(grouped), out_path))
    print("ladder_import: wrote %d name bindings to %s" % (len(shared), script_path))
    sys.stdout.write("".join(summary_lines))
    print("// exceptions: %s" % exceptions_note)
    return 0


if __name__ == "__main__":
    sys.exit(main())
