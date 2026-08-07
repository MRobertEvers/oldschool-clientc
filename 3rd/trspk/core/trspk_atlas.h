#ifndef TRSPK_ATLAS_H
#define TRSPK_ATLAS_H

#include "trspk_flags.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TRSPK_ATLAS_TILE 128u

/* ------------------------------------------------------------------ */
/* Tile: pixel position + normalized UV bounds within an atlas         */
/* ------------------------------------------------------------------ */

struct TRSPK_AtlasTile
{
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;

    float u_start;
    float v_start;
    float u_end;
    float v_end;
};

/* ------------------------------------------------------------------ */
/* Free rectangle used internally by the bin-packing allocator         */
/* ------------------------------------------------------------------ */

struct TRSPK_AtlasFreeRect
{
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
};

/* ------------------------------------------------------------------ */
/* Dirty pixel bounds                                                  */
/* ------------------------------------------------------------------ */

/*
 * Bounding rectangle of all CPU-side pixel writes since the last
 * trspk_atlas_clear_dirty().  Coordinates and dimensions are in pixels.
 */
struct TRSPK_AtlasDirtyRect
{
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
};

/* ------------------------------------------------------------------ */
/* Atlas mode                                                          */
/* ------------------------------------------------------------------ */

enum TRSPK_AtlasMode
{
    TRSPK_ATLAS_MODE_GRID = 0,
    TRSPK_ATLAS_MODE_BINPACK = 1,
};

#define TRSPK_ATLAS_FLAG_INITIALIZED TRSPK_FLAG(0)
#define TRSPK_ATLAS_FLAG_DIRTY TRSPK_FLAG(1)

/* ------------------------------------------------------------------ */
/* Atlas                                                               */
/*                                                                     */
/* Grid mode:    fixed tile_w × tile_h cells, indexed by slot.        */
/*               Slot layout matches the shader convention:            */
/*               col = slot % grid_cols, row = slot / grid_cols.      */
/*                                                                     */
/* Bin-pack mode: guillotine allocator (BSSF + longer-axis split).    */
/*               Accepts variable-sized tiles.                         */
/* ------------------------------------------------------------------ */

struct TRSPK_Atlas
{
    uint32_t width;    /* atlas pixel width                         */
    uint32_t height;   /* atlas pixel height                        */
    uint32_t stride;   /* bytes per row  (= width * channels)       */
    uint32_t channels; /* bytes per pixel (1 = gray, 3 = RGB, 4 = RGBA) */
    uint8_t* pixels;   /* CPU-side pixel buffer (caller uploads GPU) */

    enum TRSPK_AtlasMode mode;

    /* Grid mode fields */
    uint32_t grid_tile_w; /* tile width  in pixels                  */
    uint32_t grid_tile_h; /* tile height in pixels                  */
    uint32_t grid_cols;   /* number of columns                      */
    uint32_t grid_rows;   /* number of rows                         */
    uint32_t grid_next;   /* next auto-insert slot (0-based)        */

    /* Bin-pack mode fields */
    struct TRSPK_AtlasFreeRect* free_rects;
    uint32_t free_rects_count;
    uint32_t free_rects_cap;

    struct TRSPK_AtlasDirtyRect dirty_rect;

    uint32_t flags;
};

static inline bool
trspk_atlas_is_initialized(const struct TRSPK_Atlas* atlas)
{
    return trspk_flags_test(atlas->flags, TRSPK_ATLAS_FLAG_INITIALIZED);
}

static inline void
trspk_atlas_set_initialized(struct TRSPK_Atlas* atlas)
{
    trspk_flags_set(&atlas->flags, TRSPK_ATLAS_FLAG_INITIALIZED);
}

static inline void
trspk_atlas_clear_initialized(struct TRSPK_Atlas* atlas)
{
    trspk_flags_clear(&atlas->flags, TRSPK_ATLAS_FLAG_INITIALIZED);
}

static inline bool
trspk_atlas_is_dirty(const struct TRSPK_Atlas* atlas)
{
    return trspk_flags_test(atlas->flags, TRSPK_ATLAS_FLAG_DIRTY);
}

static inline void
trspk_atlas_set_dirty(struct TRSPK_Atlas* atlas)
{
    /*
     * Backward-compatible conservative behavior for callers which mutate
     * atlas->pixels directly: without write bounds, the entire atlas may
     * have changed.
     */
    atlas->dirty_rect.x = 0;
    atlas->dirty_rect.y = 0;
    atlas->dirty_rect.w = atlas->width;
    atlas->dirty_rect.h = atlas->height;
    trspk_flags_set(&atlas->flags, TRSPK_ATLAS_FLAG_DIRTY);
}

static inline void
trspk_atlas_clear_dirty(struct TRSPK_Atlas* atlas)
{
    trspk_flags_clear(&atlas->flags, TRSPK_ATLAS_FLAG_DIRTY);
    atlas->dirty_rect.x = 0;
    atlas->dirty_rect.y = 0;
    atlas->dirty_rect.w = 0;
    atlas->dirty_rect.h = 0;
}

