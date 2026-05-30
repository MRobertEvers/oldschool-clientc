#ifndef TRSPK_ATLAS_H
#define TRSPK_ATLAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
/* Atlas mode                                                          */
/* ------------------------------------------------------------------ */

enum TRSPK_AtlasMode
{
    TRSPK_ATLAS_MODE_GRID = 0,
    TRSPK_ATLAS_MODE_BINPACK = 1,
};

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
};

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
