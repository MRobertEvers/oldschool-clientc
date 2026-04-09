---
name: Painter's Wavefront Restructure
overview: Replace the FIFO queue-based painter's algorithm with a deterministic shell-based wavefront scheduler that processes tiles by Chebyshev distance from the camera, ensuring all four corners stay synchronized regardless of camera position or large loc blocking.
todos:
  - id: shell-precomp
    content: "Implement ShellTable pre-computation: compute Chebyshev distances, sort tiles by shell, build shell_offsets array. Add quadrant-aware ordering within each shell (NW, NE, SW, SE)."
    status: completed
  - id: loc-precomp
    content: "Implement LocDrawInfo pre-computation: for each visible loc, compute inner_shell (min Chebyshev distance of footprint tiles) and initialize remaining_ground counter."
    status: completed
  - id: frame-counter
    content: Replace ElementPaint memset with frame-counter dedup (drawn_frame vs paint_frame comparison).
    status: cancelled
  - id: ground-phase
    content: "Implement Phase 1 (ground drawing) in the wavefront loop: iterate shell tiles, draw terrain/far walls/decor/ground objects. After each ground draw, decrement remaining_ground on relevant deferred locs."
    status: completed
  - id: deferred-retry
    content: "Implement Phase 2 (deferred loc retry + level catch-up): when a deferred loc's remaining_ground hits 0, draw it, finish footprint tiles' near walls, then eagerly process higher levels at those tiles."
    status: completed
  - id: loc-phase
    content: "Implement Phase 3 (loc drawing for current shell): draw 1x1 locs immediately, defer multi-tile locs with pending footprint grounds. Use pre-sorted loc list or insertion sort."
    status: completed
  - id: near-wall-phase
    content: "Implement Phase 4 (near wall drawing): draw near walls/decor for tiles at current shell whose locs are all done."
    status: completed
  - id: level-interleave
    content: "Interleave levels within the shell loop (staircase): at each shell d, process level 0 then level 1 etc. Higher levels trail by at most 1 shell. After a deferred loc resolves, eagerly catch up all higher levels at that tile."
    status: completed
  - id: alloc-and-free
    content: Expand painter_new() to allocate all wavefront scratch buffers (shell_tiles, tile_flags, deferred_locs, sorted_locs). Expand painter_free() to free them. Both paths coexist.
    status: completed
  - id: declare-paint2
    content: Add painter_paint2 declaration to painters.h with same signature as painter_paint. Add wavefront fields and DeferredLoc struct to the header.
    status: completed
isProject: false
---

# Painter's Algorithm: Wavefront Restructure

## Summary

Write `painter_paint2` in [src/osrs/painters.c](src/osrs/painters.c) as a complete alternative to `painter_paint`. Add supporting structs to [src/osrs/painters.h](src/osrs/painters.h). Expand `painter_new` / `painter_free` to allocate all new buffers. Do not modify `painter_paint` or any other existing function.

## Files Changed

- [src/osrs/painters.h](src/osrs/painters.h) -- Add `DeferredLoc` struct. Add wavefront fields to `Painter` struct. Declare `painter_paint2`.
- [src/osrs/painters.c](src/osrs/painters.c) -- Expand `painter_new` / `painter_free`. Add 4 small helper functions. Add `painter_paint2`.

---

## Step 1: painters.h changes

### Add `DeferredLoc` struct (add after `ElementPaint`):

```c
struct DeferredLoc {
    int16_t element_idx;       /* index into painter->elements */
    int16_t remaining_ground;  /* footprint tiles still needing WF_GROUND */
    uint8_t level;
};
```

### Add wavefront fields to `Painter` struct (add after `catchup_queue`):

```c
/* painter_paint2 wavefront data -- allocated in painter_new */
int*               wf_shell_tiles;       /* level-0 base tile indices, sorted outer->inner shell */
int                wf_shell_offsets[52]; /* wf_shell_tiles start index for each shell (52 = max_radius+2) */
int                wf_shell_count;       /* number of distinct shell distances */
uint8_t*           wf_tile_flags;        /* per tile_idx: WF_GROUND | WF_DONE */
struct DeferredLoc* wf_deferred_locs;   /* scratch: locs awaiting footprint ground */
int                wf_deferred_count;
```

