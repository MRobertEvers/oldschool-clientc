#!/usr/bin/env python3
"""
port_constant_diff — the constant gate for the LostCity port (triage §9 step 3a).

    tools/port_constant_diff.py --report      # the review artifact, TSV on stdout
    tools/port_constant_diff.py --check       # the build bar
    tools/port_constant_diff.py --unclassified  # reference constants with no row

Why this exists. A `.constant` is the one content symbol with no namespace to
resolve against: `^cook_complete` names nothing in any pack, so the
name-resolution gate (§14) is structurally blind to it. Copying `= 2` as `= 3`
gives a file that parses, compiles, boots, and plays a slightly wrong quest.
`sscompile` now refuses a constant declared twice, which catches a collision —
it cannot catch a value that is simply the wrong number for this era.

Measured on the two trees: of the reference's 1,562 constants, 111 are already
spelled here and **14 of those disagree on the value** — twelve prayers and two
headicons, because this cache's prayer book has 29 entries where the reference's
has 15 and rev 230 gave the overhead icons an archive of their own. Every one of
those 14 is a name that resolves, in a file that compiles, meaning something
else. That is the class this gate exists for, and it is the same shape as
`obj rock_sample1` one layer over (see port_name_diff.py).

So `<tree>/port/constants.map` states, for all 1,562, what this tree does with
it, and this checks the tree against that statement:

  * a reference constant with no row               -> classify it
  * a row saying `present`/`landed` whose value
    no longer matches the reference                -> the reference moved, or
                                                      somebody edited a copy
  * a row saying `rederived`/`renamed` whose stated
    destination value is not what the tree holds   -> the deliberate difference
                                                      drifted
  * a row saying `defer-*` or `never` whose name
    IS in the tree                                 -> a deferred number got
                                                      copied anyway; this is the
                                                      failure the map is for
  * a tree constant in neither the rows nor the
    `tree-only` list                               -> undeclared

The reference half degrades: without a LostCity checkout the first two bars
cannot run and are skipped with a note. Everything that compares the tree to the
map still runs, and that is the half that catches a deferred constant being
copied in.
"""

import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TREE = os.path.join(REPO, "OSRS-Content", "osrs239-content")
DEFAULT_REF = os.path.expanduser("~/Documents/git_repos/LostCity_Server")

MAP_NAME = os.path.join("port", "constants.map")

# Dispositions that assert the name IS in this tree, and dispositions that
# assert it is NOT. Anything else is a spelling error in the map.
IN_TREE = {"present", "landed", "rederived"}
NOT_IN_TREE = {"defer-varbit", "defer-table", "defer-slice", "never"}
# `renamed` is its own case: the reference spelling must be absent and the
# destination spelling present.
RENAMED = "renamed"

DECL = re.compile(r"^\s*\^([A-Za-z0-9_]+)\s*=\s*(.*?)\s*$")
ROW = re.compile(r"^(\S+)\t(\S+)\t(.*?) \| (.*?) \| (.*)$")
# `renamed` / `rederived` destinations end in `<something> = <value>`; the value
# is what the tree has to hold.
DEST_VALUE = re.compile(r"^(.*?)\s*=\s*(.*)$")


def read_constants(root):
    """name -> (value, relative path, line). Verbatim text: a constant expands to
    whatever the grammar accepts, so parsing the value here would be a second
    opinion about a thing only the use site can decide."""
    found = {}
    if not os.path.isdir(root):
        return found
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in ("_unpack", "_test", "build")]
        for filename in sorted(filenames):
            if not filename.endswith(".constant"):
                continue
            path = os.path.join(dirpath, filename)
            rel = os.path.relpath(path, root)
            with open(path, encoding="utf-8", errors="replace") as handle:
                for number, line in enumerate(handle, 1):
                    match = DECL.match(line.rstrip("\n"))
                    if not match:
                        continue
                    value = match.group(2)
                    if "//" in value:
                        value = value.split("//")[0].strip()
                    found[match.group(1)] = (value, rel, number)
    return found


def read_map(path):
    """name -> (disposition, reference value, destination, origin)."""
    rows = {}
    tree_only = {}
    if not os.path.isfile(path):
        return rows, tree_only
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("#") or not line.strip():
                continue
            match = ROW.match(line.rstrip("\n"))
            if not match:
                continue
            name, dispo, value, dest, origin = match.groups()
            if dispo == "tree-only":
                tree_only[name] = (value, dest)
            else:
                rows[name] = (dispo, value, dest, origin)
    return rows, tree_only


def destination_value(dest):
    match = DEST_VALUE.match(dest)
    return match.group(2) if match else None


