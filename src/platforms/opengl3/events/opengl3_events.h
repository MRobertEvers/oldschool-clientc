#ifndef PLATFORMS_OPENGL3_EVENTS_OPENGL3_EVENTS_H
#define PLATFORMS_OPENGL3_EVENTS_OPENGL3_EVENTS_H

#include "platforms/common/gpu_3d.h"
#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"
#include "platforms/opengl3/opengl3_ctx.h"
#include "tori_rs_render.h"

struct OpenGL3BatchBuffer;
struct DashModel;

void
opengl3_event_cache2_batch_push_model_mesh(
    OpenGL3RenderCtx* ctx,
    OpenGL3BatchBuffer& buf,
    struct DashModel* model,
    int model_id,
    SceneBatchId scene_batch_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    const GPU3DBakeTransform& bake);

void
opengl3_event_clear_rect(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

void
opengl3_event_begin_3d(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd,
    const LogicalViewportRect* default_logical_vp);

void
opengl3_event_end_3d(OpenGL3RenderCtx* ctx);

void
opengl3_event_texture_load(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

void
opengl3_event_model_load(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

void
opengl3_event_model_unload(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

void
opengl3_event_model_animation_load(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

void
opengl3_event_batch3d_begin(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

void
opengl3_event_batch3d_model_add(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

void
opengl3_event_batch3d_end(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

void
opengl3_event_batch3d_clear(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

void
opengl3_event_draw_model(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

void
opengl3_event_res_tex_load(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd);

#endif