### Add flag constants (add after `SpanFlag` enum):

```c
enum WfTileFlag {
    WF_GROUND = 1 << 0, /* terrain + ground items drawn for this tile */
    WF_DONE   = 1 << 1, /* all locs + near walls done; tile complete */
};
```

### Add declaration at bottom of painters.h:

```c
int painter_paint2(struct Painter* painter, struct PaintersBuffer* buffer,
                   int camera_sx, int camera_sz, int camera_slevel);
```

---

## Step 2: painter_new additions

After the existing `int_queue_init` lines, add:

```c
painter->wf_shell_tiles   = malloc(tile_count * sizeof(int));
painter->wf_tile_flags    = malloc(tile_count * sizeof(uint8_t));
painter->wf_deferred_locs = malloc(1024 * sizeof(struct DeferredLoc));
painter->wf_deferred_count = 0;
painter->wf_shell_count   = 0;
```

## Step 3: painter_free additions

After the existing `int_queue_free` lines, add:

```c
free(painter->wf_shell_tiles);
free(painter->wf_tile_flags);
free(painter->wf_deferred_locs);
```

---

## Step 4: Helper functions (add before painter_paint2)

### 4a. `wf_build_shell_table`

Populates `painter->wf_shell_tiles` and `painter->wf_shell_offsets`. Stores level-0 base indices (`painter_coord_idx(painter, sx, sz, 0)`). Shell `d` tiles span `[wf_shell_offsets[d], wf_shell_offsets[d+1])`. A sentinel is stored at `wf_shell_offsets[max_dist+1]` = total tile count.

```c
static void
wf_build_shell_table(
    struct Painter* painter,
    int camera_sx, int camera_sz,
    int min_draw_x, int max_draw_x,
    int min_draw_z, int max_draw_z)
{
    /* Find maximum shell distance (farthest corner). */
    int dx0 = camera_sx - min_draw_x;   if (dx0 < 0) dx0 = -dx0;
    int dx1 = (max_draw_x-1) - camera_sx; if (dx1 < 0) dx1 = -dx1;
    int dz0 = camera_sz - min_draw_z;   if (dz0 < 0) dz0 = -dz0;
    int dz1 = (max_draw_z-1) - camera_sz; if (dz1 < 0) dz1 = -dz1;
    int max_dist = dx0 > dx1 ? dx0 : dx1;
    if (dz0 > max_dist) max_dist = dz0;
    if (dz1 > max_dist) max_dist = dz1;

    /* Count tiles per shell. */
    int counts[52];
    memset(counts, 0, (max_dist + 1) * sizeof(int));
    for (int sx = min_draw_x; sx < max_draw_x; sx++) {
        for (int sz = min_draw_z; sz < max_draw_z; sz++) {
            int dx = sx - camera_sx; if (dx < 0) dx = -dx;
            int dz = sz - camera_sz; if (dz < 0) dz = -dz;
            int d = dx > dz ? dx : dz;
            counts[d]++;
        }
    }

    /* Build offsets: shell max_dist at index 0, shell 0 last.
       wf_shell_offsets[d] = start of shell d in wf_shell_tiles.
       wf_shell_offsets[max_dist+1] = total (sentinel). */
    int running = 0;
    for (int d = max_dist; d >= 0; d--) {
        painter->wf_shell_offsets[d] = running;
        running += counts[d];
    }
    painter->wf_shell_offsets[max_dist + 1] = running; /* sentinel */
    painter->wf_shell_count = max_dist + 1;

    /* Fill wf_shell_tiles using per-shell fill pointers. */
    int fill[52];
    for (int d = 0; d <= max_dist; d++)
        fill[d] = painter->wf_shell_offsets[d];

    for (int sx = min_draw_x; sx < max_draw_x; sx++) {
        for (int sz = min_draw_z; sz < max_draw_z; sz++) {
            int dx = sx - camera_sx; if (dx < 0) dx = -dx;
            int dz = sz - camera_sz; if (dz < 0) dz = -dz;
            int d = dx > dz ? dx : dz;
            painter->wf_shell_tiles[fill[d]++] =
                painter_coord_idx(painter, sx, sz, 0);
        }
    }
}
```