def check(tree, ref, verbose):
    rows, tree_only = read_map(os.path.join(tree, MAP_NAME))
    if not rows:
        print("port_constant_diff: no %s — the map IS the gate, so its absence "
              "is the failure" % MAP_NAME, file=sys.stderr)
        return 1

    here = read_constants(os.path.join(tree, "server", "scripts"))
    there = read_constants(os.path.join(ref, "content", "scripts")) if ref else {}

    problems = []

    # --- the tree against the map -------------------------------------------
    for name, (dispo, value, dest, origin) in sorted(rows.items()):
        if dispo in IN_TREE:
            if name not in here:
                problems.append("`^%s` is %s but this tree does not declare it (%s)"
                                % (name, dispo, origin))
            elif dispo in ("present", "landed") and here[name][0] != value:
                problems.append(
                    "`^%s` is %s: the map says `%s`, %s says `%s`"
                    % (name, dispo, value, here[name][1], here[name][0]))
            elif dispo == "rederived":
                want = destination_value(dest)
                if want is not None and here[name][0] != want:
                    problems.append(
                        "`^%s` is rederived to `%s` but %s says `%s` — a "
                        "deliberate difference that drifted is indistinguishable "
                        "from a copy that went wrong"
                        % (name, want, here[name][1], here[name][0]))
        elif dispo == RENAMED:
            if name in here:
                problems.append(
                    "`^%s` is renamed and must NOT be declared here; the map "
                    "points at %s" % (name, dest))
            target = dest.split("=")[0].strip()
            if target and target not in here:
                problems.append("`^%s` is renamed to `^%s`, which this tree does "
                                "not declare" % (name, target))
        elif dispo in NOT_IN_TREE:
            if name in here:
                problems.append(
                    "`^%s` is %s (%s) but %s declares it — a deferred constant "
                    "was copied in without its row being changed"
                    % (name, dispo, dest, here[name][1]))
        else:
            problems.append("`^%s` has an unknown disposition `%s`" % (name, dispo))

    for name in sorted(here):
        if name in rows or name in tree_only:
            continue
        problems.append("`^%s` (%s) is in neither the rows nor `tree-only` — every "
                        "constant this tree declares is a decision the map has to "
                        "record" % (name, here[name][1]))

    # --- the map against the reference --------------------------------------
    if there:
        for name in sorted(there):
            if name not in rows:
                problems.append(
                    "`^%s` (%s:%d) is declared by the reference and has no row — "
                    "classify it before it is copied"
                    % (name, there[name][1], there[name][2]))
                continue
            dispo, value, _dest, _origin = rows[name]
            if there[name][0] != value:
                problems.append(
                    "`^%s`: the map records the reference as `%s`, the reference "
                    "now says `%s` (%s)"
                    % (name, value, there[name][0], there[name][1]))
        stale = [n for n in rows if n not in there]
        for name in sorted(stale):
            problems.append("`^%s` has a row but the reference no longer declares "
                            "it" % name)
    else:
        print("port_constant_diff: no reference tree at %s — the reference half "
              "of the bar is skipped (a bar that cannot run has not failed)"
              % ref, file=sys.stderr)

    if problems:
        for problem in problems:
            print("port_constant_diff: %s" % problem, file=sys.stderr)
        print("port_constant_diff: %d problem(s)" % len(problems), file=sys.stderr)
        return 1
    # Printed on success too, and deliberately: the counts are the only visible
    # evidence the bar ran against a reference at all, and `0 reference
    # constant(s)` is the difference between "agreed" and "half of this was
    # skipped".
    print("port_constant_diff: %d row(s) — %d here, %d in the reference, agreed"
          % (len(rows), len(here), len(there)))
    return 0


def report(tree, ref):
    rows, tree_only = read_map(os.path.join(tree, MAP_NAME))
    here = read_constants(os.path.join(tree, "server", "scripts"))
    print("name\tdisposition\tlc_value\tthis_value\tdestination\tlc_file")
    for name, (dispo, value, dest, origin) in sorted(rows.items()):
        mine = here[name][0] if name in here else "-"
        print("%s\t%s\t%s\t%s\t%s\t%s" % (name, dispo, value, mine, dest, origin))
    for name, (value, where) in sorted(tree_only.items()):
        print("%s\ttree-only\t-\t%s\t-\t%s" % (name, value, where))


def unclassified(tree, ref):
    rows, _ = read_map(os.path.join(tree, MAP_NAME))
    there = read_constants(os.path.join(ref, "content", "scripts"))
    missing = sorted(n for n in there if n not in rows)
    for name in missing:
        value, path, line = there[name]
        print("%s\t%s\t%s:%d" % (name, value, path, line))
    print("%d unclassified" % len(missing), file=sys.stderr)
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tree", default=DEFAULT_TREE)
    parser.add_argument("--ref", default=DEFAULT_REF)
    parser.add_argument("--report", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--unclassified", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    ref = args.ref if args.ref and os.path.isdir(
        os.path.join(args.ref, "content", "scripts")) else None

    if args.unclassified:
        if not ref:
            print("port_constant_diff: needs a reference tree", file=sys.stderr)
            return 1
        return unclassified(args.tree, ref)
    if args.report:
        report(args.tree, ref)
        return 0
    return check(args.tree, ref, args.verbose)


if __name__ == "__main__":
    sys.exit(main())
