#!/usr/bin/env python3
"""
The varp/varn/vars reclassification gate — LOSTCITY_PORT_TRIAGE.md §9 step 3d.

The bug class this exists for, in one line: **a 2004 varp is very often a rev-230
varbit BIT RANGE**, and `%name = value` on the container compiles, runs,
transmits, and destroys every other variable packed into it.

Two things stop that, and they are different things:

  * The *mechanism* — `sscompile`'s carrier rule and the engine's runtime
    backstop. Those refuse a whole-varp write to a shared container and need no
    table: the cache's own `basevar=` key says which varps are shared.
  * The *data* — which variable a reference `%name` becomes here. That is one
    human decision per name, it cannot be derived, and a wrong answer *resolves*.
    `port/vars.map` is where those decisions are written down, and this script is
    what keeps the file honest.

The distinction the map exists to make, and no name check can:

    %prayer0  -> varp 83, the prayer block          CORRECT
    %prayer9  -> varp 92, the *Clan Wars* block     WRONG, and it resolves

Both spellings exist here, both are unique, both look like a hit. The osrs239
gameval table kept the legacy `prayerN` labels on ids that were reused for
something else, so six of the reference's fifteen prayer toggles land on
somebody else's state — 68 whole-varp writes' worth. That is what `false-friend`
means in the map and why every one of them carries its evidence in the row.

Usage:
    tools/port_vars_diff.py --report      what the two trees say, as a table
    tools/port_vars_diff.py --write-map   regenerate the mechanical columns
    tools/port_vars_diff.py --check       fail if the map and the trees disagree

`--check` runs inside `make -C src test-port`. What it enforces:

  1. every `%name` the reference's scripts reference has a row (a new one is an
     unclassified name, which is bar 1 of §13 restated for this namespace)
  2. the *resolution* columns still describe the trees — a name that changed
     kind, id, or carrier status has had its decision invalidated
  3. a `false-friend` name is not spelled by this tree's own scripts
  4. a `varbit` row's target exists here and really is a varbit
  5. a `carrier` row carries no target — an undecided bit range must stay
     visibly undecided rather than acquiring a plausible one

What it deliberately does NOT enforce: the use/write/bitop counts. Those move
whenever the reference checkout does (recorded three times over in §14.5, §15.9
and §16.10), and a bar that goes red because somebody upstream added a line is a
bar people learn to re-baseline without reading.
"""

import argparse
import collections
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TREE = os.path.join(REPO, "OSRS-Content", "osrs239-content")
REF = "/Users/matthewevers/Documents/git_repos/LostCity_Server/content"
MAP = os.path.join(TREE, "port", "vars.map")

SKIP_DIRS = ("build", ".git")

DISPOSITIONS = {
    # decided, with evidence in the row
    "varbit",        # the reference's varp is a varbit here; `target` names it
    "present",       # already declared in this tree, correctly
    "clean-varp",    # a non-carrier varp of the same concept; safe when its slice lands
    "server-varp",   # LostCity bookkeeping with no cache equivalent; server-allocated, transmit=no
    "varn",          # the reference already scopes it to an npc
    "vars",          # the reference already scopes it to the world
    # undecided, and listed as such
    "false-friend",  # resolves here, to a different concept. NEVER auto-map
    "carrier",       # resolves to a shared container; which bit range is a human decision
    "bitfield",      # bit-packed in the reference and unresolved here: becomes N varbits
    "unresolved",    # no defensible target yet
}

COLUMNS = [
    "name", "ref_kind", "ref_layer", "uses", "writes", "bitops",
    "here_kind", "here_id", "carrier_bits", "disposition", "target", "evidence",
]


# ------------------------------------------------------------------
# Reading the two trees
# ------------------------------------------------------------------

def walk(root, suffixes, skip=SKIP_DIRS):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in skip]
        for name in filenames:
            if name.endswith(suffixes):
                yield os.path.join(dirpath, name)


def blocks(path):
    """Yield (section, {key: value}, line) for a LostCity-grammar config."""
    section, kv, line_no = None, {}, 0
    with open(path, encoding="utf-8", errors="replace") as handle:
        for i, raw in enumerate(handle, 1):
            text = raw.split("//")[0].strip()
            if not text:
                continue
            if text.startswith("["):
                if section is not None:
                    yield section, kv, line_no
                section, kv, line_no = text[1:text.index("]")], {}, i
            elif "=" in text and section is not None:
                key, value = text.split("=", 1)
                kv[key.strip()] = value.strip()
    if section is not None:
        yield section, kv, line_no


