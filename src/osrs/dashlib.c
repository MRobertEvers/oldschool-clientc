#include "dashlib.h"

struct DashModel*
dashmodel_new_from_cache_model(struct CacheModel* model)
{
    struct DashModel* dash_model = (struct DashModel*)malloc(sizeof(struct DashModel));
    memset(dash_model, 0, sizeof(struct DashModel));

    dash_model->vertex_count = model->vertex_count;
    dash_model->vertices_x = model->vertices_x;
    dash_model->vertices_y = model->vertices_y;
    dash_model->vertices_z = model->vertices_z;

    dash_model->face_count = model->face_count;
    dash_model->face_indices_a = model->face_indices_a;
    dash_model->face_indices_b = model->face_indices_b;
    dash_model->face_indices_c = model->face_indices_c;

    dash_model->face_alphas = model->face_alphas;
    dash_model->face_infos = model->face_infos;
    dash_model->face_priorities = model->face_priorities;

    dash_model->textured_face_count = model->textured_face_count;
    dash_model->textured_p_coordinate = model->textured_p_coordinate;
    dash_model->textured_m_coordinate = model->textured_m_coordinate;
    dash_model->textured_n_coordinate = model->textured_n_coordinate;
    dash_model->face_textures = model->face_textures;
    dash_model->face_texture_coords = model->face_texture_coords;

    dash_model->normals = dashmodel_normals_new(model->vertex_count, model->face_count);
    dash_model->lighting = dashmodel_lighting_new(model->face_count);
    dash_model->vertex_bones = dashmodel_bones_new(model->vertex_bone_map, model->vertex_count);
    dash_model->face_bones = dashmodel_bones_new(model->face_bone_map, model->face_count);

    dash_model->bounds_cylinder =
        (struct DashBoundsCylinder*)malloc(sizeof(struct DashBoundsCylinder));
    memset(dash_model->bounds_cylinder, 0, sizeof(struct DashBoundsCylinder));

    *dash_model->bounds_cylinder = dash3d_calculate_bounds_cylinder(
        dash_model->vertex_count,
        dash_model->vertices_x,
        dash_model->vertices_y,
        dash_model->vertices_z);

    return dash_model;
}
