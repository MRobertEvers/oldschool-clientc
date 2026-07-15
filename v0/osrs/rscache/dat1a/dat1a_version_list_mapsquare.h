#ifndef RSCACHE_RSCACHEDAT1A_VERSIONLISTMAPSQUARE_H
#define RSCACHE_RSCACHEDAT1A_VERSIONLISTMAPSQUARE_H

#include "../dat1disk/dat1disk.h"
#include "dat1a_configs_dat.h"

#include <stdbool.h>

// const mapId = buffer.readUnsignedShort();
// const terrainArchiveId = buffer.readUnsignedShort();
// const locArchiveId = buffer.readUnsignedShort();
// const members = buffer.readUnsignedByte() === 1;

/**
 * Found buffered in the "map_index" file of the VERSION_LIST archive
 * on the RSCacheDat1Disk_Table_Configs table.
 */
struct RSCacheDat1A_MapSquare
{
    int map_id;
    int terrain_archive_id;
    int loc_archive_id;
    bool members;
};

struct RSCacheDat1A_MapSquares
{
    struct RSCacheDat1A_MapSquare* squares;
    int squares_count;
};

struct RSCacheDat1A_MapSquares*
RSCacheDat1A_MapSquaresNewDecode(
    char* data,
    int data_size);
void
RSCacheDat1A_MapSquaresFree(struct RSCacheDat1A_MapSquares* map_squares);

struct RSCacheDat1A_MapSquareCoord
{
    int map_x;
    int map_z;
};

int
RSCacheDat1A_MapSquareId(
    int map_x,
    int map_z);

void
RSCacheDat1A_MapSquareCoord(
    struct RSCacheDat1A_MapSquareCoord* coord,
    int map_square_id);
#endif