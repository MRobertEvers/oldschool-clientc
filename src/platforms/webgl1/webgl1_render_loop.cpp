#ifdef __EMSCRIPTEN__

#include "platforms/common/pass_3d_builder/gpu_3d_cache2_webgl1.h"
#include "platforms/common/pass_3d_builder/pass_3d_builder2_webgl1.h"
#include "platforms/common/torirs_nk_ui_bridge.h"
#include "platforms/common/torirs_nuklear_debug_panel.h"
#include "platforms/platform_impl2_sdl2_renderer_webgl1.h"
#include "platforms/webgl1/ctx.h"

#include <SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TORIRS_NK_SDL_GLES2_IMPLEMENTATION
#include "nuklear/backends/sdl_opengles2/nuklear_torirs_sdl_gles2.h"

extern "C" {
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "osrs/game.h"
#include "tori_rs_render.h"
}

#include <GLES2/gl2.h>

static bool
webgl1_dash_model_needs_sorted_face_indices(struct DashModel* model)
{
    if( dashmodel_face_priorities(model) )
        return true;
    const int fc = dashmodel_face_count(model);
    const uint8_t* al = dashmodel_face_alphas_const(model);
    if( !al || fc <= 0 )
        return false;
    for( int i = 0; i < fc; ++i )
    {
        if( al[i] < 255u )
            return true;
    }
    return false;
}

static bool
webgl1_projected_face_order_is_strictly_sequential(
    const int* face_order,
    int face_order_count,
    int face_count)
{
    if( !face_order || face_order_count != face_count || face_count <= 0 )
        return false;
    for( int fi = 0; fi < face_order_count; ++fi )
    {
        if( face_order[fi] != fi )
            return false;
    }
    return true;
}

static struct nk_context* s_webgl1_nk = nullptr;
static Uint64 s_webgl1_ui_prev_perf = 0;

/** Nearest-neighbor 64×64 RGBA → 128×128 for GPU3DCache2 fixed tile size (matches Metal path). */
static void
webgl1_rgba64_nearest_to_128(
    const uint8_t* src64,
    uint8_t* dst128)
{
    const int S = 64;
    const int D = 128;
    for( int y = 0; y < S; ++y )
    {
        for( int x = 0; x < S; ++x )
        {
            const size_t si = ((size_t)y * (size_t)S + (size_t)x) * 4u;
            const uint8_t r = src64[si + 0];
            const uint8_t g = src64[si + 1];
            const uint8_t b = src64[si + 2];
            const uint8_t a = src64[si + 3];
            for( int dy = 0; dy < 2; ++dy )
            {
                for( int dx = 0; dx < 2; ++dx )
                {
                    const size_t di = (((size_t)y * 2u + (size_t)dy) * (size_t)D +
                                       ((size_t)x * 2u + (size_t)dx)) *
                                      4u;
                    dst128[di + 0] = r;
                    dst128[di + 1] = g;
                    dst128[di + 2] = b;
                    dst128[di + 3] = a;
                }
            }
        }
    }
}

static void
webgl1_delete_gl_buffer(GPUResourceHandle h)
{
    if( !h )
        return;
    GLuint name = (GLuint)(uintptr_t)h;
    glDeleteBuffers(1, &name);
}

static void
webgl1_write_atlas_tile_slot(
    struct Platform2_SDL2_Renderer_WebGL1* r,
    uint16_t tex_id,
    const struct DashTexture* tex_nullable)
{
    if( !r || tex_id >= (uint16_t)MAX_TEXTURES )
        return;
    if( r->tiles_cpu.size() < (size_t)MAX_TEXTURES )
        r->tiles_cpu.resize((size_t)MAX_TEXTURES);
    const AtlasUVRect& uv = r->model_cache2.GetAtlasUVRect(tex_id);
    WebGL1AtlasTile tile = {};
    tile.u0 = uv.u_offset;
    tile.v0 = uv.v_offset;
    tile.du = uv.u_scale;
    tile.dv = uv.v_scale;
    if( tex_nullable )
    {
        const float s = webgl1_texture_animation_signed(
            tex_nullable->animation_direction, tex_nullable->animation_speed);
        if( s >= 0.0f )
        {
            tile.anim_u = s;
            tile.anim_v = 0.0f;
        }
        else
        {
            tile.anim_u = 0.0f;
            tile.anim_v = -s;
        }
        tile.opaque = tex_nullable->opaque ? 1.0f : 0.0f;
    }
    else
    {
        tile.anim_u = 0.0f;
        tile.anim_v = 0.0f;
        tile.opaque = 1.0f;
    }
    tile._pad = 0.0f;
    r->tiles_cpu[(size_t)tex_id] = tile;
    r->tiles_dirty = true;
}

