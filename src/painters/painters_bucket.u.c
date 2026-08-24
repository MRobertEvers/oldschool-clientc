#ifndef PAINTERS_BUCKET_U_C
#define PAINTERS_BUCKET_U_C

#include "painters_i.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Manhattan distance from camera to any tile in a 128-wide grid is in [0, 2*127]. */
#define BUCKET_DIST_RANGE (2 * 128 + 1)

/* These counters fire tens of thousands of times in a normal world frame.
 * Keep their hot-path work to register increments and cross into the perf TU
 * only once per aggregate at function exit.  Perf-disabled differential
 * builds compile the bookkeeping out entirely. */
#ifndef TORIRS_PERF_DISABLE
#define BUCKET_PERF_INCREMENT(value) ((value)++)
#else
#define BUCKET_PERF_INCREMENT(value) ((void)(value))
#endif

struct PainterBucketCtx
{
    int bucket_heads[BUCKET_DIST_RANGE];
    int bucket_max;
    int n_in_queue; /* live count of tiles currently in the bucket queue */
};

#define BM(P) ((struct PainterBucketCtx*)(P)->bucket_ctx)

static inline void
bucket_reset(struct PainterBucketCtx* w)
{
    for( int i = 0; i < BUCKET_DIST_RANGE; i++ )
        w->bucket_heads[i] = -1;
    w->bucket_max = -1;
    w->n_in_queue = 0;
}

static inline void
bucket_push(
    struct PainterBucketCtx* w,
    struct TilePaint* paints,
    int ti,
    int dist)
{
    assert(dist >= 0 && dist < BUCKET_DIST_RANGE);
    paints[ti].queue_next = w->bucket_heads[dist];
    w->bucket_heads[dist] = ti;
    paints[ti].in_queue = 1;
    if( dist > w->bucket_max )
        w->bucket_max = dist;
    w->n_in_queue++;
}

/* Pop farthest distance first; LIFO within a bucket (matches reference). */
static inline int
bucket_pop(
    struct PainterBucketCtx* w,
    struct TilePaint* paints)
{
    while( w->bucket_max >= 0 )
    {
        int head = w->bucket_heads[w->bucket_max];
        if( head < 0 )
        {
            w->bucket_max--;
            continue;
        }
        w->bucket_heads[w->bucket_max] = paints[head].queue_next;
        paints[head].in_queue = 0;
        w->n_in_queue--;
        return head;
    }
    return -1;
}

static inline int
bucket_push_if_active(
    struct PainterBucketCtx* w,
    struct TilePaint* paints,
    int ti,
    int dist,
    int64_t* perf_pushes,
    int64_t* perf_push_dedup)
{
    if( paints[ti].in_queue )
    {
        BUCKET_PERF_INCREMENT(*perf_push_dedup);
        return 0;
    }
    if( paints[ti].step == PAINT_STEP_DONE )
        return 0;
    bucket_push(w, paints, ti, dist);
    BUCKET_PERF_INCREMENT(*perf_pushes);
    if( painter_wedgelog_armed() )
    {
        char extra[32];
        snprintf(extra, sizeof(extra), "d=%d", dist);
        painter_wedgelog_event(ti, "PUSH", extra);
    }
    return 1;
}

static inline int
bucket_queue_empty(struct PainterBucketCtx* w)
{
    return w->n_in_queue == 0;
}

static int
bucket_ctx_init(struct Painter* painter)
{
    struct PainterBucketCtx* w =
        (struct PainterBucketCtx*)calloc(1, sizeof(struct PainterBucketCtx));
    if( !w )
        return -1;
    painter->bucket_ctx = w;
    return 0;
}

static void
bucket_ctx_free(struct Painter* painter)
{
    struct PainterBucketCtx* w = BM(painter);
    if( !w )
        return;
    free(w);
    painter->bucket_ctx = NULL;
}

/*
 * The seam exception to the reference adjacency gate.
 *
 * The reference rule is "a tile may not draw its ground until the neighbour
 * between it and the far edge has fully retired", with one escape: if the two
 * tiles share a loc (this tile carries a span flag pointing at the neighbour),
 * the neighbour's GROUND is enough — otherwise the loc, which waits on this
 * tile's ground, would deadlock against it.
 *
 * That escape is keyed on THIS tile's spans, so it cannot fire when the
 * neighbour is held by a loc that does not cover this tile. Two large locs
 * meeting on the camera column is exactly that case (the QBD arena floor is a
 * pair of 12x18 plane-0 locs, and `sx == camera_sx` gates on both horizontal
 * neighbours), and the wait it produces is not merely long, it is wrong: a
 * multi-tile loc is released at its NEAREST footprint tile, so a loc that
 * reaches closer to the eye than the tile being held is drawn closer than that
 * tile whatever happens. Holding the farther tile behind it inverts the sweep
 * and paints its floor over the loc.
 *
 * The first version of this relaxed the gate on EITHER axis, and that was a
 * bug (2026-08-19, ToB): a large loc also straddles rings in depth. Xarpus'
 * 6x5 ledge at x[43,48] z[67,71] seen from (50,43) has its near corner at
 * ring 26, but tile (44,66) — ring 29 — is directly in FRONT of its z=67 row;
 * relaxing (44,66)'s north gate let that floor paint first and the ledge's
 * tall far part land on top of it. Same shape for the Maiden stair landing.
 *
 * So the relaxation is now confined to the LATERAL gate: the neighbour that
 * lies to the side of the eye->tile ray, not along it. For a tile at
 * (dx, dz) from the eye, the depth axis is the one with the larger |delta|;
 * the gate across the other axis is lateral. A loc pending on a lateral
 * neighbour is beside this tile's line of sight, so its far part is not
 * behind this tile's ground the way a depth-neighbour's loc is. The QBD seam
 * column has dx == 0, so both of its x gates are lateral and still relax;
 * the Xarpus/Maiden cases gate on the depth axis and now wait, as the
 * reference does. Ties (|dx| == |dz|) are treated as depth — strict.
 *
 * And the exception frees only the tile's GROUND. A relaxed tile is flagged
 * (TilePaint.seam_relaxed) and holds its scenery and completion until the
 * plain reference gate passes: a tall loc beside the tile whose far row abuts
 * it (the Xarpus barrier next to a 6x5 ledge) must still go down first, as
 * it does in painter_paint_world3d. With both halves the bucket painter is
 * pixel-identical to world3d across every ToB room and camera tried
 * (2026-08-19, 64 views), and still keeps the seam sweep the reference gate
 * loses (test_seam_between_two_large_locs_keeps_the_sweep).
 *
 * Note the QBD arena itself no longer exercises this: its floor locs now
 * overlap on column 49, so the reference span exception covers the seam.
 *
 * Walls still disqualify the neighbour — a far tile's near wall must precede
 * a nearer tile's ground — and so does any pending element whose footprint
 * does not reach past this ring.
 */
static int
bucket_neighbour_holds_only_nearer_scenery(
    struct Painter* painter,
    const struct PaintersTile* other_tile,
    struct TilePaint* other_paint,
    int camera_sx,
    int camera_sz,
    int dist,
    int lateral_gate)
{
    if( !lateral_gate )
        return 0; /* the neighbour is behind this tile along the view ray */
    if( other_paint->step == PAINT_STEP_READY )
        return 0; /* ground not down yet — nothing to relax */

    /* Memoized: the scan depends only on the neighbour and the camera (both
     * fixed for the paint), except that elements leave the pending set as
     * they draw — which can only LOWER the max, so the cached value is a
     * sound upper bound (see TilePaint.seam_scan). One chain walk per
     * neighbour per paint instead of one per retry of every gated tile. */
    if( other_paint->seam_scan == 0 )
    {
        int max_near = -1;
        if( other_tile->scenery_head == -1 || other_tile->wall_a != -1 ||
            other_tile->wall_b != -1 || other_tile->wall_decor_a != -1 )
        {
            other_paint->seam_scan = SEAM_SCAN_DISQUALIFIED;
        }
        else
        {
            for( int32_t sn = other_tile->scenery_head; sn != -1;
                 sn = painter->scenery_pool[sn].next )
            {
                int si = painter->scenery_pool[sn].element_idx;
                int d;
                if( painter->element_paints[si].drawn )
                    continue;
                d = scenery_near_corner_dist(&painter->elements[si], camera_sx, camera_sz);
                if( d > max_near )
                    max_near = d;
            }
            if( max_near < 0 )
                other_paint->seam_scan = SEAM_SCAN_DISQUALIFIED; /* nothing pending */
            else if( max_near >= SEAM_SCAN_DISQUALIFIED - 1 )
                other_paint->seam_scan = SEAM_SCAN_DISQUALIFIED - 1;
            else
                other_paint->seam_scan = (uint8_t)(1 + max_near);
        }
    }

    if( other_paint->seam_scan == SEAM_SCAN_DISQUALIFIED )
        return 0;
    return other_paint->seam_scan - 1 < dist;
}

/* One direction of the reference adjacency gate. Non-zero = this tile must
 * wait. The neighbour must be DONE, or share a loc with this tile and have
 * its ground down. */
static inline int
bucket_reference_gate_blocks(
    struct Painter* painter,
    const struct PaintersTile* tile,
    int neighbour_idx,
    unsigned span_flag)
{
    const struct TilePaint* other = &painter->tile_paints[neighbour_idx];

    if( other->step == PAINT_STEP_DONE )
        return 0;
    if( other->step != PAINT_STEP_READY && (tile->spans & span_flag) != 0 )
        return 0; /* reference span exception: the two share a loc */
    return 1;
}

/* The ground-pass gate: the reference gate plus the seam exception. Non-zero =
 * this tile must wait. `lateral_gate` says whether this direction is across
 * the eye->tile ray (the caller computes it once per tile from |dx|, |dz|:
 * the W/E gates are lateral when |dz| > |dx|, the N/S gates when |dx| > |dz|).
 * When the exception is what lets the tile through, `*relaxed` is set so the
 * tile's scenery and completion can still wait for the reference gate. */
