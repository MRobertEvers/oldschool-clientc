#ifndef RSCACHE_DATATYPES_DAT2_WORLDMAP_GEOGRAPHY_H
#define RSCACHE_DATATYPES_DAT2_WORLDMAP_GEOGRAPHY_H

#include "../rsbuffer.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * World map geography (dat2 table 18) — the tiles behind one compositemap
 * record. This is *not* a map square from the maps table: it is a flattened,
 * render-ready copy holding only what the world map draws (floor ids, overlay
 * shapes, and the locs that become wall lines or map scene icons), with no
 * heights, no collision and no full loc list.
 *
 * Two shapes, kind 0 (a whole 64x64 region, tiles in x-major order) and kind 1
 * (one 8x8 chunk of a region, addressed at (chunk_x*8, chunk_y*8)). Both write
 * into the same 64x64 grid, so a region assembled from chunk records decodes
 * several files into one struct.
 *
 * OSRS <= 237 tags the shape with a leading marker byte the file repeats along
 * with its own region/chunk coords. OSRS >= 238 files carry neither — the
 * shape and destination come only from the compositemap record that pointed
 * here (RSCache_WorldMapGeographyDecodeInplace's `headerless` parameter), and
 * the loc id inside a tile widened from the older files' BigSmart to a plain
 * u32 (see EXCEPTIONS.md B21 and the decoder's own comments for how both were
 * measured, not guessed).
 *
 * Headerless kind-1 is not "the file is 64 tiles". Compositemap chunk records
 * often point at a full 4096-tile region file (or a truncated multiple of 64 in
 * the same x-major order); `src_chunk_x/y` selects which 8x8 to take from that
 * stream and `dst_chunk_x/y` says where it lands in `out`. A true single-chunk
 * file (exactly 64 tiles as an 8x8) ignores `src_chunk` and places at dest.
 */

#define RSCACHE_WORLDMAP_TILE_COUNT 64
#define RSCACHE_WORLDMAP_TILE_AREA (RSCACHE_WORLDMAP_TILE_COUNT * RSCACHE_WORLDMAP_TILE_COUNT)
#define RSCACHE_WORLDMAP_MAX_PLANES 4
#define RSCACHE_WORLDMAP_CHUNK_TILES 8

/** One loc on a tile, reduced to what the map draws it as. */
struct RSCache_WorldMapDecor
{
    int loc_id;
    int shape;    /* loc shape: 0-3 and 9 are walls, 10/11/22 take a map scene */
    int rotation; /* 0-3 */
};

/** Grown per tile; most tiles have none, a few have several. */
struct RSCache_WorldMapDecorList
{
    struct RSCache_WorldMapDecor* items;
    int count;
    int capacity;
};

struct RSCache_WorldMapGeography
{
    int planes; /* 1..4, from the record */
    /* Ground colour per tile (0x00RRGGBB), from the matching group of the world
     * map ground table (dat2 table 20) — a 64x64 image of the already-blended
     * underlay colours. `has_ground` is false when the cache ships none, and the
     * renderer then falls back to the underlay flo's flat colour. */
    bool has_ground;
    uint32_t ground[RSCACHE_WORLDMAP_TILE_AREA];

    /* Floor ids are stored +1 by the cache: 0 means "no floor here". */
    uint16_t underlay[RSCACHE_WORLDMAP_TILE_AREA];
    uint16_t overlay[RSCACHE_WORLDMAP_MAX_PLANES][RSCACHE_WORLDMAP_TILE_AREA];
    uint8_t overlay_shape[RSCACHE_WORLDMAP_MAX_PLANES][RSCACHE_WORLDMAP_TILE_AREA];
    uint8_t overlay_rotation[RSCACHE_WORLDMAP_MAX_PLANES][RSCACHE_WORLDMAP_TILE_AREA];
    struct RSCache_WorldMapDecorList decor[RSCACHE_WORLDMAP_MAX_PLANES]
                                          [RSCACHE_WORLDMAP_TILE_AREA];
};

/** (x << 6) | y, the order the tiles are written in. */
static inline int
RSCache_WorldMapTileIndex(int tile_x, int tile_y)
{
    return ((tile_x & 63) << 6) | (tile_y & 63);
}

/**
 * Decode a table-18 file into `out`, which the caller zeroes once and may pass
 * repeatedly to assemble a region from chunk records.
 *
 * `headerless` is the cache-revision property (OSRS >= 238): the file carries
 * **no marker and no region/chunk coords at all** — table 18 is addressed by
 * (region_x << 8) | region_y instead of an explicit group/file pair (see
 * RSCache_WorldMapFlags) — so there is nothing in the stream to check `kind` or
 * the destination against, and `expect_region_x/y` are unused. `kind` is still
 * required. For kind 1, `src_chunk_x/y` select the 8x8 inside a full/partial
 * region file; `dst_chunk_x/y` are where that 8x8 lands in `out`. A 64-tile
 * single-chunk file places at dest and ignores src.
 *
 * `headerless` false is the OSRS <= 237 layout: marker + region/chunk coords
 * repeated in the file, checked against `expect_region_x/y` (and
 * `dst_chunk_x/y` for kind 1) when >= 0 — a mismatch means the compositemap
 * pointed at the wrong file, so the decode is refused rather than written into
 * the wrong tiles.
 *
 * Returns false on a marker/coord mismatch or a truncated file. Whatever was
 * written before the failure stays — partial tiles draw as background, which is
 * what a missing file does anyway.
 */
bool
RSCache_WorldMapGeographyDecodeInplace(
    struct RSCache_WorldMapGeography* out,
    const void* data,
    int data_size,
    bool headerless,
    int kind,
    int planes,
    int expect_region_x,
    int expect_region_y,
    int src_chunk_x,
    int src_chunk_y,
    int dst_chunk_x,
    int dst_chunk_y);

/**
 * Copy one 8x8 chunk (including decor) from `src` at (src_cx, src_cy) into
 * `dst` at (dst_cx, dst_cy). Used when a headerless kind-1 record shares a
 * full-region file with siblings — decode once, blit many.
 */
bool
RSCache_WorldMapGeographyBlitChunk(
    struct RSCache_WorldMapGeography* dst,
    struct RSCache_WorldMapGeography const* src,
    int src_cx,
    int src_cy,
    int dst_cx,
    int dst_cy);

/**
 * Decode a headerless table-18 payload into `out` as an x-major region stream
 * (up to 4096 tiles). Returns the tile count on success (multiple of 64, or
 * exactly 64 when the file is a lone 8x8 chunk — then tiles land at 0..7,0..7
 * and the count is 64 with shape "chunk"), or -1 on failure.
 *
 * `out_is_chunk` is set when the file was the 8x8 single-chunk layout rather
 * than region-scan order.
 */
int
RSCache_WorldMapGeographyDecodeHeaderlessFile(
    struct RSCache_WorldMapGeography* out,
    const void* data,
    int data_size,
    int planes,
    bool* out_is_chunk);

void
RSCache_WorldMapGeographyFreeInplace(struct RSCache_WorldMapGeography* geography);

#endif
