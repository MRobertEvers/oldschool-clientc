#ifndef PAINTERS_I_H
#define PAINTERS_I_H

/* Internal types and layout for the painters translation unit (painters.c + painters_*.u.c). */

#include "painters.h"
#include "scene_occluders.h"

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

    /** Bitmask: bit s set => level s participates in paint (0-3 for MAP_TERRAIN_LEVELS). Default 0xF. */
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

#endif