static inline int
bucket_gate_blocks(
    struct Painter* painter,
    const struct PaintersTile* tile,
    int neighbour_idx,
    unsigned span_flag,
    int camera_sx,
    int camera_sz,
    int dist,
    int lateral_gate,
    int* relaxed)
{
    if( !bucket_reference_gate_blocks(painter, tile, neighbour_idx, span_flag) )
        return 0;
    if( lateral_gate &&
        bucket_neighbour_holds_only_nearer_scenery(
            painter,
            &painter->tiles[neighbour_idx],
            &painter->tile_paints[neighbour_idx],
            camera_sx,
            camera_sz,
            dist,
            lateral_gate) )
    {
        *relaxed = 1;
        return 0;
    }
    return 1;
}

/* The reference gate over every far neighbour of `tile`. Non-zero = some far
 * neighbour is still pending. What a seam-relaxed tile re-checks before it
 * draws its own scenery or completes — the exception only ever frees the
 * GROUND; a tall loc beside the tile whose far part abuts it must still go
 * down first, as it does in painter_paint_world3d. */
static inline int
bucket_far_neighbours_pending(
    struct Painter* painter,
    const struct PaintersTile* tile,
    int tile_idx,
    int camera_sx,
    int camera_sz,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z)
{
    int width = painter->width;
    if( tile_is_west_inbounds(tile->sx, camera_sx, min_draw_x) &&
        bucket_reference_gate_blocks(painter, tile, tile_idx - 1, SPAN_FLAG_WEST) )
        return 1;
    if( tile_is_east_inbounds(tile->sx, camera_sx, max_draw_x) &&
        bucket_reference_gate_blocks(painter, tile, tile_idx + 1, SPAN_FLAG_EAST) )
        return 1;
    if( tile_is_south_inbounds(tile->sz, camera_sz, min_draw_z) &&
        bucket_reference_gate_blocks(painter, tile, tile_idx - width, SPAN_FLAG_SOUTH) )
        return 1;
    if( tile_is_north_inbounds(tile->sz, camera_sz, max_draw_z) &&
        bucket_reference_gate_blocks(painter, tile, tile_idx + width, SPAN_FLAG_NORTH) )
        return 1;
    return 0;
}

static inline void
bucket_emit_world_marker(
    struct PaintersElementCommand** cur,
    struct PaintersElementCommand* end,
    int kind,
    int view_id)
{
    assert(*cur < end);
    assert(view_id > 0);
    assert(view_id < PAINTER_MAX_WORLD_VIEWS);
    **cur = (struct PaintersElementCommand){
        ._entity = {
            ._bf_kind = (uint32_t)kind,
            ._bf_entity = (uint32_t)view_id,
        },
    };
    (*cur)++;
}

static inline void
bucket_emit_entity(
    struct PaintersElementCommand** cur,
    struct PaintersElementCommand* end,
    int entity)
{
    assert(*cur < end);
    **cur = (struct PaintersElementCommand){
        ._entity = {
            ._bf_kind = PNTR_CMD_ELEMENT,
            ._bf_entity = (uint32_t)entity,
        },
    };
    (*cur)++;
}

static inline void
bucket_emit_terrain(
    struct PaintersElementCommand** cur,
    struct PaintersElementCommand* end,
    int sx,
    int sz,
    int slevel)
{
    assert(*cur < end);
    **cur = (struct PaintersElementCommand){
        ._terrain = {
            ._bf_kind = PNTR_CMD_TERRAIN,
            ._bf_terrain_x = (uint32_t)sx,
            ._bf_terrain_z = (uint32_t)sz,
            ._bf_terrain_y = (uint32_t)slevel,
        },
    };
    (*cur)++;
}

static inline void
bucket_emit_terrain_pick_only(
    struct PaintersElementCommand** cur,
    struct PaintersElementCommand* end,
    int sx,
    int sz,
    int slevel)
{
    assert(*cur < end);
    **cur = (struct PaintersElementCommand){
        ._terrain = {
            ._bf_kind = PNTR_CMD_TERRAIN_PICK_ONLY,
            ._bf_terrain_x = (uint32_t)sx,
            ._bf_terrain_z = (uint32_t)sz,
            ._bf_terrain_y = (uint32_t)slevel,
        },
    };
    (*cur)++;
}

/* Everything 3-dimensional the reference emits in a tile's front pass: the
 * bridge underpass wall and scenery, the tile's far-side walls, ground decor,
 * ground objects and wall decor. Split out of the ground pass so a tile the
 * seam exception releases early can put its TERRAIN down at once (which is
 * what a waiting multi-tile loc's footprint check needs) while these — the
 * features that could wrongly cover a still-pending far neighbour — wait for
 * the plain reference gate, exactly as painter_paint_world3d orders them. */
/* Forced inline: two call sites (the ground pass for every ordinary tile, the
 * release site for the rare relaxed one); as an outlined call it cost the hot
 * path ~5% of the paint stage. */
static inline void __attribute__((always_inline))
bucket_emit_tile_features(
    struct Painter* painter,
    struct PaintersElementCommand** cmd_cur_p,
    struct PaintersElementCommand* cmd_end,
    struct PaintersTile* tile,
    int e_tile,
    int camera_sx,
    int camera_sz)
{
    struct PaintersTile* tiles = painter->tiles;
    struct PaintersElement* elements = painter->elements;
    struct ElementPaint* element_paints = painter->element_paints;
    struct SceneryNode* scenery_pool = painter->scenery_pool;
    struct SceneOccluders* occ = painter->occluders;
    struct PaintersElementCommand* cmd_cur = *cmd_cur_p;
    int tile_sx = tile->sx;
    int tile_sz = tile->sz;
    int occlusion_level = painters_tile_get_mesh_level(tile);
    int far_walls = far_wall_flags(camera_sx, camera_sz, tile_sx, tile_sz);

    (void)tiles;
    if( tile->bridge_tile != -1 )
    {
        struct PaintersTile* bridge_underpass_tile = &tiles[tile->bridge_tile];
        (void)bridge_underpass_tile;
        if( bridge_underpass_tile->wall_a != -1 )
        {
            struct PaintersElement* element = &elements[bridge_underpass_tile->wall_a];
            assert(element->kind == PNTRELEM_WALL_A);
            bucket_emit_entity(&cmd_cur, cmd_end, element->_wall.entity);
            painter_wedgelog_paint(
                tile->bridge_tile,
                "wall:bridge",
                (int)element->source_level,
                element->_wall.entity,
                bridge_underpass_tile->wall_a);
        }

        for( int32_t sn = bridge_underpass_tile->scenery_head; sn != -1;
             sn = scenery_pool[sn].next )
        {
            int scenery_element = scenery_pool[sn].element_idx;
            struct ElementPaint* ep = &element_paints[scenery_element];
            if( ep->drawn )
                continue;

            struct PaintersElement* element = &elements[scenery_element];
            assert(element->kind == PNTRELEM_SCENERY);
            /* A world-entity pseudo-loc's `entity` is a view id; emitting it
             * here would draw whichever scene element happens to share that
             * number. The dynamics pass never registers one on a bridge
             * underpass tile, so reaching this is a caller bug. */
            assert(!scenery_is_world_entity(element));
            bucket_emit_entity(&cmd_cur, cmd_end, element->_scenery.entity);
            painter_wedgelog_paint(
                tile->bridge_tile,
                scenery_element >= painter->static_element_count ? "entity:bridge"
                                                                : "loc:bridge",
                (int)element->source_level,
                element->_scenery.entity,
                scenery_element);

            ep->drawn = true;
        }
    }

    if( tile->wall_a != -1 )
    {
        struct PaintersElement* element = &elements[tile->wall_a];
        assert(element->kind == PNTRELEM_WALL_A);
        if( (element->_wall.side & far_walls) != 0 &&
            !(occ && scene_occluders_wall_hidden(
                         occ, occlusion_level, tile_sx, tile_sz, element->_wall.side)) )
        {
            bucket_emit_entity(&cmd_cur, cmd_end, element->_wall.entity);
            painter_wedgelog_paint(
                e_tile, "wall_a", (int)element->source_level, element->_wall.entity,
                tile->wall_a);
        }
    }

    if( tile->wall_b != -1 )
    {
        struct PaintersElement* element = &elements[tile->wall_b];
        assert(element->kind == PNTRELEM_WALL_B);
        if( (element->_wall.side & far_walls) != 0 &&
            !(occ && scene_occluders_wall_hidden(
                         occ, occlusion_level, tile_sx, tile_sz, element->_wall.side)) )
        {
            bucket_emit_entity(&cmd_cur, cmd_end, element->_wall.entity);
            painter_wedgelog_paint(
                e_tile, "wall_b", (int)element->source_level, element->_wall.entity,
                tile->wall_b);
        }
    }

    if( tile->ground_decor != -1 && painter_ground_decor_enabled() )
    {
        struct PaintersElement* element = &elements[tile->ground_decor];
        assert(element->kind == PNTRELEM_GROUND_DECOR);
        if( !(occ && scene_occluders_column_hidden(
                         occ, occlusion_level, tile_sx, tile_sz, 0)) )
        {
            bucket_emit_entity(&cmd_cur, cmd_end, element->_ground_decor.entity);
            painter_wedgelog_paint(
                e_tile, "grounddecor", (int)element->source_level,
                element->_ground_decor.entity, tile->ground_decor);
        }
    }

    if( tile->ground_object_bottom != -1 )
    {
        struct PaintersElement* element = &elements[tile->ground_object_bottom];
        assert(element->kind == PNTRELEM_GROUND_OBJECT);
        if( !(occ && scene_occluders_column_hidden(
                         occ, occlusion_level, tile_sx, tile_sz, 0)) )
        {
            bucket_emit_entity(&cmd_cur, cmd_end, element->_ground_object.entity);
            painter_wedgelog_paint(
                e_tile, "item", (int)element->source_level,
                element->_ground_object.entity, tile->ground_object_bottom);
        }
    }

    if( tile->wall_decor_a != -1 )
    {
        struct PaintersElement* element = &elements[tile->wall_decor_a];
        assert(element->kind == PNTRELEM_WALL_DECOR);
        int decor_hidden =
            occ &&
            scene_occluders_column_hidden(
                occ, occlusion_level, tile_sx, tile_sz, element->_wall_decor.model_height);
        if( element->_wall_decor._bf_through_wall_flags != 0 )
        {
            int x_diff = element->sx - camera_sx;
            int z_diff = element->sz - camera_sz;

            int x_near = x_diff;
            if( element->_wall_decor._bf_side == WALL_CORNER_NORTHEAST ||
                element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST )
                x_near = -x_diff;

            int z_near = z_diff;
            if( element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST ||
                element->_wall_decor._bf_side == WALL_CORNER_SOUTHWEST )
                z_near = -z_diff;

            if( z_near < x_near )
            {
                if( !decor_hidden )
                {
                    bucket_emit_entity(&cmd_cur, cmd_end, element->_wall_decor.entity);
                    painter_wedgelog_paint(
                        e_tile, "decor", (int)element->source_level,
                        element->_wall_decor.entity, tile->wall_decor_a);
                }
            }
            else if( tile->wall_decor_b != -1 )
            {
                element = &elements[tile->wall_decor_b];
                assert(element->kind == PNTRELEM_WALL_DECOR);
                if( !decor_hidden )
                {
                    bucket_emit_entity(&cmd_cur, cmd_end, element->_wall_decor.entity);
                    painter_wedgelog_paint(
                        e_tile, "decor_alt", (int)element->source_level,
                        element->_wall_decor.entity, tile->wall_decor_b);
                }
            }
        }
        else if( (element->_wall_decor._bf_side & far_walls) != 0 )
        {
            if( !decor_hidden )
            {
                bucket_emit_entity(&cmd_cur, cmd_end, element->_wall_decor.entity);
                painter_wedgelog_paint(
                    e_tile, "decor", (int)element->source_level,
                    element->_wall_decor.entity, tile->wall_decor_a);
            }
        }
    }
    else
    {
        assert(tile->wall_decor_b == -1);
    }
    *cmd_cur_p = cmd_cur;
}

