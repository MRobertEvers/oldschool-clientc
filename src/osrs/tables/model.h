#ifndef MODEL_H
#define MODEL_H

enum CacheModelFlags
{
    CMODEL_FLAG_SHARED = 1 << 0,
    CMODEL_FLAG_MERGED = 1 << 1,
    CMODEL_FLAG_TRANSFORMED = 1 << 2
};

struct CacheModel
{
    // TODO: Should this be included or carried with.
    int _id;
    int _model_type;
    int _flags;

    int vertex_count;
    int* vertices_x;
    int* vertices_y;
    int* vertices_z;
    // Each vertex can belong to 32 bone groups.
    // This is
    int* vertex_bone_map;

    int face_count;
    int* face_indices_a;
    int* face_indices_b;
    int* face_indices_c;
    int* face_alphas;
    int* face_infos;
    int* face_priorities;
    int* face_colors;
    int model_priority;
    int textured_face_count;
    // Used in type 2 >
    int* textured_p_coordinate;
    int* textured_m_coordinate;
    int* textured_n_coordinate;
    int* face_textures;
    int* face_texture_coords;
    // Texture render types for type 3 models
    unsigned char* textureRenderTypes;

    int rotated;
};

struct CacheModelBones
{
    int bones_count;
    // Array of arrays vertices... AKA arrays of bones.
    int** bones;
    int* bones_sizes;
};

struct CacheModelBones*
modelbones_new_decode(int* packed_bone_groups, int packed_bone_groups_count);

struct Cache;
struct CacheModel* model_new_from_cache(struct Cache* cache, int model_id);
struct CacheModel* model_new_decode(const unsigned char* inputData, int inputLength);
struct CacheModel* model_new_copy(struct CacheModel* model);
struct CacheModel* model_new_merge(struct CacheModel** models, int model_count);

void modelbones_free(struct CacheModelBones* modelbones);
void model_free(struct CacheModel* model);

void write_model_separate(const struct CacheModel* model, const char* filename);

#endif
