# Collision Map Verification (C vs Client-TS)

This doc describes how the C collision map construction is aligned with `Client-TS` so pathfinding (BFS) matches the client.

## Reference files

- **Client-TS:** `Client-TS/src/dash3d/CollisionMap.ts` (addLoc, addWall, blockGround, reset)
- **Client-TS:** `Client-TS/src/dash3d/ClientBuild.ts` (addLoc → shape/blockwalk/active checks)
- **C:** `src/osrs/collision_map.c` (collision_map_add_floor, _add_loc, _add_wall)
- **C:** `src/osrs/scenebuilder_scenery.u.c` (collision block after scenery switch)

## Semantics

- **Reset:** Border tiles = BOUNDS, interior = OPEN (walkable). No “add floor” for terrain; we only add blocking flags from locs.
- **FLOOR:** In both C and client, adding FLOOR *blocks* the tile (used for floor decoration with blockwalk).
- **Gate:** Client only updates collision when `loc.blockwalk` (and for GROUND_DECOR also `loc.active`). C uses `config_loc->clip_type != 0` (clip_type is the blockwalk equivalent in config).

## Shape → action mapping

| Client LocShape           | C LOC_SHAPE_*            | Action in both                         |
|---------------------------|--------------------------|----------------------------------------|
| GROUND_DECOR (22)         | FLOOR_DECORATION (22)    | blockGround / collision_map_add_floor |
| WALL_STRAIGHT (0)         | WALL_SINGLE_SIDE (0)     | addWall(shape, angle, blockrange)      |
| WALL_DIAGONAL_CORNER (1)  | WALL_TRI_CORNER (1)      | addWall                                |
| WALL_L (2)                | WALL_TWO_SIDES (2)       | addWall                                |
| WALL_SQUARE_CORNER (3)    | WALL_RECT_CORNER (3)     | addWall                                |
| WALL_DIAGONAL (9)         | WALL_DIAGONAL (9)        | addLoc(width, length, angle, blockrange) |
| CENTREPIECE_* (10,11)      | SCENERY / SCENERY_DIAGIONAL (10,11) | addLoc  |
| ROOF_* (12..21)           | ROOF_* (12..21)          | addLoc                                 |

## addLoc details

- **Size swap:** When angle is NORTH or SOUTH, both client and C swap `size_x`/`size_z` before filling the rectangle.
- **Rectangle:** Fill `[tile_x, tile_x+size_x)` × `[tile_z, tile_z+size_z)` with LOC (and optionally LOC_PROJ_BLOCKER).
- **Bounds:** Client skips out-of-range (tx,tz); C `collision_map_add` returns without writing when out of bounds.

## addWall details

- **Angle:** 0=WEST, 1=NORTH, 2=EAST, 3=SOUTH (`orientation & 0x3`).
- **blockrange:** When true, both add PROJ wall flags then call addWall again with blockrange=false to add normal wall flags.
- **Shapes:** WALL_STRAIGHT, WALL_DIAGONAL_CORNER, WALL_SQUARE_CORNER (diagonal corners), WALL_L (two sides). Logic in `collision_map.c` matches `CollisionMap.ts` addWall/delWall.

## BFS / BLOCK_* flags

- **Client:** Steps to (x-1,z) check `(flags[x-1,z] & BLOCK_WEST) === OPEN`; BLOCK_WEST = WALL_EAST | WALK_BLOCKED. So destination must not have east wall (so we can enter from east).
- **C:** Same composite flags and checks in `collision_map_bfs_path` (TRY_NBOR with COLL_FLAG_BLOCK_*).
- **Index:** Client `CollisionMap.index(x,z) = x * BuildArea.SIZE + z`. C uses `x * cm->size_z + z` everywhere; `collision_map_index()` in the header uses COLLISION_SIZE (104) and is only correct when map width is 104.

## Possible differences

1. **GROUND_DECOR:** Client requires `loc.blockwalk && loc.active`; C only has `clip_type`. Inactive decor may be blocked in C but not in client.
2. **add_loc size when no buildcachedat:** C forces `sx=sz=1`; client uses `loc.width`/`loc.length`. If buildcachedat is missing, C may under-fill large locs.

## Checklist when changing collision

- [ ] Keep shape/angle/action table above in sync with ClientBuild.addLoc and CollisionMap.
- [ ] Keep add_loc size swap (NORTH/SOUTH) and rectangle range identical to Client-TS.
- [ ] Keep add_wall shape and angle handling identical to CollisionMap.addWall.
- [ ] Keep BLOCK_* composite flags and BFS step checks aligned with Client.ts pathfinding.
