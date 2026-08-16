#ifndef RSCACHE_MAPSQUARES_H
#define RSCACHE_MAPSQUARES_H

#include <stdbool.h>

struct RSCache_MapSquare
{
    int map_id;
    int terrain_archive_id;
    int loc_archive_id;
    bool members;
};

struct RSCache_MapSquares
{
    struct RSCache_MapSquare* squares;
    int squares_count;
};

struct RSCache_MapSquares*
RSCache_MapSquaresNewDecode(
    char* data,
    int data_size);

void
RSCache_MapSquaresFree(struct RSCache_MapSquares* map_squares);

struct RSCache_MapSquareCoord
{
    int map_x;
    int map_z;
};

int
RSCache_MapSquareId(
    int map_x,
    int map_z);

void
RSCache_MapSquareCoord(
    struct RSCache_MapSquareCoord* coord,
    int map_square_id);

#endif
