#ifndef WORLD_SHARELIGHT_U_C
#define WORLD_SHARELIGHT_U_C

#include "heightmap.h"
#include "sharelight_map.h"
#include "toridraw/toridraw_gccontext.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_lighting.h"
#include "toridraw/toridraw_model.h"
#include "world_builder.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct TileCoord
{
    int x;
    int z;
    int level;
};

static int
gather_adjacent_tiles(
    int world_width,
    int world_height,
    struct TileCoord* out,
    int out_size,
    int tile_x,
    int tile_z,
    int tile_level,
    int element_size_x,
    int element_size_z)
{
    int min_tile_x = tile_x;
    int max_tile_x = tile_x + (element_size_x);
    int min_tile_z = tile_z - 1;
    int max_tile_z = tile_z + (element_size_z);

    bool huh = false;

    int count = 0;

    for( int level = tile_level; level <= tile_level + 1; level++ )
    {
        for( int x = min_tile_x; x <= max_tile_x; x++ )
        {
            for( int z = min_tile_z; z <= max_tile_z; z++ )
            {
                if( x < 0 || z < 0 || x >= world_width || z >= world_height || level < 0 ||
                    level >= WORLD_MAP_TERRAIN_LEVELS )
                    continue;
                if( (!huh && x < max_tile_x && z < max_tile_z && (z >= tile_z || x >= tile_x)) )
                    continue;

                assert(count < out_size);
                out[count++] = (struct TileCoord){ .x = x, .z = z, .level = level };
            }
        }

        min_tile_x -= 1;
        huh = true;
    }

    return count;
}

static int g_merge_index = 0;
static int g_vertex_a_merge_index[10000] = { 0 };
static int g_vertex_b_merge_index[10000] = { 0 };

static int*
toridraw_model_face_infos_ensure_zero(struct ToriDraw_Model* model)
{
    if( !model )
        return NULL;
    if( !model->face_infos && model->face_count > 0 )
        model->face_infos = calloc((size_t)model->face_count, sizeof(int));
    return model->face_infos;
}

static void
merge_normals(
    struct ToriDraw_Model* model,
    struct ToriDraw_Normal* vertex_normals,
    struct ToriDraw_Normal* lighting_vertex_normals,
    struct ToriDraw_Model* other_model,
    struct ToriDraw_Normal* other_vertex_normals,
    struct ToriDraw_Normal* other_lighting_vertex_normals,
    int check_offset_x,
    int check_offset_y,
    int check_offset_z,
    bool hide_faces)
{
    g_merge_index++;

    struct ToriDraw_Normal* model_a_normal = NULL;
    struct ToriDraw_Normal* model_b_normal = NULL;
    struct ToriDraw_Normal* model_a_lighting_normal = NULL;
    struct ToriDraw_Normal* model_b_lighting_normal = NULL;
    int x, y, z;
    int other_x, other_y, other_z;

    int merged_vertex_count = 0;

    int model_vc = model->vertex_count;
    vertexint_t* model_vx = model->vertices_x;
    vertexint_t* model_vy = model->vertices_y;
    vertexint_t* model_vz = model->vertices_z;

    int other_vc = other_model->vertex_count;
    vertexint_t* other_vx = other_model->vertices_x;
    vertexint_t* other_vy = other_model->vertices_y;
    vertexint_t* other_vz = other_model->vertices_z;

    for( int vertex = 0; vertex < model_vc; vertex++ )
    {
        x = model_vx[vertex] - check_offset_x;
        y = model_vy[vertex] - check_offset_y;
        z = model_vz[vertex] - check_offset_z;

        model_a_normal = &vertex_normals[vertex];
        model_a_lighting_normal = &lighting_vertex_normals[vertex];

        for( int other_vertex = 0; other_vertex < other_vc; other_vertex++ )
        {
            other_x = other_vx[other_vertex];
            other_y = other_vy[other_vertex];
            other_z = other_vz[other_vertex];

            model_b_normal = &other_vertex_normals[other_vertex];
            model_b_lighting_normal = &other_lighting_vertex_normals[other_vertex];

            if( x == other_x && y == other_y && z == other_z && model_b_normal->face_count > 0 &&
                model_a_normal->face_count > 0 )
            {
                model_a_lighting_normal->x += model_b_normal->x;
                model_a_lighting_normal->y += model_b_normal->y;
                model_a_lighting_normal->z += model_b_normal->z;
                model_a_lighting_normal->face_count += model_b_normal->face_count;
                model_a_lighting_normal->merged++;

                model_b_lighting_normal->x += model_a_normal->x;
                model_b_lighting_normal->y += model_a_normal->y;
                model_b_lighting_normal->z += model_a_normal->z;
                model_b_lighting_normal->face_count += model_a_normal->face_count;
                model_b_lighting_normal->merged++;

                merged_vertex_count++;

                g_vertex_a_merge_index[vertex] = g_merge_index;
                g_vertex_b_merge_index[other_vertex] = g_merge_index;
            }
        }
    }

    if( merged_vertex_count < 3 || !hide_faces )
        return;

    int m_fc = model->face_count;
    faceint_t* m_fa = model->face_indices_a;
    faceint_t* m_fb = model->face_indices_b;
    faceint_t* m_fci = model->face_indices_c;
    for( int face = 0; face < m_fc; face++ )
    {
        if( g_vertex_a_merge_index[m_fa[face]] == g_merge_index &&
            g_vertex_a_merge_index[m_fb[face]] == g_merge_index &&
            g_vertex_a_merge_index[m_fci[face]] == g_merge_index )
        {
            int* infos = toridraw_model_face_infos_ensure_zero(model);
            if( infos )
                infos[face] = 2;
        }
    }
    int o_fc = other_model->face_count;
    faceint_t* o_fa = other_model->face_indices_a;
    faceint_t* o_fb = other_model->face_indices_b;
    faceint_t* o_fci = other_model->face_indices_c;
    for( int face = 0; face < o_fc; face++ )
    {
        if( g_vertex_b_merge_index[o_fa[face]] == g_merge_index &&
            g_vertex_b_merge_index[o_fb[face]] == g_merge_index &&
            g_vertex_b_merge_index[o_fci[face]] == g_merge_index )
        {
            int* infos = toridraw_model_face_infos_ensure_zero(other_model);
            if( infos )
                infos[face] = 2;
        }
    }
}

