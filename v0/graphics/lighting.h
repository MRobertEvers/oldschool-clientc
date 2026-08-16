#ifndef LIGHTING_H
#define LIGHTING_H

#include "dash_alphaint.h"
#include "dash_faceint.h"
#include "dash_hsl16.h"
#include "dash_vertexint.h"

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
    faceint_t* face_indices_a,
    faceint_t* face_indices_b,
    faceint_t* face_indices_c,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_faces);

void
apply_lighting(
    hsl16_t* face_colors_a_hsl16,
    hsl16_t* face_colors_b_hsl16,
    hsl16_t* face_colors_c_hsl16,
    struct LightingNormal* vertex_normals,
    struct LightingNormal* face_normals,
    faceint_t* face_indices_a,
    faceint_t* face_indices_b,
    faceint_t* face_indices_c,
    int num_faces,
    const hsl16_t* face_colors_hsl16, // The flat color.
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
