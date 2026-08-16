#include "trspk_dash.h"

#include "../../include/ToriRSPlatformKit/trspk_math.h"
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "osrs/game.h"
#include "tori_rs_render.h"

#include <string.h>

void
trspk_dash_fill_model_arrays(
    struct DashModel* model,
    TRSPK_ModelArrays* out)
{
    if( !out )
        return;
    memset(out, 0, sizeof(*out));
    if( !model )
        return;
    out->vertex_count = (uint32_t)dashmodel_vertex_count(model);
    out->vertices_x = dashmodel_vertices_x_const(model);
    out->vertices_y = dashmodel_vertices_y_const(model);
    out->vertices_z = dashmodel_vertices_z_const(model);
    out->face_count = (uint32_t)dashmodel_face_count(model);
    out->faces_a = (const uint16_t*)dashmodel_face_indices_a_const(model);
    out->faces_b = (const uint16_t*)dashmodel_face_indices_b_const(model);
    out->faces_c = (const uint16_t*)dashmodel_face_indices_c_const(model);
    out->faces_a_color_hsl16 = dashmodel_face_colors_a_const(model);
    out->faces_b_color_hsl16 = dashmodel_face_colors_b_const(model);
    out->faces_c_color_hsl16 = dashmodel_face_colors_c_const(model);
    out->face_alphas = dashmodel_face_alphas_const(model);
    out->face_infos = dashmodel_face_infos_const(model);
    if( dashmodel_has_textures(model) )
    {
        out->faces_textures = (const int16_t*)dashmodel_face_textures_const(model);
        out->textured_faces = (const uint16_t*)dashmodel_face_texture_coords_const(model);
        out->textured_faces_a = (const uint16_t*)dashmodel_textured_p_coordinate_const(model);
        out->textured_faces_b = (const uint16_t*)dashmodel_textured_m_coordinate_const(model);
        out->textured_faces_c = (const uint16_t*)dashmodel_textured_n_coordinate_const(model);
    }
}

TRSPK_UVCalculationMode
trspk_dash_uv_calculation_mode(struct DashModel* model)
{
    return dashmodel__is_ground_va(model) ? TRSPK_UV_CALC_FIRST_FACE
                                          : TRSPK_UV_CALC_TEXTURED_FACE_ARRAY;
}

void
trspk_dash_fill_rgba128(
    const struct DashTexture* tex,
    uint8_t* scratch_buffer,
    uint32_t scratch_capacity,
    const uint8_t** out_pixels,
    uint32_t* out_size)
{
    *out_pixels = NULL;
    if( out_size )
        *out_size = 0;
    if( !tex || !tex->texels || !scratch_buffer )
        return;
    const int w = tex->width;
    const int h = tex->height;
    if( (w != 128 || h != 128) && (w != 64 || h != 64) )
        return;
    const uint32_t required = (uint32_t)w * (uint32_t)h * 4u;
    const uint32_t output_required =
        w == 64 ? TRSPK_TEXTURE_DIMENSION * TRSPK_TEXTURE_DIMENSION * TRSPK_ATLAS_BYTES_PER_PIXEL
                : required;
    if( scratch_capacity < output_required )
        return;
    for( int p = 0; p < w * h; ++p )
    {
        const int pix = tex->texels[p];
        scratch_buffer[(size_t)p * 4u + 0u] = (uint8_t)((pix >> 16) & 0xFF);
        scratch_buffer[(size_t)p * 4u + 1u] = (uint8_t)((pix >> 8) & 0xFF);
        scratch_buffer[(size_t)p * 4u + 2u] = (uint8_t)(pix & 0xFF);
        scratch_buffer[(size_t)p * 4u + 3u] = (uint8_t)((pix >> 24) & 0xFF);
    }
    if( w == 64 )
    {
        static uint8_t upscaled[128u * 128u * 4u];
        trspk_upscale_64_to_128_rgba(scratch_buffer, upscaled);
        for( uint32_t i = 0; i < 128u * 128u * 4u; ++i )
            scratch_buffer[i] = upscaled[i];
        if( out_size )
            *out_size = 128u * 128u * 4u;
    }
    else
    {
        if( out_size )
            *out_size = required;
    }
    *out_pixels = scratch_buffer;
}

