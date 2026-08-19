#ifndef PAINTERS_I_H
#define PAINTERS_I_H

/* Internal types and layout for the painters translation unit (painters.c + painters_*.u.c). */

#include "painters.h"
#include "scene_occluders.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum TilePaintStep
{
    // Do not draw ground until adjacent tiles are done,
    // unless we are spanned by that tile.
    // PAINT_STEP_UNREACHABLE = 0,
    PAINT_STEP_READY,
    PAINT_STEP_GROUND,
    PAINT_STEP_WAIT_ADJACENT_GROUND,
    PAINT_STEP_LOCS,
    PAINT_STEP_NEAR_WALL,
    PAINT_STEP_DONE,
};

/** Tri-state for TilePaint.occlusion (ground-tile hidden verdict). */
enum TileOcclusion
{
    TILE_OCCLUSION_UNKNOWN = 0,
    TILE_OCCLUSION_HIDDEN = 1,
    TILE_OCCLUSION_VISIBLE = 2,
};

struct TilePaint
{
    int32_t queue_next; /* bucket intrusive list; -1 = end (valid only when in_queue) */
    uint8_t step;
    uint8_t queue_count; /* distancemetric painter only */
    uint8_t near_wall_flags;
    uint8_t in_queue;
    /** Scenery chain already in reference emission order this paint (see
     *  scenery_chain_sort_once). Cleared in the classify pass. */
    uint8_t scenery_sorted;
    /**
     * Per-frame ground-occlusion tri-state for this tile:
     *   0 = not yet computed
     *   1 = ground hidden behind an occluder
     *   2 = ground visible
     * Computed once in the ground pass; emit sites read it instead of
     * re-running the four-corner test.
     */
    uint8_t occlusion;
    /** Bucket painter: the ground pass was let through by the seam exception
     *  while a far neighbour was still pending. The tile's scenery and completion then wait for the plain
     *  reference gate (see painter_paint_bucket). Cleared in the classify pass. */
    uint8_t seam_relaxed;
    /** Bucket painter: memoized seam-exception scan of THIS tile as a
     *  neighbour. 0 = not yet scanned this paint (cleared in classify).
     *  Else 1 + clamped max near-corner distance of this tile's pending
     *  scenery, or SEAM_SCAN_DISQUALIFIED for "has walls / nothing pending".
     *  Elements only leave the pending set as they draw, so the cached max is
     *  an upper bound: a stale value can only delay a relaxation (the gate
     *  blocks and the tile retries), never grant one wrongly. */
    uint8_t seam_scan;
};

#define SEAM_SCAN_DISQUALIFIED 255

struct ElementPaint
{
    uint8_t drawn;
};

struct Painter
{
    int width;
    int height;
    int levels;

    const struct PaintersCullMap* cullmap;
    int camera_pitch;
    int camera_yaw;
    /** Bit offset for current (pitch,yaw) slice: pcull_bit_index(pitch_idx, yaw_idx, 0, 0, ...). */
    size_t cull_camera_key;

    /** Per-frame analytic frustum spans; when cullspan_active, replaces the
     * baked cullmap bit test. */
    struct PaintersCullSpan cullspan;
    int cullspan_active;

    /** Draw-box centre (orbit target). -1 = use the eye tile from paint(). */
    int draw_center_sx;
    int draw_center_sz;
    int has_draw_center;

    /**
     * Draw-box / occluder footprint radius in tiles (Scene.drawDistance).
     * Clamped to [OCCLUDER_DRAW_DISTANCE_MIN, OCCLUDER_DRAW_DISTANCE_MAX]
     * by painter_set_draw_distance. Default 25.
     */
    int draw_distance;

    /** Bitmask: bit s set => level s participates in paint (0-3 for MAP_TERRAIN_LEVELS). Default
     * 0xF. */
    uint8_t level_mask;
    /** Lowest set bit in level_mask; 0 when mask is all bits or unset. */
    uint8_t min_level;

    int static_element_count;

    /** While set, the single-slot registrations (wall / wall_decor /
     *  ground_decor) no-op instead of asserting on an occupied slot. Used by
     *  WorldBuilder_ApplyLocChange so a runtime loc spawn reuses the build path
     *  without clobbering the baked static tile slots — the spawned loc is drawn
     *  via the per-frame painter_add_normal_scenery pass (world_cycle) instead. */
    int suppress_slot_registration;

    struct PaintersTile* tiles;
    struct TilePaint* tile_paints;
    int tile_count;
    int tile_capacity;

    struct SceneryNode* scenery_pool;
    int scenery_pool_count;
    int scenery_pool_capacity;

    struct PaintersElement* elements;
    struct ElementPaint* element_paints;
    int element_count;
    int element_capacity;

    void* bucket_ctx;
    void* w3d_ctx;
    void* distmetric_ctx;

    /** Planar occluders for this scene; owned by the painter when set via
     * painter_set_occluders. NULL = occlusion disabled. */
    struct SceneOccluders* occluders;
};

