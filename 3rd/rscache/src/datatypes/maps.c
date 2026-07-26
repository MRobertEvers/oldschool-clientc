#include "maps.h"

#include "../archive.h"
#include "../dat2disk.h"
#include "../filelist.h"
#include "../reference_table.h"
#include "../rsbuffer.h"
#include "noise.h"
#include "mapsquares.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
generate_height(
    int x,
    int y)
{
    int n = RSCache_NoisePerlinNoise(x + 45365, y + 91923, 4) - 128 +
            ((RSCache_NoisePerlinNoise(x + 10294, y + 37821, 2) - 128) >> 1) +
            ((RSCache_NoisePerlinNoise(x, y, 1) - 128) >> 2);
    n = (int)(0.3 * n) + 35;
    if( n < 10 )
    {
        n = 10;
    }
    else if( n > 60 )
    {
        n = 60;
    }
    return n;
}

static void
fixup_terrain(
    struct RSCache_MapTerrain* map_terrain,
    int map_x,
    int map_z)
{
    int base_x = map_x * 64;
    int base_z = map_z * 64;
    for( int level = 0; level < RSCACHE_MAP_TERRAIN_LEVELS; level++ )
    {
        for( int z = 0; z < RSCACHE_MAP_TERRAIN_Z; z++ )
        {
            for( int x = 0; x < RSCACHE_MAP_TERRAIN_X; x++ )
            {
                struct RSCache_MapFloor* map =
                    &map_terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(x, z, level)];
                if( map->height == 0 )
                {
                    if( level == 0 )
                    {
                        int world_x = base_x + x + 932731;
                        int world_z = base_z + z + 556238;
                        int height = generate_height(world_x, world_z);
                        map->height = -height * RSCACHE_MAP_UNITS_TILE_HEIGHT_BASIS;
                    }
                    else
                    {
                        int lower = map_terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(x, z, level - 1)].height;
                        map->height = lower - RSCACHE_MAP_UNITS_LEVEL_HEIGHT;
                    }
                }
                else
                {
                    if( map->height == 1 )
                        map->height = 0;

                    if( level == 0 )
                    {
                        map->height = -map->height * RSCACHE_MAP_UNITS_TILE_HEIGHT_BASIS;
                    }
                    else
                    {
                        int lower = map_terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(x, z, level - 1)].height;
                        map->height = lower - map->height * RSCACHE_MAP_UNITS_TILE_HEIGHT_BASIS;
                    }
                }
            }
        }
    }
    map_terrain->is_fixedup = true;
}

