#include "trspk_toridraw.h"

#include "graphics/uv_pnm.h"
#include "platformkit/core/trspk_vbo.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_hsl16.h"
#include "toridraw/toridraw_math.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_types.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

bool
trspk_toridraw_has_textures(const struct ToriDraw_Model* model)
{
    return model && model->face_textures && model->textured_face_count > 0;
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

static void
hsl16_to_rgbaf(
    uint16_t hsl16,
    uint8_t alpha,
    float rgba[4])
{
    uint32_t rgb = ToriDraw_Hsl16ToRgb(hsl16);
    rgba[0] = (float)((rgb >> 16) & 0xFFu) / 255.0f;
    rgba[1] = (float)((rgb >> 8) & 0xFFu) / 255.0f;
    rgba[2] = (float)(rgb & 0xFFu) / 255.0f;
    rgba[3] = (float)alpha / 255.0f;
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

static void
uv_pnm_model_face(
    struct UVFaceCoords* uv_pnm_out,
    struct ToriDraw_Model* model,
    uint32_t face_index)
{
    uint32_t face_a = model->face_indices_a[face_index];
    uint32_t face_b = model->face_indices_b[face_index];
    uint32_t face_c = model->face_indices_c[face_index];

    uint32_t texture_face = face_index;
    uint32_t p_vertex = face_a;
    uint32_t m_vertex = face_b;
    uint32_t n_vertex = face_c;

    if( model->face_texture_coords && model->face_texture_coords[face_index] != -1 )
    {
        assert(model->textured_p_coordinate != NULL);
        assert(model->textured_m_coordinate != NULL);
        assert(model->textured_n_coordinate != NULL);

        texture_face = model->face_texture_coords[face_index];

        p_vertex = model->textured_p_coordinate[texture_face];
        m_vertex = model->textured_m_coordinate[texture_face];
        n_vertex = model->textured_n_coordinate[texture_face];
    }
    else
    {
        texture_face = face_index;
        p_vertex = model->face_indices_a[texture_face];
        m_vertex = model->face_indices_b[texture_face];
        n_vertex = model->face_indices_c[texture_face];
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

    const int yaw = ToriDraw_NormalizeAngle(world_position->yaw);
    const int pitch = ToriDraw_NormalizeAngle(world_position->pitch);
    const int roll = ToriDraw_NormalizeAngle(world_position->roll);

    const int cy = ToriDraw_Cos(yaw);
    const int sy = ToriDraw_Sin(yaw);
    const int cp = ToriDraw_Cos(pitch);
    const int sp = ToriDraw_Sin(pitch);
    const int cr = ToriDraw_Cos(roll);
    const int sr = ToriDraw_Sin(roll);

    const int xr = (vx * cy + vz * sy) >> 16;
    const int zr = (vz * cy - vx * sy) >> 16;

    const int yr = (vy * cp - zr * sp) >> 16;
    const int zr2 = (vy * sp + zr * cp) >> 16;

    const int xf = (xr * cr + yr * sr) >> 16;
    const int yf = (yr * cr - xr * sr) >> 16;

    *out_x = (float)(xf + world_position->x);
    *out_y = (float)(yf + world_position->y);
    *out_z = (float)(zr2 + world_position->z);
}

void
trspk_toridraw_vbo_set(
    struct TRSPK_VBO* vbo,
    struct ToriDraw_ModelHandle model_handle)
{
    struct ToriDraw_Model* model = get_model(model_handle);
    uint32_t gpu_vertex_count = (uint32_t)model->face_count * 3u;
    trspk_vbo_ensure_capacity(vbo, gpu_vertex_count);

    uint32_t face_index = 0;
    for( uint32_t i = 0; i < gpu_vertex_count; i += 3, face_index++ )
    {
        uint32_t face_a = model->face_indices_a[face_index];
        uint32_t face_b = model->face_indices_b[face_index];
        uint32_t face_c = model->face_indices_c[face_index];

        uint16_t color_a_hsl16 = model->face_colors_a[face_index];
        uint16_t color_b_hsl16 = model->face_colors_b[face_index];
        uint16_t color_c_hsl16 = model->face_colors_c[face_index];
        uint8_t alpha = model->face_alphas ? model->face_alphas[face_index] : 0xFFu;

        float color_a[4], color_b[4], color_c[4];
        hsl16_to_rgbaf(color_a_hsl16, alpha, color_a);
        hsl16_to_rgbaf(color_b_hsl16, alpha, color_b);
        hsl16_to_rgbaf(color_c_hsl16, alpha, color_c);

        struct UVFaceCoords uv;
        uv_pnm_model_face(&uv, model, face_index);

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