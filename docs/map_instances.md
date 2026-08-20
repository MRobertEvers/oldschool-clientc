# Map instances

> Written 2026-08-04. Everything numeric here was measured in this tree;
> re-measure rather than trusting the prose (PORTING_GUIDE §7).

An instance is a private copy of a piece of the map, assembled out of 8x8 zones
taken from anywhere in the cache. It is the thing a player-owned house, the Pest
Control island, a Barrows tunnel and a cutscene set all are, and until this
landed the engine could not express any of them: `SCAPE2009_CONTENT_PORT_QUEUE`
rows 4a and 4b were **blocked** on the surface, not on the content. Slice 4a
(house enter/leave + garden hotspot build) now lives in `skill_construction/`
(`poh_*.rs2` — **do not** rename the directory to `skill_construction.skip`,
`.rs2.skip` files, or delete for other-lane compiles; see
[`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md) log and
`.cursor/rules/no-park-sibling-content.mdc`). 4c (build IF +
rooms) is still pending.

Source, in the order it is easiest to read:

- `src/torirsserver/torirs_server_mapinstance.h` — the contract and the reason for each
  field. Read this before the doc if you only read one thing.
- `src/serverscript/gen_opcode_meta.py`, the `# ---- map instances` block — why
  there are six commands and not one.
- `src/torirsserver/torirs_server_scene.c` `ToriRSServer_SceneBuildInstance` — the server's
  copy (collision + locs).
- `src/torirsserver/torirs_server_encode.c` `ToriRSServer_SendRebuildRegion` — the wire.
- `src/engine/world_builder/world_builder.c` `WorldBuilder_RebuildInstance` —
  the client's copy (terrain + scenery).

Proofs: `[debugproc,mapinstance]`, `[debugproc,mapinstance_turn]` and
`[debugproc,mapinstance_leave]` in
`OSRS-Content/.../general/scripts/misc/map_instance_debug.rs2`. §6 says what each
one shows and what a failure looks like.

---

## 1. The surface

Six commands, `ss_opcode.h` 11009..11014, in the EXTRA band:

```
map_instance_alloc(zone_w, zone_h)                        -> int handle
map_instance_setchunk(handle, level, zone_x, zone_z, src, turn)
map_instance_build(handle)
map_instance_coord(handle, dx, dz, level)                 -> coord
map_instance_free(handle)
map_instance_find(coord)                                  -> int handle
```

Handles are 1-based and **0 is the only "no instance" value** — alloc failure,
a `find` miss, and a handle varp that was never written all read as 0. That is
for content's sake rather than C's: a handle lives in a varp between the tick
that allocates a house and the tick that enters it, varps start at 0, and a
0-based handle would make "slot 0" and "never allocated" the same integer.

`setchunk` takes its source as a **coord**, not three ints, so content can name
a source zone off a landmark (`movecoord(^respawn_coord, 8, 0, 0)`) instead of
shifting tiles into zone indices by hand. Any tile inside the zone names it; the
registry floors it.

### Why six and not one

Because the four steps are separately observable in both behaviour references,
and collapsing them loses a case each time. 2009scape's `DynamicRegion`
(`reserveArea` → `setChunk` → `flagActive` → `Location` arithmetic) and Kronos's
`DynamicMap` (`FREE_REGIONS.poll` → `setChunk` → `load` → `sendRegion`) agree on
the shape. A single `map_instance_copy_region(regionId)` would cover a
whole-square copy and nothing else: it could not build a house room by room,
because a room is one zone rotated on its own.

What is deliberately **not** an opcode: rotating a whole area. An area copy that
also turned the layout is a different transform from either reference's, so
`~map_instance_copy_area` (content, unrotated) is the helper and `setchunk` per
zone is what you call when a turn is involved.

### What is not in here

No policy. No opcode decides which zones a house has, where the player lands, or
when the instance ends. `map_instance_free` will happily release an instance a
player is standing in — that is content's bug, and a 2009scape `checkInactive`
pulse is a *rule about when a minigame is over*, which belongs in content. That
separation was the condition for these existing at all (PORTING_GUIDE §2.4).

## 2. The references, and why this is not a port

**LostCity has none of it**, measured rather than assumed: `engine.rs2` declares
no map-allocation command (the nearest thing is a commented-out
`region_findbycoord` / `controller_*` block at `engine.rs2:1051-1060` that
Engine-TS wires to nothing), `BuildArea.rebuildNormal` is the only scene it can
send, and there is no construction content in the tree at all. So §2.2's grep
comes back empty and there are no reference names to match — hence the EXTRA
band.

The *behaviour* is ported from 2009scape
(`Server/src/main/core/game/world/map/build/DynamicRegion.kt`) and Kronos
(`io/ruin/game/world/map/DynamicMap.java`), and the *wire* is the client's own
(§4).

## 3. The pool is measured, not chosen

`mapinstance_scan_pool` sweeps the cache's maps reference table and treats a
square the cache does not ship as free. Nothing here hardcodes a coordinate
band, which matters because the queue's own rule is that a config-shaped
constant in C is a bug.

The sweep starts at map square x = 100 for the reason Kronos's pool does
(`DynamicMap.load` gates on `region.baseX >= 6400`): far enough from real map
that an instance is never adjacent to anything a player could walk in from. That
band was verified empty rather than trusted — this cache ships **2,934 squares,
all within x 15..98**, so the whole of x >= 100 is free.

Reservations are rounded up to whole map squares, so two instances never share a
square and a square is never half free. `map_instance_find` answers over the
**reserved footprint**, not the requested zone count: a 5-zone instance still
owns its whole square, and a player who walks off the assembled edge into void is
still inside the instance rather than mysteriously nowhere.

`map_instance_alloc` may reserve up to **16×16 zones**
(`TORIRSSERVER_MAPINSTANCE_ZONES`) — enough for The Gauntlet's 7×7 of 16-tile rooms
(14×14). The client's scene is still a sliding **13×13** window
(`TORIRSSERVER_MAPINSTANCE_SCENE_ZONES`); REBUILD_REGION never describes more than
that around the player.

## 4. The wire: REBUILD_REGION

Wire opcode **59**, `PKT_NAME_REBUILD_REGION`, var-short. Same three header
fields as REBUILD_NORMAL (a zero, then the origin zone x/z), then a bit block of
4 x 13 x 13 descriptors in `[level][zone_x][zone_z]` order, then a key count and
that many 16-byte key blocks.

Each descriptor is a presence bit; when set, 26 bits follow:

```
bits  1..2   rotation, quarter-turns clockwise
bits  3..13  source zone z   (11 bits)
bits 14..23  source zone x   (10 bits)
bits 24..25  source plane    (2 bits)
```

That layout is the client's, not this server's invention: it is what every
OSRS-era client reads out of `instanceTemplateChunks`
(`rotation = z >> 1 & 0x3`, `chunkY = z >> 3 & 0x7FF`, `chunkX = z >> 14 & 0x3FF`,
`plane = z >> 24 & 0x3`), and 2009scape's `BuildDynamicScene` and Kronos's
`sendRegion` both write exactly it.

Note the asymmetry the 10-bit source-x field creates: a **source** zone must have
x < 1024, i.e. map square x < 128. Destinations are the loop position and carry
no such limit — which is why the pool can sit at map x >= 100 while every source
it copies from is real map.

Keys are zeros, for the same reason REBUILD_NORMAL's are: this client reads its
XTEA keys from `xteas.json` beside the cache. The *count* is still computed from
the window (one block per distinct source square) so the two halves never
disagree about how many blocks follow.

## 5. The copy happens twice, and the two halves walk opposite ways

Collision is the server's and scenery is the client's, so the copy is written
twice. That is not duplication to be factored out — they need different data —
but they must agree, which is why both consume the same
`struct ToriRSServerMapInstanceWindow` shape and the same rotation helpers.

The direction differs *within* each half, and this is the part that bites:

- **Terrain** walks *destination* tiles and asks where each came from —
  `rotate_to_src`.
- **Locs** walk *source* locs and ask where each goes — `rotate_to_dst`.

They are inverses. Getting one backwards is invisible at rotations 0 and 2 and
mirrors the zone at 1 and 3, which is exactly why `[debugproc,mapinstance_turn]`
lays out all four turns side by side instead of testing one.

A loc is also not a point: it occupies `size_x` x `size_z` tiles from its
south-west corner, and a quarter-turn moves that corner to a different one of the
rectangle's four. `rotate_to_dst` takes the footprint for that reason, and the
footprint passed in is the one *as placed* — already swapped for the loc's own
odd angle.

Two ordering constraints, both load-bearing:

- Terrain settings are gathered for the whole scene before any collision rule
  runs (`gather_terrain_zone` then `apply_terrain_rules`), because the rules read
  neighbouring tiles.
- All terrain is applied before any loc, because `apply_loc_collision` reads
  `g_link_below`, which terrain writes.

A zone nobody set is **void and stays void**. That is what makes an empty house
floor empty rather than a copy of whatever the previous tenant of that pool slot
left there, and it is what the wire means too: a descriptor bit of 0 is "no
source".

## 6. Verifying

```
make -C src EMBED_SERVER=1 torirs && make -C src torirsserver-scripts

# the whole-square copy
SDL_VIDEODRIVER=dummy TORIRSSERVER_VERBOSE=1 TORIRS_MAX_FRAMES=1400 \
  TORIRS_EXIT_BMP=/tmp/inst.bmp TORIRS_NET_CHEAT="mapinstance" \
  ./src/torirs --manifest manifest_osrs230_embed.ini --user testc --pass test

# the four rotations
TORIRS_NET_CHEAT="mapinstance_turn"   # ... same otherwise
```

`::mapinstance` copies *the square you are standing on*, and the save keeps you
where the last run left you — so run `::mapinstance_leave` in a preceding session
before comparing, or you will be copying the pool square and looking at void.
`::mapinstance_turn` has no such dependency; its source zone is fixed, which is
why it is written that way.

`::mapinstance_leave` sends you home whether or not the registry still knows the
instance, because the case that most needs a way out is the one where it does
not: your coord is saved and the registry is not, so logging back in leaves you
on a pool square that no reservation covers, surrounded by void, with nothing to
walk to. (Using a fresh `--user` is the other way out, and it is the cleaner one
for a measurement, since a fresh account also has fresh stats and inventory.)

`[debugproc,mapinstance]` copies the square you are standing on and lands you on
the matching local tile of the copy. **The proof is that nothing looks
different**: the same floor, the same walls, the same scenery, but `::coord`
reads x >= 6400 and the minimap shows the instance's edge as void.

Measured from Lumbridge courtyard (3222,3218) against a control run at the same
tile: **1.08% of viewport pixels differ** (1,660 of 153,600), and they fall in
three places — the NPCs, which a map copy does not include; the animated locs
(the fountain, the trees), which are on their own sequence clocks; and the
player's own animation frame. No differing pixel is terrain or static scenery.
Do not lower this number by hand — the comparison is the test, so re-run it.

`[debugproc,mapinstance_turn]` puts four copies of one fixed zone
(`^respawn_coord`'s) west to east at turns 0, 1, 2, 3. A correct build shows the
same 8x8 motif a quarter turn further along each step, on the minimap as clearly
as in the world. Terrain turning while a wall stays put means `record_loc_at` got
the angle but not the position, or the reverse.

Measured on the run above: `8x8 zones at 6400,0 (map square 100,0)`, then
`instanced scene built at zone 802,2 (base 6368,-32 — 256 source zones, 4726
locs)`, then `REBUILD_REGION op=59 payload=941`. The 4,726 against the static
build's 8,455 for the same square is the scene window, not a loss: an instance is
one square inside a 13-zone scene, so the outer ring is void by construction.

Tests: `test-torirsserver-coverage` (the generated opcode-coverage header must know
about all six), `test-ss-meta`, `test-ss-provider`, `test-ssc`,
`test-world-builder`, `test-db`, `test-rsareabuf`, and `ToriRSServer_Pack
--check-only` at 0 errors.

## 7. Known limits

**One scene per world, so one instance at a time can be *collided* in.** The
server keeps a single collision map for a single scene origin
(`torirs_server_scene.c`), so `ToriRSServer_WorldSceneRebuild` builds from whichever
instance contains the current centre. Two players in two different houses would
share one collision map and one of them would be wrong. This is the mock server's
pre-existing shape rather than something instances introduced — the same is
already true of two players standing 200 tiles apart — and eight concurrent
reservations exist so that the *registry* is not the thing that needs rewriting
when the scene stops being a singleton.

**Nothing frees an instance on its own.** No timeout, no last-player-out sweep.
`ToriRSServer_MapInstanceReset` at world init/reset is the only automatic release, so
a handle cannot outlive the world that issued it, but within a session content
owns the lifetime. `[debugproc,mapinstance_leave]` is the manual version.

**The one exception, and it is content's too: the end of the session.** A run
that ends because the client went away — a disconnect, a `::logout`, the host
shutting down — used to leak its reservation *and* save the character standing
inside it, which is the worse half: the pool re-issues the square and the next
login draws void with nothing to walk off. `[logout,_]`
(`player/logout.rs2`) now teleports out and frees, per activity where the
activity has a policy (`~inferno_on_logout`) and through
`~map_instance_logout_release` where it does not. The engine's half is dispatching
the trigger at all, and dispatching it *above* the save —
`ToriRSServer_WorldRemovePlayer`, `osrs230_mockserver.md` §3.23. Pinned by the last
leg of `ToriRSServer --selftest`.

**A freed instance can still have npcs standing in it, and that is content's bug
by design.** The engine will not delete them — a rule about when a minigame is
over does not belong in the registry — but `map_instance_free` counts them and
says so under `TORIRSSERVER_VERBOSE`, because the symptom otherwise appears one
session later, as somebody else's boss already in the arena. Content's answer is
a despawn keyed on *position* rather than on a list of npc types
(`~inferno_despawn_arena`): an instance holds nothing but its own run's spawns,
so `map_instance_find(npc_coord) = $handle` is the whole test, and it cannot go
stale the way a type list does.

**Heights are per-zone, so zone seams can show.** The static build writes heights
across a square's boundary using the next square's data; a zone in an instance has
no such neighbour, so the client writes heights for the 8x8 interior only. Real
clients have the same seam, and it is only visible where two copied zones have
genuinely different terrain heights at the join.

**Objs and NPCs are not copied.** A map copy is terrain and locs. Everything
alive in an instance is content's to spawn, which is what both references do too.
