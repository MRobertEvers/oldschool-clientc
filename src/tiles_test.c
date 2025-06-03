#include "osrs/scene_tile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main()
{
    int tile_count;
    struct SceneTile* tiles = parse_tiles_data("../docs/tiles_data.bin", &tile_count);

    for( int i = 0; i < tile_count; i++ )
    {
        printf(
            "Tile %d: count %d, x:%d, y:%d, z:%d\n",
            i,
            tiles[i].vertex_count,
            tiles[i].vertex_x[0],
            tiles[i].vertex_y[0],
            tiles[i].vertex_z[0]);
    }

    free_tiles(tiles, tile_count);
    return 0;
}