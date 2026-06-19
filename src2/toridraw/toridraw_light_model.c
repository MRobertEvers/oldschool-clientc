#include "toridraw_light_model.h"

#include "toridraw_lighting.h"
#include "toridraw_model.h"

#include <assert.h>
#include <math.h>

void
ToriDraw_LightModelDefault(
    struct ToriDraw_ModelHandle hnd,
    int model_contrast,
    int model_ambient)
{
    if( hnd.kind != TORIDRAWMK_MODEL )
        return;

    struct ToriDraw_Model* model = ToriDraw_ModelAsFull(hnd);
    if( !model )
        return;

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

    ToriDraw_ModelAllocNormals(model);
    struct ToriDraw_Normals* nm = model->normals;
    assert(nm);

    ToriDraw_CalculateVertexNormals(
        nm->vertex_normals,
        nm->face_normals,
        model->vertex_count,
        model->face_indices_a,
        model->face_indices_b,
        model->face_indices_c,
        model->vertices_x,
        model->vertices_y,
        model->vertices_z,
        model->face_count);

    ToriDraw_ApplyLighting(
        model->face_colors_a,
        model->face_colors_b,
        model->face_colors_c,
        nm->vertex_normals,
        nm->face_normals,
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
        lightsrc_z,
        model->vertices_x,
        model->vertices_y,
        model->vertices_z);
}
