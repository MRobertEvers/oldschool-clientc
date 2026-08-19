#!/usr/bin/env python3
"""Rebuild the world's `.spawn` roster from an external spawn dump.

    tools/gen_spawns.py \
        --content OSRS-Content/osrs239-content \
        --npc-json  ~/Documents/git_repos/xrsps-typescript/server/data/npc-spawns.json \
        --obj-json  ~/Documents/git_repos/xrsps-typescript/server/gamemodes/vanilla/data/groundItemSpawnData.json \
        --out OSRS-Content/osrs239-content/server/scripts/areas/world/configs \
        --report /tmp/spawn_report.txt

An OldSchool cache holds no spawns — where an npc stands and what lies on the
ground is server state, so it has to come from outside the cache and then be
*checked against* it. That check is the whole point of this script, and
docs/ITEM_AND_NPCS.md is the long form of why.

A dump carries bare ids. This tree does not: a spawn names its npc, resolved
through `configs/all.<type>.compack`. Rewriting an id as a name is what makes a
cache bump visible, because an id that has been reallocated between the dump's
revision and this one stops meaning what the dump thought it meant --- and the
dump's own `name` field is the second opinion that catches it.

Rules, in the order they run:

  1. **The id must exist** in this cache's config table. An id past the end is
     dropped, named in the report.
  2. **The name must agree**, comparing case- and punctuation-insensitively and
     following `multinpc` chains --- a spawn of a multinpc *base* is correct and
     legitimately reports the display name of whichever variant is live, so the
     base's whole reachable name set is what the dump is checked against.
     Disagreement is `drift`: the id now names a different creature. Dropped.
  3. **A record with no name anywhere** (no `name=`, no named variant) has
     nothing to contradict and is kept, counted separately.
  4. **The map square must exist** under `maps/`. A spawn on a square this cache
     does not ship has no terrain to stand on. Dropped.
  5. Level must be 0..3; exact duplicates collapse.

Ground objs are the weak half and the report says so out loud: the obj dump
carries no name, so rule 2 cannot run on it and only rule 1 does.

Output is one file per map square, `m<mx>_<mz>.spawn`, holding that square's
`==== NPC ====` and `==== OBJ ====` sections in the grammar
`src/net/mock/mock230_content.c` reads. Files are rewritten wholesale: the
directory is cleared of `.spawn` first, so a spawn that leaves the dump leaves
the tree.
"""

import argparse
import collections
import json
import os
import re
import sys


# ---------------------------------------------------------------- cache side


def load_compack(path):
    """`configs/all.<type>.compack` --- `id=name`, the id authority."""
    out = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("//"):
                continue
            ident, name = line.split("=", 1)
            out[int(ident)] = name
    return out


def load_blocks(path):
    """`configs/all.<type>` --- `[name]` blocks of `key=value`.

    First value wins for a repeated key, which is what the multi-valued keys
    (`multinpc1`, `multinpc2`, ...) need, since each is its own key anyway.
    """
    out = {}
    current = None
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if line.startswith("[") and line.endswith("]"):
                current = line[1:-1]
                out[current] = {}
            elif current is not None and "=" in line and not line.startswith("//"):
                key, value = line.split("=", 1)
                out[current].setdefault(key, value)
    return out


def normalise(text):
    """Compare display names the way a person would.

    `<col=00ffff>Iceberg</col>` and `Iceberg` are the same npc; so are
    `Al Kharid warrior` and `Al-Kharid warrior`, and `Tea Seller` and
    `Tea seller`. None of those is id drift, and a comparison that called them
    drift would bury the 60-odd cases that are.
    """
    return re.sub(r"[^a-z0-9]", "", re.sub(r"<[^>]*>", "", text or "").lower())


# Audited display-name aliases for rows whose id and cache symbol are stable,
# but whose external dump used a genuinely different presentation. Each key is
# `(cache symbol, dump display name)` and each value is the reachable cache
# display name that must still be present. Requiring all three facts keeps this
# narrow: if a later cache reallocates the id/symbol or renames the variant, the
# row goes back to `name drift` instead of being admitted by a fuzzy match.
NPC_NAME_ALIASES = {
    ("head_wizard", normalise("Sedridor")): {normalise("Archmage Sedridor")},
}