#define ADJACENT_TILES_COUNT 48

static void
defaultlight_build(struct WorldBuilder* builder)
{
    struct World* world = builder->world;
    struct ToriDraw_GCElement* scene_element = NULL;
    struct SharelightMapTile* map_tile = NULL;
    struct SharelightMapElement* map_element = NULL;

    for( int sx = 0; sx < builder->sharelight_map->width; sx++ )
    {
        for( int sz = 0; sz < builder->sharelight_map->height; sz++ )
        {
            for( int slevel = 0; slevel < WORLD_MAP_TERRAIN_LEVELS; slevel++ )
            {
                map_tile = sharelight_map_tile_at(builder->sharelight_map, sx, sz, slevel);
                assert(map_tile && "Sharelight map tile must be valid");

                for( int32_t pi = map_tile->defaultlight_head; pi != -1;
                     pi = builder->sharelight_map->pool[pi].next )
                {
                    map_element = &builder->sharelight_map->pool[pi].element;
                    if( map_element->element_idx == -1 )
                        continue;

                    scene_element =
                        toridraw_gc_element_get(builder->context, map_element->element_idx);
                    if( !scene_element || scene_element->model.kind != TORIDRAWMK_MODEL ||
                        !scene_element->model.u.model.model )
                        continue;

                    struct ToriDraw_Model* dm = scene_element->model.u.model.model;
                    if( !toridraw_model_is_lightable(dm) )
                        continue;

                    struct ToriDraw_ModelHandle hnd = {
                        .kind = TORIDRAWMK_MODEL,
                        .u.model.model = dm,
                    };
                    toridraw_light_model_default(
                        hnd, map_element->light_attenuation, map_element->light_ambient);
                    toridraw_model_free_normals(dm);
                }
            }
        }
    }
}

#define SHARELIGHT_MERGE_LOOKAHEAD 6

