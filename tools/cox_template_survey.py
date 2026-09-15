#!/usr/bin/env python3
"""Survey the Chambers of Xeric room templates in the osrs239 cache.

The raid assembles its floors out of prefabricated rooms that live in a block of
template map squares. Nothing in the cache says how big a room is, how the rooms
are packed inside those squares, or which square holds which room — the raid's
own layout code knew, and that code is not in the cache. This tool measures it,
so `docs/minigames/cox/COX_PLAN.md` task R2 rests on the map data rather than a
guess.

The measurement has three legs:

1. **Which squares are templates.** Any square in the block whose `.jl2` places
   a `raids_*` loc.
2. **The room pitch**, by translational self-similarity. Two approaches were
   tried and rejected before this one, and both failures are informative:

   - *Wall-loc gaps.* `raids_wall_*` looked like it would trace room boundaries.
     It does not — wall scenery blankets every column of every square (occupancy
     48-67 out of 64 rows in `m51_83`), so no gap exists to measure.
   - *Position mod P.* Assumes each copy of a room places its markers at the
     same offset. Also false: two ice demon rooms in `m51_83` have their four
     braziers in visibly different arrangements, so the score is flat across
     every candidate P (0.18-0.27 for P from 8 to 64 — no signal at all).

   What does work: a room grid means the *scenery* repeats. For each plane, take
   every offset between two placements of the same loc as a candidate
   translation, then score each candidate by how many placements it maps onto
   another placement of the same loc. The real pitch wins by a wide margin. It
   does not score 100% because neighbouring cells hold *different* rooms — only
   the shared structural scenery translates — which is itself the confirmation
   that the grid is real.
3. **Which cells are real rooms**, by loc density per 32x32 cell. A cell that is
   void carries no locs; a built room carries hundreds. This is the leg the
   layout code actually consumes — it needs to know which template cells it may
   stamp, not what Jagex called them.

4. **Which room is where**, where the cache says so. Marker locs identify a room
   unambiguously — a brazier means ice demon, a laser crystal means jewelled
   crabs, a food trough means thieving.

   This leg only covers the **puzzle** rooms. The combat rooms (Tekton, Vasa,
   Vespula, Vanguards, shamans, mystics, guardians, muttadiles) carry no
   identifying locs at all: their cells hold nothing but terrain scenery
   (`raids_floor_*`, `raids_rocks*`, `raids_plant*`), because a combat room is
   defined by the NPCs the raid spawns into it, and those spawns lived in the
   raid's own code rather than in any cache table. That is not a gap to close —
   we write the layout code, so we choose those cells. Only the puzzle rooms
   must be taken as the cache laid them out.

Output is a JSON manifest under `tools/data/`, in the same spirit as the
Gauntlet's door-mask manifest: the layout code reads the manifest, never the
maps.
"""

import collections
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CONTENT = os.path.join(HERE, "..", "OSRS-Content", "osrs239-content")

# The block of map squares that holds the raid, measured with `--sweep` (which
# reports every square in the cache placing a `raids_*` loc, not just this
# block). The bulk sits at mz 80-85; m50_89/m51_89/m52_89 are a second cluster.
# Everything outside this block that --sweep reports is single digits — the
# surface entrance on m19_55 and stray reuse of raid scenery elsewhere.
BLOCK_X = range(49, 54)
BLOCK_Z = range(78, 93)

