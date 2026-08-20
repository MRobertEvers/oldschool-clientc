# Collision Map Verification (C vs Client-TS)

**Canonical directionality / reference-server write-up:**
[`COLLISION_MAP.md`](COLLISION_MAP.md).

This stub keeps the C ↔ Client-TS shape / BFS checklist. Paths below match the
current tree (`src/engine/world_builder/…`), not the old `src/osrs/…` layout.

## Reference files

- **Client-TS:** `Client-TS/src/dash3d/CollisionMap.ts` (addLoc, addWall, blockGround, reset)
- **Client-TS:** `Client-TS/src/dash3d/ClientBuild.ts` (addLoc → shape/blockwalk/active; LinkBelow → place-time collision level)
- **C:** `src/engine/world_builder/collision_map.c` (collision_map_add_floor, _add_loc, _add_wall)
- **C:** `src/engine/world_builder/world_collision.u.c` (loc / terrain / bridge collision apply)
- **Server:** `src/torirsserver/torirs_server_scene.c` (`apply_loc_collision`, terrain, bridge)

## Semantics

- **Reset:** Border tiles = BOUNDS, interior = OPEN (walkable).
- **FLOOR:** Adding FLOOR *blocks* the tile (floor decoration with blockwalk, or terrain BLOCK).
- **Gate:** Client updates collision when `loc.blockwalk` (and for GROUND_DECOR also `loc.active`). C uses `config_loc->blocks_walk` (and interactive for ground decor).
- **Walls:** Dual-tile complementary stamps; BFS checks destination only — see [`COLLISION_MAP.md`](COLLISION_MAP.md).
- **Bridges:** Place-time level shift when `LINK_BELOW` (Client-TS / LostCity), not a post-hoc whole-word column overwrite.

## Shape → action mapping

| Client LocShape           | C LOC_SHAPE_*            | Action                                   |
|---------------------------|--------------------------|------------------------------------------|
| GROUND_DECOR (22)         | FLOOR_DECORATION (22)    | blockGround / collision_map_add_floor   |
| WALL_STRAIGHT (0)         | WALL_SINGLE_SIDE (0)     | addWall(shape, angle, blockrange)        |
| WALL_DIAGONAL_CORNER (1)  | WALL_TRI_CORNER (1)      | addWall                                  |
| WALL_L (2)                | WALL_TWO_SIDES (2)       | addWall                                  |
| WALL_SQUARE_CORNER (3)    | WALL_RECT_CORNER (3)     | addWall                                  |
| WALL_DIAGONAL (9)         | WALL_DIAGONAL (9)        | addLoc(width, length, angle, blockrange) |
| CENTREPIECE_* (10,11)     | SCENERY / SCENERY_DIAGIONAL (10,11) | addLoc                        |
| ROOF_* (12..21)           | ROOF_* (12..21)          | addLoc                                   |

## addLoc / addWall details

- **Size swap:** When angle is NORTH or SOUTH, swap `size_x`/`size_z` before filling the rectangle (`collision_map_loc_apply`).
- **Bounds:** Out-of-range tiles are skipped (no write).
- **Angle:** 0=WEST, 1=NORTH, 2=EAST, 3=SOUTH (`orientation & 0x3`).
- **blockrange:** PROJ wall flags first, then recurse with `blockrange=false` for walk walls.

## BFS / BLOCK_* flags

- Steps to `(x-1,z)` check `(flags[x-1,z] & BLOCK_WEST) === OPEN`; `BLOCK_WEST = WALL_EAST | WALK_BLOCKED`.
- C: same composites in `collision_map.h` / `collision_map_can_step_*`.
- Index: `x * cm->size_z + z` via `collision_map_index_at`.

## Checklist when changing collision

- [ ] Keep shape/angle/action table in sync with ClientBuild.addLoc and CollisionMap.
- [ ] Keep add_loc size swap (NORTH/SOUTH) and rectangle range identical to Client-TS.
- [ ] Keep add_wall dual-tile pairs identical to CollisionMap.addWall.
- [ ] Keep BLOCK_* composites and dest-only step checks aligned with Client.ts pathfinding.
- [ ] Bridge / LinkBelow: place-time collision level shift (ClientBuild / LostCity), preserving wall mirrors.
