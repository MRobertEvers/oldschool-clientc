#include "dashlib.h"

#include "graphics/lighting.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct DashModel*
dashmodel_new_from_cache_model(struct CacheModel* model)
{
    struct DashModel* dash_model = (struct DashModel*)malloc(sizeof(struct DashModel));
    memset(dash_model, 0, sizeof(struct DashModel));

    dash_model->vertex_count = model->vertex_count;
    dash_model->vertices_x = model->vertices_x;
    dash_model->vertices_y = model->vertices_y;
    dash_model->vertices_z = model->vertices_z;
    model->vertices_x = NULL;
    model->vertices_y = NULL;
    model->vertices_z = NULL;

    dash_model->face_count = model->face_count;
    dash_model->face_indices_a = model->face_indices_a;
    dash_model->face_indices_b = model->face_indices_b;
    dash_model->face_indices_c = model->face_indices_c;
    model->face_indices_a = NULL;
    model->face_indices_b = NULL;
    model->face_indices_c = NULL;

    dash_model->face_colors = model->face_colors;
    dash_model->face_alphas = model->face_alphas;
    dash_model->face_infos = model->face_infos;
    dash_model->face_priorities = model->face_priorities;
    model->face_colors = NULL;
    model->face_alphas = NULL;
    model->face_infos = NULL;
    model->face_priorities = NULL;

    dash_model->textured_face_count = model->textured_face_count;
    dash_model->textured_p_coordinate = model->textured_p_coordinate;
    dash_model->textured_m_coordinate = model->textured_m_coordinate;
    dash_model->textured_n_coordinate = model->textured_n_coordinate;
    dash_model->face_textures = model->face_textures;
    dash_model->face_texture_coords = model->face_texture_coords;
    model->textured_p_coordinate = NULL;
    model->textured_m_coordinate = NULL;
    model->textured_n_coordinate = NULL;
    model->face_textures = NULL;
    model->face_texture_coords = NULL;

    dash_model->normals = dashmodel_normals_new(model->vertex_count, model->face_count);
    dash_model->lighting = dashmodel_lighting_new(model->face_count);
    if( model->vertex_bone_map )
        dash_model->vertex_bones = dashmodel_bones_new(model->vertex_bone_map, model->vertex_count);
    if( model->face_bone_map )
        dash_model->face_bones = dashmodel_bones_new(model->face_bone_map, model->face_count);

    dash_model->bounds_cylinder =
        (struct DashBoundsCylinder*)malloc(sizeof(struct DashBoundsCylinder));
    memset(dash_model->bounds_cylinder, 0, sizeof(struct DashBoundsCylinder));

    dash3d_calculate_bounds_cylinder(
        dash_model->bounds_cylinder,
        dash_model->vertex_count,
        dash_model->vertices_x,
        dash_model->vertices_y,
        dash_model->vertices_z);

    return dash_model;
}

struct DashModelBones*
dashmodel_bones_new(
    int* bone_map,
    int bone_count)
{
    struct DashModelBones* bones = (struct DashModelBones*)malloc(sizeof(struct DashModelBones));
    memset(bones, 0, sizeof(struct DashModelBones));

    struct ModelBones* model_bones = modelbones_new_decode(bone_map, bone_count);
    bones->bones_count = model_bones->bones_count;
    bones->bones = model_bones->bones;
    bones->bones_sizes = model_bones->bones_sizes;

    return bones;
}

static struct DashModelLighting*
model_lighting_new(int face_count)
{
    struct DashModelLighting* lighting = malloc(sizeof(struct DashModelLighting));
    memset(lighting, 0, sizeof(struct DashModelLighting));

    lighting->face_colors_hsl_a = malloc(sizeof(int) * face_count);
    memset(lighting->face_colors_hsl_a, 0, sizeof(int) * face_count);

    lighting->face_colors_hsl_b = malloc(sizeof(int) * face_count);
    memset(lighting->face_colors_hsl_b, 0, sizeof(int) * face_count);

    lighting->face_colors_hsl_c = malloc(sizeof(int) * face_count);
    memset(lighting->face_colors_hsl_c, 0, sizeof(int) * face_count);

    return lighting;
}

struct DashModelLighting*
dashmodel_lighting_new_default(
    struct CacheModel* model,
    struct DashModelNormals* normals,
    int model_contrast,
    int model_attenuation)
{
    struct DashModelLighting* lighting = dashmodel_lighting_new(model->face_count);

    int light_ambient = 64;
    int light_attenuation = 768;
    int lightsrc_x = -50;
    int lightsrc_y = -10;
    int lightsrc_z = -50;

    light_ambient += model_contrast;
    light_attenuation += model_attenuation;

    int light_magnitude =
        (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
    int attenuation = (light_attenuation * light_magnitude) >> 8;

    apply_lighting(
        lighting->face_colors_hsl_a,
        lighting->face_colors_hsl_b,
        lighting->face_colors_hsl_c,
        normals->lighting_vertex_normals,
        normals->lighting_face_normals,
        model->face_indices_a,
        model->face_indices_b,
        model->face_indices_c,
        model->face_count,
        model->face_colors,
        model->face_alphas,
        model->face_textures,
        model->face_infos,
        light_ambient,
        attenuation,
        lightsrc_x,
        lightsrc_y,
        lightsrc_z);

    return lighting;
}