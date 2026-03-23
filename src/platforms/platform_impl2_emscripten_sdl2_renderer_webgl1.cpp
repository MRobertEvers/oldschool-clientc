#include "platform_impl2_emscripten_sdl2_renderer_webgl1.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <GLES2/gl2.h>
#include <emscripten/html5.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <emscripten.h>
static float
yaw_to_radians(int yaw_r2pi2048)
{
    return (yaw_r2pi2048 * 2.0f * 3.14159265358979323846f) / 2048.0f;
}

static uintptr_t
model_gpu_cache_key(const struct DashModel* model)
{
    if( !model )
        return 0;
#if UINTPTR_MAX == 0xffffffffu
    uintptr_t key = 2166136261u;
#else
    uintptr_t key = 1469598103934665603ull;
#endif
    key ^= (uintptr_t)model->vertices_x;
    key *= (uintptr_t)16777619u;
    key ^= (uintptr_t)model->face_indices_a;
    key *= (uintptr_t)16777619u;
    key ^= (uintptr_t)model->face_indices_b;
    key *= (uintptr_t)16777619u;
    key ^= (uintptr_t)model->face_indices_c;
    key *= (uintptr_t)16777619u;
    key ^= (uintptr_t)model->face_count;
    key *= (uintptr_t)16777619u;
    return key;
}

static void
render_imgui_overlay(
    struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer,
    struct GGame* game)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 170), ImGuiCond_FirstUseEver);
    ImGui::Begin("WebGL1 Debug");
    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);
    ImGui::Text(
        "Camera: %d %d %d", game->camera_world_x, game->camera_world_y, game->camera_world_z);
    ImGui::Text("Mouse: %d %d", game->mouse_x, game->mouse_y);
    ImGui::Text("Loaded model keys: %zu", renderer->loaded_model_keys.size());
    ImGui::Text("Loaded scene keys: %zu", renderer->loaded_scene_element_keys.size());
    ImGui::Text("Loaded textures: %zu", renderer->loaded_texture_ids.size());
    ImGui::End();

    ImGui::Render();
    glDisable(GL_DEPTH_TEST);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static void
sync_canvas_size(struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer)
{
    double css_width = 0.0;
    double css_height = 0.0;
    emscripten_get_element_css_size("#canvas", &css_width, &css_height);
    int new_width = (int)css_width;
    int new_height = (int)css_height;
    if( new_width <= 0 || new_height <= 0 )
        return;

    if( new_width != renderer->width || new_height != renderer->height )
    {
        renderer->width = new_width;
        renderer->height = new_height;
        emscripten_set_canvas_element_size("#canvas", renderer->width, renderer->height);
        glViewport(0, 0, renderer->width, renderer->height);
        if( renderer->platform && renderer->platform->window )
            SDL_SetWindowSize(renderer->platform->window, renderer->width, renderer->height);
    }
}

struct Platform2_Emscripten_SDL2_Renderer_WebGL1*
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_New(
    int width,
    int height)
{
    auto* renderer = new Platform2_Emscripten_SDL2_Renderer_WebGL1();
    renderer->gl_context = NULL;
    renderer->platform = NULL;
    renderer->width = width;
    renderer->height = height;
    renderer->webgl_context_ready = false;
    renderer->pix3dgl = NULL;
    renderer->next_model_index = 1;
    return renderer;
}

void
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_Free(
    struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer)
{
    if( !renderer )
        return;
    if( renderer->pix3dgl )
    {
        pix3dgl_cleanup(renderer->pix3dgl);
        renderer->pix3dgl = NULL;
    }
    delete renderer;
}

bool
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_Init(
    struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer,
    struct Platform2_Emscripten_SDL2* platform)
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
    renderer->pix3dgl = pix3dgl_new();
    if( !renderer->pix3dgl )
    {
        printf("WebGL1 init failed: Pix3DGL setup failed\n");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if( !ImGui_ImplSDL2_InitForOpenGL(platform->window, renderer->gl_context) )
    {
        printf("ImGui SDL2 init failed for WebGL1\n");
        return false;
    }
    if( !ImGui_ImplOpenGL3_Init("#version 100") )
    {
        printf("ImGui OpenGL3 init failed for WebGL1\n");
        return false;
    }

    return true;
}

