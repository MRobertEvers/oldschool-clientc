#ifndef MODEL_H
#define MODEL_H

struct Model
{
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
    int* textured_p_coordinate;
    int* textured_m_coordinate;
    int* textured_n_coordinate;

    int rotated;
};

struct ModelBones
{
    int bones_count;
    // Array of arrays vertices... AKA arrays of bones.
    int** bones;
    int* bones_sizes;
};

struct ModelBones* modelbones_new_decode(int* packed_bone_groups, int packed_bone_groups_count);

struct Cache;
struct Model* model_new_from_cache(struct Cache* cache, int model_id);
struct Model* model_new_decode(const unsigned char* inputData, int inputLength);

void modelbones_free(struct ModelBones* modelbones);
void model_free(struct Model* model);

void write_model_separate(const struct Model* model, const char* filename);

#endif
