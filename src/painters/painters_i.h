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
    /**
     * Per-frame ground-occlusion tri-state for this tile:
     *   0 = not yet computed
     *   1 = ground hidden behind an occluder
     *   2 = ground visible
     * Computed once in the ground pass; emit sites read it instead of
     * re-running the four-corner test.
     */
    uint8_t occlusion;
};

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
    if( !occ || occ->active_count == 0 )
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
 * Sort a ready batch of scenery element indices for emission, reference
 * order: farthest-corner key descending, centre-distance tie-break. The
 * official runs a selection loop picking the max each time; batches are <=5
 * per tile there, so the same O(n^2) selection is fine here.
 *
 * Cost discipline: this runs at most once per tile pop, each element is in
 * exactly one emitted batch (drawn elements leave the set), batches of 0/1
 * return immediately, and both keys are computed ONCE per element rather
 * than per comparison.
 */
#define SCENERY_SORT_BATCH_MAX 64

static inline void
scenery_sort_ready_batch(
    struct Painter* painter,
    int* batch,
    int count,
    int camera_sx,
    int camera_sz)
{
    int keys[SCENERY_SORT_BATCH_MAX];
    int64_t tie[SCENERY_SORT_BATCH_MAX];
    int camera_fine_x = camera_sx * 128 + 64;
    int camera_fine_z = camera_sz * 128 + 64;

    if( count <= 1 )
        return;
    assert(count <= SCENERY_SORT_BATCH_MAX);

    for( int a = 0; a < count; a++ )
    {
        keys[a] = scenery_far_corner_dist(&painter->elements[batch[a]], camera_sx, camera_sz);
        tie[a] = scenery_centre_dist_sq(&painter->elements[batch[a]], camera_fine_x, camera_fine_z);
    }
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
            int tmp_i = batch[a];
            int tmp_k = keys[a];
            int64_t tmp_t = tie[a];
            batch[a] = batch[best];
            keys[a] = keys[best];
            tie[a] = tie[best];
            batch[best] = tmp_i;
            keys[best] = tmp_k;
            tie[best] = tmp_t;
        }
    }
}

#endif