/** Widest ready-scenery batch one pop collects; also the resume array width. */
#define BUCKET_VISIT_MAX 64

enum BucketRunResult
{
    BUCKET_RUN_DONE = 0,
    /** Suspended on a world-entity pseudo-loc; ctx->descend_view names the view. */
    BUCKET_RUN_DESCEND = 1,
};

/**
 * One suspendable paint. Everything here is state the drain mutates and cannot
 * recompute; the hoists (draw box, cull tables, tile arrays) are deliberately
 * NOT here - they are pure functions of the painter and the camera, so a resume
 * derives them again rather than carrying them.
 */
struct BucketPaintCtx
{
    struct Painter* painter;
    int camera_sx;
    int camera_sz;
    int camera_slevel;
    int view_id;
    /** Only the root context drives the wedge log. @see painter_wedgelog_suspend. */
    int wedgelog;
    int started;

    int tiles_remaining;
    int check_adjacent;
    int seed_gen_initialized;
    /** Upper bound on the commands this context may still write. @see
     *  bucket_cursor_reserve. */
    int reserve;
    struct PainterSeedGen seed_gen;

    int64_t perf_pops;
    int64_t perf_gate_rejects;
    int64_t perf_pushes;
    int64_t perf_push_dedup;

    /* The one suspend point inside a pop: partway through a tile's ready
     * scenery batch. Everything else the pop derives from e_tile. */
    int resume_active;
    int resume_e_tile;
    int resume_vi;
    int resume_n_visit;
    int resume_blocked_undrawn;
    int resume_some_drawn;
    int resume_visit_sc[BUCKET_VISIT_MAX];

    int descend_view;
};

/**
 * The shared write cursor. One PaintersBuffer serves the whole stack, so the
 * cursor cannot be a set of locals in the paint: a descended context's setup
 * may realloc the array out from under every suspended context above it. Kept
 * as a struct so that one realloc rebases all of them at once.
 */
struct BucketPaintCursor
{
    struct PaintersBuffer* buffer;
    struct PaintersElementCommand* base;
    struct PaintersElementCommand* cur;
    struct PaintersElementCommand* end;
};

/**
 * Guarantee `need` free slots ahead of the write position, rebasing the cursor
 * if the array moves.
 *
 * Taken on EVERY entry to a context, not once at its setup. A context's budget
 * is reserved from where its cursor stands, and a descent advances that cursor
 * by the whole nested paint before the outer context writes another command —
 * so a reservation taken before the descent no longer covers the outer paint's
 * remainder. Deep chains overran the array exactly this way. Re-reserving on
 * resume costs at most one realloc per suspend and is correct at any depth.
 */
static void
bucket_cursor_reserve(
    struct BucketPaintCursor* cursor,
    int need)
{
    assert(cursor);
    assert(need > 0);
    struct PaintersBuffer* buffer = cursor->buffer;
    size_t used = (size_t)(cursor->cur - cursor->base);
    int want = (int)used + need;
    if( buffer->command_capacity < want )
    {
        int cap = buffer->command_capacity > 0 ? buffer->command_capacity : 128;
        while( cap < want )
            cap *= 2;
        buffer->commands = realloc(
            buffer->commands, (size_t)cap * sizeof(struct PaintersElementCommand));
        assert(buffer->commands);
        buffer->command_capacity = cap;
    }
    cursor->base = buffer->commands;
    cursor->cur = cursor->base + used;
    cursor->end = cursor->base + buffer->command_capacity;
}

/**
 * Emit one tile's ready-scenery batch, elements `vi_start`..`n_visit`.
 *
 * Returns the view id to descend into, having recorded the resume point in
 * `ctx`; 0 when the batch ran to the end.
 *
 * Its own always-inlined function purely so the descent does NOT need a label
 * in the middle of the drain's pop loop. Resuming by `goto` into the loop body
 * measured ~6% slower per paint with no boats on screen at all — a jump into
 * the middle of a loop stops GCC keeping that tile's state in registers across
 * the whole body. Here the resume path calls this a second time with a
 * non-zero `vi_start` instead, and the drain's own call inlines with
 * `vi_start` folded to 0, exactly the loop it had before world entities.
 */
static inline int __attribute__((always_inline))
bucket_emit_scenery_batch(
    struct BucketPaintCtx* ctx,
    struct Painter* painter,
    struct PainterBucketCtx* w,
    struct PaintersElementCommand** cmd_cur_p,
    struct PaintersElementCommand* cmd_end,
    struct PaintersElement* elements,
    struct ElementPaint* element_paints,
    struct TilePaint* paints,
    const int* visit_sc,
    int n_visit,
    int vi_start,
    int e_tile,
    int occlusion_level,
    int paintgrid_level,
    int width,
    int level_stride,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int camera_sx,
    int camera_sz,
    int blocked_undrawn,
    int* some_drawn_p,
    int64_t* perf_pushes,
    int64_t* perf_push_dedup)
{
    struct PaintersElementCommand* cmd_cur = *cmd_cur_p;
    int some_drawn = *some_drawn_p;
    int descend = 0;

    for( int vi = vi_start; vi < n_visit; vi++ )
    {
        int si = visit_sc[vi];
        struct ElementPaint* ep = &element_paints[si];
        ep->drawn = true;

        struct PaintersElement* element = &elements[si];
        assert(element->kind == PNTRELEM_SCENERY);
        /* A world-entity pseudo-loc carries a VIEW ID, not a scene element.
         * It occupies this slot in painter order; the boat's own painter
         * writes its commands between the two markers. */
        int descend_view =
            scenery_is_world_entity(element) ? (int)element->_scenery.entity : 0;
        if( descend_view )
        {
            assert(descend_view > 0);
            assert(descend_view < PAINTER_MAX_WORLD_VIEWS);
            bucket_emit_world_marker(
                &cmd_cur, cmd_end, PNTR_CMD_BEGIN_WORLD, descend_view);
            painter_wedgelog_paint(
                e_tile, "world", (int)element->source_level, descend_view, si);
        }
        else if( !(painter->occluders &&
                   scene_occluders_footprint_hidden(
                       painter->occluders,
                       occlusion_level,
                       (int)element->sx,
                       (int)element->sz,
                       element->_scenery.size_x,
                       element->_scenery.size_z,
                       element->_scenery.model_height)) )
        {
            bucket_emit_entity(&cmd_cur, cmd_end, element->_scenery.entity);
            painter_wedgelog_paint(
                e_tile,
                si >= painter->static_element_count ? "entity" : "loc",
                (int)element->source_level,
                element->_scenery.entity,
                si);
        }

        int min_tile_x = (int)element->sx;
        int min_tile_z = (int)element->sz;
        int max_tile_x = min_tile_x + element->_scenery.size_x - 1;
        int max_tile_z = min_tile_z + element->_scenery.size_z - 1;

        if( max_tile_x > max_draw_x - 1 )
            max_tile_x = max_draw_x - 1;
        if( max_tile_z > max_draw_z - 1 )
            max_tile_z = max_draw_z - 1;
        if( min_tile_x < min_draw_x )
            min_tile_x = min_draw_x;
        if( min_tile_z < min_draw_z )
            min_tile_z = min_draw_z;

        if( min_tile_x <= max_tile_x && min_tile_z <= max_tile_z )
        {
            int row0 = min_tile_x + min_tile_z * width + paintgrid_level * level_stride;
            for( int oz = min_tile_z, row = row0; oz <= max_tile_z; oz++, row += width )
            {
                for( int ox = min_tile_x, ti = row; ox <= max_tile_x; ox++, ti++ )
                {
                    int ndist = abs(ox - camera_sx) + abs(oz - camera_sz);
                    bucket_push_if_active(
                        w, paints, ti, ndist, perf_pushes, perf_push_dedup);
                    some_drawn = 1;
                }
            }
        }

        if( descend_view )
        {
            /* Suspend: the footprint pushes above are already done, so the
             * resume picks up at vi + 1. `drawn` was set before the emit, so
             * this element is never revisited. */
            ctx->resume_active = 1;
            ctx->resume_e_tile = e_tile;
            ctx->resume_vi = vi + 1;
            ctx->resume_n_visit = n_visit;
            /* Already there when the resume path is the one suspending again;
             * the comparison folds away at both inline sites. */
            if( visit_sc != ctx->resume_visit_sc )
                memcpy(
                    ctx->resume_visit_sc, visit_sc, sizeof(ctx->resume_visit_sc));
            ctx->resume_blocked_undrawn = blocked_undrawn;
            ctx->resume_some_drawn = some_drawn;
            descend = descend_view;
            break;
        }
    }

    *cmd_cur_p = cmd_cur;
    *some_drawn_p = some_drawn;
    return descend;
}