def pack_ids(namespace):
    """name -> id from the namespace's two layers: the cache's member index
    (configs/all.<ns>.compack) and the server's allocation ledger
    (pack/<ns>.alloc)."""
    out = {}
    layers = (
        os.path.join(TREE, "configs", "all.%s.compack" % namespace),
        os.path.join(TREE, "pack", "%s.alloc" % namespace),
    )
    for path in layers:
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as handle:
            for raw in handle:
                text = raw.split("//")[0].strip()
                if "=" not in text:
                    continue
                number, name = text.split("=", 1)
                try:
                    out[name.strip()] = int(number.strip())
                except ValueError:
                    pass
    return out


def carrier_set():
    """varp name -> [(varbit, startbit, endbit)], from the cache's own records."""
    carriers = collections.defaultdict(list)
    for name, kv, _ in blocks(os.path.join(TREE, "configs", "all.varbit")):
        base = kv.get("basevar")
        if base is None:
            continue
        carriers[base].append((name, int(kv.get("startbit", -1)), int(kv.get("endbit", -1))))
    return carriers


def reference_declarations():
    """name -> dict(kind, layer, file, line, kv). Authored beats _unpack."""
    decl = {}
    for suffix, kind in ((".varp", "varp"), (".varn", "varn"), (".vars", "vars")):
        for path in walk(os.path.join(REF, "scripts"), (suffix,)):
            layer = "_unpack" if "/_unpack/" in path else "authored"
            for name, kv, line in blocks(path):
                if name in decl and decl[name]["layer"] == "authored":
                    continue
                decl[name] = dict(kind=kind, layer=layer,
                                  file=os.path.relpath(path, REF), line=line, kv=kv)
    return decl


NAME_RE = re.compile(r"%([A-Za-z_][A-Za-z0-9_]*)")
ASSIGN_RE = re.compile(r"^\s*%([A-Za-z_][A-Za-z0-9_]*)\s*=")
BIT_RE = re.compile(r"\b(?:testbit|setbit|clearbit)\s*\(\s*%([A-Za-z_][A-Za-z0-9_]*)")


def scan_scripts(root, skip=SKIP_DIRS):
    """(uses, whole-varp writes, bit ops) per `%name` over a script tree."""
    uses, writes, bitops = collections.Counter(), collections.Counter(), collections.Counter()
    for path in walk(root, (".rs2",), skip=skip):
        with open(path, encoding="utf-8", errors="replace") as handle:
            for raw in handle:
                text = raw.split("//")[0]
                for match in NAME_RE.finditer(text):
                    uses[match.group(1)] += 1
                assigned = ASSIGN_RE.match(text)
                if assigned:
                    writes[assigned.group(1)] += 1
                for match in BIT_RE.finditer(text):
                    bitops[match.group(1)] += 1
    return uses, writes, bitops


def measure():
    """Everything both trees say, keyed by reference `%name`."""
    carriers = carrier_set()
    varps = pack_ids("varp")
    varbits = pack_ids("varbit")
    decl = reference_declarations()
    # `_unpack` is decompiled reference *config*, not script: exclude it from the
    # usage scan (it holds no .rs2) but keep its declarations, which is where 91
    # of the referenced varps are stated.
    uses, writes, bitops = scan_scripts(os.path.join(REF, "scripts"), skip=SKIP_DIRS + ("_unpack",))

    tree_declared = set()
    for path in walk(os.path.join(TREE, "server", "scripts"), (".varp",)):
        for name, _, _ in blocks(path):
            tree_declared.add(name)

    rows = {}
    for name in sorted(uses):
        d = decl.get(name)
        here_varp = varps.get(name)
        here_varbit = varbits.get(name)
        if here_varp is not None:
            here_kind, here_id = "varp", here_varp
        elif here_varbit is not None:
            here_kind, here_id = "varbit", here_varbit
        else:
            here_kind, here_id = "none", -1
        rows[name] = dict(
            name=name,
            ref_kind=d["kind"] if d else "none",
            ref_layer=d["layer"] if d else "none",
            uses=uses[name], writes=writes[name], bitops=bitops[name],
            here_kind=here_kind, here_id=here_id,
            carrier_bits=len(carriers.get(name, [])),
            ref_kv=(d["kv"] if d else {}),
            ref_file=("%s:%d" % (d["file"], d["line"])) if d else "-",
            declared_here=name in tree_declared,
        )
    return rows, carriers, varps, varbits, tree_declared