static void
alloc_normals_for_column(
    struct WorldBuilder* builder,
    int sx)
{
    struct World* world = builder->world;
    struct SharelightMapTile* map_tile = NULL;
    struct SharelightMapElement* map_element = NULL;
    struct ToriDraw_GCElement* scene_element = NULL;

    for( int sz = 0; sz < builder->sharelight_map->height; sz++ )
    {
        for( int slevel = 0; slevel < WORLD_MAP_TERRAIN_LEVELS; slevel++ )
        {
            map_tile = sharelight_map_tile_at(builder->sharelight_map, sx, sz, slevel);
            if( !map_tile )
                continue;
            for( int32_t pi = map_tile->sharelight_head; pi != -1;
                 pi = builder->sharelight_map->pool[pi].next )
            {
                map_element = &builder->sharelight_map->pool[pi].element;
                scene_element = toridraw_gc_element_get(builder->context, map_element->element_idx);
                if( !scene_element || scene_element->model.kind != TORIDRAWMK_MODEL ||
                    !scene_element->model.u.model.model )
                    continue;
                struct ToriDraw_Model* dm = scene_element->model.u.model.model;
                if( !dm->normals )
                {
                    toridraw_model_alloc_normals(dm);
                    toridraw_model_alloc_merged_normals(dm);
                    toridraw_model_calculate_vertex_normals(dm);
                }
            }
        }
    }
}

static bool
sharelight_should_hide_faces_for_merge(
    int primary_slevel,
    int adjacent_slevel)
{
    (void)primary_slevel;
    (void)adjacent_slevel;
    return primary_slevel == adjacent_slevel;
}

static void
merge_column(
    struct WorldBuilder* builder,
    int sx)
{
    struct World* world = builder->world;
    struct TileCoord adjacent_tiles[ADJACENT_TILES_COUNT];
    struct SharelightMapTile* map_tile = NULL;
    struct SharelightMapTile* adjacent_map_tile = NULL;
    struct SharelightMapElement* map_element = NULL;
    struct SharelightMapElement* adjacent_map_element = NULL;
    struct ToriDraw_GCElement* scene_element = NULL;
    struct ToriDraw_GCElement* adjacent_scene_element = NULL;

    for( int sz = 0; sz < builder->sharelight_map->height; sz++ )
    {
        for( int slevel = 0; slevel < WORLD_MAP_TERRAIN_LEVELS; slevel++ )
        {
            map_tile = sharelight_map_tile_at(builder->sharelight_map, sx, sz, slevel);
            if( !map_tile )
                continue;

            for( int32_t pi = map_tile->sharelight_head; pi != -1;
                 pi = builder->sharelight_map->pool[pi].next )
            {
                map_element = &builder->sharelight_map->pool[pi].element;
                scene_element = toridraw_gc_element_get(builder->context, map_element->element_idx);
                if( !scene_element || scene_element->model.kind != TORIDRAWMK_MODEL ||
                    !scene_element->model.u.model.model )
                    continue;

                int adjacent_tiles_count = gather_adjacent_tiles(
                    builder->sharelight_map->width,
                    builder->sharelight_map->height,
                    adjacent_tiles,
                    ADJACENT_TILES_COUNT,
                    sx,
                    sz,
                    slevel,
                    map_element->size_x,
                    map_element->size_z);

                for( int j = 0; j < adjacent_tiles_count; j++ )
                {
                    struct TileCoord adjacent_tile_coord = adjacent_tiles[j];
                    adjacent_map_tile = sharelight_map_tile_at(
                        builder->sharelight_map,
                        adjacent_tile_coord.x,
                        adjacent_tile_coord.z,
                        adjacent_tile_coord.level);
                    for( int32_t kidx = adjacent_map_tile->sharelight_head; kidx != -1;
                         kidx = builder->sharelight_map->pool[kidx].next )
                    {
                        adjacent_map_element = &builder->sharelight_map->pool[kidx].element;
                        if( adjacent_map_element->element_idx == map_element->element_idx )
                            continue;

                        adjacent_scene_element = toridraw_gc_element_get(
                            builder->context, adjacent_map_element->element_idx);
                        if( !adjacent_scene_element ||
                            adjacent_scene_element->model.kind != TORIDRAWMK_MODEL ||
                            !adjacent_scene_element->model.u.model.model )
                            continue;

                        int check_offset_x =
                            (adjacent_tile_coord.x - sx) * 128 +
                            (adjacent_map_element->size_x - map_element->size_x) * 64;

                        int check_offset_z =
                            (adjacent_tile_coord.z - sz) * 128 +
                            (adjacent_map_element->size_z - map_element->size_z) * 64;

                        int height_center = heightmap_get_center(world->heightmap, sx, sz, slevel);
                        int adjacent_height_center = heightmap_get_center(
                            world->heightmap,
                            adjacent_tile_coord.x,
                            adjacent_tile_coord.z,
                            adjacent_tile_coord.level);
                        int check_offset_height = adjacent_height_center - height_center;

                        struct ToriDraw_Model* dm = scene_element->model.u.model.model;
                        struct ToriDraw_Model* adjacent_dm =
                            adjacent_scene_element->model.u.model.model;

                        if( !toridraw_model_is_lightable(adjacent_dm) ||
                            !toridraw_model_is_lightable(dm) )
                            continue;

                        bool hide_faces = sharelight_should_hide_faces_for_merge(
                            slevel, adjacent_tile_coord.level);

                        merge_normals(
                            dm,
                            dm->normals->vertex_normals,
                            dm->merged_normals->vertex_normals,
                            adjacent_dm,
                            adjacent_dm->normals->vertex_normals,
                            adjacent_dm->merged_normals->vertex_normals,
                            check_offset_x,
                            check_offset_height,
                            check_offset_z,
                            hide_faces);
                    }
                }
            }
        }
    }
}