/* restrict: the context and the cursor live on the driver's own stack and are
 * reached through no other pointer here. Without it every ctx-> read in the
 * drain has to be re-loaded after any store into the painter's tile/element
 * arrays, which the compiler must assume may alias them — measurably slower
 * than the single-function paint this replaced. */
static int
bucket_paint_run(
    struct BucketPaintCtx* restrict ctx,
    struct BucketPaintCursor* restrict cursor)
{
    assert(ctx);
    assert(cursor);
    struct Painter* painter = ctx->painter;
    assert(painter);
    struct PaintersBuffer* buffer = cursor->buffer;
    assert(buffer);
    int camera_sx = ctx->camera_sx;
    int camera_sz = ctx->camera_sz;
    int camera_slevel = ctx->camera_slevel;
    const int wedgelog_on = ctx->wedgelog;
    struct PainterBucketCtx* w = BM(painter);
    assert(w);

    /* Hoisted fields — setup/main store through tile_paints, so the compiler
     * cannot otherwise keep these in registers across iterations. */
    int width = painter->width;
    int height = painter->height;
    int levels = painter->levels;
    int level_stride = width * height;
    struct PaintersTile* tiles = painter->tiles;
    struct TilePaint* paints = painter->tile_paints;
    struct PaintersElement* elements = painter->elements;
    struct ElementPaint* element_paints = painter->element_paints;
    struct SceneryNode* scenery_pool = painter->scenery_pool;
    int64_t perf_pops = ctx->perf_pops;
    int64_t perf_gate_rejects = ctx->perf_gate_rejects;
    int64_t perf_pushes = ctx->perf_pushes;
    int64_t perf_push_dedup = ctx->perf_push_dedup;
    const struct PaintersCullMap* cullmap = painter->cullmap;
    size_t cull_camera_key = painter->cull_camera_key;
    int cull_all_visible = (cullmap == NULL || cullmap->all_visible);
    int cull_radius = 0;
    int cull_grid_side = 0;
    const uint8_t* cull_vis = NULL;
    if( !cull_all_visible )
    {
        cull_radius = cullmap->radius;
        cull_grid_side = cullmap->grid_side;
        cull_vis = cullmap->visibility;
    }

    int radius = painter->draw_distance;

    uint8_t draw_mask = painter->level_mask ? painter->level_mask : 0xFu;
    /* Iterate all grid stack levels; per-tile draw_mask uses packed visible_gte_level (VisBelow). */
    int min_level = 0;
    int max_level = levels;

    int draw_center_sx;
    int draw_center_sz;
    int min_draw_x;
    int max_draw_x;
    int min_draw_z;
    int max_draw_z;
    painter_resolve_draw_box(
        painter,
        camera_sx,
        camera_sz,
        radius,
        &draw_center_sx,
        &draw_center_sz,
        &min_draw_x,
        &max_draw_x,
        &min_draw_z,
        &max_draw_z);
    (void)draw_center_sx;
    (void)draw_center_sz;

    if( min_draw_x >= max_draw_x || min_draw_z >= max_draw_z )
        goto done;

    int cullspan_active = painter->cullspan_active;
    const struct PaintersCullSpan* cullspan = &painter->cullspan;

    painter_cullmap_refresh_camera_key(painter);
    cull_camera_key = painter->cull_camera_key;

    /* Draw-order telemetry (TORIRS_WEDGELOG). Armed before the classify loop so
     * the MARK rows land in the same file as the traversal. */
    if( !ctx->started && wedgelog_on )
        painter_wedgelog_frame_begin(
            painter,
            camera_sx,
            camera_sz,
            draw_center_sx,
            draw_center_sz,
            min_draw_x,
            max_draw_x,
            min_draw_z,
            max_draw_z,
            radius,
            draw_mask);

    /* Contiguous row setup: reset + classify + count + bulk push.
     *
     * Every READY tile enters its distance bucket here, so the drain is a single
     * globally distance-ordered sweep: all four quadrants advance toward the eye
     * together, ring by ring. Seeding one perimeter tile per drain instead (the
     * world3d cascade model, which this loop used to copy) lets the first seed's
     * wave flood its entire quadrant before the next seed is taken — the box then
     * paints one corner at a time, and a near tile of the first quadrant lands
     * ahead of a far tile of the next. The perimeter seed generator below stays
     * as the liveness fallback for tiles a span cycle strands; it is no longer
     * what drives the traversal. */
    int tiles_remaining;
    int check_adjacent;
    if( !ctx->started )
    {
    memset(element_paints, 0x00, (size_t)painter->element_count * sizeof(struct ElementPaint));
    tiles_remaining = 0;
    int tiles_in_box = 0;
    bucket_reset(w);
    for( int s = min_level; s < max_level; s++ )
    {
        for( int z = min_draw_z; z < max_draw_z; z++ )
        {
            int row = min_draw_x + z * width + s * level_stride;
            int adz = abs(z - camera_sz);
            int span_lo = min_draw_x;
            int span_hi = max_draw_x; /* exclusive end of visible band */
            int row_culled = 0;

            if( cullspan_active )
            {
                int lo;
                int hi;
                if( !painters_cullspan_row(cullspan, z - camera_sz, &lo, &hi) )
                {
                    row_culled = 1;
                }
                else
                {
                    span_lo = camera_sx + lo;
                    span_hi = camera_sx + hi + 1;
                    if( span_lo < min_draw_x )
                        span_lo = min_draw_x;
                    if( span_hi > max_draw_x )
                        span_hi = max_draw_x;
                    if( span_lo >= span_hi )
                        row_culled = 1;
                }
            }

            for( int x = min_draw_x, ti = row; x < max_draw_x; x++, ti++ )
            {
                tiles_in_box++;
                struct PaintersTile* t = &tiles[ti];
                struct TilePaint* tp = &paints[ti];

                tp->near_wall_flags = 0;
                tp->in_queue = 0;
                tp->occlusion = TILE_OCCLUSION_UNKNOWN;
                tp->scenery_sorted = 0;
                tp->seam_relaxed = 0;
                tp->seam_scan = 0;

                int tile_visible_gte_level = painters_tile_get_visible_gte_level(t);
                if( tile_excluded_by_bridge_or_draw_mask(
                        painters_tile_get_flags(t), tile_visible_gte_level, draw_mask) )
                {
                    tp->step = PAINT_STEP_DONE;
                    continue;
                }

                if( cullspan_active )
                {
                    if( row_culled || x < span_lo || x >= span_hi )
                    {
                        tp->step = PAINT_STEP_DONE;
                        continue;
                    }
                }
                else if( !cull_all_visible )
                {
                    int dx = x - camera_sx;
                    int dz = z - camera_sz;
                    if( dx < -cull_radius || dx > cull_radius || dz < -cull_radius ||
                        dz > cull_radius )
                    {
                        tp->step = PAINT_STEP_DONE;
                        continue;
                    }
                    int ix = dx + cull_radius;
                    int iz = dz + cull_radius;
                    size_t bidx =
                        cull_camera_key + (size_t)ix * (size_t)cull_grid_side + (size_t)iz;
                    if( !pcull_bit_get(cull_vis, bidx) )
                    {
                        tp->step = PAINT_STEP_DONE;
                        continue;
                    }
                }

                tp->step = PAINT_STEP_READY;
                tiles_remaining++;
                if( painter_wedgelog_armed() )
                    painter_wedgelog_event(ti, "MARK", NULL);
                bucket_push(w, paints, ti, abs(x - camera_sx) + adz);
                BUCKET_PERF_INCREMENT(perf_pushes);
            }
        }
    }

    /* Hard upper bound: each wall can emit twice (far + near); terrain once per box
     * tile; scenery/ground/decor at most once each. */
    int need_cmds = 2 * painter->element_count + 2 * tiles_in_box +
                    2 * PAINTER_MAX_WORLD_VIEWS + 16;
    if( need_cmds < 16 )
        need_cmds = 16;
    /* Reserved on top of whatever the suspended contexts above have already
     * written, and re-taken on every resume. */
    ctx->reserve = need_cmds;
    bucket_cursor_reserve(cursor, ctx->reserve);

    /* Incremental seed generator - initialized lazily on the first queue drain so frames
     * where the cascade covers all tiles pay zero seed-iteration cost. Lives on
     * the context: it is a cursor, and a suspended paint must resume where it
     * left the perimeter. */
    ctx->seed_gen_initialized = 0;
    check_adjacent = 1;

    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_PAINTER_TILES_REMAINING_SET, tiles_remaining);