static void
webgl1_fill_all_atlas_tiles_default(struct Platform2_SDL2_Renderer_WebGL1* r)
{
    for( uint16_t i = 0; i < (uint16_t)MAX_TEXTURES; ++i )
        webgl1_write_atlas_tile_slot(r, i, nullptr);
}

static void
webgl1_refresh_atlas_texture(struct Platform2_SDL2_Renderer_WebGL1* r)
{
    if( !r )
        return;
    GLuint nt = GPU3DCache2SubmitAtlasWebGL1(r->model_cache2);
    if( nt == 0u )
        return;
    if( r->cache2_atlas_tex != 0u )
        glDeleteTextures(1, &r->cache2_atlas_tex);
    r->cache2_atlas_tex = nt;
}

void
webgl1_atlas_resources_init(struct Platform2_SDL2_Renderer_WebGL1* r)
{
    if( !r )
        return;
    /* Match Metal `metal_init.mm`: allocate CPU atlas grid before `LoadTexture128` (needs
     * non-zero `atlas.atlas_width` for column count). */
    if( !r->model_cache2.HasBufferedAtlasData() )
    {
        r->model_cache2.InitAtlas(
            0, (uint32_t)kWebGL1WorldAtlasSize, (uint32_t)kWebGL1WorldAtlasSize);
    }
    r->tiles_cpu.assign((size_t)MAX_TEXTURES, WebGL1AtlasTile{});
    webgl1_fill_all_atlas_tiles_default(r);
    webgl1_refresh_atlas_texture(r);
    r->tiles_dirty = true;
}

void
webgl1_atlas_resources_shutdown(struct Platform2_SDL2_Renderer_WebGL1* r)
{
    if( !r )
        return;
    if( r->cache2_atlas_tex != 0u )
    {
        glDeleteTextures(1, &r->cache2_atlas_tex);
        r->cache2_atlas_tex = 0u;
    }
    r->tiles_cpu.clear();
    r->model_cache2.UnloadAtlas();
}

void
webgl1_cache2_batch_push_model_mesh(
    WebGL1RenderCtx* ctx,
    struct DashModel* model,
    int model_id,
    SceneBatchId scene_batch_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index)
{
    if( !ctx || !ctx->renderer || !model || model_id <= 0 )
        return;

    if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    const vertexint_t* vertices_x = dashmodel_vertices_x_const(model);
    const vertexint_t* vertices_y = dashmodel_vertices_y_const(model);
    const vertexint_t* vertices_z = dashmodel_vertices_z_const(model);

    const hsl16_t* face_colors_a = dashmodel_face_colors_a_const(model);
    const hsl16_t* face_colors_b = dashmodel_face_colors_b_const(model);
    const hsl16_t* face_colors_c = dashmodel_face_colors_c_const(model);
    const uint8_t* face_alphas = dashmodel_face_alphas_const(model);

    const faceint_t* face_indices_a = dashmodel_face_indices_a_const(model);
    const faceint_t* face_indices_b = dashmodel_face_indices_b_const(model);
    const faceint_t* face_indices_c = dashmodel_face_indices_c_const(model);

    const int face_count = dashmodel_face_count(model);
    const int vertex_count = dashmodel_vertex_count(model);
    const int* face_infos = dashmodel_face_infos_const(model);

    if( scene_batch_id < kGPU3DCache2MaxSceneBatches )
        ctx->renderer->model_cache2.SceneBatchAddModel(scene_batch_id, (uint16_t)model_id);

    if( dashmodel_has_textures(model) )
    {
        const faceint_t* faces_textures = dashmodel_face_textures_const(model);
        const faceint_t* textured_faces = dashmodel_face_texture_coords_const(model);
        const faceint_t* textured_faces_a = dashmodel_textured_p_coordinate_const(model);
        const faceint_t* textured_faces_b = dashmodel_textured_m_coordinate_const(model);
        const faceint_t* textured_faces_c = dashmodel_textured_n_coordinate_const(model);

        GPU3DCache2UVCalculationMode uv_calculation_mode;
        if( dashmodel__is_ground_va(model) )
            uv_calculation_mode = GPU3DCache2UVCalculationMode::FirstFace;
        else
            uv_calculation_mode = GPU3DCache2UVCalculationMode::TexturedFaceArray;

        ctx->renderer->model_cache2.BatchAddModelTexturedi16(
            (uint16_t)model_id,
            gpu_segment_slot,
            frame_index,
            (uint32_t)vertex_count,
            const_cast<vertexint_t*>(vertices_x),
            const_cast<vertexint_t*>(vertices_y),
            const_cast<vertexint_t*>(vertices_z),
            (uint32_t)face_count,
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_a)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_b)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_c)),
            const_cast<uint16_t*>(face_colors_a),
            const_cast<uint16_t*>(face_colors_b),
            const_cast<uint16_t*>(face_colors_c),
            const_cast<uint8_t*>(face_alphas),
            const_cast<faceint_t*>(faces_textures),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces_a)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces_b)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces_c)),
            face_infos,
            uv_calculation_mode);
    }
    else
    {
        ctx->renderer->model_cache2.BatchAddModeli16(
            (uint16_t)model_id,
            gpu_segment_slot,
            frame_index,
            (uint32_t)vertex_count,
            const_cast<vertexint_t*>(vertices_x),
            const_cast<vertexint_t*>(vertices_y),
            const_cast<vertexint_t*>(vertices_z),
            (uint32_t)face_count,
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_a)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_b)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_c)),
            const_cast<uint16_t*>(face_colors_a),
            const_cast<uint16_t*>(face_colors_b),
            const_cast<uint16_t*>(face_colors_c),
            const_cast<uint8_t*>(face_alphas),
            face_infos);
    }
}