# A loc that can only mean one room. The value is the room key used by the
# server content. Keep this table honest: a marker that appears in two room
# types is not a marker, and belongs in AMBIGUOUS instead.
MARKERS = {
    "raids_icedemon_brazier_unlit": "ice_demon",
    "raids_icedemon_brazier_lit": "ice_demon",
    "raids_lasercrabs_bigcrystal_1": "crabs",
    "raids_lasercrabs_bigcrystal_2": "crabs",
    "raids_lasercrabs_bigcrystal_3": "crabs",
    "raids_lasercrabs_bigcrystal_4": "crabs",
    "raids_lasercrabs_bigcrystal_5": "crabs",
    "raids_lasercrabs_smallcrystal_black": "crabs",
    "raids_lasercrabs_smallcrystal_cyan": "crabs",
    "raids_lasercrabs_smallcrystal_magenta": "crabs",
    "raids_lasercrabs_smallcrystal_yellow": "crabs",
    "raids_lasercrabs_smallcrystal_white": "crabs",
    "raids_lasercrabs_xeric_relief": "crabs",
    "raids_lasercrabs_hammer": "crabs",
    "raids_tightrope_barrier": "tightrope",
    "raids_tightrope_end": "tightrope",
    "raids_tightrope_keystone_loc": "tightrope",
    "raids_thievingchest_closed": "thieving",
    "raids_thievingchest_open": "thieving",
    "raids_thievingchest_eggs_whole": "thieving",
    "raids_thievingchest_eggs_hatched": "thieving",
    "raids_thievingchest_foodtrough_empty": "thieving",
    "raids_vasanistirio_crystal_on": "vasa",
    "raids_vasanistirio_crystal_off": "vasa",
    "raids_vasanistirio_book_vis": "vasa",
    "raids_vasanistirio_book": "vasa",
    "raids_vasanistirio_fire": "vasa",
    "raids_tekton_book_vis": "tekton",
    "raids_vespula_book_vis": "vespula",
    "raids_vanguard_book_vis": "vanguard",
    "raids_houndmaster_book_vis": "muttadile",
    "raids_dogodile_tendrils_entrance": "muttadile",
    "raids_shaman_tendrils_entrance": "shaman",
    "raids_skeletalmystics_symbol": "mystics",
    # Room furniture. A combat room places no npcs in the map data, but it does
    # place its own props, and those are as diagnostic as a puzzle marker.
    "raids_tekton_anvil": "tekton",
    "raids_tekton_fire": "tekton",
    "raids_tekton_book": "tekton",
    "raids_tekton_walkblocker": "tekton",
    "raids_meat_tree_full": "muttadile",
    "raids_meat_tree_empty": "muttadile",
    "raids_houndmaster_book": "muttadile",
    "raids_vespula_herb": "vespula",
    "raids_vespula_herb_empty": "vespula",
    "raids_vespula_boil_blocking": "vespula",
    "raids_vespula_boil_burst": "vespula",
    "raids_vespula_portal": "vespula",
    "raids_vespula_portal_closed": "vespula",
    "raids_vespula_tendrils_entrance": "vespula",
    "raids_vespula_book": "vespula",
    "raids_vanguard_book": "vanguard",
    "raids_stoneguardians_pickaxe": "guardians",
    "raids_mystics_portal_start": "mystics",
    "raids_mystics_portal_middle": "mystics",
    "raids_mystics_portal_end": "mystics",
    "raids_icedemon_snow": "ice_demon",
    "raids_icedemon_axe": "ice_demon",
    "raids_icedemon_tinderbox": "ice_demon",
    "raids_thievingchest_foodtrough_full": "thieving",
    "raids_thievingchest_creaturekeeper": "thieving",
    "raids_energy_pool": "resource",
    "raids_energy_pool_glow": "resource",
    "raids_storage_4": "resource",
    "raids_bank_chest_lobby": "lobby",
    "raids_bank_chest_lobby_working": "lobby",
    "raids_storage_lobby": "lobby",
    "raids_challenge_scores": "lobby",
    "raids_olm_barrier": "olm",
    "raids_bossexit": "olm",
    "raids_reward_chest": "olm",
    "raids_reward_crystal": "olm",
    "raids_reward_lootbeam_basic": "olm",
    "raids_reward_lootbeam_special": "olm",
    "raids_reward_lootbeam_kit": "olm",
    "raids_reward_lootbeam_dust": "olm",
    "raids_patch_empty": "resource",
    "raids_farming_tools": "resource",
    "raids_gourd_tree": "resource",
    "raids_storage_hotspot": "resource",
    "raids_entrance_steps": "lobby",
    "raids_exit_steps": "lobby",
    "raids_party_recruitment": "lobby",
    "raids_bossentrance": "olm",
    "raids_reward_lootbeam": "olm",
}

# Structural locs that appear in every room and so identify nothing.
WALL_PREFIX = "raids_wall_"
STRUCTURAL = ("raids_corridor_", "raids_descentto", WALL_PREFIX)


def read_names(path):
    """id -> name from an `all.<type>.compack` index."""
    names = {}
    with open(path, encoding="utf-8", errors="ignore") as handle:
        for line in handle:
            line = line.strip()
            if not line or "=" not in line:
                continue
            key, _, value = line.partition("=")
            try:
                names[int(key)] = value
            except ValueError:
                continue
    return names


def read_jl2(path):
    """Yield (plane, local_x, local_z, loc_id) for one map square."""
    with open(path, encoding="utf-8", errors="ignore") as handle:
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
            yield plane, local_x, local_z, loc_id


def square_path(maps_dir, map_x, map_z):
    return os.path.join(maps_dir, "m%d_%d.jl2" % (map_x, map_z))


