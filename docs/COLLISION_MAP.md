# Collision map directionality

How wall flags are set and checked across the named reference servers, why
walking through a wall one way but not the reverse means an **orphan
complementary bit**, and the locked decision for this tree's modern server
(`mock230` + shared `collision_map.c`).

Cross-links: [`OSRS_PATHING_LOS.md`](OSRS_PATHING_LOS.md) (reach / LoS —
separate from walk walls), [`PATHING_INTERACTION_PARITY.md`](PATHING_INTERACTION_PARITY.md),
[`COLLISION_MAP_VERIFICATION.md`](COLLISION_MAP_VERIFICATION.md) (C ↔ Client-TS
shape table stub).

Written 2026-08-03. Re-measure rather than trusting prose counts.

---

## 0. Verdict

Walk / BFS step checks are **destination-only**. Two-way wall blocking is a
**data invariant**: `addWall` always ORs complementary `WALL_*` bits onto
**both** tiles of the shared edge. If only one side of the edge has the facing
bit, one direction walks through and the reverse is blocked.

```
WEST wall on tile A, neighbor B = A−1:

  SET:  A |= WALL_WEST (0x80);  B |= WALL_EAST (0x08)

  A → B (west): read B & BLOCK_WEST  (= WALL_EAST | solid) → blocked
  B → A (east): read A & BLOCK_EAST  (= WALL_WEST | solid) → blocked

Orphan (A has WALL_WEST, B lacks WALL_EAST):
  A → B: allowed (walk through)
  B → A: blocked
```

**Do not fix one-way walls by checking source+dest on every step.** That
diverges from LostCity / rsmod / Kronos and from this tree's own reach docs
(which already rejected XRSPS both-tiles checks for approach). Fix the stamp.

---

## 1. Shared flag model

Low 8 wall bits are identical in LostCity, OpenRune/rsmod, Kronos,
xrsps-typescript, Client-TS, and this tree:

| Flag | Value |
|---|---|
| `WALL_NORTH_WEST` | `0x1` |
| `WALL_NORTH` | `0x2` |
| `WALL_NORTH_EAST` | `0x4` |
| `WALL_EAST` | `0x8` |
| `WALL_SOUTH_EAST` | `0x10` |
| `WALL_SOUTH` | `0x20` |
| `WALL_SOUTH_WEST` | `0x40` |
| `WALL_WEST` | `0x80` |
| solid loc | `0x100` |
| proj walls | `0x200`…`0x10000` (same order, ≪ 9) |

Composite walk masks (what steps AND against the **destination**):

| Move | Mask name | Wall bit on dest |
|---|---|---|
| west | `BLOCK_WEST` | `WALL_EAST` |
| east | `BLOCK_EAST` | `WALL_WEST` |
| south | `BLOCK_SOUTH` | `WALL_NORTH` |
| north | `BLOCK_NORTH` | `WALL_SOUTH` |

Solid / floor bits inside the composite differ by codebase:

| Codebase | typical `BLOCK_WEST` |
|---|---|
| LostCity / rsmod | `0x240108` |
| Kronos | `0x1240108` |
| this tree / Client-TS | `0x280108` |

**Wall pairing does not differ.** Angle for straight walls: `0=WEST, 1=NORTH,
2=EAST, 3=SOUTH`.

Straight-wall stamp (every tree):

| Angle | Home tile | Neighbor |
|---|---|---|
| WEST | `WALL_WEST` | `(x−1,z)` `WALL_EAST` |
| NORTH | `WALL_NORTH` | `(x,z+1)` `WALL_SOUTH` |
| EAST | `WALL_EAST` | `(x+1,z)` `WALL_WEST` |
| SOUTH | `WALL_SOUTH` | `(x,z−1)` `WALL_NORTH` |

Corners and L-walls mirror the same idea (diagonal / two-edge pairs). Diagonals
in BFS also require the two intermediate cardinal dests clear (no corner cut).

---

## 2. LostCity

Repo: `~/Documents/git_repos/LostCity_Server`

| Role | Path |
|---|---|
| Flag defs | `engine/src/engine/routefinder/flags.ts` |
| Set walls | `engine/src/engine/routefinder/CollisionEngine.ts` (`applyWallPair`) |
| Step check | `engine/src/engine/routefinder/StepValidator.ts` |
| Map load | `engine/src/engine/GameMap.ts` (`changeLocCollision`) |

