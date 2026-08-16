#!/usr/bin/env python3
"""
port_category_crawl — categories for the LostCity port (triage §9 step 3b).

    tools/port_category_crawl.py --report     # the review artifact, TSV on stdout
    tools/port_category_crawl.py --groups     # what each osrs239 id actually holds
    tools/port_category_crawl.py --check      # the build bar (every domain)
    tools/port_category_crawl.py --write-map  # regenerate the domain's map

    --domain npc | loc        which record type's `category=` to crawl (default npc)

Two domains, one id space, one map file each.

`npc` came first and `loc` joined it on 2026-08-02, when `dat2_config_loc.c`
stopped discarding config opcode 61. They are separate *crawls* — a reference
`.loc` block's `category=` is a different claim from a `.npc` block's — over one
shared *namespace*: `pack/category.pack` is `id=name` for all three record types
(obj states one too, at opcode 94, and has no reference crawl because LostCity
authors no obj categories this port needs). So a name minted by one domain is
visible to the others, which is why `--check` runs every domain and why the pack
file itself is the collision test: one id, one name, whoever wrote it.

`obj` is deliberately absent as a *crawl* even though `reference_trigger_refs`
collects the domain. The obj names in `pack/category.pack` were read off the
cache by grouping the obj table, not crawled out of the reference, and a crawl
that re-derived them would answer a question nobody asked.

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
    and only one of them can be the name. **The resolution is never to pick
    one.** An id two reference names both land on holds both concepts, so it is
    wider than either, so it is `broader` — measured, not assumed: 309 is 67
    records of which 3 are Troll general and 7 Troll spectator (it is simply
    "troll"), and 354 is 37 of which 2 are Gnome troop (it is "gnome"). See the
    pass-2 comment in `crawl`; `--check` refuses to let a `collision` rest.
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

PACK_NAME = os.path.join("pack", "category.pack")

# One map per domain. `npc` keeps the original filename because it is the
# authority a dozen documents cite by name; a new domain gets a suffix rather
# than a rename, and rather than a domain column inside one file, because the
# two crawls are reviewed independently and a merge conflict in one has no
# business touching the other.
DOMAINS = {
    "npc": {
        "suffix": ".npc",
        "config": "npc",
        "map": os.path.join("port", "categories.map"),
        "compack": "configs/all.npc.compack",
    },
    "loc": {
        "suffix": ".loc",
        "config": "loc",
        "map": os.path.join("port", "categories_loc.map"),
        "compack": "configs/all.loc.compack",
    },
}
DEFAULT_DOMAIN = "npc"

# The disposition vocabulary. `minted` is the only one that asserts the name is
# in pack/category.pack; every other one asserts it is not.
MINTED = "minted"
DISPOSITIONS = {
    MINTED,       # the members agree on one id and the group is that concept
    "split",      # members carry >= 2 osrs239 ids — H8
    "collision",  # two reference names crawl to one id — one name per id
    "broader",    # unique id, wider concept: minting would bind the wrong set
    "orphan",     # no member resolves to a categorised record — needs an id
    "allocated",  # no cache id exists, so this tree made one above the base
    "placeholder",  # `category_<n>`: a number wearing a name. Never a name here
}

# `collision` is a *derived* disposition and never a resting one — see `check`.
# The demotions a human is allowed to write over the crawl's own answer, and the
# only dispositions `write_map` preserves across a regenerate.
HELD_BACK = ("broader", "orphan", "allocated")
# The derived dispositions a hand row may sit on top of. `orphan` joined the two
# on 2026-08-02 with `allocated`: an allocated name is BY CONSTRUCTION one the
# crawl derives as an orphan, so without this a regenerate would put every
# allocated row back to `orphan` and silently unbind the content standing on it.
OVERRIDABLE = (MINTED, "collision", "orphan")

# The first id `content/content_register.c` lets this tree allocate in the
# `category` namespace. Restated here rather than read, because this tool links
# no C and parses no register — and the restatement is safe in one direction
# only, which is the direction it needs: if the two ever disagree the `allocated`
# rule below gets *weaker*, never wrong. It would accept an id `mock230_pack`
# then rejects, and that fails loudly.
CATEGORY_SERVER_BASE = 8192

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


def reference_members(ref, suffix=".npc"):
    """category name -> [(record name, provenance)] for every LostCity config of
    this domain's suffix.

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
    for name, key, value, rel in walk_configs(root, suffix, include_unpack=True):
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


