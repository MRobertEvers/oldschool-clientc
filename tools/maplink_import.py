#!/usr/bin/env python3
"""Author real destinations for every stair, ladder, trapdoor, cave mouth,
portal and lever this cache places, harvested from the wiki and verified
against cache.osrs239 before anything is trusted. See docs/MAPLINKS.md for
the full pipeline; this docstring covers the mechanics.

Two classes, two bindings
-------------------------
**Climb** (`Climb-up`/`Climb-down`/`Climb`) rides the existing `_climb_up`/
`_climb_down`/etc. categories `tools/ladder_import.py` already put on all
1,445 climb-verbed records: `~climb` in `ladders_stairs/scripts/ladders.rs2`
gains three lines at its top calling `~maplink_try`, which does a coord+loc
lookup in a new `maplink` dbtable and `p_teleport`s on a hit, falling through
to the old ±1-plane default on a miss. None of `~climb`'s ~20 other call
sites (quest scripts with their own destination) are touched, because
`~climb`'s signature does not change.

**Transitions** (cave mouths — `Enter`/`Exit`/`Board` — plus the standalone
`teleportation_portals.tsv`/`teleportation_levers.tsv` files) carry no climb
verb and so no existing category to ride. `configs/maplinks.loc` (generated)
assigns them a brand new category, `maplink_transition` — allocated by hand
in `pack/category.pack`, since that file is not part of `sscompile`'s
automatic id allocation — subject to the same two conflict rules
`ladder_import.py` applies to `ladders.loc` (don't recategorise a record
another `.loc` already claims; don't overwrite a named cache category).
`scripts/maplink.rs2` binds three static `[oploc<n>,_maplink_transition]`
lines to a small wrapper that calls the same `~maplink_try` and falls back to
a message on a miss (there is no sensible ±1-plane-style default for
"Enter a cave mouth").

Both classes share one `maplink` dbtable and one `~maplink_try` lookup.

Verification
------------
Every row is an unverified *claim* about a different game revision. This
importer accepts a row only when the cache agrees on all three legs of
record identity: the object id exists in `all.loc.compack` (or, for a
name-only fallback, `all.npc.compack`), its `name=` matches the row's menu
target, one of its `opN=` matches the row's menu option, AND it is actually
placed near the row's stated origin in `maps/*.jl2`. A row failing any leg is
dropped and printed by `--report`/written to `docs/MAPLINKS_REJECTS.md`,
never guessed — the same rule `ladder_import.py` already applies to a
`loc_<id>` it cannot resolve by name.

The key is a tile AND a direction
---------------------------------
A row carries `dir`, the plane change its own menu verb names (+1 Climb-up,
-1 Climb-down, ±2 the Top-floor/Bottom-floor op that skips a landing, 0 for a
verb that names none), and `~maplink_try` is told which one the player asked
for. That is not bookkeeping: a spiral staircase's middle floor offers up and
down from ONE tile, so a tile-only key answers both ops with whichever
direction survived the harvest — and exactly one does, because
`classify_displacement` drops the direction whose destination already equals
`~climb`'s ±1 default. Lumbridge castle's two landings each kept one, and
each answered both of its ops with it.

Two kinds of ambiguity, both name-scoped
-----------------------------------------
A `src -> dest` row is only emitted when every verified placement of that
EXACT origin tile AND direction agrees on one destination (the table cannot
hold two answers for one key). Where a key disagrees,
the affected records are bound by NAME and op slot instead — but only when
that name has EXACTLY ONE placement in the whole accepted set. A name+op
binding is name-wide, not tile-specific: it fires for every instance of the
name, so binding it from one ambiguous tile's answer would silently hijack
every OTHER, unambiguous placement of the same name too. (Found by checking
the actual generated output, not by inspection: `my2arm_cliff_shortcut_1`/
`_2`, the two ends of one bidirectional cliff jump, would have been bound
wrong without this check.)

Usage
-----
    python3 tools/maplink_import.py [--content <dir>] [--data <dir>]
                                     [--check] [--report] [--near-miss]

`--near-miss` additionally emits climb rows for placements within 3 tiles and
1 plane of the ±1-plane default — the "same-ish tile but wrong" case, wired
separately from genuine jumps so the two can be measured independently. Does
not affect transitions, which have no default to compare against.

`--check` writes nothing and exits non-zero if a generated file differs from
what is on disk, the same contract `ladder_import.py --check` has.
"""

import argparse
import collections
import os
import re
import sys

# Every menu verb that changes the player's plane, and BY HOW MANY planes.
#
# The magnitude matters as much as the sign, and it is the whole reason this
# is a mapping rather than a tuple: a three-storey spiral staircase states
# `op1=Climb-up`, `op2=Top-floor` on its ground floor, so one origin tile
# carries two "up" rows that land on two different planes. `dir` (below) is
# what tells them apart, in the table and at the trigger.
#
# 0 is "the verb names no direction" — a bare `Climb`, which the record only
# ever carries alone. A 0 row matches any direction at runtime; see
# `~maplink_try`.
CLIMB_DELTAS = {
    "Climb-up": 1,
    "Climb-down": -1,
    "Climb": 0,
    "Top-floor": 2,
    "Bottom-floor": -2,
}
CLIMB_VERBS = tuple(CLIMB_DELTAS)
TRANSITION_VERBS = ("Enter", "Exit", "Board")
TRANSITION_CATEGORY = "maplink_transition"  # bare name — the value `category=` and category.pack use
TRANSITION_CATEGORY_TRIGGER = "_" + TRANSITION_CATEGORY  # trigger subject spelling: `[oploc1,_maplink_transition]`
AGILITY_CATEGORY = "maplink_agility"
AGILITY_CATEGORY_TRIGGER = "_" + AGILITY_CATEGORY

HEADER = """\
// maplink rows for climb-verbed locs whose real destination is not the
// ±1-plane, same-tile default `~climb` falls back to.
//
// Generated by tools/maplink_import.py — do not edit, re-run the importer.
// `python3 tools/maplink_import.py --check` fails when this file and the
// source data disagree. See docs/MAPLINKS.md for the full pipeline.
//
// {count} rows, harvested from tools/data/shortest_path/transports/*.tsv and
// verified against cache.osrs239: object id in all.loc.compack, display name
// and menu op matching, and the record actually placed at the stated origin
// in maps/*.jl2. {near_miss_note}
"""

