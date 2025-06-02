#include "maps.h"

#include "buffer.h"
#include "osrs/archive.h"
#include "osrs/archive_decompress.h"
#include "osrs/cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
dat_map_square_id(int map_x, int map_y)
{
    return map_x << 8 + map_y;
}

static int
dat2_map_terrain_id(struct Cache* cache, int map_x, int map_y)
{
    char name[13];

    snprintf(name, sizeof(name), "m%d_%d", map_x, map_y);

    int name_hash = archive_name_hash(name);

    struct ReferenceTable* table = cache->tables[CACHE_MAPS];

    for( int i = 0; i < table->archive_count; i++ )
    {
        struct ArchiveReference* archive_reference = &table->archives[i];

        if( archive_reference->identifier == name_hash )
            return archive_reference->index;
    }

    return -1;
}

static int
dat2_map_loc_id(struct Cache* cache, int map_x, int map_y)
{
    char name[13];
    snprintf(name, sizeof(name), "l%d_%d", map_x, map_y);

    int name_hash = archive_name_hash(name);

    struct ReferenceTable* table = cache->tables[CACHE_MAPS];

    for( int i = 0; i < table->archive_count; i++ )
    {
        struct ArchiveReference* archive_reference = &table->archives[i];

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
struct MapTerrain*
map_terrain_new_from_cache(struct Cache* cache, int map_x, int map_y)
{
    struct CacheArchive* archive = NULL;
    struct MapTerrain* map_terrain = NULL;
    int archive_id = dat2_map_terrain_id(cache, map_x, map_y);

    if( archive_id == -1 )
    {
        printf("Failed to load map terrain %d, %d\n", map_x, map_y);
        return NULL;
    }

    archive = cache_archive_new_load(cache, CACHE_MAPS, archive_id);
    if( !archive )
    {
        printf("Failed to load map terrain %d, %d\n", map_x, map_y);
        return NULL;
    }

    map_terrain = map_terrain_new_from_decode(archive->data, archive->data_size);
    if( !map_terrain )
    {
        printf("Failed to load map terrain %d, %d\n", map_x, map_y);
        return NULL;
    }

    cache_archive_free(archive);

    return map_terrain;

error:
    cache_archive_free(archive);
    map_terrain_free(map_terrain);
    return NULL;
}

struct MapTerrain*
map_terrain_new_from_decode(char* data, int data_size)
{
    struct MapTerrain* map_terrain = malloc(sizeof(struct MapTerrain));
    memset(map_terrain, 0, sizeof(struct MapTerrain));

    struct Buffer buffer = { .data = data, .position = 0, .data_size = data_size };

    for( int z = 0; z < MAP_TERRAIN_Z; z++ )
    {
        for( int x = 0; x < MAP_TERRAIN_X; x++ )
        {
            for( int y = 0; y < MAP_TERRAIN_Y; y++ )
            {
                struct MapTile* tile = &map_terrain->tiles_xyz[MAP_TILE_COORD(x, y, z)];

                while( true )
                {
                    int attribute = read_u16(&buffer);
                    if( attribute == 0 )
                    {
                        break;
                    }
                    else if( attribute == 1 )
                    {
                        tile->height = read_u8(&buffer);
                        break;
                    }
                    else if( attribute <= 49 )
                    {
                        tile->attrOpcode = attribute;
                        tile->overlayId = read_16(&buffer);
                        tile->overlayPath = (attribute - 2) / 4;
                        tile->overlayRotation = attribute - 2 & 3;
                    }
                    else if( attribute <= 81 )
                    {
                        tile->settings = attribute - 49;
                    }
                    else
                    {
                        tile->underlayId = attribute - 81;
                    }
                }
            }
        }
    }

    return map_terrain;
}

void
map_terrain_free(struct MapTerrain* map_terrain)
{
    if( map_terrain )
        free(map_terrain);
}

struct MapLocs*
map_locs_new_from_cache(struct Cache* cache, int map_x, int map_y)
{
    int archive_id = dat2_map_loc_id(cache, map_x, map_y);

    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_MAPS, archive_id);
    if( !archive )
    {
        printf("Failed to load map %d, %d\n", map_x, map_y);
        return NULL;
    }

    uint32_t* xtea_key = cache_archive_xtea_key(cache, CACHE_MAPS, archive_id);

    if( !xtea_key )
    {
        printf("Failed to load xtea key for map %d, %d\n", map_x, map_y);
        return NULL;
    }

    archive_decrypt_decompress(archive, xtea_key);
}