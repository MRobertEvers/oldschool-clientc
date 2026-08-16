#include "dat2a_maps.h"

#include "../shared/shared_archive.h"
#include "../shared/shared_archive_decompress.h"
#include "../dat2disk/dat2disk.h"
#include "../dat1disk/dat1disk.h"
#include "../shared/shared_rs_buffer.h"
#include "dat2a_noise.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
generate_height(
    int x,
    int y)
{
    int n = RSCacheDat2A_NoisePerlinNoise(x + 45365, y + 91923, 4) - 128 +
            ((RSCacheDat2A_NoisePerlinNoise(x + 10294, y + 37821, 2) - 128) >> 1) +
            ((RSCacheDat2A_NoisePerlinNoise(x, y, 1) - 128) >> 2);
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

/**
 * Normally, some of this calculation is done in the map terrain loader.
 *
 * The deob meteor client and rs map viewer do this calculation there.
 * src/rs/scene/SceneBuilder.ts decodeTerrainTile
 */
static void
fixup_terrain(
    struct RSCacheDat2A_MapTerrain* map_terrain,
    int map_x,
    int map_z)
{
    int base_x = map_x * 64;
    int base_z = map_z * 64;
    for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
    {
        for( int z = 0; z < MAP_TERRAIN_Z; z++ )
        {
            for( int x = 0; x < MAP_TERRAIN_X; x++ )
            {
                struct RSCacheDat2A_MapFloor* map = &map_terrain->tiles_xyz[MAP_TILE_COORD(x, z, level)];
                // If the height is unset, then the terrain is procedurally generated with
                // generate_height.
                if( map->height == 0 )
                {
                    if( level == 0 )
                    {
                        int world_x = base_x + x + 932731;
                        int world_z = base_z + z + 556238;
                        int height = generate_height(world_x, world_z);
                        map->height = -height * MAP_UNITS_TILE_HEIGHT_BASIS;
                    }
                    else
                    {
                        int lower = map_terrain->tiles_xyz[MAP_TILE_COORD(x, z, level - 1)].height;
                        map->height = lower - MAP_UNITS_LEVEL_HEIGHT;
                    }
                }
                else
                {
                    if( map->height == 1 )
                        map->height = 0;

                    if( level == 0 )
                    {
                        map->height = -map->height * MAP_UNITS_TILE_HEIGHT_BASIS;
                    }
                    else
                    {
                        int lower = map_terrain->tiles_xyz[MAP_TILE_COORD(x, z, level - 1)].height;
                        map->height = lower - map->height * MAP_UNITS_TILE_HEIGHT_BASIS;
                    }
                }
            }
        }
    }
}

static int
dat_map_square_id(
    int map_x,
    int map_y)
{
    return (map_x << 8) + map_y;
}

static int
dat2_map_terrain_id(
    struct RSCacheDat2Disk* cache,
    int map_x,
    int map_y)
{
    char name[13];

    snprintf(name, sizeof(name), "m%d_%d", map_x, map_y);

    int name_hash = RSCacheShared_ArchiveNameHashDat2(name);

    struct RSCacheDat2Disk_ReferenceTable* table = cache->tables[RSCacheDat2Disk_Table_Maps];

    for( int i = 0; i < table->archive_count; i++ )
    {
        struct RSCacheDat2Disk_ArchiveReference* archive_reference = &table->archives[i];

        if( archive_reference->identifier == name_hash )
            return archive_reference->index;
    }

    return -1;
}

static int
dat2_map_loc_id(
    struct RSCacheDat2Disk* cache,
    int map_x,
    int map_y)
{
    char name[13];
    snprintf(name, sizeof(name), "l%d_%d", map_x, map_y);

    int name_hash = RSCacheShared_ArchiveNameHashDat2(name);

    struct RSCacheDat2Disk_ReferenceTable* table = cache->tables[RSCacheDat2Disk_Table_Maps];

    for( int i = 0; i < table->archive_count; i++ )
    {
        struct RSCacheDat2Disk_ArchiveReference* archive_reference = &table->archives[i];

        if( archive_reference->identifier == name_hash )
            return archive_reference->index;
    }

    return -1;
}

static int
dat2_map_npc_id(
    struct RSCacheDat2Disk* cache,
    int map_x,
    int map_y)
{
    char name[13];
    snprintf(name, sizeof(name), "n%d_%d", map_x, map_y);

    int name_hash = RSCacheShared_ArchiveNameHashDat2(name);

    struct RSCacheDat2Disk_ReferenceTable* table = cache->tables[RSCacheDat2Disk_Table_Maps];

    for( int i = 0; i < table->archive_count; i++ )
    {
        struct RSCacheDat2Disk_ArchiveReference* archive_reference = &table->archives[i];

        if( archive_reference->identifier == name_hash )
            return archive_reference->index;
    }

    return -1;
}

// private void loadTerrain(MapDefinition map, byte[] buf)
// 	{
// 		Tile[][][] tiles = map.getTiles();

// 		InputStream in = new InputStream(buf);

// 		for (int z = 0; z < Z; z++)
// 		{
// 			for (int x = 0; x < X; x++)
// 			{
// 				for (int y = 0; y < Y; y++)
// 				{
// 					Tile tile = tiles[z][x][y] = new Tile();
// 					while (true)
// 					{
// 						int attribute = in.readUnsignedShort();
// 						if (attribute == 0)
// 						{
// 							break;
// 						}
// 						else if (attribute == 1)
// 						{
// 							int height = in.readUnsignedByte();
// 							tile.height = height;
// 							break;
// 						}
// 						else if (attribute <= 49)
// 						{
// 							tile.attrOpcode = attribute;
// 							tile.overlayId = in.readShort();
// 							tile.overlayPath = (byte) ((attribute - 2) / 4);
// 							tile.overlayRotation = (byte) (attribute - 2 & 3);
// 						}
// 						else if (attribute <= 81)
// 						{
// 							tile.settings = (byte) (attribute - 49);
// 						}
// 						else
// 						{
// 							tile.underlayId = (short) (attribute - 81);
// 						}
// 					}
// 				}
// 			}
// 		}
// 	}
struct RSCacheDat2A_MapTerrain*
RSCacheDat2A_MapTerrainNewFromCache(
    struct RSCacheDat2Disk* cache,
    int map_x,
    int map_y)
{
    struct RSCacheDat2Disk_Archive* archive = NULL;
    struct RSCacheDat2A_MapTerrain* map_terrain = NULL;
    int archive_id = dat2_map_terrain_id(cache, map_x, map_y);

    if( archive_id == -1 )
    {
        printf("Failed to load map terrain %d, %d (archive_id: %d)\n", map_x, map_y, archive_id);
        return NULL;
    }

    archive = RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Maps, archive_id);
    if( !archive )
    {
        printf("Failed to load map terrain %d, %d cache_load\n", map_x, map_y);
        return NULL;
    }

    map_terrain = map_terrain_new_from_decode(archive->data, archive->data_size, map_x, map_y);
    if( !map_terrain )
    {
        printf("Failed to load map terrain %d, %d terrain_new_from_decode\n", map_x, map_y);
        return NULL;
    }

    RSCacheDat2Disk_ArchiveFree(archive);

    return map_terrain;

    RSCacheDat2Disk_ArchiveFree(archive);
    RSCacheDat2A_MapTerrainFree(map_terrain);
    return NULL;
}