/*
 * Merge a changed pixel rectangle into the atlas dirty bounds.
 * The rectangle must be non-empty and wholly inside the atlas.
 * Returns false for invalid bounds without changing dirty state.
 */
bool
trspk_atlas_mark_dirty_rect(
    struct TRSPK_Atlas* atlas,
    uint32_t x,
    uint32_t y,
    uint32_t w,
    uint32_t h);

/*
 * Copy the current merged dirty bounds into rect_out.
 * Returns false when the atlas is clean. rect_out may be NULL.
 */
bool
trspk_atlas_get_dirty_rect(
    const struct TRSPK_Atlas* atlas,
    struct TRSPK_AtlasDirtyRect* rect_out);

/* ------------------------------------------------------------------ */
/* Pixel updates                                                       */
/* ------------------------------------------------------------------ */

/*
 * Copy a rectangular pixel block into the atlas and merge its exact
 * destination bounds into the dirty rectangle.
 */
bool
trspk_atlas_update_rect(
    struct TRSPK_Atlas* atlas,
    uint32_t dst_x,
    uint32_t dst_y,
    const uint8_t* src_pixels,
    uint32_t src_stride,
    uint32_t src_w,
    uint32_t src_h);

/* Zero a pixel rectangle and merge its exact bounds into the dirty rectangle. */
bool
trspk_atlas_clear_rect(
    struct TRSPK_Atlas* atlas,
    uint32_t x,
    uint32_t y,
    uint32_t w,
    uint32_t h);

/* Zero all atlas pixels and mark the complete atlas dirty. */
void
trspk_atlas_clear(struct TRSPK_Atlas* atlas);

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

/*
 * Initialise a grid-mode atlas.
 * Allocates and zero-fills the pixel buffer.
 * Returns false on invalid arguments or allocation failure.
 */
bool
trspk_atlas_init_grid(
    struct TRSPK_Atlas* atlas,
    uint32_t atlas_w,
    uint32_t atlas_h,
    uint32_t tile_w,
    uint32_t tile_h,
    uint32_t channels);

/*
 * Initialise a bin-pack-mode atlas.
 * Allocates and zero-fills the pixel buffer.
 * Returns false on invalid arguments or allocation failure.
 */
bool
trspk_atlas_init_binpack(
    struct TRSPK_Atlas* atlas,
    uint32_t atlas_w,
    uint32_t atlas_h,
    uint32_t channels);

/* Free the pixel buffer and any internal allocations. Does not free `atlas` itself. */
void
trspk_atlas_free(struct TRSPK_Atlas* atlas);

/* ------------------------------------------------------------------ */
/* Grid insertion                                                       */
/* ------------------------------------------------------------------ */

/*
 * Append pixels into the next available grid slot.
 * src_w / src_h must be <= tile_w / tile_h.
 * src_pixels may be NULL to reserve the slot without blitting.
 * Writes the resulting tile into tile_out (may be NULL).
 * Returns false when the atlas is full or arguments are invalid.
 */
bool
trspk_atlas_grid_insert(
    struct TRSPK_Atlas* atlas,
    const uint8_t* src_pixels,
    uint32_t src_stride,
    uint32_t src_w,
    uint32_t src_h,
    struct TRSPK_AtlasTile* tile_out);

/*
 * Insert pixels into an explicit grid slot.
 * Useful when the caller controls slot assignment (e.g. atlas_id 0-255).
 * src_pixels may be NULL to query the tile without blitting.
 */
bool
trspk_atlas_grid_insert_at(
    struct TRSPK_Atlas* atlas,
    uint32_t slot,
    const uint8_t* src_pixels,
    uint32_t src_stride,
    uint32_t src_w,
    uint32_t src_h,
    struct TRSPK_AtlasTile* tile_out);

/*
 * Compute the tile UV/position for a grid slot without blitting anything.
 * Returns false if slot is out of range or atlas is not in grid mode.
 */
bool
trspk_atlas_grid_tile_for_slot(
    const struct TRSPK_Atlas* atlas,
    uint32_t slot,
    struct TRSPK_AtlasTile* tile_out);

/* ------------------------------------------------------------------ */
/* Bin-pack insertion                                                   */
/* ------------------------------------------------------------------ */

/*
 * Insert pixels using the guillotine bin-packing allocator.
 * Best Short Side Fit (BSSF) heuristic selects the placement rectangle.
 * Longer-axis split divides the remainder.
 * src_pixels may be NULL to reserve space without blitting.
 * Returns false when no suitable free region exists.
 */
bool
trspk_atlas_binpack_insert(
    struct TRSPK_Atlas* atlas,
    const uint8_t* src_pixels,
    uint32_t src_stride,
    uint32_t src_w,
    uint32_t src_h,
    struct TRSPK_AtlasTile* tile_out);

#endif