static void
apply_and_free_column(
    struct WorldBuilder* builder,
    int sx)
{
    struct World* world = builder->world;
    struct SharelightMapTile* map_tile = NULL;
    struct SharelightMapElement* map_element = NULL;
    struct ToriDraw_GCElement* scene_element = NULL;

    for( int sz = 0; sz < builder->sharelight_map->height; sz++ )
    {
        for( int slevel = 0; slevel < WORLD_MAP_TERRAIN_LEVELS; slevel++ )
        {
            map_tile = sharelight_map_tile_at(builder->sharelight_map, sx, sz, slevel);
            if( !map_tile )
                continue;

            for( int32_t pi = map_tile->sharelight_head; pi != -1;
                 pi = builder->sharelight_map->pool[pi].next )
            {
                map_element = &builder->sharelight_map->pool[pi].element;
                scene_element = toridraw_gc_element_get(builder->context, map_element->element_idx);
                if( !scene_element || scene_element->model.kind != TORIDRAWMK_MODEL ||
                    !scene_element->model.u.model.model )
                    continue;

                int light_ambient = 64;
                int light_attenuation = 768;
                int lightsrc_x = -50;
                int lightsrc_y = -10;
                int lightsrc_z = -50;

                light_ambient += map_element->light_ambient;
                light_attenuation += (map_element->light_attenuation & 0xff) * 5;

                int light_magnitude =
                    (int)sqrt((double)(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y +
                                       lightsrc_z * lightsrc_z));
                int attenuation = (light_attenuation * light_magnitude) >> 8;

                struct ToriDraw_Model* dm = scene_element->model.u.model.model;
                if( !toridraw_model_is_lightable(dm) )
                    continue;
                toridraw_apply_lighting(
                    dm->face_colors_a,
                    dm->face_colors_b,
                    dm->face_colors_c,
                    dm->merged_normals->vertex_normals,
                    dm->normals->face_normals,
                    dm->face_indices_a,
                    dm->face_indices_b,
                    dm->face_indices_c,
                    dm->face_count,
                    dm->face_colors,
                    dm->face_alphas,
                    dm->face_textures,
                    dm->face_infos,
                    light_ambient,
                    attenuation,
                    lightsrc_x,
                    lightsrc_y,
                    lightsrc_z,
                    dm->vertices_x,
                    dm->vertices_y,
                    dm->vertices_z);

                toridraw_model_free_normals(dm);
            }
        }
    }
}

static void
world_build_lighting(struct WorldBuilder* builder)
{
    if( !builder->sharelight_map )
        return;

    int scene_size = builder->sharelight_map->width;

    int initial_cols =
        scene_size < SHARELIGHT_MERGE_LOOKAHEAD + 1 ? scene_size : SHARELIGHT_MERGE_LOOKAHEAD + 1;
    for( int sx = 0; sx < initial_cols; sx++ )
        alloc_normals_for_column(builder, sx);

    for( int sx = 0; sx < scene_size; sx++ )
    {
        int alloc_sx = sx + SHARELIGHT_MERGE_LOOKAHEAD;
        if( alloc_sx < scene_size && alloc_sx >= initial_cols )
            alloc_normals_for_column(builder, alloc_sx);

        merge_column(builder, sx);

        if( sx >= 1 )
            apply_and_free_column(builder, sx - 1);
    }

    apply_and_free_column(builder, scene_size - 1);

    defaultlight_build(builder);
}

#endif
