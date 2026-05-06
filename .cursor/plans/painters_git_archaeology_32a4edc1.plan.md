---
name: Painters git archaeology
overview: Focus on the **interaction of VisBelow (0x08) and LinkBelow (0x02)** — in **[`ClientBuild.getVisBelowLevel`](Client-TS/src/dash3d/ClientBuild.ts)** they are **one formula**: VisBelow on that cache tile forces draw tier **0**; else, if the column has LinkBelow on **cache level 1**, non-zero cache levels map to **level−1** for draw tier. That matches **[`map_floor_vis_below_draw_level`](src/osrs/rscache/tables/maps.h)** + **`ce4af6f3`** per-grid **src** mapping. **Loc spans** should still match **[`World.setSprite`](Client-TS/src/dash3d/World.ts)** bit-for-bit; the likely bug is using **cache `chunk_pos_level`** for **`painter_tile_at`** when **LinkBelow** has already **moved** that floor’s content to a **different painter grid slot**, while **VisBelow** only affects **packed slevel** / **draw_mask** — wrong combo breaks span adjacency on bridge+vis columns.
todos:
  - id: verify-span-bits
    content: Diff Client-TS World.setSprite span loop vs compute_normal_scenery_spans (tx/tz edge tests ↔ SPAN_FLAG_*); confirm identical bitmask semantics
    status: pending
  - id: verify-span-grid-index
    content: For LinkBelow columns, derive cache_level→painter grid slot (inverse of ce4af6f3 src mapping) and check whether painter_add_normal_scenery should use grid index vs map chunk_pos_level for painter_tile_at in spans
    status: pending
  - id: confirm-visbelow-separation
    content: Confirm TS stores VisBelow on tile.drawLevel while sprites stay on levelTiles[cacheLevel]; C should attach spans to tiles keyed like TS level index, not draw-only slevel
    status: pending
  - id: repro-both-flags
    content: Repro on map columns with LinkBelow@L1 and VisBelow on at least one floor; compare draw_level pass vs loc span grid index (cache vs painter grid) for the same (x,z)
    status: pending
isProject: false
---

# Loc spans vs Client.ts (and VisBelow)

## Reference implementation (Client-TS)

In **[`World.setSprite`](Client-TS/src/dash3d/World.ts)** (~1220–1263), for each footprint cell **`(tx,tz)`**:

- **`level`** is passed through from **`addScenery(level, …)`** → **`ClientBuild`** uses the map’s **terrain/loc cache level** for loc placement (same **`level`** as **`world?.addScenery(level, …)`** in **[`ClientBuild.ts`](Client-TS/src/dash3d/ClientBuild.ts)** ~1200).
- Span bits (then **`tile.spriteSpan`** / **`tile.spriteSpans`**) are computed as:

  - **`tx > tileX`** → `0x1`
  - **`tx < tileX + tileSizeX - 1`** → `+ 0x4`
  - **`tz > tileZ`** → `+ 0x8`
  - **`tz < tileZ + tileSizeZ - 1`** → `+ 0x2`

These correspond to the same compass edges as **`SPAN_FLAG_WEST/NORTH/EAST/SOUTH`** in **[`compute_normal_scenery_spans`](src/osrs/painters.c)** (~762–783): interior edges wait on the neighbor underlay before drawing the loc.

Sprites are stored on **`this.levelTiles[level][tx][tz]`** — **cache/stack index `level`**, not **`drawLevel`**.

**VisBelow + LinkBelow (same function in TS):** [`getVisBelowLevel`](Client-TS/src/dash3d/ClientBuild.ts) (~1111–1116) — if **VisBelow** on **`(level, stx, stz)`** → return **0**; else if **level > 0** and **`mapl[1][stx][stz] & LinkBelow`** → return **level − 1**; else return **level**. So “vis below” and “link below” are **not independent** for draw tier: LinkBelow on L1 is the **precondition** for the **level−1** branch. **[`ClientBuild`](Client-TS/src/dash3d/ClientBuild.ts)** then calls **`world.setLayer(level, stx, stz, getVisBelowLevel(...))`** (~327), which only sets **`Square.drawLevel`**; it does **not** change **`levelTiles[level]`** for sprite storage.

## C implementation today

- **[`compute_normal_scenery_spans`](src/osrs/painters.c)** uses the same edge rules as TS for **`min_tile_x/z` … `max_tile_exclusive_*`**.
- **[`painter_add_normal_scenery`](src/osrs/painters.c)** is called with **`entity->scene_coord.slevel`**, set from **`map_tile->chunk_pos_level`** in **[`world.c`](src/osrs/world.c)** (~1406) when building loc entities — i.e. **cache terrain level** from the map.

So **per-tile span bit math** should already match **Client.ts** if naming/conventions align.

## Most probable gap (VisBelow **and** LinkBelow **together** + spans)

Two different concepts must stay straight:

1. **Draw tier / visibility** — **`getVisBelowLevel`** vs **`map_floor_vis_below_draw_level`** (+ **`ce4af6f3`** **`src`** mapping per grid slot after push-down). Handles **VisBelow** overriding LinkBelow’s **level−1** reduction on specific tiles, and sets packed **`slevel`** for **`draw_mask`**.

2. **Where spans attach in the painter grid** — **`compute_normal_scenery_spans`** uses **`painter_tile_at(..., loc_level)`** with **`loc_level = chunk_pos_level`**. After **`world_rebuild_centerzone_end`** LinkBelow **copies** tiles between **grid** indices; **cache level L** terrain may live at **grid g ≠ L**. That mismatch hurts most when **both** flags matter: e.g. bridge column (**LinkBelow**) plus a floor forcing draw tier 0 (**VisBelow**) — inclusion may look fixed (**ce4af6f3**) while **scenery links** still target tiles by **cache index**.

**Concrete check:** For **`(sx,sz)`** with **`mapl[1]&LinkBelow`**, invert **`src` ↔ grid `g`** from **`ce4af6f3`**: map **`chunk_pos_level` → g** for **`painter_tile_at`** in **`compute_normal_scenery_spans`** / **`painter_add_normal_scenery`**, and confirm it matches how **`levelTiles[cacheLevel]`** is interpreted relative to the client’s **pushDown** ordering for that column.

## Related commits (span / level semantics)

| Commit | Relevance to spans / levels |
|--------|------------------------------|
| **`ce4af6f3`** | Sets **per-tile draw slevel** from VisBelow/LinkBelow; does not by itself retarget **which grid slot** loc spans attach to. |
| **`60c2ca5e`** | **terrain_level** vs **grid_level** on tiles — same coordinate system issue class as span grid index. |
| **`00883691`** | Early bridge / painter tile semantics. |

## `painter_paint_world3d` note

If **world3d** looks correct while **bucket/distancemetric** do not, that can still be consistent with **wrong span linkage**: different traversals stress **scenery span dependencies** differently. Prioritize **span grid index** parity with **Client.ts `levelTiles[level]`** before further backend diffing.

## Git drill-down for spans

- `git log -p -S'compute_normal_scenery_spans' -- src/osrs/painters.c`
- `git log -p -S'spriteSpan' -- Client-TS/src/dash3d/World.ts`
- `git log -p -S'chunk_pos_level' -- src/osrs/world.c src/osrs/world_scenery.u.c`
