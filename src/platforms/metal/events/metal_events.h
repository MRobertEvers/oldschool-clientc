#ifndef PLATFORMS_METAL_EVENTS_METAL_EVENTS_H
#define PLATFORMS_METAL_EVENTS_METAL_EVENTS_H

#include "platforms/common/gpu_3d.h"
#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"
#include "tori_rs_render.h"

struct MetalBatchBuffer;
struct MetalRenderCtx;
struct ToriRSRenderCommand;
struct LogicalViewportRect;
struct DashModel;

void
metal_cache2_batch_push_model_mesh(
    MetalRenderCtx* ctx,
    MetalBatchBuffer& buf,
    struct DashModel* model,
    int model_id,
    SceneBatchId scene_batch_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    const GPU3DBakeTransform& bake);

void
metal_event_batch3d_begin(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
metal_event_batch3d_model_add(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
metal_event_batch3d_end(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
metal_event_batch3d_clear(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
metal_event_draw_model(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
metal_event_res_tex_load(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
metal_event_batch2d_tex_begin(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
metal_event_batch2d_tex_end(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

#endif
