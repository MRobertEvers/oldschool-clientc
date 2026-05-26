#ifndef TRSPK_TORIDRAW_H
#define TRSPK_TORIDRAW_H

#include "platforms/ToriRSPlatformKit/include/ToriRSPlatformKit/trspk_types.h"
#include "toridraw/toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

struct TRSPK_Batch32;
struct TRSPK_ResourceCache;

void
trspk_toridraw_fill_model_arrays(
    const struct ToriDraw_Model* model,
    TRSPK_ModelArrays* out);

bool
trspk_toridraw_has_textures(const struct ToriDraw_Model* model);

TRSPK_UVCalculationMode
trspk_toridraw_uv_calculation_mode(const struct ToriDraw_ModelHandle* handle);

const struct ToriDraw_Model*
trspk_toridraw_model_from_handle(const struct ToriDraw_ModelHandle* handle);

void
trspk_toridraw_fill_rgba128(
    const struct ToriDraw_Texture* tex,
    uint8_t* scratch_buffer,
    uint32_t scratch_capacity,
    const uint8_t** out_pixels,
    uint32_t* out_size);

void
trspk_toridraw_batch_add_model32(
    struct TRSPK_Batch32* batch,
    const struct ToriDraw_Model* model,
    uint16_t model_id,
    uint8_t segment,
    uint16_t frame_index,
    const TRSPK_BakeTransform* bake,
    struct TRSPK_ResourceCache* resource_cache);

#endif
