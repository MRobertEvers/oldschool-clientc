#include "config_versionlist_mapsquare.h"

#include "../rsbuf.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CacheMapSquares*
cache_map_squares_new_decode(
    char* data,
    int data_size)
{
    struct CacheMapSquares* map_squares = malloc(sizeof(struct CacheMapSquares));
    if( !map_squares )
        return NULL;
    memset(map_squares, 0, sizeof(struct CacheMapSquares));

    int count = data_size / 7;
    map_squares->squares = malloc(count * sizeof(struct CacheMapSquare));
    if( !map_squares->squares )
        return NULL;

    memset(map_squares->squares, 0, count * sizeof(struct CacheMapSquare));

    struct RSBuffer buffer = { .data = data, .size = data_size, .position = 0 };
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
cache_map_squares_free(struct CacheMapSquares* map_squares)
{
    if( !map_squares )
        return;
    free(map_squares->squares);
    free(map_squares);
}

int
cache_map_square_id(
    int map_x,
    int map_z)
{
    return (map_x << 8) + map_z;
}

void
cache_map_square_coord(
    struct MapSquareCoord* coord,
    int map_square_id)
{
    coord->map_x = map_square_id >> 8;
    coord->map_z = map_square_id & 0xFF;
}