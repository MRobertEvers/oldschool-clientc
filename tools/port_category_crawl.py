#!/usr/bin/env python3
"""
port_category_crawl — npc categories for the LostCity port (triage §9 step 3b).

    tools/port_category_crawl.py --report     # the review artifact, TSV on stdout
    tools/port_category_crawl.py --groups     # what each osrs239 id actually holds
    tools/port_category_crawl.py --check      # the build bar
    tools/port_category_crawl.py --write-map  # regenerate port/categories.map

Why a crawler and not a lookup.

A category is the one config type LostCity never authors. `PackFile.ts` builds
`CategoryPack` with `validateCategoryPack` and no `.category` extension, and
`CategoryType.ts` says so in a sentence: the table is **derived**, regenerated
wholesale by crawling the `category=` key out of every `.npc`, `.loc` and `.obj`
(CONTENT_ARCHITECTURE.md §2.1, "fully regenerated ... Never authored"). So the
port cannot look a category up: there is nothing to look up. It has to run the
same crawl.

What differs here, and it is the whole design of this tool. In the reference the
crawl **mints** the id — `pack.max++` over its own records. Here
`content.ini` says `[namespace:category] ids = cache`, and the cache already
states the number: config opcode 18 on an npc record, decoded into
`RSCache_Dat2ConfigNpc.category` and exported to `configs/all.npc` on 9,149 of
16,292 records. So the crawl's job is to attach the reference's *name* to the id
**this tree's own records carry**, member by member, resolved by name:

    LostCity `.npc` blocks with `category=bank_teller`
        -> 6 record names
        -> resolved through configs/all.npc.compack (name -> osrs239 id)
        -> each of those records' own `category=` in configs/all.npc
        -> all six say 249
        -> `249=bank_teller` is READ, not chosen.

No id is ever copied between trees, which is the §4.1 bar. LostCity's own
category id for `bank_teller` is never read by anything here.

The three ways that goes wrong, all of them measured rather than hypothesised,
and all of them silent without this tool:

  * **split** — the members carry more than one osrs239 id (`petcat` is three,
    `shop_keeper` is six). There is no single answer and picking one binds the
    trigger to a third of the group.
  * **collision** — two reference names crawl to the *same* osrs239 id. A pack
    file is `id=name`; `troll_general` and `troll_spectator` both land on 309
    and only one of them can be the name.
  * **broader** — the id resolves, is unique, and names a *wider concept*.
    `black_demon`'s three members all carry 275, and 275 is the demon category:
    18 Lesser demons and 15 Greater demons sit in it beside the 17 Black demons.
    Minting it gives every lesser demon the black demon's drop table, and every
    check that exists today passes on it. This is `obj rock_sample1` (see
    port_name_diff.py) one namespace over.

Only a name whose osrs239 group is *what the reference name says* is minted, and
`--groups` prints the display-name histogram that decision was made from.
"""

import argparse
import collections
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TREE = os.path.join(REPO, "OSRS-Content", "osrs239-content")
DEFAULT_REF = os.path.expanduser("~/Documents/git_repos/LostCity_Server")

MAP_NAME = os.path.join("port", "categories.map")
PACK_NAME = os.path.join("pack", "category.pack")

# The disposition vocabulary. `minted` is the only one that asserts the name is
# in pack/category.pack; every other one asserts it is not.
MINTED = "minted"
DISPOSITIONS = {
    MINTED,       # the members agree on one id and the group is that concept
    "split",      # members carry >= 2 osrs239 ids — H8
    "collision",  # two reference names crawl to one id — one name per id
    "broader",    # unique id, wider concept: minting would bind the wrong set
    "orphan",     # no member resolves to a categorised record — needs an id
    "placeholder",  # `category_<n>`: a number wearing a name. Never a name here
}

# A reference category named after a *LostCity* id. `category_453` is not a
# name, it is the 2004 cache's number with an underscore in front, and this
# tree's 453 meaning something plausible is a coincidence, not a resolution.
PLACEHOLDER = re.compile(r"^category_\d+$")


# ----------------------------------------------------------------------
# Reading both trees
# ----------------------------------------------------------------------