SHARED_HEADER = """\
// Climb destinations that a coord-only lookup cannot disambiguate: two
// verified rows share one origin tile but name two different destinations —
// almost always a spiral staircase's middle floor, where op2 (up) and op3
// (down) from the SAME tile go to different places.
//
// Generated by tools/maplink_import.py — do not edit, re-run the importer.
//
// Bound by NAME and op slot, which is what makes it work: the trigger
// table's name rung beats its category rung (the same mechanism
// `ladders_stairs/scripts/climb_shared.rs2` uses), so `[oploc{{n}},<name>]`
// below wins over `[oploc{{n}},_climb_*]` in ladders.rs2 for that op — and
// because the destination is a literal coord baked into the block rather
// than a table lookup, op1 and op2 on the same name can disagree safely.
//
// {count} names, {rows} bindings.
"""


def read_compack(path):
    """id -> name, from an `all.<kind>.compack` file (`<id>=<name>` per line)."""
    out = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or "=" not in line:
                continue
            key, _, value = line.partition("=")
            try:
                out[int(key)] = value
            except ValueError:
                continue
    return out


def read_configs(path):
    """[name] -> {key: value} for an `all.<kind>`-shaped config file."""
    records = {}
    current = None
    with open(path, encoding="utf-8", errors="ignore") as handle:
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


def ops_of(record):
    ops = {}
    for slot in range(1, 6):
        verb = record.get("op%d" % slot)
        if verb:
            ops[slot] = verb
    return ops


def read_category_pack(path):
    """category id -> name, for pack/category.pack (id=name per line)."""
    out = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or "=" not in line or line.startswith("//"):
                continue
            key, _, value = line.partition("=")
            try:
                out[int(key)] = value
            except ValueError:
                continue
    return out


def read_other_loc_categories(content, exclude_paths):
    """loc name -> (relative path, category=... line) for every `category=`
    ANY authored .loc overlay states, other than the files this importer
    itself generates. Mirrors ladder_import.py's conflict rule: a record
    carries one category and cachepack refuses a second, so a record another
    overlay already categorises must never be categorised here too."""
    claimed = {}
    exclude = {os.path.abspath(p) for p in exclude_paths}
    root = os.path.join(content, "server", "scripts")
    for base, _dirs, files in os.walk(root):
        for name in files:
            if not name.endswith(".loc"):
                continue
            path = os.path.join(base, name)
            if os.path.abspath(path) in exclude:
                continue
            block = None
            with open(path, encoding="utf-8", errors="ignore") as handle:
                for line in handle:
                    line = line.strip()
                    match = re.match(r"^\[([^\]]+)\]$", line)
                    if match:
                        block = match.group(1)
                    elif block and line.startswith("category="):
                        claimed[block] = (os.path.relpath(path, content), line)
    return claimed


def scan_existing_oploc_bindings(content, exclude_paths):
    """(slot, name) -> relative path, for every `[oploc<n>,<name>]` trigger any
    hand-authored .rs2 already declares, other than the files this importer
    itself generates. A name-bound trigger for a record this importer would
    also bind must lose to the existing one — it is real, purpose-built
    content (a quest's own dungeon entrance, a minigame's own trapdoor), not a
    default this importer should silently shadow or collide with."""
    bindings = {}
    exclude = {os.path.abspath(p) for p in exclude_paths}
    root = os.path.join(content, "server", "scripts")
    # `_name` is a category trigger, not a real loc name (the leading
    # underscore is what tells sscompile to look it up as a category) — those
    # aren't "someone else's content" for a specific record, so they're not
    # collisions and must not be counted as one.
    pattern = re.compile(r"^\[oploc(\d),([A-Za-z0-9]\w*)\]")
    for base, _dirs, files in os.walk(root):
        for name in files:
            if not name.endswith(".rs2"):
                continue
            path = os.path.join(base, name)
            if os.path.abspath(path) in exclude:
                continue
            with open(path, encoding="utf-8", errors="ignore") as handle:
                for line in handle:
                    match = pattern.match(line.strip())
                    if match:
                        bindings[(int(match.group(1)), match.group(2))] = os.path.relpath(path, content)
    return bindings


def read_placements(maps_dir):
    """locId -> set of (worldX, worldZ, plane) this cache actually places it at."""
    placed = collections.defaultdict(set)
    for name in os.listdir(maps_dir):
        if not name.endswith(".jl2"):
            continue
        stem = name[:-4]
        if not re.match(r"^m-?\d+_-?\d+$", stem):
            continue
        map_x, map_z = (int(v) for v in stem[1:].split("_"))
        with open(os.path.join(maps_dir, name), encoding="utf-8", errors="ignore") as handle:
            for line in handle:
                line = line.strip()
                if not line or line.startswith("="):
                    continue
                pos, _, rest = line.partition(":")
                try:
                    plane, local_x, local_z = (int(v) for v in pos.split())
                    loc_id = int(rest.split()[0])
                except (ValueError, IndexError):
                    continue
                placed[loc_id].add((map_x * 64 + local_x, map_z * 64 + local_z, plane))
    return placed


def to_coord_literal(x, z, plane):
    return "%d_%d_%d_%d_%d" % (plane, x >> 6, z >> 6, x & 63, z & 63)


def read_tsv(path):
    rows = []
    with open(path, encoding="utf-8") as handle:
        header = None
        for line in handle:
            line = line.rstrip("\n")
            if header is None:
                if not line.startswith("#"):
                    continue
                header = line.lstrip("# ").split("\t")
                continue
            if not line.strip() or line.startswith("#"):
                continue
            rows.append(dict(zip(header, line.split("\t"))))
    return rows


def parse_point(field):
    parts = (field or "").split()
    if len(parts) != 3:
        return None
    try:
        return tuple(int(p) for p in parts)
    except ValueError:
        return None


PLACEMENT_TOLERANCE = 2  # tiles, Chebyshev distance, same plane