# These three records are scenery/cage occupants in the external map dump, not
# live world combatants.  Fight Arena creates an owner-private copy for the
# player whose round is active; retaining the dump rows would expose an
# attackable public duplicate and permit cross-player quest credit.
#
# Source audit: https://oldschool.runescape.wiki/w/Fight_Arena?oldid=15240956
# Whole map squares whose every npc belongs to an instance rather than to the
# world. A per-tile exclusion list is the wrong shape for these: the Theatre of
# Blood's squares hold the six bosses AND whatever the external dump happened
# to be carrying when it was taken, and both are equally wrong in public space.
#
# What the dump actually put in the Maiden room, before this: one Maiden at
# (3162,4444) and a 2x5 GRID of `tob_verzik_phase2_bloodnylocas_story` at
# x=3170/3176, y=4438..4454. Verzik's blood nylocas, parked in Maiden's room, in
# rows. They are not a spawn anybody intended - they are an artefact of the
# import - and in game they read as "the boss room is full of crabs before the
# fight starts".
#
# The Maiden was worse than cosmetic. `map_instance_from_square` COPIES a
# square's spawns into the instance, so a world-spawned boss becomes two: the
# public one and the instanced one, standing side by side. Every ToB room is
# built by `~tob_build_room`, which adds its own boss with party-scaled
# hitpoints, so the square must ship empty.
#
# Squares: 12613 Maiden, 13125 Bloat, 13122 Nylocas, 13123 Sotetseg + 13379 its
# shadow realm, 12612 Xarpus, 12611 Verzik, 12867 the loot room, 12869 the
# corridor. Named as (square_x, square_z), which is `region >> 8` and
# `region & 0xFF`.
INSTANCED_SQUARES = {
    (49, 69),   # Maiden
    (51, 69),   # Bloat
    (51, 66),   # Nylocas
    (51, 67),   # Sotetseg
    (52, 67),   # Sotetseg's shadow realm
    (49, 68),   # Xarpus
    (49, 67),   # Verzik
    (50, 67),   # loot room
    (50, 69),   # corridor

    # The same bug, found by auditing every `map_instance_from_square` caller
    # against its square. All three squares hold ONLY the encounter's actors,
    # so the exclusion costs no public npc:
    (37, 79),   # Fight Cave  - a world-spawned TzTok-Jad, copied per instance
    (35, 83),   # Inferno     - THREE `inferno_jad_finalwave`, ditto
    (28, 80),   # Dream Mentor - The Inadequacy
}

# Worth stating why this is fixed here rather than in each encounter: the
# Inferno already survives it, by deleting every npc inside its fresh instance
# on build (`npc_findallany` + `npc_del` in inferno.rs2). That is a workaround
# for this bug, and it works, which is exactly why the bug went unnoticed for
# so long - the arena looked right while the square underneath it was wrong.
# The Theatre had no such sweep, so its Maiden simply appeared twice.

