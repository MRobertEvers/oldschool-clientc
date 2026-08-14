#!/usr/bin/env python3
"""Generate the `::tele` destination table from data the tree already holds.

`::tele <name>` needs a name -> coord table, and the interesting property of
that table is that **nothing here is invented**. Three sources already state
where things are, and each one is the authority for a different kind of place:

  1. `worldmap/areas/labels_10.wml` -- the 548 labels the world map draws on the
     surface. This is the game's own answer to "what is this place called", in
     world coordinates, and it covers every town, guild, mine and landmark a
     player would name.

  2. `worldmap/areas/details.wma` -- the 52 world *maps*, each with an
     `external=` display name and an `origin=` packed coord. The surface labels
     file is the only populated one (every `labels_<N>.wml` for a dungeon map is
     an empty header), so without this the whole underground -- Zanaris,
     Keldagrim, the God Wars Dungeon, the TzHaar city -- would have no name at
     all.

  3. `server/scripts/areas/world/configs/*.spawn` + `configs/all.npc` -- the npc
     roster. A boss or a quest npc is the destination a debug session actually
     wants ("put me at Zulrah"), and the roster is the only thing that knows
     where one stands.

  4. `tools/tele_extra_destinations.tsv` -- hand-authored, for the handful of
     rooms that none of the above name: raid chambers, the Inferno, instanced
     minigames. Data, not code, so adding one is a one-line edit and does not
     mean touching this file.

**Ambiguity is dropped, never guessed.** A name that resolves to two places
which are not the same place is not a teleport destination -- "Bank" is 60
different tiles and "Guard" is 400. The rule is uniform across all four sources:
group by slug, and keep the group only if every coord in it sits inside one
small box on one plane. Everything else is discarded with a count in the report.
That is the whole reason this is a generator and not a checked-in list: the
ambiguity test has to be re-run whenever the roster or the cache moves.

    tools/gen_tele_table.py [--tree OSRS-Content/osrs239-content] [--check]

`--check` regenerates into memory and fails if the tree's files differ, which is
what a build hook wants.

Two files come out, and they are two shapes of one table because rs2 has no way
to do both jobs with one:

  * `.../misc/tele_destinations.rs2` -- an if-ladder, bucketed by first letter,
    returning a `coord` literal. Lookup. The coords stay readable and the
    compiler validates every one of them.
  * `.../general/configs/tele_names.enum` -- `int -> string`, the same names in
    index order. Search (`::telefind`), which needs to *iterate* the table, and
    an if-ladder cannot be iterated.

Both come from one pass over one merged dict, so the two cannot drift.
"""

import argparse
import collections
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TREE = os.path.join(REPO, "OSRS-Content", "osrs239-content")

# Bounding box, in tiles, inside which several spawns of one name are still
# "the same place". 20 covers a shop's two shopkeepers and a boss's arena; it
# does not cover two towns.
SAME_PLACE_BOX = 20

# The rs2 word splitter in mock230_scripts.c caps an argument at 63 characters.
MAX_SLUG = 60

BUCKETS = "abcdefghijklmnopqrstuvwxyz"

GENERATED_BY = "tools/gen_tele_table.py"


# --------------------------------------------------------------------------
# Names
# --------------------------------------------------------------------------


def slugify(text):
    """A display name as one typeable word.

    `::tele` arguments are split on spaces by the debugproc dispatcher, so a
    destination name has to survive as a single word -- "Duke Horacio" can only
    be reached as `duke_horacio`. Colour tags are stripped (the roster has a
    literal `<col=00ffff>Flower</col>`), a `/` is a world-map line break rather
    than punctuation, and everything else non-alphanumeric collapses to `_`.

    Returns None for a name that cannot be a destination: empty, or one that
    starts with a digit. The digit case matters -- `::tele` decides between a
    coord literal and a name by looking at the first character, so a
    destination called `50th_something` would be unreachable.
    """
    text = re.sub(r"<[^>]*>", " ", text)
    text = text.replace("/", " ")
    text = text.lower()
    text = re.sub(r"[^a-z0-9]+", "_", text).strip("_")
    if not text or not text[0].isalpha():
        return None
    if len(text) > MAX_SLUG:
        return None
    return text


# --------------------------------------------------------------------------
# Sources
# --------------------------------------------------------------------------