void
webgl1_frame_event_clear_rect(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    const int rx = cmd->_clear_rect.x;
    const int ry = cmd->_clear_rect.y;
    const int rw = cmd->_clear_rect.w;
    const int rh = cmd->_clear_rect.h;
    if( rw <= 0 || rh <= 0 )
        return;

    if( ctx->clear_rect_slot >= kWebGL1MaxClearRectsPerFrame )
    {
        static bool s_warned;
        if( !s_warned )
        {
            fprintf(
                stderr,
                "[webgl1] CLEAR_RECT: more than %d clears in one frame; extra clears ignored\n",
                kWebGL1MaxClearRectsPerFrame);
            s_warned = true;
        }
        return;
    }
    ctx->clear_rect_slot++;

    glViewport(0, 0, r->width, r->height);
    glEnable(GL_SCISSOR_TEST);
    webgl1_clamped_gl_scissor(r->width, r->height, ctx->win_width, ctx->win_height, rx, ry, rw, rh);
    glDepthMask(GL_TRUE);
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    glScissor(0, 0, r->width > 0 ? r->width : 1, r->height > 0 ? r->height : 1);
}

void
webgl1_frame_event_begin_3d(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd,
    const LogicalViewportRect* default_logical_vp)
{
    if( !ctx || !ctx->renderer || !default_logical_vp )
        return;

    struct Platform2_SDL2_Renderer_WebGL1* renderer = ctx->renderer;

    LogicalViewportRect pass_lr = *default_logical_vp;
    if( cmd && cmd->_begin_3d.w > 0 && cmd->_begin_3d.h > 0 )
    {
        pass_lr.x = cmd->_begin_3d.x;
        pass_lr.y = cmd->_begin_3d.y;
        pass_lr.width = cmd->_begin_3d.w;
        pass_lr.height = cmd->_begin_3d.h;
    }

    const ToriGlViewportPixels gl_vp = compute_gl_world_viewport_rect(
        renderer->width, renderer->height, ctx->win_width, ctx->win_height, pass_lr);
    ctx->pass_3d_dst_logical = pass_lr;
    ctx->world_gl_vp = gl_vp;

    glViewport(gl_vp.x, gl_vp.y, gl_vp.width, gl_vp.height);
    glEnable(GL_SCISSOR_TEST);
    webgl1_clamped_gl_scissor(
        renderer->width,
        renderer->height,
        ctx->win_width,
        ctx->win_height,
        pass_lr.x,
        pass_lr.y,
        pass_lr.width,
        pass_lr.height);

    struct ToriRSRenderCommand world_clear = { 0 };
    world_clear.kind = TORIRS_GFX_STATE_CLEAR_RECT;
    world_clear._clear_rect.x = pass_lr.x;
    world_clear._clear_rect.y = pass_lr.y;
    world_clear._clear_rect.w = pass_lr.width;
    world_clear._clear_rect.h = pass_lr.height;
    if( world_clear._clear_rect.w > 0 && world_clear._clear_rect.h > 0 )
        webgl1_frame_event_clear_rect(ctx, &world_clear);

    glViewport(gl_vp.x, gl_vp.y, gl_vp.width, gl_vp.height);
    webgl1_clamped_gl_scissor(
        renderer->width,
        renderer->height,
        ctx->win_width,
        ctx->win_height,
        pass_lr.x,
        pass_lr.y,
        pass_lr.width,
        pass_lr.height);

    renderer->pass3d_builder.Begin3D();
}

