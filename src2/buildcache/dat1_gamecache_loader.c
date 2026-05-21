#include "dat1_gamecache_loader.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct GameCacheModel*
dat1_gamecache_loader_new_gamecache_model_from_cache_model(struct CacheModel* model)
{
    if( !model )
        return NULL;

    struct GameCacheModel* gamecache_model =
        (struct GameCacheModel*)malloc(sizeof(struct GameCacheModel));
    assert(gamecache_model);

    memset(gamecache_model, 0, sizeof(struct GameCacheModel));

    gamecache_model->vertex_count = model->vertex_count;

    if( model->vertices_x )
    {
        gamecache_model->vertices_x = (int*)malloc(model->vertex_count * sizeof(int));
        memcpy(gamecache_model->vertices_x, model->vertices_x, model->vertex_count * sizeof(int));
    }
    else
    {
        gamecache_model->vertices_x = NULL;
    }

    if( model->vertices_y )
    {
        gamecache_model->vertices_y = (int*)malloc(model->vertex_count * sizeof(int));
        memcpy(gamecache_model->vertices_y, model->vertices_y, model->vertex_count * sizeof(int));
    }
    else
    {
        gamecache_model->vertices_y = NULL;
    }

    if( model->vertices_z )
    {
        gamecache_model->vertices_z = (int*)malloc(model->vertex_count * sizeof(int));
        memcpy(gamecache_model->vertices_z, model->vertices_z, model->vertex_count * sizeof(int));
    }
    else
    {
        gamecache_model->vertices_z = NULL;
    }

    if( model->vertex_bone_map )
    {
        gamecache_model->vertex_bone_map =
            (uint8_t*)malloc((size_t)model->vertex_count * sizeof(uint8_t));
        memcpy(
            gamecache_model->vertex_bone_map,
            model->vertex_bone_map,
            (size_t)model->vertex_count * sizeof(uint8_t));
    }
    else
    {
        gamecache_model->vertex_bone_map = NULL;
    }

    gamecache_model->face_count = model->face_count;

    if( model->face_bone_map )
    {
        gamecache_model->face_bone_map =
            (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
        memcpy(
            gamecache_model->face_bone_map,
            model->face_bone_map,
            (size_t)model->face_count * sizeof(uint8_t));
    }
    else
    {
        gamecache_model->face_bone_map = NULL;
    }

    if( model->face_indices_a )
    {
        gamecache_model->face_indices_a = (int*)malloc(model->face_count * sizeof(int));
        memcpy(
            gamecache_model->face_indices_a,
            model->face_indices_a,
            model->face_count * sizeof(int));
    }
    else
    {
        gamecache_model->face_indices_a = NULL;
    }

    if( model->face_indices_b )
    {
        gamecache_model->face_indices_b = (int*)malloc(model->face_count * sizeof(int));
        memcpy(
            gamecache_model->face_indices_b,
            model->face_indices_b,
            model->face_count * sizeof(int));
    }
    else
    {
        gamecache_model->face_indices_b = NULL;
    }

    if( model->face_indices_c )
    {
        gamecache_model->face_indices_c = (int*)malloc(model->face_count * sizeof(int));
        memcpy(
            gamecache_model->face_indices_c,
            model->face_indices_c,
            model->face_count * sizeof(int));
    }
    else
    {
        gamecache_model->face_indices_c = NULL;
    }

    if( model->face_alphas )
    {
        gamecache_model->face_alphas =
            (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
        memcpy(
            gamecache_model->face_alphas,
            model->face_alphas,
            (size_t)model->face_count * sizeof(uint8_t));
    }
    else
    {
        gamecache_model->face_alphas = NULL;
    }

    if( model->face_infos )
    {
        gamecache_model->face_infos = (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
        memcpy(
            gamecache_model->face_infos,
            model->face_infos,
            (size_t)model->face_count * sizeof(uint8_t));
    }
    else
    {
        gamecache_model->face_infos = NULL;
    }

    if( model->face_priorities )
    {
        gamecache_model->face_priorities =
            (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
        memcpy(
            gamecache_model->face_priorities,
            model->face_priorities,
            (size_t)model->face_count * sizeof(uint8_t));
    }
    else
    {
        gamecache_model->face_priorities = NULL;
    }

    if( model->face_colors )
    {
        gamecache_model->face_colors =
            (uint16_t*)malloc((size_t)model->face_count * sizeof(uint16_t));
        memcpy(
            gamecache_model->face_colors,
            model->face_colors,
            (size_t)model->face_count * sizeof(uint16_t));
    }
    else
    {
        gamecache_model->face_colors = NULL;
    }

    gamecache_model->model_priority = model->model_priority;
    gamecache_model->textured_face_count = model->textured_face_count;

    if( model->textured_p_coordinate )
    {
        gamecache_model->textured_p_coordinate =
            (uint16_t*)malloc((size_t)model->textured_face_count * sizeof(uint16_t));
        memcpy(
            gamecache_model->textured_p_coordinate,
            model->textured_p_coordinate,
            (size_t)model->textured_face_count * sizeof(uint16_t));
    }
    else
    {
        gamecache_model->textured_p_coordinate = NULL;
    }

    if( model->textured_m_coordinate )
    {
        gamecache_model->textured_m_coordinate =
            (uint16_t*)malloc((size_t)model->textured_face_count * sizeof(uint16_t));
        memcpy(
            gamecache_model->textured_m_coordinate,
            model->textured_m_coordinate,
            (size_t)model->textured_face_count * sizeof(uint16_t));
    }
    else
    {
        gamecache_model->textured_m_coordinate = NULL;
    }

    if( model->textured_n_coordinate )
    {
        gamecache_model->textured_n_coordinate =
            (uint16_t*)malloc((size_t)model->textured_face_count * sizeof(uint16_t));
        memcpy(
            gamecache_model->textured_n_coordinate,
            model->textured_n_coordinate,
            (size_t)model->textured_face_count * sizeof(uint16_t));
    }
    else
    {
        gamecache_model->textured_n_coordinate = NULL;
    }

    if( model->face_textures )
    {
        gamecache_model->face_textures =
            (int16_t*)malloc((size_t)model->face_count * sizeof(int16_t));
        memcpy(
            gamecache_model->face_textures,
            model->face_textures,
            (size_t)model->face_count * sizeof(int16_t));
    }
    else
    {
        gamecache_model->face_textures = NULL;
    }

    if( model->face_texture_coords )
    {
        gamecache_model->face_texture_coords =
            (int16_t*)malloc((size_t)model->face_count * sizeof(int16_t));
        memcpy(
            gamecache_model->face_texture_coords,
            model->face_texture_coords,
            (size_t)model->face_count * sizeof(int16_t));
    }
    else
    {
        gamecache_model->face_texture_coords = NULL;
    }

    return gamecache_model;
}