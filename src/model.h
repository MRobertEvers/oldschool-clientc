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
};

struct ModelBones
{
    int bones_count;
    // Array of arrays vertices... AKA arrays of bones.
    int** bones;
    int* bones_sizes;
};

struct ModelBones* model_decode_bones(int* packed_bone_groups, int packed_bone_groups_count);

struct Model* decodeModel(const unsigned char* inputData, int inputLength);
void write_model_separate(const struct Model* model, const char* filename);

#endif