static int
dat2_map_archive_id(
    struct RSCache_Dat2Disk* cache,
    const char* name_fmt,
    int map_x,
    int map_z)
{
    char name[13];

    snprintf(name, sizeof(name), name_fmt, map_x, map_z);

    int name_hash = RSCache_ArchiveNameHashDat2(name);

    int maps_table = RSCache_Dat2DiskTableId(cache, RSCACHE_DAT2_TABLE_MAPS);
    if( maps_table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return -1;

    struct RSCache_ReferenceTable* table = cache->tables[maps_table];
    if( !table )
        return -1;

    for( int i = 0; i < table->archive_count; i++ )
    {
        struct RSCache_ReferenceTableArchive* archive_reference = &table->archives[i];

        if( archive_reference->identifier == name_hash )
            return archive_reference->index;
    }

    return -1;
}

/** True when the maps table lists `archive_id` as a present archive. */
static bool
dat2_maps_has_archive(
    struct RSCache_Dat2Disk* cache,
    int archive_id)
{
    int maps_table = RSCache_Dat2DiskTableId(cache, RSCACHE_DAT2_TABLE_MAPS);
    if( maps_table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return false;
    struct RSCache_ReferenceTable* table = cache->tables[maps_table];
    if( !table || archive_id < 0 || archive_id >= table->archive_count )
        return false;
    return table->archives[archive_id].index >= 0;
}

/**
 * Resolve the maps-table archive for a square.
 *
 * Classic (named) layout: separate `mX_Z` / `lX_Z` archives looked up by name
 * hash. Modern OldSchool caches (seen at revision 239) drop archive identifiers
 * and store one multi-file archive per region id (`(x<<8)|z`), with terrain in
 * file 0 and locs in file 1. Prefer the named form when present so a cache that
 * still has names — and may also list the numeric region id as a stub — keeps
 * working.
 *
 * `*out_file_index` is -1 for "whole archive payload", or the child file index
 * inside a region group.
 */
static int
dat2_map_resolve(
    struct RSCache_Dat2Disk* cache,
    int map_x,
    int map_z,
    bool want_locs,
    int* out_file_index)
{
    assert(out_file_index);
    *out_file_index = -1;

    int named = dat2_map_archive_id(cache, want_locs ? "l%d_%d" : "m%d_%d", map_x, map_z);
    if( named >= 0 )
        return named;

    int region_id = RSCache_MapSquareId(map_x, map_z);
    if( dat2_maps_has_archive(cache, region_id) )
    {
        *out_file_index = want_locs ? 1 : 0;
        return region_id;
    }
    return -1;
}

/** Borrow terrain/locs bytes from a loaded maps archive (named or region-grouped). */
static bool
dat2_map_payload(
    struct RSCache_Dat2Disk* cache,
    struct RSCache_Dat2DiskArchive* archive,
    bool want_locs,
    char** out_data,
    int* out_size,
    struct RSCache_FileList** out_files)
{
    assert(archive && out_data && out_size && out_files);
    *out_files = NULL;
    *out_data = archive->data;
    *out_size = archive->data_size;

    if( archive->file_count < 0 && cache )
        RSCache_Dat2DiskArchiveInitMetadata(cache, archive);

    if( archive->file_count < 2 )
        return true;

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files || files->file_count < 2 )
    {
        RSCache_FileListFree(files);
        return false;
    }
    int idx = want_locs ? 1 : 0;
    *out_data = files->files[idx];
    *out_size = files->file_sizes[idx];
    *out_files = files;
    return true;
}

struct RSCache_MapTerrain*
RSCache_MapTerrainNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int map_x,
    int map_z)
{
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_MapTerrain* map_terrain = NULL;
    struct RSCache_FileList* files = NULL;
    char* data;
    int size;
    int file_index;
    int archive_id = dat2_map_resolve(cache, map_x, map_z, false, &file_index);

    if( archive_id == -1 )
    {
        printf("Failed to load map terrain %d, %d (archive_id: %d)\n", map_x, map_z, archive_id);
        return NULL;
    }

    archive = RSCache_Dat2DiskArchiveNewLoad(
        cache, RSCache_Dat2DiskTableId(cache, RSCACHE_DAT2_TABLE_MAPS), archive_id);
    if( !archive )
    {
        printf("Failed to load map terrain %d, %d cache_load\n", map_x, map_z);
        return NULL;
    }
    RSCache_Dat2DiskArchiveInitMetadata(cache, archive);

    if( !dat2_map_payload(cache, archive, false, &data, &size, &files) )
    {
        printf("Failed to load map terrain %d, %d (grouped payload)\n", map_x, map_z);
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }

    map_terrain = RSCache_MapTerrainNewDecode(data, size, map_x, map_z);
    RSCache_FileListFree(files);
    if( !map_terrain )
    {
        printf("Failed to load map terrain %d, %d terrain_new_from_decode\n", map_x, map_z);
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }

    RSCache_Dat2DiskArchiveFree(archive);

    return map_terrain;
}

struct RSCache_Dat2DiskArchive*
RSCache_MapTerrainArchiveNewLoad(
    struct RSCache_Dat2Disk* cache,
    int map_x,
    int map_z)
{
    struct RSCache_Dat2DiskArchive* archive = NULL;
    int file_index;
    int archive_id = dat2_map_resolve(cache, map_x, map_z, false, &file_index);

    if( archive_id == -1 )
    {
        printf("Failed to load map terrain %d, %d (archive_id: %d)\n", map_x, map_z, archive_id);
        return NULL;
    }

    archive = RSCache_Dat2DiskArchiveNewLoad(
        cache, RSCache_Dat2DiskTableId(cache, RSCACHE_DAT2_TABLE_MAPS), archive_id);
    if( !archive )
    {
        printf("Failed to load map terrain %d, %d cache_load\n", map_x, map_z);
        return NULL;
    }
    RSCache_Dat2DiskArchiveInitMetadata(cache, archive);

    return archive;
}