- **Set:** always both sides via `applyWallPair`.
- **Check:** size-1 cardinals read **only the destination** against `BLOCK_*`.
- **Bridges:** place-time level shift — `actualLevel = bridged ? level - 1 :
  level`; skip when `actualLevel < 0`. Locs and land BLOCK use that level; no
  post-hoc whole-word column copy for collision.
- **One-sidedness:** `forceapproach` / `BlockAccessFlag` on **loc approach
  only**, not tile collision.

---

## 3. OpenRune + rsmod-routefinder

Repo: `~/Documents/git_repos/OpenRune-Server` (dependency `org.rsmod:rsmod-routefinder`)

| Role | Path |
|---|---|
| Set walls | `game-server/.../collision/CollisionFlagMapExtensions.kt` (`toggleWall`) |
| Traverse | `game-server/.../model/World.kt` → `StepValidator.canTravel` |
| Flags / BFS | rsmod `CollisionFlag`, `StepValidator`, `RouteFinding` |

Same algorithm family as LostCity: mirrored wall pairs, dest-only
`BLOCK_*` checks. Prefer this (with Ash) for modern OSRS pathing wiring.

---

## 4. Kronos

Repo: `~/Documents/git_repos/kronos-osrs-184`

| Role | Path |
|---|---|
| Atomic flags (client API) | `runelite/.../CollisionDataFlag.java` |
| Set walls | `kronos-server/.../map/ClipUtils.java` (`addVariableClipping`) |
| BFS | `kronos-server/.../route/RouteFinder.java` (`WEST_MASK=0x1240108`, …) |
| Storage | `Tile.clipping` / `projectileClipping` (no `CollisionMap` class) |

Same dual-flag numbers and dest-only inverted masks. Corroborates
decompiled-client BFS; already cited from `OSRS_PATHING_LOS.md`.

---

## 5. xrsps-typescript

Repo: `~/Documents/git_repos/xrsps-typescript`

| Role | Path |
|---|---|
| Flags | `client/common/CollisionFlag.ts` |
| Set | `client/rs/scene/CollisionMap.ts` `addWall` |
| Step | `server/src/pathfinding/PathService.ts` `canMoveDirection` |
| BFS | `server/src/pathfinding/legacy/pathfinder/Pathfinder.ts` |

- **Set:** mirrored pairs (same as client).
- **Check:** dest-only (source flag is fetched but unused for walls).
- **Extra:** `edgeHasWallBetween` unions both sides — **overlay / helper
  only**, not the walk gate.
- Reach / loc-profile heuristics elsewhere in XRSPS are **not** authorities
  for this tree (`OSRS_PATHING_LOS.md` §0 / §1.1).

---

## 6. Modern server decision (locked)

**Keep the convergent model already in**
[`src/engine/world_builder/collision_map.c`](../src/engine/world_builder/collision_map.c):

1. Dual-tile wall stamp in `collision_map_wall_apply`.
2. Destination-only `collision_map_can_step_*` / `can_travel_typed`.
3. Server / client scene apply use Client-TS / LostCity **place-time** bridge
   level shift when stamping collision (`LINK_BELOW` → collision `level - 1`,
   skip if `< 0`). Do **not** reinstate a post-hoc whole-flag-word column
   overwrite — that splits wall edges and creates one-way orphans.
4. Refuse XRSPS both-tiles walk checks.
5. Symmetry is a **data invariant**, enforced by tests.

Scene writers:

- Client build: [`world_collision.u.c`](../src/engine/world_builder/world_collision.u.c)
- Server: [`mock230_scene.c`](../src/net/mock/mock230_scene.c) `apply_loc_collision`

Reference Client-TS place-time shift (`ClientBuild.ts`): when
`mapl[1][x][z] & LinkBelow`, collision goes to `collisions[level - 1]` (null if
`< 0`); geometry still uses the raw cache level. LostCity `GameMap` does the
same for land and locs.

## 6b. Removal is not the inverse of adding (doors)

`collision_map_del_wall` / `del_loc` / `del_floor` clear their bits with `&= ~`.
That is the reference's own primitive (LostCity `applyWallPair(..., add=false)`,
Client-TS `CollisionMap.delWall`) and it is correct **only while no two things
share a bit**. Three ways they do:

