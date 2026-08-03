#include "dat2_worldmap_geography.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    WORLDMAP_GEOGRAPHY_MARKER_REGION = 0,
    WORLDMAP_GEOGRAPHY_MARKER_CHUNK = 1,
};

static bool
worldmap_decor_add(
    struct RSCache_WorldMapDecorList* list,
    int loc_id,
    int shape,
    int rotation)
{
    if( list->count == list->capacity )
    {
        int capacity = list->capacity ? list->capacity * 2 : 2;
        struct RSCache_WorldMapDecor* grown =
            (struct RSCache_WorldMapDecor*)realloc(list->items, (size_t)capacity * sizeof(*grown));
        if( !grown )
            return false;
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count].loc_id = loc_id;
    list->items[list->count].shape = shape;
    list->items[list->count].rotation = rotation;
    list->count++;
    return true;
}

static bool
worldmap_decor_copy(
    struct RSCache_WorldMapDecorList* dst,
    struct RSCache_WorldMapDecorList const* src)
{
    if( src->count <= 0 )
        return true;
    if( dst->capacity < src->count )
    {
        struct RSCache_WorldMapDecor* grown = (struct RSCache_WorldMapDecor*)realloc(
            dst->items, (size_t)src->count * sizeof(*grown));
        if( !grown )
            return false;
        dst->items = grown;
        dst->capacity = src->count;
    }
    memcpy(dst->items, src->items, (size_t)src->count * sizeof(*dst->items));
    dst->count = src->count;
    return true;
}

/*
 * A tile is one flags byte then, depending on bit 0, a short form (an underlay
 * and optionally one overlay) or a long form (underlay, an overlay per plane,
 * and a loc list per plane). flags == 0 is the common case: nothing here.
 *
 * Bits 5-7 are never set anywhere in cache.osrs239's geography table; a set bit
 * means the read has drifted off the true tile boundary, so it is refused here
 * rather than read as more of whatever garbage field it lands on — the same
 * "exact consumption is the evidence" reasoning the headerless region loop
 * already uses at the whole-file level (tools/cachepack/cp_decode.c geo_decode,
 * where this grammar was worked out and validated: 2,057/2,057 single-file
 * geography archives round-trip byte-exact under it).
 */
static bool
worldmap_read_tile(
    struct RSCache_WorldMapGeography* out,
    int tile_x,
    int tile_y,
    struct RSCache_Buffer* buffer)
{
    int index = RSCache_WorldMapTileIndex(tile_x, tile_y);
    int flags;

    if( RSCache_BufferRemaining(buffer) < 1 )
        return false;

    flags = RSCache_BufferG1(buffer);
    if( flags == 0 )
        return true;
    if( (flags & 0xE0) != 0 )
        return false;

    if( (flags & 1) != 0 )
    {
        if( (flags & 2) != 0 )
            out->overlay[0][index] = (uint16_t)RSCache_BufferG2(buffer);
        out->underlay[index] = (uint16_t)RSCache_BufferG2(buffer);
        return true;
    }

    /* Long form. Bits 3-4 carry "planes of locs to read" minus one — a count of
     * what the *file* holds, which can exceed the record's plane count; the
     * extra planes are read and dropped so the stream stays aligned. */
    int loc_planes = ((flags & 24) >> 3) + 1;
    bool has_overlays = (flags & 2) != 0;
    bool has_locs = (flags & 4) != 0;

    out->underlay[index] = (uint16_t)RSCache_BufferG2(buffer);

    if( has_overlays )
    {
        int overlay_count = RSCache_BufferG1(buffer);
        for( int plane = 0; plane < overlay_count; plane++ )
        {
            int overlay_id = RSCache_BufferG2(buffer);
            if( overlay_id == 0 )
                continue;
            int overlay_info = RSCache_BufferG1(buffer);
            if( plane < out->planes && plane < RSCACHE_WORLDMAP_MAX_PLANES )
            {
                out->overlay[plane][index] = (uint16_t)overlay_id;
                out->overlay_shape[plane][index] = (uint8_t)(overlay_info >> 2);
                out->overlay_rotation[plane][index] = (uint8_t)(overlay_info & 3);
            }
        }
    }