def placed_near(origin, placement_set):
    """True if some placement of this loc sits within PLACEMENT_TOLERANCE tiles
    of `origin` on the same plane. shortest-path's Origin is the PLAYER's tile
    when the transport fires, not necessarily the loc's own registered tile —
    a multi-tile staircase is clickable from any tile it occupies, so an exact
    match is too strict (measured: with 0 tolerance ~85% of otherwise-valid
    climb rows reject as "not placed", with a scatter of ±1..±3 tile deltas in
    every direction and no dominant offset, i.e. real per-loc footprint noise
    rather than a systematic coordinate bug)."""
    ox, oz, op = origin
    for x, z, p in placement_set:
        if p == op and abs(x - ox) <= PLACEMENT_TOLERANCE and abs(z - oz) <= PLACEMENT_TOLERANCE:
            return True
    return False


def resolve_object(menu_target, menu_id, loc_names, loc_records, npc_names, npc_records):
    """menu id -> ('loc'|'npc', name, ops) if the cache agrees on id AND name."""
    name = loc_names.get(menu_id)
    if name is not None and loc_records.get(name, {}).get("name") == menu_target:
        return "loc", name, ops_of(loc_records[name])
    name = npc_names.get(menu_id)
    if name is not None and npc_records.get(name, {}).get("name") == menu_target:
        return "npc", name, ops_of(npc_records[name])
    return None


def classify_displacement(origin, dest, delta):
    """What `~climb` would do with this row's verb, versus where the row says
    the player actually lands.

    `delta` is the verb's own plane change (CLIMB_DELTAS), and the comparison
    has to use it rather than `abs(dp - op) == 1`: "the default" is not "one
    plane in either direction", it is one plane in THE DIRECTION THE VERB
    NAMES. A `Climb-down` row landing one plane UP is not a row `~climb`
    already gets right — it is a row that disagrees with the verb, and
    dropping it as redundant is how a staircase ends up answering the wrong
    op. (A bare `Climb` names no direction; `~climb_unqualified` guesses up,
    so that guess is what a 0-delta row is measured against.)"""
    ox, oz, op = origin
    dx, dz, dp = dest
    step = delta if delta else 1
    if (ox, oz) == (dx, dz) and dp - op == step:
        return "default"  # ~climb already gets this right
    if abs(dp - op) <= abs(step) and abs(dx - ox) <= 3 and abs(dz - oz) <= 3:
        return "near-miss"
    return "jump"


def harvest(content, data_dir, include_near_miss):
    loc_records = read_configs(os.path.join(content, "configs", "all.loc"))
    loc_names = read_compack(os.path.join(content, "configs", "all.loc.compack"))
    npc_records = read_configs(os.path.join(content, "configs", "all.npc"))
    npc_names = read_compack(os.path.join(content, "configs", "all.npc.compack"))
    placements = read_placements(os.path.join(content, "maps"))

    tsv_path = os.path.join(data_dir, "transports", "transports.tsv")
    rows = read_tsv(tsv_path)

    accepted = []  # (origin, dest, kind, name, slot, verb, dclass)
    rejects = collections.Counter()
    reject_examples = collections.defaultdict(list)

    for row in rows:
        object_info = (row.get("menuOption menuTarget objectID") or "").strip()
        parts = object_info.split()
        if len(parts) < 2 or not parts[-1].lstrip("-").isdigit():
            continue  # not an object-keyed row (permutation transports etc.) — not this importer's job
        verb = parts[0]
        if verb not in CLIMB_VERBS:
            continue  # non-climb transitions are a later stage (docs/MAPLINKS.md §Stage 5+)
        menu_id = int(parts[-1])
        menu_target = " ".join(parts[1:-1])

        origin = parse_point(row.get("Origin"))
        dest = parse_point(row.get("Destination"))
        if origin is None or dest is None:
            rejects["no-coords"] += 1
            continue

        resolved = resolve_object(menu_target, menu_id, loc_names, loc_records, npc_names, npc_records)
        if resolved is None:
            rejects["id-or-name-mismatch"] += 1
            reject_examples["id-or-name-mismatch"].append("id %d %r, verb %r" % (menu_id, menu_target, verb))
            continue
        kind, name, ops = resolved
        if kind != "loc":
            rejects["npc-not-loc"] += 1  # climb verbs are locs; an npc match here is a data error
            continue

        slots = [s for s, v in ops.items() if v == verb]
        if not slots:
            rejects["op-mismatch"] += 1
            reject_examples["op-mismatch"].append("%s wants %r, cache ops are %r" % (name, verb, ops))
            continue

        if not placed_near(origin, placements.get(menu_id, ())):
            rejects["not-placed-at-origin"] += 1
            reject_examples["not-placed-at-origin"].append("%s at %r" % (name, origin))
            continue

        dclass = classify_displacement(origin, dest, CLIMB_DELTAS[verb])
        if dclass == "default":
            rejects["already-correct-default"] += 1
            continue
        if dclass == "near-miss" and not include_near_miss:
            rejects["near-miss-deferred"] += 1
            continue

        accepted.append((origin, dest, name, tuple(sorted(slots)), verb, dclass))

    return accepted, rejects, reject_examples, len(rows)


TRANSITION_SOURCES = (
    # (tsv filename, verb filter or None to accept the file's own verb as-is)
    ("transports.tsv", TRANSITION_VERBS),
    ("teleportation_portals.tsv", None),
    ("teleportation_levers.tsv", None),
)