void
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_Render(
    struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer,
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

    pix3dgl_set_animation_clock(renderer->pix3dgl, (float)(emscripten_get_now() / 1000.0));
    pix3dgl_begin_frame(
        renderer->pix3dgl,
        (float)game->camera_world_x,
        (float)game->camera_world_y,
        (float)game->camera_world_z,
        (float)game->camera_pitch,
        (float)game->camera_yaw,
        (float)renderer->width,
        (float)renderer->height);

    // Process the command buffer in three ordered passes so that textures are always uploaded
    // to the atlas before any model that references them is uploaded to the GPU.  Atlas slots
    // and UV coordinates are baked into model VBOs at upload time and cannot be patched
    // afterwards (pix3dgl_model_load skips already-cached models), so correct ordering is
    // critical.  All static scene models (SCENE_ELEMENT_LOAD) are uploaded in pass 2 as a
    // single batch, guaranteed to follow every texture upload in pass 1.

    LibToriRS_FrameBegin(game, render_command_buffer);
    int total_commands = LibToriRS_RenderCommandBufferCount(render_command_buffer);

    // ── Pass 1: upload textures ───────────────────────────────────────────────────────────
    for( int i = 0; i < total_commands; i++ )
    {
        struct ToriRSRenderCommand* cmd =
            LibToriRS_RenderCommandBufferAt(render_command_buffer, i);
        if( !cmd || cmd->kind != TORIRS_GFX_TEXTURE_LOAD )
            continue;

        renderer->loaded_texture_ids.insert(cmd->_texture_load.texture_id);
        struct DashTexture* texture = cmd->_texture_load.texture_nullable;
        if( texture && texture->texels )
        {
            pix3dgl_load_texture(
                renderer->pix3dgl,
                cmd->_texture_load.texture_id,
                texture->texels,
                texture->width,
                texture->height,
                texture->animation_direction,
                texture->animation_speed,
                texture->opaque);
        }
    }

    // ── Pass 2: upload models (textures are now in the atlas) ─────────────────────────────
    for( int i = 0; i < total_commands; i++ )
    {
        struct ToriRSRenderCommand* cmd =
            LibToriRS_RenderCommandBufferAt(render_command_buffer, i);
        if( !cmd )
            continue;

        if( cmd->kind == TORIRS_GFX_MODEL_LOAD )
        {
            struct DashModel* model = cmd->_model_load.model;
            if( !model || !model->lighting || !model->vertices_x || !model->vertices_y ||
                !model->vertices_z || !model->face_indices_a || !model->face_indices_b ||
                !model->face_indices_c || model->face_count <= 0 )
            {
                continue;
            }
            uintptr_t model_key = model_gpu_cache_key(model);
            renderer->loaded_model_keys.insert(model_key);
            if( renderer->model_index_by_key.find(model_key) == renderer->model_index_by_key.end() )
            {
                int model_idx = renderer->next_model_index++;
                renderer->model_index_by_key[model_key] = model_idx;
                pix3dgl_model_load(
                    renderer->pix3dgl,
                    model_idx,
                    model->vertices_x,
                    model->vertices_y,
                    model->vertices_z,
                    model->face_indices_a,
                    model->face_indices_b,
                    model->face_indices_c,
                    model->face_count,
                    model->face_textures,
                    model->face_texture_coords,
                    model->textured_p_coordinate,
                    model->textured_m_coordinate,
                    model->textured_n_coordinate,
                    model->lighting->face_colors_hsl_a,
                    model->lighting->face_colors_hsl_b,
                    model->lighting->face_colors_hsl_c,
                    model->face_infos,
                    model->face_alphas);
            }
        }
        else if( cmd->kind == TORIRS_GFX_SCENE_ELEMENT_LOAD )
        {
            // Upload the static scene model keyed on scene_element_key.
            // All scene element models are uploaded here as a single batch (pass 2),
            // after all textures are available (pass 1).
            uintptr_t scene_key = cmd->_scene_element_load.scene_element_key;
            renderer->loaded_scene_element_keys.insert(scene_key);

            struct DashModel* model = cmd->_scene_element_load.model;
            if( !model || !model->lighting || !model->vertices_x || !model->vertices_y ||
                !model->vertices_z || !model->face_indices_a || !model->face_indices_b ||
                !model->face_indices_c || model->face_count <= 0 )
            {
                continue;
            }
            if( renderer->model_index_by_key.find(scene_key) == renderer->model_index_by_key.end() )
            {
                int model_idx = renderer->next_model_index++;
                renderer->model_index_by_key[scene_key] = model_idx;
                pix3dgl_model_load(
                    renderer->pix3dgl,
                    model_idx,
                    model->vertices_x,
                    model->vertices_y,
                    model->vertices_z,
                    model->face_indices_a,
                    model->face_indices_b,
                    model->face_indices_c,
                    model->face_count,
                    model->face_textures,
                    model->face_texture_coords,
                    model->textured_p_coordinate,
                    model->textured_m_coordinate,
                    model->textured_n_coordinate,
                    model->lighting->face_colors_hsl_a,
                    model->lighting->face_colors_hsl_b,
                    model->lighting->face_colors_hsl_c,
                    model->face_infos,
                    model->face_alphas);
            }
        }
    }

    // ── Pass 3: draw models ────────────────────────────────────────────────────────────────
    int model_draw_count = 0;
    for( int i = 0; i < total_commands; i++ )
    {
        struct ToriRSRenderCommand* cmd =
            LibToriRS_RenderCommandBufferAt(render_command_buffer, i);
        if( !cmd || cmd->kind != TORIRS_GFX_MODEL_DRAW )
            continue;

        model_draw_count++;
        struct DashModel* model = cmd->_model_draw.model;
        if( !model || !model->lighting || !model->vertices_x || !model->vertices_y ||
            !model->vertices_z || !model->face_indices_a || !model->face_indices_b ||
            !model->face_indices_c || model->face_count <= 0 )
        {
            continue;
        }

        uintptr_t model_key = model_gpu_cache_key(model);
        auto it = renderer->model_index_by_key.find(model_key);
        if( it == renderer->model_index_by_key.end() )
            continue;

        pix3dgl_model_draw(
            renderer->pix3dgl,
            it->second,
            (float)cmd->_model_draw.position.x,
            (float)cmd->_model_draw.position.y,
            (float)cmd->_model_draw.position.z,
            yaw_to_radians(cmd->_model_draw.position.yaw));
    }

    LibToriRS_FrameEnd(game);
    pix3dgl_end_frame(renderer->pix3dgl);

    s_frame++;
    if( (s_frame % 120) == 0 )
    {
        printf(
            "WebGL1 frame stats: cmds=%d draws=%d loaded_models=%zu loaded_scene=%zu\n",
            total_commands,
            model_draw_count,
            renderer->loaded_model_keys.size(),
            renderer->loaded_scene_element_keys.size());
    }

    render_imgui_overlay(renderer, game);

    if( renderer->platform && renderer->platform->window )
        SDL_GL_SwapWindow(renderer->platform->window);
}
