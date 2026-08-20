#!/usr/bin/env python3
"""
Decide which npcs stand still, from what other servers independently state.

    tools/gen_npc_movement.py --validate     measure the layers, write nothing
    tools/gen_npc_movement.py --write        write the ledger + the config

## The problem

`torirs_server_content.c` gives every npc `wanderrange = 5` by default, which is the
reference's `NpcType` default and is right: an npc nothing describes should
drift around its spawn rather than stand frozen. But a large minority of the
world is *authored* to stand still — a banker behind a booth, a bench, a cat in
a box, Count Check by the Lumbridge graveyard — and none of that is in the
cache. Where an npc stands is server state, and so is whether it stays there.

So this tree has 23,084 world spawns and, before this ran, 22 files stating a
wanderrange. Everything else wandered, including the scenery.

## Why the answer has to be crowdsourced

No cache field says it. The wiki does not record it. It is a per-npc decision
someone made once at Jagex, and the only public record of it is the set of open
servers that have re-derived it, npc by npc, from watching the live game. This
reads five of them and prefers the one closest to this cache's revision.

Layers, highest confidence first. Each answers only where the ones above it are
silent, and every ledger row records which one answered.

  m1  **rsmod** (revision 231), `wanderRange = 0` / `moveRestrict = nomove` in
      its Kotlin `NpcEditor` blocks. The strongest source and the smallest: it
      is two revisions off ours and it names npcs by the *same gamevals this
      tree uses*, so there is no join to get wrong. It has only covered the
      areas it has implemented.
  m0  **rs-map-viewer's spawn dump** (revision 239) — the dump `tools/
      gen_spawns.py` already built this tree's world roster from, which carries
      `wanderRadius` on 741 of its rows and which that import drops on the
      floor. Same revision, and joined by *tile*, so it cannot be wrong about
      which npc it means. Asymmetric: the field is only ever present as 0, so
      this layer can say "stationary" and can never say "wanders".
  m2  **Zenyte** (OSRS, revision ~190), `data/npcs/spawns.json` `radius`.
  m3  **2009scape** (revision 530), `npc_spawns.json` — the fourth field of each
      `loc_data` tuple is the flag its `NPCSpawner.kt` reads into `isWalks`.
  m4  **LostCity** (revision 254 content, 2004 game), `moverestrict=nomove` /
      `wanderrange=0` in its `.npc` files. Oldest, and the only one joined by
      npc *name* rather than by tile — but it is also the only hand-authored,
      hand-reviewed one of the five.

m2 and m3 are keyed by their own revision's npc ids, which have drifted from
ours, so neither is read by id. Both are joined **by absolute tile** against
this tree's own spawn roster: if some other server puts an npc on the exact tile
this cache's roster puts one on, it is that npc, whatever either side calls it.
That join is deliberately lossy — it answers 1,735 npcs out of Zenyte's 16,839
spawn rows — and lossy in the safe direction.

## The second decision: `moverestrict`

Standing still is only half of it. rsmod's Lumbridge sets `moveRestrict =
indoors` on the shop keeper, the cook and Father Aereck: they *do* wander, and a
roof is what stops them leaving the building. The engine has carried that field
the whole time — `npc_collision_type` maps it to `COLL_TYPE_INDOORS` and
`collision_map.c` tests `COLL_FLAG_ROOF`, stamped from each tile's
`REMOVE_ROOF` setting — and one npc in the whole tree stated it.

Unlike `wanderrange`, **this cache can check the answer**. `maps/m<mx>_<mz>.jm2`
carries every tile's raw settings byte as `f<N>`, and bit 2 is the flag the
client hides roofs over, which is the same bit the reference stamps `ROOF` from.
So every layer below is put through a **roof gate**: a source that calls an npc
`indoors` whose spawns are not roofed *in this cache* is dropped, and so is an
`outdoors` whose spawns are. Measured, the gate rejects nothing — 7 of 7 rsmod
rows, 118 of 118 LostCity rows and 8 of 8 `*_outdoors` gamevals pass it — which
is what earns the gate the right to be trusted the other way round, as a layer:

  r1  **rsmod** `moveRestrict = indoors` / `outdoors`, by gameval.
  r2  **LostCity** `moverestrict=indoors` / `outdoors`, by gameval.
  r3  **This cache**: every world spawn of the npc stands on a `REMOVE_ROOF`
      tile, and its wander square is *mixed* — some tiles roofed, some not, so
      the restriction has something to bite on. A fully-roofed neighbourhood (a
      cave) is left alone: a restriction that cannot change a step is not a
      statement about the npc.

r3 is the broad one and it is only admissible because of where it sits. Graded
closed-world against rsmod's Lumbridge — a complete, hand-authored area at
revision 231 — the roof gate misses none of rsmod's `indoors` npcs and adds
seven; **every one of those seven is an npc rsmod separately pins with
`wanderRange = 0` or `1`**, which this tool decides first. Deciding
`wanderrange` before `moverestrict`, and skipping the field for anything already
stationary, is what collapses the disagreement to nothing.

`outdoors` gets no derived layer. "Not roofed" describes most of the world and
is not evidence that an npc is forbidden a doorway, so it is emitted only where
r1 or r2 states it.

## The ledger and the config

`npc_movement/<shard>/<gameval>.move` is the decision; `server/scripts/npc/
configs/npc_movement.generated.npc` is compiled from it. A ledger exists for
every npc *any* source describes, including the ones the sources call mobile,
because "four servers agree this one wanders" is a finding worth keeping — and
because it is what makes a later disagreement visible as a change rather than
as a first opinion. Npcs no source has ever mentioned get no file.

Hand-editing a ledger and setting `source = authored` freezes it: later runs
read it back and never overwrite it.

The config states `wanderrange=0` and nothing else. `torirs_server_content.c`'s npc
loader seeds a def once and then applies every later block to the same record,
so this file overlays the npc's existing blocks rather than competing with them
— no restating, and no chain of generators rewriting each other's output. What
it must not do is land on top of a *deliberate* authored wanderrange, so an npc
whose hand-written block already states any movement field is skipped outright
and named in the report.
"""