def harvest_transitions(content, data_dir):
    """Locs with no climb verb — cave mouths, portals, levers — that need a
    NEW category (`_maplink_transition`) rather than riding on ~climb's
    existing ones. Unlike climb rows there is no ±1-plane default to compare
    against, so every verified row is a candidate; the only filter is
    placement + identity, same as `harvest`."""
    loc_records = read_configs(os.path.join(content, "configs", "all.loc"))
    loc_names = read_compack(os.path.join(content, "configs", "all.loc.compack"))
    npc_records = read_configs(os.path.join(content, "configs", "all.npc"))
    npc_names = read_compack(os.path.join(content, "configs", "all.npc.compack"))
    placements = read_placements(os.path.join(content, "maps"))

    accepted = []  # (origin, dest, name, slots, verb, 'transition')
    rejects = collections.Counter()
    reject_examples = collections.defaultdict(list)
    total_rows = 0

    for filename, verb_filter in TRANSITION_SOURCES:
        path = os.path.join(data_dir, "transports", filename)
        rows = read_tsv(path)
        total_rows += len(rows)
        for row in rows:
            object_info = (row.get("menuOption menuTarget objectID") or "").strip()
            parts = object_info.split()
            if len(parts) < 2 or not parts[-1].lstrip("-").isdigit():
                continue
            verb = parts[0]
            if verb_filter is not None and verb not in verb_filter:
                continue
            if filename == "transports.tsv" and verb in CLIMB_VERBS:
                continue
            menu_id = int(parts[-1])
            menu_target = " ".join(parts[1:-1])

            origin = parse_point(row.get("Origin"))
            dest = parse_point(row.get("Destination"))
            if origin is None or dest is None:
                rejects["no-coords"] += 1
                continue

            resolved = resolve_object(menu_target, menu_id, loc_names, loc_records, npc_names, npc_records)
            if resolved is None:
                rejects["id-or-name-mismatch"] += 1
                reject_examples["id-or-name-mismatch"].append(
                    "id %d %r, verb %r (%s)" % (menu_id, menu_target, verb, filename)
                )
                continue
            kind, name, ops = resolved
            if kind != "loc":
                rejects["npc-not-loc"] += 1
                continue

            slots = [s for s, v in ops.items() if v == verb]
            if not slots:
                rejects["op-mismatch"] += 1
                reject_examples["op-mismatch"].append("%s wants %r, cache ops are %r" % (name, verb, ops))
                continue

            if not placed_near(origin, placements.get(menu_id, ())):
                rejects["not-placed-at-origin"] += 1
                reject_examples["not-placed-at-origin"].append("%s at %r" % (name, origin))
                continue

            accepted.append((origin, dest, name, tuple(sorted(slots)), verb, "transition"))

    return accepted, rejects, reject_examples, total_rows


def parse_agility_level(skills_field):
    """'23 Agility' -> 23. Multi-skill rows (grapple shortcuts — Agility PLUS
    Ranged and Strength for the crossbow-and-rope) are a different mechanic
    this importer does not model; returns None for those and for anything
    that isn't Agility-only."""
    parts = (skills_field or "").split(";")
    if len(parts) != 1:
        return None
    tokens = parts[0].split()
    if len(tokens) != 2 or tokens[1] != "Agility" or not tokens[0].isdigit():
        return None
    return int(tokens[0])


def harvest_agility(content, data_dir):
    """Agility shortcuts — a loc plus a level check, no fail-consequence
    modelling (the TSV doesn't carry one). Bound only where a record is
    category-eligible: unlike climb/transitions, a name+op fallback here
    would need to bake in a level check per block too, and getting a skill
    gate wrong (letting an under-levelled player through) is worse than
    dropping the row — so a conflicted or tile-ambiguous agility record is
    reported and left undone rather than approximated."""
    loc_records = read_configs(os.path.join(content, "configs", "all.loc"))
    loc_names = read_compack(os.path.join(content, "configs", "all.loc.compack"))
    placements = read_placements(os.path.join(content, "maps"))

    rows = read_tsv(os.path.join(data_dir, "transports", "agility_shortcuts.tsv"))
    accepted = []  # (origin, dest, name, slots, verb, level)
    rejects = collections.Counter()
    reject_examples = collections.defaultdict(list)

    for row in rows:
        object_info = (row.get("menuOption menuTarget objectID") or "").strip()
        parts = object_info.split()
        if len(parts) < 2 or not parts[-1].lstrip("-").isdigit():
            continue
        verb = parts[0]
        menu_id = int(parts[-1])
        menu_target = " ".join(parts[1:-1])

        level = parse_agility_level(row.get("Skills"))
        if level is None:
            rejects["not-agility-only"] += 1
            continue

        origin = parse_point(row.get("Origin"))
        dest = parse_point(row.get("Destination"))
        if origin is None or dest is None:
            rejects["no-coords"] += 1
            continue

        name = loc_names.get(menu_id)
        if name is None or loc_records.get(name, {}).get("name") != menu_target:
            rejects["id-or-name-mismatch"] += 1
            reject_examples["id-or-name-mismatch"].append(
                "id %d %r, verb %r" % (menu_id, menu_target, verb)
            )
            continue

        slots = [s for s, v in ops_of(loc_records[name]).items() if v == verb]
        if not slots:
            rejects["op-mismatch"] += 1
            reject_examples["op-mismatch"].append(
                "%s wants %r, cache ops are %r" % (name, verb, ops_of(loc_records[name]))
            )
            continue

        if not placed_near(origin, placements.get(menu_id, ())):
            rejects["not-placed-at-origin"] += 1
            reject_examples["not-placed-at-origin"].append("%s at %r" % (name, origin))
            continue

        accepted.append((origin, dest, name, tuple(sorted(slots)), verb, level))

    return accepted, rejects, reject_examples, len(rows)


def row_dir(verb):
    """The plane change the verb names, 0 when it names none. Transition and
    agility verbs (`Enter`, `Squeeze-through`, ...) are all 0 — they have no
    direction to disagree about, so those two harvests key exactly the way
    they did before `dir` existed."""
    return CLIMB_DELTAS.get(verb, 0)


def split_unambiguous(accepted):
    """Group by (exact origin tile, direction); a key with >1 distinct
    destination cannot be answered by the table and needs a name+op binding
    instead.

    Direction is part of the key because it is part of the lookup: a spiral
    staircase's middle floor offers up and down from ONE tile, and
    `~maplink_try` is told which one the player asked for. Two rows that share
    a tile but name opposite directions are two answers to two questions, not
    an ambiguity. A 0-direction row is the exception — it matches any
    direction at runtime, so it collides with every other row on its tile and
    the whole tile is ambiguous when one turns up alongside a directed row.

    A name+op binding is name-wide, not tile-specific — `[oploc1,<name>]`
    fires for EVERY placement of that name, not just the one tile it was
    generated from. So it is only safe for a name with EXACTLY ONE placement
    in the whole accepted set. A name placed at tile A (one destination,
    correctly answered by the coord table) that ALSO happens to sit at an
    ambiguous tile B (shared with some other name, each offering a different
    direction) cannot be given a name+op override for B — that override would
    hijack tile A's clicks too, sending every instance of the name to B's
    answer. Measured: this is not hypothetical — `my2arm_cliff_shortcut_1`
    and `_2` are placed at both ends of one bidirectional cliff jump, and
    without this check each would silently teleport an already-arrived player
    straight back to where they started."""
    by_key = collections.defaultdict(list)
    wildcard_origins = set()  # tiles carrying a 0-direction row — collides with every other row there
    for entry in accepted:
        by_key[(entry[0], row_dir(entry[4]))].append(entry)
        if row_dir(entry[4]) == 0:
            wildcard_origins.add(entry[0])

    origins_by_name = collections.defaultdict(set)
    for entry in accepted:
        origins_by_name[entry[2]].add(entry[0])

    keys_by_origin = collections.Counter(origin for origin, _dir in by_key)

    table_rows = []  # (origin, dest, name, dir)
    ambiguous = []  # (origin, dest, name, slots, verb, ...) — single-placement names only
    dropped_multi_placement = set()
    for (origin, direction), entries in by_key.items():
        dests = {e[1] for e in entries}
        collides = direction != 0 and origin in wildcard_origins
        if len(dests) == 1 and not (collides or (direction == 0 and keys_by_origin[origin] > 1)):
            table_rows.append((origin, entries[0][1], entries[0][2], direction))
            continue
        for entry in entries:
            name = entry[2]
            if len(origins_by_name[name]) == 1:
                ambiguous.append(entry)
            else:
                dropped_multi_placement.add(name)
    return table_rows, ambiguous, dropped_multi_placement