    if( has_locs )
    {
        for( int plane = 0; plane < loc_planes; plane++ )
        {
            int loc_count = RSCache_BufferG1(buffer);
            for( int i = 0; i < loc_count; i++ )
            {
                /* A plain u32, not a smart — the client generation that widened
                 * this field predates the one BigSmart handles. Reading it as a
                 * BigSmart is not merely wrong for this one value: for any real
                 * loc id (always well under 32768, so BigSmart always takes the
                 * 2-byte form) it under-reads by 2 bytes, and that deficit
                 * desyncs every byte read after the first loc-bearing tile in
                 * the file — the deficit compounds until a downstream field
                 * looks like garbage or the stream runs out mid-tile, which is
                 * indistinguishable from "the format is unknown" unless you are
                 * looking for a fixed-width field specifically. */
                int loc_id = (int)(RSCache_BufferG4(buffer) & 0x7fffffff);
                int info = RSCache_BufferG1(buffer);
                if( plane >= out->planes || plane >= RSCACHE_WORLDMAP_MAX_PLANES )
                    continue;
                if( !worldmap_decor_add(
                        &out->decor[plane][index], loc_id, info >> 2, info & 3) )
                    return false;
            }
        }
    }

    return true;
}

static void
worldmap_apply_planes(
    struct RSCache_WorldMapGeography* out,
    int planes)
{
    if( planes < 1 )
        planes = 1;
    if( planes > RSCACHE_WORLDMAP_MAX_PLANES )
        planes = RSCACHE_WORLDMAP_MAX_PLANES;
    if( planes > out->planes )
        out->planes = planes;
}

bool
RSCache_WorldMapGeographyBlitChunk(
    struct RSCache_WorldMapGeography* dst,
    struct RSCache_WorldMapGeography const* src,
    int src_cx,
    int src_cy,
    int dst_cx,
    int dst_cy)
{
    if( !dst || !src )
        return false;
    if( src_cx < 0 || src_cy < 0 || dst_cx < 0 || dst_cy < 0 )
        return false;
    if( src_cx >= RSCACHE_WORLDMAP_CHUNK_TILES || src_cy >= RSCACHE_WORLDMAP_CHUNK_TILES ||
        dst_cx >= RSCACHE_WORLDMAP_CHUNK_TILES || dst_cy >= RSCACHE_WORLDMAP_CHUNK_TILES )
        return false;

    worldmap_apply_planes(dst, src->planes);

    for( int tile_x = 0; tile_x < RSCACHE_WORLDMAP_CHUNK_TILES; tile_x++ )
    {
        for( int tile_y = 0; tile_y < RSCACHE_WORLDMAP_CHUNK_TILES; tile_y++ )
        {
            int src_index =
                RSCache_WorldMapTileIndex(src_cx * 8 + tile_x, src_cy * 8 + tile_y);
            int dst_index =
                RSCache_WorldMapTileIndex(dst_cx * 8 + tile_x, dst_cy * 8 + tile_y);

            dst->underlay[dst_index] = src->underlay[src_index];
            if( src->has_ground )
            {
                dst->ground[dst_index] = src->ground[src_index];
                dst->has_ground = true;
            }
            for( int plane = 0; plane < RSCACHE_WORLDMAP_MAX_PLANES; plane++ )
            {
                dst->overlay[plane][dst_index] = src->overlay[plane][src_index];
                dst->overlay_shape[plane][dst_index] = src->overlay_shape[plane][src_index];
                dst->overlay_rotation[plane][dst_index] =
                    src->overlay_rotation[plane][src_index];
                free(dst->decor[plane][dst_index].items);
                dst->decor[plane][dst_index].items = NULL;
                dst->decor[plane][dst_index].count = 0;
                dst->decor[plane][dst_index].capacity = 0;
                if( !worldmap_decor_copy(
                        &dst->decor[plane][dst_index], &src->decor[plane][src_index]) )
                    return false;
            }
        }
    }
    return true;
}

