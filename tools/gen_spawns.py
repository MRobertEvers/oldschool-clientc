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
`src/torirsserver/torirs_server_content.c` reads. Files are rewritten wholesale: the
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
    # Terry Balando, The Dig Site's expert: the dump still says "Archaeological
    # expert"; the cache renamed the record and kept id 3639. Without this the
    # quest has no expert to hand the tablet to (spawn_report: name drift 3639).
    ("archaeological_expert", normalise("Archaeological expert")): {normalise("Terry Balando")},
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


# Rows the dump places on a tile this cache no longer has the room on, keyed
# `(cache symbol, x, z, plane)` -> `(x, z, plane)`. The relocation runs BEFORE
# every other rule, so the moved row is checked (square, exclusion, duplicate)
# at its corrected tile and a regeneration keeps it there.
#
# Hazel Cult, the Carnillean kitchen. LostCity (2004) builds the kitchen at
# maps/m40_151.jm2 local x 4..8, z 4..8 -- cookingutensils 391 at 4,4 and 4,7,
# bigtable2 595 at 4,5, stools 1102 at 5,5 / 5,6, carnilleanrange 2859 at 4,8
# (LOC lines 6914-6954) -- with Claus (npc 886) at 6,6 (NPC line 8660), the
# knife (946) at 4,5 and the bread (2309) at 4,6 (OBJ lines 8689-8690). That is
# 2566,9670 / 2564,9669 / 2564,9670, exactly the dump's tiles. The osrs239 cache
# moved the kitchen: m40_151 local 4..8,4..8 is now bare blocked rock (jm2
# `o42;0;0 f1`, no locs), and the same furniture stands in maps/m39_151.jl2 --
# cookingutensils at 42,31 / 42,34 (lines 116-117), bigtable2 at 42,32 (119),
# stools at 43,32 / 43,33 (149-150), carnilleanrange at 42,35 (537),
# carnillean_ladder_up at 48,30 (540) -- i.e. the LostCity room shifted by
# local (+38, +27), world (-26, +27). Each row keeps its place in the room:
# Claus 6,6 -> 44,33 = 2540,9697; the knife on the table 4,5 -> 42,32 =
# 2538,9696; the bread 4,6 -> 42,33 = 2538,9697 (jm2 lines 2723-2852: all three
# tiles are `o12;0;0` floor with no blocking flag).
NPC_SPAWN_RELOCATIONS = {
    ("claus_carnillean", 2566, 9670, 0): (2540, 9697, 0),
}