NPC_SPAWN_EXCLUSIONS = {
    ("arena_scorpion", 2608, 3159, 0),
    ("arena_bouncer", 2608, 3162, 0),
    ("arena_ogre", 2608, 3165, 0),
    # Priest in Peril's Temple Guardian has been instanced since 2017. The
    # quest zone creates one owner-private actor for each eligible player.
    ("priestperilguarddog", 3405, 9902, 0),
    # Underground Pass encounter actors are recreated owner-privately by the
    # quest controller. Keep the pinned dump's scenery/public copies out.
    ("upass_paladin1", 2424, 9721, 0),
    ("upass_paladin2", 2422, 9718, 0),
    ("upass_paladin3", 2426, 9718, 0),
    ("kalrag", 2356, 9911, 0),
    ("othainian", 2122, 4562, 1),
    ("holthion", 2132, 4554, 1),
    ("doomion", 2134, 4565, 1),
    ("iban", 2133, 4647, 1),
    ("ibanmonk", 2149, 4646, 1),
    ("ibanmonk", 2150, 4648, 1),
    ("ibanmonk", 2153, 4646, 1),
    ("ibanmonk", 2153, 4649, 1),
    ("ibanmonk", 2156, 4646, 1),
    ("ibanmonk", 2157, 4649, 1),
    ("ibanmonk", 2159, 4635, 1),
    ("ibanmonk", 2159, 4642, 1),
    ("ibanmonk", 2159, 4646, 1),
    ("ibanmonk", 2159, 4650, 1),
    ("ibanmonk", 2160, 4662, 1),
    ("ibanmonk", 2163, 4653, 1),
    ("ibanmonk", 2163, 4660, 1),
    # Regicide creates the trail-blocking Tyras guard privately at the exact
    # dense-forest crossing. These dump rows are occupants of the later tent
    # cutscene map and must not become public quest-credit targets.
    ("regicide_old_camp_guard", 2312, 4556, 0),
    ("regicide_old_camp_guard", 2314, 4556, 0),
    ("regicide_old_camp_guard", 2316, 4556, 0),
    # Tai Bwo Wannai Trio's two Tamayu/Shaikahan cutscene pairs are created
    # owner-privately when the player joins a hunt.
    ("tbwt_tamayu_hunter", 2523, 4567, 0),
    ("tbwt_beast_cutscene", 2524, 4567, 0),
    ("tbwt_beast_cutscene", 2540, 4566, 0),
    ("tbwt_tamayu_final_hunter", 2541, 4565, 0),
    # Troll Stronghold recreates Dad and the two prisoners owner-privately so
    # one player's surrender/cell state cannot advance another player's quest.
    # Eadgar's post-rescue cave spawn (2890, 10086, plane 2) remains public.
    ("troll_champion", 2911, 3612, 0),
    ("troll_godric", 2827, 10077, 0),
    ("troll_eadgar", 2829, 10083, 0),
}


# The obj dump is the weak half --- it carries a bare id and no name, so rule 2
# cannot run on it and a row that names the wrong member of an id family sails
# straight through. Each entry here is `(dump id, x, z, plane)` -> the cache
# symbol the row must resolve to instead. Pinning all four coordinates keeps it
# narrow: the correction applies to the one tile that was audited and to no
# other spawn of the same id.
#
# `druid_pouch_empty` (2957) is the pouch; `druid_pouch` (2958) is stackable and
# is how this tree represents a single *charge* --- `[label,druid_pouch_fill]`
# in quest_druidspirit.rs2 adds `druid_pouch` x <charges> once three herbs are
# in. So the dump's 2958 in the Nature Grotto lays down one loose charge and no
# container, which is not a replacement for a lost pouch; Filliman's own
# recovery path hands out `druid_pouch_empty`, and the grotto respawn has to
# agree with it. Audited against
# https://oldschool.runescape.wiki/w/Druid_pouch and quest_druidspirit.rs2.
OBJ_SPAWN_ID_CORRECTIONS = {
    (2958, 3443, 9741, 1): "druid_pouch_empty",
}


def reachable_names(blocks, name, depth=0, seen=None):
    """Every display name this record can present as, following `multinpc`.

    A multinpc base has no `name=` of its own --- it is a shell that a varp or
    varbit resolves to one of its variants. A dump that observed the live world
    saw the variant, so the base has to be checked against the variants' names
    rather than against nothing. Checking against nothing is how npc 401
    (`tog_light_creature`, a Tears of Guthix light creature) passed as Turael.
    """
    seen = set() if seen is None else seen
    if name in seen or depth > 4 or name not in blocks:
        return set()
    seen.add(name)
    block = blocks[name]
    out = set()
    if "name" in block:
        out.add(normalise(block["name"]))
    for key, value in block.items():
        if key.startswith("multinpc") and value != "-1":
            out |= reachable_names(blocks, value, depth + 1, seen)
    return out


def load_squares(maps_dir):
    """The map squares this cache ships, as `(mx, mz)`."""
    out = set()
    for entry in os.listdir(maps_dir):
        match = re.match(r"^m(\d+)_(\d+)\.", entry)
        if match:
            out.add((int(match.group(1)), int(match.group(2))))
    return out


