#include "render/trspk_toridraw.h"

#include "core/trspk_color_simd.h"
#include "core/trspk_uv_pnm.h"
#include "core/trspk_vbo.h"
#include "toridraw.h"
#include "toridraw_hsl16.h"
#include "toridraw_math.h"
#include "toridraw_model.h"
#include "toridraw_scene.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

bool
trspk_toridraw_texture_is_animated(
    struct ToriDraw_Scene* ctx,
    int tex_id)
{
    if( tex_id < 0 )
        return false;
    assert(ctx);

    struct ToriDraw_TextureState* tex = ToriDraw_SceneTexState(ctx);
    struct ToriDraw_Texture* tex_obj =
        tex ? ToriDraw_TextureMapGet(&tex->texture_map, tex_id) : NULL;
    return tex_obj && tex_obj->animation_direction != TORIDRAW_TEXANIM_DIRECTION_NONE;
}

void
trspk_toridraw_hsl16_to_rgba(
    uint16_t hsl16,
    uint8_t alpha,
    float rgba[4])
{
    const uint32_t rgb = (uint32_t)ToriDraw_Hsl16ToRgb(hsl16);
    trspk_color_unpack_rgb_alpha(rgb, alpha, rgba);
}

float
trspk_pack_gpu_uv_mode(
    int animation_direction,
    int animation_speed)
{
    if( animation_direction == 0 || animation_speed == 0 )
        return 0.0f;

    int enc;
    if( animation_direction == 2 || animation_direction == 4 )
        enc = animation_speed * 2 + 1;
    else
        enc = animation_speed * 2 + 257;

    return (float)(2 * enc);
}

float
trspk_encode_vertex_tex_id(
    int atlas_slot,
    bool cutout,
    int slot_capacity)
{
    if( atlas_slot < 0 )
        return -1.0f;
    if( cutout )
        return (float)(atlas_slot + slot_capacity);
    return (float)atlas_slot;
}

void
trspk_toridraw_uv_pnm_face(
    struct UVFaceCoords* uv_pnm_out,
    struct ToriDraw_Model* model,
    uint32_t face_index)
{
    const uint32_t face_a = (uint32_t)model->face_indices_a[face_index];
    const uint32_t face_b = (uint32_t)model->face_indices_b[face_index];
    const uint32_t face_c = (uint32_t)model->face_indices_c[face_index];

    uint32_t p_vertex = face_a;
    uint32_t m_vertex = face_b;
    uint32_t n_vertex = face_c;

    if( model->face_texture_coords && model->face_texture_coords[face_index] != -1 )
    {
        assert(model->textured_p_coordinate != NULL);
        assert(model->textured_m_coordinate != NULL);
        assert(model->textured_n_coordinate != NULL);

        const uint32_t texture_face = (uint32_t)model->face_texture_coords[face_index];
        p_vertex = (uint32_t)model->textured_p_coordinate[texture_face];
        m_vertex = (uint32_t)model->textured_m_coordinate[texture_face];
        n_vertex = (uint32_t)model->textured_n_coordinate[texture_face];
    }

    uv_pnm_compute(
        uv_pnm_out,
        (float)model->vertices_x[p_vertex],
        (float)model->vertices_y[p_vertex],
        (float)model->vertices_z[p_vertex],
        (float)model->vertices_x[m_vertex],
        (float)model->vertices_y[m_vertex],
        (float)model->vertices_z[m_vertex],
        (float)model->vertices_x[n_vertex],
        (float)model->vertices_y[n_vertex],
        (float)model->vertices_z[n_vertex],
        (float)model->vertices_x[face_a],
        (float)model->vertices_y[face_a],
        (float)model->vertices_z[face_a],
        (float)model->vertices_x[face_b],
        (float)model->vertices_y[face_b],
        (float)model->vertices_z[face_b],
        (float)model->vertices_x[face_c],
        (float)model->vertices_y[face_c],
        (float)model->vertices_z[face_c]);
}

const struct TRSPK_WorldPlacement trspk_world_placement_identity = { 0, 0, 0, 0, 0, 0, 0, false, false };

