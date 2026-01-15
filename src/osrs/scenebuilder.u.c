#ifndef SCENEBUILDER_U_C
#define SCENEBUILDER_U_C

#include "configmap.h"
#include "graphics/dash.h"
#include "osrs/painters.h"

struct ModelEntry
{
    int id;
    struct CacheModel* model;
};

struct MapLocsEntry
{
    int id;
    int mapx;
    int mapz;
    struct CacheMapLocs* locs;
};

struct MapTerrainEntry
{
    int id;
    int mapx;
    int mapz;
    struct CacheMapTerrain* map_terrain;
};

enum WallOffsetType
{
    WALL_OFFSET_TYPE_NONE = 0,
    WALL_OFFSET_TYPE_STRAIGHT,
    WALL_OFFSET_TYPE_DIAGONAL,
};

struct BuildElement
{
    struct DashModelNormals* aliased_lighting_normals;
    enum WallOffsetType wall_offset_type;
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
};

struct BuildTile
{
    int wall_a_element_idx;
    int wall_b_element_idx;

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

struct SceneBuilder
{
    struct Painter* painter;
    int mapx_sw;
    int mapz_sw;
    int mapx_ne;
    int mapz_ne;

    struct DashMap* models_hmap;
    struct DashMap* map_terrains_hmap;
    struct DashMap* map_locs_hmap;
    struct DashMap* config_locs_configmap;
    struct DashMap* config_underlay_configmap;
    struct DashMap* config_overlay_configmap;

    struct BuildGrid* build_grid;
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
    build_tile->wall_a_element_idx = -1;
    build_tile->wall_b_element_idx = -1;

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
#endif