### 4b. `wf_draw_ground`

Draws the ground phase for one tile at (sx, sz, level). This is a direct copy of the `PAINT_STEP_GROUND` block from `painter_paint` (lines 1134-1316), with these two changes:

1. Replace `tile_paint->near_wall_flags |= ~far_walls` (line 1139) -- omit it entirely; near_wall_flags is not used in painter_paint2.
2. Remove the span-push block at lines 1258-1316 entirely (that was for the old FIFO).
3. Replace `element_paint->drawn` checks with `== 2` and sets with `= 2`.

```c
static void
wf_draw_ground(
    struct Painter* painter,
    struct PaintersBuffer* buffer,
    int sx, int sz, int level,
    int camera_sx, int camera_sz)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, level);
    struct PaintersElement* element;
    struct ElementPaint* element_paint;
    int far_walls = far_wall_flags(camera_sx, camera_sz, sx, sz);

    if (tile->bridge_tile != -1) {
        struct PaintersTile* bt = &painter->tiles[tile->bridge_tile];
        push_command_terrain(buffer,
            PAINTER_TILE_X(painter, bt),
            PAINTER_TILE_Z(painter, bt),
            painters_tile_get_terrain_slevel(bt));
        if (bt->wall_a != -1) {
            element = &painter->elements[bt->wall_a];
            push_command_entity(buffer, element->_wall.entity);
        }
        for (int32_t sn = bt->scenery_head; sn != -1;
             sn = painter->scenery_pool[sn].next) {
            int se = painter->scenery_pool[sn].element_idx;
            element_paint = &painter->element_paints[se];
            if (element_paint->drawn == 2) continue;
            element_paint->drawn = 2;
            element = &painter->elements[se];
            push_command_entity(buffer, element->_scenery.entity);
        }
    }

    push_command_terrain(buffer, sx, sz, painters_tile_get_terrain_slevel(tile));

    if (tile->wall_a != -1) {
        element = &painter->elements[tile->wall_a];
        if ((element->_wall.side & far_walls) != 0)
            push_command_entity(buffer, element->_wall.entity);
    }
    if (tile->wall_b != -1) {
        element = &painter->elements[tile->wall_b];
        if ((element->_wall.side & far_walls) != 0)
            push_command_entity(buffer, element->_wall.entity);
    }
    if (tile->ground_decor != -1) {
        element = &painter->elements[tile->ground_decor];
        push_command_entity(buffer, element->_ground_decor.entity);
    }
    if (tile->ground_object_bottom != -1) {
        element = &painter->elements[tile->ground_object_bottom];
        push_command_entity(buffer, element->_ground_object.entity);
    }
    if (tile->ground_object_middle != -1) {
        element = &painter->elements[tile->ground_object_middle];
        push_command_entity(buffer, element->_ground_object.entity);
    }
    if (tile->ground_object_top != -1) {
        element = &painter->elements[tile->ground_object_top];
        push_command_entity(buffer, element->_ground_object.entity);
    }

    /* Far wall decor -- copy lines 1216-1251 of painter_paint verbatim here. */
    /* (The throughwall logic and the non-throughwall else branch.) */
}
```

### 4c. `wf_deferred_decrement`

Called right after `wf_tile_flags[tile_idx] |= WF_GROUND`. Decrements `remaining_ground` for every deferred loc whose footprint contains (sx, sz) at the given level.

