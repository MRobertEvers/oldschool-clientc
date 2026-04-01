#ifndef LIGHTING_H
#define LIGHTING_H

#include "dash_hsl16.h"

#include <stdint.h>

struct LightingNormal
{
    int16_t x;
    int16_t y;
    int16_t z;
    uint16_t face_count;
    uint8_t merged;
};

void
calculate_vertex_normals(
    struct LightingNormal* vertex_normals,
    struct LightingNormal* face_normals,
    int vertex_count,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_faces);

void
apply_lighting(
    DashHSL16* face_colors_a_hsl16,
    DashHSL16* face_colors_b_hsl16,
    DashHSL16* face_colors_c_hsl16,
    struct LightingNormal* vertex_normals,
    struct LightingNormal* face_normals,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int num_faces,
    const DashHSL16* face_colors_hsl16, // The flat color.
    int* face_alphas,
    int* face_textures,
    int* face_infos,
    int light_ambient,
    int light_attenuation,
    int lightsrc_x,
    int lightsrc_y,
    int lightsrc_z);

#endif
