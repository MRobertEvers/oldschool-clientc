#include "frustrum_cullmap.h"

#include <math.h>
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
    pitch = pitch / 128;
    yaw = yaw / 64;

    if( x <= -frustrum_cullmap->radius || y <= -frustrum_cullmap->radius ||
        x >= frustrum_cullmap->radius || y >= frustrum_cullmap->radius )
        return 0;

    x += frustrum_cullmap->radius;
    y += frustrum_cullmap->radius;

    int index = coord_for_grid(x, y, frustrum_cullmap->radius, pitch, yaw);
    return frustrum_cullmap->cullmap[index];
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

            // Get direction vector from camera angles (2D horizontal only)
            // Match the yaw convention used in projection.c (clockwise rotation)
            int dir_x = -g_sin_table[yaw_rad];
            int dir_z = g_cos_table[yaw_rad];

            // For each tile in the radius
            for( int tile_x = 0; tile_x < radius * 2; tile_x++ )
            {
                for( int tile_z = 0; tile_z < radius * 2; tile_z++ )
                {
                    // Get vector from camera (at center) to tile
                    int to_tile_x = tile_x - radius; // Camera at radius,radius
                    int to_tile_z = tile_z - radius;

                    // Skip center tile (camera position)
                    if( to_tile_x == 0 && to_tile_z == 0 )
                    {
                        cullmap_set(
                            frustrum_cullmap, tile_x, tile_z, pitch, yaw, 1); // Always visible
                        continue;
                    }

                    // Project tile vector onto view direction (2D dot product)
                    int dot = (to_tile_x * dir_x + to_tile_z * dir_z) >> 16;

                    // Tile is visible if it's in front of camera (dot > 0)
                    // and within view angle (cross product < threshold)
                    int cross = (to_tile_x * dir_z - to_tile_z * dir_x) >> 16;
                    int fov_threshold = (abs(dot) * frustrum_cullmap->fov_multiplier) >> 16;
                    int visible = (dot > 0) && (abs(cross) < fov_threshold);

                    // Store visibility result for this yaw/pitch combination
                    cullmap_set(frustrum_cullmap, tile_x, tile_z, pitch, yaw, visible ? 1 : 0);
                }
            }
        }
    }

    for( int yaw = 0; yaw < YAW_STEPS; yaw++ )
    {
        for( int pitch = 0; pitch < PITCH_STEPS; pitch++ )
        {
            int forward_yaw = (yaw + 1) % YAW_STEPS;
            int backward_yaw = (yaw - 1 + YAW_STEPS) % YAW_STEPS;

            // For each tile in the radius
            for( int tile_x = 0; tile_x < radius * 2; tile_x++ )
            {
                for( int tile_z = 0; tile_z < radius * 2; tile_z++ )
                {
                    int forward_visible =
                        cullmap_get(frustrum_cullmap, tile_x, tile_z, pitch, forward_yaw);
                    int backward_visible =
                        cullmap_get(frustrum_cullmap, tile_x, tile_z, pitch, backward_yaw);
                    int center_visible = cullmap_get(frustrum_cullmap, tile_x, tile_z, pitch, yaw);
                    int visible =
                        (forward_visible & 1) | (backward_visible & 1) | (center_visible & 1);

                    cullmap_set(frustrum_cullmap, tile_x, tile_z, pitch, yaw, visible << 1);
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
                    int visible = cullmap_get(frustrum_cullmap, tile_x, tile_z, pitch, yaw);
                    cullmap_set(frustrum_cullmap, tile_x, tile_z, pitch, yaw, visible != 0);
                }
            }
        }
    }

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