def read_labels(tree, report):
    """Surface world-map labels: `label<N>=name,x,z,size`, world coords, plane 0."""
    out = []
    path = os.path.join(tree, "worldmap", "areas", "labels_10.wml")
    if not os.path.exists(path):
        report.append("labels_10.wml missing -- no surface place names")
        return out
    for line in open(path, errors="replace"):
        m = re.match(r"label\d+=(.*),(-?\d+),(-?\d+),(\d+)\s*$", line.strip())
        if not m:
            continue
        out.append((m.group(1), 0, int(m.group(2)), int(m.group(3))))
    report.append("labels_10.wml: %d surface labels" % len(out))
    return out


def read_worldmap_areas(tree, report):
    """World maps: `external=` display name, `origin=` packed coord.

    `origin` is packed the way every other coord is -- plane<<28 | x<<14 | z --
    and it is in *source* (real world) space, not map-surface space: `main`
    decodes to 3232,3232 (Lumbridge) and `ancient_cavern` to 1760,5344, both the
    centre of that map's own section. That makes it exactly the "take me to this
    map" coord, which is the one thing labels_10 cannot give for anywhere
    underground.
    """
    out = []
    path = os.path.join(tree, "worldmap", "areas", "details.wma")
    if not os.path.exists(path):
        report.append("details.wma missing -- no underground map names")
        return out
    external = None
    origin = None
    for raw in open(path, errors="replace"):
        line = raw.strip()
        if line.startswith("["):
            external = origin = None
            continue
        if line.startswith("external="):
            external = line[len("external=") :]
        elif line.startswith("origin="):
            origin = int(line[len("origin=") :])
        if external is not None and origin is not None:
            out.append(
                (external, (origin >> 28) & 0x3, (origin >> 14) & 0x3FFF, origin & 0x3FFF)
            )
            external = origin = None
    report.append("details.wma: %d world maps" % len(out))
    return out


def read_npc_names(tree):
    """Symbol -> display name, from the cache's npc configs."""
    names = {}
    block = None
    path = os.path.join(tree, "configs", "all.npc")
    for raw in open(path, errors="replace"):
        line = raw.strip()
        m = re.match(r"^\[(.+)\]$", line)
        if m:
            block = m.group(1)
            continue
        if block and line.startswith("name="):
            names[block] = line[len("name=") :]
    return names


def read_npc_spawns(tree, report):
    """Roster spawns, grouped onto the npc's display name.

    A symbol with no `name=` is skipped rather than falling back to the symbol:
    those are the roster's scenery npcs and multinpc bases, and their symbols
    (`godwars_armadyl_male_armor02_red`) are not names anyone would type.
    """
    names = read_npc_names(tree)
    by_display = collections.defaultdict(list)
    symbols = 0
    unnamed = 0
    root = os.path.join(tree, "server", "scripts", "areas", "world", "configs")
    for entry in sorted(os.listdir(root)):
        if not entry.endswith(".spawn"):
            continue
        section = None
        for raw in open(os.path.join(root, entry), errors="replace"):
            line = raw.strip()
            if line.startswith("===="):
                section = line.strip("= ").strip()
                continue
            if not line or line.startswith("//") or section != "NPC":
                continue
            parts = line.split()
            if len(parts) < 4:
                continue
            symbol, x, z, level = parts[0], int(parts[1]), int(parts[2]), int(parts[3])
            symbols += 1
            display = names.get(symbol)
            if not display or display == "null":
                unnamed += 1
                continue
            by_display[display].append((level, x, z))
    report.append(
        "npc roster: %d spawns, %d with no display name, %d distinct names"
        % (symbols, unnamed, len(by_display))
    )
    return by_display


def read_constants(tree):
    """Every `^name = <coord literal>` the server's content declares.

    The extras table names these rather than copying their values, so a room
    that moves moves here too. It is also the provenance: `^cerberus_north_spawn`
    is a coord the Cerberus scripts already stand a player on, which is a much
    better warrant than a number recalled from the live game.
    """
    values = {}
    for root, _, files in os.walk(os.path.join(tree, "server", "scripts")):
        for entry in files:
            if not entry.endswith(".constant"):
                continue
            for raw in open(os.path.join(root, entry), errors="replace"):
                m = re.match(
                    r"^\s*\^([a-z0-9_]+)\s*=\s*(\d+_\d+_\d+_\d+_\d+)\s*$", raw
                )
                if m:
                    values.setdefault(m.group(1), m.group(2))
    return values