void
webgl1_frame_event_end_3d(WebGL1RenderCtx* ctx)
{
    if( !ctx || !ctx->renderer )
        return;

    struct Platform2_SDL2_Renderer_WebGL1* renderer = ctx->renderer;

    if( renderer->prog_world3d == 0u )
        return;

    if( !renderer->pass3d_builder.HasCommands() )
    {
        renderer->pass3d_builder.End3D();
        return;
    }

    if( renderer->uniform_pass_subslot >= (uint32_t)kWebGL1Max3dPassesPerFrame )
    {
        fprintf(
            stderr,
            "webgl1_frame_event_end_3d: more than %d 3D passes in one frame; skipping draw\n",
            kWebGL1Max3dPassesPerFrame);
        renderer->pass3d_builder.ClearAfterSubmit();
        renderer->pass3d_builder.End3D();
        return;
    }

    const LogicalViewportRect& pl = ctx->pass_3d_dst_logical;
    const float projection_width = (float)pl.width;
    const float projection_height = (float)pl.height;

    MetalUniformsWebGL uniforms{};
    struct GGame* game = ctx->game;
    const float pitch_rad = game ? webgl1_dash_yaw_to_radians(game->camera_pitch) : 0.0f;
    const float yaw_rad = game ? webgl1_dash_yaw_to_radians(game->camera_yaw) : 0.0f;
    webgl1_compute_view_matrix(uniforms.modelViewMatrix, 0.0f, 0.0f, 0.0f, pitch_rad, yaw_rad);
    webgl1_compute_projection_matrix(
        uniforms.projectionMatrix,
        (90.0f * (float)M_PI) / 180.0f,
        projection_width > 0.0f ? projection_width : 1.0f,
        projection_height > 0.0f ? projection_height : 1.0f);
    uniforms.uClock = (float)(SDL_GetTicks64() / 20);
    uniforms._pad_uniform[0] = 0.0f;
    uniforms._pad_uniform[1] = 0.0f;
    uniforms._pad_uniform[2] = 0.0f;

    Pass3DBuilder2SubmitWebGL1(
        renderer->pass3d_builder,
        renderer->model_cache2,
        renderer,
        renderer->prog_world3d,
        renderer->world_locs,
        renderer->cache2_atlas_tex,
        uniforms.modelViewMatrix,
        uniforms.projectionMatrix,
        uniforms.uClock);

    renderer->uniform_pass_subslot += 1u;

    renderer->pass3d_builder.ClearAfterSubmit();
    renderer->pass3d_builder.End3D();
}

void
webgl1_frame_event_texture_load(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer )
        return;
    const int tex_id = cmd->_texture_load.texture_id;
    struct DashTexture* tex = cmd->_texture_load.texture_nullable;
    if( !tex || !tex->texels || tex_id < 0 || tex_id >= (int)MAX_TEXTURES )
        return;

    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;

    const int w = tex->width;
    const int h = tex->height;
    const bool ok128 = (w == (int)TEXTURE_DIMENSION && h == (int)TEXTURE_DIMENSION);
    const bool ok64 = (w == 64 && h == 64);
    if( !ok128 && !ok64 )
    {
        fprintf(
            stderr,
            "[webgl1] texture %d: GPU3DCache2 path requires %zu×%zu or 64×64 (got %d×%d)\n",
            tex_id,
            TEXTURE_DIMENSION,
            TEXTURE_DIMENSION,
            w,
            h);
        return;
    }

    if( r->tiles_cpu.empty() )
        webgl1_atlas_resources_init(r);

    std::vector<uint8_t>& rgba = r->rgba_scratch;
    rgba.resize((size_t)w * (size_t)h * 4u);
    for( int p = 0; p < w * h; ++p )
    {
        int pix = tex->texels[p];
        rgba[(size_t)p * 4u + 0] = (uint8_t)((pix >> 16) & 0xFF);
        rgba[(size_t)p * 4u + 1] = (uint8_t)((pix >> 8) & 0xFF);
        rgba[(size_t)p * 4u + 2] = (uint8_t)(pix & 0xFF);
        rgba[(size_t)p * 4u + 3] = (uint8_t)((pix >> 24) & 0xFF);
    }

    const uint8_t* atlas_pixels = rgba.data();
    std::vector<uint8_t> upscale128;
    if( ok64 )
    {
        upscale128.resize((size_t)TEXTURE_DIMENSION * (size_t)TEXTURE_DIMENSION * 4u);
        webgl1_rgba64_nearest_to_128(rgba.data(), upscale128.data());
        atlas_pixels = upscale128.data();
    }

    r->model_cache2.LoadTexture128((uint16_t)tex_id, atlas_pixels);
    webgl1_write_atlas_tile_slot(r, (uint16_t)tex_id, tex);
    webgl1_refresh_atlas_texture(r);
}

