#include "render/trspk_toridraw.h"

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TORIRS_FACE_DEBUG=1: per-model bake tallies. Temporary. */
static int g_gl_face_dbg_on = -1;
static int g_gl_face_dbg_faces;
static int g_gl_face_dbg_alpha0;
static int g_gl_face_dbg_hidden;
static int g_gl_face_dbg_textured;
static int g_gl_face_dbg_models;

static int
gl_face_dbg_enabled(void)
{
    if( g_gl_face_dbg_on < 0 )
        g_gl_face_dbg_on = getenv("TORIRS_FACE_DEBUG") != NULL;
    return g_gl_face_dbg_on;
}

static void
gl_face_dbg_flush_model(int face_count)
{
    if( !gl_face_dbg_enabled() || g_gl_face_dbg_faces <= 0 )
        return;
    if( face_count >= 200 || g_gl_face_dbg_faces >= 200 )
    {
        fprintf(
            stderr,
            "face_dbg gl3 model#%d baked=%d alpha0=%d hidden=%d textured=%d\n",
            g_gl_face_dbg_models,
            g_gl_face_dbg_faces,
            g_gl_face_dbg_alpha0,
            g_gl_face_dbg_hidden,
            g_gl_face_dbg_textured);
    }
    g_gl_face_dbg_models++;
    g_gl_face_dbg_faces = 0;
    g_gl_face_dbg_alpha0 = 0;
    g_gl_face_dbg_hidden = 0;
    g_gl_face_dbg_textured = 0;
}

