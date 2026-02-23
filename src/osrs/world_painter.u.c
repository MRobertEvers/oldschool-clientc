#ifndef WORLD_PAINTER_U_C
#define WORLD_PAINTER_U_C

#include "osrs/world.h"

struct PainterPadding
{
    int x_sw;
    int z_sw;
    int x_size;
    int z_size;
};
static void
entity_calculate_painter_padding(
    struct World* world,
    struct EntityDrawPosition* draw_position,
    int draw_padding,
    struct PainterPadding* size)
{
    int x0 = draw_position->x - draw_padding;
    int z0 = draw_position->z - draw_padding;
    int x1 = draw_position->x + draw_padding;
    int z1 = draw_position->z + draw_padding;

    // if( forwardPadding )
    // {
    //     if( yaw > 640 && yaw < 1408 )
    //     {
    //         z1 += 128;
    //     }
    //     if( yaw > 1152 && yaw < 1920 )
    //     {
    //         x1 += 128;
    //     }
    //     if( yaw > 1664 || yaw < 384 )
    //     {
    //         z0 -= 128;
    //     }
    //     if( yaw > 128 && yaw < 896 )
    //     {
    //         x0 -= 128;
    //     }
    // }

    x0 = (x0 / 128) | 0;
    z0 = (z0 / 128) | 0;
    x1 = (x1 / 128) | 0;
    z1 = (z1 / 128) | 0;

    size->x_sw = x0;
    size->z_sw = z0;
    size->x_size = x1 - x0 + 1;
    size->z_size = z1 - z0 + 1;
}

#endif