#ifndef PAINTERS_I_H
#define PAINTERS_I_H

/* Internal types and layout for the painters translation unit (painters.c + painters_*.u.c). */

#include "painters.h"

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

struct TilePaint
{
    uint8_t step;
    uint8_t queue_count;
    uint8_t near_wall_flags;
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

    /** Bitmask: bit s set => level s participates in paint (0-3 for MAP_TERRAIN_LEVELS). Default 0xF. */
    uint8_t level_mask;
    /** Lowest set bit in level_mask; 0 when mask is all bits or unset. */
    uint8_t min_level;

    int static_element_count;

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

#endif
