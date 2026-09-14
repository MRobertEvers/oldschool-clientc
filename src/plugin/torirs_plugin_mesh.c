#include "plugin/torirs_plugin_mesh.h"

#include "toridraw.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
ToriRS_PluginMeshGrow(
    struct ToriRS_PluginMesh* mesh,
    int faces,
    int want)
{
    int cap;

    assert(mesh);
    cap = faces ? mesh->face_cap : mesh->vertex_cap;
    if( want <= cap )
        return;
    cap = cap ? cap * 2 : 64;
    while( cap < want )
        cap *= 2;

    if( faces )
    {
        mesh->face_a = realloc(mesh->face_a, (size_t)cap * sizeof(*mesh->face_a));
        mesh->face_b = realloc(mesh->face_b, (size_t)cap * sizeof(*mesh->face_b));
        mesh->face_c = realloc(mesh->face_c, (size_t)cap * sizeof(*mesh->face_c));
        mesh->face_color = realloc(mesh->face_color, (size_t)cap * sizeof(*mesh->face_color));
        mesh->face_alpha = realloc(mesh->face_alpha, (size_t)cap * sizeof(*mesh->face_alpha));
        assert(mesh->face_a);
        assert(mesh->face_b);
        assert(mesh->face_c);
        assert(mesh->face_color);
        assert(mesh->face_alpha);
        mesh->face_cap = cap;
        return;
    }

    mesh->vertices_x = realloc(mesh->vertices_x, (size_t)cap * sizeof(*mesh->vertices_x));
    mesh->vertices_y = realloc(mesh->vertices_y, (size_t)cap * sizeof(*mesh->vertices_y));
    mesh->vertices_z = realloc(mesh->vertices_z, (size_t)cap * sizeof(*mesh->vertices_z));
    assert(mesh->vertices_x);
    assert(mesh->vertices_y);
    assert(mesh->vertices_z);
    mesh->vertex_cap = cap;
}

void
ToriRS_PluginMeshRelease(struct ToriRS_PluginMesh* mesh)
{
    assert(mesh);
    free(mesh->vertices_x);
    free(mesh->vertices_y);
    free(mesh->vertices_z);
    free(mesh->face_a);
    free(mesh->face_b);
    free(mesh->face_c);
    free(mesh->face_color);
    free(mesh->face_alpha);
    memset(mesh, 0, sizeof(*mesh));
}

struct ToriDraw_Model*
ToriRS_PluginMeshBuildModel(struct ToriRS_PluginMesh const* mesh)
{
    struct ToriDraw_Model* model;

    assert(mesh);
    assert(mesh->vertex_count > 0);
    assert(mesh->face_count > 0);

    model = ToriDraw_ModelNew(mesh->vertex_count, mesh->face_count, 0);
    assert(model);

    model->vertices_x =
        ToriDraw_BufCopy(mesh->vertices_x, (size_t)mesh->vertex_count, sizeof(*model->vertices_x));
    model->vertices_y =
        ToriDraw_BufCopy(mesh->vertices_y, (size_t)mesh->vertex_count, sizeof(*model->vertices_y));
    model->vertices_z =
        ToriDraw_BufCopy(mesh->vertices_z, (size_t)mesh->vertex_count, sizeof(*model->vertices_z));
    model->face_indices_a =
        ToriDraw_BufCopy(mesh->face_a, (size_t)mesh->face_count, sizeof(*model->face_indices_a));
    model->face_indices_b =
        ToriDraw_BufCopy(mesh->face_b, (size_t)mesh->face_count, sizeof(*model->face_indices_b));
    model->face_indices_c =
        ToriDraw_BufCopy(mesh->face_c, (size_t)mesh->face_count, sizeof(*model->face_indices_c));
    model->face_colors =
        ToriDraw_BufCopy(mesh->face_color, (size_t)mesh->face_count, sizeof(*model->face_colors));
    model->face_alphas =
        ToriDraw_BufCopy(mesh->face_alpha, (size_t)mesh->face_count, sizeof(*model->face_alphas));

    model->face_colors_a = calloc((size_t)mesh->face_count, sizeof(*model->face_colors_a));
    model->face_colors_b = calloc((size_t)mesh->face_count, sizeof(*model->face_colors_b));
    model->face_colors_c = calloc((size_t)mesh->face_count, sizeof(*model->face_colors_c));
    assert(model->face_colors_a);
    assert(model->face_colors_b);
    assert(model->face_colors_c);

    return model;
}
