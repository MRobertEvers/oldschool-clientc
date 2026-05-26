#include "trspk_toridraw.h"

#include "platforms/ToriRSPlatformKit/src/tools/trspk_batch32.h"

#include <string.h>

const struct ToriDraw_Model*
trspk_toridraw_model_from_handle(const struct ToriDraw_ModelHandle* handle)
{
    if( !handle || handle->kind != TORIDRAWMK_MODEL )
        return NULL;
    return handle->u.model.model;
}

void
trspk_toridraw_fill_model_arrays(
    const struct ToriDraw_Model* model,
    TRSPK_ModelArrays* out)
{
    if( !out )
        return;
    memset(out, 0, sizeof(*out));
    if( !model )
        return;

    out->vertex_count = (uint32_t)model->vertex_count;
    out->vertices_x = model->vertices_x;
    out->vertices_y = model->vertices_y;
    out->vertices_z = model->vertices_z;
    out->face_count = (uint32_t)model->face_count;
    out->faces_a = (const uint16_t*)model->face_indices_a;
    out->faces_b = (const uint16_t*)model->face_indices_b;
    out->faces_c = (const uint16_t*)model->face_indices_c;
    out->faces_a_color_hsl16 = model->face_colors_a;
    out->faces_b_color_hsl16 = model->face_colors_b;
    out->faces_c_color_hsl16 = model->face_colors_c;
    out->face_alphas = model->face_alphas;
    out->face_infos = model->face_infos;

    if( model->face_textures && model->textured_face_count > 0 )
    {
        out->faces_textures = model->face_textures;
        out->textured_faces = (const uint16_t*)model->face_texture_coords;
        out->textured_faces_a = (const uint16_t*)model->textured_p_coordinate;
        out->textured_faces_b = (const uint16_t*)model->textured_m_coordinate;
        out->textured_faces_c = (const uint16_t*)model->textured_n_coordinate;
    }
}

bool
trspk_toridraw_has_textures(const struct ToriDraw_Model* model)
{
    return model && model->face_textures && model->textured_face_count > 0;
}

TRSPK_UVCalculationMode
trspk_toridraw_uv_calculation_mode(const struct ToriDraw_ModelHandle* handle)
{
    if( handle && handle->kind == TORIDRAWMK_GROUND )
        return TRSPK_UV_CALC_FIRST_FACE;
    return TRSPK_UV_CALC_TEXTURED_FACE_ARRAY;
}

void
trspk_toridraw_batch_add_model32(
    struct TRSPK_Batch32* batch,
    const struct ToriDraw_Model* model,
    uint16_t model_id,
    uint8_t segment,
    uint16_t frame_index,
    const TRSPK_BakeTransform* bake,
    struct TRSPK_ResourceCache* resource_cache)
{
    if( !batch || !model || model->face_count <= 0 )
        return;
    if( !model->face_colors_a || !model->face_colors_b || !model->face_colors_c ||
        !model->vertices_x || !model->vertices_y || !model->vertices_z || !model->face_indices_a ||
        !model->face_indices_b || !model->face_indices_c )
        return;

    if( trspk_toridraw_has_textures(model) )
    {
        trspk_batch32_add_model_textured(
            batch,
            model_id,
            segment,
            frame_index,
            (uint32_t)model->vertex_count,
            model->vertices_x,
            model->vertices_y,
            model->vertices_z,
            (uint32_t)model->face_count,
            (const uint16_t*)model->face_indices_a,
            (const uint16_t*)model->face_indices_b,
            (const uint16_t*)model->face_indices_c,
            model->face_colors_a,
            model->face_colors_b,
            model->face_colors_c,
            model->face_textures,
            (const uint16_t*)model->face_texture_coords,
            (const uint16_t*)model->textured_p_coordinate,
            (const uint16_t*)model->textured_m_coordinate,
            (const uint16_t*)model->textured_n_coordinate,
            model->face_alphas,
            model->face_infos,
            TRSPK_UV_CALC_TEXTURED_FACE_ARRAY,
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
            (uint32_t)model->vertex_count,
            model->vertices_x,
            model->vertices_y,
            model->vertices_z,
            (uint32_t)model->face_count,
            (const uint16_t*)model->face_indices_a,
            (const uint16_t*)model->face_indices_b,
            (const uint16_t*)model->face_indices_c,
            model->face_colors_a,
            model->face_colors_b,
            model->face_colors_c,
            model->face_alphas,
            model->face_infos,
            bake);
    }
}
