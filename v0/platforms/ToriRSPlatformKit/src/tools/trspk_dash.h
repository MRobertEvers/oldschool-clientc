#ifndef TORIRS_PLATFORM_KIT_TRSPK_DASH_H
#define TORIRS_PLATFORM_KIT_TRSPK_DASH_H

#include "trspk_batch16.h"
#include "trspk_batch32.h"

#ifdef __cplusplus
extern "C" {
#endif

struct DashModel;
struct DashTexture;
struct GGame;
struct ToriRSRenderCommand;

void
trspk_dash_fill_model_arrays(struct DashModel* model, TRSPK_ModelArrays* out);

/** How CPU builds UVs from PNM for textured meshes (not used by the GPU directly). */
TRSPK_UVCalculationMode
trspk_dash_uv_calculation_mode(struct DashModel* model);

void
trspk_dash_fill_rgba128(
    const struct DashTexture* tex,
    uint8_t* scratch_buffer,
    uint32_t scratch_capacity,
    const uint8_t** out_pixels,
    uint32_t* out_size);

void
trspk_dash_batch_add_model16(
    TRSPK_Batch16* batch,
    struct DashModel* model,
    uint16_t model_id,
    uint8_t segment,
    uint16_t frame_index,
    const TRSPK_BakeTransform* bake,
     TRSPK_ResourceCache* resource_cache);

void
trspk_dash_batch_add_model32(
    TRSPK_Batch32* batch,
    struct DashModel* model,
    uint16_t model_id,
    uint8_t segment,
    uint16_t frame_index,
    const TRSPK_BakeTransform* bake,
    TRSPK_ResourceCache* resource_cache);

uint32_t
trspk_dash_prepare_sorted_indices(
    struct GGame* game,
    struct DashModel* model,
    const struct ToriRSRenderCommand* cmd,
    uint32_t* out_indices,
    uint32_t max_indices);

#ifdef __cplusplus
}
#endif

#endif