struct RSCache_MapTerrain*
RSCache_MapTerrainNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int map_x,
    int map_z)
{
    /* Legacy entry point: assumes the modern OSRS width. Callers that know their
     * cache should use the Profile variant — a pre-209 square decodes as void
     * here. See RSCache_MapTerrainFlags. */
    return RSCache_MapTerrainNewFromArchiveProfile(archive, map_x, map_z, NULL);
}

struct RSCache_MapTerrain*
RSCache_MapTerrainNewFromArchiveProfile(
    struct RSCache_Dat2DiskArchive* archive,
    int map_x,
    int map_z,
    const struct RSCache* cache)
{
    char* data;
    int size;
    struct RSCache_FileList* files = NULL;
    if( !dat2_map_payload(NULL, archive, false, &data, &size, &files) )
    {
        printf("Failed to load map terrain %d, %d (grouped payload)\n", map_x, map_z);
        return NULL;
    }

    struct RSCache_MapTerrain* map_terrain = RSCache_MapTerrainNewFromDecodeFlags(
        data,
        size,
        map_x,
        map_z,
        RSCache_MapTerrainFlags(cache));
    RSCache_FileListFree(files);
    if( !map_terrain )
    {
        printf("Failed to load map terrain %d, %d terrain_new_from_decode\n", map_x, map_z);
        return NULL;
    }

    return map_terrain;
}

static int
read_decode(
    struct RSCache_Buffer* buffer,
    bool u16)
{
    if( u16 )
        return g2(buffer);
    else
        return g1(buffer);
}

int
RSCache_MapTerrainFlags(const struct RSCache* cache)
{
    assert(cache);
    /*
     * The tile attribute opcode and the overlay id widened from u8 to u16 at
     * **OldSchool revision 209** — rs-map-viewer's SceneBuilder gates on exactly
     * `game === "oldschool" && revision >= 209`.
     *
     * It is a lineage difference, not a container one. This used to read
     * `IsDat1 ? U8 : U16`, which handed the wide layout to *every* dat2 cache. A
     * pre-209 dat2 square then desyncs on its very first tile — the u16 attribute
     * read swallows the following tile's opcode — and because the loop only ever
     * breaks on 0 or 1 it resynchronises into garbage rather than failing. The
     * square decodes as almost entirely void: 120 of 15,376 tiles carried an
     * underlay or overlay for 643, against 4,481 for a working OSRS square, so
     * both the 3D scene and the terrain-baked minimap came out blank.
     *
     * Non-OSRS (dat1 classic and RS2 dat2) must take the narrow branch even when
     * their revision number clears 209: they are not on the OldSchool line.
     * That is the D16/D20 trap; RevisionAtLeastOsrs is only reached for OSRS.
     */
    if( !RSCache_IsOsrs(cache) )
        return RSCACHE_MAP_TERRAIN_DECODE_U8;

    /* Default true for an unidentified OSRS cache: that preserves the previous
     * behaviour for the OSRS corpus, all of which is well past 209. */
    return RSCache_RevisionAtLeastOsrs(
               cache, RSCACHE_TYPE_MAP_TERRAIN, 209, RSCACHE_GROUP_REVISION_UNKNOWN, true)
               ? RSCACHE_MAP_TERRAIN_DECODE_U16
               : RSCACHE_MAP_TERRAIN_DECODE_U8;
}

bool
RSCache_MapLocsEncrypted(const struct RSCache* cache)
{
    assert(cache);
    /*
     * Two independent lineage gates — do not share one constant with the
     * component model-id widen at OldSchool 237.
     *
     * OldSchool: encrypted only below 237. default_when_unknown=false keeps an
     * unidentified OSRS cache on the keyed path (every pre-237 corpus cache).
     *
     * RS2: encrypted from 414, which is also around when that lineage's dat2
     * maps started using XTEA (dat2 itself lands post-~377). default true for
     * unknown RS2 revision keeps 643-era unidentified profiles keyed.
     *
     * Dat1 / unset: no dat2 map table — not encrypted.
     */
    if( RSCache_IsOsrs(cache) )
    {
        return !RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_MAP_LOCS, 237, RSCACHE_GROUP_REVISION_UNKNOWN, false);
    }
    if( RSCache_IsRs2Dat2(cache) )
    {
        return RSCache_RevisionAtLeastRs2(
            cache, RSCACHE_TYPE_MAP_LOCS, 414, RSCACHE_GROUP_REVISION_UNKNOWN, true);
    }
    return false;
}

