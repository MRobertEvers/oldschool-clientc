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
 * Headerless kind-1 is a stream of consecutive 64-tile zone blocks, one per
 * compositemap record for that destination region, in record order. The
 * archive group is (dst_region_x << 8) | dst_region_y. Use
 * RSCache_WorldMapGeographyReader + ReadChunk to walk the blocks; do not treat
 * the file as an x-major 64x64 grid to slice src_chunk out of.
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
 * Cursor over a headerless table-18 payload. Consecutive kind-1 records for
 * one destination region share one file and each ReadChunk consumes the next
 * 64-tile zone block. Kind-0 uses ReadRegion once for the whole 64x64.
 *
 * The buffer does not own `data`; the caller keeps the bytes alive for the
 * reader's lifetime (the load task copies the archive file for this).
 */
struct RSCache_WorldMapGeographyReader
{
    struct RSCache_Buffer buffer;
};

void
RSCache_WorldMapGeographyReaderInit(
    struct RSCache_WorldMapGeographyReader* reader,
    const void* data,
    int data_size);

uint32_t
RSCache_WorldMapGeographyReaderRemaining(
    struct RSCache_WorldMapGeographyReader const* reader);

/**
 * Read a whole 64x64 region (x-major) from the cursor into `out`.
 */
bool
RSCache_WorldMapGeographyReadRegion(
    struct RSCache_WorldMapGeography* out,
    struct RSCache_WorldMapGeographyReader* reader,
    int planes);

/**
 * Read the next 8x8 zone from the cursor and place it at
 * (dst_cx*8 + lx, dst_cy*8 + ly) in `out`.
 */
bool
RSCache_WorldMapGeographyReadChunk(
    struct RSCache_WorldMapGeography* out,
    struct RSCache_WorldMapGeographyReader* reader,
    int planes,
    int dst_cx,
    int dst_cy);

/**
 * Decode a table-18 file into `out`, which the caller zeroes once and may pass
 * repeatedly to assemble a region from chunk records.
 *
 * `headerless` is the cache-revision property (OSRS >= 238): the file carries
 * **no marker and no region/chunk coords at all** — table 18 is addressed by
 * (dst_region_x << 8) | dst_region_y instead of an explicit group/file pair
 * (see RSCache_WorldMapFlags), so there is nothing in the stream to check
 * `kind` or the destination against, and `expect_region_x/y` are unused.
 * `kind` is still required. For kind 1, `dst_chunk_x/y` are where the next
 * 8x8 lands in `out` (one zone per call; prefer the Reader API when several
 * records share one file).
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
    int dst_chunk_x,
    int dst_chunk_y);

void
RSCache_WorldMapGeographyFreeInplace(struct RSCache_WorldMapGeography* geography);

#endif
