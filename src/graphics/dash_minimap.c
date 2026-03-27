#include "dash_minimap.h"

#include "dash.h"

void
dash_minimap_raster_tile_commands(
    struct Minimap* minimap,
    int level,
    struct MinimapRenderCommandBuffer* tile_commands,
    int sw_x,
    int sw_z,
    int ne_x,
    int ne_z,
    int* dst_pixels,
    int dst_stride,
    int dst_width,
    int dst_height)
{
    if( !minimap || !tile_commands || !dst_pixels || dst_stride <= 0 || dst_width <= 0 ||
        dst_height <= 0 )
        return;

    for( int i = 0; i < tile_commands->count; i++ )
    {
        struct MinimapRenderCommand* command = &tile_commands->commands[i];
        if( command->kind != MINIMAP_RENDER_COMMAND_TILE )
            continue;

        int shape = minimap_tile_shape(
            minimap, command->_tile.tile_sx, command->_tile.tile_sz, level);
        int angle = minimap_tile_rotation(
            minimap, command->_tile.tile_sx, command->_tile.tile_sz, level);
        int rgb_background = minimap_tile_rgb(
            minimap,
            command->_tile.tile_sx,
            command->_tile.tile_sz,
            level,
            MINIMAP_BACKGROUND);
        int rgb_foreground = minimap_tile_rgb(
            minimap,
            command->_tile.tile_sx,
            command->_tile.tile_sz,
            level,
            MINIMAP_FOREGROUND);
        if( rgb_foreground == 0 && rgb_background == 0 )
            continue;

        int tile_x = (command->_tile.tile_sx - sw_x) * 4;
        int tile_y = (ne_z - command->_tile.tile_sz) * 4;
        dash2d_fill_minimap_tile(
            dst_pixels,
            dst_stride,
            tile_x,
            tile_y,
            rgb_background,
            rgb_foreground,
            angle,
            shape,
            dst_width,
            dst_height);

        int wall = minimap_tile_wall(
            minimap, command->_tile.tile_sx, command->_tile.tile_sz, level);
        if( wall != 0 )
        {
            dash2d_draw_minimap_wall(
                dst_pixels,
                dst_stride,
                tile_x,
                tile_y,
                wall,
                dst_width,
                dst_height);
        }
    }
}
