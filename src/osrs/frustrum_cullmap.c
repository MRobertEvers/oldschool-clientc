#include "frustrum_cullmap.h"

#include "projection.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PITCH_STEPS 16
#define YAW_STEPS 32

extern int g_sin_table[2048];
extern int g_cos_table[2048];

static inline int
coord_for_grid(int x, int y, int radius, int pitch, int yaw)
{
    int gridsize = (radius * radius) << 2;
    int pitchyaw_offset = (gridsize) * ((yaw * PITCH_STEPS) + pitch);
    return pitchyaw_offset + (x + y * (radius << 1));
}

static inline void
cullmap_set(struct FrustrumCullmap* frustrum_cullmap, int x, int y, int pitch, int yaw, int visible)
{
    int index = coord_for_grid(x, y, frustrum_cullmap->radius, pitch, yaw);

    frustrum_cullmap->cullmap[index] = visible;
}

static inline int
cullmap_get(struct FrustrumCullmap* frustrum_cullmap, int x, int y, int pitch, int yaw)
{
    int index = coord_for_grid(x, y, frustrum_cullmap->radius, pitch, yaw);

    return frustrum_cullmap->cullmap[index];
}

int
frustrum_cullmap_get(struct FrustrumCullmap* frustrum_cullmap, int x, int y, int pitch, int yaw)
{
    pitch = pitch / (128 / 4);
    yaw = yaw / (2048 / YAW_STEPS);

    if( pitch < 0 )
        pitch = 0;
    if( pitch >= PITCH_STEPS )
        pitch = PITCH_STEPS - 1;
    if( yaw < 0 )
        yaw = 0;
    if( yaw >= YAW_STEPS )
        yaw = YAW_STEPS - 1;

    if( x <= -frustrum_cullmap->radius || y <= -frustrum_cullmap->radius ||
        x >= frustrum_cullmap->radius || y >= frustrum_cullmap->radius )
        return 0;

    x += frustrum_cullmap->radius;
    y += frustrum_cullmap->radius;

    int index = coord_for_grid(x, y, frustrum_cullmap->radius, pitch, yaw);
    return frustrum_cullmap->cullmap[index];
}

static int64_t
pitch_height(int pitch)
{
    // Multiply by 32 to get [0, 16] => [0, 512]
    int angle = pitch * 32 + 15;
    int offset = 600;
    int sin = g_sin_table[angle];
    return offset * sin >> 16;
}

static bool
test_point_in_frustrum(int x, int z, int y, int pitch, int yaw)
{
    struct ProjectedTriangle projected_triangle =
        project(0, 0, 0, 0, 0, 0, x, y, z, pitch, yaw, 0, 512, 100, 1024, 768);

    if( projected_triangle.clipped )
        return false;

    projected_triangle.x = (projected_triangle.x + (1024 >> 1));
    projected_triangle.y = (projected_triangle.y + (768 >> 1));

    return projected_triangle.x >= 0 && projected_triangle.x <= 1024 && projected_triangle.y >= 0 &&
           projected_triangle.y <= 768;
    // int px = (z * g_sin_table[yaw] + x * g_cos_table[yaw]) >> 16;
    // int tmp = (z * g_cos_table[yaw] - x * g_sin_table[yaw]) >> 16;
    // int pz = (y * g_sin_table[pitch] + tmp * g_cos_table[pitch]) >> 16;
    // int py = (y * g_cos_table[pitch] - tmp * g_sin_table[pitch]) >> 16;
    // if( pz < 50 || pz > 3500 )
    // {
    //     return false;
    // }

    // int viewportX = (1024 >> 1) + (px << 9) / pz;
    // int viewportY = (768 >> 1) + (py << 9) / pz;
    // return viewportX >= 0 && viewportX <= 1024 && viewportY >= 0 && viewportY <= 768;
}

