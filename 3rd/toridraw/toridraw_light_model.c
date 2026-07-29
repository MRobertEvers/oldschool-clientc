#include "toridraw_light_model.h"

#include "toridraw_lighting.h"
#include "toridraw_model.h"
#include "toridraw_types.h"

#include <assert.h>
#include <math.h>
#include <string.h>

void
ToriDraw_LightModelParams(
    struct ToriDraw_ModelHandle hnd,
    int light_ambient,
    int light_attenuation,
    int lightsrc_x,
    int lightsrc_y,
    int lightsrc_z)
{
    if( hnd.kind != TORIDRAWMK_MODEL )
        return;

    struct ToriDraw_Model* model = ToriDraw_ModelAsFull(hnd);
    assert(model);

    int light_magnitude =
        (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
    int attenuation = (light_attenuation * light_magnitude) >> 8;

    ToriDraw_ModelAllocNormals(model);
    struct ToriDraw_Normals* nm = model->normals;
    assert(nm);

    /* CalculateVertexNormals accumulates; clear so re-lighting is idempotent. */
    if( model->vertex_count > 0 && nm->vertex_normals )
        memset(
            nm->vertex_normals,
            0,
            (size_t)model->vertex_count * sizeof(struct ToriDraw_Normal));
    if( model->face_count > 0 && nm->face_normals )
        memset(
            nm->face_normals, 0, (size_t)model->face_count * sizeof(struct ToriDraw_Normal));

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

/* Scene/widget default light: Model.calculateNormals(64, 768, -50, -10, -50).
 *
 * Both parameters are added as they arrive, signed. There used to be a second
 * mode here that applied `(contrast & 0xff) * 5`, on the theory that the caller
 * held the raw config byte — but no rscache decoder hands out a raw byte: obj
 * opcode 114 and npc opcode 101 pre-scale by 5, loc opcode 39 by 5 (dat1) or 25
 * (dat2), and spotanim opcodes 7/8 are unscaled in the reference too. So that
 * mode scaled an already-scaled contrast, and its mask turned every negative
 * ambient/contrast into a large positive one — which is what washed loc and obj
 * models out to white. */
void
ToriDraw_LightModelDefault(
    struct ToriDraw_ModelHandle hnd,
    int model_contrast,
    int model_ambient)
{
    ToriDraw_LightModelParams(hnd, 64 + model_ambient, 768 + model_contrast, -50, -10, -50);
}

void
ToriDraw_LightModelDefaultPreScaled(
    struct ToriDraw_ModelHandle hnd,
    int model_contrast,
    int model_ambient)
{
    ToriDraw_LightModelDefault(hnd, model_contrast, model_ambient);
}