    /* Runtime outcome: a live cullmap that marks every tile in the draw box as
     * DONE leaves the world blank. Warn once so a bad bake is obvious; do not
     * assert (cullmap contents are data, not a programming invariant). */
    if( tiles_in_box > 0 && tiles_remaining == 0 &&
        (cullspan_active || !cull_all_visible) )
    {
        static int s_warned_empty_cull;
        if( !s_warned_empty_cull )
        {
            s_warned_empty_cull = 1;
            fprintf(
                stderr,
                "painter_paint_bucket: draw box %dx%d has 0 visible tiles "
                "(cullspan=%d cullmap_radius=%d) — world will be blank; set "
                "TORIRS_PAINTER_NOCULL=1 to recover\n",
                max_draw_x - min_draw_x,
                max_draw_z - min_draw_z,
                cullspan_active,
                cull_radius);
        }
    }
    ctx->started = 1;
    }
    else
    {
        tiles_remaining = ctx->tiles_remaining;
        check_adjacent = ctx->check_adjacent;
        /* Resuming behind a completed descent: the cursor has moved by the
         * whole nested paint, so this context's budget is reserved again from
         * where it now stands. */
        bucket_cursor_reserve(cursor, ctx->reserve);
    }

    /* Copied in and out rather than pointed at: seed_gen_next() steps it a
     * field at a time, and through a pointer into the context every step
     * re-reads memory. */
    struct PainterSeedGen seed_gen_local = ctx->seed_gen;
    struct PainterSeedGen* seed_gen = &seed_gen_local;
    int seed_gen_initialized = ctx->seed_gen_initialized;
    struct PaintersElementCommand* cmd_cur = cursor->cur;
    struct PaintersElementCommand* cmd_end = cursor->end;

    /* Finish the ready-scenery batch a descent cut in half, before the drain
     * proper — never from inside it, so the pop loop carries no resume state.
     * The interrupted tile had at least one ready element, so this always ends
     * in the same "scenery was emitted" tail the loop runs and never falls
     * through to that tile's wall pass. */
    if( ctx->resume_active )
    {
        ctx->resume_active = 0;
        int e_tile = ctx->resume_e_tile;
        struct PaintersTile* tile = &tiles[e_tile];
        int n_visit = ctx->resume_n_visit;
        int blocked_undrawn = ctx->resume_blocked_undrawn;
        int some_drawn = ctx->resume_some_drawn;
        assert(n_visit > 0);
        int descend_view = bucket_emit_scenery_batch(
            ctx,
            painter,
            w,
            &cmd_cur,
            cmd_end,
            elements,
            element_paints,
            paints,
            ctx->resume_visit_sc,
            n_visit,
            ctx->resume_vi,
            e_tile,
            painters_tile_get_mesh_level(tile),
            painters_tile_get_paintgrid_level(tile),
            width,
            level_stride,
            min_draw_x,
            max_draw_x,
            min_draw_z,
            max_draw_z,
            camera_sx,
            camera_sz,
            blocked_undrawn,
            &some_drawn,
            &perf_pushes,
            &perf_push_dedup);
        if( descend_view )
        {
            ctx->descend_view = descend_view;
            goto suspend;
        }
        /* Containment, as in the drain: a blocked contained loc needs the tile
         * revisited when no footprint push already scheduled it. */
        if( blocked_undrawn && !some_drawn )
            bucket_push_if_active(
                w,
                paints,
                e_tile,
                abs(tile->sx - camera_sx) + abs(tile->sz - camera_sz),
                &perf_pushes,
                &perf_push_dedup);
    }

    for( ;; )
    {
        int e_tile;
        struct PaintersTile* tile;
        struct TilePaint* tile_paint;
        int tile_sx;
        int tile_sz;
        int paintgrid_level;
        int occlusion_level;
        int adx;
        int adz;
        int tile_dist;
        int visit_sc[BUCKET_VISIT_MAX];
        int n_visit;
        int blocked_undrawn;
        int some_drawn;

        if( bucket_queue_empty(w) )
        {
            if( tiles_remaining == 0 )
                break;
            if( !seed_gen_initialized )
            {
                int seed_r = painter_seed_radius_for_box(
                    camera_sx,
                    camera_sz,
                    min_draw_x,
                    max_draw_x,
                    min_draw_z,
                    max_draw_z,
                    radius);
                seed_gen_init(
                    seed_gen,
                    camera_sx,
                    camera_sz,
                    min_draw_x,
                    max_draw_x,
                    min_draw_z,
                    max_draw_z,
                    levels,
                    seed_r);
                seed_gen_initialized = 1;
            }
            int seeded = 0;
            int sx, sz, level, phase;
            while( seed_gen_next(seed_gen, &sx, &sz, &level, &phase) )
            {
                int tidx = sx + sz * width + level * level_stride;
                if( paints[tidx].step == PAINT_STEP_READY )
                {
                    int dist = abs(sx - camera_sx) + abs(sz - camera_sz);
                    if( painter_wedgelog_armed() )
                    {
                        char extra[48];
                        snprintf(extra, sizeof(extra), "phase=%d d=%d", phase, dist);
                        painter_wedgelog_event(tidx, "SEED", extra);
                    }
                    bucket_push_if_active(
                        w, paints, tidx, dist, &perf_pushes, &perf_push_dedup);
                    check_adjacent = (phase == 1);
                    seeded = 1;
                    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_DRAIN_EVENTS, 1);
                    break;
                }
            }
            if( !seeded )
                break;
        }

        e_tile = bucket_pop(w, paints);
        if( e_tile < 0 )
            continue;
        BUCKET_PERF_INCREMENT(perf_pops);
        if( painter_wedgelog_armed() )
        {
            char extra[32];
            snprintf(extra, sizeof(extra), "step=%d", (int)paints[e_tile].step);
            painter_wedgelog_event(e_tile, "POP", extra);
        }

        tile = &tiles[e_tile];
        tile_paint = &paints[e_tile];
        if( tile_paint->step == PAINT_STEP_DONE )
            continue;

        tile_sx = tile->sx;
        tile_sz = tile->sz;
        paintgrid_level = painters_tile_get_paintgrid_level(tile);
        /* Occlusion samples ground heights by originalLevel (Client-TS /
         * Square.originalLevel). mesh_level survives bridge push-down;
         * paintgrid_level is the shifted grid slot used for traversal. */
        occlusion_level = painters_tile_get_mesh_level(tile);
        adx = abs(tile_sx - camera_sx);
        adz = abs(tile_sz - camera_sz);
        tile_dist = adx + adz;

        if( tile_paint->step == PAINT_STEP_READY )
        {
            if( check_adjacent )
            {
                if( paintgrid_level > 0 )
                {
                    if( paints[e_tile - level_stride].step != PAINT_STEP_DONE )
                    {
                        BUCKET_PERF_INCREMENT(perf_gate_rejects);
                        continue;
                    }
                }

                /* Match painter_paint_world3d draw_front adjacent deps, plus
                 * the seam exception (bucket_gate_blocks). */
                int relaxed = 0;
                /* Which gates cross the eye->tile ray (adx/adz from above). */
                int lateral_we = adz > adx;
                int lateral_ns = adx > adz;
                if( tile_is_west_inbounds(tile_sx, camera_sx, min_draw_x) &&
                    bucket_gate_blocks(
                        painter, tile, e_tile - 1, SPAN_FLAG_WEST, camera_sx, camera_sz,
                        tile_dist, lateral_we, &relaxed) )
                {
                    BUCKET_PERF_INCREMENT(perf_gate_rejects);
                    continue;
                }
                if( tile_is_east_inbounds(tile_sx, camera_sx, max_draw_x) &&
                    bucket_gate_blocks(
                        painter, tile, e_tile + 1, SPAN_FLAG_EAST, camera_sx, camera_sz,
                        tile_dist, lateral_we, &relaxed) )
                {
                    BUCKET_PERF_INCREMENT(perf_gate_rejects);
                    continue;
                }
                if( tile_is_south_inbounds(tile_sz, camera_sz, min_draw_z) &&
                    bucket_gate_blocks(
                        painter, tile, e_tile - width, SPAN_FLAG_SOUTH, camera_sx, camera_sz,
                        tile_dist, lateral_ns, &relaxed) )
                {
                    BUCKET_PERF_INCREMENT(perf_gate_rejects);
                    continue;
                }
                if( tile_is_north_inbounds(tile_sz, camera_sz, max_draw_z) &&
                    bucket_gate_blocks(
                        painter, tile, e_tile + width, SPAN_FLAG_NORTH, camera_sx, camera_sz,
                        tile_dist, lateral_ns, &relaxed) )
                {
                    BUCKET_PERF_INCREMENT(perf_gate_rejects);
                    continue;
                }
                tile_paint->seam_relaxed = (uint8_t)relaxed;
            }
            else
            {
                check_adjacent = 1;
            }

            int far_walls = far_wall_flags(camera_sx, camera_sz, tile_sx, tile_sz);
            tile_paint->near_wall_flags |= (uint8_t)~far_walls;

            struct SceneOccluders* occ = painter->occluders;
            /* Occluders are optional; without them nothing is hidden. */
            int ground_hidden =
                occ ? painter_tile_ground_hidden(
                          occ, tile_paint, occlusion_level, tile_sx, tile_sz)
                    : 0;

            if( tile->bridge_tile != -1 )
            {
                struct PaintersTile* bridge_underpass_tile = &tiles[tile->bridge_tile];
                /* Reference groundOccluded(0, ...) on the linked underpass square. */
                if( !(occ &&
                      scene_occluders_ground_tile_hidden(
                          occ,
                          painters_tile_get_mesh_level(bridge_underpass_tile),
                          bridge_underpass_tile->sx,
                          bridge_underpass_tile->sz)) )
                {
                    unsigned uset = bridge_underpass_tile->terrain_levels;
                    for( int ml = 0; ml < 4; ml++ )
                        if( uset & (1u << ml) )
                        {
                            bucket_emit_terrain(&cmd_cur, cmd_end, bridge_underpass_tile->sx,
                                                bridge_underpass_tile->sz, ml);
                            painter_wedgelog_paint(
                                tile->bridge_tile, "floor:bridge", ml, -1, -1);
                        }
                }

            }

            {
                /* Every mesh this tile owns, ascending, so a VisBelow mesh
                 * moved down from above lands on top of the one below it.
                 * See PaintersTile::terrain_levels. */
                unsigned set = tile->terrain_levels;
                for( int ml = 0; ml < 4; ml++ )
                    if( set & (1u << ml) )
                    {
                        if( !ground_hidden )
                        {
                            bucket_emit_terrain(&cmd_cur, cmd_end, tile_sx, tile_sz, ml);
                            painter_wedgelog_paint(e_tile, "floor", ml, -1, -1);
                        }
                        else if( camera_slevel >= 0 && ml <= camera_slevel )
                        {
                            bucket_emit_terrain_pick_only(
                                &cmd_cur, cmd_end, tile_sx, tile_sz, ml);
                        }
                    }
            }

            /* The 3D features. A tile the seam exception let through defers
             * them — its terrain is what the waiting loc needed; walls and
             * decor hold with the scenery until the reference gate passes
             * (world3d order). Emitted at the release site below. */
            if( !tile_paint->seam_relaxed )
                bucket_emit_tile_features(
                    painter,
                    &cmd_cur,
                    cmd_end,
                    tile,
                    e_tile,
                    camera_sx,
                    camera_sz);


            tile_paint->step = PAINT_STEP_GROUND;

            unsigned spans = tile->spans;
            if( spans )
            {
                if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) &&
                    (spans & SPAN_FLAG_EAST) )
                {
                    bucket_push_if_active(
                        w,
                        paints,
                        e_tile + 1,
                        abs(tile_sx + 1 - camera_sx) + adz,
                        &perf_pushes,
                        &perf_push_dedup);
                }
                if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) &&
                    (spans & SPAN_FLAG_NORTH) )
                {
                    bucket_push_if_active(
                        w,
                        paints,
                        e_tile + width,
                        adx + abs(tile_sz + 1 - camera_sz),
                        &perf_pushes,
                        &perf_push_dedup);
                }
                if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) &&
                    (spans & SPAN_FLAG_WEST) )
                {
                    bucket_push_if_active(
                        w,
                        paints,
                        e_tile - 1,
                        abs(tile_sx - 1 - camera_sx) + adz,
                        &perf_pushes,
                        &perf_push_dedup);
                }
                if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) &&
                    (spans & SPAN_FLAG_SOUTH) )
                {
                    bucket_push_if_active(
                        w,
                        paints,
                        e_tile - width,
                        adx + abs(tile_sz - 1 - camera_sz),
                        &perf_pushes,
                        &perf_push_dedup);
                }
            }

            /* A pop the seam exception just let through cannot pass the
             * reference re-check below in the same pop — nothing has changed
             * since the gate ran. Skip the scenery walk and the four-gate
             * check; the far neighbour's completion (or a footprint push)
             * re-queues this tile. */
            if( tile_paint->seam_relaxed )
            {
                BUCKET_PERF_INCREMENT(perf_gate_rejects);
                continue;
            }
        }

        /* PAINT_STEP_GROUND == reference PAINTER_STEP_BASE for scenery / completion. */
        n_visit = 0;
        blocked_undrawn = 0;

        /* A tile the seam exception let through paints nothing but its ground
         * until the reference gate passes; the neighbour's completion pushes
         * this tile back into the queue (its inward neighbour). */
        if( tile_paint->seam_relaxed )
        {
            if( bucket_far_neighbours_pending(
                    painter, tile, e_tile, camera_sx, camera_sz, min_draw_x, max_draw_x,
                    min_draw_z, max_draw_z) )
            {
                BUCKET_PERF_INCREMENT(perf_gate_rejects);
                continue;
            }
            tile_paint->seam_relaxed = 0;
            /* The reference gate finally passes: put down the walls, decor
             * and objects the relaxed ground pass deferred, then fall through
             * to scenery — the order world3d's front pass produces. */
            bucket_emit_tile_features(
                painter,
                &cmd_cur,
                cmd_end,
                tile,
                e_tile,
                camera_sx,
                camera_sz);
        }

        /* Reference emission order (class112.java:1030-1058 + method3971),
         * paid ONCE per tile per paint: the chain is relinked farthest-corner
         * first here, so every pop's ready subset — collected below in chain
         * order — is already sorted. */
        scenery_chain_sort_once(painter, tile, tile_paint, camera_sx, camera_sz);

        for( int32_t sn = tile->scenery_head; sn != -1; sn = scenery_pool[sn].next )
        {
            int si = scenery_pool[sn].element_idx;
            struct ElementPaint* ep = &element_paints[si];
            if( ep->drawn )
                continue;

            struct PaintersElement* element = &elements[si];
            assert(element->kind == PNTRELEM_SCENERY);

            /* RAISED ground items emit at tile completion, not here. */
            if( scenery_is_raised(element) )
                continue;

            int min_tile_x = (int)element->sx;
            int min_tile_z = (int)element->sz;
            int max_tile_x = min_tile_x + element->_scenery.size_x - 1;
            int max_tile_z = min_tile_z + element->_scenery.size_z - 1;

            if( max_tile_x > max_draw_x - 1 )
                max_tile_x = max_draw_x - 1;
            if( max_tile_z > max_draw_z - 1 )
                max_tile_z = max_draw_z - 1;
            if( min_tile_x < min_draw_x )
                min_tile_x = min_draw_x;
            if( min_tile_z < min_draw_z )
                min_tile_z = min_draw_z;

            if( min_tile_x > max_tile_x || min_tile_z > max_tile_z )
            {
                ep->drawn = true;
                continue;
            }

            /* Use the current tile's paintgrid_level for the footprint readiness check, matching
             * world3d's approach. This avoids a circular dependency when bridge level-shifting
             * places elements (with their original source_level) into a lower paintgrid_level
             * tile's scenery_head: checking at source_level would stall on tiles that wait on the
             * current tile to be DONE, creating a deadlock. */
            int all_base = 1;
            int row0 = min_tile_x + min_tile_z * width + paintgrid_level * level_stride;
            for( int oz = min_tile_z, row = row0; oz <= max_tile_z; oz++, row += width )
            {
                for( int ox = min_tile_x, ti = row; ox <= max_tile_x; ox++, ti++ )
                {
                    if( paints[ti].step < PAINT_STEP_GROUND )
                    {
                        all_base = 0;
                        break;
                    }
                }
                if( !all_base )
                    break;
            }

            if( all_base && n_visit < (int)(sizeof(visit_sc) / sizeof(visit_sc[0])) )
                visit_sc[n_visit++] = si;
            else
                blocked_undrawn = 1;
        }

        some_drawn = 0;
        {
            int descend_view = bucket_emit_scenery_batch(
                ctx,
                painter,
                w,
                &cmd_cur,
                cmd_end,
                elements,
                element_paints,
                paints,
                visit_sc,
                n_visit,
                0,
                e_tile,
                occlusion_level,
                paintgrid_level,
                width,
                level_stride,
                min_draw_x,
                max_draw_x,
                min_draw_z,
                max_draw_z,
                camera_sx,
                camera_sz,
                blocked_undrawn,
                &some_drawn,
                &perf_pushes,
                &perf_push_dedup);
            if( descend_view )
            {
                ctx->descend_view = descend_view;
                goto suspend;
            }
        }
        /* Emitting scenery defers near-wall completion until a later visit (world3d parity). */
        if( some_drawn || n_visit > 0 )
        {
            /* Containment: if a STACK_BASE was drawn but a contained loc remains
             * blocked, ensure this tile is revisited (footprint push usually
             * covers it; push explicitly when nothing was footprint-pushed). */
            if( blocked_undrawn && !some_drawn )
                bucket_push_if_active(
                    w,
                    paints,
                    e_tile,
                    tile_dist,
                    &perf_pushes,
                    &perf_push_dedup);
            continue;
        }

        if( blocked_undrawn )
            continue;

        /* Elevated ground items (RAISED) before near walls — Client-TS parity. */
        for( int32_t sn = tile->scenery_head; sn != -1; sn = scenery_pool[sn].next )
        {
            int si = scenery_pool[sn].element_idx;
            struct ElementPaint* ep = &element_paints[si];
            struct PaintersElement* el;
            if( ep->drawn )
                continue;
            el = &elements[si];
            if( !scenery_is_raised(el) )
                continue;
            ep->drawn = true;
            bucket_emit_entity(&cmd_cur, cmd_end, el->_scenery.entity);
            painter_wedgelog_paint(
                e_tile, "item_back", (int)el->source_level, el->_scenery.entity, si);
        }

        {
            struct SceneOccluders* occ = painter->occluders;
            int decor_hidden = 0;
            if( tile->wall_decor_a != -1 )
            {
                struct PaintersElement* decor_el = &elements[tile->wall_decor_a];
                decor_hidden =
                    occ &&
                    scene_occluders_column_hidden(
                        occ,
                        occlusion_level,
                        tile_sx,
                        tile_sz,
                        decor_el->_wall_decor.model_height);
            }

            if( tile->wall_decor_a != -1 )
            {
                struct PaintersElement* element = &elements[tile->wall_decor_a];
                assert(element->kind == PNTRELEM_WALL_DECOR);

                if( element->_wall_decor._bf_through_wall_flags != 0 )
                {
                    int x_diff = element->sx - camera_sx;
                    int z_diff = element->sz - camera_sz;

                    int x_near = x_diff;
                    if( element->_wall_decor._bf_side == WALL_CORNER_NORTHEAST ||
                        element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST )
                        x_near = -x_diff;

                    int z_near = z_diff;
                    if( element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST ||
                        element->_wall_decor._bf_side == WALL_CORNER_SOUTHWEST )
                        z_near = -z_diff;

                    if( z_near >= x_near )
                    {
                        if( !decor_hidden )
                        {
                            bucket_emit_entity(&cmd_cur, cmd_end, element->_wall_decor.entity);
                            painter_wedgelog_paint(
                                e_tile, "decor_back", (int)element->source_level,
                                element->_wall_decor.entity, tile->wall_decor_a);
                        }
                    }
                    else if( tile->wall_decor_b != -1 )
                    {
                        element = &elements[tile->wall_decor_b];
                        assert(element->kind == PNTRELEM_WALL_DECOR);
                        if( !decor_hidden )
                        {
                            bucket_emit_entity(&cmd_cur, cmd_end, element->_wall_decor.entity);
                            painter_wedgelog_paint(
                                e_tile, "decor_back_alt", (int)element->source_level,
                                element->_wall_decor.entity, tile->wall_decor_b);
                        }
                    }
                }
                else if( (element->_wall_decor._bf_side & tile_paint->near_wall_flags) != 0 )
                {
                    if( !decor_hidden )
                    {
                        bucket_emit_entity(&cmd_cur, cmd_end, element->_wall_decor.entity);
                        painter_wedgelog_paint(
                            e_tile, "decor_back", (int)element->source_level,
                            element->_wall_decor.entity, tile->wall_decor_a);
                    }
                }
            }

            if( tile->wall_a != -1 )
            {
                struct PaintersElement* element = &elements[tile->wall_a];
                assert(element->kind == PNTRELEM_WALL_A);
                if( (element->_wall.side & tile_paint->near_wall_flags) != 0 &&
                    !(occ && scene_occluders_wall_hidden(
                                 occ, occlusion_level, tile_sx, tile_sz, element->_wall.side)) )
                {
                    bucket_emit_entity(&cmd_cur, cmd_end, element->_wall.entity);
                    painter_wedgelog_paint(
                        e_tile, "wall_back_a", (int)element->source_level, element->_wall.entity,
                        tile->wall_a);
                }
            }

            if( tile->wall_b != -1 )
            {
                struct PaintersElement* element = &elements[tile->wall_b];
                assert(element->kind == PNTRELEM_WALL_B);
                if( (element->_wall.side & tile_paint->near_wall_flags) != 0 &&
                    !(occ && scene_occluders_wall_hidden(
                                 occ, occlusion_level, tile_sx, tile_sz, element->_wall.side)) )
                {
                    bucket_emit_entity(&cmd_cur, cmd_end, element->_wall.entity);
                    painter_wedgelog_paint(
                        e_tile, "wall_back_b", (int)element->source_level, element->_wall.entity,
                        tile->wall_b);
                }
            }
        }

        tile_paint->step = PAINT_STEP_DONE;
        tiles_remaining--;

        if( paintgrid_level < levels - 1 )
        {
            bucket_push_if_active(
                w,
                paints,
                e_tile + level_stride,
                tile_dist,
                &perf_pushes,
                &perf_push_dedup);
        }

        if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
        {
            bucket_push_if_active(
                w,
                paints,
                e_tile + width,
                adx + abs(tile_sz + 1 - camera_sz),
                &perf_pushes,
                &perf_push_dedup);
        }
        if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
        {
            bucket_push_if_active(
                w,
                paints,
                e_tile - 1,
                abs(tile_sx - 1 - camera_sx) + adz,
                &perf_pushes,
                &perf_push_dedup);
        }
        if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
        {
            bucket_push_if_active(
                w,
                paints,
                e_tile - width,
                adx + abs(tile_sz - 1 - camera_sz),
                &perf_pushes,
                &perf_push_dedup);
        }
        if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) )
        {
            bucket_push_if_active(
                w,
                paints,
                e_tile + 1,
                abs(tile_sx + 1 - camera_sx) + adz,
                &perf_pushes,
                &perf_push_dedup);
        }
    }

    /* This context is finished. The command COUNT, the wedge-log close and the
     * order dump describe the whole stack, not one context, so the driver owns
     * them — writing them here would truncate an outer paint to an inner
     * paint's length. */
    cursor->cur = cmd_cur;

