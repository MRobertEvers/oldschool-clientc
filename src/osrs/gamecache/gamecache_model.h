#ifndef GAMECACHE_MODEL_H
#define GAMECACHE_MODEL_H

#include <stdint.h>

enum GameCacheModelFlags
{
    GAMECACHE_MODEL_FLAG_SHARED = 1 << 0,
    GAMECACHE_MODEL_FLAG_MERGED = 1 << 1,
    GAMECACHE_MODEL_FLAG_TRANSFORMED = 1 << 2,
};

/** Standalone GameCache model type -- drops _ids[10] and rotated (not read by consumers). */
struct GameCacheModel
{
    int _id;
    int _model_type;
    int _flags;

    int vertex_count;
    int* vertices_x;
    int* vertices_y;
    int* vertices_z;
    uint8_t* vertex_bone_map;

    uint8_t* face_bone_map;

    int face_count;
    int* face_indices_a;
    int* face_indices_b;
    int* face_indices_c;
    uint8_t* face_alphas;
    uint8_t* face_infos;
    uint8_t* face_priorities;
    uint16_t* face_colors;
    uint8_t model_priority;
    int textured_face_count;
    uint16_t* textured_p_coordinate;
    uint16_t* textured_m_coordinate;
    uint16_t* textured_n_coordinate;
    int16_t* face_textures;
    int16_t* face_texture_coords;
    unsigned char* textureRenderTypes;
};

struct GameCacheModel*
gamecache_model_new_copy(struct GameCacheModel* model);

struct GameCacheModel*
gamecache_model_new_merge(struct GameCacheModel** models, int model_count);

void
gamecache_model_free(struct GameCacheModel* model);

/* In-place transforms mirroring model_transforms.h for the GameCache model path. */
void gamecache_model_transform_mirror(struct GameCacheModel* model);
void gamecache_model_transform_recolor(struct GameCacheModel* model, int color_src, int color_dst);
void gamecache_model_transform_retexture(struct GameCacheModel* model, int tex_src, int tex_dst);
void gamecache_model_transform_scale(struct GameCacheModel* model, int x, int z, int height);
void gamecache_model_transform_orient(struct GameCacheModel* model, int orientation);
void gamecache_model_transform_translate(struct GameCacheModel* model, int x, int y, int z);

#endif