def crawl(tree, ref, domain=DEFAULT_DOMAIN):
    """Every reference category of this domain, classified.

    Returns (rows, group index). `npc` below is this *domain's* record table —
    the variable keeps its name because every line of the logic is identical
    between the two and renaming it would make the diff unreadable while
    changing nothing.
    """
    spec = DOMAINS[domain]
    npc = tree_records(tree, spec["config"])
    by_category = collections.defaultdict(list)
    for record, fields in npc.items():
        if "category" in fields:
            try:
                cid = int(fields["category"])
            except ValueError:
                continue
            if cid != 0:
                by_category[cid].append(record)

    # Display name -> the categories records *with that display name* carry.
    #
    # This is what makes an `orphan` row say something. An orphan is a name whose
    # own members carry nothing, and the next reader's instinct is to go looking
    # for "the obvious id" by display name — which usually finds one, and it is
    # usually a different concept wearing the same word. `Pirate` lands on three
    # (Trouble Brewing's, the pickpocketable set, the Warrens'); `Witch's
    # experiment` lands on 725, which is the *Nightmare Zone boss* category, 67
    # records of `nzone_*`. Recording the near-miss in the row is how the search
    # stops at the row instead of at the pack file. Unnamed records are skipped:
    # a `(unnamed)` key would match thousands and mean nothing.
    by_display = collections.defaultdict(collections.Counter)
    for record, fields in npc.items():
        display = fields.get("name")
        if not display:
            continue
        cid = fields.get("category")
        if cid and cid != "0":
            try:
                by_display[display][int(cid)] += 1
            except ValueError:
                pass

    groups = reference_members(ref, spec["suffix"])
    referenced = reference_trigger_refs(ref).get(domain, set())

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
            # Over the *set* of display names, not the member list: four members
            # all called `Pirate` would otherwise count every candidate group
            # four times and print ratios above 1.
            near = collections.Counter()
            for display in {npc.get(record, {}).get("name")
                            for record, _provenance in members} - {None}:
                near.update(by_display.get(display, {}))
            row.evidence = ("%d member(s): %d not in %s, %d "
                            "carry no category"
                            % (row.total, unnamed, spec["compack"], uncategorised))
            if near:
                # `<id>(<records sharing a display name>/<the whole group>)` —
                # the same ratio a `broader` call is made from, so the reader can
                # see at a glance that 725 is 4 records out of 67.
                row.evidence += ("; SUSPECT %s share these display names but not "
                                 "these records — a plausible id is not this set: "
                                 "do not mint"
                                 % ",".join(
                                     "%d(%d/%d)" % (i, n, len(by_category.get(i, [])))
                                     for i, n in near.most_common(4)))
            else:
                row.evidence += ("; no categorised record anywhere in this cache "
                                 "shares these display names — there is no id to read")
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
    #
    # **A collision is never resolved by picking a winner**, and that is a result
    # rather than a policy. If two reference names crawl to one osrs239 id, the
    # id holds both concepts, so it is *wider than either* — which is `broader`,
    # measured. 354 is neither `battle_mage` nor `gnome_troop`, it is "gnome"
    # (37 records, 2 of them Gnome troop); 309 is neither `troll_general` nor
    # `troll_spectator`, it is "troll" (67 records, 3 and 7 of them). The one
    # case where picking would be right — two reference names that are synonyms —
    # has no instance here and would be visible as one group answering to both.
    # So `collision` is a question, and `check` refuses to let it rest as an
    # answer: a human demotes each side to `broader` or `orphan` with the group
    # histogram (`--groups`) as the evidence, and `write_map` preserves that.
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


def group_names(tree, by_category, cid, domain=DEFAULT_DOMAIN):
    """The display-name histogram of an osrs239 category, which is the evidence
    the `broader` calls were made from."""
    npc = tree_records(tree, DOMAINS[domain]["config"])
    counts = collections.Counter()
    for record in by_category.get(cid, []):
        counts[npc.get(record, {}).get("name", "(unnamed)")] += 1
    return counts


