# Occluder System in `dash3d`

The occluder system is a visibility optimization that prevents the renderer from drawing scene elements (tiles, walls, decorations) that are hidden behind large opaque surfaces like walls, roofs, and floors. It works in three phases: **marking**, **building**, and **testing**.

## Phase 1: Marking the `mapo` Bitfield

The `mapo` array (`Int32Array[level][x][z]`) in `ClientBuild` is the central bitfield that records which tiles contain occluding geometry. It uses a 12-bit scheme packed across 4 levels, with 3 bits per level:

| Bit Purpose | Bit Pattern (level 0) | Meaning |
|---|---|---|
| `wall0` | `0x1` | Wall aligned to X-axis (angles West/East) |
| `wall1` | `0x2` | Wall aligned to Z-axis (angles North/South) |
| `floor` | `0x4` | Flat floor tile that fully covers its square |

Each level shifts these 3 bits left by 3 positions:

```
Level 0:  wall0=0x001  wall1=0x002  floor=0x004
Level 1:  wall0=0x008  wall1=0x010  floor=0x020
Level 2:  wall0=0x040  wall1=0x080  floor=0x100
Level 3:  wall0=0x200  wall1=0x400  floor=0x800
```

The composite constants used in code:

- `0x249 = 0x001 | 0x008 | 0x040 | 0x200` — wall0 on all levels
- `0x492 = 0x002 | 0x010 | 0x080 | 0x400` — wall1 on all levels
- `0x924 = 0x004 | 0x020 | 0x100 | 0x800` — floor on all levels

### Sources of marking in `ClientBuild.addLoc`

1. **Straight walls** (`WALL_STRAIGHT`, shape 0): If `loc.occlude` is true, set `wall0` or `wall1` bits depending on rotation. For example, a west-facing wall at `[x][z]` sets `0x249` (wall0), while a north-facing wall sets `0x492` at `[x][z+1]`.

2. **L-shaped walls** (`WALL_L`, shape 2): Sets bits for both wall segments based on the two rotation angles.

3. **Roof shapes** (shapes 12–16, excluding 15): Always set `0x924` (floor bits on all levels) at their tile position.

4. **Flat floor tiles**: During `finishBuild`, if a tile on level > 0 is fully covered by an underlay (or a non-transparent overlay) AND has all four corners at the same height, it sets `0x924`.

## Phase 2: Building Occluders from Marked Tiles

In the second half of `ClientBuild.finishBuild`, the engine scans the `mapo` bitfield and constructs `Occlude` objects by flood-filling contiguous regions of marked tiles. For each level (`topLevel`), it processes three categories:

### Wall0 (X-aligned walls)

Starting from a marked tile, it expands along the Z-axis and across vertical levels to find the largest contiguous rectangle of `wall0`-flagged tiles. If the area is >= 8, it creates a **type 1** occluder (an X-constant plane):

```typescript
World.setOcclude(topLevel, 1, tileX * 128, minY, minTileZ * 128, tileX * 128, maxX, maxTileZ * 128 + 128);
```

### Wall1 (Z-aligned walls)

Same process expanding along the X-axis. Creates a **type 2** occluder (a Z-constant plane):

```typescript
World.setOcclude(topLevel, 2, minTileX * 128, minY, tileZ * 128, maxTileX * 128 + 128, maxY, tileZ * 128);
```

### Floors

Expands along both X and Z axes on a single level. Minimum area threshold is 4 tiles. Creates a **type 4** occluder (a Y-constant plane):

```typescript
World.setOcclude(topLevel, 4, minTileX * 128, y, minTileZ * 128, maxTileX * 128 + 128, y, maxTileZ * 128 + 128);
```

After creating each occluder, the consumed bits are cleared (`&= ~flag`) so they aren't reprocessed.

## Phase 3: Runtime Occlusion Testing

At the start of each draw call, `updateActiveOccluders()` selects which occluders are relevant for the current camera position. It filters by:

- Whether the occluder overlaps any visible tile in the `visibilityMap`
- Whether the camera is on the correct side (and far enough away, > 32 units)

Each active occluder gets assigned a **mode** (1–5) based on its type and which side of it the camera is on:

| Mode | Occluder Type | Camera Position |
|---|---|---|
| 1 | Type 1 (X-plane) | Camera east of the plane |
| 2 | Type 1 (X-plane) | Camera west of the plane |
| 3 | Type 2 (Z-plane) | Camera south of the plane |
| 4 | Type 2 (Z-plane) | Camera north of the plane |
| 5 | Type 4 (Y-plane/floor) | Camera below the plane |

Delta values are precomputed as fixed-point perspective projections from the camera to the occluder plane edges.

### Point Occlusion Test

The `occluded(x, y, z)` method tests whether a 3D point is behind any active occluder. It projects the point onto the occluder's plane using the precomputed deltas and checks if the point falls within the perspectively-scaled bounding rectangle.

This is used by three visibility checks during rendering:

1. **`tileVisible`**: Tests all 4 corners of a ground tile. If all corners are occluded, the entire tile is skipped (with caching via `levelTileOcclusionCycles`).

2. **`isTileSideOccluded`**: Tests wall surfaces. Samples points at the ground level, 120 units up (if multi-level), and 230 units up (approximate wall top). The `type` parameter uses the wall shape flags (`1`/`2`/`4`/`8` for straight walls, `16`/`32`/`64`/`128` for diagonal corners) from `WSHAPE0`/`WSHAPE1` to determine which tile edge to test.

3. **`isTileColumnOccluded`**: Tests wall decorations and vertical elements by checking if all 4 corners at a given height above the tile are occluded.

## Key Files

| File | Role |
|---|---|
| `Occlude.ts` | Data class holding occluder bounds, type, mode, and precomputed deltas |
| `ClientBuild.ts` | Marks `mapo` bitfield during map load; builds occluders in `finishBuild` |
| `World3D.ts` / `World.ts` | Stores occluders per level; runs `updateActiveOccluders` and `occluded` tests at render time |
| `LocType.ts` | `occlude` property (opcode 23) controls whether a wall contributes to occlusion |
| `FloType.ts` | `occlude` property (default true, opcode 5 disables) controls whether a floor overlay blocks occlusion |

## Summary

The system is a conservative frustum-culling optimization using axis-aligned planar occluders:

1. During map load, walls/roofs/floors are flagged in a per-tile bitfield (`mapo`) using 3 bits per level (wall-X, wall-Z, floor).
2. Contiguous regions of flagged tiles are merged into large planar `Occlude` objects.
3. At render time, occluders facing the camera are activated and their bounds are perspective-projected.
4. Points in the scene are tested against these projected bounds to skip drawing hidden geometry.