struct RSCache_MapTerrain*
RSCache_MapTerrainNewFromDecodeFlags(
    char* data,
    int data_size,
    int map_x,
    int map_z,
    int flags)
{
    struct RSCache_MapTerrain* map_terrain = malloc(sizeof(struct RSCache_MapTerrain));
    if( !map_terrain )
        return NULL;
    memset(map_terrain, 0, sizeof(struct RSCache_MapTerrain));

    map_terrain->map_x = map_x;
    map_terrain->map_z = map_z;

    struct RSCache_Buffer buffer = { .data = (uint8_t*)(data), .position = 0, .size = (uint32_t)(data_size) };

    for( int level = 0; level < RSCACHE_MAP_TERRAIN_LEVELS; level++ )
    {
        for( int x = 0; x < RSCACHE_MAP_TERRAIN_X; x++ )
        {
            for( int z = 0; z < RSCACHE_MAP_TERRAIN_Z; z++ )
            {
                struct RSCache_MapFloor* tile =
                    &map_terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(x, z, level)];

                while( true )
                {
                    int attribute = read_decode(&buffer, !(flags & RSCACHE_MAP_TERRAIN_DECODE_U8));
                    if( attribute == 0 )
                    {
                        break;
                    }
                    else if( attribute == 1 )
                    {
                        int height = g1(&buffer);
                        assert(tile->height == 0);
                        tile->height = height;
                        /* Provenance for the encoder: the fixup below will
                         * overwrite `height` if it is 0, so keep the raw byte. */
                        tile->height_authored = 1;
                        tile->authored_height = (uint8_t)height;
                        break;
                    }
                    else if( attribute <= 49 )
                    {
                        tile->overlay_id = read_decode(&buffer, !(flags & RSCACHE_MAP_TERRAIN_DECODE_U8));
                        tile->attr_opcode = attribute;
                        tile->shape = (attribute - 2) / 4;
                        tile->rotation = (attribute - 2) & 3;
                    }
                    else if( attribute <= 81 )
                    {
                        tile->settings = attribute - 49;
                    }
                    else
                    {
                        tile->underlay_id = attribute - 81;
                    }
                }
            }
        }
    }

    /* fixup_terrain sets is_fixedup itself. */
    if( !(flags & RSCACHE_MAP_TERRAIN_DECODE_NO_FIXUP) )
        fixup_terrain(map_terrain, map_x, map_z);

    return map_terrain;
}

static void
write_decode(
    struct RSCache_Buffer* buffer,
    int value,
    bool u16)
{
    if( u16 )
        p2(buffer, value);
    else
        p1(buffer, value);
}

uint32_t
RSCache_MapTerrainEncodeBound(int flags)
{
    /* Worst case per tile: an overlay opcode and its id, a settings opcode, an
     * underlay opcode, then the height opcode and its byte. */
    uint32_t width = (flags & RSCACHE_MAP_TERRAIN_DECODE_U8) ? 1u : 2u;
    uint32_t per_tile = width * 5u + 1u;
    return per_tile * (uint32_t)RSCACHE_CHUNK_TILE_COUNT + 64u;
}

