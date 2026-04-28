#ifndef TORIRS_PLATFORM_KIT_TRSPK_DYNAMIC_SLOTMAP32_H
#define TORIRS_PLATFORM_KIT_TRSPK_DYNAMIC_SLOTMAP32_H

#include "../../include/ToriRSPlatformKit/trspk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TRSPK_DynamicSlotmap32 TRSPK_DynamicSlotmap32;

TRSPK_DynamicSlotmap32*
trspk_dynamic_slotmap32_create(
    uint32_t initial_vertex_capacity,
    uint32_t initial_index_capacity);
void
trspk_dynamic_slotmap32_destroy(TRSPK_DynamicSlotmap32* map);
void
trspk_dynamic_slotmap32_reset(TRSPK_DynamicSlotmap32* map);

bool
trspk_dynamic_slotmap32_alloc(
    TRSPK_DynamicSlotmap32* map,
    uint32_t vertex_count,
    uint32_t index_count,
    TRSPK_DynamicSlotHandle* out_handle,
    uint32_t* out_vbo_offset,
    uint32_t* out_ebo_offset);

void
trspk_dynamic_slotmap32_free(
    TRSPK_DynamicSlotmap32* map,
    TRSPK_DynamicSlotHandle handle);

bool
trspk_dynamic_slotmap32_get_slot(
    const TRSPK_DynamicSlotmap32* map,
    TRSPK_DynamicSlotHandle handle,
    uint32_t* out_vbo_offset,
    uint32_t* out_ebo_offset,
    uint32_t* out_vertex_count,
    uint32_t* out_index_count);

uint32_t
trspk_dynamic_slotmap32_vertex_capacity(const TRSPK_DynamicSlotmap32* map);
uint32_t
trspk_dynamic_slotmap32_index_capacity(const TRSPK_DynamicSlotmap32* map);

#ifdef __cplusplus
}
#endif

#endif