def read_extras(tree, report):
    """Hand-authored destinations: `name<TAB>coord-or-^constant<TAB>note`."""
    out = []
    path = os.path.join(REPO, "tools", "tele_extra_destinations.tsv")
    if not os.path.exists(path):
        report.append("tele_extra_destinations.tsv missing -- no curated extras")
        return out
    constants = read_constants(tree)
    for number, raw in enumerate(open(path, errors="replace"), 1):
        line = raw.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 2:
            sys.exit("%s:%d: expected `name<TAB>coord`" % (path, number))
        where = parts[1].strip()
        if where.startswith("^"):
            resolved = constants.get(where[1:])
            if resolved is None:
                sys.exit(
                    "%s:%d: `%s` is not a coord constant in the content tree"
                    % (path, number, where)
                )
            where = resolved
        m = re.match(r"^(\d+)_(\d+)_(\d+)_(\d+)_(\d+)$", where)
        if not m:
            sys.exit("%s:%d: `%s` is not a coord literal" % (path, number, parts[1]))
        level, mx, mz, lx, lz = (int(g) for g in m.groups())
        out.append((parts[0].strip(), level, mx * 64 + lx, mz * 64 + lz))
    report.append("tele_extra_destinations.tsv: %d curated destinations" % len(out))
    return out


# --------------------------------------------------------------------------
# Merge
# --------------------------------------------------------------------------


def one_place(coords):
    """Whether every coord names the same place.

    One plane, and a bounding box small enough that the difference between the
    members does not matter to someone teleporting there.
    """
    if len({c[0] for c in coords}) != 1:
        return False
    xs = [c[1] for c in coords]
    zs = [c[2] for c in coords]
    return max(xs) - min(xs) <= SAME_PLACE_BOX and max(zs) - min(zs) <= SAME_PLACE_BOX


def pick(coords):
    """The member closest to the group's centre -- a real tile, not an average.

    Averaging two spawns either side of a wall lands in the wall.
    """
    cx = sum(c[1] for c in coords) / len(coords)
    cz = sum(c[2] for c in coords) / len(coords)
    return min(coords, key=lambda c: (c[1] - cx) ** 2 + (c[2] - cz) ** 2)


def merge(sources, report):
    """Slug -> (level, x, z), dropping every slug that names more than one place.

    `sources` is ordered by precedence and the order is a decision: a place beats
    an npc. Someone typing `::tele lumbridge` wants the town, not whichever npc
    the roster happens to also call that -- so a later source never displaces an
    earlier one, it is just dropped as a duplicate.
    """
    accepted = {}
    origin = {}
    ambiguous = collections.Counter()
    shadowed = collections.Counter()
    unusable = collections.Counter()

    for source_name, rows in sources:
        grouped = collections.defaultdict(list)
        for name, level, x, z in rows:
            slug = slugify(name)
            if slug is None:
                unusable[source_name] += 1
                continue
            grouped[slug].append((level, x, z))
        for slug, coords in sorted(grouped.items()):
            if slug in accepted:
                # Same tile from two sources is agreement, not a conflict.
                if all(c == accepted[slug] for c in coords):
                    continue
                shadowed[source_name] += 1
                continue
            if not one_place(coords):
                ambiguous[source_name] += 1
                continue
            accepted[slug] = pick(coords)
            origin[slug] = source_name

    for source_name, _ in sources:
        report.append(
            "  %-14s kept %4d, dropped %4d ambiguous, %4d already named, %3d unusable"
            % (
                source_name,
                sum(1 for s in origin.values() if s == source_name),
                ambiguous[source_name],
                shadowed[source_name],
                unusable[source_name],
            )
        )
    report.append("total destinations: %d" % len(accepted))
    return accepted, origin


# --------------------------------------------------------------------------
# Emit
# --------------------------------------------------------------------------


