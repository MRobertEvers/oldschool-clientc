#ifndef GAMECACHE_MODEL_H
#define GAMECACHE_MODEL_H

#include <stdint.h>

struct CacheModel;

struct GameCacheModel
{
    int vertex_count;
    int* vertices_x;
    int* vertices_y;
    int* vertices_z;
    // Each vertex can belong to 32 bone groups.
    //
    uint8_t* vertex_bone_map;

    // These are sometimes called "packed transparency vertex groups"
    // because the animation system uses them to apply alpha animations.
    // packed transparency vertex
    uint8_t* face_bone_map;

    int face_count;
    int* face_indices_a;
    int* face_indices_b;
    int* face_indices_c;
    uint8_t* face_alphas;
    // The bottom 2 bits are the face render kind.
    // The top bits are the face texture id.
    uint8_t* face_infos;
    uint8_t* face_priorities;
    uint16_t* face_colors;
    // If model priority is set, this is important for merged_models,
    // such as characters. For example, "arms" will have a model priority of 10,
    // but do not have face_priorities. When a model with model_priority is merged,
    // all of its faces will have the model_priority.
    uint8_t model_priority;
    int textured_face_count;
    // Used in type 2 >
    uint16_t* textured_p_coordinate;
    uint16_t* textured_m_coordinate;
    uint16_t* textured_n_coordinate;
    int16_t* face_textures;
    int16_t* face_texture_coords;
};

void
gamecache_model_free(struct GameCacheModel* gamecache_model);

struct CacheModel*
gamecache_model_transfer_to_cache_model(struct GameCacheModel* gamecache_model);

#endif