# Dump rows whose id this cache reallocated, keyed `(dump id, x, z, plane)` ->
# the cache symbol that stands there now. Keyed on the exact row like
# OBJ_SPAWN_ID_CORRECTIONS, so no other spawn of the old id is admitted, and the
# corrected symbol must still carry the dump's display name (rule 2 runs on it).
#
# Turael, Burthorpe's slayer master: the dump has `401 Turael` at 2931,3536;
# in this cache 401 is `tog_light_creature`, so the row was dropped as name
# drift and Burthorpe had no Turael -- Animal Magnetism's blessed axe (Quest
# Helper AnimalMagnetism.java:228 talkToTurael, WorldPoint(2931, 3536, 0);
# steps 160/170) was unreachable, and so was every slayer assignment from him.
# The cache's Turael is `slayer_master_1_tureal` (13618, name=Turael), the
# symbol skill_slayer/scripts/slayer_masters.rs2 and anma.rs2 bind.
NPC_SPAWN_ID_CORRECTIONS = {
    (401, 2931, 3536, 0): "slayer_master_1_tureal",
}
#
# Getting Ahead, Gordon and Mary's farmhouse upstairs: the dump lays the empty
# pot (the one the flour barrel fills) on 1239,3682,1, and maps/m19_57.jl2 line
# 397 (`1 23 34: 3025 11 2`) stands `fai_varrock_sack_pile` -- a blocking
# centrepiece -- on that very tile, so Take answers "I can't reach that!". The
# next tile north-west of the pile, 1239,3681,1 (local 23,33), carries no loc.
# Quest Helper GettingAhead.java `takePot` sends the player to this pot upstairs
# (zone 1238..1244, 3677..3687, plane 1); the wiki's Gordon and Mary's farm
# (oldid 15229525) names the upstairs flour source.
OBJ_SPAWN_RELOCATIONS = {
    ("knife", 2564, 9669, 0): (2538, 9696, 0),
    ("bread", 2564, 9670, 0): (2538, 9697, 0),
    ("pot_empty", 1239, 3682, 1): (1239, 3681, 1),
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
#
# Death Plateau, the five mechanism balls in the Burthorpe barracks: the dump
# lays five `death_cannonball_green` (3113) on 2893,3561..3565. LostCity's
# maps/m45_55.jm2 OBJ section (`0 13 41..45`) lays one of each colour --
# 3111 yellow, 3113 green, 3112 purple, 3110 blue, 3109 red, in that order
# (LostCity pack/obj.pack) -- and quest_death's mechanism needs all five
# colours (OSRS-Content b6aef218bc hand-edited m45_55.spawn to this; folded
# in here so a regeneration keeps it).
OBJ_SPAWN_ID_CORRECTIONS = {
    (2958, 3443, 9741, 1): "druid_pouch_empty",
    (3113, 2893, 3561, 0): "death_cannonball_yellow",
    (3113, 2893, 3563, 0): "death_cannonball_purple",
    (3113, 2893, 3564, 0): "death_cannonball_blue",
    (3113, 2893, 3565, 0): "death_cannonball_red",
    # Biohazard, the pigeon cages behind Jerico's house: the dump lays three
    # EMPTY cages (425 pigeoncage) at 2618,3323..3325; LostCity maps/m40_51.jm2
    # OBJ `0 58 59..61: 424` lays three cages WITH pigeons (424 pigeons), which
    # the quest has you take (OSRS-Content 4420b02611 hand-edited this).
    (425, 2618, 3323, 0): "pigeons",
    (425, 2618, 3324, 0): "pigeons",
    (425, 2618, 3325, 0): "pigeons",
    # Murder Mystery, the dagger by the Sinclair mansion: the dump lays the
    # dusted copy (1814 murderweapondust); LostCity maps/m42_55.jm2 OBJ
    # `0 58 58: 1813` lays the undusted `murderweapon`, which the player dusts
    # for prints (OSRS-Content 0ee0ee5e9d hand-edited this).
    (1814, 2746, 3578, 0): "murderweapon",
}


# Obj rows the dump carries that the real game does not lay on the ground in
# public. Keyed `(cache symbol, x, z, plane)` like NPC_SPAWN_EXCLUSIONS, so a
# regeneration keeps the tile empty and no other spawn of the id is touched.
#
# Recruitment Drive, Miss Cheevers' trial: the dump lays `rd_metal_spade_no_handle`
# (5587, the spade HEAD) on her table at 2473,4941. The table's spade is the
# whole metal spade (5586) and the head is what the Bunsen burner makes of it --
# https://oldschool.runescape.wiki/w/Metal_spade : "It is found on the table in
# Miss Cheevers's trial" (item 5586, respawn 10 ticks) and "It is used on a
# bunsen burner to remove the handle"; https://oldschool.runescape.wiki/w/Recruitment_Drive :
# "Take the metal spade, and use it on the Bunsen burner to remove the wood."
# A public head let a player skip that step. The quest places the table spade
# per player (`obj_add_private(0_38_77_41_13, rd_metal_spade, ...)` in
# quest_recruitmentdrive/scripts/recruitmentdrive_cheevers.rs2), so the tile
# needs no public row at all.
#
# Getting Ahead: the dump lays red AND yellow dye on one tile, 1240,3688 -- the
# tile of `ga_shelves`. In the game the dyes are not ground items: the player
# Searches the shelves and picks one ("Take some red dye." -- Quest Helper
# GettingAhead.java `takeDye`, an ObjectStep on GA_SHELVES at 1240,3688;
# https://oldschool.runescape.wiki/w/Shelves_(Getting_Ahead)?oldid=15202355).
# quest_gettingahead's [oploc,ga_shelves] is that search, so a public pile of
# both dyes beside it would only be a second, wrong source.
OBJ_SPAWN_EXCLUSIONS = {
    ("rd_metal_spade_no_handle", 2473, 4941, 0),
    ("reddye", 1240, 3688, 0),
    ("yellowdye", 1240, 3688, 0),
}


# Obj rows the dump lacks and the quest cannot be finished without, keyed like
# OBJ_SPAWN_EXCLUSIONS: `(cache symbol, x, z, plane, count)`.
#
# The Dig Site: the compound that blows the rockfall needs an arcenia root, and
# the only cave the player can reach before the rockfall is gone is m52_153
# (z 9792..9855); m52_152 (z 9728..9791, the four roots the dump does carry) is
# the cavern behind it. LostCity lays four roots in each --- maps/m52_153.jm2
# lines 8817-8829 (local 20,27 / 23,32 / 37,37 / 41,43, obj 708) against
# m52_152.jm2 lines 8831-8843 --- so the dump's single set leaves the quest
# uncompletable on a fresh save. These four are the m52_153 set, at the
# LostCity tiles minus (1,1): the osrs239 caves sit one tile north-west of
# LostCity's (the dump's own m52_152 roots are the LostCity rows minus (1,1),
# as the ladders 2352/2353 and the brick 2362 are).
OBJ_SPAWN_ADDITIONS = (
    ("arcenia_root", 3328 + 19, 9792 + 26, 0, 1),
    ("arcenia_root", 3328 + 22, 9792 + 31, 0, 1),
    ("arcenia_root", 3328 + 36, 9792 + 36, 0, 1),
    ("arcenia_root", 3328 + 40, 9792 + 42, 0, 1),
    # Getting Ahead: the knife the clay head is carved with and the bucket the
    # sink fills to soften the clay are both taken in the farmhouse, and the dump
    # carries neither. Quest Helper GettingAhead.java: `takeKnife` "Take the knife
    # near Mary." at WorldPoint(1241, 3679, 0); `takeBucket` "Take the bucket in
    # the house." at WorldPoint(1244, 3682, 0). docs/quests/getting_ahead.md
    # (section 5, local supply sources) records the missing knife.
    ("knife", 1241, 3679, 0, 1),
    ("bucket_empty", 1244, 3682, 0, 1),
)


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
    relocated = collections.Counter()
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
        npc_correction = NPC_SPAWN_ID_CORRECTIONS.get((ident, row["x"], row["y"], row["level"]))
        if npc_correction is not None:
            corrected["npc %d -> %s (%d,%d,%d)" % (ident, npc_correction, row["x"], row["y"], row["level"])] += 1
            name = npc_correction
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
        target = NPC_SPAWN_RELOCATIONS.get((name, row["x"], row["y"], level))
        if target is not None:
            relocated["npc %s (%d,%d,%d) -> (%d,%d,%d)" % (
                (name, row["x"], row["y"], level) + target)] += 1
            row = dict(row, x=target[0], y=target[1])
            level = target[2]
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
        target = OBJ_SPAWN_RELOCATIONS.get((name, row["x"], row["y"], level))
        if target is not None:
            relocated["obj %s (%d,%d,%d) -> (%d,%d,%d)" % (
                (name, row["x"], row["y"], level) + target)] += 1
            row = dict(row, x=target[0], y=target[1])
            level = target[2]
        if not 0 <= level <= 3:
            reject["obj: level outside 0..3"] += 1
            continue
        count = max(1, int(row.get("count", 1)))
        square = (row["x"] // 64, row["y"] // 64)
        if (name, row["x"], row["y"], level) in OBJ_SPAWN_EXCLUSIONS:
            reject["obj: audited map-dump artefact (OBJ_SPAWN_EXCLUSIONS)"] += 1
            continue
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

    for name, x, z, level, count in OBJ_SPAWN_ADDITIONS:
        assert name in obj_ids.values(), name
        square = (x // 64, z // 64)
        assert square in squares, square
        key = ("obj", name, x, z, level, count)
        assert key not in seen, key
        seen.add(key)
        kept[square].append(Spawn("obj", name, x, z, level, count))

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
    print("audited id corrections (NPC_SPAWN_ID_CORRECTIONS, OBJ_SPAWN_ID_CORRECTIONS):", file=out)
    for note, count in sorted(corrected.items()):
        print("  %-64s %d" % (note, count), file=out)
    if not corrected:
        print("  (none applied)", file=out)
    print("", file=out)
    print("audited relocations (NPC_SPAWN_RELOCATIONS / OBJ_SPAWN_RELOCATIONS):", file=out)
    for note, count in sorted(relocated.items()):
        print("  %-64s %d" % (note, count), file=out)
    unused = (len(NPC_SPAWN_RELOCATIONS) + len(OBJ_SPAWN_RELOCATIONS)) - len(relocated)
    if unused:
        print("  %d relocation(s) matched no dump row -- re-audit them" % unused, file=out)
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