static bool
worldmap_try_read_region_scan(
    struct RSCache_WorldMapGeography* dest,
    const void* data,
    int data_size,
    int* out_tile_count)
{
    struct RSCache_Buffer buf;
    int tile_count = 0;

    RSCache_BufferInit(&buf, (uint8_t*)data, (uint32_t)data_size);
    for( int tile_x = 0; tile_x < RSCACHE_WORLDMAP_TILE_COUNT; tile_x++ )
    {
        for( int tile_y = 0; tile_y < RSCACHE_WORLDMAP_TILE_COUNT; tile_y++ )
        {
            if( RSCache_BufferRemaining(&buf) < 1 )
                goto done;
            if( !worldmap_read_tile(dest, tile_x, tile_y, &buf) )
                return false;
            tile_count++;
        }
    }
done:
    if( RSCache_BufferRemaining(&buf) != 0 )
        return false;
    if( tile_count <= 0 || (tile_count % RSCACHE_WORLDMAP_TILE_COUNT) != 0 )
        return false;
    if( out_tile_count )
        *out_tile_count = tile_count;
    return true;
}

static bool
worldmap_try_read_single_chunk(
    struct RSCache_WorldMapGeography* dest,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buf;

    RSCache_BufferInit(&buf, (uint8_t*)data, (uint32_t)data_size);
    for( int tile_x = 0; tile_x < RSCACHE_WORLDMAP_CHUNK_TILES; tile_x++ )
    {
        for( int tile_y = 0; tile_y < RSCACHE_WORLDMAP_CHUNK_TILES; tile_y++ )
        {
            if( !worldmap_read_tile(dest, tile_x, tile_y, &buf) )
                return false;
        }
    }
    return RSCache_BufferRemaining(&buf) == 0;
}

int
RSCache_WorldMapGeographyDecodeHeaderlessFile(
    struct RSCache_WorldMapGeography* out,
    const void* data,
    int data_size,
    int planes,
    bool* out_is_chunk)
{
    struct RSCache_WorldMapGeography* temp;
    int tile_count = 0;

    if( out_is_chunk )
        *out_is_chunk = false;
    if( !out || !data || data_size <= 0 )
        return -1;

    temp = calloc(1, sizeof(*temp));
    if( !temp )
        return -1;
    worldmap_apply_planes(temp, planes);

    /* 1) Full 64x64 region (exact 4096). */
    if( worldmap_try_read_region_scan(temp, data, data_size, &tile_count) &&
        tile_count == RSCACHE_WORLDMAP_TILE_AREA )
    {
        RSCache_WorldMapGeographyFreeInplace(out);
        *out = *temp;
        free(temp);
        return tile_count;
    }
    RSCache_WorldMapGeographyFreeInplace(temp);
    memset(temp, 0, sizeof(*temp));
    worldmap_apply_planes(temp, planes);

    /* 2) Lone 8x8 chunk. Must beat a 1-column "partial region" reading of the
     * same 64 tiles — those layouts place the bytes differently. */
    if( worldmap_try_read_single_chunk(temp, data, data_size) )
    {
        RSCache_WorldMapGeographyFreeInplace(out);
        *out = *temp;
        free(temp);
        if( out_is_chunk )
            *out_is_chunk = true;
        return RSCACHE_WORLDMAP_CHUNK_TILES * RSCACHE_WORLDMAP_CHUNK_TILES;
    }
    RSCache_WorldMapGeographyFreeInplace(temp);
    memset(temp, 0, sizeof(*temp));
    worldmap_apply_planes(temp, planes);

    /* 3) Truncated region in the same x-major order (128..4032 tiles). */
    if( worldmap_try_read_region_scan(temp, data, data_size, &tile_count) &&
        tile_count != RSCACHE_WORLDMAP_TILE_AREA &&
        tile_count != RSCACHE_WORLDMAP_CHUNK_TILES * RSCACHE_WORLDMAP_CHUNK_TILES )
    {
        RSCache_WorldMapGeographyFreeInplace(out);
        *out = *temp;
        free(temp);
        return tile_count;
    }

    RSCache_WorldMapGeographyFreeInplace(temp);
    free(temp);
    return -1;
}