```c
static void
wf_deferred_decrement(struct Painter* painter, int sx, int sz, int level)
{
    for (int i = 0; i < painter->wf_deferred_count; i++) {
        struct DeferredLoc* dl = &painter->wf_deferred_locs[i];
        if (dl->level != level) continue;
        struct PaintersElement* el = &painter->elements[dl->element_idx];
        if (sx < el->sx || sx >= el->sx + el->_scenery.size_x) continue;
        if (sz < el->sz || sz >= el->sz + el->_scenery.size_z) continue;
        dl->remaining_ground--;
    }
}
```

### 4d. `wf_draw_near_walls`

Copy the `PAINT_STEP_NEAR_WALL` block from `painter_paint` (lines 1536-1599) verbatim. Replace `tile_paint->near_wall_flags` with `~far_wall_flags(camera_sx, camera_sz, sx, sz)`.

```c
static void
wf_draw_near_walls(
    struct Painter* painter,
    struct PaintersBuffer* buffer,
    int sx, int sz, int level,
    int camera_sx, int camera_sz)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, level);
    struct PaintersElement* element;
    int near_walls = ~far_wall_flags(camera_sx, camera_sz, sx, sz);

    /* Copy lines 1538-1598 of painter_paint verbatim,
       replacing tile_paint->near_wall_flags with near_walls. */
}
```

### 4e. `wf_all_locs_drawn`

Returns true if every scenery element on a tile's `scenery_head` list has `drawn == 2`.

```c
static bool
wf_all_locs_drawn(struct Painter* painter, int sx, int sz, int level)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, level);
    for (int32_t sn = tile->scenery_head; sn != -1;
         sn = painter->scenery_pool[sn].next) {
        int ei = painter->scenery_pool[sn].element_idx;
        if (painter->element_paints[ei].drawn != 2) return false;
    }
    return true;
}
```

---

## Step 5: `painter_paint2`

Use the same local variable setup as `painter_paint` (radius=25, max_level=4, same min/max_draw_x/z clamping). `level_stride = painter->width * painter->height` converts a level-0 base index to a level-N index by adding `N * level_stride`.

