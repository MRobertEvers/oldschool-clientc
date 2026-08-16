#ifndef _LIGHT_MODEL_DEFAULT_U_C
#define _LIGHT_MODEL_DEFAULT_U_C

#include "graphics/dash.h"
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

    dashmodel_alloc_normals(dash_model);
    struct DashModelNormals* nm = dashmodel_normals(dash_model);
    assert(nm);

    calculate_vertex_normals(
        nm->lighting_vertex_normals,
        nm->lighting_face_normals,
        dashmodel_vertex_count(dash_model),
        dashmodel_face_indices_a(dash_model),
        dashmodel_face_indices_b(dash_model),
        dashmodel_face_indices_c(dash_model),
        dashmodel_vertices_x(dash_model),
        dashmodel_vertices_y(dash_model),
        dashmodel_vertices_z(dash_model),
        dashmodel_face_count(dash_model));

    apply_lighting(
        dashmodel_face_colors_a(dash_model),
        dashmodel_face_colors_b(dash_model),
        dashmodel_face_colors_c(dash_model),
        nm->lighting_vertex_normals,
        nm->lighting_face_normals,
        dashmodel_face_indices_a(dash_model),
        dashmodel_face_indices_b(dash_model),
        dashmodel_face_indices_c(dash_model),
        dashmodel_face_count(dash_model),
        dashmodel_face_colors_flat(dash_model),
        dashmodel_face_alphas(dash_model),
        dashmodel_face_textures(dash_model),
        dashmodel_face_infos(dash_model),
        light_ambient,
        attenuation,
        lightsrc_x,
        lightsrc_y,
        lightsrc_z,
        dashmodel_vertices_x(dash_model),
        dashmodel_vertices_y(dash_model),
        dashmodel_vertices_z(dash_model));
}

#endif