void
webgl1_frame_event_model_load(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer )
        return;

    struct DashModel* model = cmd->_model_load.model;
    const int model_id = cmd->_model_load.model_id;
    if( !model || model_id <= 0 )
        return;

    if( dashmodel__is_ground_va(model) )
        return;
    if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    GPU3DCache2Resource prev =
        ctx->renderer->model_cache2.TakeStandaloneRetainedBuffers((uint16_t)model_id);
    webgl1_delete_gl_buffer(prev.vbo);
    webgl1_delete_gl_buffer(prev.ebo);

    ctx->renderer->model_cache2.BatchBegin();
    webgl1_cache2_batch_push_model_mesh(
        ctx, model, model_id, kSceneBatchSlotNone, GPU_MODEL_ANIMATION_NONE_IDX, 0u);

    BatchBuffersWebGL1 v2bufs{};
    GPU3DCache2BatchSubmitWebGL1(ctx->renderer->model_cache2, v2bufs, kSceneBatchSlotNone);

    ctx->renderer->model_cache2.SetStandaloneRetainedBuffers(
        (uint16_t)model_id,
        (GPUResourceHandle)(uintptr_t)v2bufs.vbo,
        (GPUResourceHandle)(uintptr_t)v2bufs.ebo);
    v2bufs.vbo = 0u;
    v2bufs.ebo = 0u;
    (void)cmd->_model_load.usage_hint;
}

void
webgl1_frame_event_model_unload(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int mid = cmd->_model_load.model_id;
    if( mid <= 0 || !ctx || !ctx->renderer )
        return;
    GPU3DCache2Resource solo =
        ctx->renderer->model_cache2.TakeStandaloneRetainedBuffers((uint16_t)mid);
    webgl1_delete_gl_buffer(solo.vbo);
    webgl1_delete_gl_buffer(solo.ebo);
    ctx->renderer->model_cache2.ClearModel((uint16_t)mid);
    (void)cmd->_model_load.usage_hint;
}

void
webgl1_frame_event_batch_model_load_start(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const uint32_t bid = cmd->_batch.batch_id;
    ctx->renderer->current_model_batch_id = bid;
    ctx->renderer->current_model_batch_active = true;
    ctx->renderer->model_cache2.BatchBegin();
    (void)ctx->renderer->model_cache2.SceneBatchBegin(
        bid, (ToriRS_UsageHint)cmd->_batch.usage_hint);
}

void
webgl1_frame_event_model_batched_load(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    struct DashModel* model = cmd->_model_load.model;
    const int model_id = cmd->_model_load.model_id;
    const uint32_t bid = ctx->renderer->current_model_batch_id;
    if( !model || model_id <= 0 || !ctx->renderer->current_model_batch_active )
        return;

    webgl1_cache2_batch_push_model_mesh(
        ctx, model, model_id, bid, GPU_MODEL_ANIMATION_NONE_IDX, 0u);
}

void
webgl1_frame_event_batch_model_load_end(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const uint32_t bid = cmd->_batch.batch_id;

    BatchBuffersWebGL1 v2bufs{};
    GPU3DCache2BatchSubmitWebGL1(ctx->renderer->model_cache2, v2bufs, bid);
    GPU3DCache2Resource res{};
    res.vbo = (GPUResourceHandle)(uintptr_t)v2bufs.vbo;
    res.ebo = (GPUResourceHandle)(uintptr_t)v2bufs.ebo;
    res.valid = (v2bufs.vbo != 0u && v2bufs.ebo != 0u);
    res.policy = GPU3DCache2PolicyForUsageHint((ToriRS_UsageHint)cmd->_batch.usage_hint);
    ctx->renderer->model_cache2.SceneBatchSetResource(bid, res);
    v2bufs.vbo = 0u;
    v2bufs.ebo = 0u;

    ctx->renderer->current_model_batch_id = 0;
    ctx->renderer->current_model_batch_active = false;
}

