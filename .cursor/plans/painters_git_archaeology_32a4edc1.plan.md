---
name: Painters git archaeology
overview: Treat **loc/scenery span calculation** as the primary hypothesis: match **[`World.setSprite`](Client-TS/src/dash3d/World.ts)** (lines ~1233–1260) to **[`compute_normal_scenery_spans`](src/osrs/painters.c)** for footprint edge bits. Separately, ensure the **vertical index** used for **`painter_tile_at`** when registering spans matches what the client uses for **`levelTiles[level]`** — after **LinkBelow** push-down, that may be **painter grid level**, not raw **`chunk_pos_level`**. VisBelow (**`getVisBelowLevel`**) maps to **`Square.drawLevel`** / packed **slevel** and must **not** replace the stack index used for span attachment the way TS keeps **`setSprite(..., level, ...)`** on **cache level** while **`setLayer`** stores draw tier.
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

**VisBelow:** **[`ClientBuild`](Client-TS/src/dash3d/ClientBuild.ts)** calls **`world.setLayer(level, stx, stz, this.getVisBelowLevel(level, stx, stz))`** (~327). That sets **`Square.drawLevel`** only; it does **not** move the sprite to a different **`levelTiles[…]`** index.

## C implementation today

- **[`compute_normal_scenery_spans`](src/osrs/painters.c)** uses the same edge rules as TS for **`min_tile_x/z` … `max_tile_exclusive_*`**.
- **[`painter_add_normal_scenery`](src/osrs/painters.c)** is called with **`entity->scene_coord.slevel`**, set from **`map_tile->chunk_pos_level`** in **[`world.c`](src/osrs/world.c)** (~1406) when building loc entities — i.e. **cache terrain level** from the map.

So **per-tile span bit math** should already match **Client.ts** if naming/conventions align.

## Most probable gap (VisBelow + LinkBelow + spans)

**Packed `slevel` on `PaintersTile`** (VisBelow / **`map_floor_vis_below_draw_level`**) controls **draw_mask participation**, per **[`ce4af6f3`](https://github.com/)** and **[`painters.h`](src/osrs/painters.h)** packed_meta docs.

**Hypothesis:** **`painter_tile_at(painter, x, z, loc_level)`** in **`compute_normal_scenery_spans`** must use the same **vertical index** as the client’s **`levelTiles[level]`** for that loc after **bridge push-down**. If **LinkBelow** permutes **which painter grid slot** holds **cache level L** terrain (see **`world_rebuild_centerzone_end`** in [`world.c`](src/osrs/world.c)), then **`chunk_pos_level`** may no longer equal **painter `grid_level`** for span registration — scenery nodes get chained off the **wrong PaintersTile**, so **span waits / ordering** break even when **bit patterns** match TS and **`painter_paint_world3d`** still looks acceptable in some cases.

**Concrete check:** invert the **`src` ↔ grid `g`** mapping from **`ce4af6f3`** / **`scenebuilder_apply_vis_below_draw_levels`**: given **cache level** at a column, compute **grid index `g`**; compare to **`loc_level`** passed into **`painter_add_normal_scenery`**.

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
