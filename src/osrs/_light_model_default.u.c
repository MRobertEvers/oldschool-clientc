#ifndef _LIGHT_MODEL_DEFAULT_U_C
#define _LIGHT_MODEL_DEFAULT_U_C

#include "graphics/lighting.h"

#include <math.h>

static void
_light_model_default(
    struct DashModel* dash_model,
    int model_contrast,
    int model_ambient)
{
    int light_ambient = 64;
    int light_attenuation = 768;
    int lightsrc_x = -50;
    int lightsrc_y = -10;
    int lightsrc_z = -50;

    light_ambient += model_ambient;
    // This is what 2004Scape does. Later revs do not.
    light_attenuation += (model_contrast & 0xff) * 5;

    int light_magnitude =
        (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
    int attenuation = (light_attenuation * light_magnitude) >> 8;

    calculate_vertex_normals(
        dash_model->normals->lighting_vertex_normals,
        dash_model->normals->lighting_face_normals,
        dash_model->vertex_count,
        dash_model->face_indices_a,
        dash_model->face_indices_b,
        dash_model->face_indices_c,
        dash_model->vertices_x,
        dash_model->vertices_y,
        dash_model->vertices_z,
        dash_model->face_count);

    apply_lighting(
        dash_model->lighting->face_colors_hsl_a,
        dash_model->lighting->face_colors_hsl_b,
        dash_model->lighting->face_colors_hsl_c,
        dash_model->normals->lighting_vertex_normals,
        dash_model->normals->lighting_face_normals,
        dash_model->face_indices_a,
        dash_model->face_indices_b,
        dash_model->face_indices_c,
        dash_model->face_count,
        dash_model->face_colors,
        dash_model->face_alphas,
        dash_model->face_textures,
        dash_model->face_infos,
        light_ambient,
        attenuation,
        lightsrc_x,
        lightsrc_y,
        lightsrc_z);
}

#endif