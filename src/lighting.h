#ifndef LIGHTING_H
#define LIGHTING_H

struct VertexNormal
{
    int x;
    int y;
    int z;
    int face_count;
};

void calculate_vertex_normals(
    struct VertexNormal* vertex_normals,
    struct VertexNormal* face_normals,
    int vertex_count,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_faces);

void apply_lighting(
    int* face_colors_a_hsl16,
    int* face_colors_b_hsl16,
    int* face_colors_c_hsl16,
    struct VertexNormal* vertex_normals,
    struct VertexNormal* face_normals,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int num_faces,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int* face_colors_hsl16, // The flat color.
    int* face_alphas,
    int* face_textures,
    int* face_infos,
    int light_ambient,
    int light_attenuation,
    int lightsrc_x,
    int lightsrc_y,
    int lightsrc_z);

#endif