import argparse
import collections
import json
import os
import re
import sys

# Layer order is the whole decision procedure: the first one that answers wins.
# Ordered by distance from this cache's revision, except that m0 outranks
# everything below it because it is joined by tile against the very dump this
# tree's roster was built from.
LAYERS = ("m1", "m0", "m2", "m3", "m4")

LAYER_WHAT = {
    "m1": "rsmod rev-231 NpcEditor",
    "m0": "rs-map-viewer rev-239 spawn dump",
    "m2": "Zenyte spawns.json radius",
    "m3": "2009scape npc_spawns isWalks",
    "m4": "LostCity 2004 .npc",
}

# The `moverestrict` layers, same rule: the first one that answers wins.
RESTRICT_LAYERS = ("r1", "r2", "r3")

RESTRICT_WHAT = {
    "r1": "rsmod rev-231 moveRestrict",
    "r2": "LostCity 2004 moverestrict",
    "r3": "this cache: spawned under a roof",
}

# `settings & 4` in a `.jm2` MAP row is REMOVE_ROOF -- the tiles the client hides
# roofs over, which is the bit the reference stamps ROOF from and the one
# `torirs_server_scene.c` reads into COLL_FLAG_ROOF. Named rather than inlined because
# the same byte carries BLOCK 0x1, LINK_BELOW 0x2 and VIS_BELOW 0x8, and the
# wrong bit here would be a plausible-looking half-right answer.
FLOFLAG_REMOVE_ROOF = 4

# How far around its spawn r3 looks to decide whether a roof can bite. The
# engine's own default wander radius, so the question asked is exactly "can this
# npc's wander take it out from under the roof" rather than a rounder number.
WANDER_PROBE = 5

# Fields whose presence in a hand-written block means a person has already
# decided how this npc moves. Landing `wanderrange=0` on top of one of these
# would silently overrule a checked port.
MOVEMENT_KEYS = ("wanderrange", "moverestrict", "nomove", "defaultmode",
                 "maxrange", "patrol1")

CONFIG_OUT = os.path.join("server", "scripts", "npc", "configs",
                          "npc_movement.generated.npc")
LEDGER_DIR = "npc_movement"


# ---------------------------------------------------------------- this tree


def parse_config(path):
    """A `.npc`-grammar file as {block: {key: value}}, first value per key."""
    blocks = {}
    current = None
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            if line.startswith("//"):
                continue
            if line.startswith("[") and line.endswith("]"):
                current = line[1:-1]
                blocks.setdefault(current, {})
            elif current and "=" in line:
                key, value = line.split("=", 1)
                blocks[current].setdefault(key.strip(), value.strip())
    return blocks


def load_compack(path):
    """`configs/all.npc.compack` — `id=name`, the id authority."""
    out = {}
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("//") or "=" not in line:
                continue
            npc_id, name = line.split("=", 1)
            try:
                out[name.strip()] = int(npc_id)
            except ValueError:
                pass
    return out


