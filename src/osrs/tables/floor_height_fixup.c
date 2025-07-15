
#include "floor_height_fixup.h"

extern int g_cos_table[2048];

static int
interpolate(int i, int i_4_, int i_5_, int freq)
{
    int i_8_ = (65536 - g_cos_table[(i_5_ * 1024) / freq]) >> 1;
    return ((i_8_ * i_4_) >> 16) + (((65536 - i_8_) * i) >> 16);
}

static int
noise(int x, int y)
{
    int n = y * 57 + x;
    n = (n << 13) ^ n;
    int n2 = (n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff;
    return (n2 >> 19) & 0xff;
}

static int
smoothed_noise(int x, int y)
{
    int corners =
        noise(x - 1, y - 1) + noise(x + 1, y - 1) + noise(x - 1, y + 1) + noise(x + 1, y + 1);
    int sides = noise(x - 1, y) + noise(x + 1, y) + noise(x, y - 1) + noise(x, y + 1);
    int center = noise(x, y);
    return (center / 4) + (sides / 8) + (corners / 16);
}

static int
interpolate_noise(int x, int y, int freq)
{
    int intX = x / freq;
    int fracX = x & (freq - 1);
    int intY = y / freq;
    int fracY = y & (freq - 1);
    int v1 = smoothed_noise(intX, intY);
    int v2 = smoothed_noise(intX + 1, intY);
    int v3 = smoothed_noise(intX, intY + 1);
    int v4 = smoothed_noise(intX + 1, intY + 1);
    int i1 = interpolate(v1, v2, fracX, freq);
    int i2 = interpolate(v3, v4, fracX, freq);
    return interpolate(i1, i2, fracY, freq);
}

static int
calculate_height(int x, int y)
{
    int n = interpolate_noise(x + 45365, y + 91923, 4) - 128 +
            ((interpolate_noise(x + 10294, y + 37821, 2) - 128) >> 1) +
            ((interpolate_noise(x, y, 1) - 128) >> 2);
    n = (int)(0.3 * n) + 35;
    if( n < 10 )
    {
        n = 10;
    }
    else if( n > 60 )
    {
        n = 60;
    }
    return n;
}

/**
 * Normally, some of this calculation is done in the map terrain loader.
 *
 * The deob meteor client and rs map viewer do this calculation there.
 * src/rs/scene/SceneBuilder.ts decodeTerrainTile
 */
void
fixup_terrain_tile(
    struct CacheMapTerrain* map_terrain, int world_scene_origin_x, int world_scene_origin_y)
{
    for( int z = 0; z < MAP_TERRAIN_Z; z++ )
    {
        for( int y = 0; y < MAP_TERRAIN_Y - 1; y++ )
        {
            for( int x = 0; x < MAP_TERRAIN_X - 1; x++ )
            {
                struct CacheMapFloor* map = &map_terrain->tiles_xyz[MAP_TILE_COORD(x, y, z)];

                if( map->height == 0 )
                {
                    if( z == 0 )
                    {
                        int world_x = world_scene_origin_x + (-58) + 932731;
                        int world_y = world_scene_origin_y + (-58) + 556238;
                        map->height =
                            -calculate_height(world_x, world_y) * MAP_UNITS_TILE_HEIGHT_BASIS;
                    }
                    else
                    {
                        int lower = map_terrain->tiles_xyz[MAP_TILE_COORD(x, y, z - 1)].height;
                        map->height = lower - MAP_UNITS_LEVEL_HEIGHT;
                    }
                }
                else
                {
                    if( map->height == 1 )
                        map->height = 0;

                    if( z == 0 )
                    {
                        map->height = -map->height * MAP_UNITS_TILE_HEIGHT_BASIS;
                    }
                    else
                    {
                        map->height = map_terrain->tiles_xyz[MAP_TILE_COORD(x, y, z - 1)].height -
                                      map->height * MAP_UNITS_TILE_HEIGHT_BASIS;
                    }
                }
            }
        }
    }
}
