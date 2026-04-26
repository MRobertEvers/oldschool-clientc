#ifndef PLATFORMS_WEBGL1_EVENTS_WEBGL1_EVENTS_H
#define PLATFORMS_WEBGL1_EVENTS_WEBGL1_EVENTS_H

#ifdef __EMSCRIPTEN__

#    include "platforms/common/gpu_3d.h"
#    include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"
#    include "tori_rs_render.h"

struct WebGL1BatchBuffer;
struct WebGL1RenderCtx;
struct ToriRSRenderCommand;
struct DashModel;

void
webgl1_cache2_batch_push_model_mesh(
    WebGL1RenderCtx* ctx,
    WebGL1BatchBuffer& buf,
    struct DashModel* model,
    int model_id,
    SceneBatchId scene_batch_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    const GPU3DBakeTransform& bake);

void
webgl1_event_batch3d_begin(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
webgl1_event_batch3d_model_add(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
webgl1_event_batch3d_end(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
webgl1_event_batch3d_clear(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
webgl1_event_draw_model(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
webgl1_event_res_tex_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
webgl1_event_batch2d_tex_begin(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

void
webgl1_event_batch2d_tex_end(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd);

#endif

#endif
