#ifndef TORIRS_PLATFORM_KIT_TRSPK_DYNAMIC_SLOTMAP16_H
#define TORIRS_PLATFORM_KIT_TRSPK_DYNAMIC_SLOTMAP16_H

#include "../../include/ToriRSPlatformKit/trspk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TRSPK_DynamicSlotmap16 TRSPK_DynamicSlotmap16;

TRSPK_DynamicSlotmap16*
trspk_dynamic_slotmap16_create(void);
void
trspk_dynamic_slotmap16_destroy(TRSPK_DynamicSlotmap16* map);
void
trspk_dynamic_slotmap16_reset(TRSPK_DynamicSlotmap16* map);

bool
trspk_dynamic_slotmap16_alloc(
    TRSPK_DynamicSlotmap16* map,
    uint32_t vertex_count,
    uint32_t index_count,
    TRSPK_DynamicSlotHandle* out_handle,
    uint8_t* out_chunk,
    uint32_t* out_vbo_offset,
    uint32_t* out_ebo_offset);

void
trspk_dynamic_slotmap16_free(
    TRSPK_DynamicSlotmap16* map,
    TRSPK_DynamicSlotHandle handle);

bool
trspk_dynamic_slotmap16_get_slot(
    const TRSPK_DynamicSlotmap16* map,
    TRSPK_DynamicSlotHandle handle,
    uint8_t* out_chunk,
    uint32_t* out_vbo_offset,
    uint32_t* out_ebo_offset,
    uint32_t* out_vertex_count,
    uint32_t* out_index_count);

uint32_t
trspk_dynamic_slotmap16_chunk_count(const TRSPK_DynamicSlotmap16* map);

#ifdef __cplusplus
}
#endif

#endif