def coord_literal(level, x, z):
    return "%d_%d_%d_%d_%d" % (level, x // 64, z // 64, x % 64, z % 64)


def emit_rs2(table, origin):
    """The lookup ladder, bucketed by first letter.

    Bucketed for two reasons. A lookup walks one bucket instead of the whole
    table, and -- the one that actually forced it -- a single proc holding every
    destination is one enormous basic block, where the per-letter split keeps
    each script an ordinary size.
    """
    lines = []
    add = lines.append
    add("// `::tele <name>` destinations -- name -> coord.")
    add("//")
    add("// Generated by %s; do not hand-edit, the next run" % GENERATED_BY)
    add("// overwrites it. Sources: the world map's own labels and map origins,")
    add("// the npc spawn roster, and tools/tele_extra_destinations.tsv.")
    add("//")
    add("// A name that resolved to more than one place was dropped rather than")
    add("// guessed at, so `bank` and `guard` are deliberately absent.")
    add("//")
    add("// The commands that read this live in cheat_tele.rs2.")
    add("")

    add("[proc,tele_lookup_generated](string $name)(coord, boolean)")
    add("def_string $head = substring($name, 0, 1);")
    add("def_coord $dest = null;")
    add("def_boolean $found = false;")
    used = []
    for letter in BUCKETS:
        if any(slug[0] == letter for slug in table):
            used.append(letter)
            add(
                'if (compare($head, "%s") = 0) {\n'
                "    $dest, $found = ~tele_dest_%s($name);\n"
                "    return($dest, $found);\n"
                "}" % (letter, letter)
            )
    add("return(null, false);")
    add("")

    for letter in used:
        rows = sorted(slug for slug in table if slug[0] == letter)
        add("[proc,tele_dest_%s](string $name)(coord, boolean)" % letter)
        add("// %d destinations." % len(rows))
        for slug in rows:
            level, x, z = table[slug]
            add(
                'if (compare($name, "%s") = 0) { return(%s, true); } // %s'
                % (slug, coord_literal(level, x, z), origin[slug])
            )
        add("return(null, false);")
        add("")
    return "\n".join(lines) + "\n"


def emit_enum(table):
    """The same names, in index order, for `::telefind` to walk.

    A string-keyed enum would have made the ladder above redundant, but the
    `enum` opcode pops its key off the *int* stack -- there is no such thing.
    So this half carries names only, and the coord stays in the ladder where the
    compiler can check it.
    """
    lines = []
    add = lines.append
    add("// Every `::tele` destination name, in index order.")
    add("//")
    add("// Generated by %s; do not hand-edit, the next run" % GENERATED_BY)
    add("// overwrites it. The names are exactly those of the lookup ladder in")
    add("// server/scripts/general/scripts/misc/tele_destinations.rs2 -- one pass")
    add("// over one table emits both, so the two cannot drift.")
    add("//")
    add("// This exists because an if-ladder cannot be iterated and `::telefind`")
    add("// has to be: it answers `varrock` with every name containing it. Search")
    add("// is the only reader; a lookup by name goes through the ladder.")
    add("")
    add("[tele_name_enum]")
    add("inputtype=int")
    add("outputtype=string")
    for index, slug in enumerate(sorted(table)):
        add("val=%d,%s" % (index, slug))
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------


def write_or_check(path, text, check, changed):
    existing = None
    if os.path.exists(path):
        existing = open(path, errors="replace").read()
    if existing == text:
        return
    changed.append(path)
    if check:
        return
    with open(path, "w") as handle:
        handle.write(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", default=DEFAULT_TREE)
    parser.add_argument(
        "--check",
        action="store_true",
        help="do not write; exit non-zero if the tree is out of date",
    )
    args = parser.parse_args()

    report = []
    sources = [
        ("extra", read_extras(args.tree, report)),
        ("place", read_labels(args.tree, report)),
        ("worldmap", read_worldmap_areas(args.tree, report)),
        (
            "npc",
            [
                (display, level, x, z)
                for display, coords in read_npc_spawns(args.tree, report).items()
                for (level, x, z) in coords
            ],
        ),
    ]
    table, origin = merge(sources, report)
    if not table:
        sys.exit("no destinations resolved -- refusing to write an empty table")

    changed = []
    write_or_check(
        os.path.join(
            args.tree, "server", "scripts", "general", "scripts", "misc",
            "tele_destinations.rs2",
        ),
        emit_rs2(table, origin),
        args.check,
        changed,
    )
    write_or_check(
        os.path.join(args.tree, "server", "scripts", "general", "configs", "tele_names.enum"),
        emit_enum(table),
        args.check,
        changed,
    )

    for line in report:
        print(line)
    if args.check and changed:
        print("\nout of date, re-run %s:" % GENERATED_BY)
        for path in changed:
            print("  %s" % os.path.relpath(path, REPO))
        return 1
    for path in changed:
        print("wrote %s" % os.path.relpath(path, REPO))
    return 0


if __name__ == "__main__":
    sys.exit(main())
