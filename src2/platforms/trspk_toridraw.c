#include "trspk_toridraw.h"

#include "platformkit/core/trspk_vbo.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_hsl16.h"
#include "toridraw/toridraw_math.h"
#include "toridraw/toridraw_model.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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

static inline void
write_vertex(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    float color[4],
    float u,
    float v,
    float tex_id)
{
    switch( vbo->format )
    {
    case TRSPK_VERTEX_FORMAT_D3D9:
        trspk_vbo_write_vertex_d3d9(vbo, index, x, y, z, color, u, v, tex_id);
        break;
    case TRSPK_VERTEX_FORMAT_OPENGL3:
        trspk_vbo_write_vertex_opengl3(vbo, index, x, y, z, color, u, v, tex_id);
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        trspk_vbo_write_vertex_webgl1(vbo, index, x, y, z, color, u, v, tex_id);
        break;
    case TRSPK_VERTEX_FORMAT_TRSPK:
        assert(0);
        break;
    default:
        assert(0);
        break;
    }
}

void
trspk_toridraw_uv_pnm_face(
    struct UVFaceCoords* uv_pnm_out,
    struct ToriDraw_Model* model,
    uint32_t face_index)
{
    const uint32_t face_a = model->face_indices_a[face_index];
    const uint32_t face_b = model->face_indices_b[face_index];
    const uint32_t face_c = model->face_indices_c[face_index];

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
        model->vertices_x[p_vertex],
        model->vertices_y[p_vertex],
        model->vertices_z[p_vertex],
        model->vertices_x[m_vertex],
        model->vertices_y[m_vertex],
        model->vertices_z[m_vertex],
        model->vertices_x[n_vertex],
        model->vertices_y[n_vertex],
        model->vertices_z[n_vertex],
        model->vertices_x[face_a],
        model->vertices_y[face_a],
        model->vertices_z[face_a],
        model->vertices_x[face_b],
        model->vertices_y[face_b],
        model->vertices_z[face_b],
        model->vertices_x[face_c],
        model->vertices_y[face_c],
        model->vertices_z[face_c]);
}

static inline struct ToriDraw_Model*
get_model(struct ToriDraw_ModelHandle model_handle)
{
    return model_handle.u.model.model;
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
    struct ToriDraw_Model* model,
    uint32_t face_index,
    bool invert_face_alpha,
    float color_a[4],
    float color_b[4],
    float color_c[4])
{
    const uint16_t color_a_hsl16 = model->face_colors_a[face_index];
    const uint16_t color_b_hsl16 = model->face_colors_b[face_index];
    const uint16_t color_c_hsl16 = model->face_colors_c[face_index];
    uint8_t alpha;
    if( model->face_alphas )
        alpha = invert_face_alpha ? (uint8_t)(0xFFu - model->face_alphas[face_index])
                                  : model->face_alphas[face_index];
    else
        alpha = 0xFFu;

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

    trspk_toridraw_face_colors(
        model, face_index, invert_face_alpha, out->color_a, out->color_b, out->color_c);

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
}

void
trspk_toridraw_vbo_set(
    struct TRSPK_VBO* vbo,
    struct ToriDraw_ModelHandle model_handle)
{
    struct ToriDraw_Model* model = get_model(model_handle);
    const uint32_t gpu_vertex_count = (uint32_t)model->face_count * 3u;
    trspk_vbo_ensure_capacity(vbo, gpu_vertex_count);

    uint32_t face_index = 0;
    for( uint32_t i = 0; i < gpu_vertex_count; i += 3, face_index++ )
    {
        const uint32_t face_a = model->face_indices_a[face_index];
        const uint32_t face_b = model->face_indices_b[face_index];
        const uint32_t face_c = model->face_indices_c[face_index];

        float color_a[4], color_b[4], color_c[4];
        trspk_toridraw_face_colors(model, face_index, false, color_a, color_b, color_c);

        struct UVFaceCoords uv;
        trspk_toridraw_uv_pnm_face(&uv, model, face_index);

        write_vertex(
            vbo,
            i + 0,
            model->vertices_x[face_a],
            model->vertices_y[face_a],
            model->vertices_z[face_a],
            color_a,
            uv.u1,
            uv.v1,
            -1.0f);
        write_vertex(
            vbo,
            i + 1,
            model->vertices_x[face_b],
            model->vertices_y[face_b],
            model->vertices_z[face_b],
            color_b,
            uv.u2,
            uv.v2,
            -1.0f);
        write_vertex(
            vbo,
            i + 2,
            model->vertices_x[face_c],
            model->vertices_y[face_c],
            model->vertices_z[face_c],
            color_c,
            uv.u3,
            uv.v3,
            -1.0f);
    }
}
