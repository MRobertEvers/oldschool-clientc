#include "toridraw_light_model.h"
#include <stdbool.h>

#include "toridraw_lighting.h"
#include "toridraw_model.h"
#include "toridraw_types.h"

#include <assert.h>
#include <math.h>
#include <string.h>

/* Reference defaults — Client-TS / xrsps both agree on these two regimes. */
static struct ToriDraw_LightProfile g_actor_profile = {
    .ambient = 64,
    .attenuation = 850,
    .src_x = -30,
    .src_y = -50,
    .src_z = -30,
};

static struct ToriDraw_LightProfile g_scene_profile = {
    .ambient = 64,
    .attenuation = 768,
    .src_x = -50,
    .src_y = -10,
    .src_z = -50,
};

void
ToriDraw_LightSetProfiles(
    struct ToriDraw_LightProfile const* actor,
    struct ToriDraw_LightProfile const* scene)
{
    if( actor )
        g_actor_profile = *actor;
    if( scene )
        g_scene_profile = *scene;
}

struct ToriDraw_LightProfile const*
ToriDraw_LightSceneProfile(void)
{
    return &g_scene_profile;
}

struct ToriDraw_LightProfile const*
ToriDraw_LightActorProfile(void)
{
    return &g_actor_profile;
}

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

    /* Lighting writes the per-corner colours and the normals, which are the
     * placement-private half -- legal on a model whose FACES are on loan, and
     * not on one that is shared whole, where it would relight every placement
     * of the loc at once. The accessor is the one that says so. */
    struct ToriDraw_Model* model = ToriDraw_ModelWrite(hnd);
    assert(model);

    int light_magnitude =
        (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
    int attenuation = (light_attenuation * light_magnitude) >> 8;

    /*
     * A set this call allocated arrives zeroed already (ToriDraw_NormalsNew
     * clears exactly the range this model uses, on the pooled path as well as
     * the fresh one), so clearing it again is a second pass over the same
     * bytes. Only a model that ALREADY had normals needs it -- that is the
     * re-lighting case the clear exists for, since CalculateVertexNormals
     * accumulates into the vertex normals.
     *
     * It is not a small saving at scene scale: a rebuild default-lights
     * several thousand scenery models, and the two arrays are ten bytes a
     * vertex and ten a face.
     */
    bool const normals_were_fresh = model->normals == NULL;

    ToriDraw_ModelAllocNormals(model);
    struct ToriDraw_Normals* nm = model->normals;
    assert(nm);

    if( !normals_were_fresh )
    {
        if( model->vertex_count > 0 && nm->vertex_normals )
            memset(
                nm->vertex_normals,
                0,
                (size_t)model->vertex_count * sizeof(struct ToriDraw_Normal));
        if( model->face_count > 0 && nm->face_normals )
            memset(
                nm->face_normals, 0, (size_t)model->face_count * sizeof(struct ToriDraw_Normal));
    }

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

/* Both regimes add signed ambient/contrast offsets as they arrive. There used
 * to be a second mode that applied `(contrast & 0xff) * 5`, on the theory that
 * the caller held the raw config byte — but no rscache decoder hands out a
 * raw byte: obj opcode 114 and npc opcode 101 pre-scale by 5, loc opcode 39
 * by 5 (dat1) or 25 (dat2), and spotanim opcodes 7/8 are unscaled in the
 * reference too. So that mode scaled an already-scaled contrast, and its mask
 * turned every negative ambient/contrast into a large positive one — which is
 * what washed loc and obj models out to white. */
void
ToriDraw_LightModelScene(
    struct ToriDraw_ModelHandle hnd,
    int model_contrast,
    int model_ambient)
{
    struct ToriDraw_LightProfile const* p = &g_scene_profile;
    ToriDraw_LightModelParams(
        hnd,
        p->ambient + model_ambient,
        p->attenuation + model_contrast,
        p->src_x,
        p->src_y,
        p->src_z);
}

void
ToriDraw_LightModelActor(
    struct ToriDraw_ModelHandle hnd,
    int model_contrast,
    int model_ambient)
{
    struct ToriDraw_LightProfile const* p = &g_actor_profile;
    ToriDraw_LightModelParams(
        hnd,
        p->ambient + model_ambient,
        p->attenuation + model_contrast,
        p->src_x,
        p->src_y,
        p->src_z);
}

void
ToriDraw_LightModelDefault(
    struct ToriDraw_ModelHandle hnd,
    int model_contrast,
    int model_ambient)
{
    ToriDraw_LightModelScene(hnd, model_contrast, model_ambient);
}