def build_dbtable_text():
    return (
        "// The climb destinations `~climb` cannot derive from the loc's own\n"
        "// menu verb — see docs/MAPLINKS.md. Generated rows live in maplink.dbrow;\n"
        "// this schema is hand-authored and stable.\n"
        "//\n"
        "// `dir` is the plane change the row's own menu verb names: +1 Climb-up,\n"
        "// -1 Climb-down, +2 Top-floor, -2 Bottom-floor, 0 for a verb that names\n"
        "// no direction (a bare Climb, or a transition — a cave mouth or portal).\n"
        "// `~maplink_try` skips a row whose `dir` contradicts the op the player\n"
        "// clicked, which is what keeps a spiral staircase's middle floor from\n"
        "// answering Climb-down with the tile's Climb-up row.\n"
        "[maplink]\n"
        "column=src,coord,INDEXED,REQUIRED\n"
        "column=dest,coord,REQUIRED\n"
        "column=loc,loc,REQUIRED\n"
        "column=dir,int,REQUIRED\n"
    )


# One tile can now hold more than one row, so the row's own name has to say
# which — `[maplink_<src>]` alone would emit two blocks under one key.
DIR_SUFFIX = {0: "", 1: "_up", -1: "_down", 2: "_topfloor", -2: "_bottomfloor"}


def build_dbrow_text(table_rows, near_miss):
    lines = [
        "// Generated by tools/maplink_import.py — do not edit, re-run the importer.\n",
        "// %d rows (%s). See docs/MAPLINKS.md.\n\n"
        % (len(table_rows), "jumps + near-miss" if near_miss else "jumps only"),
    ]
    for origin, dest, name, direction in sorted(table_rows):
        src_lit = to_coord_literal(*origin)
        dest_lit = to_coord_literal(*dest)
        lines.append("[maplink_%s%s]\n" % (src_lit, DIR_SUFFIX[direction]))
        lines.append("table=maplink\n")
        lines.append("data=src,%s\n" % src_lit)
        lines.append("data=dest,%s\n" % dest_lit)
        lines.append("data=loc,%s\n" % name)
        lines.append("data=dir,%d\n\n" % direction)
    return "".join(lines)


def build_shared_text(ambiguous, dropped_multi_placement, header=SHARED_HEADER):
    # split_unambiguous already restricts `ambiguous` to names with exactly
    # one placement in the whole accepted set, so every name here is safe for
    # a name-wide binding.
    by_name = collections.defaultdict(list)
    for entry in ambiguous:
        origin, dest, name, slots, verb = entry[0], entry[1], entry[2], entry[3], entry[4]
        by_name[name].append((origin, dest, slots, verb))

    total_rows = sum(len(v) for v in by_name.values())
    dropped_note = (
        "\n// %d more names are placed at MORE THAN ONE tile and one of those\n"
        "// tiles is ambiguous — a name+op binding is name-wide, not\n"
        "// tile-specific, so it would hijack the name's other, unambiguous\n"
        "// placements too. Dropped rather than risk that: %s\n"
        % (len(dropped_multi_placement), ", ".join(sorted(dropped_multi_placement)))
        if dropped_multi_placement
        else ""
    )
    lines = [header.format(count=len(by_name), rows=total_rows) + dropped_note]
    for name in sorted(by_name):
        entries = by_name[name]
        lines.append("\n// %s\n" % name)
        for origin, dest, slots, verb in sorted(entries):
            dest_lit = to_coord_literal(*dest)
            for slot in slots:
                lines.append("[oploc%d,%s] p_teleport(%s);\n" % (slot, name, dest_lit))
    return "".join(lines), by_name


TRANSITION_LOC_HEADER = """\
// `category=%s` on every cave mouth, portal and lever this cache places that
// `tools/maplink_import.py` found a verified, unambiguous destination for.
// The bare name here is the one `pack/category.pack` allocates; the trigger
// that binds it spells it `[oploc<n>,%s]` — the leading underscore is what
// tells the compiler this trigger subject is a category, not a loc name.
//
// Generated by tools/maplink_import.py — do not edit, re-run the importer.
//
// These carry no climb verb, so they have no existing category to ride on
// the way climb-verbed locs ride `_climb_up`/`_climb_down` — this is what
// gives `[oploc<n>,%s]` in scripts/maplink.rs2 something to bind to. A
// record another authored `.loc` already categorises, or whose cache
// category `pack/category.pack` already names, is skipped here and reported
// — the same two conflict rules `tools/ladder_import.py` applies to
// `ladders.loc`.
//
// {count} records.
""" % (TRANSITION_CATEGORY, TRANSITION_CATEGORY_TRIGGER, TRANSITION_CATEGORY_TRIGGER)

TRANSITION_SHARED_HEADER = """\
// Transition destinations (portals, levers, cave mouths) that cannot take
// the `{cat}` category: either another authored `.loc` or the cache itself
// already categorises the record, or two verified rows share one origin tile
// and name two different destinations.
//
// Generated by tools/maplink_import.py — do not edit, re-run the importer.
//
// Bound by NAME and op slot, the same mechanism
// `ladders_stairs/scripts/climb_shared.rs2` and `maplink_shared.rs2` use: the
// trigger table's name rung beats its category rung.
//
// {{count}} names, {{rows}} bindings.
""".format(cat=TRANSITION_CATEGORY)