def sweep(maps_dir, names):
    """Every map square that places a `raids_*` loc, with a count. Use this to
    re-derive BLOCK_X/BLOCK_Z if the cache revision changes."""
    hits = collections.Counter()
    for name in sorted(os.listdir(maps_dir)):
        match = re.match(r"^m(-?\d+)_(-?\d+)\.jl2$", name)
        if not match:
            continue
        map_x, map_z = int(match.group(1)), int(match.group(2))
        for _plane, _lx, _lz, loc_id in read_jl2(os.path.join(maps_dir, name)):
            if names.get(loc_id, "").startswith("raids_"):
                hits[(map_x, map_z)] += 1
    return hits


def plane_placements(path, names):
    """plane -> {(name, x, z)} for every non-wall `raids_*` loc in one square.

    Wall scenery is excluded: it blankets the square uniformly and so translates
    onto itself under *any* offset, which would swamp the pitch score.
    """
    out = collections.defaultdict(set)
    for plane, local_x, local_z, loc_id in read_jl2(path):
        name = names.get(loc_id, "")
        if not name.startswith("raids_") or name.startswith(WALL_PREFIX):
            continue
        out[plane].add((name, local_x, local_z))
    return out


def best_translations(points, limit=60, min_points=20):
    """Score candidate translations by how much of `points` maps onto itself.

    Returns [(hits, (dx, dz)), ...] best first. Candidates come from the offsets
    between two placements of the *same* loc, which is where a repeated room
    grid leaves its fingerprint.
    """
    if len(points) < min_points:
        return []
    by_name = collections.defaultdict(list)
    for name, x, z in points:
        by_name[name].append((x, z))
    candidates = collections.Counter()
    for spots in by_name.values():
        for first in spots:
            for second in spots:
                if first == second:
                    continue
                offset = (second[0] - first[0], second[1] - first[1])
                # Canonicalise to one of each +/- pair.
                if offset[0] > 0 or (offset[0] == 0 and offset[1] > 0):
                    candidates[offset] += 1
    scored = []
    for offset, _votes in candidates.most_common(limit):
        hits = sum(1 for n, x, z in points if (n, x + offset[0], z + offset[1]) in points)
        scored.append((hits, offset))
    scored.sort(reverse=True)
    return scored


def measure_pitch(maps_dir, names):
    """The room grid pitch, in tiles, plus the evidence for it."""
    evidence = []
    axis_votes = collections.Counter()
    for map_x in BLOCK_X:
        for map_z in BLOCK_Z:
            path = square_path(maps_dir, map_x, map_z)
            if not os.path.exists(path):
                continue
            for plane, points in sorted(plane_placements(path, names).items()):
                scored = best_translations(points)
                if not scored:
                    continue
                hits, offset = scored[0]
                share = hits / len(points)
                # A translation of one or two tiles is dense scenery repeating,
                # not a room grid. Require a real stride.
                if share < 0.25 or max(abs(offset[0]), abs(offset[1])) < 8:
                    continue
                evidence.append({
                    "square": "m%d_%d" % (map_x, map_z),
                    "plane": plane,
                    "points": len(points),
                    "translation": list(offset),
                    "mapped": hits,
                    "share": round(share, 3),
                })
                axis_votes[max(abs(offset[0]), abs(offset[1]))] += 1
    pitch = axis_votes.most_common(1)[0][0] if axis_votes else 0
    return pitch, evidence, axis_votes


# A cell holding fewer locs than this is void, not a room. Real rooms carry
# hundreds; the threshold sits in the empty middle of the observed histogram.
CELL_OCCUPIED_MIN = 40