# ----------------------------------------------------------------------
# The map
# ----------------------------------------------------------------------

HEADER = """\
# %(map)s — %(domain)s categories, crawled (docs/LOSTCITY_PORT_TRIAGE.md §16).
#
# Generated by tools/port_category_crawl.py.
#
# THIS FILE, NOT `--report`, IS THE AUTHORITY. `--report` re-derives every row
# from scratch, so it prints the crawl's *unreviewed* answer and will happily
# say `black_demon minted 275` — a row this file holds back. `--report` prints
# both now (a `derived` column) and says so on stderr; `--check` is what enforces
# the file. Read a disposition from here.
#
# The demotions in `HELD_BACK` (`broader`, `orphan`) are hand edits over rows the
# crawl derives as `minted` or `collision`. They are preserved verbatim — id,
# evidence and all — across `--write-map`, and `--check` re-validates the id they
# state against the members every run, so a preserved row cannot rot.
#
# name  disposition  id  resolved/total  refs  provenance  evidence
#
#   minted       the members agree on one osrs239 id AND that id's group is the
#                concept the name states. The id is READ from this tree's own
#                records, never copied from LostCity.
#   split        members carry >= 2 ids. A human picks (triage H8).
#   collision    two reference names crawl to one id. Derived only, and NEVER a
#                resting state: one id holding two concepts is by construction
#                wider than either, so both sides demote to `broader` (or to
#                `orphan` if the one member that resolves does so for an
#                unrelated reason). `--check` fails on a collision left standing.
#   broader      unique id, wider concept — minting binds the wrong set.
#   orphan       no id in this cache states this concept, so one would have to be
#                *allocated*. `category` HAS an allocation base now (8192,
#                content/content_register.c) — so an orphan is no longer blocked,
#                it is undecided: allocating an id also means authoring the
#                `category=` key on every member, and until something states it
#                `mock230_pack` reports the name as carried by nothing. The
#                evidence column carries the near-miss ids a later reader would
#                otherwise find by display name and mint — marked SUSPECT because
#                they are a different concept sharing a word.
#   allocated    no cache record states this concept, so the id came from this
#                tree's own allocation base (8192) rather than being read. A hand
#                disposition over a derived `orphan`, and the ONE held-back
#                disposition that REQUIRES the name to be in pack/category.pack —
#                because for an allocated id that file is where the id comes from.
#                It also requires an authored config block to *state* the
#                category, which is `mock230_pack`'s half of the check, not this
#                tool's.
#   placeholder  `category_<n>` — a LostCity id wearing a name.
"""


def write_map(path, rows, domain=DEFAULT_DOMAIN):
    existing = read_map(path)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(HEADER % {"map": DOMAINS[domain]["map"], "domain": domain})
        for name in sorted(rows):
            row = rows[name]
            # A hand demotion is a judgement about what an osrs239 group holds;
            # the crawl cannot re-derive it and must not undo it.
            #
            # This used to preserve `broader` alone, and only over a derived
            # `minted`. That was the whole set at the time and it is not now: the
            # 354 and 309 collisions resolve to `broader`/`orphan`, and a
            # regenerate would have silently put both back to `collision` —
            # re-arming exactly the "one name wins and nothing says so" failure
            # the demotion was written to disarm. Every hand disposition in
            # HELD_BACK survives, over every derived disposition in OVERRIDABLE.
            if name in existing and existing[name][0] in HELD_BACK and \
                    row.disposition in OVERRIDABLE:
                row.disposition = existing[name][0]
                row.category_id = existing[name][1]
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

