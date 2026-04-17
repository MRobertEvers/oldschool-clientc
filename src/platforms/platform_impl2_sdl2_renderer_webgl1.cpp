#include "platform_impl2_sdl2_renderer_webgl1.h"

#include "graphics/dash.h"
#include "platforms/common/torirs_nk_ui_bridge.h"
#include "platforms/common/torirs_nuklear_debug_panel.h"
#include "tori_rs_render.h"

extern "C" {
#include "platforms/common/platform_memory.h"
#include "tori_rs.h"
}

#include <SDL.h>

#define TORIRS_NK_SDL_GLES2_IMPLEMENTATION
#include "nuklear/backends/sdl_opengles2/nuklear_torirs_sdl_gles2.h"

#include <GLES2/gl2.h>
#include <emscripten/html5.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <emscripten.h>

static struct nk_context* s_webgl_nk;
static Uint64 s_webgl_ui_prev_perf;

static inline int
model_id_from_model_cache_key(uint64_t k)
{
    return (int)(uint32_t)(k >> 24);
}

static const char* g_font_vertex_shader_es2 = R"(#version 100
attribute vec2 aPos;
attribute vec2 aUv;
attribute vec4 aColor;
uniform mat4 uProjection;
varying vec2 vUv;
varying vec4 vColor;
void main()
{
    vUv = aUv;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)";

static const char* g_font_fragment_shader_es2 = R"(#version 100
precision mediump float;
varying vec2 vUv;
varying vec4 vColor;
uniform sampler2D uTex;
void main()
{
    float a = texture2D(uTex, vUv).a;
    if( a < 0.01 )
        discard;
    gl_FragColor = vec4(vColor.rgb, a * vColor.a);
}
)";