void
trspk_dash_batch_add_model16(
    TRSPK_Batch16* batch,
    struct DashModel* model,
    uint16_t model_id,
    uint8_t segment,
    uint16_t frame_index,
    const TRSPK_BakeTransform* bake,
    TRSPK_ResourceCache* resource_cache)
{
    if( !batch || !model || model_id == 0 )
        return;
    if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    if( dashmodel_has_textures(model) )
    {
        trspk_batch16_add_model_textured(
            batch,
            model_id,
            segment,
            frame_index,
            (uint32_t)dashmodel_vertex_count(model),
            dashmodel_vertices_x_const(model),
            dashmodel_vertices_y_const(model),
            dashmodel_vertices_z_const(model),
            (uint32_t)dashmodel_face_count(model),
            (const uint16_t*)dashmodel_face_indices_a_const(model),
            (const uint16_t*)dashmodel_face_indices_b_const(model),
            (const uint16_t*)dashmodel_face_indices_c_const(model),
            dashmodel_face_colors_a_const(model),
            dashmodel_face_colors_b_const(model),
            dashmodel_face_colors_c_const(model),
            (const int16_t*)dashmodel_face_textures_const(model),
            (const uint16_t*)dashmodel_face_texture_coords_const(model),
            (const uint16_t*)dashmodel_textured_p_coordinate_const(model),
            (const uint16_t*)dashmodel_textured_m_coordinate_const(model),
            (const uint16_t*)dashmodel_textured_n_coordinate_const(model),
            dashmodel_face_alphas_const(model),
            dashmodel_face_infos_const(model),
            trspk_dash_uv_calculation_mode(model),
            resource_cache,
            bake);
    }
    else
    {
        trspk_batch16_add_model(
            batch,
            model_id,
            segment,
            frame_index,
            (uint32_t)dashmodel_vertex_count(model),
            dashmodel_vertices_x_const(model),
            dashmodel_vertices_y_const(model),
            dashmodel_vertices_z_const(model),
            (uint32_t)dashmodel_face_count(model),
            (const uint16_t*)dashmodel_face_indices_a_const(model),
            (const uint16_t*)dashmodel_face_indices_b_const(model),
            (const uint16_t*)dashmodel_face_indices_c_const(model),
            dashmodel_face_colors_a_const(model),
            dashmodel_face_colors_b_const(model),
            dashmodel_face_colors_c_const(model),
            dashmodel_face_alphas_const(model),
            dashmodel_face_infos_const(model),
            bake);
    }
}

void
trspk_dash_batch_add_model32(
    TRSPK_Batch32* batch,
    struct DashModel* model,
    uint16_t model_id,
    uint8_t segment,
    uint16_t frame_index,
    const TRSPK_BakeTransform* bake,
    TRSPK_ResourceCache* resource_cache)
{
    if( !batch || !model || model_id == 0 )
        return;
    if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    if( dashmodel_has_textures(model) )
    {
        trspk_batch32_add_model_textured(
            batch,
            model_id,
            segment,
            frame_index,
            (uint32_t)dashmodel_vertex_count(model),
            dashmodel_vertices_x_const(model),
            dashmodel_vertices_y_const(model),
            dashmodel_vertices_z_const(model),
            (uint32_t)dashmodel_face_count(model),
            (const uint16_t*)dashmodel_face_indices_a_const(model),
            (const uint16_t*)dashmodel_face_indices_b_const(model),
            (const uint16_t*)dashmodel_face_indices_c_const(model),
            dashmodel_face_colors_a_const(model),
            dashmodel_face_colors_b_const(model),
            dashmodel_face_colors_c_const(model),
            (const int16_t*)dashmodel_face_textures_const(model),
            (const uint16_t*)dashmodel_face_texture_coords_const(model),
            (const uint16_t*)dashmodel_textured_p_coordinate_const(model),
            (const uint16_t*)dashmodel_textured_m_coordinate_const(model),
            (const uint16_t*)dashmodel_textured_n_coordinate_const(model),
            dashmodel_face_alphas_const(model),
            dashmodel_face_infos_const(model),
            trspk_dash_uv_calculation_mode(model),
            resource_cache,
            bake);
    }
    else
    {
        trspk_batch32_add_model(
            batch,
            model_id,
            segment,
            frame_index,
            (uint32_t)dashmodel_vertex_count(model),
            dashmodel_vertices_x_const(model),
            dashmodel_vertices_y_const(model),
            dashmodel_vertices_z_const(model),
            (uint32_t)dashmodel_face_count(model),
            (const uint16_t*)dashmodel_face_indices_a_const(model),
            (const uint16_t*)dashmodel_face_indices_b_const(model),
            (const uint16_t*)dashmodel_face_indices_c_const(model),
            dashmodel_face_colors_a_const(model),
            dashmodel_face_colors_b_const(model),
            dashmodel_face_colors_c_const(model),
            dashmodel_face_alphas_const(model),
            dashmodel_face_infos_const(model),
            bake);
    }
}

uint32_t
trspk_dash_prepare_sorted_indices(
    struct GGame* game,
    struct DashModel* model,
    const struct ToriRSRenderCommand* cmd,
    uint32_t* out_indices,
    uint32_t max_indices)
{
    if( !game || !model || !cmd || !out_indices || max_indices == 0 )
        return 0;
    struct DashPosition draw_position = cmd->_model_draw.position;
    int face_order_count = dash3d_prepare_projected_face_order(
        game->sys_dash, model, &draw_position, game->view_port, game->camera);
    const int* face_order = dash3d_projected_face_order(game->sys_dash, &face_order_count);
    const int face_count = dashmodel_face_count(model);
    uint32_t count = 0;
    for( int fi = 0; fi < face_order_count; ++fi )
    {
        const int face_index = face_order ? face_order[fi] : fi;
        if( face_index < 0 || face_index >= face_count )
            continue;
        if( count + 3u > max_indices )
            break;
        const uint32_t b = (uint32_t)face_index * 3u;
        out_indices[count++] = b;
        out_indices[count++] = b + 1u;
        out_indices[count++] = b + 2u;
    }
    return count;
}