def split_category_conflicts(table_rows, accepted, claimed):
    """table_rows -> (category_ok, needs_name_binding, dropped).

    A record already claimed by another `.loc` overlay or a named cache
    category cannot take the new category (cachepack allows one). Route it to
    a name+op binding instead — but ONLY if it has exactly one placement in
    the whole accepted set, for the same reason `split_unambiguous` requires
    that: a name-wide binding would otherwise hijack every other placement of
    the name too."""
    origins_by_name = collections.defaultdict(set)
    for entry in accepted:
        origins_by_name[entry[2]].add(entry[0])

    category_ok = []
    needs_name_binding = []
    dropped = set()
    for origin, dest, name, direction in table_rows:
        if name not in claimed:
            category_ok.append((origin, dest, name, direction))
            continue
        if len(origins_by_name[name]) == 1:
            slots = next(e[3] for e in accepted if e[2] == name and e[0] == origin)
            verb = next(e[4] for e in accepted if e[2] == name and e[0] == origin)
            needs_name_binding.append((origin, dest, name, slots, verb))
        else:
            dropped.add(name)
    return category_ok, needs_name_binding, dropped


def build_transition_loc_text(category_ok_names):
    lines = [TRANSITION_LOC_HEADER.format(count=len(category_ok_names))]
    for name in sorted(category_ok_names):
        lines.append("\n[%s]\ncategory=%s\n" % (name, TRANSITION_CATEGORY))
    return "".join(lines)


AGILITY_LOC_HEADER = """\
// `category=%s` on every agility shortcut this cache places that
// `tools/maplink_import.py` found a verified, unambiguous destination AND a
// single Agility level requirement for (multi-skill grapple shortcuts are a
// different mechanic — parse_agility_level rejects them, see
// docs/MAPLINKS.md). Only category-eligible records are bound: a record
// already claimed by another overlay, or ambiguous at more than one tile, is
// dropped rather than approximated, because a wrong fallback here means a
// skill check that silently doesn't apply.
//
// Generated by tools/maplink_import.py — do not edit, re-run the importer.
//
// {count} records.
""" % (AGILITY_CATEGORY,)

AGILITY_DBTABLE_TEXT = (
    "// Agility shortcut destinations plus the level `~maplink_agility`\n"
    "// (scripts/maplink.rs2) checks before teleporting. See docs/MAPLINKS.md.\n"
    "// Rows live in maplink_agility.dbrow; this schema is hand-authored and stable.\n"
    "[maplink_agility]\n"
    "column=src,coord,INDEXED,REQUIRED\n"
    "column=dest,coord,REQUIRED\n"
    "column=loc,loc,REQUIRED\n"
    "column=level,int,REQUIRED\n"
)


def build_agility_dbrow_text(rows_with_level):
    lines = [
        "// Generated by tools/maplink_import.py — do not edit, re-run the importer.\n",
        "// %d rows. See docs/MAPLINKS.md.\n\n" % len(rows_with_level),
    ]
    for origin, dest, name, level in sorted(rows_with_level):
        src_lit = to_coord_literal(*origin)
        dest_lit = to_coord_literal(*dest)
        lines.append("[maplink_agility_%s]\n" % src_lit)
        lines.append("table=maplink_agility\n")
        lines.append("data=src,%s\n" % src_lit)
        lines.append("data=dest,%s\n" % dest_lit)
        lines.append("data=loc,%s\n" % name)
        lines.append("data=level,%d\n\n" % level)
    return "".join(lines)


def build_agility_loc_text(category_ok_names):
    lines = [AGILITY_LOC_HEADER.format(count=len(category_ok_names))]
    for name in sorted(category_ok_names):
        lines.append("\n[%s]\ncategory=%s\n" % (name, AGILITY_CATEGORY))
    return "".join(lines)


REJECT_REASONS = {
    "already-correct-default": "Same tile, plane ±1 — `~climb`'s own default already gets this right.",
    "not-placed-at-origin": "No placement of this object id sits within %d tiles of the row's Origin "
    "in this cache's maps/*.jl2 — usually content added to the wiki's source after rev 239." % PLACEMENT_TOLERANCE,
    "id-or-name-mismatch": "The object id doesn't resolve in this cache, or resolves to a record whose "
    "`name=` doesn't match the row's menu target.",
    "op-mismatch": "The resolved record's `opN=` fields don't state the row's menu option at all.",
    "npc-not-loc": "The id resolved against `all.npc.compack` instead of `all.loc.compack` — a climb "
    "verb should never key an npc; treated as a data error.",
    "near-miss-deferred": "Within 3 tiles / 1 plane of the default — accepted only with --near-miss.",
    "no-coords": "Row has no parseable Origin/Destination world point.",
    "not-agility-only": "Skills column names more than Agility (a grapple shortcut — Agility, Ranged and "
    "Strength for the crossbow-and-rope) or isn't Agility at all; a different mechanic, not modelled here.",
}


def _append_reject_section(lines, rejects, examples):
    for reason, count in rejects.most_common():
        lines.append("## %s — %d\n\n%s\n\n" % (reason, count, REJECT_REASONS.get(reason, "")))
        ex = examples.get(reason, [])
        if ex:
            shown = ex[:50]
            for e in shown:
                lines.append("- %s\n" % e)
            if len(ex) > len(shown):
                lines.append("- … %d more\n" % (len(ex) - len(shown)))
            lines.append("\n")


