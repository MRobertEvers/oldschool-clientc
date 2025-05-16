#ifndef MODEL_H
#define MODEL_H

struct Model
{
    int vertex_count;
    int* vertices_x;
    int* vertices_y;
    int* vertices_z;

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

#endif
