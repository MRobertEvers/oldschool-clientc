#ifndef SCENEBUILDER_SHARELIGHT_U_C
#define SCENEBUILDER_SHARELIGHT_U_C

#include "graphics/dash.h"
#include "graphics/lighting.h"
#include "scene.h"

// clang-format off
#include "scenebuilder.u.c"
// clang-format on

struct TileCoord
{
    int x;
    int z;
    int level;
};

/**
 * @brief TODO: IDK how this works
 *
 * @param out
 * @param out_size
 * @param scenery
 * @param tile_x
 * @param tile_z
 * @param tile_level
 * @param tile_size_x
 * @param tile_size_z
 * @return int
 */
static int
gather_adjacent_tiles(
    struct TileCoord* out,
    int out_size,
    struct SceneScenery* scenery,
    int tile_x,
    int tile_z,
    int tile_level,
    int tile_size_x,
    int tile_size_z)
{
    int min_tile_x = tile_x;
    int max_tile_x = tile_x + (tile_size_x);
    int min_tile_z = tile_z - 1;
    int max_tile_z = tile_z + (tile_size_z);

    bool huh = false;

    int count = 0;

    for( int level = tile_level; level <= tile_level + 1; level++ )
    {
        for( int x = min_tile_x; x <= max_tile_x; x++ )
        {
            for( int z = min_tile_z; z <= max_tile_z; z++ )
            {
                // if( (x == tile_x && z == tile_z && level == tile_level) )
                //     continue;
                if( x < 0 || z < 0 || x >= scenery->tile_width_x || z >= scenery->tile_width_z ||
                    level < 0 || level >= MAP_TERRAIN_LEVELS )
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

static int
gather_sharelight_models(
    struct BuildGrid* build_grid,
    struct BuildTile* build_tile,
    int* out,
    int out_size)
{
    int count = 0;
    struct BuildElement* build_element = NULL;

    // if( build_tile->wall_b_element_idx != -1 )
    // {
    //     build_element = build_grid_element_at(build_grid, build_tile->wall_b_element_idx);
    //     assert(build_element && "Element must be valid");

    //     if( build_element->sharelight )
    //         out[count++] = build_tile->wall_b_element_idx;
    // }

    // if( tile->ground_decor != -1 )
    // {
    //     assert(tile->ground_decor < loc_pool_size);
    //     loc = &loc_pool[tile->ground_decor];

    //     model = tile_ground_decor_model_nullable(models, loc);
    //     if( model && model->sharelight )
    //         out[count++] = model;
    // }

    // for( int i = 0; i < tile->locs_length; i++ )
    // {
    //     loc = &loc_pool[tile->locs[i]];
    //     model = tile_scenery_model_nullable(models, loc);
    //     if( model && model->sharelight )
    //         out[count++] = model;

    //     model = tile_wall_model_nullable(models, loc, 1);
    //     if( model && model->sharelight )
    //         out[count++] = model;

    //     model = tile_wall_model_nullable(models, loc, 0);
    //     if( model && model->sharelight )
    //         out[count++] = model;

    //     model = tile_ground_decor_model_nullable(models, loc);
    //     if( model && model->sharelight )
    //         out[count++] = model;
    // }

    for( int i = 0; i < build_tile->elements_length; i++ )
    {
        build_element = build_grid_element_at(build_grid, build_tile->elements[i]);
        assert(build_element && "Element must be valid");

        if( build_element->sharelight )
            out[count++] = build_tile->elements[i];
    }

    assert(count <= out_size);
    return count;
}

static int g_merge_index = 0;
static int g_vertex_a_merge_index[10000] = { 0 };
static int g_vertex_b_merge_index[10000] = { 0 };

static void
merge_normals(
    struct DashModel* model,
    struct LightingNormal* vertex_normals,
    struct LightingNormal* lighting_vertex_normals,
    struct DashModel* other_model,
    struct LightingNormal* other_vertex_normals,
    struct LightingNormal* other_lighting_vertex_normals,
    int check_offset_x,
    int check_offset_y,
    int check_offset_z)
{
    g_merge_index++;

    struct LightingNormal* model_a_normal = NULL;
    struct LightingNormal* model_b_normal = NULL;
    struct LightingNormal* model_a_lighting_normal = NULL;
    struct LightingNormal* model_b_lighting_normal = NULL;
    int x, y, z;
    int other_x, other_y, other_z;

    int merged_vertex_count = 0;

    for( int vertex = 0; vertex < model->vertex_count; vertex++ )
    {
        x = model->vertices_x[vertex] - check_offset_x;
        y = model->vertices_y[vertex] - check_offset_y;
        z = model->vertices_z[vertex] - check_offset_z;

        model_a_normal = &vertex_normals[vertex];
        model_a_lighting_normal = &lighting_vertex_normals[vertex];

        for( int other_vertex = 0; other_vertex < other_model->vertex_count; other_vertex++ )
        {
            other_x = other_model->vertices_x[other_vertex];
            other_y = other_model->vertices_y[other_vertex];
            other_z = other_model->vertices_z[other_vertex];

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

    /**
     * Normals that are merged are assumed to be abutting each other and their faces not visible.
     * Hide them.
     */

    // TODO: This isn't allow for locs. Only ground decor.
    if( merged_vertex_count < 3 || true )
        return;

    // Can't have two faces with the same 3 points, so only need to check two.
    for( int face = 0; face < model->face_count; face++ )
    {
        if( g_vertex_a_merge_index[model->face_indices_a[face]] == g_merge_index &&
            g_vertex_a_merge_index[model->face_indices_b[face]] == g_merge_index &&
            g_vertex_a_merge_index[model->face_indices_c[face]] == g_merge_index )
        {
            // OS1 initializes face infos to 0 here.
            if( !model->face_infos )
            {
                model->face_infos = malloc(sizeof(int) * model->face_count);
                memset(model->face_infos, 0, sizeof(int) * model->face_count);
            }
            // Hidden face (facetype 2 is hidden)
            model->face_infos[face] = 2;
            break;
        }
    }
    for( int face = 0; face < other_model->face_count; face++ )
    {
        if( g_vertex_b_merge_index[other_model->face_indices_a[face]] == g_merge_index &&
            g_vertex_b_merge_index[other_model->face_indices_b[face]] == g_merge_index &&
            g_vertex_b_merge_index[other_model->face_indices_c[face]] == g_merge_index )
        {
            // OS1 initializes face infos to 0 here.
            if( !other_model->face_infos )
            {
                other_model->face_infos = malloc(sizeof(int) * other_model->face_count);
                memset(other_model->face_infos, 0, sizeof(int) * other_model->face_count);
            }
            // Hidden face (facetype 2 is hidden)
            other_model->face_infos[face] = 2;
        }
    }
}

static struct DashModelNormals*
model_normals_new_copy(struct DashModelNormals* normals)
{
    struct DashModelNormals* aliased_normals = malloc(sizeof(struct DashModelNormals));
    memset(aliased_normals, 0, sizeof(struct DashModelNormals));

    int vertex_count = normals->lighting_vertex_normals_count;
    int face_count = normals->lighting_face_normals_count;

    aliased_normals->lighting_vertex_normals = malloc(sizeof(struct LightingNormal) * vertex_count);
    memcpy(
        aliased_normals->lighting_vertex_normals,
        normals->lighting_vertex_normals,
        sizeof(struct LightingNormal) * vertex_count);

    aliased_normals->lighting_face_normals = malloc(sizeof(struct LightingNormal) * face_count);
    memcpy(
        aliased_normals->lighting_face_normals,
        normals->lighting_face_normals,
        sizeof(struct LightingNormal) * face_count);

    aliased_normals->lighting_vertex_normals_count = vertex_count;
    aliased_normals->lighting_face_normals_count = face_count;

    return aliased_normals;
}

static void
dashmodel_alias_normals(
    struct BuildElement* build_element,
    struct DashModelNormals* model_normals)
{
    if( build_element->aliased_lighting_normals )
        return;

    build_element->aliased_lighting_normals = model_normals_new_copy(model_normals);
}

static void
sharelight_build_scene(
    struct SceneBuilder* scene_builder,
    struct TerrainGrid* terrain_grid,
    struct Scene* scene)
{
#define SHARELIGHT_MODELS_COUNT 80
    struct TileCoord adjacent_tiles[SHARELIGHT_MODELS_COUNT] = { 0 };
    struct BuildElement* sharelight_build_element = NULL;
    struct BuildElement* adjacent_build_element = NULL;
    struct SceneElement* sharelight_scene_element = NULL;
    struct SceneElement* adjacent_scene_element = NULL;
    struct BuildTile* sharelight_build_tile = NULL;
    struct BuildTile* adjacent_build_tile = NULL;
    int sharelight_models_count = 0;
    int adjacent_sharelight_elements_count = 0;
    int sharelight_elements[SHARELIGHT_MODELS_COUNT] = { 0 };
    int adjacent_sharelight_elements[SHARELIGHT_MODELS_COUNT] = { 0 };
    for( int sx = 0; sx < scene->tile_width_x; sx++ )
    {
        for( int sz = 0; sz < scene->tile_width_z; sz++ )
        {
            for( int slevel = 0; slevel < MAP_TERRAIN_LEVELS; slevel++ )
            {
                sharelight_build_tile =
                    build_grid_tile_at(scene_builder->build_grid, sx, sz, slevel);
                assert(sharelight_build_tile && "Build tile must be valid");

                sharelight_models_count = gather_sharelight_models(
                    scene_builder->build_grid,
                    sharelight_build_tile,
                    sharelight_elements,
                    SHARELIGHT_MODELS_COUNT);

                for( int i = 0; i < sharelight_models_count; i++ )
                {
                    sharelight_build_element =
                        build_grid_element_at(scene_builder->build_grid, sharelight_elements[i]);
                    sharelight_scene_element =
                        scene_element_at(scene->scenery, sharelight_elements[i]);
                    assert(sharelight_scene_element && "Sharelight scene element must be valid");

                    if( !sharelight_scene_element->dash_model )
                        continue;

                    int adjacent_tiles_count = gather_adjacent_tiles(
                        adjacent_tiles,
                        SHARELIGHT_MODELS_COUNT,
                        scene->scenery,
                        sx,
                        sz,
                        slevel,
                        sharelight_build_element->size_x,
                        sharelight_build_element->size_z);

                    for( int j = 0; j < adjacent_tiles_count; j++ )
                    {
                        struct TileCoord adjacent_tile_coord = adjacent_tiles[j];
                        adjacent_build_tile = build_grid_tile_at(
                            scene_builder->build_grid,
                            adjacent_tile_coord.x,
                            adjacent_tile_coord.z,
                            adjacent_tile_coord.level);

                        adjacent_sharelight_elements_count = gather_sharelight_models(
                            scene_builder->build_grid,
                            adjacent_build_tile,
                            adjacent_sharelight_elements,
                            SHARELIGHT_MODELS_COUNT);

                        for( int k = 0; k < adjacent_sharelight_elements_count; k++ )
                        {
                            if( adjacent_sharelight_elements[k] == sharelight_elements[i] )
                                continue;
                            adjacent_build_element = build_grid_element_at(
                                scene_builder->build_grid, adjacent_sharelight_elements[k]);
                            assert(
                                adjacent_build_element && "Adjacent build element must be valid");
                            adjacent_scene_element =
                                scene_element_at(scene->scenery, adjacent_sharelight_elements[k]);

                            assert(
                                adjacent_scene_element && "Adjacent scene element must be valid");
                            if( !adjacent_scene_element->dash_model )
                                continue;

                            int check_offset_x = (adjacent_tile_coord.x - sx) * 128 +
                                                 (adjacent_build_element->size_x -
                                                  sharelight_build_element->size_x) *
                                                     64;

                            int check_offset_z = (adjacent_tile_coord.z - sz) * 128 +
                                                 (adjacent_build_element->size_z -
                                                  sharelight_build_element->size_z) *
                                                     64;
                            int check_offset_level = adjacent_build_tile->height_center -
                                                     sharelight_build_tile->height_center;

                            if( check_offset_z != 0 && check_offset_x != 0 &&
                                check_offset_level == 0 &&
                                (sx == 31 && (sz == 24 || sz == 23 || sz == 25)) )
                            {
                                printf(
                                    "Merging normals for tile %d, %d, %d: %d %d %d\n",
                                    sx,
                                    sz,
                                    slevel,
                                    check_offset_x,
                                    check_offset_z,
                                    check_offset_level);
                            }

                            dashmodel_alias_normals(
                                sharelight_build_element,
                                sharelight_scene_element->dash_model->normals);
                            dashmodel_alias_normals(
                                adjacent_build_element,
                                adjacent_scene_element->dash_model->normals);

                            merge_normals(
                                sharelight_scene_element->dash_model,
                                sharelight_scene_element->dash_model->normals
                                    ->lighting_vertex_normals,
                                sharelight_build_element->aliased_lighting_normals
                                    ->lighting_vertex_normals,
                                adjacent_scene_element->dash_model,
                                adjacent_scene_element->dash_model->normals
                                    ->lighting_vertex_normals,
                                adjacent_build_element->aliased_lighting_normals
                                    ->lighting_vertex_normals,
                                check_offset_x,
                                check_offset_level,
                                check_offset_z);
                        }
                    }
                }
            }
        }
    }

    for( int i = 0; i < scene->scenery->elements_length; i++ )
    {
        struct SceneElement* scene_element = &scene->scenery->elements[i];
        struct BuildElement* build_element = build_grid_element_at(scene_builder->build_grid, i);
        assert(scene_element && "Element must be valid");

        if( !scene_element->dash_model )
            continue;

        int light_ambient = 64;
        int light_attenuation = 768;
        int lightsrc_x = -50;
        int lightsrc_y = -10;
        int lightsrc_z = -50;

        light_ambient += build_element->light_ambient;
        light_attenuation += build_element->light_attenuation;

        int light_magnitude =
            (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
        int attenuation = (light_attenuation * light_magnitude) >> 8;

        if( !build_element->sharelight )
            continue;
        dashmodel_alias_normals(build_element, scene_element->dash_model->normals);

        apply_lighting(
            scene_element->dash_model->lighting->face_colors_hsl_a,
            scene_element->dash_model->lighting->face_colors_hsl_b,
            scene_element->dash_model->lighting->face_colors_hsl_c,
            build_element->aliased_lighting_normals->lighting_vertex_normals,
            build_element->aliased_lighting_normals->lighting_face_normals,
            scene_element->dash_model->face_indices_a,
            scene_element->dash_model->face_indices_b,
            scene_element->dash_model->face_indices_c,
            scene_element->dash_model->face_count,
            scene_element->dash_model->face_colors,
            scene_element->dash_model->face_alphas,
            scene_element->dash_model->face_textures,
            scene_element->dash_model->face_infos,
            light_ambient,
            attenuation,
            lightsrc_x,
            lightsrc_y,
            lightsrc_z);
    }
}

#endif