uint32_t
RSCache_MapTerrainEncode(
    const struct RSCache_MapTerrain* map_terrain,
    int flags,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !map_terrain || !out )
        return 0;
    if( out_capacity < RSCache_MapTerrainEncodeBound(flags) )
        return 0;

    bool u16 = !(flags & RSCACHE_MAP_TERRAIN_DECODE_U8);

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* Same traversal order as the decode: level, then x, then z. */
    for( int level = 0; level < RSCACHE_MAP_TERRAIN_LEVELS; level++ )
    {
        for( int x = 0; x < RSCACHE_MAP_TERRAIN_X; x++ )
        {
            for( int z = 0; z < RSCACHE_MAP_TERRAIN_Z; z++ )
            {
                const struct RSCache_MapFloor* tile =
                    &map_terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(x, z, level)];

                /*
                 * The decoder accepts these in any order and stops at opcode 0 or 1,
                 * so height is necessarily last. Everything else is emitted in
                 * ascending opcode order.
                 *
                 * shape and rotation are not written: both are derived from
                 * attr_opcode, which is stored verbatim, so writing the opcode
                 * reproduces them.
                 */
                if( tile->attr_opcode != 0 )
                {
                    write_decode(&buffer, tile->attr_opcode, u16);
                    write_decode(&buffer, tile->overlay_id, u16);
                }
                if( tile->settings != 0 )
                    write_decode(&buffer, tile->settings + 49, u16);
                if( tile->underlay_id != 0 )
                    write_decode(&buffer, tile->underlay_id + 81, u16);

                /*
                 * Height comes from `authored_height`, never from `height`: the
                 * fixup rewrites `height` for every tile — procedurally where none
                 * was given, and scaled by the tile-height basis where one was — so
                 * it no longer holds anything writable. `height_authored` is what
                 * distinguishes "the file said nothing" from "the file said this",
                 * and it is recorded whether or not the fixup ran.
                 */
                if( tile->height_authored )
                {
                    write_decode(&buffer, 1, u16);
                    p1(&buffer, tile->authored_height);
                }
                else
                {
                    write_decode(&buffer, 0, u16);
                }
            }
        }
    }

    return buffer.position;
}

struct RSCache_MapTerrain*
RSCache_MapTerrainNewDecode(
    char* data,
    int data_size,
    int map_x,
    int map_z)
{
    return RSCache_MapTerrainNewFromDecodeFlags(
        data, data_size, map_x, map_z, RSCACHE_MAP_TERRAIN_DECODE_U16);
}

void
RSCache_MapTerrainFree(struct RSCache_MapTerrain* map_terrain)
{
    if( map_terrain )
        free(map_terrain);
}

struct RSCache_MapLocs*
RSCache_MapLocsNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int map_x,
    int map_z)
{
    assert(cache);
    const struct RSCache* profile = RSCache_Dat2DiskProfile(cache);
    assert(profile && "Dat2DiskSetProfile required before map loc load");

    int file_index;
    int archive_id = dat2_map_resolve(cache, map_x, map_z, true, &file_index);
    struct RSCache_MapLocs* map_locs = NULL;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* files = NULL;
    int maps_table = RSCache_Dat2DiskTableId(cache, RSCACHE_DAT2_TABLE_MAPS);
    uint32_t* xtea_key = NULL;
    char* data;
    int size;

    if( archive_id < 0 )
    {
        printf("Failed to load map %d, %d (archive_id)\n", map_x, map_z);
        goto error;
    }

    if( RSCache_MapLocsEncrypted(profile) )
    {
        xtea_key = RSCache_Dat2DiskArchiveXteaKey(cache, maps_table, archive_id);
        if( !xtea_key )
        {
            printf("Failed to load xtea key for map %d, %d\n", map_x, map_z);
            goto error;
        }
    }

    archive =
        RSCache_Dat2DiskArchiveNewLoadDecrypted(cache, maps_table, archive_id, xtea_key);
    if( !archive )
    {
        printf("Failed to load map %d, %d\n", map_x, map_z);
        goto error;
    }
    RSCache_Dat2DiskArchiveInitMetadata(cache, archive);

    if( !dat2_map_payload(cache, archive, true, &data, &size, &files) )
    {
        printf("Failed to load map %d, %d (grouped payload)\n", map_x, map_z);
        goto error;
    }

    map_locs = RSCache_MapLocsNewDecode(data, size);
    RSCache_FileListFree(files);
    files = NULL;
    if( !map_locs )
    {
        printf("Failed to load map %d, %d\n", map_x, map_z);
        goto error;
    }

    RSCache_Dat2DiskArchiveFree(archive);

    return map_locs;

error:
    RSCache_FileListFree(files);
    RSCache_MapLocsFree(map_locs);
    RSCache_Dat2DiskArchiveFree(archive);
    return NULL;
}

