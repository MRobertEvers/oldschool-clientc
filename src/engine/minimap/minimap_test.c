#include "minimap.h"

#include "bmp.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
    enum
    {
        MAP_W = 16,
        MAP_H = 16,
    };

    struct Minimap* mm = minimap_new(MAP_W, MAP_H);
    assert(mm);

    /* Green grass wash. */
    for( int sx = 0; sx < MAP_W; sx++ )
    {
        for( int sz = 0; sz < MAP_H; sz++ )
            minimap_set_tile_color(mm, sx, sz, 0xFF3A7A2Au, MINIMAP_BACKGROUND);
    }

    /* Path strip through the middle. */
    for( int sx = 0; sx < MAP_W; sx++ )
        minimap_set_tile_color(mm, sx, 7, 0xFFC2A36Au, MINIMAP_BACKGROUND);

    /* Water pond with overlay shape. */
    for( int sx = 3; sx <= 6; sx++ )
    {
        for( int sz = 3; sz <= 6; sz++ )
        {
            minimap_set_tile_color(mm, sx, sz, 0xFF2A5A7Au, MINIMAP_BACKGROUND);
            minimap_set_tile_color(mm, sx, sz, 0xFF4A8ABAu, MINIMAP_FOREGROUND);
            minimap_set_tile_shape(mm, sx, sz, 2, (sx + sz) & 3);
        }
    }

    /* Walled building. */
    for( int sx = 10; sx <= 13; sx++ )
    {
        for( int sz = 10; sz <= 13; sz++ )
            minimap_set_tile_color(mm, sx, sz, 0xFF6B5A4Au, MINIMAP_BACKGROUND);
    }
    for( int sx = 10; sx <= 13; sx++ )
    {
        minimap_add_tile_wall(mm, sx, 10, MINIMAP_WALL_SOUTH);
        minimap_add_tile_wall(mm, sx, 13, MINIMAP_WALL_NORTH);
    }
    for( int sz = 10; sz <= 13; sz++ )
    {
        minimap_add_tile_wall(mm, 10, sz, MINIMAP_WALL_WEST);
        minimap_add_tile_wall(mm, 13, sz, MINIMAP_WALL_EAST);
    }

    /* Diagonal wall accent. */
    minimap_add_tile_wall(mm, 8, 8, MINIMAP_WALL_NORTHWEST_SOUTHEAST);

    struct ToriRS_Sprite* sprite = minimap_render_to_sprite(mm);
    assert(sprite);
    assert(sprite->frame_count == 1);
    assert(sprite->frames);
    assert(sprite->frames[0].pixels_argb);
    assert(sprite->frames[0].width == MAP_W * 4);
    assert(sprite->frames[0].height == MAP_H * 4);

    const char* path = "build/minimap_test.bmp";
    bmp_write_file(
        path,
        (int*)sprite->frames[0].pixels_argb,
        sprite->frames[0].width,
        sprite->frames[0].height);

    printf("wrote %s (%dx%d)\n", path, sprite->frames[0].width, sprite->frames[0].height);

    ToriRS_SpriteFree(sprite);
    minimap_free(mm);
    return 0;
}
