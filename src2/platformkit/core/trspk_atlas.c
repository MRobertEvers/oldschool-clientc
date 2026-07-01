#include "platformkit/core/trspk_atlas.h"

#include "toridraw/toridraw_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static void
tile_fill(
    struct TRSPK_AtlasTile* tile,
    uint32_t                tile_x,
    uint32_t                tile_y,
    uint32_t                tile_w,
    uint32_t                tile_h,
    uint32_t                atlas_w,
    uint32_t                atlas_h)
{
    tile->x       = tile_x;
    tile->y       = tile_y;
    tile->w       = tile_w;
    tile->h       = tile_h;
    tile->u_start = (float)tile_x / (float)atlas_w;
    tile->v_start = (float)tile_y / (float)atlas_h;
    tile->u_end   = (float)(tile_x + tile_w) / (float)atlas_w;
    tile->v_end   = (float)(tile_y + tile_h) / (float)atlas_h;
}

static void
blit_pixels(
    uint8_t*       dst,
    uint32_t       dst_x,
    uint32_t       dst_y,
    uint32_t       dst_stride,
    const uint8_t* src,
    uint32_t       src_stride,
    uint32_t       src_w,
    uint32_t       src_h,
    uint32_t       channels)
{
    uint32_t row_bytes = src_w * channels;
    for( uint32_t row = 0; row < src_h; row++ )
    {
        uint8_t*       dst_row = dst + ((dst_y + row) * dst_stride) + (dst_x * channels);
        const uint8_t* src_row = src + (row * src_stride);
        memcpy(dst_row, src_row, row_bytes);
    }
}

/* ------------------------------------------------------------------ */
/* Grid — lifetime                                                     */
/* ------------------------------------------------------------------ */

bool
trspk_atlas_init_grid(
    struct TRSPK_Atlas* atlas,
    uint32_t            atlas_w,
    uint32_t            atlas_h,
    uint32_t            tile_w,
    uint32_t            tile_h,
    uint32_t            channels)
{
    if( !atlas || !atlas_w || !atlas_h || !tile_w || !tile_h || !channels )
    {
        return false;
    }
    if( tile_w > atlas_w || tile_h > atlas_h )
    {
        return false;
    }

    uint32_t cols = atlas_w / tile_w;
    uint32_t rows = atlas_h / tile_h;
    if( !cols || !rows )
    {
        return false;
    }

    uint32_t stride = atlas_w * channels;
    uint8_t* pixels = (uint8_t*)calloc(1, (size_t)stride * atlas_h);
    if( !pixels )
    {
        return false;
    }

    atlas->width            = atlas_w;
    atlas->height           = atlas_h;
    atlas->stride           = stride;
    atlas->channels         = channels;
    atlas->pixels           = pixels;
    atlas->mode             = TRSPK_ATLAS_MODE_GRID;
    atlas->grid_tile_w      = tile_w;
    atlas->grid_tile_h      = tile_h;
    atlas->grid_cols        = cols;
    atlas->grid_rows        = rows;
    atlas->grid_next        = 0;
    atlas->free_rects       = NULL;
    atlas->free_rects_count = 0;
    atlas->free_rects_cap   = 0;
    trspk_atlas_set_initialized(atlas);
    return true;
}

/* ------------------------------------------------------------------ */
/* Grid — tile UV query                                                */
/* ------------------------------------------------------------------ */

bool
trspk_atlas_grid_tile_for_slot(
    const struct TRSPK_Atlas* atlas,
    uint32_t                  slot,
    struct TRSPK_AtlasTile*   tile_out)
{
    if( !atlas || !tile_out || atlas->mode != TRSPK_ATLAS_MODE_GRID )
    {
        return false;
    }

    uint32_t total = atlas->grid_cols * atlas->grid_rows;
    if( slot >= total )
    {
        return false;
    }

    uint32_t col = slot % atlas->grid_cols;
    uint32_t row = slot / atlas->grid_cols;
    tile_fill(
        tile_out,
        col * atlas->grid_tile_w,
        row * atlas->grid_tile_h,
        atlas->grid_tile_w,
        atlas->grid_tile_h,
        atlas->width,
        atlas->height);
    return true;
}

/* ------------------------------------------------------------------ */
/* Grid — insertion at explicit slot                                   */
/* ------------------------------------------------------------------ */

bool
trspk_atlas_grid_insert_at(
    struct TRSPK_Atlas*     atlas,
    uint32_t                slot,
    const uint8_t*          src_pixels,
    uint32_t                src_stride,
    uint32_t                src_w,
    uint32_t                src_h,
    struct TRSPK_AtlasTile* tile_out)
{
    if( !atlas || atlas->mode != TRSPK_ATLAS_MODE_GRID )
    {
        return false;
    }
    if( src_w > atlas->grid_tile_w || src_h > atlas->grid_tile_h )
    {
        return false;
    }

    struct TRSPK_AtlasTile tile;
    if( !trspk_atlas_grid_tile_for_slot(atlas, slot, &tile) )
    {
        return false;
    }