# ------------------------------------------------------------------
# The map file
# ------------------------------------------------------------------

HEADER = """\
# port/vars.map — one row per `%name` the LostCity reference's scripts use.
#
# LOSTCITY_PORT_TRIAGE.md §9 step 3d / §7.5. Generated columns are re-measured by
# `tools/port_vars_diff.py --check` (which runs in `make -C src test-port`); the
# `disposition`, `target` and `evidence` columns are HUMAN and are never
# regenerated. A name with no row is an unclassified name, and that is an error —
# not because the row is paperwork, but because the failure mode of this
# namespace is a name that resolves to the wrong thing and says nothing.
#
# columns
#   name           the reference's spelling
#   ref_kind       what the reference declares it: varp / varn / vars / none
#   ref_layer      authored (a .varp beside its scripts) or _unpack (decompiled)
#   uses/writes    references, and whole-container writes, in the reference's .rs2
#   bitops         testbit/setbit/clearbit calls on it — a bit-packed container
#   here_kind      what the name resolves to in osrs239: varp / varbit / none
#   here_id        that id
#   carrier_bits   how many varbits osrs239 packs into it. > 0 means a whole-varp
#                  write destroys other variables (this is the whole point)
#   disposition    the decision. See below.
#   target         where the value goes here, when that is decided
#   evidence       why. A row with a target and no evidence is a guess.
#
# dispositions
#   varbit         decided: the reference's varp is a varbit here
#   present        already declared in this tree, and correctly
#   clean-varp     a non-carrier varp of the same concept — safe when its slice lands
#   server-varp    LostCity server bookkeeping with no cache equivalent. Destination
#                  is a server-allocated player varp, transmit=no (18 already exist)
#   varn / vars    the reference already scopes it to an npc / to the world
#   false-friend   RESOLVES HERE, TO A DIFFERENT CONCEPT. Never auto-map one
#   carrier        resolves to a shared container; which bit range is a per-quest
#                  human decision, and until it is made this stays unported
#   bitfield       bit-packed in the reference and unresolved here: becomes N
#                  varbits, which this cache cannot yet author (see §18)
#   unresolved     no defensible target yet
#
# An unported varp is a gap. A wrongly-classified one is a corruption. The file
# is arranged so the second cannot be reached by accident.
"""


def load_map():
    if not os.path.exists(MAP):
        return None
    rows = {}
    with open(MAP, encoding="utf-8") as handle:
        for raw in handle:
            if raw.startswith("#") or not raw.strip():
                continue
            fields = raw.rstrip("\n").split("\t")
            if len(fields) != len(COLUMNS):
                continue
            row = dict(zip(COLUMNS, fields))
            rows[row["name"]] = row
    return rows


def default_disposition(fact):
    if fact["ref_kind"] == "varn":
        return "varn"
    if fact["ref_kind"] == "vars":
        return "vars"
    if fact["here_kind"] == "varbit":
        return "varbit"
    if fact["here_kind"] == "varp":
        # Carrier first, and deliberately ahead of `declared_here`: this tree
        # declaring `[prayer0]` means the *container* must be transmitted, which
        # is correct and says nothing about whether a reference script writing
        # `%prayer0` is. Nine of the tree's own declarations are carriers.
        if fact["carrier_bits"]:
            return "carrier"
        return "present" if fact["declared_here"] else "clean-varp"
    if fact["bitops"]:
        return "bitfield"
    return "unresolved"