# --------------------------------------------------------------- dataset side


Spawn = collections.namedtuple("Spawn", "kind name x z level count")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--content", required=True, help="unpacked content tree")
    parser.add_argument("--npc-json", required=True)
    parser.add_argument("--obj-json", required=True)
    parser.add_argument("--out", required=True, help="directory for the .spawn files")
    parser.add_argument("--report", help="where to write the drop report (default stdout)")
    parser.add_argument("--source", default="unspecified",
                        help="one line naming the dump, written into every file header")
    args = parser.parse_args()

    content = os.path.expanduser(args.content)
    configs = os.path.join(content, "configs")

    npc_ids = load_compack(os.path.join(configs, "all.npc.compack"))
    obj_ids = load_compack(os.path.join(configs, "all.obj.compack"))
    npc_blocks = load_blocks(os.path.join(configs, "all.npc"))
    squares = load_squares(os.path.join(content, "maps"))

    reject = collections.Counter()
    corrected = collections.Counter()
    drift = {}
    absent_square = collections.Counter()
    kept = collections.defaultdict(list)
    seen = set()

    names_cache = {}

    with open(os.path.expanduser(args.npc_json), encoding="utf-8") as handle:
        npc_rows = json.load(handle)
    with open(os.path.expanduser(args.obj_json), encoding="utf-8") as handle:
        obj_rows = json.load(handle)

    unnamed = 0
    for row in npc_rows:
        ident = row["id"]
        name = npc_ids.get(ident)
        if name is None:
            reject["npc: id past the end of this cache's npc table"] += 1
            continue
        if name not in names_cache:
            names_cache[name] = reachable_names(npc_blocks, name)
        candidates = names_cache[name]
        if not candidates:
            unnamed += 1
        else:
            claimed = normalise(row["name"])
            alias_targets = NPC_NAME_ALIASES.get((name, claimed), set())
        if candidates and claimed not in candidates and not (candidates & alias_targets):
            reject["npc: name drift"] += 1
            drift.setdefault(ident, [name, sorted(candidates), row["name"], 0])
            drift[ident][3] += 1
            continue
        level = row["level"]
        if not 0 <= level <= 3:
            reject["npc: level outside 0..3"] += 1
            continue
        square = (row["x"] // 64, row["y"] // 64)
        if square not in squares:
            reject["npc: map square not in this cache"] += 1
            absent_square["m%d_%d" % square] += 1
            continue
        key = ("npc", name, row["x"], row["y"], level)
        if (row["x"] // 64, row["y"] // 64) in INSTANCED_SQUARES:
            reject["npc: instanced encounter square"] += 1
            # Same reason as the per-tile case below: keep the empty file, so
            # it still states that the square has no public actors.
            kept[square]
            continue
        if (name, row["x"], row["y"], level) in NPC_SPAWN_EXCLUSIONS:
            reject["npc: scripted owner-private encounter actor"] += 1
            # Preserve an empty generated file when every row on a shipped map
            # square is intentionally excluded. Contract checks use the file
            # as the authoritative statement that the square has no public
            # encounter actors.
            kept[square]
            continue
        if key in seen:
            reject["npc: duplicate"] += 1
            continue
        seen.add(key)
        kept[square].append(Spawn("npc", name, row["x"], row["y"], level, None))

    for row in obj_rows:
        ident = row["id"]
        name = obj_ids.get(ident)
        if name is None:
            reject["obj: id past the end of this cache's obj table"] += 1
            continue
        correction = OBJ_SPAWN_ID_CORRECTIONS.get(
            (ident, row["x"], row["y"], row["plane"]))
        if correction is not None:
            assert correction in obj_ids.values(), correction
            corrected["%s -> %s @ (%d,%d,%d)" % (
                name, correction, row["x"], row["y"], row["plane"])] += 1
            name = correction
        level = row["plane"]
        if not 0 <= level <= 3:
            reject["obj: level outside 0..3"] += 1
            continue
        count = max(1, int(row.get("count", 1)))
        square = (row["x"] // 64, row["y"] // 64)
        if square not in squares:
            reject["obj: map square not in this cache"] += 1
            absent_square["m%d_%d" % square] += 1
            continue
        key = ("obj", name, row["x"], row["y"], level, count)
        if key in seen:
            reject["obj: duplicate"] += 1
            continue
        seen.add(key)
        kept[square].append(Spawn("obj", name, row["x"], row["y"], level, count))

    # ---------------------------------------------------------------- write

    out_dir = os.path.expanduser(args.out)
    os.makedirs(out_dir, exist_ok=True)
    for entry in os.listdir(out_dir):
        if entry.endswith(".spawn"):
            os.unlink(os.path.join(out_dir, entry))

    npc_total = 0
    obj_total = 0
    for square in sorted(kept):
        npcs = sorted((s for s in kept[square] if s.kind == "npc"),
                      key=lambda s: (s.level, s.x, s.z, s.name))
        objs = sorted((s for s in kept[square] if s.kind == "obj"),
                      key=lambda s: (s.level, s.x, s.z, s.name))
        npc_total += len(npcs)
        obj_total += len(objs)
        lines = [
            "// Map square m%d_%d --- %d npc, %d obj." % (square[0], square[1], len(npcs), len(objs)),
            "//",
            "// Generated by tools/gen_spawns.py; do not hand-edit, the next run",
            "// overwrites it. Source: %s" % args.source,
            "// Method, and how to redo this for another revision: docs/ITEM_AND_NPCS.md",
            "",
        ]
        if npcs:
            lines.append("==== NPC ====")
            for spawn in npcs:
                lines.append("%-44s %5d %5d %d" % (spawn.name, spawn.x, spawn.z, spawn.level))
            lines.append("")
        if objs:
            lines.append("==== OBJ ====")
            for spawn in objs:
                lines.append("%-44s %5d %5d %d %d"
                             % (spawn.name, spawn.x, spawn.z, spawn.level, spawn.count))
            lines.append("")
        path = os.path.join(out_dir, "m%d_%d.spawn" % square)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("\n".join(lines))

    # --------------------------------------------------------------- report

    out = open(os.path.expanduser(args.report), "w", encoding="utf-8") if args.report else sys.stdout
    print("source: %s" % args.source, file=out)
    print("read:    %d npc rows, %d obj rows" % (len(npc_rows), len(obj_rows)), file=out)
    print("written: %d npc, %d obj across %d map squares in %s"
          % (npc_total, obj_total, len(kept), args.out), file=out)
    print("", file=out)
    print("kept without a name check (record and every variant unnamed): %d npc" % unnamed, file=out)
    print("obj spawns carry no name in the dump, so NONE of them got a name check.", file=out)
    print("", file=out)
    print("audited obj id corrections (OBJ_SPAWN_ID_CORRECTIONS):", file=out)
    for note, count in sorted(corrected.items()):
        print("  %-64s %d" % (note, count), file=out)
    if not corrected:
        print("  (none applied)", file=out)
    print("", file=out)
    print("dropped:", file=out)
    for reason, count in reject.most_common():
        print("  %-52s %d" % (reason, count), file=out)
    print("", file=out)
    print("name drift --- the id names a different record in this cache than the dump saw.", file=out)
    print("Each line is a spawn this tree does NOT have as a result.", file=out)
    print("  %-6s %-6s %-44s %-40s %s" % ("id", "count", "this cache calls it", "and it presents as", "the dump said"),
          file=out)
    for ident, (name, candidates, claimed, count) in sorted(drift.items(), key=lambda kv: -kv[1][3]):
        print("  %-6d %-6d %-44s %-40s %s"
              % (ident, count, name, "/".join(candidates) or "(unnamed)", claimed), file=out)
    print("", file=out)
    print("map squares the dump has spawns on and this cache does not ship:", file=out)
    for square, count in absent_square.most_common():
        print("  %-10s %d" % (square, count), file=out)
    if out is not sys.stdout:
        out.close()
        print("report written to %s" % args.report)
    print("%d npc + %d obj spawns across %d squares" % (npc_total, obj_total, len(kept)))


if __name__ == "__main__":
    main()