    if( src_pixels )
    {
        blit_pixels(
            atlas->pixels, tile.x, tile.y, atlas->stride,
            src_pixels, src_stride, src_w, src_h, atlas->channels);
        trspk_atlas_set_dirty(atlas);
    }

    if( tile_out )
    {
        *tile_out = tile;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Grid — sequential insertion                                         */
/* ------------------------------------------------------------------ */

bool
trspk_atlas_grid_insert(
    struct TRSPK_Atlas*     atlas,
    const uint8_t*          src_pixels,
    uint32_t                src_stride,
    uint32_t                src_w,
    uint32_t                src_h,
    struct TRSPK_AtlasTile* tile_out)
{
    if( !atlas || atlas->mode != TRSPK_ATLAS_MODE_GRID )
    {
        return false;
    }

    uint32_t total = atlas->grid_cols * atlas->grid_rows;
    if( atlas->grid_next >= total )
    {
        return false;
    }

    uint32_t slot = atlas->grid_next++;
    return trspk_atlas_grid_insert_at(
        atlas, slot, src_pixels, src_stride, src_w, src_h, tile_out);
}

/* ------------------------------------------------------------------ */
/* Bin-pack — lifetime                                                 */
/* ------------------------------------------------------------------ */

#define TRSPK_ATLAS_BINPACK_INIT_CAP 32u

bool
trspk_atlas_init_binpack(
    struct TRSPK_Atlas* atlas,
    uint32_t            atlas_w,
    uint32_t            atlas_h,
    uint32_t            channels)
{
    if( !atlas || !atlas_w || !atlas_h || !channels )
    {
        return false;
    }

    struct TRSPK_AtlasFreeRect* free_rects = (struct TRSPK_AtlasFreeRect*)malloc(
        TRSPK_ATLAS_BINPACK_INIT_CAP * sizeof(*free_rects));
    if( !free_rects )
    {
        return false;
    }

    uint32_t stride = atlas_w * channels;
    uint8_t* pixels = (uint8_t*)calloc(1, (size_t)stride * atlas_h);
    if( !pixels )
    {
        free(free_rects);
        return false;
    }

    free_rects[0].x = 0;
    free_rects[0].y = 0;
    free_rects[0].w = atlas_w;
    free_rects[0].h = atlas_h;

    atlas->width            = atlas_w;
    atlas->height           = atlas_h;
    atlas->stride           = stride;
    atlas->channels         = channels;
    atlas->pixels           = pixels;
    atlas->mode             = TRSPK_ATLAS_MODE_BINPACK;
    atlas->grid_tile_w      = 0;
    atlas->grid_tile_h      = 0;
    atlas->grid_cols        = 0;
    atlas->grid_rows        = 0;
    atlas->grid_next        = 0;
    atlas->free_rects       = free_rects;
    atlas->free_rects_count = 1;
    atlas->free_rects_cap   = TRSPK_ATLAS_BINPACK_INIT_CAP;
    trspk_atlas_set_initialized(atlas);
    return true;
}

/* ------------------------------------------------------------------ */
/* Bin-pack — internal helpers                                         */
/* ------------------------------------------------------------------ */

static bool
binpack_grow(struct TRSPK_Atlas* atlas)
{
    uint32_t                    new_cap    = atlas->free_rects_cap * 2u;
    struct TRSPK_AtlasFreeRect* new_rects  = (struct TRSPK_AtlasFreeRect*)realloc(
        atlas->free_rects, new_cap * sizeof(*new_rects));
    if( !new_rects )
    {
        return false;
    }
    atlas->free_rects     = new_rects;
    atlas->free_rects_cap = new_cap;
    return true;
}

static bool
binpack_push(struct TRSPK_Atlas* atlas, struct TRSPK_AtlasFreeRect free_rect)
{
    if( !free_rect.w || !free_rect.h )
    {
        return true; /* degenerate rect — discard silently */
    }

    if( atlas->free_rects_count >= atlas->free_rects_cap )
    {
        if( !binpack_grow(atlas) )
        {
            return false;
        }
    }
    atlas->free_rects[atlas->free_rects_count++] = free_rect;
    return true;
}

/* ------------------------------------------------------------------ */
/* Bin-pack — guillotine insertion (BSSF + longer-axis split)         */
/*                                                                     */
/* Best Short Side Fit: among all free rects large enough to hold the */
/* item, pick the one that minimises leftover on the shorter axis.    */
/*                                                                     */
/* Longer-axis split: after placement, divide the remaining L-shaped  */
/* area by cutting along the longer remaining dimension, producing    */
/* the largest possible second free rect.                             */
/*                                                                     */
/*  rem_w > rem_h (vertical cut first):                               */
/*  ┌──────────┬────────┐                                             */
/*  │  placed  │        │                                             */
/*  │          │ rect_a │                                             */
/*  ├──────────┤        │                                             */
/*  │  rect_b  │        │                                             */
/*  └──────────┴────────┘                                             */
/*                                                                     */
/*  rem_w <= rem_h (horizontal cut first):                            */
/*  ┌──────────────┬─────┐                                            */
/*  │    placed    │     │                                            */
/*  │              │rect_b                                            */
/*  ├──────────────┴─────┤                                            */
/*  │       rect_a       │                                            */
/*  └────────────────────┘                                            */
/* ------------------------------------------------------------------ */

bool
trspk_atlas_binpack_insert(
    struct TRSPK_Atlas*     atlas,
    const uint8_t*          src_pixels,
    uint32_t                src_stride,
    uint32_t                src_w,
    uint32_t                src_h,
    struct TRSPK_AtlasTile* tile_out)
{
    if( !atlas || atlas->mode != TRSPK_ATLAS_MODE_BINPACK )
    {
        return false;
    }
    if( !src_w || !src_h || !tile_out )
    {
        return false;
    }

    uint32_t best_idx   = UINT32_MAX;
    uint32_t best_score = UINT32_MAX;

    for( uint32_t idx = 0; idx < atlas->free_rects_count; idx++ )
    {
        const struct TRSPK_AtlasFreeRect* candidate = &atlas->free_rects[idx];
        if( candidate->w < src_w || candidate->h < src_h )
        {
            continue;
        }

        uint32_t leftover_w = candidate->w - src_w;
        uint32_t leftover_h = candidate->h - src_h;
        uint32_t short_side = leftover_w < leftover_h ? leftover_w : leftover_h;

        if( short_side < best_score )
        {
            best_score = short_side;
            best_idx   = idx;
        }
    }

    if( best_idx == UINT32_MAX )
    {
        return false; /* no space */
    }

    struct TRSPK_AtlasFreeRect chosen = atlas->free_rects[best_idx];

    /* Remove chosen rect by swapping with the last element */
    atlas->free_rects[best_idx] = atlas->free_rects[--atlas->free_rects_count];

    uint32_t rem_w = chosen.w - src_w;
    uint32_t rem_h = chosen.h - src_h;

    struct TRSPK_AtlasFreeRect rect_a;
    struct TRSPK_AtlasFreeRect rect_b;
    if( rem_w > rem_h )
    {
        rect_a.x = chosen.x + src_w; rect_a.y = chosen.y;         rect_a.w = rem_w;  rect_a.h = chosen.h;
        rect_b.x = chosen.x;         rect_b.y = chosen.y + src_h; rect_b.w = src_w;  rect_b.h = rem_h;
    }
    else
    {
        rect_a.x = chosen.x;         rect_a.y = chosen.y + src_h; rect_a.w = chosen.w; rect_a.h = rem_h;
        rect_b.x = chosen.x + src_w; rect_b.y = chosen.y;         rect_b.w = rem_w;    rect_b.h = src_h;
    }

    binpack_push(atlas, rect_a);
    binpack_push(atlas, rect_b);

    if( src_pixels )
    {
        blit_pixels(
            atlas->pixels, chosen.x, chosen.y, atlas->stride,
            src_pixels, src_stride, src_w, src_h, atlas->channels);
        trspk_atlas_set_dirty(atlas);
    }

    tile_fill(tile_out, chosen.x, chosen.y, src_w, src_h, atlas->width, atlas->height);
    return true;
}

void
trspk_atlas_decode_texture_rgba(
    const struct ToriDraw_Texture* tex,
    uint32_t tile_size,
    uint8_t* out_rgba)
{
    const uint32_t src_w = (uint32_t)tex->width;
    const uint32_t src_h = (uint32_t)tex->height;
    const uint32_t pixel_count = tile_size * tile_size * 4u;

    memset(out_rgba, 0, pixel_count);
    if( src_w == 0u || src_h == 0u )
        return;

    for( uint32_t dst_row = 0; dst_row < tile_size; dst_row++ )
    {
        const uint32_t src_row = (dst_row * src_h) / tile_size;
        for( uint32_t dst_col = 0; dst_col < tile_size; dst_col++ )
        {
            const uint32_t src_col = (dst_col * src_w) / tile_size;
            const int texel = tex->texels[src_row * src_w + src_col];
            const uint8_t rv = (uint8_t)((texel >> 16) & 0xFF);
            const uint8_t gv = (uint8_t)((texel >> 8) & 0xFF);
            const uint8_t bv = (uint8_t)(texel & 0xFF);
            const uint8_t av = (tex->opaque || texel != 0) ? 255u : 0u;
            const uint32_t idx = (dst_row * tile_size + dst_col) * 4u;
            out_rgba[idx + 0] = rv;
            out_rgba[idx + 1] = gv;
            out_rgba[idx + 2] = bv;
            out_rgba[idx + 3] = av;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Lifetime — free                                                     */
/* ------------------------------------------------------------------ */

void
trspk_atlas_free(struct TRSPK_Atlas* atlas)
{
    if( !atlas )
    {
        return;
    }
    free(atlas->pixels);
    free(atlas->free_rects);
    atlas->pixels           = NULL;
    atlas->free_rects       = NULL;
    atlas->free_rects_count = 0;
    atlas->free_rects_cap   = 0;
    atlas->width            = 0;
    atlas->height           = 0;
    atlas->stride           = 0;
    trspk_atlas_clear_initialized(atlas);
    trspk_atlas_clear_dirty(atlas);
}