void
trspk_toridraw_placement_init(
    struct TRSPK_WorldPlacement* out,
    const struct ToriDraw_Position* world_position)
{
    assert(out);

    if( !world_position )
    {
        memset(out, 0, sizeof(*out));
        return;
    }

    {
        const int pitch = ToriDraw_NormalizeAngle(world_position->pitch);
        const int yaw = ToriDraw_NormalizeAngle(world_position->yaw);

        out->has_pitch = pitch != 0;
        out->cos_pitch = out->has_pitch ? ToriDraw_Cos(pitch) : 0;
        out->sin_pitch = out->has_pitch ? ToriDraw_Sin(pitch) : 0;
        out->has_yaw = yaw != 0;
        out->cos_yaw = out->has_yaw ? ToriDraw_Cos(yaw) : 0;
        out->sin_yaw = out->has_yaw ? ToriDraw_Sin(yaw) : 0;
        out->offset_x = world_position->x;
        out->offset_y = world_position->y;
        out->offset_z = world_position->z;
    }
}

void
trspk_toridraw_world_vertex(
    const struct TRSPK_WorldPlacement* placement,
    int vx,
    int vy,
    int vz,
    float* out_x,
    float* out_y,
    float* out_z)
{
    assert(placement);

    int x_rotated = vx;
    int y_rotated = vy;
    int z_rotated = vz;

    if( placement->has_pitch )
    {
        const int cp = placement->cos_pitch;
        const int sp = placement->sin_pitch;
        y_rotated = (vy * cp - vz * sp) >> 16;
        z_rotated = (vy * sp + vz * cp) >> 16;
    }

    if( placement->has_yaw )
    {
        const int cy = placement->cos_yaw;
        const int sy = placement->sin_yaw;
        const int x_yaw = (x_rotated * cy + z_rotated * sy) >> 16;
        z_rotated = (z_rotated * cy - x_rotated * sy) >> 16;
        x_rotated = x_yaw;
    }

    *out_x = (float)(x_rotated + placement->offset_x);
    *out_y = (float)(y_rotated + placement->offset_y);
    *out_z = (float)(z_rotated + placement->offset_z);
}

/* hsl16 -> the packed 0xAARRGGBB a D3D9 vertex stores.
 *
 * ToriDraw_Hsl16ToRgb already hands back 0x00RRGGBB, so this is a shift and
 * an or. The float form below reaches the same 32 bits by way of four
 * divisions by 255 and four multiplications back up again, every one of
 * which the D3D9 vertex writer then discards. */
static inline uint32_t
trspk_toridraw_hsl16_to_argb(
    hsl16_t hsl16,
    uint8_t alpha)
{
    return ((uint32_t)alpha << 24) |
        ((uint32_t)ToriDraw_Hsl16ToRgb(hsl16) & 0x00FFFFFFu);
}

static void
trspk_toridraw_face_colors(
    hsl16_t color_a_hsl16,
    hsl16_t color_b_hsl16,
    hsl16_t color_c_hsl16,
    uint8_t alpha,
    enum TRSPK_BakeColorForm color_form,
    struct TRSPK_ToriDrawBakeFaceVerts* out)
{
    /* HIDDEN and FLAT both collapse the face onto corner A's shade; HIDDEN
     * additionally forces it transparent. Resolving that here leaves one
     * conversion site per colour form instead of one per branch. */
    bool flat = true;
    if( color_c_hsl16 == TORIDRAWHSL16_HIDDEN )
        alpha = 0u;
    else if( color_c_hsl16 != TORIDRAWHSL16_FLAT )
        flat = false;

    if( color_form == TRSPK_BAKE_COLOR_ARGB )
    {
        out->argb_a = trspk_toridraw_hsl16_to_argb(color_a_hsl16, alpha);
        if( flat )
        {
            out->argb_b = out->argb_a;
            out->argb_c = out->argb_a;
        }
        else
        {
            out->argb_b = trspk_toridraw_hsl16_to_argb(color_b_hsl16, alpha);
            out->argb_c = trspk_toridraw_hsl16_to_argb(color_c_hsl16, alpha);
        }
        return;
    }

