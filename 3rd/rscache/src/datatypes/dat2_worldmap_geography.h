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
 */

#define RSCACHE_WORLDMAP_TILE_COUNT 64
#define RSCACHE_WORLDMAP_TILE_AREA (RSCACHE_WORLDMAP_TILE_COUNT * RSCACHE_WORLDMAP_TILE_COUNT)
#define RSCACHE_WORLDMAP_MAX_PLANES 4

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
 * the destination against, and `expect_region_x/y` are unused. `kind` and
 * `expect_chunk_x/y` (for kind 1) are still required, and still real: the
 * compositemap record that led here always knows its own kind and destination,
 * addressing scheme aside, and headerless placement has no other source for
 * them. A headerless kind-1 file may bundle more than one chunk's tiles back to
 * back with no way to place the rest; only the record's own chunk is read.
 *
 * `headerless` false is the OSRS <= 237 layout: marker + region/chunk coords
 * repeated in the file, checked against `expect_region_x/y` (and
 * `expect_chunk_x/y` for kind 1) when >= 0 — a mismatch means the compositemap
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
    int expect_chunk_x,
    int expect_chunk_y);

void
RSCache_WorldMapGeographyFreeInplace(struct RSCache_WorldMapGeography* geography);

#endif
