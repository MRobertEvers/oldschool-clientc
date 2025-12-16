#include "maps.h"

#include "noise.h"
#include "osrs/archive.h"
#include "osrs/archive_decompress.h"
#include "osrs/cache.h"
#include "osrs/rsbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
generate_height(int x, int y)
{
    int n = perlin_noise(x + 45365, y + 91923, 4) - 128 +
            ((perlin_noise(x + 10294, y + 37821, 2) - 128) >> 1) +
            ((perlin_noise(x, y, 1) - 128) >> 2);
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
    struct CacheMapTerrain* map_terrain, int world_scene_origin_x, int world_scene_origin_y)
{
    for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
    {
        for( int z = 0; z < MAP_TERRAIN_Z; z++ )
        {
            for( int x = 0; x < MAP_TERRAIN_X - 1; x++ )
            {
                struct CacheMapFloor* map = &map_terrain->tiles_xyz[MAP_TILE_COORD(x, z, level)];
                // If the height is unset, then the terrain is procedurally generated with
                // generate_height.
                if( map->height == 0 )
                {
                    if( z == 0 )
                    {
                        int worldX = world_scene_origin_x + (-58) + 932731;
                        int worldY = world_scene_origin_y + (-58) + 556238;
                        map->height =
                            -generate_height(worldX, worldY) * MAP_UNITS_TILE_HEIGHT_BASIS;
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

                    if( z == 0 )
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

static int
dat2_map_npc_id(struct Cache* cache, int map_x, int map_y)
{
    char name[13];
    snprintf(name, sizeof(name), "n%d_%d", map_x, map_y);

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
struct CacheMapTerrain*
map_terrain_new_from_cache(struct Cache* cache, int map_x, int map_y)
{
    struct CacheArchive* archive = NULL;
    struct CacheMapTerrain* map_terrain = NULL;
    int archive_id = dat2_map_terrain_id(cache, map_x, map_y);

    if( archive_id == -1 )
    {
        printf("Failed to load map terrain %d, %d (archive_id: %d)\n", map_x, map_y, archive_id);
        return NULL;
    }

    archive = cache_archive_new_load(cache, CACHE_MAPS, archive_id);
    if( !archive )
    {
        printf("Failed to load map terrain %d, %d cache_load\n", map_x, map_y);
        return NULL;
    }

    map_terrain = map_terrain_new_from_decode(archive->data, archive->data_size);
    if( !map_terrain )
    {
        printf("Failed to load map terrain %d, %d terrain_new_from_decode\n", map_x, map_y);
        return NULL;
    }

    fixup_terrain(map_terrain, map_x, map_y);

    cache_archive_free(archive);

    return map_terrain;

    cache_archive_free(archive);
    map_terrain_free(map_terrain);
    return NULL;
}

struct CacheArchive* //
map_terrain_archive_new_load( //
    struct Cache* cache, int map_x, int map_y)
{
    struct CacheArchive* archive = NULL;
    struct CacheMapTerrain* map_terrain = NULL;
    int archive_id = dat2_map_terrain_id(cache, map_x, map_y);

    if( archive_id == -1 )
    {
        printf("Failed to load map terrain %d, %d (archive_id: %d)\n", map_x, map_y, archive_id);
        return NULL;
    }

    archive = cache_archive_new_load(cache, CACHE_MAPS, archive_id);
    if( !archive )
    {
        printf("Failed to load map terrain %d, %d cache_load\n", map_x, map_y);
        return NULL;
    }

    return archive;

error:
    cache_archive_free(archive);
    return NULL;
}

struct CacheMapTerrain*
map_terrain_new_from_archive(struct CacheArchive* archive, int map_x, int map_y)
{
    struct CacheMapTerrain* map_terrain =
        map_terrain_new_from_decode(archive->data, archive->data_size);
    if( !map_terrain )
    {
        printf("Failed to load map terrain %d, %d terrain_new_from_decode\n", map_x, map_y);
        return NULL;
    }

    fixup_terrain(map_terrain, map_x, map_y);

    return map_terrain;
}

struct CacheMapTerrain*
map_terrain_new_from_decode(char* data, int data_size)
{
    struct CacheMapTerrain* map_terrain = malloc(sizeof(struct CacheMapTerrain));
    memset(map_terrain, 0, sizeof(struct CacheMapTerrain));

    struct RSBuffer buffer = { .data = data, .position = 0, .size = data_size };

    for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
    {
        for( int x = 0; x < MAP_TERRAIN_X; x++ )
        {
            for( int z = 0; z < MAP_TERRAIN_Z; z++ )
            {
                struct CacheMapFloor* tile = &map_terrain->tiles_xyz[MAP_TILE_COORD(x, z, level)];

                while( true )
                {
                    int attribute = g2(&buffer);
                    if( attribute == 0 )
                    {
                        break;
                    }
                    else if( attribute == 1 )
                    {
                        int height = g1(&buffer);
                        tile->height = height;
                        break;
                    }
                    else if( attribute <= 49 )
                    {
                        tile->attr_opcode = attribute;
                        tile->overlay_id = g2(&buffer);
                        tile->shape = (attribute - 2) / 4;
                        tile->rotation = attribute - 2 & 3;
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

    return map_terrain;
}

void
map_terrain_free(struct CacheMapTerrain* map_terrain)
{
    if( map_terrain )
        free(map_terrain);
}

struct CacheMapTerrainIter*
map_terrain_iter_new_from_ptrs(struct CacheMapTerrain** chunks, int count, int width)
{
    struct CacheMapTerrainIter* iter = malloc(sizeof(struct CacheMapTerrainIter));
    if( !iter )
        return NULL;
    memset(iter, 0, sizeof(struct CacheMapTerrainIter));
    iter->chunks_ptrs = chunks;
    iter->chunks_count = count;
    iter->chunks_width = width;
    iter->index = 0;
    return iter;
}

void
map_terrain_iter_free(struct CacheMapTerrainIter* iter)
{
    if( iter )
        free(iter);
}

void
map_terrain_iter_begin(struct CacheMapTerrainIter* iter)
{
    iter->index = 0;
}

struct CacheMapFloor*
map_terrain_iter_next(struct CacheMapTerrainIter* iter)
{
    int index = iter->index++;
    int chunk = 0;
    while( index >= CHUNK_TILE_COUNT )
    {
        index -= CHUNK_TILE_COUNT;
        chunk++;
    }
    return &iter->chunks_ptrs[chunk]->tiles_xyz[index];
}

struct CacheMapFloor*
map_terrain_iter_at(struct CacheMapTerrainIter* iter, int sx, int sz, int slevel)
{
    assert(sx >= 0 && sx < map_terrain_iter_bound_x(iter));
    assert(sz >= 0 && sz < map_terrain_iter_bound_z(iter));
    assert(slevel >= 0 && slevel < MAP_TERRAIN_LEVELS);

    int chunk_idx_x = sx / MAP_CHUNK_SIZE;
    int chunk_idx_y = sz / MAP_CHUNK_SIZE;
    int chunk_idx = chunk_idx_y + chunk_idx_x * iter->chunks_width;

    int tile_idx_x = sx % MAP_CHUNK_SIZE;
    int tile_idx_y = sz % MAP_CHUNK_SIZE;
    int tile_idx = MAP_TILE_COORD(tile_idx_x, tile_idx_y, slevel);

    return &iter->chunks_ptrs[chunk_idx]->tiles_xyz[tile_idx];
}

int
map_terrain_iter_tiles_size(struct CacheMapTerrainIter* iter)
{
    return iter->chunks_count * MAP_TERRAIN_X * MAP_TERRAIN_Z * MAP_TERRAIN_LEVELS;
}

int
map_terrain_iter_bound_x(struct CacheMapTerrainIter* iter)
{
    return iter->chunks_width * MAP_TERRAIN_X;
}

int
map_terrain_iter_bound_z(struct CacheMapTerrainIter* iter)
{
    return iter->chunks_count / iter->chunks_width * MAP_TERRAIN_Z;
}

struct CacheMapLocs*
map_locs_new_from_cache(struct Cache* cache, int map_x, int map_y)
{
    int archive_id = dat2_map_loc_id(cache, map_x, map_y);
    struct CacheMapLocs* map_locs = NULL;
    struct CacheArchive* archive = NULL;
    uint32_t* xtea_key = cache_archive_xtea_key(cache, CACHE_MAPS, archive_id);

    if( !xtea_key )
    {
        printf("Failed to load xtea key for map %d, %d\n", map_x, map_y);
        goto error;
    }

    archive = cache_archive_new_load_decrypted(cache, CACHE_MAPS, archive_id, xtea_key);
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

    cache_archive_free(archive);

    return map_locs;

error:
    map_locs_free(map_locs);
    cache_archive_free(archive);
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

struct CacheMapLocs*
map_locs_new_from_decode(char* data, int data_size)
{
    struct CacheMapLocs* map_locs = malloc(sizeof(struct CacheMapLocs));
    if( !map_locs )
        return NULL;

    // First pass to count number of locations
    int count = 0;
    int pos = 0;
    int id = -1;
    int id_offset;

    struct RSBuffer buffer = { .data = data, .position = 0, .size = data_size };

    while( pos < data_size &&
           (id_offset = rsbuf_read_unsigned_int_smart_short_compat(&buffer)) != 0 )
    {
        id += id_offset;

        int position = 0;
        int pos_offset;
        while( (pos_offset = rsbuf_read_unsigned_short_smart(&buffer)) != 0 )
        {
            rsbuf_g1(&buffer);
            position += pos_offset - 1;
            count++;
            pos++; // Skip attributes byte
        }
    }

    map_locs->locs = malloc(sizeof(struct CacheMapLoc) * count);
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
           (id_offset = rsbuf_read_unsigned_int_smart_short_compat(&buffer)) != 0 )
    {
        id += id_offset;

        int position = 0;
        int pos_offset;
        while( (pos_offset = rsbuf_read_unsigned_short_smart(&buffer)) != 0 )
        {
            position += pos_offset - 1;

            int local_y = position & 0x3F;
            int local_x = (position >> 6) & 0x3F;
            int height = (position >> 12) & 0x3;

            int attributes = rsbuf_g1(&buffer);
            int shape_select = attributes >> 2;
            int orientation = attributes & 0x3;

            map_locs->locs[loc_idx].loc_id = id;
            map_locs->locs[loc_idx].shape_select = shape_select;
            map_locs->locs[loc_idx].orientation = orientation;
            map_locs->locs[loc_idx].chunk_pos_x = local_x;
            map_locs->locs[loc_idx].chunk_pos_y = local_y;
            map_locs->locs[loc_idx].chunk_pos_level = height;

            loc_idx++;
        }
    }

    return map_locs;
}

void
map_locs_free(struct CacheMapLocs* map_locs)
{
    if( map_locs )
    {
        free(map_locs->locs);
        free(map_locs);
    }
}

struct CacheArchive*
map_locs_archive_new_load(struct Cache* cache, int map_x, int map_y)
{
    int* xtea_key = NULL;
    struct CacheArchive* archive = NULL;

    int archive_id = dat2_map_loc_id(cache, map_x, map_y);
    xtea_key = cache_archive_xtea_key(cache, CACHE_MAPS, archive_id);
    if( !xtea_key )
    {
        printf("Failed to load xtea key for map %d, %d\n", map_x, map_y);
        goto error;
    }

    archive = cache_archive_new_load_decrypted(cache, CACHE_MAPS, archive_id, xtea_key);
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

struct CacheMapLocsIter*
map_locs_iter_new_from_ptrs(struct CacheMapLocs** chunks, int count, int width)
{
    struct CacheMapLocsIter* iter = malloc(sizeof(struct CacheMapLocsIter));
    if( !iter )
        return NULL;
    memset(iter, 0, sizeof(struct CacheMapLocsIter));
    iter->is_ptrs = true;
    iter->_chunks_ptrs = chunks;
    iter->chunks_count = count;
    iter->width = width;
    iter->index = 0;
    return iter;
}

void
map_locs_iter_free(struct CacheMapLocsIter* iter)
{
    if( iter )
    {
        free(iter);
    }
}

void
map_locs_iter_begin(struct CacheMapLocsIter* iter)
{
    iter->index = 0;
}

struct CacheMapLoc*
map_locs_iter_next(struct CacheMapLocsIter* iter)
{
    int index = iter->index++;
    int chunk = 0;
    while( index >= iter->_chunks_ptrs[chunk]->locs_count )
    {
        index -= iter->_chunks_ptrs[chunk]->locs_count;
        chunk++;

        if( chunk >= iter->chunks_count )
            return NULL;
    }

    return &iter->_chunks_ptrs[chunk]->locs[index];

    // int chunk_side = MAP_CHUNK_SIZE;
    // int stride = iter->width * chunk_side;

    // int y = iter->index / stride;
    // int chunk_y = y / chunk_side;

    // int x = iter->index % stride;
    // int chunk_x = x / chunk_side;

    // int elem_idx = (y % chunk_side) * chunk_side + (x % chunk_side);
    // int chunk_idx = chunk_y * iter->width + chunk_x;

    // iter->index++;

    // if( iter->is_ptrs )
    //     return &iter->_chunks_ptrs[chunk_idx]->locs[elem_idx];
    // else
    //     return &iter->_chunks[chunk_idx].locs[elem_idx];
}

// struct CacheMapLoc*
// DEPRECATED_map_locs_iter_next(struct CacheMapLocsIter* iter)
// {
//     struct RSBuffer* buffer = &iter->_buffer;

//     iter->is_populated = false;

//     if( buffer->position >= buffer->size )
//         return NULL;

//     while( iter->_state != 2 )
//     {
//         switch( iter->_state )
//         {
//         case 0:
//         {
//             int id_offset = rsbuf_read_unsigned_int_smart_short_compat(buffer);
//             if( id_offset == 0 )
//             {
//                 iter->_state = 2;
//                 return NULL;
//             }

//             iter->_id += id_offset;
//             iter->_state = 1;
//             iter->_pos = 0;
//             break;
//         }
//         case 1:
//         {
//             int pos_offset = rsbuf_read_unsigned_short_smart(buffer);
//             if( pos_offset == 0 )
//             {
//                 iter->_state = 0;
//                 break;
//             }

//             iter->_pos += pos_offset - 1;
//             int position = 0;
//             position = iter->_pos;

//             int local_y = position & 0x3F;
//             int local_x = (position >> 6) & 0x3F;
//             int height = (position >> 12) & 0x3;

//             int attributes = rsbuf_g1(buffer);
//             int shape_select = attributes >> 2;
//             int orientation = attributes & 0x3;

//             iter->value.loc_id = iter->_id;
//             iter->value.shape_select = shape_select;
//             iter->value.orientation = orientation;
//             iter->value.chunk_pos_x = local_x;
//             iter->value.chunk_pos_y = local_y;
//             iter->value.chunk_pos_level = height;

//             iter->is_populated = true;
//             return &iter->value;
//         }
//         break;
//         }
//     }

//     return NULL;
// }