done:
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_POPS, perf_pops);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_GATE_REJECTS, perf_gate_rejects);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_PUSHES, perf_pushes);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_PUSH_DEDUP, perf_push_dedup);
    return BUCKET_RUN_DONE;

suspend:
    /* The descended context's setup may realloc buffer->commands, so the write
     * position goes back to the shared cursor before we return: the reload
     * after the resume picks up the rebased pointers. */
    cursor->cur = cmd_cur;
    ctx->tiles_remaining = tiles_remaining;
    ctx->check_adjacent = check_adjacent;
    ctx->seed_gen_initialized = seed_gen_initialized;
    ctx->seed_gen = seed_gen_local;
    ctx->perf_pops = perf_pops;
    ctx->perf_gate_rejects = perf_gate_rejects;
    ctx->perf_pushes = perf_pushes;
    ctx->perf_push_dedup = perf_push_dedup;
    return BUCKET_RUN_DESCEND;
}

/**
 * The paint driver: an explicit stack of suspendable contexts, never the C call
 * stack. stack[0] is the root painter; a world-entity pseudo-loc pushes the
 * view's painter with a camera already transformed into that view's space.
 */
int
painter_paint_bucket(
    struct Painter* painter,
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel)
{
    assert(painter);
    assert(buffer);

    struct BucketPaintCtx stack[PAINTER_MAX_WORLD_VIEWS];
    /* One bit per view id. The cheap first test only — what actually must not
     * repeat on the stack is the PAINTER, not the view id: re-entering one runs
     * its `!ctx->started` setup, which memsets element_paints and resets the
     * bucket heap out from under the suspended frame below. Two distinct view
     * ids bound to the same painter (root(1)->A, A(2)->B, B(3)->A) clear every
     * bit test and still corrupt it, so the identity scan below is the guard
     * and this is just the fast path. */
    uint32_t on_stack = 1u; /* view 0 (the root) is always on the stack */
    int depth = 0;

    memset(&stack[0], 0, sizeof(stack[0]));
    stack[0].painter = painter;
    stack[0].camera_sx = camera_sx;
    stack[0].camera_sz = camera_sz;
    stack[0].camera_slevel = camera_slevel;
    stack[0].view_id = 0;
    stack[0].wedgelog = 1;

    struct BucketPaintCursor cursor;
    cursor.buffer = buffer;
    cursor.base = buffer->commands;
    cursor.cur = buffer->commands;
    cursor.end = buffer->commands + buffer->command_capacity;

    for( ;; )
    {
        assert(depth >= 0);
        assert(depth < PAINTER_MAX_WORLD_VIEWS);
        struct BucketPaintCtx* top = &stack[depth];
        int result;
        if( top->wedgelog )
        {
            result = bucket_paint_run(top, &cursor);
        }
        else
        {
            /* A nested world's rows would index the BOUND painter's tiles[]
             * with this painter's indices. Closing the log for the duration is
             * what enforces that, so the drain's ~20 emit sites stay free of
             * any per-row ownership test. @see painter_wedgelog_suspend. */
            int saved = painter_wedgelog_suspend();
            result = bucket_paint_run(top, &cursor);
            painter_wedgelog_resume(saved);
        }

        if( result == BUCKET_RUN_DESCEND )
        {
            int view_id = top->descend_view;
            assert(view_id > 0);
            assert(view_id < PAINTER_MAX_WORLD_VIEWS);
            const struct PainterWorldEntityView* view =
                &top->painter->world_entity_views[view_id];

            /* Linear over at most PAINTER_MAX_WORLD_VIEWS live frames, and only
             * on a descent — cheaper than carrying a side table, and it catches
             * the same painter arriving under a second view id. */
            int painter_on_stack = 0;
            for( int d = 0; d <= depth; d++ )
            {
                if( stack[d].painter == view->painter )
                {
                    painter_on_stack = 1;
                    break;
                }
            }

            /* An unbound view, a repeat of a view id or of a painter, or a full
             * stack is not a descent: emit the closing marker immediately so the
             * stream stays balanced, and let the outer context carry on. */
            if( !view->active || (on_stack & (1u << view_id)) || painter_on_stack ||
                depth + 1 >= PAINTER_MAX_WORLD_VIEWS )
            {
                bucket_cursor_reserve(&cursor, 1);
                bucket_emit_world_marker(
                    &cursor.cur, cursor.end, PNTR_CMD_END_WORLD, view_id);
                continue;
            }

            depth++;
            on_stack |= (1u << view_id);
            memset(&stack[depth], 0, sizeof(stack[depth]));
            stack[depth].painter = view->painter;
            stack[depth].camera_sx = view->camera_sx;
            stack[depth].camera_sz = view->camera_sz;
            stack[depth].camera_slevel = view->camera_slevel;
            stack[depth].view_id = view_id;
            /* Only the root drives the wedge log: an inner context indexes a
             * different tile array. @see painter_wedgelog_suspend. */
            stack[depth].wedgelog = 0;
            continue;
        }

        assert(result == BUCKET_RUN_DONE);
        if( depth == 0 )
            break;

        int view_id = top->view_id;
        assert(view_id > 0);
        bucket_cursor_reserve(&cursor, 1);
        bucket_emit_world_marker(&cursor.cur, cursor.end, PNTR_CMD_END_WORLD, view_id);
        on_stack &= ~(1u << view_id);
        depth--;
    }

    buffer->command_count = (int)(cursor.cur - cursor.base);
    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_PAINTER_COMMANDS, buffer->command_count);

    painter_wedgelog_frame_end((long)buffer->command_count);
    painter_dump_command_order(painter, buffer);
    return 0;
}