def load_world_spawns(content_dir):
    """This tree's own roster: [(gameval, x, z, level)] over every map square."""
    root = os.path.join(content_dir, "server", "scripts", "areas", "world",
                        "configs")
    spawns = []
    for filename in sorted(os.listdir(root)):
        if not filename.endswith(".spawn"):
            continue
        section = None
        with open(os.path.join(root, filename), encoding="latin-1") as f:
            for line in f:
                line = line.strip()
                if line.startswith("===="):
                    section = "npc" if "NPC" in line else "obj"
                    continue
                if not line or line.startswith("//") or section != "npc":
                    continue
                parts = line.split()
                if len(parts) >= 4:
                    spawns.append((parts[0], int(parts[1]), int(parts[2]),
                                   int(parts[3])))
    return spawns


def load_authored_movement(content_dir, exclude_path):
    """Npcs whose hand-written block already states how they move.

    A *generated* file is not a hand-written override even though it lives under
    `server/scripts` and ends in `.npc`. Every file this tooling writes says so
    in its own opening lines; skip those by content rather than by a hardcoded
    path, so a future sibling generator does not have to be listed here.
    """
    authored = {}
    root = os.path.join(content_dir, "server", "scripts")
    exclude = os.path.abspath(exclude_path)
    for dirpath, _dirs, files in os.walk(root):
        for filename in files:
            if not filename.endswith(".npc"):
                continue
            full = os.path.join(dirpath, filename)
            if os.path.abspath(full) == exclude:
                continue
            with open(full, encoding="latin-1") as f:
                head = "".join(next(f, "") for _ in range(6))
            if "Generated by" in head or "COMPILED, not authored" in head:
                continue
            where = os.path.relpath(full, content_dir)
            for block, keys in parse_config(full).items():
                if block == "default":
                    continue
                stated = [k for k in MOVEMENT_KEYS if k in keys]
                if stated:
                    authored.setdefault(block, (where, stated))
    return authored


# ------------------------------------------------------------- the sources


FIND_RE = re.compile(r"""val\s+(\w+)\s*=\s*find\(\s*["'](\w+)["']""")
EDIT_RE = re.compile(r"edit\(\s*[\w.]*\.(\w+)\s*\)\s*\{")


def rsmod_edits(root):
    """Every `edit(<npc>) { ... }` body in rsmod's Kotlin, keyed by gameval.

    Its `NpcEditor` objects bind a Kotlin field to a gameval once
    (`val count_check = find("count_check", <hash>)`) and then edit through the
    field, so the two halves are read together and the alias is resolved locally
    per file.

    The body is taken by **matching braces**, not by a regex ending at the first
    newline-plus-`}`. A one-line `edit(x) { moveRestrict = indoors }` is how
    rsmod spells every edit that sets a single field, and a line-anchored
    pattern reads none of them: it found 13 npcs where the tree has 24, and
    silently lost the whole `indoors` half of the source it exists to read.
    """
    out = {}
    if not os.path.isdir(root):
        return out
    for dirpath, _dirs, files in os.walk(root):
        for filename in files:
            if not filename.endswith(".kt"):
                continue
            try:
                with open(os.path.join(dirpath, filename), encoding="utf-8") as f:
                    text = f.read()
            except OSError:
                continue
            if "wanderRange" not in text and "moveRestrict" not in text:
                continue
            alias = dict(FIND_RE.findall(text))
            for match in EDIT_RE.finditer(text):
                gameval = alias.get(match.group(1))
                if not gameval:
                    continue
                i = match.end()
                depth = 1
                while i < len(text) and depth:
                    if text[i] == "{":
                        depth += 1
                    elif text[i] == "}":
                        depth -= 1
                    i += 1
                out[gameval] = text[match.end():i - 1]
    return out


def layer_m1_rsmod(edits, known):
    """rsmod's `edit(<npc>) { wanderRange = 0 }`, keyed by gameval."""
    if not edits:
        return {}, "not found"
    out = {}
    for gameval, body in edits.items():
        if gameval not in known:
            continue
        if (re.search(r"wanderRange\s*=\s*0\b", body)
                or re.search(r"moveRestrict\s*=\s*nomove\b", body)):
            out[gameval] = 0
        elif re.search(r"wanderRange\s*=\s*[1-9]", body) or "= patrol" in body:
            out[gameval] = 1
    return out, None

def layer_m0_dump(path, tile_index):
    """rs-map-viewer's own `wanderRadius`, joined by tile.

    Stationary-only by construction: the field appears in the dump exclusively
    as 0, so its absence is silence rather than "this one wanders".
    """
    if not os.path.exists(path):
        return {}, "not found"
    out = collections.defaultdict(list)
    with open(path, encoding="utf-8") as f:
        rows = json.load(f)
    for row in rows:
        radius = row.get("wanderRadius")
        if radius is None:
            continue
        for gameval in tile_index.get((row["x"], row["y"], row["level"]), ()):
            out[gameval].append(radius)
    return {g: (0 if max(v) == 0 else 1) for g, v in out.items()}, None