def walk_configs(root, suffix, include_unpack):
    """Yield (record name, key, value, relative path) for every key of every
    `[block]` in every file with this suffix."""
    if not os.path.isdir(root):
        return
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in ("_test", "build")]
        if not include_unpack:
            dirnames[:] = [d for d in dirnames if d != "_unpack"]
        for filename in sorted(filenames):
            if not filename.endswith(suffix):
                continue
            path = os.path.join(dirpath, filename)
            rel = os.path.relpath(path, root)
            name = None
            with open(path, encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    text = line.strip()
                    if text.startswith("[") and text.endswith("]"):
                        name = text[1:-1]
                    elif name and "=" in text and not text.startswith(("//", ";")):
                        key, value = text.split("=", 1)
                        yield name, key.strip(), value.strip(), rel


def reference_members(ref):
    """category name -> [(record name, provenance)] for every LostCity `.npc`.

    `_unpack/` is included, and that is deliberate rather than sloppy. The
    reference's own build crawls it — CONTENT_ARCHITECTURE.md §2.1: "the queue is
    inside `scripts/`, so its `[name]` headers are crawled into the packs
    immediately" — and eleven of the categories content binds a trigger to
    (`cow`, `chicken`, `bear`, `pirate`, `witch`, `barbarian`, ...) are declared
    **only** there. Excluding it, which is what every other tool in this repo
    correctly does for authored content, turns eleven mechanical answers into
    eleven orphans. The provenance column records which half a member came from.
    """
    root = os.path.join(ref, "content", "scripts")
    groups = collections.defaultdict(list)
    for name, key, value, rel in walk_configs(root, ".npc", include_unpack=True):
        if key != "category":
            continue
        groups[value].append((name, "unpack" if rel.startswith("_unpack") else "authored"))
    return groups


def reference_trigger_refs(ref):
    """The category subjects content actually binds, by domain.

    `[ai_queue3,_cow]` — a leading underscore is a category subject
    (`ssc_compile.c`'s `is_category`), and an unresolved one kills the whole file
    rather than one line, which is why triage §6 ranks this above its 122
    references.
    """
    root = os.path.join(ref, "content", "scripts")
    header = re.compile(r"^\[([a-z_0-9]+),_([a-z_0-9]+)\]")
    domains = collections.defaultdict(set)
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in ("_unpack", "_test", "build")]
        for filename in sorted(filenames):
            if not filename.endswith(".rs2"):
                continue
            with open(os.path.join(dirpath, filename), encoding="utf-8",
                      errors="replace") as handle:
                for line in handle:
                    match = header.match(line)
                    if not match:
                        continue
                    trigger, subject = match.groups()
                    if trigger.startswith(("opnpc", "apnpc", "ai_")):
                        domains["npc"].add(subject)
                    elif trigger.startswith(("oploc", "aploc")):
                        domains["loc"].add(subject)
                    elif trigger.startswith(("opheld", "opobj", "apobj")):
                        domains["obj"].add(subject)
    return domains


def tree_records(tree, kind):
    """record name -> {key: value} from `configs/all.<kind>` (layer 0)."""
    path = os.path.join(tree, "configs", "all." + kind)
    records = {}
    current = None
    if not os.path.isfile(path):
        return records
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            text = line.strip()
            if text.startswith("[") and text.endswith("]"):
                current = records.setdefault(text[1:-1], {})
            elif current is not None and "=" in text and not text.startswith("//"):
                key, value = text.split("=", 1)
                current[key.strip()] = value.strip()
    return records


def read_pack(path):
    """id -> name, comments and blank lines dropped."""
    entries = {}
    if not os.path.isfile(path):
        return entries
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            text = line.strip()
            if not text or text.startswith("//") or "=" not in text:
                continue
            number, name = text.split("=", 1)
            try:
                entries[int(number)] = name.strip()
            except ValueError:
                pass
    return entries


# ----------------------------------------------------------------------
# The crawl
# ----------------------------------------------------------------------

class Row:
    __slots__ = ("name", "disposition", "category_id", "total", "resolved",
                 "referenced", "provenance", "ids", "evidence")

    def __init__(self, name):
        self.name = name
        self.disposition = "orphan"
        self.category_id = -1
        self.total = 0
        self.resolved = 0
        self.referenced = False
        self.provenance = ""
        self.ids = {}
        self.evidence = ""