static GLuint
compile_shader_es2(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        printf("Font shader compile error (ES2): %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint
link_program_es2(GLuint vert, GLuint frag)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aUv");
    glBindAttribLocation(prog, 2, "aColor");
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if( !ok )
    {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        printf("Font program link error (ES2): %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static bool
webgl1_ensure_font_program(struct Platform2_SDL2_Renderer_WebGL1* renderer)
{
    if( renderer->font_program )
        return true;

    GLuint vs = compile_shader_es2(GL_VERTEX_SHADER, g_font_vertex_shader_es2);
    GLuint fs = compile_shader_es2(GL_FRAGMENT_SHADER, g_font_fragment_shader_es2);
    if( !vs || !fs )
    {
        if( vs )
            glDeleteShader(vs);
        if( fs )
            glDeleteShader(fs);
        return false;
    }
    renderer->font_program = link_program_es2(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if( !renderer->font_program )
        return false;

    renderer->font_attrib_pos = glGetAttribLocation(renderer->font_program, "aPos");
    renderer->font_attrib_uv = glGetAttribLocation(renderer->font_program, "aUv");
    renderer->font_attrib_color = glGetAttribLocation(renderer->font_program, "aColor");
    renderer->font_uniform_projection =
        glGetUniformLocation(renderer->font_program, "uProjection");
    renderer->font_uniform_tex = glGetUniformLocation(renderer->font_program, "uTex");

    glGenBuffers(1, &renderer->font_vbo);
    return true;
}

static void
webgl1_font_load(
    struct Platform2_SDL2_Renderer_WebGL1* renderer,
    struct DashPixFont* font)
{
    if( !font || !font->atlas )
        return;
    if( renderer->font_atlas_cache.count(font) )
        return;
    if( !webgl1_ensure_font_program(renderer) )
        return;

    struct DashFontAtlas* atlas = font->atlas;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if( !tex )
        return;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        atlas->atlas_width,
        atlas->atlas_height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        atlas->rgba_pixels);

    GLFontAtlasEntryES2 entry;
    entry.texture = tex;
    renderer->font_atlas_cache[font] = entry;
}

struct GLViewportRect
{
    int x;
    int y;
    int width;
    int height;
};

struct LogicalViewportRect
{
    int x;
    int y;
    int width;
    int height;
};

static LogicalViewportRect
compute_logical_viewport_rect(
    int window_width,
    int window_height,
    const struct GGame* game)
{
    LogicalViewportRect rect = { 0, 0, window_width, window_height };
    if( window_width <= 0 || window_height <= 0 || !game || !game->view_port )
        return rect;

    int x = game->viewport_offset_x;
    int y = game->viewport_offset_y;
    int w = game->view_port->width;
    int h = game->view_port->height;
    if( w <= 0 || h <= 0 )
        return rect;

    if( x < 0 )
        x = 0;
    if( y < 0 )
        y = 0;
    if( x >= window_width || y >= window_height )
        return rect;
    if( x + w > window_width )
        w = window_width - x;
    if( y + h > window_height )
        h = window_height - y;
    if( w <= 0 || h <= 0 )
        return rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;
    return rect;
}

static GLViewportRect
compute_world_viewport_rect(
    int framebuffer_width,
    int framebuffer_height,
    int window_width,
    int window_height,
    const LogicalViewportRect& logical_rect)
{
    GLViewportRect rect = { 0, 0, framebuffer_width, framebuffer_height };
    if( framebuffer_width <= 0 || framebuffer_height <= 0 || window_width <= 0 ||
        window_height <= 0 )
        return rect;

    const double scale_x =
        (window_width > 0) ? ((double)framebuffer_width / (double)window_width) : 1.0;
    const double scale_y =
        (window_height > 0) ? ((double)framebuffer_height / (double)window_height) : 1.0;

    int scaled_x = (int)lround((double)logical_rect.x * scale_x);
    int scaled_top_y = (int)lround((double)logical_rect.y * scale_y);
    int scaled_w = (int)lround((double)logical_rect.width * scale_x);
    int scaled_h = (int)lround((double)logical_rect.height * scale_y);

    int clamped_x = scaled_x < 0 ? 0 : scaled_x;
    int clamped_top_y = scaled_top_y < 0 ? 0 : scaled_top_y;
    if( clamped_x >= framebuffer_width || clamped_top_y >= framebuffer_height )
        return rect;

    int clamped_w = scaled_w;
    int clamped_h = scaled_h;
    if( clamped_x + clamped_w > framebuffer_width )
        clamped_w = framebuffer_width - clamped_x;
    if( clamped_top_y + clamped_h > framebuffer_height )
        clamped_h = framebuffer_height - clamped_top_y;
    if( clamped_w <= 0 || clamped_h <= 0 )
        return rect;

    rect.x = clamped_x;
    rect.y = framebuffer_height - (clamped_top_y + clamped_h);
    rect.width = clamped_w;
    rect.height = clamped_h;
    return rect;
}

static float
yaw_to_radians(int yaw_r2pi2048)
{
    return (yaw_r2pi2048 * 2.0f * 3.14159265358979323846f) / 2048.0f;
}

static uint64_t
model_gpu_cache_key(const struct DashModel* model)
{
    if( !model )
        return 0;
    uint64_t key = 14695981039346656037ULL;
    const uint64_t fnv_prime = 1099511628211ULL;
    auto mix_word = [&](uint64_t word) {
        key ^= word;
        key *= fnv_prime;
    };

    mix_word((uint64_t)(uintptr_t)dashmodel_vertices_x_const(model));
    mix_word((uint64_t)(uintptr_t)dashmodel_face_indices_a_const(model));
    mix_word((uint64_t)(uintptr_t)dashmodel_face_indices_b_const(model));
    mix_word((uint64_t)(uintptr_t)dashmodel_face_indices_c_const(model));
    mix_word((uint64_t)dashmodel_face_count(model));

    // Animated models are deformed in-place, so pointer-based keys are not enough:
    // hash current vertex contents to get a distinct GPU mesh per keyframe pose.
    struct DashModel* mw = (struct DashModel*)model;
    const bool is_animated = dashmodel_original_vertices_x(mw) && dashmodel_original_vertices_y(mw) &&
                             dashmodel_original_vertices_z(mw) && dashmodel_vertex_count(model) > 0;
    if( is_animated )
    {
        const vertexint_t* vx = dashmodel_vertices_x_const(model);
        const vertexint_t* vy = dashmodel_vertices_y_const(model);
        const vertexint_t* vz = dashmodel_vertices_z_const(model);
        int vc = dashmodel_vertex_count(model);
        for( int i = 0; i < vc; ++i )
        {
            mix_word((uint64_t)(uint32_t)vx[i]);
            mix_word((uint64_t)(uint32_t)vy[i]);
            mix_word((uint64_t)(uint32_t)vz[i]);
        }
    }
    return key;
}

static void
render_nuklear_overlay(
    struct Platform2_SDL2_Renderer_WebGL1* renderer,
    struct GGame* game)
{
    if( !s_webgl_nk )
        return;

    double const dt = torirs_nk_ui_frame_delta_sec(&s_webgl_ui_prev_perf);

    int mw = renderer->max_width > 0 ? renderer->max_width : 4096;
    int mh = renderer->max_height > 0 ? renderer->max_height : 4096;

    TorirsNkDebugPanelParams params = {};
    params.window_title = "WebGL1 Debug";
    params.delta_time_sec = dt;
    params.view_w_cap = mw;
    params.view_h_cap = mh;
    params.include_soft3d_extras = 0;
    params.include_load_counts = 1;
    params.loaded_models = renderer->loaded_model_keys.size();
    params.loaded_scenes = renderer->loaded_scene_element_keys.size();
    params.loaded_textures = renderer->loaded_texture_ids.size();

    torirs_nk_debug_panel_draw(s_webgl_nk, game, &params);
    torirs_nk_ui_after_frame(s_webgl_nk);

    glDisable(GL_DEPTH_TEST);
    torirs_nk_gles2_render(NK_ANTI_ALIASING_ON, 512 * 1024, 128 * 1024);
    pix3dgl_restore_gl_state_after_ui(renderer->pix3dgl);
}

struct LetterboxRect
{
    int x, y, w, h;
};

/* Returns a centered rect that fits game_w×game_h inside canvas_w×canvas_h
 * while preserving the game's aspect ratio (letterbox / pillarbox). */
static LetterboxRect
compute_letterbox_rect(int canvas_w, int canvas_h, int game_w, int game_h)
{
    LetterboxRect r = { 0, 0, canvas_w, canvas_h };
    if( canvas_w <= 0 || canvas_h <= 0 || game_w <= 0 || game_h <= 0 )
        return r;
    const float ca = (float)canvas_w / (float)canvas_h;
    const float ga = (float)game_w / (float)game_h;
    if( ga > ca )
    {
        r.w = canvas_w;
        r.h = (int)((float)canvas_w / ga);
        r.x = 0;
        r.y = (canvas_h - r.h) / 2;
    }
    else
    {
        r.h = canvas_h;
        r.w = (int)((float)canvas_h * ga);
        r.y = 0;
        r.x = (canvas_w - r.w) / 2;
    }
    return r;
}

static void
sync_canvas_size(struct Platform2_SDL2_Renderer_WebGL1* renderer)
{
    if( !renderer->platform )
        return;

    Platform2_SDL2_SyncCanvasCssSize(
        renderer->platform, renderer->platform->current_game);

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
    renderer->pix3dgl = NULL;
    renderer->next_model_index = 1;
    renderer->font_program = 0;
    renderer->font_vbo = 0;
    renderer->font_attrib_pos = -1;
    renderer->font_attrib_uv = -1;
    renderer->font_attrib_color = -1;
    renderer->font_uniform_projection = -1;
    renderer->font_uniform_tex = -1;
    return renderer;
}

void
PlatformImpl2_SDL2_Renderer_WebGL1_Free(
    struct Platform2_SDL2_Renderer_WebGL1* renderer)
{
    if( !renderer )
        return;

    for( auto& kv : renderer->font_atlas_cache )
    {
        if( kv.second.texture )
            glDeleteTextures(1, &kv.second.texture);
    }
    renderer->font_atlas_cache.clear();
    if( renderer->font_vbo )
        glDeleteBuffers(1, &renderer->font_vbo);
    if( renderer->font_program )
        glDeleteProgram(renderer->font_program);

    if( renderer->pix3dgl )
    {
        pix3dgl_cleanup(renderer->pix3dgl);
        renderer->pix3dgl = NULL;
    }
    if( s_webgl_nk )
    {
        torirs_nk_ui_clear_active();
        torirs_nk_gles2_shutdown();
        s_webgl_nk = NULL;
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
        printf("WebGL1 init failed: could not create context\n");
        return false;
    }
    if( emscripten_webgl_make_context_current(ctx) != EMSCRIPTEN_RESULT_SUCCESS )
    {
        printf("WebGL1 init failed: could not make context current\n");
        return false;
    }

    renderer->gl_context = (SDL_GLContext)ctx;
    renderer->webgl_context_ready = true;
    emscripten_set_canvas_element_size("#canvas", renderer->width, renderer->height);
    glViewport(0, 0, renderer->width, renderer->height);
    renderer->pix3dgl = pix3dgl_new(false, true);
    if( !renderer->pix3dgl )
    {
        printf("WebGL1 init failed: Pix3DGL setup failed\n");
        return false;
    }

    s_webgl_nk = torirs_nk_gles2_init(platform->window);
    if( !s_webgl_nk )
    {
        printf("Nuklear WebGL1 init failed\n");
        return false;
    }
    {
        struct nk_font_atlas* atlas = NULL;
        torirs_nk_gles2_font_stash_begin(&atlas);
        nk_font_atlas_add_default(atlas, 13.0f, NULL);
        torirs_nk_gles2_font_stash_end();
    }
    torirs_nk_ui_set_active(s_webgl_nk, torirs_nk_gles2_handle_event, torirs_nk_gles2_handle_grab);
    s_webgl_ui_prev_perf = SDL_GetPerformanceCounter();

    return true;
}

void
PlatformImpl2_SDL2_Renderer_WebGL1_Render(
    struct Platform2_SDL2_Renderer_WebGL1* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    static uint64_t s_frame = 0;
    if( !renderer || !renderer->webgl_context_ready || !renderer->pix3dgl || !game ||
        !render_command_buffer )
        return;

    emscripten_webgl_make_context_current((EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)renderer->gl_context);
    sync_canvas_size(renderer);

    glViewport(0, 0, renderer->width, renderer->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Logical window size — same source as OpenGL3 (platform game_screen + SDL fallback).
     * Use for compute_logical_viewport_rect, compute_world_viewport_rect scaling, and
     * pix3dgl_sprite_draw. Drawable size stays renderer->width/height (framebuffer). */
    int window_width = renderer->platform ? renderer->platform->game_screen_width : 0;
    int window_height = renderer->platform ? renderer->platform->game_screen_height : 0;
    if( window_width <= 0 || window_height <= 0 )
    {
        if( renderer->platform && renderer->platform->window )
            SDL_GetWindowSize(renderer->platform->window, &window_width, &window_height);
    }
    if( window_width <= 0 || window_height <= 0 )
    {
        window_width = renderer->width;
        window_height = renderer->height;
    }
    /* Letterbox the fixed output (game_screen_width × game_screen_height) into the
     * canvas (renderer->width × renderer->height), preserving aspect ratio. */
    const LetterboxRect lb =
        compute_letterbox_rect(renderer->width, renderer->height, window_width, window_height);
    /* GL Y origin of the letterbox (GL y=0 is bottom of canvas). */
    const int lb_gl_y = renderer->height - lb.y - lb.h;

    const LogicalViewportRect logical_viewport =
        compute_logical_viewport_rect(window_width, window_height, game);
    /* Scale the world sub-viewport from output space into the letterbox area. */
    const GLViewportRect world_vp_in_lb = compute_world_viewport_rect(
        lb.w, lb.h, window_width, window_height, logical_viewport);
    const GLViewportRect world_viewport = { lb.x + world_vp_in_lb.x,
                                            lb_gl_y + world_vp_in_lb.y,
                                            world_vp_in_lb.width,
                                            world_vp_in_lb.height };
    glViewport(world_viewport.x, world_viewport.y, world_viewport.width, world_viewport.height);

    const float projection_width = (float)logical_viewport.width;
    const float projection_height = (float)logical_viewport.height;

    pix3dgl_set_animation_clock(renderer->pix3dgl, (float)((uint64_t)emscripten_get_now() / 20));
    pix3dgl_begin_3dframe(
        renderer->pix3dgl,
        (float)0,
        (float)0,
        (float)0,
        (float)game->camera_pitch,
        (float)game->camera_yaw,
        projection_width,
        projection_height);

    LibToriRS_FrameBegin(game, render_command_buffer);

    /* Sprite commands must be deferred until after pix3dgl_end_3dframe. Everything else
     * (texture loads, model loads, model draws) is processed inline so that the projection
     * result written into sys_dash by FrameNextCommand (project_models=true) can be consumed
     * immediately by dash3d_prepare_projected_face_order without a second project call. */
    static std::vector<ToriRSRenderCommand> sprite_cmds;
    sprite_cmds.clear();

    {
        struct ToriRSRenderCommand cmd = { 0 };
        while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, true) )
        {
            switch( cmd.kind )
            {
            case TORIRS_GFX_FONT_LOAD:
                webgl1_font_load(renderer, cmd._font_load.font);
                break;

            case TORIRS_GFX_FONT_DRAW:
                sprite_cmds.push_back(cmd);
                break;

            case TORIRS_GFX_TEXTURE_LOAD:
            {
                renderer->loaded_texture_ids.insert(cmd._texture_load.texture_id);
                struct DashTexture* texture = cmd._texture_load.texture_nullable;
                if( texture && texture->texels )
                {
                    pix3dgl_load_texture(
                        renderer->pix3dgl,
                        cmd._texture_load.texture_id,
                        texture->texels,
                        texture->width,
                        texture->height,
                        texture->animation_direction,
                        texture->animation_speed,
                        texture->opaque);
                }
                break;
            }

            case TORIRS_GFX_MODEL_LOAD:
            {
                struct DashModel* model = cmd._model_load.model;
                if( !model || !dashmodel_face_colors_a(model) || !dashmodel_face_colors_b(model) ||
                    !dashmodel_face_colors_c(model) || !dashmodel_vertices_x(model) ||
                    !dashmodel_vertices_y(model) || !dashmodel_vertices_z(model) ||
                    !dashmodel_face_indices_a(model) || !dashmodel_face_indices_b(model) ||
                    !dashmodel_face_indices_c(model) || dashmodel_face_count(model) <= 0 )
                {
                    break;
                }
                if( model_id_from_model_cache_key(cmd._model_load.model_key) <= 0 )
                    break;
                renderer->loaded_model_keys.insert(cmd._model_load.model_key);
                if( renderer->model_index_by_key.find(cmd._model_load.model_key) ==
                    renderer->model_index_by_key.end() )
                {
                    int model_idx = renderer->next_model_index++;
                    renderer->model_index_by_key[cmd._model_load.model_key] = model_idx;
                    pix3dgl_model_load(
                        renderer->pix3dgl,
                        model_idx,
                        dashmodel_vertices_x(model),
                        dashmodel_vertices_y(model),
                        dashmodel_vertices_z(model),
                        dashmodel_face_indices_a(model),
                        dashmodel_face_indices_b(model),
                        dashmodel_face_indices_c(model),
                        dashmodel_face_count(model),
                        dashmodel_face_textures(model),
                        dashmodel_face_texture_coords(model),
                        dashmodel_textured_p_coordinate(model),
                        dashmodel_textured_m_coordinate(model),
                        dashmodel_textured_n_coordinate(model),
                        dashmodel_face_colors_a(model),
                        dashmodel_face_colors_b(model),
                        dashmodel_face_colors_c(model),
                        dashmodel_face_infos(model),
                        dashmodel_face_alphas(model));
                }
                break;
            }

            case TORIRS_GFX_MODEL_UNLOAD:
            {
                const int mid = cmd._model_load.model_id;
                if( mid <= 0 )
                    break;
                for( auto it = renderer->model_index_by_key.begin();
                     it != renderer->model_index_by_key.end(); )
                {
                    if( model_id_from_model_cache_key(it->first) == mid )
                        it = renderer->model_index_by_key.erase(it);
                    else
                        ++it;
                }
                for( auto it = renderer->loaded_model_keys.begin();
                     it != renderer->loaded_model_keys.end(); )
                {
                    if( model_id_from_model_cache_key(*it) == mid )
                        it = renderer->loaded_model_keys.erase(it);
                    else
                        ++it;
                }
                break;
            }

            case TORIRS_GFX_CLEAR_RECT:
            {
                int rx = cmd._clear_rect.x;
                int ry = cmd._clear_rect.y;
                int rw = cmd._clear_rect.w;
                int rh = cmd._clear_rect.h;
                if( rw <= 0 || rh <= 0 )
                    break;
                LogicalViewportRect lr = { rx, ry, rw, rh };
                const GLViewportRect glr_lb =
                    compute_world_viewport_rect(lb.w, lb.h, window_width, window_height, lr);
                const GLViewportRect glr = { lb.x + glr_lb.x,
                                             lb_gl_y + glr_lb.y,
                                             glr_lb.width,
                                             glr_lb.height };
                GLint vp[4];
                glGetIntegerv(GL_VIEWPORT, vp);
                glViewport(0, 0, renderer->width, renderer->height);
                glEnable(GL_SCISSOR_TEST);
                glScissor(glr.x, glr.y, glr.width, glr.height);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glDisable(GL_SCISSOR_TEST);
                glViewport(vp[0], vp[1], vp[2], vp[3]);
                break;
            }

            case TORIRS_GFX_MODEL_DRAW:
            {
                struct DashModel* model = cmd._model_draw.model;
                if( !model || !dashmodel_face_colors_a(model) || !dashmodel_face_colors_b(model) ||
                    !dashmodel_face_colors_c(model) || !dashmodel_vertices_x(model) ||
                    !dashmodel_vertices_y(model) || !dashmodel_vertices_z(model) ||
                    !dashmodel_face_indices_a(model) || !dashmodel_face_indices_b(model) ||
                    !dashmodel_face_indices_c(model) || dashmodel_face_count(model) <= 0 )
                {
                    break;
                }

                uint64_t model_key = cmd._model_draw.model_key;
                if( model_id_from_model_cache_key(model_key) <= 0 )
                    break;
                auto it = renderer->model_index_by_key.find(model_key);
                if( it == renderer->model_index_by_key.end() )
                {
                    int model_idx = renderer->next_model_index++;
                    renderer->model_index_by_key[model_key] = model_idx;
                    renderer->loaded_model_keys.insert(model_key);
                    pix3dgl_model_load(
                        renderer->pix3dgl,
                        model_idx,
                        dashmodel_vertices_x(model),
                        dashmodel_vertices_y(model),
                        dashmodel_vertices_z(model),
                        dashmodel_face_indices_a(model),
                        dashmodel_face_indices_b(model),
                        dashmodel_face_indices_c(model),
                        dashmodel_face_count(model),
                        dashmodel_face_textures(model),
                        dashmodel_face_texture_coords(model),
                        dashmodel_textured_p_coordinate(model),
                        dashmodel_textured_m_coordinate(model),
                        dashmodel_textured_n_coordinate(model),
                        dashmodel_face_colors_a(model),
                        dashmodel_face_colors_b(model),
                        dashmodel_face_colors_c(model),
                        dashmodel_face_infos(model),
                        dashmodel_face_alphas(model));
                    it = renderer->model_index_by_key.find(model_key);
                }

                /* FrameNextCommand already projected this model (project_models=true).
                 * The sys_dash projection state is still valid — use it directly. */
                struct DashPosition draw_position = cmd._model_draw.position;
                int face_order_count = dash3d_prepare_projected_face_order(
                    game->sys_dash, model, &draw_position, game->view_port, game->camera);
                const int* face_order =
                    dash3d_projected_face_order(game->sys_dash, &face_order_count);
                pix3dgl_model_draw_ordered(
                    renderer->pix3dgl,
                    it->second,
                    (float)draw_position.x,
                    (float)draw_position.y,
                    (float)draw_position.z,
                    yaw_to_radians(draw_position.yaw),
                    face_order,
                    face_order_count);
                break;
            }

            case TORIRS_GFX_SPRITE_LOAD:
            case TORIRS_GFX_SPRITE_UNLOAD:
            case TORIRS_GFX_SPRITE_DRAW:
                sprite_cmds.push_back(cmd);
                break;

            case TORIRS_GFX_BEGIN_3D:
            case TORIRS_GFX_END_3D:
            case TORIRS_GFX_BEGIN_2D:
            case TORIRS_GFX_END_2D:
            case TORIRS_GFX_VERTEX_ARRAY_LOAD:
            case TORIRS_GFX_VERTEX_ARRAY_UNLOAD:
            case TORIRS_GFX_FACE_ARRAY_LOAD:
            case TORIRS_GFX_FACE_ARRAY_UNLOAD:
                break;

            default:
                break;
            }
        }
    }

    glViewport(world_viewport.x, world_viewport.y, world_viewport.width, world_viewport.height);
    pix3dgl_end_3dframe(renderer->pix3dgl);

    /* Sprites are authored in output (game_screen) space; map them into the letterbox
     * area of the canvas so they scale with the 3D world. */
    glViewport(lb.x, lb_gl_y, lb.w, lb.h);

    for( const auto& sc : sprite_cmds )
    {
        if( sc.kind == TORIRS_GFX_SPRITE_LOAD )
        {
            struct DashSprite* sp = sc._sprite_load.sprite;
            if( sp )
                pix3dgl_sprite_load(renderer->pix3dgl, sp);
        }
        else if( sc.kind == TORIRS_GFX_SPRITE_UNLOAD )
        {
            struct DashSprite* sp = sc._sprite_load.sprite;
            if( sp )
                pix3dgl_sprite_unload(renderer->pix3dgl, sp);
        }
    }

    pix3dgl_begin_2dframe(renderer->pix3dgl);
    for( const auto& sc : sprite_cmds )
    {
        if( sc.kind != TORIRS_GFX_SPRITE_DRAW )
            continue;
        struct DashSprite* sp = sc._sprite_draw.sprite;
        if( !sp )
            continue;
        /* Sprite coordinates are authored in logical screen space; match OpenGL3 renderer. */
        pix3dgl_sprite_draw(
            renderer->pix3dgl,
            sp,
            sc._sprite_draw.dst_bb_x,
            sc._sprite_draw.dst_bb_y,
            window_width,
            window_height,
            sc._sprite_draw.rotation_r2pi2048,
            sc._sprite_draw.src_bb_x,
            sc._sprite_draw.src_bb_y,
            sc._sprite_draw.src_bb_w,
            sc._sprite_draw.src_bb_h);
    }
    pix3dgl_end_2dframe(renderer->pix3dgl);

    /* Font draws: emit per-glyph quads from the pre-built atlas textures. */
    if( renderer->font_program && renderer->font_vbo )
    {
        static std::vector<float> font_verts;
        font_verts.clear();

        GLuint current_font_tex = 0;
        struct DashPixFont* current_font_ptr = NULL;

        auto flush_font_batch = [&]() {
            if( font_verts.empty() || !current_font_tex )
                return;
            int vert_count = (int)(font_verts.size() / 8);

            glUseProgram(renderer->font_program);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);

            if( renderer->font_uniform_projection >= 0 && window_width > 0 && window_height > 0 )
            {
                float left = 0.0f;
                float right = (float)window_width;
                float bottom = (float)window_height;
                float top = 0.0f;
                float ortho[16] = { 0 };
                ortho[0] = 2.0f / (right - left);
                ortho[5] = 2.0f / (top - bottom);
                ortho[10] = -1.0f;
                ortho[12] = -(right + left) / (right - left);
                ortho[13] = -(top + bottom) / (top - bottom);
                ortho[15] = 1.0f;
                glUniformMatrix4fv(renderer->font_uniform_projection, 1, GL_FALSE, ortho);
            }
            if( renderer->font_uniform_tex >= 0 )
                glUniform1i(renderer->font_uniform_tex, 0);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, current_font_tex);

            glBindBuffer(GL_ARRAY_BUFFER, renderer->font_vbo);
            glBufferData(
                GL_ARRAY_BUFFER,
                (GLsizeiptr)(font_verts.size() * sizeof(float)),
                font_verts.data(),
                GL_STREAM_DRAW);

            const GLsizei stride = 8 * sizeof(float);
            if( renderer->font_attrib_pos >= 0 )
            {
                glEnableVertexAttribArray(renderer->font_attrib_pos);
                glVertexAttribPointer(
                    renderer->font_attrib_pos, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
            }
            if( renderer->font_attrib_uv >= 0 )
            {
                glEnableVertexAttribArray(renderer->font_attrib_uv);
                glVertexAttribPointer(
                    renderer->font_attrib_uv,
                    2,
                    GL_FLOAT,
                    GL_FALSE,
                    stride,
                    (void*)(2 * sizeof(float)));
            }
            if( renderer->font_attrib_color >= 0 )
            {
                glEnableVertexAttribArray(renderer->font_attrib_color);
                glVertexAttribPointer(
                    renderer->font_attrib_color,
                    4,
                    GL_FLOAT,
                    GL_FALSE,
                    stride,
                    (void*)(4 * sizeof(float)));
            }

            glDrawArrays(GL_TRIANGLES, 0, vert_count);

            if( renderer->font_attrib_pos >= 0 )
                glDisableVertexAttribArray(renderer->font_attrib_pos);
            if( renderer->font_attrib_uv >= 0 )
                glDisableVertexAttribArray(renderer->font_attrib_uv);
            if( renderer->font_attrib_color >= 0 )
                glDisableVertexAttribArray(renderer->font_attrib_color);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glUseProgram(0);

            font_verts.clear();
        };

        for( const auto& sc : sprite_cmds )
        {
            if( sc.kind != TORIRS_GFX_FONT_DRAW )
                continue;
            struct DashPixFont* f = sc._font_draw.font;
            if( !f || !f->atlas || !sc._font_draw.text || f->height2d <= 0 )
                continue;

            auto it = renderer->font_atlas_cache.find(f);
            if( it == renderer->font_atlas_cache.end() )
            {
                webgl1_font_load(renderer, f);
                it = renderer->font_atlas_cache.find(f);
                if( it == renderer->font_atlas_cache.end() )
                    continue;
            }

            if( current_font_ptr != f )
            {
                flush_font_batch();
                current_font_ptr = f;
                current_font_tex = it->second.texture;
            }

            struct DashFontAtlas* atlas = f->atlas;
            const float inv_aw = 1.0f / (float)atlas->atlas_width;
            const float inv_ah = 1.0f / (float)atlas->atlas_height;

            const uint8_t* text = sc._font_draw.text;
            int length = (int)strlen((const char*)text);
            int color_rgb = sc._font_draw.color_rgb;
            float cr = (float)((color_rgb >> 16) & 0xFF) / 255.0f;
            float cg = (float)((color_rgb >> 8) & 0xFF) / 255.0f;
            float cb = (float)(color_rgb & 0xFF) / 255.0f;
            float ca = 1.0f;
            int pen_x = sc._font_draw.x;
            int base_y = sc._font_draw.y;

            for( int i = 0; i < length; i++ )
            {
                if( text[i] == '@' && i + 5 <= length && text[i + 4] == '@' )
                {
                    int new_color = dashfont_evaluate_color_tag((const char*)&text[i + 1]);
                    if( new_color >= 0 )
                    {
                        color_rgb = new_color;
                        cr = (float)((color_rgb >> 16) & 0xFF) / 255.0f;
                        cg = (float)((color_rgb >> 8) & 0xFF) / 255.0f;
                        cb = (float)(color_rgb & 0xFF) / 255.0f;
                    }
                    if( i + 6 <= length && text[i + 5] == ' ' )
                        i += 5;
                    else
                        i += 4;
                    continue;
                }

                int c = dashfont_charcode_to_glyph(text[i]);
                if( c < DASH_FONT_CHAR_COUNT )
                {
                    int gw = atlas->glyph_w[c];
                    int gh = atlas->glyph_h[c];
                    if( gw > 0 && gh > 0 )
                    {
                        float x0 = (float)(pen_x + f->char_offset_x[c]);
                        float y0 = (float)(base_y + f->char_offset_y[c]);
                        float x1 = x0 + (float)gw;
                        float y1 = y0 + (float)gh;

                        float u0 = (float)atlas->glyph_x[c] * inv_aw;
                        float v0 = (float)atlas->glyph_y[c] * inv_ah;
                        float u1 = (float)(atlas->glyph_x[c] + gw) * inv_aw;
                        float v1 = (float)(atlas->glyph_y[c] + gh) * inv_ah;

                        float v[6 * 8] = {
                            x0, y0, u0, v0, cr, cg, cb, ca,
                            x1, y0, u1, v0, cr, cg, cb, ca,
                            x1, y1, u1, v1, cr, cg, cb, ca,
                            x0, y0, u0, v0, cr, cg, cb, ca,
                            x1, y1, u1, v1, cr, cg, cb, ca,
                            x0, y1, u0, v1, cr, cg, cb, ca,
                        };
                        font_verts.insert(font_verts.end(), v, v + 48);
                    }
                }
                int adv = f->char_advance[c];
                if( adv <= 0 )
                    adv = 4;
                pen_x += adv;
            }
        }
        flush_font_batch();
    }

    LibToriRS_FrameEnd(game);

    glViewport(0, 0, renderer->width, renderer->height);
    render_nuklear_overlay(renderer, game);

    if( renderer->platform && renderer->platform->window )
        SDL_GL_SwapWindow(renderer->platform->window);

    s_frame++;
}
