#ifndef TORIRS_PLACEMENT_CHAIN_FORMAT_H
#define TORIRS_PLACEMENT_CHAIN_FORMAT_H
#include "platform/platform_renderer_gles2_placement.h"
#define PLACEMENT_CHAIN_MAGIC 0x504c4331u
#define PLACEMENT_CHAIN_PREFETCH 0x504c4350u
#define PLACEMENT_CHAIN_CALL 0x504c4343u
#define PLACEMENT_CHAIN_END 0x504c4345u
struct PlacementChainHeader
{
    uint32_t magic, version, frame, elements, batches, pages, epoch;
};
struct PlacementChainBatch
{
    uint32_t active, has_cpu, entries, page_capacity;
};
struct PlacementChainPage
{
    uint32_t valid, offset;
};
struct PlacementChainCall
{
    uint32_t magic;
    int32_t element_id, anim_index, pose_id, result;
    struct GLES2StaticPrimary placement;
};
_Static_assert(
    sizeof(struct TRSPK_Batch16Entry) == 24,
    "captured entry layout");
_Static_assert(
    sizeof(struct PlacementChainCall) == 52,
    "captured query layout");
#endif