def report(tree, ref, domain=DEFAULT_DOMAIN):
    """The review artifact — and it prints the *map's* disposition, not its own.

    This used to print the raw crawl. That is a trap with a witness: a reader
    running `--report` to see what was blocking a port was told
    `black_demon minted 275` and `giantrat minted 262`, both of which
    port/categories.map holds back as `broader` — the two rows §16.4 exists to
    hold back. `--write-map` preserved the demotions and `--report` did not, so
    the two commands disagreed and the louder one was wrong.

    Now: the disposition column is the map's, an eighth `derived` column is the
    crawl's own answer, and stderr counts both and names every row where they
    differ. A row with no map entry shows `derived` as its disposition too and
    is reported — that is `--check`'s "a new reference category is a decision".
    """
    map_name = DOMAINS[domain]["map"]
    rows, by_category = crawl(tree, ref, domain)
    stated = read_map(os.path.join(tree, map_name))
    print("name\tdisposition\tid\tresolved/total\trefs\tprovenance\tevidence\tderived")
    overrides = []
    for name in sorted(rows):
        row = rows[name]
        disposition, cid, evidence = row.disposition, row.category_id, row.evidence
        if name in stated:
            disposition, cid = stated[name][0], stated[name][1]
            evidence = stated[name][4]
            if disposition != row.disposition:
                overrides.append((name, disposition, row.disposition, cid))
        print("%s\t%s\t%d\t%d/%d\t%s\t%s\t%s\t%s" % (
            row.name, disposition, cid, row.resolved, row.total,
            "yes" if row.referenced else "no", row.provenance, evidence,
            row.disposition))
    effective = {n: (stated[n][0] if n in stated else rows[n].disposition)
                 for n in rows}
    counts = collections.Counter(effective.values())
    referenced = collections.Counter(
        effective[n] for n in rows if rows[n].referenced)
    print("\n# %d reference %s categories: %s" % (
        len(rows), domain, ", ".join("%s %d" % kv for kv in sorted(counts.items()))),
        file=sys.stderr)
    print("# of the %d bound by a trigger: %s" % (
        sum(referenced.values()),
        ", ".join("%s %d" % kv for kv in sorted(referenced.items()))),
        file=sys.stderr)
    if not stated:
        print("# no %s — every disposition above is the crawl's own, unreviewed"
              % map_name, file=sys.stderr)
    for name, held, derived, cid in overrides:
        print("# %s: the map says %s, the crawl alone would say %s (%d) — the "
              "map is the authority" % (name, held, derived, cid), file=sys.stderr)
    return 0


def groups(tree, ref, domain=DEFAULT_DOMAIN):
    rows, by_category = crawl(tree, ref, domain)
    npc = tree_records(tree, DOMAINS[domain]["config"])
    # The map's disposition, for the same reason `report` uses it: this listing
    # is the evidence a `broader` call is *made from*, so printing `minted`
    # beside the histogram that disproves it is the worst possible caption.
    stated = read_map(os.path.join(tree, DOMAINS[domain]["map"]))
    for name in sorted(rows):
        row = rows[name]
        disposition = stated[name][0] if name in stated else row.disposition
        for cid in sorted(row.ids):
            counts = collections.Counter()
            for record in by_category.get(cid, []):
                counts[npc.get(record, {}).get("name", "(unnamed)")] += 1
            print("%-26s %-12s %5d  %3d record(s)  %s" % (
                name, disposition, cid, len(by_category.get(cid, [])),
                ", ".join("%s x%d" % (k, v) for k, v in counts.most_common(6))))
    return 0


