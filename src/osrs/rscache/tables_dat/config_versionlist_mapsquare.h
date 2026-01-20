#ifndef CONFIG_VERSIONLIST_MAPSQUARE_H
#define CONFIG_VERSIONLIST_MAPSQUARE_H

#include "../cache_dat.h"
#include "configs_dat.h"

#include <stdbool.h>

// const mapId = buffer.readUnsignedShort();
// const terrainArchiveId = buffer.readUnsignedShort();
// const locArchiveId = buffer.readUnsignedShort();
// const members = buffer.readUnsignedByte() === 1;

/**
 * Found buffered in the "map_index" file of the VERSION_LIST archive
 * on the CACHE_DAT_CONFIGS table.
 */
struct CacheMapSquare
{
    int map_id;
    int terrain_archive_id;
    int loc_archive_id;
    bool members;
};

struct CacheMapSquares
{
    struct CacheMapSquare* squares;
    int squares_count;
};

struct CacheMapSquares*
cache_map_squares_new_decode(
    char* data,
    int data_size);
void
cache_map_squares_free(struct CacheMapSquares* map_squares);

struct MapSquareCoord
{
    int map_x;
    int map_z;
};

int
cache_map_square_id(
    int map_x,
    int map_z);

void
cache_map_square_coord(
    struct MapSquareCoord* coord,
    int map_square_id);
#endif