struct RSCacheDat2A_MapTerrain*
RSCacheDat2A_MapTerrainNewFromCacheDat(
    struct RSCacheDat1Disk* cache_dat,
    int map_x,
    int map_y)
{
    struct RSCacheDat1Disk_Archive* archive = NULL;
    struct RSCacheDat2A_MapTerrain* map_terrain = NULL;
    int archive_id = dat2_map_terrain_id((void*)cache_dat, map_x, map_y);

    if( archive_id == -1 )
    {
        printf("Failed to load map terrain %d, %d (archive_id: %d)\n", map_x, map_y, archive_id);
        return NULL;
    }

    archive = RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Maps, archive_id);
    if( !archive )
    {
        printf("Failed to load map terrain %d, %d cache_load\n", map_x, map_y);
        return NULL;
    }

    map_terrain = map_terrain_new_from_decode(archive->data, archive->data_size, map_x, map_y);
    if( !map_terrain )
    {
        printf("Failed to load map terrain %d, %d terrain_new_from_decode\n", map_x, map_y);
        return NULL;
    }

    RSCacheDat1Disk_ArchiveFree(archive);

    return map_terrain;

    RSCacheDat1Disk_ArchiveFree(archive);
    RSCacheDat2A_MapTerrainFree(map_terrain);
    return NULL;
}

struct RSCacheDat2Disk_Archive* //
RSCacheDat2A_MapTerrainArchiveNewLoad( //
    struct RSCacheDat2Disk* cache, int map_x, int map_y)
{
    struct RSCacheDat2Disk_Archive* archive = NULL;
    struct RSCacheDat2A_MapTerrain* map_terrain = NULL;
    int archive_id = dat2_map_terrain_id(cache, map_x, map_y);