static bool
worldmap_headerless_chunk_into(
    struct RSCache_WorldMapGeography* out,
    const void* data,
    int data_size,
    int planes,
    int src_chunk_x,
    int src_chunk_y,
    int dst_chunk_x,
    int dst_chunk_y)
{
    struct RSCache_WorldMapGeography full;
    bool is_chunk = false;
    int tile_count;
    int src_cx = src_chunk_x < 0 ? 0 : src_chunk_x;
    int src_cy = src_chunk_y < 0 ? 0 : src_chunk_y;
    int dst_cx = dst_chunk_x < 0 ? 0 : dst_chunk_x;
    int dst_cy = dst_chunk_y < 0 ? 0 : dst_chunk_y;

    memset(&full, 0, sizeof(full));
    tile_count =
        RSCache_WorldMapGeographyDecodeHeaderlessFile(&full, data, data_size, planes, &is_chunk);
    if( tile_count < 0 )
        return false;

    if( is_chunk )
    {
        /* Single-chunk file: the only 8x8 is at (0,0) in `full`. */
        bool ok = RSCache_WorldMapGeographyBlitChunk(out, &full, 0, 0, dst_cx, dst_cy);
        RSCache_WorldMapGeographyFreeInplace(&full);
        return ok;
    }

    /* Region-scan file (full or truncated). Chunk (cx,cy) needs columns
     * cx*8 .. cx*8+7, i.e. tile_count/64 columns present. */
    {
        int columns = tile_count / RSCACHE_WORLDMAP_TILE_COUNT;
        if( src_cx < 0 || src_cy < 0 || src_cx >= RSCACHE_WORLDMAP_CHUNK_TILES ||
            src_cy >= RSCACHE_WORLDMAP_CHUNK_TILES )
        {
            RSCache_WorldMapGeographyFreeInplace(&full);
            return false;
        }
        if( src_cx * RSCACHE_WORLDMAP_CHUNK_TILES + RSCACHE_WORLDMAP_CHUNK_TILES > columns )
        {
            RSCache_WorldMapGeographyFreeInplace(&full);
            return false;
        }
    }

    {
        bool ok =
            RSCache_WorldMapGeographyBlitChunk(out, &full, src_cx, src_cy, dst_cx, dst_cy);
        RSCache_WorldMapGeographyFreeInplace(&full);
        return ok;
    }
}

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
    int dst_chunk_y)
{
    struct RSCache_Buffer buffer;
    int marker;

    if( !out || !data || data_size <= 0 )
        return false;

    worldmap_apply_planes(out, planes);
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    if( headerless )
    {
        /* OSRS >= 238: no marker, no region/chunk coords in the file — table 18
         * is addressed by (region_x << 8) | region_y instead (see
         * RSCache_WorldMapFlags), so the compositemap record's own kind and
         * destination coords are what place these tiles, not anything the file
         * repeats about itself. */
        if( kind == WORLDMAP_GEOGRAPHY_MARKER_CHUNK )
        {
            /* Compositemap kind=1 often points at a *full* 4096-tile region file
             * (Ardent Ocean Underground, Ancient Cavern, …). Reading only the
             * first 64 tiles and painting them at every dst_chunk produced a
             * repeating 8x8 grid. Decode the stream properly and extract
             * src_chunk → dst_chunk. */
            return worldmap_headerless_chunk_into(
                out, data, data_size, planes, src_chunk_x, src_chunk_y, dst_chunk_x,
                dst_chunk_y);
        }

        /* Whole region, straight into its tiles from byte 0. */
        for( int tile_x = 0; tile_x < RSCACHE_WORLDMAP_TILE_COUNT; tile_x++ )
        {
            for( int tile_y = 0; tile_y < RSCACHE_WORLDMAP_TILE_COUNT; tile_y++ )
            {
                if( !worldmap_read_tile(out, tile_x, tile_y, &buffer) )
                    return false;
            }
        }
        /* Exact consumption. The older files are framed by a marker and their
         * own region coords, so a misread shows up immediately; a headerless
         * file has no such check, and a tile layout this decoder reads wrongly
         * would otherwise surface as plausible-looking floor ids drawn in the
         * wrong places. Landing exactly on the end is the evidence that every
         * tile was read the way the file wrote it. */
        if( RSCache_BufferRemaining(&buffer) != 0 )
        {
            printf(
                "RSCache_WorldMapGeographyDecodeInplace: headerless file left %u bytes\n",
                RSCache_BufferRemaining(&buffer));
            return false;
        }
        return true;
    }

    marker = RSCache_BufferG1(&buffer);
    if( marker != kind )
    {
        printf(
            "RSCache_WorldMapGeographyDecodeInplace: marker %d, expected %d\n", marker, kind);
        return false;
    }

    int region_x = RSCache_BufferG1(&buffer);
    int region_y = RSCache_BufferG1(&buffer);
    if( expect_region_x >= 0 &&
        (region_x != (expect_region_x & 0xFF) || region_y != (expect_region_y & 0xFF)) )
    {
        printf(
            "RSCache_WorldMapGeographyDecodeInplace: region %d,%d, expected %d,%d\n",
            region_x,
            region_y,
            expect_region_x,
            expect_region_y);
        return false;
    }

    if( kind == WORLDMAP_GEOGRAPHY_MARKER_REGION )
    {
        for( int tile_x = 0; tile_x < RSCACHE_WORLDMAP_TILE_COUNT; tile_x++ )
        {
            for( int tile_y = 0; tile_y < RSCACHE_WORLDMAP_TILE_COUNT; tile_y++ )
            {
                if( !worldmap_read_tile(out, tile_x, tile_y, &buffer) )
                    return false;
            }
        }
        return true;
    }

    if( kind != WORLDMAP_GEOGRAPHY_MARKER_CHUNK )
        return false;

    int chunk_x = RSCache_BufferG1(&buffer);
    int chunk_y = RSCache_BufferG1(&buffer);
    /* Pre-238: the file names its own chunk; dst_chunk_* is the expected value
     * when the caller wants a check (>= 0). Placement follows the file. */
    if( dst_chunk_x >= 0 && (chunk_x != dst_chunk_x || chunk_y != dst_chunk_y) )
    {
        printf(
            "RSCache_WorldMapGeographyDecodeInplace: chunk %d,%d, expected %d,%d\n",
            chunk_x,
            chunk_y,
            dst_chunk_x,
            dst_chunk_y);
        return false;
    }

    for( int tile_x = 0; tile_x < 8; tile_x++ )
    {
        for( int tile_y = 0; tile_y < 8; tile_y++ )
        {
            if( !worldmap_read_tile(
                    out, tile_x + (chunk_x * 8), tile_y + (chunk_y * 8), &buffer) )
                return false;
        }
    }
    return true;
}

void
RSCache_WorldMapGeographyFreeInplace(struct RSCache_WorldMapGeography* geography)
{
    if( !geography )
        return;
    for( int plane = 0; plane < RSCACHE_WORLDMAP_MAX_PLANES; plane++ )
    {
        for( int i = 0; i < RSCACHE_WORLDMAP_TILE_AREA; i++ )
            free(geography->decor[plane][i].items);
    }
    memset(geography, 0, sizeof(*geography));
}