static int
painter_depth_tile_visible(
    struct Painter* painter,
    int sx,
    int sz,
    int camera_sx,
    int camera_sz)
{
    const struct PaintersCullMap* cullmap = painter->cullmap;
    if( painter->cullspan_active )
    {
        int lo;
        int hi;
        if( !painters_cullspan_row(&painter->cullspan, sz - camera_sz, &lo, &hi) )
            return 0;
        return sx >= camera_sx + lo && sx <= camera_sx + hi;
    }
    if( !cullmap || cullmap->all_visible )
        return 1;
    {
        int dx = sx - camera_sx;
        int dz = sz - camera_sz;
        int radius = cullmap->radius;
        size_t index;
        if( dx < -radius || dx > radius || dz < -radius || dz > radius )
            return 0;
        index = painter->cull_camera_key +
            (size_t)(dx + radius) * (size_t)cullmap->grid_side +
            (size_t)(dz + radius);
        return pcull_bit_get(cullmap->visibility, index) != 0;
    }
}

static void
painter_depth_emit_scenery(
    struct Painter* painter,
    struct PaintersElementCommand** cur,
    struct PaintersElementCommand* end,
    int scenery_element,
    int occlusion_level,
    int use_occlusion)
{
    struct PaintersElement* element;
    if( scenery_element < 0 || scenery_element >= painter->element_count ||
        painter->element_paints[scenery_element].drawn )
        return;
    element = &painter->elements[scenery_element];
    if( element->kind != PNTRELEM_SCENERY )
        return;
    /* The depth collector (TORIRS_WORLD_DEPTH) has no descent: it emits a flat
     * element list, so a world entity has nothing to expand into. Consume it
     * rather than emit a view id as an element id. */
    if( scenery_is_world_entity(element) )
    {
        painter->element_paints[scenery_element].drawn = 1;
        return;
    }
    painter->element_paints[scenery_element].drawn = 1;
    if( use_occlusion && painter->occluders &&
        scene_occluders_footprint_hidden(
            painter->occluders,
            occlusion_level,
            (int)element->sx,
            (int)element->sz,
            element->_scenery.size_x,
            element->_scenery.size_z,
            element->_scenery.model_height) )
        return;
    bucket_emit_entity(cur, end, element->_scenery.entity);
}