def survey(maps_dir, names, pitch):
    """Per-square: planes used, per-cell occupancy, and identified rooms."""
    out = {}
    for map_x in BLOCK_X:
        for map_z in BLOCK_Z:
            path = square_path(maps_dir, map_x, map_z)
            if not os.path.exists(path):
                continue
            planes = set()
            density = collections.Counter()
            votes = collections.defaultdict(collections.Counter)
            markers = collections.defaultdict(list)
            total = 0
            for plane, local_x, local_z, loc_id in read_jl2(path):
                name = names.get(loc_id)
                if not name or not name.startswith("raids_"):
                    continue
                total += 1
                planes.add(plane)
                cell = (plane, local_x // pitch, local_z // pitch)
                density[cell] += 1
                if name.startswith(STRUCTURAL):
                    continue
                room = MARKERS.get(name)
                if room is None:
                    continue
                votes[cell][room] += 1
                markers[cell].append(name)
            if total == 0:
                continue
            cells = {}
            for cell, count in sorted(density.items()):
                if count < CELL_OCCUPIED_MIN:
                    continue
                entry = {"locs": count, "room": None}
                if cell in votes:
                    winner, hits = votes[cell].most_common(1)[0]
                    entry["room"] = winner
                    entry["votes"] = dict(votes[cell])
                    entry["contested"] = len(votes[cell]) > 1
                    entry["markers"] = sorted(set(markers[cell]))
                cells["%d/%d/%d" % cell] = entry
            out["m%d_%d" % (map_x, map_z)] = {
                "map_x": map_x,
                "map_z": map_z,
                "raids_loc_count": total,
                "planes": sorted(planes),
                "occupied_cells": len(cells),
                "identified_cells": sum(1 for c in cells.values() if c["room"]),
                "cells": cells,
            }
    return out


def main():
    maps_dir = os.path.join(CONTENT, "maps")
    names = read_names(os.path.join(CONTENT, "configs", "all.loc.compack"))

    if "--find" in sys.argv:
        # Where, exactly, is a named loc placed inside its template cell?
        #
        # The survey above answers "which cell holds a Tekton anvil". This
        # answers "at which tile", which is the number an encounter script needs
        # and the only way to check a coordinate decoded from another server
        # against this cache. A position ported from Zenyte that this tool
        # cannot corroborate is a position to distrust: their map is a different
        # revision of the same rooms, and the rooms have been edited since.
        wanted = sys.argv[sys.argv.index("--find") + 1]
        pitch = 32
        found = 0
        for name in sorted(os.listdir(maps_dir)):
            match = re.match(r"^m(-?\d+)_(-?\d+)\.jl2$", name)
            if not match:
                continue
            map_x, map_z = int(match.group(1)), int(match.group(2))
            if map_x not in BLOCK_X or map_z not in BLOCK_Z:
                continue
            for plane, lx, lz, loc_id in read_jl2(os.path.join(maps_dir, name)):
                loc_name = names.get(loc_id, "")
                if wanted not in loc_name:
                    continue
                found += 1
                print("m%d_%d cell %d/%d plane %d  room-local (%2d,%2d)  %s"
                      % (map_x, map_z, lx // pitch, lz // pitch, plane,
                         lx % pitch, lz % pitch, loc_name))
        if not found:
            print("no loc matching %r in the template block" % wanted)
            return 1
        return 0

    if "--sweep" in sys.argv:
        hits = sweep(maps_dir, names)
        print("map squares placing raids_* locs:")
        for (map_x, map_z), count in sorted(hits.items()):
            print("  m%d_%d: %d" % (map_x, map_z, count))
        return 0

    pitch, evidence, axis_votes = measure_pitch(maps_dir, names)
    assert pitch > 0, "no room grid found - check BLOCK_X/BLOCK_Z against --sweep"
    assert 64 % pitch == 0, "pitch %d does not divide a 64-tile map square" % pitch

    manifest = {
        "note": "Generated by tools/cox_template_survey.py. See docs/minigames/cox/COX_PLAN.md task R2.",
        "cache": "osrs239",
        "pitch": {
            "tiles": pitch,
            "zones": pitch // 8,
            "rooms_per_square_axis": 64 // pitch,
            "stride_votes": dict(axis_votes.most_common()),
            "evidence": evidence,
        },
        "squares": survey(maps_dir, names, pitch),
    }

    out_dir = os.path.join(HERE, "data")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "cox_templates.json")
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True)
        handle.write("\n")

    print("room pitch: %d tiles (%d zones), %d rooms per square axis"
          % (pitch, pitch // 8, 64 // pitch))
    print("  stride votes: %s" % dict(axis_votes.most_common()))
    for row in evidence:
        print("  %-8s plane%d  n=%-5d translation %s maps %d (%.0f%%)"
              % (row["square"], row["plane"], row["points"], tuple(row["translation"]),
                 row["mapped"], 100 * row["share"]))
    print()
    total_cells = 0
    for key in sorted(manifest["squares"]):
        entry = manifest["squares"][key]
        total_cells += entry["occupied_cells"]
        rooms = sorted({c["room"] for c in entry["cells"].values() if c["room"]})
        print("%-8s planes=%-8s locs=%-6d cells=%-3d named=%-2d %s"
              % (key, ",".join(str(p) for p in entry["planes"]), entry["raids_loc_count"],
                 entry["occupied_cells"], entry["identified_cells"], ", ".join(rooms)))
    print("\n%d occupied room cells across %d squares"
          % (total_cells, len(manifest["squares"])))
    print("\nwrote %s" % os.path.relpath(out_path, os.path.join(HERE, "..")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
