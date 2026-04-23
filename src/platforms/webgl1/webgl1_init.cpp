#include "platforms/webgl1/webgl1_internal.h"

#include <climits>
#include <cstring>

#define TORIRS_NK_SDL_GLES2_IMPLEMENTATION
#include "nuklear/backends/sdl_opengles2/nuklear_torirs_sdl_gles2.h"

#include <SDL.h>
#include <emscripten/html5.h>

struct nk_context* s_webgl1_nk = nullptr;
Uint64 s_webgl1_ui_prev_perf = 0;

void
sync_canvas_size(struct Platform2_SDL2_Renderer_WebGL1* renderer)
{
    if( !renderer->platform )
        return;
    Platform2_SDL2_SyncCanvasCssSize(renderer->platform, renderer->platform->current_game);
    int new_w = renderer->platform->drawable_width;
    int new_h = renderer->platform->drawable_height;
    if( new_w <= 0 || new_h <= 0 )
        return;
    if( new_w != renderer->width || new_h != renderer->height )
    {
        renderer->width = new_w;
        renderer->height = new_h;
        glViewport(0, 0, renderer->width, renderer->height);
    }
}

static void
wg1_free_all_gpu_user_data(struct Platform2_SDL2_Renderer_WebGL1* r)
{
    if( !r )
        return;
    for( int i = 0; i < GpuFontCache<GLuint>::kMaxFonts; ++i )
    {
        GpuFontCache<GLuint>::FontEntry* e = r->font_cache.get_font(i);
        if( e && e->atlas_texture )
            glDeleteTextures(1, &e->atlas_texture);
    }
    r->font_cache.destroy();

    const int sp_cap = r->sprite_cache.get_element_capacity();
    const int sp_ma = r->sprite_cache.get_atlas_per_element();
    for( int el = 0; el < sp_cap; ++el )
    {
        for( int ai = 0; ai < sp_ma; ++ai )
        {
            auto* s = r->sprite_cache.get_sprite(el, ai);
            if( s && s->atlas_texture && !s->is_batched )
                glDeleteTextures(1, &s->atlas_texture);
        }
    }
    r->sprite_cache.destroy();

    r->texture_cache.destroy();

    for( int m = 0; m < r->model_cache.get_model_capacity(); ++m )
        wg1_release_model_gpu_buffers(r, m);
    r->model_cache.destroy();
    r->geometry_mirror.clear();
    r->va_staging.clear();

    wg1_world_atlas_shutdown(r);
    wg1_shaders_shutdown(r);

    if( r->exp_stream_vbo )
    {
        glDeleteBuffers(1, &r->exp_stream_vbo);
        r->exp_stream_vbo = 0;
    }
}

struct Platform2_SDL2_Renderer_WebGL1*
PlatformImpl2_SDL2_Renderer_WebGL1_New(
    int width,
    int height,
    int max_width,
    int max_height)
{
    auto* renderer = new Platform2_SDL2_Renderer_WebGL1();
    renderer->gl_context = NULL;
    renderer->platform = NULL;
    renderer->width = width;
    renderer->height = height;
    renderer->max_width = max_width;
    renderer->max_height = max_height;
    renderer->webgl_context_ready = false;
    renderer->current_model_batch_id = 0;
    renderer->current_sprite_batch_id = 0;
    renderer->current_font_batch_id = 0;
    renderer->texture_cache.init(256, 0u);
    renderer->model_cache.init(
        Gpu3DCache<GLuint>::kGpuIdTableSize,
        0u,
        Gpu3DCache<GLuint>::BatchCaps{ INT_MAX, INT_MAX },
        Gpu3DAngleEncoding::DashR2pi2048);
    renderer->sprite_cache.init(4096, 8, 0u);
    renderer->font_cache.init(0u);
    memset(renderer->texture_id_ever_loaded, 0, sizeof(renderer->texture_id_ever_loaded));
    renderer->texture_id_ever_loaded_count = 0;
    renderer->has_float_tex = false;
    renderer->has_instanced_arrays = false;
    renderer->world_atlas_tex = 0;
    renderer->world_tile_meta_tex = 0;
    renderer->exp_stream_vbo = 0;
    renderer->font_vbo = 0;
    renderer->program_world = 0;
    renderer->program_ui_sprite = 0;
    renderer->program_ui_sprite_invrot = 0;
    renderer->font_program = 0;
    return renderer;
}

void
PlatformImpl2_SDL2_Renderer_WebGL1_Free(struct Platform2_SDL2_Renderer_WebGL1* renderer)
{
    if( !renderer )
        return;
    if( renderer->webgl_context_ready )
    {
        emscripten_webgl_make_context_current((EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)renderer->gl_context);
        wg1_free_all_gpu_user_data(renderer);
    }

    if( renderer->font_vbo && renderer->webgl_context_ready )
        glDeleteBuffers(1, &renderer->font_vbo);

    if( s_webgl1_nk )
    {
        torirs_nk_ui_clear_active();
        torirs_nk_gles2_shutdown();
        s_webgl1_nk = nullptr;
    }

    delete renderer;
}

bool
PlatformImpl2_SDL2_Renderer_WebGL1_Init(
    struct Platform2_SDL2_Renderer_WebGL1* renderer,
    struct Platform2_SDL2* platform)
{
    if( !renderer || !platform )
        return false;
    renderer->platform = platform;

    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = 1;
    attrs.depth = 1;
    attrs.stencil = 0;
    attrs.antialias = 0;
    attrs.premultipliedAlpha = 0;
    attrs.preserveDrawingBuffer = 0;
    attrs.enableExtensionsByDefault = 1;
    attrs.majorVersion = 1;
    attrs.minorVersion = 0;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if( ctx <= 0 )
    {
        fprintf(stderr, "WebGL1 init failed: could not create context\n");
        return false;
    }
    if( emscripten_webgl_make_context_current(ctx) != EMSCRIPTEN_RESULT_SUCCESS )
    {
        fprintf(stderr, "WebGL1 init failed: could not make context current\n");
        return false;
    }

    renderer->gl_context = (SDL_GLContext)ctx;
    renderer->webgl_context_ready = true;
    emscripten_set_canvas_element_size("#canvas", renderer->width, renderer->height);
    glViewport(0, 0, renderer->width, renderer->height);

    renderer->has_float_tex =
        emscripten_webgl_enable_extension(ctx, "OES_texture_float") == EM_TRUE;
    renderer->has_instanced_arrays =
        emscripten_webgl_enable_extension(ctx, "ANGLE_instanced_arrays") == EM_TRUE;

    if( !wg1_shaders_init(renderer) )
    {
        fprintf(stderr, "WebGL1 init failed: shader compile\n");
        return false;
    }

    s_webgl1_nk = torirs_nk_gles2_init(platform->window);
    if( !s_webgl1_nk )
    {
        fprintf(stderr, "Nuklear WebGL1 init failed\n");
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

    return true;
}