def layer_m2_zenyte(path, tile_index):
    """Zenyte's per-spawn `radius`, joined by tile."""
    if not os.path.exists(path):
        return {}, "not found"
    out = collections.defaultdict(list)
    with open(path, encoding="utf-8") as f:
        rows = json.load(f)
    for row in rows:
        radius = row.get("radius")
        if radius is None:
            continue
        key = (row["x"], row["y"], row.get("z", 0))
        for gameval in tile_index.get(key, ()):
            out[gameval].append(radius)
    return {g: (0 if max(v) == 0 else 1) for g, v in out.items()}, None


def layer_m3_2009scape(path, tile_index):
    """2009scape's `isWalks`, joined by tile.

    `loc_data` is a `-`-separated list of `{x,y,z,walks,direction}` tuples, one
    per spawn of that npc id; `NPCSpawner.kt` reads field 3 as `isWalks`.
    """
    if not os.path.exists(path):
        return {}, "not found"
    out = collections.defaultdict(list)
    with open(path, encoding="utf-8") as f:
        rows = json.load(f)
    for row in rows:
        for tuple_text in str(row.get("loc_data", "")).split("-"):
            tuple_text = tuple_text.strip().strip("{}")
            if not tuple_text:
                continue
            fields = [t.strip() for t in tuple_text.split(",")]
            if len(fields) < 4:
                continue
            try:
                key = (int(fields[0]), int(fields[1]), int(fields[2]))
                walks = int(fields[3])
            except ValueError:
                continue
            for gameval in tile_index.get(key, ()):
                out[gameval].append(walks)
    return {g: (0 if max(v) == 0 else 1) for g, v in out.items()}, None


def layer_m4_lostcity(root, known):
    """LostCity's `moverestrict=nomove` / `wanderrange=0`, keyed by gameval."""
    if not os.path.isdir(root):
        return {}, "not found"
    out = {}
    for dirpath, _dirs, files in os.walk(root):
        for filename in files:
            if not filename.endswith(".npc"):
                continue
            for gameval, keys in parse_config(
                    os.path.join(dirpath, filename)).items():
                if gameval not in known:
                    continue
                still = (keys.get("moverestrict") == "nomove"
                         or keys.get("wanderrange") == "0")
                out[gameval] = 0 if still else 1
    return out, None


# ------------------------------------------------------- the cache's own roof


def load_roof(content_dir, squares):
    """`{(level, x, z): settings}` for the map squares the roster stands on.

    A `.jm2`'s `==== MAP ====` rows are `<level> <lx> <lz>: h<height> o<overlay>
    f<settings> u<underlay>`, and `f` is the raw tile settings byte cachepack
    unpacked out of the map archive. Read straight out of the content tree
    rather than by decoding the cache: the tree already holds it, and reading it
    here is the same byte `torirs_server_scene.c` stamps ROOF from.

    No bridge shift. `apply_terrain_column` takes LINK_BELOW into account for
    the *floor* flag and deliberately does not for the roof one ("Raw cache
    level, no bridge shift"), so this must not either.
    """
    out = {}
    for map_x, map_z in sorted(squares):
        path = os.path.join(content_dir, "maps", "m%d_%d.jm2" % (map_x, map_z))
        if not os.path.exists(path):
            continue
        section = None
        with open(path, encoding="latin-1") as f:
            for line in f:
                line = line.strip()
                if line.startswith("===="):
                    section = "map" if "MAP" in line else "other"
                    continue
                if section != "map" or ":" not in line:
                    continue
                head, rest = line.split(":", 1)
                try:
                    level, local_x, local_z = [int(t) for t in head.split()]
                except ValueError:
                    continue
                match = re.search(r"\bf(\d+)\b", rest)
                if match:
                    out[(level, map_x * 64 + local_x, map_z * 64 + local_z)] = \
                        int(match.group(1))
    return out


def roofed(roof, level, x, z):
    settings = roof.get((level, x, z))
    return settings is not None and (settings & FLOFLAG_REMOVE_ROOF) != 0