```c
int
painter_paint2(struct Painter* painter, struct PaintersBuffer* buffer,
               int camera_sx, int camera_sz, int camera_slevel)
{
    (void)camera_slevel;
    buffer->command_count = 0;

    const int radius    = 25;
    const int max_level = 4;

    int min_draw_x = camera_sx - radius; if (min_draw_x < 0) min_draw_x = 0;
    int min_draw_z = camera_sz - radius; if (min_draw_z < 0) min_draw_z = 0;
    int max_draw_x = camera_sx + radius; if (max_draw_x > painter->width)  max_draw_x = painter->width;
    int max_draw_z = camera_sz + radius; if (max_draw_z > painter->height) max_draw_z = painter->height;
    if (min_draw_x >= max_draw_x || min_draw_z >= max_draw_z) return 0;

    memset(painter->wf_tile_flags,  0, painter->tile_capacity * sizeof(uint8_t));
    memset(painter->element_paints, 0, painter->element_count  * sizeof(struct ElementPaint));
    painter->wf_deferred_count = 0;

    wf_build_shell_table(painter, camera_sx, camera_sz,
                         min_draw_x, max_draw_x, min_draw_z, max_draw_z);

    int max_dist     = painter->wf_shell_count - 1;
    int level_stride = painter->width * painter->height;

    for (int d = max_dist; d >= 0; d--) {
        int shell_start = painter->wf_shell_offsets[d];
        int shell_end   = painter->wf_shell_offsets[d + 1]; /* sentinel ensures this is valid */

        for (int level = 0; level < max_level; level++) {

            /* ---- Phase 1: Ground ---- */
            for (int i = shell_start; i < shell_end; i++) {
                int base_idx = painter->wf_shell_tiles[i];
                int tile_idx = base_idx + level * level_stride;
                int sx = base_idx % painter->width;
                int sz = (base_idx / painter->width) % painter->height;

                struct PaintersTile* tile = &painter->tiles[tile_idx];
                if ((painters_tile_get_flags(tile) & PAINTERS_TILE_FLAG_BRIDGE) != 0
                    || painters_tile_get_slevel(tile) > max_level)
                    continue;

                /* Staircase: level N requires level N-1 DONE at this tile. */
                if (level > 0) {
                    int lower_idx = base_idx + (level - 1) * level_stride;
                    if (!(painter->wf_tile_flags[lower_idx] & WF_DONE)) continue;
                }

                if (painter->wf_tile_flags[tile_idx] & WF_GROUND) continue;

                wf_draw_ground(painter, buffer, sx, sz, level, camera_sx, camera_sz);
                painter->wf_tile_flags[tile_idx] |= WF_GROUND;
                wf_deferred_decrement(painter, sx, sz, level);
            }

            /* ---- Phase 2: Retry deferred locs with remaining_ground == 0 ---- */
            for (int i = 0; i < painter->wf_deferred_count; ) {
                struct DeferredLoc* dl = &painter->wf_deferred_locs[i];
                if (dl->level != level || dl->remaining_ground != 0) { i++; continue; }

                struct PaintersElement* el = &painter->elements[dl->element_idx];
                push_command_entity(buffer, el->_scenery.entity);
                painter->element_paints[dl->element_idx].drawn = 2;

                /* Remove from list (swap-with-last). */
                painter->wf_deferred_locs[i] =
                    painter->wf_deferred_locs[--painter->wf_deferred_count];
                /* Do NOT increment i -- recheck the swapped entry. */

                /* Try to complete footprint tiles and catch up higher levels. */
                int lx0 = el->sx, lz0 = el->sz;
                int lx1 = lx0 + el->_scenery.size_x - 1;
                int lz1 = lz0 + el->_scenery.size_z - 1;
                if (lx0 < min_draw_x) lx0 = min_draw_x;
                if (lz0 < min_draw_z) lz0 = min_draw_z;
                if (lx1 > max_draw_x - 1) lx1 = max_draw_x - 1;
                if (lz1 > max_draw_z - 1) lz1 = max_draw_z - 1;

                for (int fx = lx0; fx <= lx1; fx++) {
                    for (int fz = lz0; fz <= lz1; fz++) {
                        int fi = painter_coord_idx(painter, fx, fz, level);
                        if (painter->wf_tile_flags[fi] & WF_DONE) continue;
                        if (!wf_all_locs_drawn(painter, fx, fz, level)) continue;

                        wf_draw_near_walls(painter, buffer, fx, fz, level,
                                           camera_sx, camera_sz);
                        painter->wf_tile_flags[fi] |= WF_DONE;

                        /* Catch up higher levels at this tile. */
                        for (int cl = level + 1; cl < max_level; cl++) {
                            int ci = painter_coord_idx(painter, fx, fz, cl);
                            struct PaintersTile* ct = &painter->tiles[ci];
                            if ((painters_tile_get_flags(ct) & PAINTERS_TILE_FLAG_BRIDGE) != 0
                                || painters_tile_get_slevel(ct) > max_level) break;
                            if (painter->wf_tile_flags[ci] & WF_DONE) break;
                            if (!(painter->wf_tile_flags[ci] & WF_GROUND)) {
                                wf_draw_ground(painter, buffer, fx, fz, cl,
                                               camera_sx, camera_sz);
                                painter->wf_tile_flags[ci] |= WF_GROUND;
                                wf_deferred_decrement(painter, fx, fz, cl);
                            }
                            if (!wf_all_locs_drawn(painter, fx, fz, cl)) break;
                            wf_draw_near_walls(painter, buffer, fx, fz, cl,
                                               camera_sx, camera_sz);
                            painter->wf_tile_flags[ci] |= WF_DONE;
                        }
                    }
                }
            }

            /* ---- Phase 3: Locs ---- */
            for (int i = shell_start; i < shell_end; i++) {
                int base_idx = painter->wf_shell_tiles[i];
                int tile_idx = base_idx + level * level_stride;
                int sx = base_idx % painter->width;
                int sz = (base_idx / painter->width) % painter->height;

                if (!(painter->wf_tile_flags[tile_idx] & WF_GROUND)) continue;
                if (  painter->wf_tile_flags[tile_idx] & WF_DONE)    continue;

                struct PaintersTile* tile = &painter->tiles[tile_idx];
                for (int32_t sn = tile->scenery_head; sn != -1;
                     sn = painter->scenery_pool[sn].next) {
                    int ei = painter->scenery_pool[sn].element_idx;
                    if (painter->element_paints[ei].drawn != 0) continue;

                    struct PaintersElement* el = &painter->elements[ei];

                    int lx0 = el->sx, lz0 = el->sz;
                    int lx1 = lx0 + el->_scenery.size_x - 1;
                    int lz1 = lz0 + el->_scenery.size_z - 1;
                    if (lx0 < min_draw_x) lx0 = min_draw_x;
                    if (lz0 < min_draw_z) lz0 = min_draw_z;
                    if (lx1 > max_draw_x - 1) lx1 = max_draw_x - 1;
                    if (lz1 > max_draw_z - 1) lz1 = max_draw_z - 1;

                    int missing = 0;
                    for (int fx = lx0; fx <= lx1; fx++)
                        for (int fz = lz0; fz <= lz1; fz++) {
                            int fi = painter_coord_idx(painter, fx, fz, level);
                            if (!(painter->wf_tile_flags[fi] & WF_GROUND)) missing++;
                        }

                    if (missing == 0) {
                        push_command_entity(buffer, el->_scenery.entity);
                        painter->element_paints[ei].drawn = 2;
                    } else {
                        painter->element_paints[ei].drawn = 1; /* deferred */
                        painter->wf_deferred_locs[painter->wf_deferred_count++] =
                            (struct DeferredLoc){ .element_idx      = (int16_t)ei,
                                                  .remaining_ground = (int16_t)missing,
                                                  .level            = (uint8_t)level };
                    }
                }
            }

            /* ---- Phase 4: Near walls ---- */
            for (int i = shell_start; i < shell_end; i++) {
                int base_idx = painter->wf_shell_tiles[i];
                int tile_idx = base_idx + level * level_stride;
                int sx = base_idx % painter->width;
                int sz = (base_idx / painter->width) % painter->height;

                if (!(painter->wf_tile_flags[tile_idx] & WF_GROUND)) continue;
                if (  painter->wf_tile_flags[tile_idx] & WF_DONE)    continue;
                if (!wf_all_locs_drawn(painter, sx, sz, level))       continue;

                wf_draw_near_walls(painter, buffer, sx, sz, level, camera_sx, camera_sz);
                painter->wf_tile_flags[tile_idx] |= WF_DONE;
            }

        } /* level loop */
    } /* shell loop */

    return 0;
}
```

---

## Key Implementation Notes

### Shell offset sentinel

`wf_shell_offsets[max_dist + 1]` is set to the total tile count during `wf_build_shell_table`. The array is sized 52 (radius 25 → max_dist ≤ 50 → index 51 is safe). The main loop uses `shell_end = wf_shell_offsets[d + 1]` without any special-casing.

### Element `drawn` states used in painter_paint2

`painter_paint2` reuses `element_paints[ei].drawn` (the existing field) with three values:

- `0` -- not yet encountered this frame (memset to 0 at top of function)
- `1` -- deferred (entry exists in `wf_deferred_locs`)
- `2` -- drawn (`push_command_entity` was called)

### Level stride

`level_stride = painter->width * painter->height`. Level-N tile index = `wf_shell_tiles[i] + N * level_stride`.

### Copying the wall decor throughwall block

In `wf_draw_ground`, the far wall decor block (lines 1216-1251 of `painter_paint`) should be copied verbatim. In `wf_draw_near_walls`, lines 1538-1598 copied verbatim with `tile_paint->near_wall_flags` replaced by the local `near_walls` variable.