def build_rejects_doc(rejects, examples, total_rows, accepted_count, near_miss,
                       t_rejects=None, t_examples=None, t_total=None, t_accepted_count=None,
                       a_rejects=None, a_examples=None, a_total=None, a_accepted_count=None):
    lines = [
        "# maplink_import rejects\n\n",
        "Generated by `tools/maplink_import.py` — do not edit, re-run the importer.\n"
        "Every `transports.tsv` row that did NOT make it into `maplink.dbrow` "
        "or a name-bound override, and why. See `docs/MAPLINKS.md` for the "
        "pipeline this is the tail end of.\n\n",
        "## Climb (stairs, ladders, trapdoors)\n\n"
        "%d climb-verb rows considered, %d accepted (%s), %d rejected.\n\n"
        % (total_rows, accepted_count, "jumps + near-miss" if near_miss else "jumps only",
           sum(rejects.values())),
    ]
    _append_reject_section(lines, rejects, examples)
    if t_rejects is not None:
        lines.append(
            "## Transitions (portals, levers, cave mouths)\n\n"
            "%d rows considered, %d accepted, %d rejected.\n\n"
            % (t_total, t_accepted_count, sum(t_rejects.values()))
        )
        _append_reject_section(lines, t_rejects, t_examples)
    if a_rejects is not None:
        lines.append(
            "## Agility shortcuts\n\n"
            "%d rows considered, %d accepted, %d rejected.\n\n"
            % (a_total, a_accepted_count, sum(a_rejects.values()))
        )
        _append_reject_section(lines, a_rejects, a_examples)
    return "".join(lines)