    trspk_toridraw_hsl16_to_rgba(color_a_hsl16, alpha, out->color_a);
    if( flat )
    {
        memcpy(out->color_b, out->color_a, sizeof(out->color_b));
        memcpy(out->color_c, out->color_a, sizeof(out->color_c));
        return;
    }
    trspk_toridraw_hsl16_to_rgba(color_b_hsl16, alpha, out->color_b);
    trspk_toridraw_hsl16_to_rgba(color_c_hsl16, alpha, out->color_c);
}

void
trspk_toridraw_bake_face(
    struct ToriDraw_Model* model,
    uint32_t face_index,
    const struct TRSPK_WorldPlacement* placement,
    struct ToriDraw_Scene* ctx,
    bool invert_face_alpha,
    enum TRSPK_BakeColorForm color_form,
    struct TRSPK_ToriDrawBakeFaceVerts* out)
{
    const uint32_t face_a = (uint32_t)model->face_indices_a[face_index];
    const uint32_t face_b = (uint32_t)model->face_indices_b[face_index];
    const uint32_t face_c = (uint32_t)model->face_indices_c[face_index];

    /* Reference SceneBuffer.getModelFaces: face alpha applies only to
     * untextured faces; textured faces are always opaque at the face level. */
    int const tex_id = model->face_textures ? (int)model->face_textures[face_index] : -1;
    uint8_t alpha;
    if( tex_id != -1 )
        alpha = 0xFFu;
    else if( model->face_alphas )
        alpha = invert_face_alpha ? (uint8_t)(0xFFu - model->face_alphas[face_index])
                                  : model->face_alphas[face_index];
    else
        alpha = 0xFFu;

    /* Cull fully / near-fully transparent untextured faces (alpha === 0 || 1). */
    if( tex_id == -1 && alpha <= 1u )
        alpha = 0u;

    trspk_toridraw_face_colors(
        model->face_colors_a[face_index],
        model->face_colors_b[face_index],
        model->face_colors_c[face_index],
        alpha,
        color_form,
        out);

    out->tex_id = tex_id;
    struct ToriDraw_Texture* tex = NULL;
    out->uv_mode = 0.0f;
    out->is_animated = false;
    out->tex_cutout = false;
    out->tex_id_encoded = -1.0f;
    if( out->tex_id >= 0 && ctx )
    {
        tex = ToriDraw_TextureMapGet(&ToriDraw_SceneTexState(ctx)->texture_map, out->tex_id);
        if( tex )
        {
            out->uv_mode = trspk_pack_gpu_uv_mode(tex->animation_direction, tex->animation_speed);
            out->is_animated =
                tex->animation_direction != TORIDRAW_TEXANIM_DIRECTION_NONE;
            out->tex_cutout = !tex->opaque;
        }
    }

    trspk_toridraw_uv_pnm_face(&out->uv, model, face_index);

    trspk_toridraw_world_vertex(
        placement,
        model->vertices_x[face_a],
        model->vertices_y[face_a],
        model->vertices_z[face_a],
        &out->wx_a,
        &out->wy_a,
        &out->wz_a);
    trspk_toridraw_world_vertex(
        placement,
        model->vertices_x[face_b],
        model->vertices_y[face_b],
        model->vertices_z[face_b],
        &out->wx_b,
        &out->wy_b,
        &out->wz_b);
    trspk_toridraw_world_vertex(
        placement,
        model->vertices_x[face_c],
        model->vertices_y[face_c],
        model->vertices_z[face_c],
        &out->wx_c,
        &out->wy_c,
        &out->wz_c);
}

static void
trspk_toridraw_bake_face_ground(
    struct ToriDraw_ModelGround* ground,
    uint32_t face_index,
    const struct TRSPK_WorldPlacement* placement,
    struct ToriDraw_Scene* ctx,
    enum TRSPK_BakeColorForm color_form,
    struct TRSPK_ToriDrawBakeFaceVerts* out)
{
    const uint32_t face_a = (uint32_t)ground->face_indices_a[face_index];
    const uint32_t face_b = (uint32_t)ground->face_indices_b[face_index];
    const uint32_t face_c = (uint32_t)ground->face_indices_c[face_index];

