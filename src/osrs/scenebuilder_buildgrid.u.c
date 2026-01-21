#ifndef SCENEBUILDER_BUILDGRID_U_C
#define SCENEBUILDER_BUILDGRID_U_C

#include "rscache/tables/config_locs.h"
#include "rscache/tables/maps.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

// clang-format off
#include "terrain_grid.u.c"
// clang-format on

#define BUILDGRID_WALL_A 0
#define BUILDGRID_WALL_B 1

enum DecorDisplacementKind
{
    DECOR_DISPLACEMENT_KIND_NONE = 0,
    DECOR_DISPLACEMENT_KIND_STRAIGHT,
    DECOR_DISPLACEMENT_KIND_DIAGONAL,
    DECOR_DISPLACEMENT_KIND_STRAIGHT_ONWALL_OFFSET,
    DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET,
};

struct BuildElement
{
    struct DashModelNormals* aliased_lighting_normals;
    enum DecorDisplacementKind decor_displacement_kind;
    // Named differently from orientation because this is not the exact value
    // stored in the map config.
    int rotation;

    /**
     * From config_loc.
     */
    bool sharelight;
    uint32_t light_ambient;
    uint32_t light_attenuation;
    uint8_t wall_offset;
    uint8_t size_x;
    uint8_t size_z;

    uint16_t sx;
    uint16_t sz;
    uint8_t slevel;

    // Debug
    struct CacheConfigLocation* __config_loc;
};

struct BuildTile
{
    int elements[10];
    int elements_length;

    // These are also included in the elements array.
    int wall_element_a_idx;
    int wall_element_b_idx;

    uint8_t sx;
    uint8_t sz;
    uint8_t slevel;
    uint16_t height_center;
};

struct BuildGrid
{
    struct BuildTile* tiles;
    int tile_count;

    struct BuildElement* elements;
    int elements_length;
    int elements_capacity;

    int tile_width_x;
    int tile_width_z;
};

static void
init_build_element(struct BuildElement* build_element)
{
    memset(build_element, 0, sizeof(struct BuildElement));
    build_element->aliased_lighting_normals = NULL;
    build_element->sharelight = false;
    build_element->light_ambient = 0;
    build_element->light_attenuation = 0;
    build_element->wall_offset = 0;
    build_element->size_x = 0;
    build_element->size_z = 0;
}

static void
init_build_tile(
    struct BuildTile* build_tile,
    int sx,
    int sz,
    int slevel,
    int height_center)
{
    memset(build_tile, 0, sizeof(struct BuildTile));
    build_tile->wall_element_a_idx = -1;
    build_tile->wall_element_b_idx = -1;

    build_tile->sx = sx;
    build_tile->sz = sz;
    build_tile->slevel = slevel;
    build_tile->height_center = height_center;
}

static int
build_grid_index(
    int tile_width_x,
    int tile_width_z,
    int sx,
    int sz,
    int slevel)
{
    return sx + sz * tile_width_x + slevel * tile_width_x * tile_width_z;
}

static struct BuildGrid*
build_grid_new(
    int tile_width_x,
    int tile_width_z)
{
    struct BuildGrid* build_grid = malloc(sizeof(struct BuildGrid));
    memset(build_grid, 0, sizeof(struct BuildGrid));

    int element_count = tile_width_x * tile_width_z * MAP_TERRAIN_LEVELS;
    build_grid->elements = malloc(element_count * sizeof(struct BuildElement));
    memset(build_grid->elements, 0, element_count * sizeof(struct BuildElement));
    build_grid->elements_capacity = element_count;
    build_grid->elements_length = 0;

    for( int i = 0; i < element_count; i++ )
        init_build_element(&build_grid->elements[i]);

    int tile_count = tile_width_x * tile_width_z * MAP_TERRAIN_LEVELS;
    build_grid->tiles = malloc(tile_count * sizeof(struct BuildTile));
    memset(build_grid->tiles, 0, tile_count * sizeof(struct BuildTile));
    build_grid->tile_count = tile_count;

    for( int sx = 0; sx < tile_width_x; sx++ )
    {
        for( int sz = 0; sz < tile_width_z; sz++ )
        {
            for( int slevel = 0; slevel < MAP_TERRAIN_LEVELS; slevel++ )
            {
                int index = build_grid_index(tile_width_x, tile_width_z, sx, sz, slevel);
                init_build_tile(&build_grid->tiles[index], sx, sz, slevel, 0);
            }
        }
    }

    build_grid->tile_width_x = tile_width_x;
    build_grid->tile_width_z = tile_width_z;

    return build_grid;
}

static void
build_grid_free(struct BuildGrid* build_grid)
{
    free(build_grid->elements);
    free(build_grid);
}

static struct BuildElement*
build_grid_element_at(
    struct BuildGrid* build_grid,
    int element)
{
    assert(element >= 0 && element < build_grid->elements_capacity);
    return &build_grid->elements[element];
}

static struct BuildTile*
build_grid_tile_at(
    struct BuildGrid* build_grid,
    int sx,
    int sz,
    int slevel)
{
    int index =
        build_grid_index(build_grid->tile_width_x, build_grid->tile_width_z, sx, sz, slevel);

    return &build_grid->tiles[index];
}

static void
build_grid_tile_push_element(
    struct BuildGrid* build_grid,
    int sx,
    int sz,
    int slevel,
    int element_idx)
{
    struct BuildTile* tile = build_grid_tile_at(build_grid, sx, sz, slevel);
    assert(tile->elements_length < 10);
    tile->elements[tile->elements_length] = element_idx;
    tile->elements_length++;
}

static void
build_grid_set_element(
    struct BuildGrid* build_grid,
    int element_idx,
    struct CacheConfigLocation* config_loc,
    struct TerrainGridOffsetFromSW* offset,
    int orientation,
    int size_x,
    int size_z)
{
    struct BuildElement* build_element = build_grid_element_at(build_grid, element_idx);

    build_element->size_x = size_x;
    build_element->size_z = size_z;
    build_element->wall_offset = config_loc->wall_width;
    build_element->sharelight = config_loc->sharelight;
    build_element->rotation = orientation;
    build_element->light_ambient = config_loc->ambient;
    // Old Revisions multiply the contract by 5 instead of 25
    build_element->light_attenuation = config_loc->contrast;
    build_element->aliased_lighting_normals = NULL;

    build_element->sx = offset->x;
    build_element->sz = offset->z;
    build_element->slevel = offset->level;

    build_element->__config_loc = config_loc;
}

static void
build_grid_set_decor(
    struct BuildGrid* build_grid,
    int element_idx,
    enum DecorDisplacementKind decor_displacement_kind)
{
    struct BuildElement* build_element = build_grid_element_at(build_grid, element_idx);
    build_element->decor_displacement_kind = decor_displacement_kind;
}

static void
build_grid_tile_add_wall(
    struct BuildGrid* build_grid,
    int sx,
    int sz,
    int slevel,
    int wall_ab,
    int wall_element_idx)
{
    struct BuildTile* tile = build_grid_tile_at(build_grid, sx, sz, slevel);
    if( wall_ab == WALL_A )
        tile->wall_element_a_idx = wall_element_idx;
    else
        tile->wall_element_b_idx = wall_element_idx;
}

static void
build_grid_tile_add_element(
    struct BuildGrid* build_grid,
    int sx,
    int sz,
    int slevel,
    int element_idx)
{
    struct BuildTile* tile = build_grid_tile_at(build_grid, sx, sz, slevel);
    assert(tile->elements_length < 10);
    tile->elements[tile->elements_length] = element_idx;
    tile->elements_length++;
}

#endif