#ifndef TORIDRAW_LIGHTING_H
#define TORIDRAW_LIGHTING_H

#include "toridraw_types.h"

void
toridraw_calculate_vertex_normals(
    struct ToriDraw_Normal* vertex_normals,
    struct ToriDraw_Normal* face_normals,
    int vertex_count,
    faceint_t* face_indices_a,
    faceint_t* face_indices_b,
    faceint_t* face_indices_c,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_faces);

void
toridraw_apply_lighting(
    hsl16_t* face_colors_a_hsl16,
    hsl16_t* face_colors_b_hsl16,
    hsl16_t* face_colors_c_hsl16,
    struct ToriDraw_Normal* vertex_normals,
    struct ToriDraw_Normal* face_normals,
    faceint_t* face_indices_a,
    faceint_t* face_indices_b,
    faceint_t* face_indices_c,
    int num_faces,
    const hsl16_t* face_colors_hsl16,
    alphaint_t* face_alphas,
    faceint_t* face_textures,
    int* face_infos,
    int light_ambient,
    int light_attenuation,
    int lightsrc_x,
    int lightsrc_y,
    int lightsrc_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z);

#endif