/**
 * Bridge tiles are drawn via bridge_tile, not the normal level iteration. Otherwise a tile is
 * excluded when draw_mask has no bit for its draw slevel (slevel is set at world build from
 * cache VisBelow / LinkBelow; the painter does not read floor flags at draw time).
 */
static inline bool
tile_excluded_by_bridge_or_draw_mask(
    uint16_t tile_flags,
    int tile_slevel,
    uint8_t draw_mask)
{
    if( (tile_flags & PAINTERS_TILE_FLAG_BRIDGE) != 0 )
        return true;
    return (draw_mask & (1u << tile_slevel)) == 0;
}

/**
 * Cached ground-tile occlusion for this paint slot. Computes once per frame
 * via scene_occluders_ground_tile_hidden and stores the tri-state on tp.
 * Returns true when the ground is hidden (skip terrain emit).
 * Requires scene_occluders.h visible in the including TU.
 */
static inline int
painter_tile_ground_hidden(
    struct SceneOccluders* occ,
    struct TilePaint* tp,
    int level,
    int sx,
    int sz)
{
    /* Whether the painter HAS occluders is the caller's question; this one
     * needs a set to test against. */
    assert(occ);
    assert(tp);
    if( occ->active_count == 0 )
        return 0;
    if( tp->occlusion == TILE_OCCLUSION_HIDDEN )
        return 1;
    if( tp->occlusion == TILE_OCCLUSION_VISIBLE )
        return 0;
    if( scene_occluders_ground_tile_hidden(occ, level, sx, sz) )
    {
        tp->occlusion = TILE_OCCLUSION_HIDDEN;
        return 1;
    }
    tp->occlusion = TILE_OCCLUSION_VISIBLE;
    return 0;
}

/** True when el is RAISED ground-item scenery (emit at tile completion). */
static inline int
scenery_is_raised(const struct PaintersElement* el)
{
    return el->kind == PNTRELEM_SCENERY && (el->_scenery.flags & PNTR_SCENERY_RAISED) != 0;
}

/**
 * True when outer's footprint strictly contains inner's (larger area, inner
 * min/max inside outer). Used to defer stacked 1x1 locs behind multi-tile bases.
 */
static inline int
scenery_footprint_contains(
    const struct PaintersElement* outer,
    const struct PaintersElement* inner)
{
    int o_area;
    int i_area;
    int o_max_x;
    int o_max_z;
    int i_max_x;
    int i_max_z;

    assert(outer && inner);
    assert(outer->kind == PNTRELEM_SCENERY);
    assert(inner->kind == PNTRELEM_SCENERY);
    o_area = (int)outer->_scenery.size_x * (int)outer->_scenery.size_z;
    i_area = (int)inner->_scenery.size_x * (int)inner->_scenery.size_z;
    if( o_area <= i_area )
        return 0;
    o_max_x = (int)outer->sx + (int)outer->_scenery.size_x - 1;
    o_max_z = (int)outer->sz + (int)outer->_scenery.size_z - 1;
    i_max_x = (int)inner->sx + (int)inner->_scenery.size_x - 1;
    i_max_z = (int)inner->sz + (int)inner->_scenery.size_z - 1;
    return (int)inner->sx >= (int)outer->sx && (int)inner->sz >= (int)outer->sz &&
           i_max_x <= o_max_x && i_max_z <= o_max_z;
}

/*
 * Reference class112.method3971: a ready entity's draw-order key is the
 * Manhattan distance from the camera tile to the FARTHEST corner of its
 * footprint (max extent per axis, summed). The drain draws the ready batch
 * max-key first, ties broken by the squared fine distance of the entity
 * centre (class112.java:1030-1058). This is the whole of the reference's
 * loc-vs-loc ordering — there is no containment rule and no static/dynamic
 * distinction; an earlier containment heuristic here inverted the order
 * around every multi-tile loc (docs/ORANGE_WEDGE.md §24-25).
 */
static inline int
scenery_far_corner_dist(
    const struct PaintersElement* el,
    int camera_sx,
    int camera_sz)
{
    int span_x = camera_sx - (int)el->sx;
    int span_x2 = (int)el->sx + (int)el->_scenery.size_x - 1 - camera_sx;
    int span_z = camera_sz - (int)el->sz;
    int span_z2 = (int)el->sz + (int)el->_scenery.size_z - 1 - camera_sz;
    if( span_x2 > span_x )
        span_x = span_x2;
    if( span_z2 > span_z )
        span_z = span_z2;
    return span_x + span_z;
}

/*
 * Manhattan distance from the camera tile to the NEAREST tile of an entity's
 * footprint — the ring the drain actually emits it at. A loc is released only
 * once every tile it covers has its ground down, and the last of those to
 * happen is always the one closest to the eye, so this is where a multi-tile
 * loc lands no matter how far its far corner reaches.
 *
 * The counterpart to scenery_far_corner_dist, which is the reference's
 * loc-vs-loc sort key and answers a different question.
 */