def crawl(tree, ref):
    """Every reference npc category, classified. Returns (rows, group index)."""
    npc = tree_records(tree, "npc")
    by_category = collections.defaultdict(list)
    for record, fields in npc.items():
        if "category" in fields:
            try:
                cid = int(fields["category"])
            except ValueError:
                continue
            if cid != 0:
                by_category[cid].append(record)

    groups = reference_members(ref)
    referenced = reference_trigger_refs(ref).get("npc", set())

    # Two reference names landing on one id can only be seen across rows, so the
    # first pass records the id and the second decides the disposition.
    rows = {}
    for name, members in sorted(groups.items()):
        row = Row(name)
        row.total = len(members)
        row.referenced = name in referenced
        row.provenance = "+".join(sorted({p for _, p in members}))
        counts = collections.Counter()
        unnamed = 0
        uncategorised = 0
        for record, _provenance in members:
            fields = npc.get(record)
            if fields is None:
                unnamed += 1
            elif "category" not in fields or fields["category"] == "0":
                uncategorised += 1
            else:
                counts[int(fields["category"])] += 1
        row.ids = dict(counts)
        row.resolved = sum(counts.values())
        if PLACEHOLDER.match(name):
            row.disposition = "placeholder"
            row.evidence = ("named after a LostCity id, not a concept; this "
                            "tree's %s is a coincidence"
                            % (sorted(counts)[0] if counts else "match"))
        elif not counts:
            row.disposition = "orphan"
            row.evidence = ("%d member(s): %d not in configs/all.npc.compack, %d "
                            "carry no category" % (row.total, unnamed, uncategorised))
        elif len(counts) > 1:
            row.disposition = "split"
            row.category_id = -1
            row.evidence = "members carry %d ids: %s" % (
                len(counts), ",".join(str(i) for i in sorted(counts)))
        else:
            row.category_id = next(iter(counts))
            row.disposition = MINTED  # provisional; the passes below demote
            row.evidence = "%d/%d member(s) resolve, all to %d" % (
                row.resolved, row.total, row.category_id)
        rows[name] = row

    # Pass 2 — collisions. A pack file is `id=name`; two names on one id is not
    # a thing it can express, and picking one silently binds the other name's
    # scripts to the same set.
    owners = collections.defaultdict(list)
    for row in rows.values():
        if row.disposition == MINTED:
            owners[row.category_id].append(row.name)
    for cid, names in owners.items():
        if len(names) > 1:
            for name in names:
                rows[name].disposition = "collision"
                rows[name].evidence = ("%d is also crawled from %s — one id, one "
                                       "name" % (cid, ", ".join(sorted(set(names) - {name}))))

    return rows, by_category


def group_names(tree, by_category, cid):
    """The display-name histogram of an osrs239 category, which is the evidence
    the `broader` calls were made from."""
    npc = tree_records(tree, "npc")
    counts = collections.Counter()
    for record in by_category.get(cid, []):
        counts[npc.get(record, {}).get("name", "(unnamed)")] += 1
    return counts


# ----------------------------------------------------------------------
# The map
# ----------------------------------------------------------------------

HEADER = """\
# port/categories.map — npc categories, crawled (docs/LOSTCITY_PORT_TRIAGE.md §16).
#
# Generated by tools/port_category_crawl.py; the `broader` demotions are hand
# edits and are preserved across a regenerate (see --write-map).
#
# name  disposition  id  resolved/total  refs  provenance  evidence
#
#   minted       the members agree on one osrs239 id AND that id's group is the
#                concept the name states. The id is READ from this tree's own
#                records, never copied from LostCity.
#   split        members carry >= 2 ids. A human picks (triage H8).
#   collision    two reference names crawl to one id. One id, one name.
#   broader      unique id, wider concept — minting binds the wrong set.
#   orphan       no member resolves to a categorised record. Blocked: `category`
#                has no `server_base` in content.ini (triage §9 step 3c-0).
#   placeholder  `category_<n>` — a LostCity id wearing a name.
"""


def write_map(path, rows):
    existing = read_map(path)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(HEADER)
        for name in sorted(rows):
            row = rows[name]
            # A hand demotion to `broader` is a judgement about what an osrs239
            # group holds; the crawl cannot re-derive it and must not undo it.
            if name in existing and existing[name][0] == "broader" and \
                    row.disposition == MINTED:
                row.disposition = "broader"
                row.evidence = existing[name][4]
            handle.write("%s\t%s\t%d\t%d/%d\t%s\t%s\t%s\n" % (
                row.name, row.disposition, row.category_id, row.resolved,
                row.total, "yes" if row.referenced else "no", row.provenance,
                row.evidence))


def read_map(path):
    """name -> (disposition, id, resolved, total, evidence, referenced)."""
    rows = {}
    if not os.path.isfile(path):
        return rows
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 7:
                continue
            name, disposition, cid, ratio, refs, _prov, evidence = parts[:7]
            resolved, _, total = ratio.partition("/")
            rows[name] = (disposition, int(cid), int(resolved), int(total),
                          evidence, refs == "yes")
    return rows


# ----------------------------------------------------------------------
# Commands
# ----------------------------------------------------------------------

def report(tree, ref):
    rows, by_category = crawl(tree, ref)
    print("name\tdisposition\tid\tresolved/total\trefs\tprovenance\tevidence")
    for name in sorted(rows):
        row = rows[name]
        print("%s\t%s\t%d\t%d/%d\t%s\t%s\t%s" % (
            row.name, row.disposition, row.category_id, row.resolved, row.total,
            "yes" if row.referenced else "no", row.provenance, row.evidence))
    counts = collections.Counter(r.disposition for r in rows.values())
    referenced = collections.Counter(
        r.disposition for r in rows.values() if r.referenced)
    print("\n# %d reference npc categories: %s" % (
        len(rows), ", ".join("%s %d" % kv for kv in sorted(counts.items()))),
        file=sys.stderr)
    print("# of the %d bound by a trigger: %s" % (
        sum(referenced.values()),
        ", ".join("%s %d" % kv for kv in sorted(referenced.items()))),
        file=sys.stderr)
    return 0


