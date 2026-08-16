#include "mapsquares.h"

#include "../rsbuffer.h"

#include <stdlib.h>
#include <string.h>

struct RSCache_MapSquares*
RSCache_MapSquaresNewDecode(
    char* data,
    int data_size)
{
    struct RSCache_MapSquares* map_squares = malloc(sizeof(struct RSCache_MapSquares));
    if( !map_squares )
        return NULL;
    memset(map_squares, 0, sizeof(struct RSCache_MapSquares));

    int count = data_size / 7;
    map_squares->squares = malloc(count * sizeof(struct RSCache_MapSquare));
    if( !map_squares->squares )
    {
        free(map_squares);
        return NULL;
    }
    memset(map_squares->squares, 0, count * sizeof(struct RSCache_MapSquare));

    struct RSCache_Buffer buffer = { .data = (uint8_t*)data, .size = (uint32_t)data_size, .position = 0 };
    for( int i = 0; i < count; i++ )
    {
        map_squares->squares[i].map_id = g2(&buffer);
        map_squares->squares[i].terrain_archive_id = g2(&buffer);
        map_squares->squares[i].loc_archive_id = g2(&buffer);
        map_squares->squares[i].members = g1(&buffer) == 1;
    }

    map_squares->squares_count = count;
    return map_squares;
}

void
RSCache_MapSquaresFree(struct RSCache_MapSquares* map_squares)
{
    if( !map_squares )
        return;
    free(map_squares->squares);
    free(map_squares);
}

int
RSCache_MapSquareId(
    int map_x,
    int map_z)
{
    return (map_x << 8) + map_z;
}

void
RSCache_MapSquareCoord(
    struct RSCache_MapSquareCoord* coord,
    int map_square_id)
{
    coord->map_x = map_square_id >> 8;
    coord->map_z = map_square_id & 0xFF;
}