| Sharers | The shared bit |
|---|---|
| A wall on the east edge of `(x,z)` and one on the west edge of `(x+1,z)` | the same edge — both stamp `WALL_EAST` on `(x,z)` and `WALL_WEST` on `(x+1,z)` |
| A blockwalk ground-decor loc and the map square's own `Block` tile setting | `FLOOR` |
| A door swung open onto a tile a wall already covers | whatever that wall stamps |

The third is what a **swinging door** does. `doors/scripts/doors.rs2` opens a
door with `loc_del` on its own tile and `loc_add` a quarter turn on; where the
tile it swings to already carries a wall, closing the door used to clear that
wall's bits and nothing put them back. Measured across every map square in
`cache.osrs239`: **75 doors, 44 wall edges left permanently walkable**, 25 of the
loc ids being content-declared doors (`1535`, the Lumbridge courtyard door,
among them).

Two rules, both in [`mock230_scene.c`](../src/net/mock/mock230_scene.c):

1. **Collision is a function of the loc set, not a running total.**
   `restamp_after_removal` re-applies the map square's terrain and every other
   active loc whose stamp reaches the tiles a removal touched. Adds are ORs, so
   over-stamping is free.
2. **`loc_add` does not consume what is already on the tile.** The reference's
   `World.addLoc` / `Zone.addLoc` never look at an existing loc, and
   `Zone.removeLoc` puts a static one back on the list. `mock230_scene_add_loc`
   leaves the map square's loc standing; `mock230_scene_find_loc_exact` answers
   the added loc first so a following `loc_change` / `loc_del` addresses it; and
   `mock230_world_loc_set` takes an explicit `MOCK230_LOC_SET_ADD` /
   `_CHANGE` because the two are different mutations that had been collapsed
   into "replace whatever is on this tile".

A delete that uncovers the square's own loc sends `LOC_ADD_CHANGE` for it rather
than `LOC_DEL`, which is also what retires the ZoneMap record. `over_base` on
`struct Mock230ZoneLoc` is what makes a scene rebuild replay the add as an add.

**The doorway rule is not a rule about doors.** Nothing refuses a diagonal
because a door is there; the walls flanking the gap refuse it, through the
`BLOCK_*` diagonal composites. So "you cannot cut a doorway diagonally" is a
statement about the flanking walls' bits, and the way to check it is over the
whole map — see `test-collision-doors` below.

### Failure modes (orphan complementary bit)

| Cause | Why one-way |
|---|---|
| One-sided `flags[i] \|= WALL_*` (test / bug) | Dest-only check sees only one face |
| Post-hoc bridge **column overwrite** of one tile of an edge | Mirror bit stays on the un-copied neighbor level — **removed**; use place-time LinkBelow shift |
| OOB neighbor silent skip in `collision_map_add` | Scene-border walls stamp one side only (same as client) |
| Runtime add/del with mismatched shape/angle | Clears one geometry, leaves the other |

### Debugging a crossed edge

Dump both tiles and both `can_step` directions:

```
flags(A), flags(B)
can_step west from A / east from B   (or N/S as appropriate)
```

If only one tile has the facing `WALL_*`, the stamp/del/bridge path is wrong —
not the step checker. Embed courtyard-door checks in `embed_test.c` already
require bits on both sides and refuse both directions.

### Tests

- [`src/world/test/world_test_route.c`](../src/world/test/world_test_route.c) —
  dual-stamp blocks both ways; one-sided orphan produces one-way walk;
  approach models stay source-only for size-1 rect.
- [`src/net/mock/test/embed_test.c`](../src/net/mock/test/embed_test.c) —
  live-map door / fence both-directions refusal.
- [`src/net/mock/test/collision_doors_test.c`](../src/net/mock/test/collision_doors_test.c)
  — `make -C src test-collision-doors`. Builds real scenes from the cache and
  asserts, over every `wall_straight` door with an "Open" op: no orphan
  complementary bit anywhere in the scene, a closed door blocks its own edge, no
  diagonal step crosses a doorway line open **or** shut, and opening then
  closing a door leaves the collision map byte-identical. The default 25 squares
  are the towns plus every square that carried one of the 75 round-trip
  failures; `TORIRS_COLLISION_DOORS_FULL=1` sweeps the whole overworld (480
  scenes, 1,637 doors, ~45s).

Reach and LoS asymmetries are intentional and documented elsewhere; they are
not wall-walk directionality.