bool
trspk_toridraw_texture_is_animated(
    struct ToriDraw_Scene* ctx,
    int tex_id)
{
    if( tex_id < 0 || !ctx )
        return false;

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
    const uint32_t rgb = ToriDraw_Hsl16ToRgb(hsl16);
    rgba[0] = (float)((rgb >> 16) & 0xFFu) / 255.0f;
    rgba[1] = (float)((rgb >> 8) & 0xFFu) / 255.0f;
    rgba[2] = (float)(rgb & 0xFFu) / 255.0f;
    rgba[3] = (float)alpha / 255.0f;
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
    int tex_id,
    const struct ToriDraw_Texture* tex)
{
    if( tex_id < 0 )
        return -1.0f;
    if( tex && !tex->opaque )
        return (float)(tex_id + 256);
    return (float)tex_id;
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

void
trspk_toridraw_world_vertex(
    const struct ToriDraw_Position* world_position,
    int vx,
    int vy,
    int vz,
    float* out_x,
    float* out_y,
    float* out_z)
{
    if( !world_position )
    {
        *out_x = (float)vx;
        *out_y = (float)vy;
        *out_z = (float)vz;
        return;
    }

    const int pitch = ToriDraw_NormalizeAngle(world_position->pitch);
    const int yaw = ToriDraw_NormalizeAngle(world_position->yaw);

    int x_rotated = vx;
    int y_rotated = vy;
    int z_rotated = vz;

    if( pitch != 0 )
    {
        const int cp = ToriDraw_Cos(pitch);
        const int sp = ToriDraw_Sin(pitch);
        y_rotated = (vy * cp - vz * sp) >> 16;
        z_rotated = (vy * sp + vz * cp) >> 16;
    }

    if( yaw != 0 )
    {
        const int cy = ToriDraw_Cos(yaw);
        const int sy = ToriDraw_Sin(yaw);
        const int x_yaw = (x_rotated * cy + z_rotated * sy) >> 16;
        z_rotated = (z_rotated * cy - x_rotated * sy) >> 16;
        x_rotated = x_yaw;
    }

    *out_x = (float)(x_rotated + world_position->x);
    *out_y = (float)(y_rotated + world_position->y);
    *out_z = (float)(z_rotated + world_position->z);
}

static void
trspk_toridraw_face_colors(
    hsl16_t color_a_hsl16,
    hsl16_t color_b_hsl16,
    hsl16_t color_c_hsl16,
    uint8_t alpha,
    float color_a[4],
    float color_b[4],
    float color_c[4])
{
    if( color_c_hsl16 == TORIDRAWHSL16_HIDDEN )
    {
        alpha = 0u;
        trspk_toridraw_hsl16_to_rgba(color_a_hsl16, alpha, color_a);
        color_b[0] = color_a[0];
        color_b[1] = color_a[1];
        color_b[2] = color_a[2];
        color_b[3] = color_a[3];
        color_c[0] = color_a[0];
        color_c[1] = color_a[1];
        color_c[2] = color_a[2];
        color_c[3] = color_a[3];
    }
    else if( color_c_hsl16 == TORIDRAWHSL16_FLAT )
    {
        trspk_toridraw_hsl16_to_rgba(color_a_hsl16, alpha, color_a);
        color_b[0] = color_a[0];
        color_b[1] = color_a[1];
        color_b[2] = color_a[2];
        color_b[3] = color_a[3];
        color_c[0] = color_a[0];
        color_c[1] = color_a[1];
        color_c[2] = color_a[2];
        color_c[3] = color_a[3];
    }
    else
    {
        trspk_toridraw_hsl16_to_rgba(color_a_hsl16, alpha, color_a);
        trspk_toridraw_hsl16_to_rgba(color_b_hsl16, alpha, color_b);
        trspk_toridraw_hsl16_to_rgba(color_c_hsl16, alpha, color_c);
    }
}

void
trspk_toridraw_bake_face(
    struct ToriDraw_Model* model,
    uint32_t face_index,
    const struct ToriDraw_Position* world_position,
    struct ToriDraw_Scene* ctx,
    bool invert_face_alpha,
    struct TRSPK_ToriDrawBakeFaceVerts* out)
{
    const uint32_t face_a = (uint32_t)model->face_indices_a[face_index];
    const uint32_t face_b = (uint32_t)model->face_indices_b[face_index];
    const uint32_t face_c = (uint32_t)model->face_indices_c[face_index];

    uint8_t alpha;
    if( model->face_alphas )
        alpha = invert_face_alpha ? (uint8_t)(0xFFu - model->face_alphas[face_index])
                                  : model->face_alphas[face_index];
    else
        alpha = 0xFFu;

    if( gl_face_dbg_enabled() )
    {
        g_gl_face_dbg_faces++;
        if( model->face_colors_c[face_index] == TORIDRAWHSL16_HIDDEN )
            g_gl_face_dbg_hidden++;
        if( alpha <= 1 )
            g_gl_face_dbg_alpha0++;
        if( model->face_textures && model->face_textures[face_index] != -1 )
            g_gl_face_dbg_textured++;
    }

    trspk_toridraw_face_colors(
        model->face_colors_a[face_index],
        model->face_colors_b[face_index],
        model->face_colors_c[face_index],
        alpha,
        out->color_a,
        out->color_b,
        out->color_c);

    out->tex_id = model->face_textures ? (int)model->face_textures[face_index] : -1;
    struct ToriDraw_Texture* tex = NULL;
    out->uv_mode = 0.0f;
    out->is_animated = false;
    if( out->tex_id >= 0 && ctx )
    {
        tex = ToriDraw_TextureMapGet(&ToriDraw_SceneTexState(ctx)->texture_map, out->tex_id);
        if( tex )
        {
            out->uv_mode = trspk_pack_gpu_uv_mode(tex->animation_direction, tex->animation_speed);
            out->is_animated =
                tex->animation_direction != TORIDRAW_TEXANIM_DIRECTION_NONE;
        }
    }

    out->tex_id_encoded = trspk_encode_vertex_tex_id(out->tex_id, tex);
    trspk_toridraw_uv_pnm_face(&out->uv, model, face_index);

    trspk_toridraw_world_vertex(
        world_position,
        model->vertices_x[face_a],
        model->vertices_y[face_a],
        model->vertices_z[face_a],
        &out->wx_a,
        &out->wy_a,
        &out->wz_a);
    trspk_toridraw_world_vertex(
        world_position,
        model->vertices_x[face_b],
        model->vertices_y[face_b],
        model->vertices_z[face_b],
        &out->wx_b,
        &out->wy_b,
        &out->wz_b);
    trspk_toridraw_world_vertex(
        world_position,
        model->vertices_x[face_c],
        model->vertices_y[face_c],
        model->vertices_z[face_c],
        &out->wx_c,
        &out->wy_c,
        &out->wz_c);

    if( gl_face_dbg_enabled() && (int)face_index + 1 >= model->face_count )
        gl_face_dbg_flush_model(model->face_count);
}

static void
trspk_toridraw_bake_face_ground(
    struct ToriDraw_ModelGround* ground,
    uint32_t face_index,
    const struct ToriDraw_Position* world_position,
    struct ToriDraw_Scene* ctx,
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
        out->color_a,
        out->color_b,
        out->color_c);

    out->tex_id = ground->face_textures ? (int)ground->face_textures[face_index] : -1;
    struct ToriDraw_Texture* tex = NULL;
    out->uv_mode = 0.0f;
    out->is_animated = false;
    if( out->tex_id >= 0 && ctx )
    {
        tex = ToriDraw_TextureMapGet(&ToriDraw_SceneTexState(ctx)->texture_map, out->tex_id);
        if( tex )
        {
            out->uv_mode = trspk_pack_gpu_uv_mode(tex->animation_direction, tex->animation_speed);
            out->is_animated =
                tex->animation_direction != TORIDRAW_TEXANIM_DIRECTION_NONE;
        }
    }
    out->tex_id_encoded = trspk_encode_vertex_tex_id(out->tex_id, tex);

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
        world_position,
        ground->vertices_x[face_a],
        ground->vertices_y[face_a],
        ground->vertices_z[face_a],
        &out->wx_a,
        &out->wy_a,
        &out->wz_a);
    trspk_toridraw_world_vertex(
        world_position,
        ground->vertices_x[face_b],
        ground->vertices_y[face_b],
        ground->vertices_z[face_b],
        &out->wx_b,
        &out->wy_b,
        &out->wz_b);
    trspk_toridraw_world_vertex(
        world_position,
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
    const struct ToriDraw_Position* world_position,
    struct ToriDraw_Scene* ctx,
    bool invert_face_alpha,
    struct TRSPK_ToriDrawBakeFaceVerts* out)
{
    assert(out);
    switch( model_handle.kind )
    {
    case TORIDRAWMK_MODEL:
        assert(model_handle.u.model.model);
        trspk_toridraw_bake_face(
            model_handle.u.model.model,
            face_index,
            world_position,
            ctx,
            invert_face_alpha,
            out);
        return true;
    case TORIDRAWMK_GROUND:
        assert(model_handle.u.model.ground);
        trspk_toridraw_bake_face_ground(
            model_handle.u.model.ground, face_index, world_position, ctx, out);
        return true;
    default:
        return false;
    }
}