def roof_state(roof, spawns):
    """`(all_roofed, any_roofed, bites)` for one npc's spawns.

    `bites` asks whether a roof restriction could change a step: it is true when
    the wander square around the npc holds both roofed and open tiles. A cave
    whose every tile is roofed answers false — `indoors` there forbids nothing,
    and a restriction that cannot change a step is not a statement about the
    npc.
    """
    if not spawns:
        return (False, False, False)
    flags = [roofed(roof, level, x, z) for x, z, level in spawns]
    bites = False
    if all(flags):
        for x, z, level in spawns:
            near = [roofed(roof, level, nx, nz)
                    for nx in range(x - WANDER_PROBE, x + WANDER_PROBE + 1)
                    for nz in range(z - WANDER_PROBE, z + WANDER_PROBE + 1)]
            if not all(near):
                bites = True
                break
    return (all(flags), any(flags), bites)


def layer_r1_rsmod(edits, known):
    """rsmod's `moveRestrict = indoors` / `outdoors`, keyed by gameval."""
    if not edits:
        return {}, "not found"
    out = {}
    for gameval, body in edits.items():
        if gameval not in known:
            continue
        match = re.search(r"moveRestrict\s*=\s*(indoors|outdoors)\b", body)
        if match:
            out[gameval] = match.group(1)
    return out, None


def layer_r2_lostcity(root, known):
    """LostCity's `moverestrict=indoors` / `outdoors`, keyed by gameval."""
    if not os.path.isdir(root):
        return {}, "not found"
    out = {}
    for dirpath, _dirs, files in os.walk(root):
        for filename in files:
            if not filename.endswith(".npc"):
                continue
            for gameval, keys in parse_config(
                    os.path.join(dirpath, filename)).items():
                if gameval in known and keys.get("moverestrict") in ("indoors",
                                                                     "outdoors"):
                    out[gameval] = keys["moverestrict"]
    return out, None


def layer_r3_cache(roof, spawns_of):
    """This cache: every spawn under a roof, and a roof edge inside the wander
    square. The only layer here that is not another server's opinion."""
    out = {}
    for gameval, spawns in spawns_of.items():
        all_roofed, _any_roofed, bites = roof_state(roof, spawns)
        if all_roofed and bites:
            out[gameval] = "indoors"
    return out, None


# ------------------------------------------------------------- the ledger


LEDGER_HEADER = """\
// {display}
// npc {npc_id}{where}
//
// Generated by tools/gen_npc_movement.py. Re-running rewrites this file.
//
// To pin this by hand: edit the value, set `source = authored`, and every later
// run will read this file back and leave it exactly as it stands. That is the
// whole contract - there is no second place to register the exception.
//
// Every row below is another server's independent answer; the layers are
// documented in the tool's own header. `-` means that source has never
// described this npc.
"""


def ledger_path(content_dir, gameval):
    shard = gameval[0].lower()
    if not shard.isalnum():
        shard = "_"
    return os.path.join(content_dir, LEDGER_DIR, shard, gameval + ".move")


def read_ledger(path):
    """`{key: value}` from a ledger file, comments stripped."""
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.split("//", 1)[0].strip()
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            out[key.strip()] = value.strip()
    return out


def write_ledger(path, gameval, display, npc_id, where, decision, layer, votes,
                 restrict, restrict_layer, restrict_votes):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    lines = [LEDGER_HEADER.format(
        display=display or "(no name)",
        npc_id=npc_id,
        where=(" - " + where) if where else "")]
    lines.append("")
    lines.append("source       = generated")
    lines.append("")
    verdict = "stationary" if decision == 0 else "wanders"
    lines.append("%-12s = %-12s // %s  %s"
                 % ("movement", verdict, layer,
                    LAYER_WHAT.get(layer, "no source; the engine default")))
    lines.append("%-12s = %-12s // %s  %s"
                 % ("restrict", restrict or "normal",
                    restrict_layer or "--",
                    RESTRICT_WHAT[restrict_layer] if restrict_layer
                    else ("moot, this npc does not wander" if decision == 0
                          else "no source, and no roof over its spawn")))
    lines.append("")
    for name in LAYERS:
        vote = votes.get(name)
        text = "-" if vote is None else ("stationary" if vote == 0 else "wanders")
        lines.append("%-12s = %-12s // %s" % (name, text, LAYER_WHAT[name]))
    lines.append("")
    for name in RESTRICT_LAYERS:
        vote = restrict_votes.get(name)
        lines.append("%-12s = %-12s // %s"
                     % (name, vote or "-", RESTRICT_WHAT[name]))
    with open(path, "w", encoding="latin-1") as f:
        f.write("\n".join(lines) + "\n")