void
webgl1_frame_event_batch_model_clear(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const uint32_t bid = cmd->_batch.batch_id;
    GPU3DCache2SceneBatchEntry cleared = ctx->renderer->model_cache2.SceneBatchClear(bid);
    for( uint16_t mid : cleared.scene_model_ids )
        ctx->renderer->model_cache2.ClearModel(mid);
    webgl1_delete_gl_buffer(cleared.resource.vbo);
    webgl1_delete_gl_buffer(cleared.resource.ebo);
}

void
webgl1_frame_event_model_draw(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    struct DashModel* model = cmd->_model_draw.model;
    if( !model || !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    const int mid_v2 = cmd->_model_draw.model_id;
    if( mid_v2 <= 0 )
        return;

    if( ctx->renderer )
        ctx->renderer->diag_frame_model_draw_cmds++;

    const bool use_anim = cmd->_model_draw.use_animation;
    const uint8_t anim_idx = cmd->_model_draw.animation_index;
    const uint8_t frame_idx = cmd->_model_draw.frame_index;
    const GPUModelPosedData pose = ctx->renderer->model_cache2.GetModelPoseForDraw(
        (uint16_t)mid_v2, use_anim, (int)anim_idx, (int)frame_idx);
    if( !pose.valid || pose.gpu_batch_id == 0u )
    {
        if( ctx->renderer )
            ctx->renderer->diag_frame_pose_invalid_skips++;
        return;
    }

    struct DashPosition draw_position = cmd->_model_draw.position;
    int face_order_count = dash3d_prepare_projected_face_order(
        ctx->game->sys_dash, model, &draw_position, ctx->game->view_port, ctx->game->camera);
    const int* face_order = dash3d_projected_face_order(ctx->game->sys_dash, &face_order_count);

    const int face_count = dashmodel_face_count(model);

    bool need_sorted_indices = webgl1_dash_model_needs_sorted_face_indices(model);
    if( !need_sorted_indices && face_order_count > 0 && face_order &&
        !webgl1_projected_face_order_is_strictly_sequential(
            face_order, face_order_count, face_count) )
        need_sorted_indices = true;

    static std::vector<uint16_t> g_sorted;
    g_sorted.clear();
    uint16_t* idx_ptr = nullptr;
    uint32_t idx_count = 0u;

    if( need_sorted_indices && face_order_count > 0 && face_count > 0 )
    {
        g_sorted.reserve((size_t)face_order_count * 3u);
        for( int fi = 0; fi < face_order_count; ++fi )
        {
            const int face_index = face_order ? face_order[fi] : fi;
            if( face_index < 0 || face_index >= face_count )
                continue;
            const uint32_t b = (uint32_t)face_index * 3u;
            g_sorted.push_back((uint16_t)b);
            g_sorted.push_back((uint16_t)(b + 1u));
            g_sorted.push_back((uint16_t)(b + 2u));
        }
        idx_count = (uint32_t)g_sorted.size();
        idx_ptr = idx_count ? g_sorted.data() : nullptr;
    }

    ctx->renderer->pass3d_builder.AddModelDrawYawOnly(
        ctx->renderer->model_cache2,
        (uint16_t)mid_v2,
        draw_position.x,
        draw_position.y,
        draw_position.z,
        draw_position.yaw,
        use_anim,
        anim_idx,
        frame_idx,
        idx_ptr,
        idx_count);
    (void)cmd->_model_draw.usage_hint;
}

void
webgl1_frame_event_batch_texture_load_start(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    (void)ctx;
    (void)cmd;
}

void
webgl1_frame_event_batch_texture_load_end(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    (void)ctx;
    (void)cmd;
}

void
webgl1_frame_event_model_animation_load(
    WebGL1RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int mid = cmd->_animation_load.model_gpu_id;
    const int fidx = cmd->_animation_load.frame_index;
    if( !cmd->_animation_load.model || mid <= 0 )
        return;

    if( !ctx->renderer->current_model_batch_active )
        return;
    const uint32_t bid = ctx->renderer->current_model_batch_id;

    struct DashModel* model = cmd->_animation_load.model;
    dashmodel_animate(model, cmd->_animation_load.frame, cmd->_animation_load.framemap);
    const uint8_t gpu_seg = cmd->_animation_load.animation_index == 1
                                ? GPU_MODEL_ANIMATION_SECONDARY_IDX
                                : GPU_MODEL_ANIMATION_PRIMARY_IDX;
    webgl1_cache2_batch_push_model_mesh(ctx, model, mid, bid, gpu_seg, (uint16_t)fidx);
    (void)cmd->_animation_load.usage_hint;
}

struct Platform2_SDL2_Renderer_WebGL1*
PlatformImpl2_SDL2_Renderer_WebGL1_New(
    int width,
    int height)
{
    return new Platform2_SDL2_Renderer_WebGL1();
}

void
PlatformImpl2_SDL2_Renderer_WebGL1_Free(struct Platform2_SDL2_Renderer_WebGL1* renderer)
{
    if( !renderer )
        return;

    if( renderer->platform && renderer->platform->window && renderer->gl_context )
    {
        SDL_GL_MakeCurrent(renderer->platform->window, (SDL_GLContext)renderer->gl_context);
        if( s_webgl1_nk )
        {
            torirs_nk_ui_clear_active();
            torirs_nk_gles2_shutdown();
            s_webgl1_nk = nullptr;
        }
        webgl1_gl_destroy_programs(renderer);
        webgl1_atlas_resources_shutdown(renderer);
        SDL_GL_DeleteContext((SDL_GLContext)renderer->gl_context);
        renderer->gl_context = nullptr;
    }

    delete renderer;
}

bool
PlatformImpl2_SDL2_Renderer_WebGL1_Init(
    struct Platform2_SDL2_Renderer_WebGL1* renderer,
    struct Platform2_SDL2* platform)
{
    if( !renderer || !platform || !platform->window )
        return false;
    renderer->platform = platform;

    renderer->gl_context = SDL_GL_CreateContext(platform->window);
    if( !renderer->gl_context )
    {
        fprintf(stderr, "WebGL1: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }
    if( SDL_GL_MakeCurrent(platform->window, (SDL_GLContext)renderer->gl_context) != 0 )
    {
        fprintf(stderr, "WebGL1: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext((SDL_GLContext)renderer->gl_context);
        renderer->gl_context = nullptr;
        return false;
    }

    /* Do not call SDL_GL_SetSwapInterval here: on Emscripten it touches main-loop timing
     * before emscripten_set_main_loop is registered (browser main sets the loop later). */

    webgl1_sync_drawable_size(renderer);
    glViewport(0, 0, renderer->width, renderer->height);

    if( !webgl1_gl_create_programs(renderer) )
    {
        fprintf(stderr, "WebGL1: shader init failed\n");
        webgl1_gl_destroy_programs(renderer);
        SDL_GL_DeleteContext((SDL_GLContext)renderer->gl_context);
        renderer->gl_context = nullptr;
        return false;
    }

    webgl1_atlas_resources_init(renderer);

    s_webgl1_nk = torirs_nk_gles2_init(platform->window);
    if( !s_webgl1_nk )
    {
        fprintf(stderr, "WebGL1: Nuklear GLES2 init failed\n");
        webgl1_atlas_resources_shutdown(renderer);
        webgl1_gl_destroy_programs(renderer);
        SDL_GL_DeleteContext((SDL_GLContext)renderer->gl_context);
        renderer->gl_context = nullptr;
        return false;
    }
    {
        struct nk_font_atlas* atlas = NULL;
        torirs_nk_gles2_font_stash_begin(&atlas);
        nk_font_atlas_add_default(atlas, 13.0f, NULL);
        torirs_nk_gles2_font_stash_end();
    }
    torirs_nk_ui_set_active(s_webgl1_nk, torirs_nk_gles2_handle_event, torirs_nk_gles2_handle_grab);
    s_webgl1_ui_prev_perf = SDL_GetPerformanceCounter();

    webgl1_gl_bind_default_world_gl_state();

    renderer->gl_ready = true;
    return true;
}

void
PlatformImpl2_SDL2_Renderer_WebGL1_Render(
    struct Platform2_SDL2_Renderer_WebGL1* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->gl_ready || !game || !render_command_buffer ||
        !renderer->platform || !renderer->platform->window )
        return;
    struct Platform2_SDL2* platform = renderer->platform;
    if( SDL_GL_MakeCurrent(platform->window, (SDL_GLContext)renderer->gl_context) != 0 )
        return;

    webgl1_sync_drawable_size(renderer);

    renderer->diag_frame_model_draw_cmds = 0u;
    renderer->diag_frame_pose_invalid_skips = 0u;
    renderer->diag_frame_submitted_model_draws = 0u;
    renderer->diag_frame_dynamic_index_draws = 0u;

    renderer->uniform_pass_subslot = 0u;

    glViewport(0, 0, renderer->width, renderer->height);
    webgl1_gl_bind_default_world_gl_state();
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int win_width = platform->game_screen_width;
    int win_height = platform->game_screen_height;
    if( win_width <= 0 || win_height <= 0 )
        SDL_GetWindowSize(platform->window, &win_width, &win_height);

    const LogicalViewportRect logical_vp =
        compute_logical_viewport_rect(win_width, win_height, game);

    LibToriRS_FrameBegin(game, render_command_buffer);

    WebGL1RenderCtx ctx = {};
    ctx.renderer = renderer;
    ctx.game = game;
    ctx.win_width = win_width;
    ctx.win_height = win_height;
    ctx.clear_rect_slot = 0;

    {
        struct ToriRSRenderCommand cmd = { 0 };
        while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, true) )
        {
            switch( cmd.kind )
            {
            case TORIRS_GFX_STATE_BEGIN_3D:
                webgl1_frame_event_begin_3d(&ctx, &cmd, &logical_vp);
                break;
            case TORIRS_GFX_STATE_END_3D:
                webgl1_frame_event_end_3d(&ctx);
                break;
            case TORIRS_GFX_STATE_CLEAR_RECT:
                webgl1_frame_event_clear_rect(&ctx, &cmd);
                break;
            case TORIRS_GFX_RES_TEX_LOAD:
                webgl1_frame_event_texture_load(&ctx, &cmd);
                break;
            case TORIRS_GFX_RES_MODEL_LOAD:
                webgl1_frame_event_model_load(&ctx, &cmd);
                break;
            case TORIRS_GFX_RES_MODEL_UNLOAD:
                webgl1_frame_event_model_unload(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH3D_BEGIN:
                webgl1_frame_event_batch_model_load_start(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH3D_MODEL_ADD:
                webgl1_frame_event_model_batched_load(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH3D_END:
                webgl1_frame_event_batch_model_load_end(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH3D_CLEAR:
                webgl1_frame_event_batch_model_clear(&ctx, &cmd);
                break;
            case TORIRS_GFX_DRAW_MODEL:
                webgl1_frame_event_model_draw(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH2D_TEX_BEGIN:
                webgl1_frame_event_batch_texture_load_start(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH2D_TEX_END:
                webgl1_frame_event_batch_texture_load_end(&ctx, &cmd);
                break;
            case TORIRS_GFX_RES_ANIM_LOAD:
            case TORIRS_GFX_BATCH3D_ANIM_ADD:
                webgl1_frame_event_model_animation_load(&ctx, &cmd);
                break;
            default:
                break;
            }
        }
    }

    LibToriRS_FrameEnd(game);

    if( s_webgl1_nk )
    {
        double const dt = torirs_nk_ui_frame_delta_sec(&s_webgl1_ui_prev_perf);
        TorirsNkDebugPanelParams params = {};
        params.window_title = "Info";
        params.delta_time_sec = dt;
        params.view_w_cap = renderer->width;
        params.view_h_cap = renderer->height;
        params.sdl_window = platform->window;
        params.soft3d = nullptr;
        params.include_soft3d_extras = 0;
        params.include_gpu_frame_stats = 1;
        params.gpu_model_draws = renderer->diag_frame_model_draw_cmds;
        params.gpu_tris = 0u;
        params.gpu_submitted_model_draws = renderer->diag_frame_submitted_model_draws;
        params.gpu_pose_invalid_skips = renderer->diag_frame_pose_invalid_skips;
        params.gpu_dynamic_index_draws = renderer->diag_frame_dynamic_index_draws;
        torirs_nk_debug_panel_draw(s_webgl1_nk, game, &params);
        torirs_nk_ui_after_frame(s_webgl1_nk);
        torirs_nk_gles2_render(NK_ANTI_ALIASING_ON, 512 * 1024, 128 * 1024);
    }

    SDL_GL_SwapWindow(platform->window);
}

#endif // __EMSCRIPTEN__