int
painter_collect_visible_depth(
    struct Painter* painter,
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel)
{
    int width;
    int levels;
    int level_stride;
    int radius;
    int min_draw_x;
    int max_draw_x;
    int min_draw_z;
    int max_draw_z;
    int draw_center_sx;
    int draw_center_sz;
    int need_cmds;
    uint8_t draw_mask;
    struct PaintersElementCommand* cmd_base;
    struct PaintersElementCommand* cmd_cur;
    struct PaintersElementCommand* cmd_end;
    assert(painter);
    assert(buffer);

    width = painter->width;
    levels = painter->levels;
    level_stride = painter->width * painter->height;
    radius = painter->draw_distance;
    draw_mask = painter->level_mask ? painter->level_mask : 0xfu;
    buffer->command_count = 0;
    memset(
        painter->element_paints,
        0,
        (size_t)painter->element_count * sizeof(painter->element_paints[0]));
    painter_resolve_draw_box(
        painter,
        camera_sx,
        camera_sz,
        radius,
        &draw_center_sx,
        &draw_center_sz,
        &min_draw_x,
        &max_draw_x,
        &min_draw_z,
        &max_draw_z);
    (void)draw_center_sx;
    (void)draw_center_sz;
    if( min_draw_x >= max_draw_x || min_draw_z >= max_draw_z )
        return 0;
    painter_cullmap_refresh_camera_key(painter);

    need_cmds = 2 * painter->element_count +
        8 * (max_draw_x - min_draw_x) * (max_draw_z - min_draw_z) + 16;
    if( need_cmds < 128 )
        need_cmds = 128;
    if( buffer->command_capacity < need_cmds )
    {
        int capacity = buffer->command_capacity > 0 ? buffer->command_capacity : 128;
        while( capacity < need_cmds )
            capacity *= 2;
        buffer->commands = (struct PaintersElementCommand*)realloc(
            buffer->commands,
            (size_t)capacity * sizeof(buffer->commands[0]));
        assert(buffer->commands);
        buffer->command_capacity = capacity;
    }
    cmd_base = buffer->commands;
    cmd_cur = cmd_base;
    cmd_end = cmd_base + buffer->command_capacity;

    /* Stable level/row order is retained only as a deterministic coplanar tie
     * rule. Visibility does not depend on adjacency readiness or distance. */
    for( int s = 0; s < levels; s++ )
    {
        for( int sz = min_draw_z; sz < max_draw_z; sz++ )
        {
            for( int sx = min_draw_x; sx < max_draw_x; sx++ )
            {
                int tile_index = sx + sz * width + s * level_stride;
                struct PaintersTile* tile = &painter->tiles[tile_index];
                struct TilePaint* tile_paint = &painter->tile_paints[tile_index];
                struct SceneOccluders* occ = painter->occluders;
                int occlusion_level;
                int ground_hidden;
                if( tile_excluded_by_bridge_or_draw_mask(
                        painters_tile_get_flags(tile),
                        painters_tile_get_visible_gte_level(tile),
                        draw_mask) ||
                    !painter_depth_tile_visible(
                        painter,
                        sx,
                        sz,
                        camera_sx,
                        camera_sz) )
                    continue;
                tile_paint->occlusion = TILE_OCCLUSION_UNKNOWN;
                occlusion_level = painters_tile_get_mesh_level(tile);
                ground_hidden =
                    occ ? painter_tile_ground_hidden(
                              occ, tile_paint, occlusion_level, sx, sz)
                        : 0;

                if( tile->bridge_tile != -1 )
                {
                    struct PaintersTile* underpass = &painter->tiles[tile->bridge_tile];
                    int under_level = painters_tile_get_mesh_level(underpass);
                    if( !(occ && scene_occluders_ground_tile_hidden(
                                      occ,
                                      under_level,
                                      underpass->sx,
                                      underpass->sz)) )
                    {
                        unsigned terrain = underpass->terrain_levels;
                        for( int ml = 0; ml < 4; ml++ )
                            if( terrain & (1u << ml) )
                                bucket_emit_terrain(
                                    &cmd_cur,
                                    cmd_end,
                                    underpass->sx,
                                    underpass->sz,
                                    ml);
                    }
                    if( underpass->wall_a >= 0 )
                    {
                        struct PaintersElement* element =
                            &painter->elements[underpass->wall_a];
                        if( element->kind == PNTRELEM_WALL_A )
                            bucket_emit_entity(&cmd_cur, cmd_end, element->_wall.entity);
                    }
                    for( int32_t node = underpass->scenery_head; node != -1;
                         node = painter->scenery_pool[node].next )
                        painter_depth_emit_scenery(
                            painter,
                            &cmd_cur,
                            cmd_end,
                            painter->scenery_pool[node].element_idx,
                            under_level,
                            0);
                }

                {
                    unsigned terrain = tile->terrain_levels;
                    for( int ml = 0; ml < 4; ml++ )
                        if( terrain & (1u << ml) )
                        {
                            if( !ground_hidden )
                                bucket_emit_terrain(&cmd_cur, cmd_end, sx, sz, ml);
                            else if( camera_slevel >= 0 && ml <= camera_slevel )
                                bucket_emit_terrain_pick_only(
                                    &cmd_cur, cmd_end, sx, sz, ml);
                        }
                }

                if( tile->wall_a >= 0 )
                {
                    struct PaintersElement* element = &painter->elements[tile->wall_a];
                    if( element->kind == PNTRELEM_WALL_A &&
                        !(occ && scene_occluders_wall_hidden(
                                     occ,
                                     occlusion_level,
                                     sx,
                                     sz,
                                     element->_wall.side)) )
                        bucket_emit_entity(&cmd_cur, cmd_end, element->_wall.entity);
                }
                if( tile->wall_b >= 0 )
                {
                    struct PaintersElement* element = &painter->elements[tile->wall_b];
                    if( element->kind == PNTRELEM_WALL_B &&
                        !(occ && scene_occluders_wall_hidden(
                                     occ,
                                     occlusion_level,
                                     sx,
                                     sz,
                                     element->_wall.side)) )
                        bucket_emit_entity(&cmd_cur, cmd_end, element->_wall.entity);
                }
                if( tile->ground_decor >= 0 && painter_ground_decor_enabled() )
                {
                    struct PaintersElement* element =
                        &painter->elements[tile->ground_decor];
                    if( element->kind == PNTRELEM_GROUND_DECOR &&
                        !(occ && scene_occluders_column_hidden(
                                     occ,
                                     occlusion_level,
                                     sx,
                                     sz,
                                     0)) )
                        bucket_emit_entity(
                            &cmd_cur,
                            cmd_end,
                            element->_ground_decor.entity);
                }
                if( tile->ground_object_bottom >= 0 )
                {
                    struct PaintersElement* element =
                        &painter->elements[tile->ground_object_bottom];
                    if( element->kind == PNTRELEM_GROUND_OBJECT &&
                        !(occ && scene_occluders_column_hidden(
                                     occ,
                                     occlusion_level,
                                     sx,
                                     sz,
                                     0)) )
                        bucket_emit_entity(
                            &cmd_cur,
                            cmd_end,
                            element->_ground_object.entity);
                }
                if( tile->wall_decor_a >= 0 )
                {
                    int decor_index = tile->wall_decor_a;
                    struct PaintersElement* element =
                        &painter->elements[decor_index];
                    if( element->kind == PNTRELEM_WALL_DECOR &&
                        element->_wall_decor._bf_through_wall_flags != 0 &&
                        tile->wall_decor_b >= 0 )
                    {
                        int x_diff = (int)element->sx - camera_sx;
                        int z_diff = (int)element->sz - camera_sz;
                        int x_near = x_diff;
                        int z_near = z_diff;
                        if( element->_wall_decor._bf_side == WALL_CORNER_NORTHEAST ||
                            element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST )
                            x_near = -x_near;
                        if( element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST ||
                            element->_wall_decor._bf_side == WALL_CORNER_SOUTHWEST )
                            z_near = -z_near;
                        if( z_near >= x_near )
                        {
                            decor_index = tile->wall_decor_b;
                            element = &painter->elements[decor_index];
                        }
                    }
                    int hidden = element->kind != PNTRELEM_WALL_DECOR ||
                        (occ && scene_occluders_column_hidden(
                                    occ,
                                    occlusion_level,
                                    sx,
                                    sz,
                                    element->_wall_decor.model_height));
                    if( !hidden )
                        bucket_emit_entity(
                            &cmd_cur,
                            cmd_end,
                            element->_wall_decor.entity);
                }

                for( int32_t node = tile->scenery_head; node != -1;
                     node = painter->scenery_pool[node].next )
                    painter_depth_emit_scenery(
                        painter,
                        &cmd_cur,
                        cmd_end,
                        painter->scenery_pool[node].element_idx,
                        occlusion_level,
                        1);
            }
        }
    }
    buffer->command_count = (int)(cmd_cur - cmd_base);
    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_PAINTER_COMMANDS, buffer->command_count);
    return 0;
}

#undef BUCKET_PERF_INCREMENT

#endif /* PAINTERS_BUCKET_U_C */