def groups(tree, ref):
    rows, by_category = crawl(tree, ref)
    npc = tree_records(tree, "npc")
    for name in sorted(rows):
        row = rows[name]
        for cid in sorted(row.ids):
            counts = collections.Counter()
            for record in by_category.get(cid, []):
                counts[npc.get(record, {}).get("name", "(unnamed)")] += 1
            print("%-26s %-12s %5d  %3d npc(s)  %s" % (
                name, row.disposition, cid, len(by_category.get(cid, [])),
                ", ".join("%s x%d" % (k, v) for k, v in counts.most_common(6))))
    return 0


def check(tree, ref, verbose):
    map_path = os.path.join(tree, MAP_NAME)
    stated = read_map(map_path)
    if not stated:
        print("port_category_crawl: no %s — the map IS the gate, so its absence "
              "is the failure" % MAP_NAME, file=sys.stderr)
        return 1

    pack = read_pack(os.path.join(tree, PACK_NAME))
    by_name = {}
    for cid, name in pack.items():
        by_name.setdefault(name, cid)

    problems = []

    # --- the tree against the map ------------------------------------------
    for name, (disposition, cid, _resolved, _total, _evidence, _refs) in sorted(stated.items()):
        if disposition not in DISPOSITIONS:
            problems.append("`%s` has an unknown disposition `%s`" % (name, disposition))
        elif disposition == MINTED:
            if name not in by_name:
                problems.append(
                    "`%s` is minted at %d and is not in %s — a minted category "
                    "whose name never landed binds nothing, silently"
                    % (name, cid, PACK_NAME))
            elif by_name[name] != cid:
                problems.append(
                    "`%s` is minted at %d but %s says %d"
                    % (name, cid, PACK_NAME, by_name[name]))
        elif name in by_name:
            problems.append(
                "`%s` is %s (%s) but %s names it at %d — a category held back "
                "for a human was minted anyway"
                % (name, disposition, _evidence, PACK_NAME, by_name[name]))

    # --- the map against the reference --------------------------------------
    if ref and os.path.isdir(os.path.join(ref, "content", "scripts")):
        rows, _by_category = crawl(tree, ref)
        for name, row in sorted(rows.items()):
            if name not in stated:
                problems.append(
                    "`%s` is declared by the reference on %d record(s) and has no "
                    "row — a new reference category is a decision, not a default"
                    % (name, row.total))
                continue
            disposition, cid, resolved, total, _evidence, _refs = stated[name]
            if row.total != total or row.resolved != resolved:
                problems.append(
                    "`%s`: the map records %d/%d member(s) resolving, the crawl "
                    "now finds %d/%d — the membership moved in one of the two trees"
                    % (name, resolved, total, row.resolved, row.total))
            if disposition == MINTED and row.category_id != cid:
                problems.append(
                    "`%s` is minted at %d but its members now carry %s — the id "
                    "a minted name states is READ from the members and this one "
                    "no longer reads that way"
                    % (name, cid, sorted(row.ids) or "nothing"))
        for name in sorted(set(stated) - set(rows)):
            problems.append("`%s` has a row but the reference no longer declares "
                            "it on any .npc" % name)
    else:
        print("port_category_crawl: no reference tree at %s — the half that "
              "re-runs the crawl is skipped; the tree-against-map half still ran"
              % ref, file=sys.stderr)

    for problem in problems:
        print("port_category_crawl: %s" % problem, file=sys.stderr)
    if problems:
        print("port_category_crawl: %d problem(s)" % len(problems), file=sys.stderr)
        return 1
    if verbose:
        print("port_category_crawl: %d row(s), %d minted, all agree with %s"
              % (len(stated),
                 sum(1 for v in stated.values() if v[0] == MINTED), PACK_NAME))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tree", default=DEFAULT_TREE)
    parser.add_argument("--ref", default=DEFAULT_REF)
    parser.add_argument("--report", action="store_true")
    parser.add_argument("--groups", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--write-map", action="store_true")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    if args.report:
        return report(args.tree, args.ref)
    if args.groups:
        return groups(args.tree, args.ref)
    if args.write_map:
        rows, _ = crawl(args.tree, args.ref)
        write_map(os.path.join(args.tree, MAP_NAME), rows)
        print("wrote %s (%d rows)" % (os.path.join(args.tree, MAP_NAME), len(rows)))
        return 0
    if args.check:
        return check(args.tree, args.ref, args.verbose)
    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