def merge_table_rows(climb_rows, transition_rows):
    """Combine both classes into one `maplink` table. A collision — the same
    origin tile AND direction claimed by both a climb row and a transition row
    with DIFFERENT destinations — can't be represented as one dbrow, so both
    sides are dropped and reported rather than one silently overwriting the
    other (a genuine cross-class conflict has never been observed on this
    cache; guarded anyway since a coord key is shared state).

    A transition row is always direction 0, which `~maplink_try` matches for
    any op — so a transition sharing a tile with a directed climb row is a
    collision on that tile whatever the directions say, and the key widens to
    the tile alone as soon as one side is 0."""
    by_key = {}
    conflicts = set()
    wildcard_origins = {o for o, _d, _n, direction in climb_rows + transition_rows if direction == 0}
    for origin, dest, name, direction in climb_rows + transition_rows:
        key = origin if origin in wildcard_origins else (origin, direction)
        if key in by_key and by_key[key][0] != dest:
            conflicts.add(origin)
        else:
            by_key[key] = (dest, name, origin, direction)
    merged = [(o, d, n, r) for (d, n, o, r) in by_key.values() if o not in conflicts]
    return merged, conflicts


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser()
    parser.add_argument("--content", default=os.path.join(here, "OSRS-Content", "osrs239-content"))
    parser.add_argument("--data", default=os.path.join(here, "tools", "data", "shortest_path"))
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--report", action="store_true")
    parser.add_argument("--near-miss", action="store_true")
    args = parser.parse_args()

    ladders_dir = os.path.join(args.content, "server", "scripts", "ladders_stairs")
    dbtable_path = os.path.join(ladders_dir, "configs", "maplink.dbtable")
    dbrow_path = os.path.join(ladders_dir, "configs", "maplink.dbrow")
    shared_path = os.path.join(ladders_dir, "scripts", "maplink_shared.rs2")
    transition_loc_path = os.path.join(ladders_dir, "configs", "maplinks.loc")
    transition_shared_path = os.path.join(ladders_dir, "scripts", "maplink_transitions_shared.rs2")
    rejects_path = os.path.join(here, "docs", "MAPLINKS_REJECTS.md")

    agility_dir = os.path.join(args.content, "server", "scripts", "skill_agility")
    agility_dbtable_path = os.path.join(agility_dir, "configs", "maplink_agility.dbtable")
    agility_dbrow_path = os.path.join(agility_dir, "configs", "maplink_agility.dbrow")
    agility_loc_path = os.path.join(agility_dir, "configs", "maplink_agility.loc")

    # --- climb (~climb's own destinations) ---
    accepted, rejects, examples, total_rows = harvest(args.content, args.data, args.near_miss)
    table_rows, ambiguous, dropped_multi = split_unambiguous(accepted)
    shared_text, _ = build_shared_text(ambiguous, dropped_multi, header=SHARED_HEADER)

    # --- transitions (portals, levers, cave mouths — no climb verb) ---
    t_accepted, t_rejects, t_examples, t_total = harvest_transitions(args.content, args.data)
    t_table_rows, t_ambiguous, t_dropped_multi = split_unambiguous(t_accepted)

    loc_records = read_configs(os.path.join(args.content, "configs", "all.loc"))
    category_names = read_category_pack(os.path.join(args.content, "pack", "category.pack"))
    other_categories = read_other_loc_categories(args.content, exclude_paths=[transition_loc_path])
    cache_named_categories = {
        name
        for name, record in loc_records.items()
        if record.get("category") and int(record["category"]) in category_names
    }
    claimed = set(other_categories) | cache_named_categories

    category_ok, needs_binding, conflict_dropped = split_category_conflicts(
        t_table_rows, t_accepted, claimed
    )
    transition_loc_text = build_transition_loc_text({name for _, _, name, _dir in category_ok})
    transition_shared_text, _ = build_shared_text(
        t_ambiguous + needs_binding, t_dropped_multi | conflict_dropped, header=TRANSITION_SHARED_HEADER
    )

    # --- agility shortcuts (loc + level check, no fail modelling) ---
    a_accepted, a_rejects, a_examples, a_total = harvest_agility(args.content, args.data)
    a_table_rows, a_ambiguous, a_dropped_multi = split_unambiguous(a_accepted)
    # Real, purpose-built agility content already exists for some shortcuts
    # (skill_agility/scripts/agility_shortcuts_osrs.rs2 — animations, XP,
    # Kronos-policy fail handling). Categorising a name that already has its
    # own [oploc,<name>] binding would just be dead code (name beats category
    # regardless), so those are excluded up front rather than counted as
    # "covered" by this generic pass.
    # Exclude every file this run itself writes (or is about to write) — not
    # just the .loc overlays. `maplink_shared.rs2`/`maplink_transitions_shared.rs2`
    # are generated by the SAME invocation, earlier in this function; scanning
    # their on-disk content here would read the PREVIOUS run's output rather
    # than what this run just computed, which is a real source of non-idempotency
    # across two consecutive runs (caught by `--check` disagreeing with itself).
    already_scripted = scan_existing_oploc_bindings(
        args.content,
        exclude_paths=[
            agility_loc_path, transition_loc_path, shared_path, transition_shared_path,
            os.path.join(ladders_dir, "scripts", "maplink.rs2"),
            os.path.join(agility_dir, "scripts", "maplink_agility.rs2"),
        ],
    )
    already_scripted_names = {name for (_slot, name) in already_scripted}
    a_names_before_filter = {n for _, _, n, _dir in a_table_rows}
    a_table_rows = [r for r in a_table_rows if r[2] not in already_scripted_names]
    a_hand_scripted_count = len(a_names_before_filter & already_scripted_names)
    # `claimed` (computed above for transitions) excluded the wrong path for
    # this purpose — it did not exclude agility_loc_path, so on a second run
    # it would read agility's OWN previous output back as "another overlay's
    # claim" and self-poison every record it had just categorised. Recomputed
    # fresh here, excluding agility's own file instead.
    a_other_categories = read_other_loc_categories(args.content, exclude_paths=[agility_loc_path])
    a_claimed = (
        set(a_other_categories) | cache_named_categories
        | {name for _, _, name, _dir in category_ok} | already_scripted_names
    )
    a_category_ok, a_needs_binding, a_conflict_dropped = split_category_conflicts(
        a_table_rows, a_accepted, a_claimed
    )
    a_level_by_origin = {e[0]: e[5] for e in a_accepted}
    a_rows_with_level = [
        (origin, dest, name, a_level_by_origin[origin]) for origin, dest, name, _dir in a_category_ok
    ]
    agility_loc_text = build_agility_loc_text({name for _, _, name, _dir in a_category_ok})
    agility_dbrow_text = build_agility_dbrow_text(a_rows_with_level)
    # a_needs_binding/a_conflict_dropped are deliberately NOT bound by name —
    # see harvest_agility's docstring. Folded into "dropped" for reporting.
    a_dropped_total = a_dropped_multi | a_conflict_dropped | {n for o, d, n, s, v in a_needs_binding}

    # --- shared dbtable ---
    merged_table_rows, cross_class_conflicts = merge_table_rows(
        table_rows, list(category_ok)
    )
    dbtable_text = build_dbtable_text()
    dbrow_text = build_dbrow_text(merged_table_rows, args.near_miss)
    rejects_text = build_rejects_doc(
        rejects, examples, total_rows, len(accepted), args.near_miss,
        t_rejects=t_rejects, t_examples=t_examples, t_total=t_total, t_accepted_count=len(t_accepted),
        a_rejects=a_rejects, a_examples=a_examples, a_total=a_total, a_accepted_count=len(a_accepted),
    )

    if args.report:
        print("maplink_import: climb — %d rows considered, %d accepted, %d table rows, %d name-bound, "
              "%d dropped (multi-placement)" % (
                  total_rows, len(accepted), len(table_rows), len(ambiguous), len(dropped_multi)))
        for reason, count in rejects.most_common():
            print("  rejected %-24s %5d" % (reason, count))
            for ex in examples.get(reason, [])[:5]:
                print("      e.g. %r" % (ex,))
        print("maplink_import: transitions — %d rows considered, %d accepted, %d category-bound, "
              "%d name-bound, %d dropped (category conflict or multi-placement)" % (
                  t_total, len(t_accepted), len(category_ok), len(t_ambiguous) + len(needs_binding),
                  len(t_dropped_multi) + len(conflict_dropped)))
        for reason, count in t_rejects.most_common():
            print("  rejected %-24s %5d" % (reason, count))
            for ex in t_examples.get(reason, [])[:5]:
                print("      e.g. %r" % (ex,))
        if cross_class_conflicts:
            print("maplink_import: %d origin tiles conflict between climb and transition rows, dropped" %
                  len(cross_class_conflicts))
        print("maplink_import: agility — %d rows considered, %d accepted, %d category-bound, "
              "%d already hand-scripted, %d dropped (conflict or multi-placement)" % (
                  a_total, len(a_accepted), len(a_category_ok), a_hand_scripted_count,
                  len(a_dropped_total)))
        for reason, count in a_rejects.most_common():
            print("  rejected %-24s %5d" % (reason, count))
            for ex in a_examples.get(reason, [])[:5]:
                print("      e.g. %r" % (ex,))
        return 0

    outputs = (
        (dbtable_path, dbtable_text),
        (dbrow_path, dbrow_text),
        (shared_path, shared_text),
        (transition_loc_path, transition_loc_text),
        (transition_shared_path, transition_shared_text),
        (rejects_path, rejects_text),
        (agility_dbtable_path, AGILITY_DBTABLE_TEXT),
        (agility_dbrow_path, agility_dbrow_text),
        (agility_loc_path, agility_loc_text),
    )

    if args.check:
        for path, want in outputs:
            try:
                with open(path, encoding="utf-8") as handle:
                    on_disk = handle.read()
            except OSError:
                print("maplink_import: %s is missing" % path, file=sys.stderr)
                return 1
            if on_disk != want:
                print(
                    "maplink_import: %s does not match the source data — re-run tools/maplink_import.py"
                    % path,
                    file=sys.stderr,
                )
                return 1
        return 0

    for path, want in outputs:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(want)
    print("maplink_import: wrote %d combined rows (%d climb + %d transition) to %s" % (
        len(merged_table_rows), len(table_rows), len(category_ok), dbrow_path))
    print("maplink_import: wrote %d climb name bindings to %s" % (len(ambiguous), shared_path))
    print("maplink_import: wrote %d transition category records to %s" % (
        len(category_ok), transition_loc_path))
    print("maplink_import: wrote %d transition name bindings to %s" % (
        len(t_ambiguous) + len(needs_binding), transition_shared_path))
    if dropped_multi:
        print("maplink_import: %d climb names dropped (multi-placement collision): %s" % (
            len(dropped_multi), ", ".join(sorted(dropped_multi))))
    if t_dropped_multi or conflict_dropped:
        print("maplink_import: %d transition names dropped (conflict or multi-placement): %s" % (
            len(t_dropped_multi | conflict_dropped), ", ".join(sorted(t_dropped_multi | conflict_dropped))))
    if cross_class_conflicts:
        print("maplink_import: %d tiles dropped (climb/transition destination conflict)" %
              len(cross_class_conflicts))
    print("maplink_import: wrote %d agility category records to %s" % (
        len(a_category_ok), agility_loc_path))
    if a_hand_scripted_count:
        print("maplink_import: %d agility names skipped (already hand-scripted elsewhere)" %
              a_hand_scripted_count)
    if a_dropped_total:
        print("maplink_import: %d agility names dropped (conflict or multi-placement): %s" % (
            len(a_dropped_total), ", ".join(sorted(a_dropped_total))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