struct RSCache_MapLocs*
RSCache_MapLocsNewDecode(
    char* data,
    int data_size)
{
    struct RSCache_MapLocs* map_locs = malloc(sizeof(struct RSCache_MapLocs));
    if( !map_locs )
        return NULL;
    memset(map_locs, 0, sizeof(struct RSCache_MapLocs));

    int count = 0;
    int pos = 0;
    int id = -1;
    int id_offset;

    struct RSCache_Buffer buffer = { .data = (uint8_t*)(data), .position = 0, .size = (uint32_t)(data_size) };

    while( pos < data_size &&
           (id_offset = RSCache_BufferReadUnsignedIntSmartShortCompat(&buffer)) != 0 )
    {
        id += id_offset;

        int position = 0;
        int pos_offset;
        while( (pos_offset = RSCache_BufferReadUnsignedShortSmart(&buffer)) != 0 )
        {
            g1(&buffer);
            position += pos_offset - 1;
            count++;
            pos++;
        }
        (void)position;
        (void)id;
    }

    map_locs->locs = malloc(sizeof(struct RSCache_MapLoc) * count);
    if( !map_locs->locs )
    {
        free(map_locs);
        return NULL;
    }
    map_locs->locs_count = count;

    buffer.position = 0;

    pos = 0;
    id = -1;
    int loc_idx = 0;

    while( pos < data_size &&
           (id_offset = RSCache_BufferReadUnsignedIntSmartShortCompat(&buffer)) != 0 )
    {
        id += id_offset;

        int position = 0;
        int pos_offset;
        while( (pos_offset = RSCache_BufferReadUnsignedShortSmart(&buffer)) != 0 )
        {
            position += pos_offset - 1;

            int local_z = position & 0x3F;
            int local_x = (position >> 6) & 0x3F;
            int height = (position >> 12) & 0x3;

            int attributes = g1(&buffer);
            int shape_select = attributes >> 2;
            int orientation = attributes & 0x3;

            map_locs->locs[loc_idx].loc_id = id;
            map_locs->locs[loc_idx].shape_select = shape_select;
            map_locs->locs[loc_idx].orientation = orientation;
            map_locs->locs[loc_idx].chunk_pos_x = local_x;
            map_locs->locs[loc_idx].chunk_pos_z = local_z;
            map_locs->locs[loc_idx].chunk_pos_level = height;

            loc_idx++;
        }
    }

    return map_locs;
}

void
RSCache_MapLocsFree(struct RSCache_MapLocs* map_locs)
{
    if( map_locs )
    {
        free(map_locs->locs);
        free(map_locs);
    }
}

struct RSCache_Dat2DiskArchive*
RSCache_MapLocsArchiveNewLoad(
    struct RSCache_Dat2Disk* cache,
    int map_x,
    int map_z)
{
    assert(cache);
    const struct RSCache* profile = RSCache_Dat2DiskProfile(cache);
    assert(profile && "Dat2DiskSetProfile required before map loc load");

    uint32_t* xtea_key = NULL;
    struct RSCache_Dat2DiskArchive* archive = NULL;

    int file_index;
    int archive_id = dat2_map_resolve(cache, map_x, map_z, true, &file_index);
    int maps_table = RSCache_Dat2DiskTableId(cache, RSCACHE_DAT2_TABLE_MAPS);
    if( archive_id < 0 )
    {
        printf("Failed to load map %d, %d (archive_id)\n", map_x, map_z);
        goto error;
    }
    if( RSCache_MapLocsEncrypted(profile) )
    {
        xtea_key = RSCache_Dat2DiskArchiveXteaKey(cache, maps_table, archive_id);
        if( !xtea_key )
        {
            printf("Failed to load xtea key for map %d, %d\n", map_x, map_z);
            goto error;
        }
    }

    archive =
        RSCache_Dat2DiskArchiveNewLoadDecrypted(cache, maps_table, archive_id, xtea_key);
    if( !archive )
    {
        printf("Failed to load map %d, %d\n", map_x, map_z);
        goto error;
    }
    RSCache_Dat2DiskArchiveInitMetadata(cache, archive);

    return archive;

error:
    RSCache_Dat2DiskArchiveFree(archive);
    return NULL;
}