def claim_server_membership(content_dir, names):
    """Name every configured npc in `pack/npc.server`, merging in sorted place.

    `cachepack pack` routes on that file: a record gets a server band only if it
    is named there, and stating a server field without being named is a hard
    error. The file is `membership = authored`, so this merges and never removes
    a name or rewrites the header.
    """
    path = os.path.join(content_dir, "pack", "npc.server")
    with open(path, encoding="latin-1") as f:
        lines = f.read().splitlines()
    listed = {l.strip() for l in lines
              if l.strip() and not l.strip().startswith("//")}
    new = sorted(set(names) - listed)
    if not new:
        return 0
    out = []
    pending = list(new)
    for line in lines:
        stripped = line.strip()
        if stripped and not stripped.startswith("//"):
            while pending and pending[0] < stripped:
                out.append(pending.pop(0))
        out.append(line)
    out.extend(pending)
    with open(path, "w", encoding="latin-1") as f:
        f.write("\n".join(out) + "\n")
    return len(new)


# ------------------------------------------------------------------- main


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--content", default="OSRS-Content/osrs239-content")
    ap.add_argument("--spawn-dump", default=os.path.expanduser(
        "~/Documents/git_repos/xrsps-typescript/server/data/npc-spawns.json"))
    ap.add_argument("--rsmod", default=os.path.expanduser(
        "~/Documents/git_repos/rsmod"))
    ap.add_argument("--zenyte", default=os.path.expanduser(
        "~/Documents/git_repos/ZenyteLikeServer/data/npcs/spawns.json"))
    ap.add_argument("--scape2009", default=os.path.expanduser(
        "~/Documents/git_repos/2009scape/Server/data/configs/npc_spawns.json"))
    ap.add_argument("--lostcity", default=os.path.expanduser(
        "~/Documents/git_repos/LostCity_Server/content"))
    ap.add_argument("--write", action="store_true",
                    help="write the ledger and the compiled config")
    ap.add_argument("--validate", action="store_true",
                    help="measure the layers against each other, write nothing")
    args = ap.parse_args()

    content = args.content
    npcs = parse_config(os.path.join(content, "configs", "all.npc"))
    ids = load_compack(os.path.join(content, "configs", "all.npc.compack"))
    spawns = load_world_spawns(content)

    tile_index = collections.defaultdict(set)
    square_of = {}
    for gameval, x, z, level in spawns:
        tile_index[(x, z, level)].add(gameval)
        square_of.setdefault(gameval, "m%d_%d" % (x // 64, z // 64))

    spawns_of = collections.defaultdict(list)
    for gameval, x, z, level in spawns:
        spawns_of[gameval].append((x, z, level))
    roof = load_roof(content, set((x // 64, z // 64) for _g, x, z, _l in spawns))

    edits = rsmod_edits(args.rsmod)

    layers = {}
    missing = []
    layers["m1"], why = layer_m1_rsmod(edits, npcs)
    if why:
        missing.append("m1 " + args.rsmod)
    layers["m0"], why = layer_m0_dump(args.spawn_dump, tile_index)
    if why:
        missing.append("m0 " + args.spawn_dump)
    layers["m2"], why = layer_m2_zenyte(args.zenyte, tile_index)
    if why:
        missing.append("m2 " + args.zenyte)
    layers["m3"], why = layer_m3_2009scape(args.scape2009, tile_index)
    if why:
        missing.append("m3 " + args.scape2009)
    layers["m4"], why = layer_m4_lostcity(args.lostcity, npcs)
    if why:
        missing.append("m4 " + args.lostcity)

    restrict_layers = {}
    restrict_layers["r1"], _why = layer_r1_rsmod(edits, npcs)
    restrict_layers["r2"], _why = layer_r2_lostcity(args.lostcity, npcs)
    restrict_layers["r3"], _why = layer_r3_cache(roof, spawns_of)

    # The roof gate. A source that calls an npc `indoors` whose spawns are not
    # roofed in *this* cache is describing a world that has moved -- the same
    # shape as gen_npc_combat.py putting LostCity's animation names through the
    # rig gate. r3 is derived from the roof and so passes trivially; it is gated
    # anyway rather than special-cased, so the count below means one thing.
    gate_dropped = collections.Counter()
    for name in ("r1", "r2"):
        for gameval, value in list(restrict_layers[name].items()):
            spots = spawns_of.get(gameval)
            if not spots:
                continue          # not in the roster: nothing to check it against
            all_roofed, any_roofed, _bites = roof_state(roof, spots)
            if (value == "indoors" and not all_roofed) or \
               (value == "outdoors" and any_roofed):
                del restrict_layers[name][gameval]
                gate_dropped[name] += 1

    print("this cache: %d npcs, %d world spawns on %d tiles"
          % (len(npcs), len(spawns), len(tile_index)))
    for name in LAYERS:
        answers = layers[name]
        print("  %s %-34s %5d npcs, %4d stationary"
              % (name, LAYER_WHAT[name], len(answers),
                 sum(1 for v in answers.values() if v == 0)))
    for line in missing:
        print("  !! source not found, layer empty: " + line)
    print("  roof: %d tiles read from maps/*.jm2, %d npcs stand only on roofed ones"
          % (len(roof),
             sum(1 for g, sp in spawns_of.items() if roof_state(roof, sp)[0])))
    for name in RESTRICT_LAYERS:
        answers = restrict_layers[name]
        print("  %s %-34s %5d npcs, %4d indoors%s"
              % (name, RESTRICT_WHAT[name], len(answers),
                 sum(1 for v in answers.values() if v == "indoors"),
                 "" if not gate_dropped[name]
                 else "   (%d dropped by the roof gate)" % gate_dropped[name]))

    if args.validate:
        print("\nlayers agree, where two of them both answer:")
        for i, a in enumerate(LAYERS):
            for b in LAYERS[i + 1:]:
                both = set(layers[a]) & set(layers[b])
                if not both:
                    continue
                same = sum(1 for g in both if layers[a][g] == layers[b][g])
                print("  %s vs %s  %4d/%4d  %3d%%"
                      % (a, b, same, len(both),
                         round(100 * same / len(both))))

    # ---- the decision: the highest-ranked layer that has an opinion --------
    described = set()
    for name in LAYERS:
        described |= set(layers[name])

    authored = load_authored_movement(content, os.path.join(content, CONFIG_OUT))

    described |= set(g for name in RESTRICT_LAYERS
                     for g in restrict_layers[name])

    decided = {}
    restricted = {}
    skipped = {}
    pinned = 0
    for gameval in sorted(described):
        votes = {n: layers[n].get(gameval) for n in LAYERS}
        layer = next((n for n in LAYERS if votes[n] is not None), None)
        # No movement layer answers, only a restriction one: the npc wanders,
        # which is the engine default and needs no block -- but it may still
        # need a roof over it, so it keeps a ledger and reaches the code below.
        decision = votes[layer] if layer else 1
        layer = layer or "--"

        restrict_votes = {n: restrict_layers[n].get(gameval)
                          for n in RESTRICT_LAYERS}
        restrict_layer = next((n for n in RESTRICT_LAYERS
                               if restrict_votes[n] is not None), None)
        restrict = restrict_votes[restrict_layer] if restrict_layer else None
        # A restriction on an npc that never takes a step says nothing, and
        # saying it anyway would put a second, unfalsifiable field on a thousand
        # blocks. The stationary decision runs first precisely so this one does
        # not have to answer for them -- and grading against rsmod's Lumbridge,
        # that ordering is the whole of the difference between the two sources.
        if decision == 0:
            restrict_layer = None
            restrict = None

        path = ledger_path(content, gameval)
        existing = read_ledger(path)
        if existing.get("source") == "authored":
            decision = 0 if existing.get("movement") == "stationary" else 1
            pinned_restrict = existing.get("restrict", "normal")
            restrict = None if pinned_restrict == "normal" else pinned_restrict
            layer = "--"
            restrict_layer = "--" if restrict else None
            pinned += 1
        elif args.write:
            write_ledger(path, gameval, npcs.get(gameval, {}).get("name"),
                         ids.get(gameval, -1), square_of.get(gameval),
                         decision, layer, votes,
                         restrict, restrict_layer, restrict_votes)

        if gameval in authored:
            if decision == 0 or restrict:
                skipped[gameval] = authored[gameval]
            continue
        if decision == 0:
            decided[gameval] = (layer, votes)
        elif restrict:
            restricted[gameval] = (restrict, restrict_layer, restrict_votes)

    conflicted = [g for g in described
                  if 0 in [v for v in (layers[n].get(g) for n in LAYERS) if v is not None]
                  and 1 in [v for v in (layers[n].get(g) for n in LAYERS) if v is not None]]

    print("\n%d npcs at least one source describes" % len(described))
    print("%d of them come out stationary" % (len(decided) + len(skipped)))
    print("%d sources disagree about; the highest-ranked layer decides"
          % len(conflicted))
    print("%d already state a movement field in a hand-written block, left alone:"
          % len(skipped))
    for gameval, (where, keys) in sorted(skipped.items())[:8]:
        print("    %-40s %s (%s)" % (gameval, where, ",".join(keys)))
    if len(skipped) > 8:
        print("    ... and %d more" % (len(skipped) - 8))
    if pinned:
        print("%d ledger rows are pinned `source = authored` and were read back"
              % pinned)
    print("%d of the ones that still wander are restricted (%d indoors, "
          "%d outdoors)"
          % (len(restricted),
             sum(1 for v in restricted.values() if v[0] == "indoors"),
             sum(1 for v in restricted.values() if v[0] == "outdoors")))
    print("%d blocks to write" % len(set(decided) | set(restricted)))

    if args.validate:
        # A closed-world grade. rsmod's Lumbridge file enumerates that area's
        # npcs, so for those npcs "rsmod says nothing" means normal rather than
        # unknown -- which is the only way to measure r3's *false* positives at
        # all. Every other source is silent about most of the world.
        agree = differ = moot = held = 0
        rows = []
        for gameval in sorted(set(edits) & set(npcs)):
            theirs = "indoors" if re.search(
                r"moveRestrict\s*=\s*indoors", edits[gameval]) else "normal"
            if gameval in decided:
                moot += 1
                continue
            if gameval in authored:
                held += 1
                continue
            ours = restricted.get(gameval, ("normal",))[0]
            if ours == theirs:
                agree += 1
            else:
                differ += 1
                rows.append((gameval, theirs, ours))
        print("\ngraded against rsmod's own edited npcs, closed-world:")
        print("  %d agree, %d differ" % (agree, differ))
        print("  %d moot -- this tool stops them entirely, which is stricter"
              % moot)
        print("  %d held by a hand-written block in this tree" % held)
        for gameval, theirs, ours in rows:
            print("    %-34s rsmod=%-9s here=%s" % (gameval, theirs, ours))

    if not args.write:
        print("\n(nothing written; pass --write)")
        return 0

    # ---- the config -------------------------------------------------------
    out_path = os.path.join(content, CONFIG_OUT)
    with open(out_path, "w", encoding="latin-1") as f:
        f.write(
            "// Generated by tools/gen_npc_movement.py --write. Re-running\n"
            "// rewrites this file. The decision for each npc lives in its own\n"
            "// file under `npc_movement/`; edit that one and set\n"
            "// `source = authored` there, which is honoured forever.\n"
            "//\n"
            "// An npc stands still because other servers independently say it\n"
            "// does -- no cache field states it, so the answer is joined from\n"
            "// rsmod, rs-map-viewer's spawn dump, Zenyte, 2009scape and\n"
            "// LostCity. The trailing note on each block names the layer that\n"
            "// answered and the layer's own verdict list.\n"
            "//\n"
            "// A block states one field, and never both. `wanderrange=0` is\n"
            "// an npc that does not take a step; `moverestrict=indoors` is one\n"
            "// that does, and stops at the roof line -- the field the engine\n"
            "// has carried since npc_collision_type and which one npc in the\n"
            "// tree stated. A restriction on an npc that never steps would be\n"
            "// unfalsifiable, so the stationary decision runs first and this\n"
            "// file leaves those alone.\n"
            "//\n"
            "// `torirs_server_content.c` seeds an npc def once and applies every\n"
            "// later block to the same record, so this file overlays\n"
            "// npc_anims.generated.npc and the area files rather than\n"
            "// shadowing them. An npc whose hand-written block already states\n"
            "// a movement field is not repeated here at all -- a person\n"
            "// decided that one.\n"
            "//\n"
            "// %d npcs: %d stationary, %d restricted.\n\n"
            % (len(set(decided) | set(restricted)), len(decided),
               len(restricted)))
        for gameval in sorted(set(decided) | set(restricted)):
            display = npcs.get(gameval, {}).get("name") or "(no name)"
            if gameval in decided:
                _layer, votes = decided[gameval]
                agree = " ".join(
                    "%s=%s" % (n, "still" if v == 0 else "walks")
                    for n, v in votes.items() if v is not None)
                f.write("// %s -- %s\n" % (display, agree))
                f.write("[%s]\n" % gameval)
                f.write("wanderrange=0\n\n")
            else:
                value, _layer, votes = restricted[gameval]
                agree = " ".join("%s=%s" % (n, v)
                                 for n, v in votes.items() if v is not None)
                f.write("// %s -- %s\n" % (display, agree))
                f.write("[%s]\n" % gameval)
                f.write("moverestrict=%s\n\n" % value)
    print("\nwrote %s (%d blocks: %d wanderrange, %d moverestrict)"
          % (out_path, len(set(decided) | set(restricted)), len(decided),
             len(restricted)))

    added = claim_server_membership(content, set(decided) | set(restricted))
    print("pack/npc.server: %d name(s) added" % added)
    return 0


if __name__ == "__main__":
    sys.exit(main())