def check(tree, ref, verbose, domain=DEFAULT_DOMAIN):
    map_name = DOMAINS[domain]["map"]
    map_path = os.path.join(tree, map_name)
    stated = read_map(map_path)
    if not stated:
        print("port_category_crawl: no %s — the map IS the gate, so its absence "
              "is the failure" % map_name, file=sys.stderr)
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
        elif disposition == "collision":
            # `collision` is derived, never a resting state. Leaving one standing
            # is the failure the brief names: two names claim one id, the pack
            # can hold one, and whichever the port gets to first wins with
            # nothing anywhere saying the other lost. Demote both sides with the
            # `--groups` histogram as evidence — see the pass-2 comment in
            # `crawl` for why the answer is `broader`/`orphan` and not a winner.
            problems.append(
                "`%s` is still `collision` on %d — a collision is a question, not "
                "an answer: the id holds two concepts so it is wider than either. "
                "Demote both sides to `broader` (or `orphan`) with the group "
                "histogram as evidence; `--groups %s` prints it"
                % (name, cid, name))
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
        elif disposition == "allocated":
            # The mirror image of `minted`, and why it is its own disposition
            # rather than a `minted` with a big number: for a minted name the pack
            # file *records* an id the cache states, so the map is the authority
            # and the pack must agree with it. For an allocated one the pack file
            # *is* where the id came from, so the same equality is checked from
            # the other side and the extra rule is the base.
            if name not in by_name:
                problems.append(
                    "`%s` is `allocated` at %d and is not in %s — an allocated id "
                    "no pack file states is a number in a comment"
                    % (name, cid, PACK_NAME))
            elif by_name[name] != cid:
                problems.append(
                    "`%s` is `allocated` at %d but %s says %d"
                    % (name, cid, PACK_NAME, by_name[name]))
            elif cid < CATEGORY_SERVER_BASE:
                problems.append(
                    "`%s` is `allocated` at %d, below the base %d — an id under the "
                    "base belongs to the cache, so it is READ (`minted`) or refused, "
                    "never allocated on top of"
                    % (name, cid, CATEGORY_SERVER_BASE))
        elif name in by_name:
            problems.append(
                "`%s` is %s (%s) but %s names it at %d — a category held back "
                "for a human was minted anyway"
                % (name, disposition, _evidence, PACK_NAME, by_name[name]))

    # --- the map against the reference --------------------------------------
    overrides = 0
    if ref and os.path.isdir(os.path.join(ref, "content", "scripts")):
        rows, _by_category = crawl(tree, ref, domain)
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
            # A hand demotion states an id it is *refusing*, and `write_map`
            # preserves that id verbatim rather than re-deriving it. So it is the
            # one number in this file nothing else would notice going stale:
            # `black_demon broader 275` still has to be the id its members carry,
            # or the paragraph justifying the refusal is about a different group.
            # `allocated` is excluded: its id comes from the pack file, not from
            # the members, so "the id it refuses" is not a thing it states. The
            # rule that keeps it honest is the base test above, plus
            # `mock230_pack` demanding an authored block that states it.
            if disposition in HELD_BACK and disposition != row.disposition and \
                    row.disposition in OVERRIDABLE:
                overrides += 1
                if disposition != "allocated" and row.category_id != cid:
                    problems.append(
                        "`%s` is held back as `%s` at %d but its members now carry "
                        "%s — a preserved demotion states the id it refuses, and "
                        "this one no longer refuses the id it names"
                        % (name, disposition, cid, sorted(row.ids) or "nothing"))
        for name in sorted(set(stated) - set(rows)):
            problems.append("`%s` has a row but the reference no longer declares "
                            "it on any %s" % (name, DOMAINS[domain]["suffix"]))
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
        print("port_category_crawl: %s: %d row(s), %d minted, %d demoted by hand "
              "over the crawl's own answer, all agree with %s"
              % (domain, len(stated),
                 sum(1 for v in stated.values() if v[0] == MINTED),
                 overrides, PACK_NAME))
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
    parser.add_argument("--domain", choices=sorted(DOMAINS), default=DEFAULT_DOMAIN)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    if args.report:
        return report(args.tree, args.ref, args.domain)
    if args.groups:
        return groups(args.tree, args.ref, args.domain)
    if args.write_map:
        map_name = DOMAINS[args.domain]["map"]
        rows, _ = crawl(args.tree, args.ref, args.domain)
        write_map(os.path.join(args.tree, map_name), rows, args.domain)
        print("wrote %s (%d rows)" % (os.path.join(args.tree, map_name), len(rows)))
        return 0
    if args.check:
        # Every domain, always. The three record types share one
        # `pack/category.pack`, so checking one map against it and not the other
        # would pass a tree where the two disagree — and `--check` is the build
        # bar, which has no `--domain` to forget to pass. `--report`, `--groups`
        # and `--write-map` are one domain at a time because each is something a
        # human is reading or reviewing.
        worst = 0
        for domain in sorted(DOMAINS):
            worst |= check(args.tree, args.ref, args.verbose, domain)
        return worst
    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
