#include "lightmap.h"

#include "heightmap.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LIGHT_DIR_X -50
#define LIGHT_DIR_Y -10
#define LIGHT_DIR_Z -50
#define LIGHT_AMBIENT 96
#define LIGHT_ATTENUATION 768
#define HEIGHT_SCALE 65536

static inline int
lightmap_coord_idx(
    struct Lightmap* lightmap,
    int x,
    int z,
    int level)
{
    assert(x >= 0 && x < lightmap->width);
    assert(z >= 0 && z < lightmap->height);
    assert(level >= 0 && level < lightmap->levels);
    return x + z * lightmap->width + level * lightmap->width * lightmap->height;
}

struct Lightmap*
lightmap_new(
    int width,
    int height,
    int levels)
{
    struct Lightmap* lightmap = malloc(sizeof(struct Lightmap));
    assert(lightmap);
    lightmap->lights = malloc((size_t)width * (size_t)height * (size_t)levels);
    assert(lightmap->lights);
    memset(lightmap->lights, 0, (size_t)width * (size_t)height * (size_t)levels);
    lightmap->width = width;
    lightmap->height = height;
    lightmap->levels = levels;
    return lightmap;
}

void
lightmap_free(struct Lightmap* lightmap)
{
    if( !lightmap )
        return;
    free(lightmap->lights);
    free(lightmap);
}

int
lightmap_get(
    struct Lightmap* lightmap,
    int x,
    int z,
    int level)
{
    return lightmap->lights[lightmap_coord_idx(lightmap, x, z, level)];
}

void
lightmap_set(
    struct Lightmap* lightmap,
    int x,
    int z,
    int level,
    uint8_t light)
{
    lightmap->lights[lightmap_coord_idx(lightmap, x, z, level)] = light;
}

void
lightmap_build(
    struct Lightmap* lightmap,
    struct Heightmap* heightmap)
{
    int magnitude =
        (int)sqrt((double)(LIGHT_DIR_X * LIGHT_DIR_X + LIGHT_DIR_Y * LIGHT_DIR_Y +
                           LIGHT_DIR_Z * LIGHT_DIR_Z));
    int intensity = (magnitude * LIGHT_ATTENUATION) >> 8;

    for( int level = 0; level < lightmap->levels; level++ )
    {
        for( int z = 1; z < lightmap->height - 1; z++ )
        {
            for( int x = 1; x < lightmap->width - 1; x++ )
            {
                int height_delta_x = heightmap_get(heightmap, x + 1, z, level) -
                                     heightmap_get(heightmap, x - 1, z, level);
                int height_delta_z = heightmap_get(heightmap, x, z + 1, level) -
                                     heightmap_get(heightmap, x, z - 1, level);

                int tile_normal_length = (int)sqrt(
                    (double)(height_delta_z * height_delta_z + height_delta_x * height_delta_x +
                             HEIGHT_SCALE));

                int normalized_tile_normal_x = (height_delta_x << 8) / tile_normal_length;
                int normalized_tile_normal_y = HEIGHT_SCALE / tile_normal_length;
                int normalized_tile_normal_z = (height_delta_z << 8) / tile_normal_length;

                int dot = normalized_tile_normal_x * LIGHT_DIR_X +
                          normalized_tile_normal_y * LIGHT_DIR_Y +
                          normalized_tile_normal_z * LIGHT_DIR_Z;
                int sunlight = (dot / intensity + LIGHT_AMBIENT);
                if( sunlight < 0 )
                    sunlight = 0;
                else if( sunlight > 255 )
                    sunlight = 255;

                lightmap_set(lightmap, x, z, level, (uint8_t)sunlight);
            }
        }
    }
}
