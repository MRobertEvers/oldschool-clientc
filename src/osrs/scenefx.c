#include "scenefx.h"

#include "pix3dgl.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

struct SceneFX
{
    struct Scene* scene;
    struct Pix3DGL* pix3dgl;
};

struct SceneFX*
scenefx_new_pix3d()
{
    struct SceneFX* scene_fx = (struct SceneFX*)malloc(sizeof(struct SceneFX));
    memset(scene_fx, 0, sizeof(struct SceneFX));
    scene_fx->pix3dgl = pix3dgl_new();
    return scene_fx;
}

void
scenefx_free(struct SceneFX* scene_fx)
{
    if( !scene_fx )
        return;

    scene_free(scene_fx->scene);
    free(scene_fx);
}

void
scenefx_scene_load_map(struct SceneFX* scene_fx, struct Cache* cache, int chunk_x, int chunk_y)
{
    scene_fx->scene = scene_new_from_map(cache, chunk_x, chunk_y);

    for( int i = 0; i < scene_fx->scene->models_length; i++ )
    {
        struct SceneModel* scene_model = &scene_fx->scene->models[i];
        if( scene_model->model->face_textures || !scene_model->lighting->face_colors_hsl_b )
            continue;

        pix3dgl_model_load(
            scene_fx->pix3dgl,
            i,
            scene_model->model->vertices_x,
            scene_model->model->vertices_y,
            scene_model->model->vertices_z,
            scene_model->model->face_indices_a,
            scene_model->model->face_indices_b,
            scene_model->model->face_indices_c,
            scene_model->model->face_count,
            scene_model->model->face_alphas,
            scene_model->lighting->face_colors_hsl_a,
            scene_model->lighting->face_colors_hsl_b,
            scene_model->lighting->face_colors_hsl_c);
    }
}

void
scenefx_render(struct SceneFX* scene_fx)
{
    pix3dgl_render(scene_fx->pix3dgl);
}