def write_map(facts, existing):
    lines = [HEADER]
    for name in sorted(facts):
        fact = facts[name]
        prior = (existing or {}).get(name, {})
        row = [
            name, fact["ref_kind"], fact["ref_layer"],
            str(fact["uses"]), str(fact["writes"]), str(fact["bitops"]),
            fact["here_kind"], str(fact["here_id"]), str(fact["carrier_bits"]),
            prior.get("disposition") or default_disposition(fact),
            prior.get("target", "-") or "-",
            prior.get("evidence", "-") or "-",
        ]
        lines.append("\t".join(row))
    os.makedirs(os.path.dirname(MAP), exist_ok=True)
    with open(MAP, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    return len(facts)


# ------------------------------------------------------------------
# Commands
# ------------------------------------------------------------------

def report(facts, carriers):
    counts = collections.Counter()
    existing = load_map() or {}
    for name, fact in facts.items():
        counts[existing.get(name, {}).get("disposition") or default_disposition(fact)] += 1
    print("reference `%%name` references: %d" % len(facts))
    for key, value in sorted(counts.items(), key=lambda kv: -kv[1]):
        print("  %-14s %4d" % (key, value))
    print()
    print("carrier varps the reference writes whole (the §7.5 class):")
    rows = [f for f in facts.values() if f["carrier_bits"] > 0]
    for fact in sorted(rows, key=lambda f: -f["carrier_bits"]):
        print("  %-32s varp %-5d %2d varbit(s)  %d use(s) %d whole-write(s)" % (
            fact["name"], fact["here_id"], fact["carrier_bits"], fact["uses"], fact["writes"]))
    print()
    print("total whole-container writes in the reference: %d" %
          sum(f["writes"] for f in facts.values()))
    print("  ... landing on a carrier varp: %d over %d name(s)" % (
        sum(f["writes"] for f in rows), len([f for f in rows if f["writes"]])))


def check(facts, carriers, varbits, tree_uses):
    rows = load_map()
    problems = []
    if rows is None:
        print("port_vars_diff: no %s — run --write-map" % os.path.relpath(MAP, REPO))
        return 1

    for name, fact in facts.items():
        row = rows.get(name)
        if row is None:
            problems.append("`%%%s` is referenced by the reference and has no row — "
                            "an unclassified name" % name)
            continue
        # Only the columns a decision depends on. Counts drift with the
        # reference checkout and are informational.
        for column, measured in (("ref_kind", fact["ref_kind"]),
                                 ("here_kind", fact["here_kind"]),
                                 ("here_id", str(fact["here_id"])),
                                 ("carrier_bits", str(fact["carrier_bits"]))):
            if row[column] != str(measured):
                problems.append(
                    "`%%%s`: %s is now `%s`, the map says `%s` — the decision in this "
                    "row was made about a different fact" % (name, column, measured, row[column]))

    for name, row in rows.items():
        if row["disposition"] not in DISPOSITIONS:
            problems.append("`%%%s`: unknown disposition `%s`" % (name, row["disposition"]))
            continue
        if name not in facts:
            problems.append("`%%%s` has a row but the reference no longer references it" % name)
            continue
        if row["disposition"] == "false-friend" and name in tree_uses:
            problems.append(
                "`%%%s` is a false friend — it resolves here to %s %s, which is a different "
                "concept — and this tree's own scripts spell it" % (
                    name, row["here_kind"], row["here_id"]))
        if row["target"] not in ("-", ""):
            if row["target"] not in varbits:
                problems.append("`%%%s`: target `%s` is not a varbit in this cache" % (
                    name, row["target"]))
            if row["evidence"] in ("-", ""):
                problems.append(
                    "`%%%s` names the target `%s` with no evidence — a target without one "
                    "is a guess wearing a decision's clothes" % (name, row["target"]))
        if row["disposition"] == "carrier" and row["target"] not in ("-", ""):
            problems.append(
                "`%%%s` is marked `carrier` — the bit range is undecided — but names a "
                "target `%s`. Decide it or leave it blank" % (name, row["target"]))
        if row["disposition"] in ("varbit", "false-friend", "server-varp") and \
                row["evidence"] in ("-", ""):
            problems.append("`%%%s` is decided (`%s`) with no evidence" % (
                name, row["disposition"]))

    for problem in problems:
        print("port_vars_diff: %s" % problem)
    if problems:
        print("port_vars_diff: %d problem(s)" % len(problems))
        return 1
    print("port_vars_diff: %d row(s), all classified and still describing the trees" % len(rows))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", action="store_true")
    parser.add_argument("--write-map", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    if not os.path.isdir(REF):
        # The reference is a sibling checkout, not a submodule. Absent, the check
        # degrades to "nothing to compare against" rather than failing the build —
        # the same rule port_name_diff and port_config_diff already follow.
        print("port_vars_diff: no reference tree at %s (skipping)" % REF)
        return 0

    facts, carriers, _varps, varbits, _declared = measure()
    tree_uses, _w, _b = scan_scripts(os.path.join(TREE, "server", "scripts"))

    if args.write_map:
        count = write_map(facts, load_map())
        print("port_vars_diff: wrote %d row(s) to %s" % (count, os.path.relpath(MAP, REPO)))
        return 0
    if args.check:
        return check(facts, carriers, varbits, set(tree_uses))
    report(facts, carriers)
    return 0


if __name__ == "__main__":
    sys.exit(main())