static inline int
scenery_near_corner_dist(
    const struct PaintersElement* el,
    int camera_sx,
    int camera_sz)
{
    int min_x = (int)el->sx;
    int max_x = min_x + (int)el->_scenery.size_x - 1;
    int min_z = (int)el->sz;
    int max_z = min_z + (int)el->_scenery.size_z - 1;
    int dx = camera_sx < min_x ? min_x - camera_sx : (camera_sx > max_x ? camera_sx - max_x : 0);
    int dz = camera_sz < min_z ? min_z - camera_sz : (camera_sz > max_z ? camera_sz - max_z : 0);
    return dx + dz;
}

/* The reference tie-break: squared fine distance from the camera position to
 * the entity's centre (class112.java:1040-1046 uses the entity's stored fine
 * coords; ours is the footprint centre, the same point placement uses). */
static inline int64_t
scenery_centre_dist_sq(
    const struct PaintersElement* el,
    int camera_fine_x,
    int camera_fine_z)
{
    int64_t cx = (int64_t)el->sx * 128 + (int64_t)el->_scenery.size_x * 64;
    int64_t cz = (int64_t)el->sz * 128 + (int64_t)el->_scenery.size_z * 64;
    int64_t dx = cx - camera_fine_x;
    int64_t dz = cz - camera_fine_z;
    return dx * dx + dz * dz;
}

/*
 * Sort a tile's scenery CHAIN into reference emission order, once per paint:
 * farthest-corner key descending, centre-distance tie-break.
 *
 * The chain is fixed before the drain (registration happens at build /
 * per-frame re-add, never mid-paint) and the keys depend only on the camera
 * tile, which is constant for the whole paint — so one sort per tile per
 * paint is complete. Every later pop of the tile emits its READY subset in
 * chain order, and an in-order subset of a sorted list is sorted. The
 * `scenery_sorted` latch on TilePaint is cleared in the classify pass.
 *
 * The permutation moves (element_idx, span) PAIRS between nodes and leaves
 * the node topology alone: `span` is per (tile, element), and
 * tile_remove_scenery_element unlinks by element payload, so a permuted
 * chain stays reset-safe when dynamic elements are stripped.
 *
 * Chains are tiny (the official caps at 5 per tile); anything past the cap
 * keeps its unsorted tail rather than growing the scratch.
 */
#define SCENERY_SORT_BATCH_MAX 64

static inline void
scenery_chain_sort_once(
    struct Painter* painter,
    struct PaintersTile* tile,
    struct TilePaint* tile_paint,
    int camera_sx,
    int camera_sz)
{
    int32_t nodes[SCENERY_SORT_BATCH_MAX];
    int16_t elems[SCENERY_SORT_BATCH_MAX];
    uint8_t spans[SCENERY_SORT_BATCH_MAX];
    int keys[SCENERY_SORT_BATCH_MAX];
    int64_t tie[SCENERY_SORT_BATCH_MAX];
    int count = 0;
    int camera_fine_x = camera_sx * 128 + 64;
    int camera_fine_z = camera_sz * 128 + 64;

    if( tile_paint->scenery_sorted )
        return;
    tile_paint->scenery_sorted = 1;

    for( int32_t sn = tile->scenery_head; sn != -1 && count < SCENERY_SORT_BATCH_MAX;
         sn = painter->scenery_pool[sn].next )
    {
        struct SceneryNode const* node = &painter->scenery_pool[sn];
        nodes[count] = sn;
        elems[count] = node->element_idx;
        spans[count] = node->span;
        keys[count] =
            scenery_far_corner_dist(&painter->elements[node->element_idx], camera_sx, camera_sz);
        tie[count] = scenery_centre_dist_sq(
            &painter->elements[node->element_idx], camera_fine_x, camera_fine_z);
        count++;
    }
    if( count <= 1 )
        return;

    for( int a = 0; a < count; a++ )
    {
        int best = a;
        for( int b = a + 1; b < count; b++ )
        {
            if( keys[b] > keys[best] || (keys[b] == keys[best] && tie[b] > tie[best]) )
                best = b;
        }
        if( best != a )
        {
            int16_t tmp_e = elems[a];
            uint8_t tmp_s = spans[a];
            int tmp_k = keys[a];
            int64_t tmp_t = tie[a];
            elems[a] = elems[best];
            spans[a] = spans[best];
            keys[a] = keys[best];
            tie[a] = tie[best];
            elems[best] = tmp_e;
            spans[best] = tmp_s;
            keys[best] = tmp_k;
            tie[best] = tmp_t;
        }
    }
    for( int a = 0; a < count; a++ )
    {
        painter->scenery_pool[nodes[a]].element_idx = elems[a];
        painter->scenery_pool[nodes[a]].span = spans[a];
    }
}

#endif