    trspk_toridraw_face_colors(
        ground->face_colors_a[face_index],
        ground->face_colors_b[face_index],
        ground->face_colors_c[face_index],
        0xFFu,
        color_form,
        out);

    out->tex_id = ground->face_textures ? (int)ground->face_textures[face_index] : -1;
    struct ToriDraw_Texture* tex = NULL;
    out->uv_mode = 0.0f;
    out->is_animated = false;
    out->tex_cutout = false;
    out->tex_id_encoded = -1.0f;
    if( out->tex_id >= 0 && ctx )
    {
        tex = ToriDraw_TextureMapGet(&ToriDraw_SceneTexState(ctx)->texture_map, out->tex_id);
        if( tex )
        {
            out->uv_mode = trspk_pack_gpu_uv_mode(tex->animation_direction, tex->animation_speed);
            out->is_animated =
                tex->animation_direction != TORIDRAW_TEXANIM_DIRECTION_NONE;
            out->tex_cutout = !tex->opaque;
        }
    }

    /* Ground has no textured_p/m/n — UV from face vertices. */
    uv_pnm_compute(
        &out->uv,
        (float)ground->vertices_x[face_a],
        (float)ground->vertices_y[face_a],
        (float)ground->vertices_z[face_a],
        (float)ground->vertices_x[face_b],
        (float)ground->vertices_y[face_b],
        (float)ground->vertices_z[face_b],
        (float)ground->vertices_x[face_c],
        (float)ground->vertices_y[face_c],
        (float)ground->vertices_z[face_c],
        (float)ground->vertices_x[face_a],
        (float)ground->vertices_y[face_a],
        (float)ground->vertices_z[face_a],
        (float)ground->vertices_x[face_b],
        (float)ground->vertices_y[face_b],
        (float)ground->vertices_z[face_b],
        (float)ground->vertices_x[face_c],
        (float)ground->vertices_y[face_c],
        (float)ground->vertices_z[face_c]);

    trspk_toridraw_world_vertex(
        placement,
        ground->vertices_x[face_a],
        ground->vertices_y[face_a],
        ground->vertices_z[face_a],
        &out->wx_a,
        &out->wy_a,
        &out->wz_a);
    trspk_toridraw_world_vertex(
        placement,
        ground->vertices_x[face_b],
        ground->vertices_y[face_b],
        ground->vertices_z[face_b],
        &out->wx_b,
        &out->wy_b,
        &out->wz_b);
    trspk_toridraw_world_vertex(
        placement,
        ground->vertices_x[face_c],
        ground->vertices_y[face_c],
        ground->vertices_z[face_c],
        &out->wx_c,
        &out->wy_c,
        &out->wz_c);
}

int
trspk_toridraw_face_count(struct ToriDraw_ModelHandle model_handle)
{
    switch( model_handle.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
        return model_handle.u.model.model ? model_handle.u.model.model->face_count : 0;
    case TORIDRAWMK_GROUND:
        return model_handle.u.model.ground ? model_handle.u.model.ground->face_count : 0;
    default:
        return 0;
    }
}

bool
trspk_toridraw_bake_face_handle(
    struct ToriDraw_ModelHandle model_handle,
    uint32_t face_index,
    const struct TRSPK_WorldPlacement* placement,
    struct ToriDraw_Scene* ctx,
    bool invert_face_alpha,
    enum TRSPK_BakeColorForm color_form,
    struct TRSPK_ToriDrawBakeFaceVerts* out)
{
    assert(out);
    switch( model_handle.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
        assert(model_handle.u.model.model);
        trspk_toridraw_bake_face(
            model_handle.u.model.model,
            face_index,
            placement,
            ctx,
            invert_face_alpha,
            color_form,
            out);
        return true;
    case TORIDRAWMK_GROUND:
        assert(model_handle.u.model.ground);
        trspk_toridraw_bake_face_ground(
            model_handle.u.model.ground,
            face_index,
            placement,
            ctx,
            color_form,
            out);
        return true;
    default:
        return false;
    }
}
