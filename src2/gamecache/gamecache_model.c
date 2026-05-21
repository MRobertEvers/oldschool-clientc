#include "gamecache_model.h"

#include "src/osrs/rscache/tables/model.h"

#include <stdlib.h>
#include <string.h>

void
gamecache_model_free(struct GameCacheModel* gamecache_model)
{
    if( !gamecache_model )
        return;

    free(gamecache_model->vertices_x);
    free(gamecache_model->vertices_y);
    free(gamecache_model->vertices_z);
    free(gamecache_model->vertex_bone_map);
    free(gamecache_model->face_bone_map);
    free(gamecache_model->face_indices_a);
    free(gamecache_model->face_indices_b);
    free(gamecache_model->face_indices_c);
    free(gamecache_model->face_alphas);
    free(gamecache_model->face_infos);
    free(gamecache_model->face_priorities);
    free(gamecache_model->face_colors);
    free(gamecache_model->textured_p_coordinate);
    free(gamecache_model->textured_m_coordinate);
    free(gamecache_model->textured_n_coordinate);
    free(gamecache_model->face_textures);
    free(gamecache_model->face_texture_coords);

    free(gamecache_model);
}

struct CacheModel*
gamecache_model_transfer_to_cache_model(struct GameCacheModel* gamecache_model)
{
    if( !gamecache_model )
        return NULL;

    struct CacheModel* cache_model = (struct CacheModel*)malloc(sizeof(struct CacheModel));
    if( !cache_model )
        return NULL;

    memset(cache_model, 0, sizeof(struct CacheModel));

    cache_model->vertex_count = gamecache_model->vertex_count;
    cache_model->vertices_x = gamecache_model->vertices_x;
    cache_model->vertices_y = gamecache_model->vertices_y;
    cache_model->vertices_z = gamecache_model->vertices_z;
    cache_model->vertex_bone_map = gamecache_model->vertex_bone_map;

    cache_model->face_count = gamecache_model->face_count;
    cache_model->face_bone_map = gamecache_model->face_bone_map;
    cache_model->face_indices_a = gamecache_model->face_indices_a;
    cache_model->face_indices_b = gamecache_model->face_indices_b;
    cache_model->face_indices_c = gamecache_model->face_indices_c;
    cache_model->face_alphas = gamecache_model->face_alphas;
    cache_model->face_infos = gamecache_model->face_infos;
    cache_model->face_priorities = gamecache_model->face_priorities;
    cache_model->face_colors = gamecache_model->face_colors;

    cache_model->model_priority = gamecache_model->model_priority;
    cache_model->textured_face_count = gamecache_model->textured_face_count;
    cache_model->textured_p_coordinate = gamecache_model->textured_p_coordinate;
    cache_model->textured_m_coordinate = gamecache_model->textured_m_coordinate;
    cache_model->textured_n_coordinate = gamecache_model->textured_n_coordinate;
    cache_model->face_textures = (int16_t*)gamecache_model->face_textures;
    cache_model->face_texture_coords = (int16_t*)gamecache_model->face_texture_coords;

    memset(gamecache_model, 0, sizeof(struct GameCacheModel));

    return cache_model;
}