struct FrustrumCullmap*
frustrum_cullmap_new(int radius, int fov_multiplier)
{
    struct FrustrumCullmap* frustrum_cullmap =
        (struct FrustrumCullmap*)malloc(sizeof(struct FrustrumCullmap));
    frustrum_cullmap->cullmap =
        (int*)malloc(((radius * radius) << 2) * sizeof(int) * PITCH_STEPS * YAW_STEPS);
    frustrum_cullmap->radius = radius;
    frustrum_cullmap->fov_multiplier = fov_multiplier;

    // 16 quadrants
    for( int yaw = 0; yaw < YAW_STEPS; yaw++ )
    {
        for( int pitch = 0; pitch < PITCH_STEPS; pitch++ )
        {
            // The camera is at the center of the grid.
            // Convert yaw to radians (pitch unused for 2D culling)
            int yaw_rad = yaw * (2048 / YAW_STEPS);
            int pitch_rad = pitch * (512 / PITCH_STEPS);

            // For each tile in the radius
            for( int tile_x = 0; tile_x < radius * 2; tile_x++ )
            {
                for( int tile_z = 0; tile_z < radius * 2; tile_z++ )
                {
                    // Camera at radius,radius
                    int to_tile_x = tile_x - radius;
                    int to_tile_z = tile_z - radius;
                    int visible = 0;

                    // If the tile is visible from any height within the frustrum, it's visible.
                    for( int fr = -500; fr < 1500; fr += 128 )
                    {
                        int to_tile_y = pitch_height(pitch) + fr;

                        visible = test_point_in_frustrum(
                            to_tile_x * 128, to_tile_z * 128, to_tile_y, pitch_rad, yaw_rad);
                        if( visible )
                            break;
                    }

                    cullmap_set(frustrum_cullmap, tile_x, tile_z, pitch, yaw, visible ? 1 : 0);
                }
            }
        }
    }

    for( int yaw = 0; yaw < YAW_STEPS; yaw++ )
    {
        for( int pitch = 0; pitch < PITCH_STEPS; pitch++ )
        {
            // For each tile in the radius
            for( int tile_x = 0; tile_x < radius * 2; tile_x++ )
            {
                for( int tile_z = 0; tile_z < radius * 2; tile_z++ )
                {
                    int self = cullmap_get(frustrum_cullmap, tile_x, tile_z, pitch, yaw);
                    int visible = 0;
                    int blend_radius = 3;

                    for( int blend_range = -blend_radius; blend_range <= blend_radius;
                         blend_range++ )
                    {
                        if( blend_range == 0 )
                            continue;
                        int test_yaw = (yaw + blend_range + YAW_STEPS) % YAW_STEPS;

                        int test_visible =
                            cullmap_get(frustrum_cullmap, tile_x, tile_z, pitch, test_yaw);
                        visible |= (test_visible & 1) != 0;
                    }

                    cullmap_set(
                        frustrum_cullmap, tile_x, tile_z, pitch, yaw, visible ? (self | 2) : 0);
                }
            }
        }
    }

    // for( int yaw = 0; yaw < YAW_STEPS; yaw++ )
    // {
    //     for( int pitch = 0; pitch < PITCH_STEPS; pitch++ )
    //     {
    //         // For each tile in the radius
    //         for( int tile_x = 0; tile_x < radius * 2; tile_x++ )
    //         {
    //             for( int tile_z = 0; tile_z < radius * 2; tile_z++ )
    //             {
    //                 int visible = cullmap_get(frustrum_cullmap, tile_x, tile_z, pitch, yaw);
    //                 cullmap_set(frustrum_cullmap, tile_x, tile_z, pitch, yaw, visible != 0);
    //             }
    //         }
    //     }
    // }

    return frustrum_cullmap;
}

void
frustrum_cullmap_free(struct FrustrumCullmap* frustrum_cullmap)
{
    if( frustrum_cullmap )
    {
        if( frustrum_cullmap->cullmap )
        {
            free(frustrum_cullmap->cullmap);
        }
        free(frustrum_cullmap);
    }
}