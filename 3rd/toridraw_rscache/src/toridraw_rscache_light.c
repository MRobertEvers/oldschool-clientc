#include "toridraw_rscache.h"

#include <assert.h>

static const struct ToriDraw_RSCacheLight g_reference_light = {
    /* Model.calculateNormals(64, 768, -50, -10, -50) -- every reference client,
     * for world geometry and widget previews alike. */
    64, 768, -50, -10, -50
};

static void
light_model(
    struct ToriDraw_Model* model,
    const struct ToriDraw_RSCacheLight* light,
    bool keep_normals)
{
    assert(model);
    assert(model->face_count > 0);
    assert(model->vertices_x);
    assert(model->vertices_y);
    assert(model->vertices_z);
    assert(model->face_indices_a);
    assert(model->face_indices_b);
    assert(model->face_indices_c);
    /* The conversion allocates these; a model that reaches here without them
     * was not built by this library and has nowhere to put the result. */
    assert(model->face_colors_a);
    assert(model->face_colors_b);
    assert(model->face_colors_c);

    if( !light )
        light = &g_reference_light;

    /* Idempotent: ToriDraw_ModelCalculateVertexNormals allocates the normals if
     * they are absent and recomputes them from the CURRENT vertices if they are
     * present -- which is what a relight after a resize needs. */
    ToriDraw_ModelCalculateVertexNormals(model);

    ToriDraw_ApplyLighting(
        model->face_colors_a,
        model->face_colors_b,
        model->face_colors_c,
        model->normals ? model->normals->vertex_normals : NULL,
        model->normals ? model->normals->face_normals : NULL,
        model->face_indices_a,
        model->face_indices_b,
        model->face_indices_c,
        model->face_count,
        model->face_colors,
        model->face_alphas,
        model->face_textures,
        model->face_infos,
        light->ambient,
        light->attenuation,
        light->x,
        light->y,
        light->z,
        model->vertices_x,
        model->vertices_y,
        model->vertices_z);

    if( !keep_normals )
        ToriDraw_ModelFreeNormals(model);
}

void
ToriDraw_RSCacheModelLight(
    struct ToriDraw_Model* model,
    const struct ToriDraw_RSCacheLight* light)
{
    light_model(model, light, false);
}

void
ToriDraw_RSCacheModelLightKeepNormals(
    struct ToriDraw_Model* model,
    const struct ToriDraw_RSCacheLight* light)
{
    light_model(model, light, true);
}