    if( archive_id == -1 )
    {
        printf("Failed to load map terrain %d, %d (archive_id: %d)\n", map_x, map_y, archive_id);
        return NULL;
    }

    archive = RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Maps, archive_id);
    if( !archive )
    {
        printf("Failed to load map terrain %d, %d cache_load\n", map_x, map_y);
        return NULL;
    }

    return archive;
}

struct RSCacheDat2A_MapTerrain*
RSCacheDat2A_MapTerrainNewFromArchive(
    struct RSCacheDat2Disk_Archive* archive,
    int map_x,
    int map_z)
{
    struct RSCacheDat2A_MapTerrain* map_terrain =
        map_terrain_new_from_decode(archive->data, archive->data_size, map_x, map_z);
    if( !map_terrain )
    {
        printf("Failed to load map terrain %d, %d terrain_new_from_decode\n", map_x, map_z);
        return NULL;
    }

    return map_terrain;
}

static int
read_decode(
    struct RSCacheShared_RSBuffer* buffer,
    bool u16)
{
    if( u16 )
        return g2(buffer);
    else
        return g1(buffer);
}

struct RSCacheDat2A_MapTerrain* //
RSCacheDat2A_MapTerrainNewFromDecodeFlags( //
    char* data, int data_size, int map_x, int map_z, int flags)
{
    struct RSCacheDat2A_MapTerrain* map_terrain = malloc(sizeof(struct RSCacheDat2A_MapTerrain));
    memset(map_terrain, 0, sizeof(struct RSCacheDat2A_MapTerrain));

    struct RSCacheShared_RSBuffer buffer = { .data = (uint8_t*)(data), .position = 0, .size = (uint32_t)(data_size) };

    for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
    {
        for( int x = 0; x < MAP_TERRAIN_X; x++ )
        {
            for( int z = 0; z < MAP_TERRAIN_Z; z++ )
            {
                struct RSCacheDat2A_MapFloor* tile = &map_terrain->tiles_xyz[MAP_TILE_COORD(x, z, level)];

                while( true )
                {
                    int attribute = read_decode(&buffer, flags == MAP_TERRAIN_DECODE_U16);
                    if( attribute == 0 )
                    {
                        break;
                    }
                    else if( attribute == 1 )
                    {
                        int height = g1(&buffer);
                        assert(tile->height == 0);
                        tile->height = height;
                        break;
                    }
                    else if( attribute <= 49 )
                    {
                        tile->overlay_id = read_decode(&buffer, flags == MAP_TERRAIN_DECODE_U16);
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

    fixup_terrain(map_terrain, map_x, map_z);

    return map_terrain;
}

struct RSCacheDat2A_MapTerrain*
map_terrain_new_from_decode(
    char* data,
    int data_size,
    int map_x,
    int map_z)
{
    return RSCacheDat2A_MapTerrainNewFromDecodeFlags(data, data_size, map_x, map_z, MAP_TERRAIN_DECODE_U16);
}

void
RSCacheDat2A_MapTerrainFree(struct RSCacheDat2A_MapTerrain* map_terrain)
{
    if( map_terrain )
        free(map_terrain);
}

struct RSCacheDat2A_MapLocs*
RSCacheDat2A_MapLocsNewFromCache(
    struct RSCacheDat2Disk* cache,
    int map_x,
    int map_y)
{
    int archive_id = dat2_map_loc_id(cache, map_x, map_y);
    struct RSCacheDat2A_MapLocs* map_locs = NULL;
    struct RSCacheDat2Disk_Archive* archive = NULL;
    uint32_t* xtea_key = RSCacheDat2Disk_ArchiveXteaKey(cache, RSCacheDat2Disk_Table_Maps, archive_id);

    if( !xtea_key )
    {
        printf("Failed to load xtea key for map %d, %d\n", map_x, map_y);
        goto error;
    }

    archive = RSCacheDat2Disk_ArchiveNewLoadDecrypted(cache, RSCacheDat2Disk_Table_Maps, archive_id, xtea_key);
    if( !archive )
    {
        printf("Failed to load map %d, %d\n", map_x, map_y);
        return NULL;
    }

    map_locs = map_locs_new_from_decode(archive->data, archive->data_size);
    if( !map_locs )
    {
        printf("Failed to load map %d, %d\n", map_x, map_y);
        goto error;
    }

    RSCacheDat2Disk_ArchiveFree(archive);

    return map_locs;

error:
    RSCacheDat2A_MapLocsFree(map_locs);
    RSCacheDat2Disk_ArchiveFree(archive);
    return NULL;
}

// public static Position
// fromPacked(int packedPosition)
// {
//     if( packedPosition == -1 )
//     {
//         return new Position(-1, -1, -1);
//     }

//     int z = packedPosition >> 28 & 3;
//     int x = packedPosition >> 14 & 16383;
//     int y = packedPosition & 16383;
//     return new Position(x, y, z);
// }

// private void loadLocations(LocationsDefinition loc, byte[] b)
// {
// 	InputStream buf = new InputStream(b);

// 	int id = -1;
// 	int idOffset;

// 	while ((idOffset = buf.readUnsignedIntSmartShortCompat()) != 0)
// 	{
// 		id += idOffset;

// 		int position = 0;
// 		int positionOffset;

// 		while ((positionOffset = buf.readUnsignedShortSmart()) != 0)
// 		{
// 			position += positionOffset - 1;

// 			int localY = position & 0x3F;
// 			int localX = position >> 6 & 0x3F;
// 			int height = position >> 12 & 0x3;

// 			int attributes = buf.readUnsignedByte();
// 			int type = attributes >> 2;
// 			int orientation = attributes & 0x3;

// 			loc.getLocations().add(new Location(id, type, orientation, new Position(localX, localY,
// height)));
// 		}
// 	}
// }

struct RSCacheDat2A_MapLocs*
map_locs_new_from_decode(
    char* data,
    int data_size)
{
    struct RSCacheDat2A_MapLocs* map_locs = malloc(sizeof(struct RSCacheDat2A_MapLocs));
    if( !map_locs )
        return NULL;

    // First pass to count number of locations
    int count = 0;
    int pos = 0;
    int id = -1;
    int id_offset;

    struct RSCacheShared_RSBuffer buffer = { .data = (uint8_t*)(data), .position = 0, .size = (uint32_t)(data_size) };

    while( pos < data_size &&
           (id_offset = RSCacheShared_RSBufferReadUnsignedIntSmartShortCompat(&buffer)) != 0 )
    {
        id += id_offset;

        int position = 0;
        int pos_offset;
        while( (pos_offset = RSCacheShared_RSBufferReadUnsignedShortSmart(&buffer)) != 0 )
        {
            RSCacheShared_RSBufferG1(&buffer);
            position += pos_offset - 1;
            count++;
            pos++; // Skip attributes byte
        }
        (void)position;
    }

    map_locs->locs = malloc(sizeof(struct RSCacheDat2A_MapLoc) * count);
    if( !map_locs->locs )
    {
        free(map_locs);
        return NULL;
    }
    map_locs->locs_count = count;

    buffer.position = 0;

    // Second pass to actually read the data
    pos = 0;
    id = -1;
    int loc_idx = 0;

    while( pos < data_size &&
           (id_offset = RSCacheShared_RSBufferReadUnsignedIntSmartShortCompat(&buffer)) != 0 )
    {
        id += id_offset;

        int position = 0;
        int pos_offset;
        while( (pos_offset = RSCacheShared_RSBufferReadUnsignedShortSmart(&buffer)) != 0 )
        {
            position += pos_offset - 1;

            int local_z = position & 0x3F;
            int local_x = (position >> 6) & 0x3F;
            int height = (position >> 12) & 0x3;

            int attributes = RSCacheShared_RSBufferG1(&buffer);
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
RSCacheDat2A_MapLocsFree(struct RSCacheDat2A_MapLocs* map_locs)
{
    if( map_locs )
    {
        free(map_locs->locs);
        free(map_locs);
    }
}

struct RSCacheDat2Disk_Archive*
RSCacheDat2A_MapLocsArchiveNewLoad(
    struct RSCacheDat2Disk* cache,
    int map_x,
    int map_y)
{
    uint32_t* xtea_key = NULL;
    struct RSCacheDat2Disk_Archive* archive = NULL;

    int archive_id = dat2_map_loc_id(cache, map_x, map_y);
    xtea_key = RSCacheDat2Disk_ArchiveXteaKey(cache, RSCacheDat2Disk_Table_Maps, archive_id);
    if( !xtea_key )
    {
        printf("Failed to load xtea key for map %d, %d\n", map_x, map_y);
        goto error;
    }

    archive = RSCacheDat2Disk_ArchiveNewLoadDecrypted(cache, RSCacheDat2Disk_Table_Maps, archive_id, xtea_key);
    if( !archive )
    {
        printf("Failed to load map %d, %d\n", map_x, map_y);
        goto error;
    }

    return archive;

error:
    free(archive);
    return NULL;
}
