#include "platform_impl2_osx_sdl2_renderer_opengl3.h"

#include "graphics/dash.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "tori_rs_render.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstring>

static const char* g_font_vertex_shader = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
uniform mat4 uProjection;
out vec2 vUv;
out vec4 vColor;
void main()
{
    vUv = aUv;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)";

static const char* g_font_fragment_shader = R"(
#version 330 core
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 FragColor;
void main()
{
    float a = texture(uTex, vUv).a;
    if( a < 0.01 )
        discard;
    FragColor = vec4(vColor.rgb, a * vColor.a);
}
)";

static GLuint
compile_shader(GLenum type, const char* source)
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
        printf("Font shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint
link_program(GLuint vert, GLuint frag)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if( !ok )
    {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        printf("Font program link error: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static bool
gl3_ensure_font_program(struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer)
{
    if( renderer->font_program )
        return true;

    GLuint vs = compile_shader(GL_VERTEX_SHADER, g_font_vertex_shader);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, g_font_fragment_shader);
    if( !vs || !fs )
    {
        if( vs )
            glDeleteShader(vs);
        if( fs )
            glDeleteShader(fs);
        return false;
    }
    renderer->font_program = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if( !renderer->font_program )
        return false;

    renderer->font_uniform_projection =
        glGetUniformLocation(renderer->font_program, "uProjection");
    renderer->font_uniform_tex =
        glGetUniformLocation(renderer->font_program, "uTex");

    glGenVertexArrays(1, &renderer->font_vao);
    glGenBuffers(1, &renderer->font_vbo);

    glBindVertexArray(renderer->font_vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->font_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

static void
gl3_font_load(
    struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer,
    struct DashPixFont* font)
{
    if( !font || !font->atlas )
        return;
    if( renderer->font_atlas_cache.count(font) )
        return;
    if( !gl3_ensure_font_program(renderer) )
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
        GL_RGBA8,
        atlas->atlas_width,
        atlas->atlas_height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        atlas->rgba_pixels);

    GLFontAtlasEntry entry;
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

    mix_word((uint64_t)(uintptr_t)model->vertices_x);
    mix_word((uint64_t)(uintptr_t)model->face_indices_a);
    mix_word((uint64_t)(uintptr_t)model->face_indices_b);
    mix_word((uint64_t)(uintptr_t)model->face_indices_c);
    mix_word((uint64_t)model->face_count);

    const bool is_animated = model->original_vertices_x && model->original_vertices_y &&
                             model->original_vertices_z && model->vertex_count > 0;
    if( is_animated )
    {
        for( int i = 0; i < model->vertex_count; ++i )
        {
            mix_word((uint64_t)(uint32_t)model->vertices_x[i]);
            mix_word((uint64_t)(uint32_t)model->vertices_y[i]);
            mix_word((uint64_t)(uint32_t)model->vertices_z[i]);
        }
    }
    return key;
}

static void
render_imgui_overlay(
    struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer,
    struct GGame* game)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 220), ImGuiCond_FirstUseEver);
    ImGui::Begin("OpenGL3 Debug");
    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);
    ImGui::Text(
        "Camera: %d %d %d", game->camera_world_x, game->camera_world_y, game->camera_world_z);
    ImGui::Text("Mouse: %d %d", game->mouse_x, game->mouse_y);
    if( game->view_port )
    {
        ImGui::Separator();
        int w = game->view_port->width;
        int h = game->view_port->height;
        bool changed = ImGui::InputInt("World viewport W", &w);
        changed |= ImGui::InputInt("World viewport H", &h);
        if( changed )
        {
            if( w > 4096 )
                w = 4096;
            if( h > 4096 )
                h = 4096;
            LibToriRS_GameSetWorldViewportSize(game, w, h);
        }
    }
    ImGui::Text("Loaded model keys: %zu", renderer->loaded_model_keys.size());
    ImGui::Text("Loaded scene keys: %zu", renderer->loaded_scene_element_keys.size());
    ImGui::Text("Loaded textures: %zu", renderer->loaded_texture_ids.size());
    ImGui::End();

    ImGui::Render();
    glDisable(GL_DEPTH_TEST);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    pix3dgl_restore_gl_state_after_imgui(renderer->pix3dgl);
}

static void
sync_drawable_size(struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer)
{
    if( !renderer || !renderer->platform || !renderer->platform->window )
        return;

    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GL_GetDrawableSize(renderer->platform->window, &drawable_width, &drawable_height);
    if( drawable_width <= 0 || drawable_height <= 0 )
        return;

    renderer->width = drawable_width;
    renderer->height = drawable_height;
}

struct Platform2_OSX_SDL2_Renderer_OpenGL3*
PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_New(
    int width,
    int height)
{
    auto* renderer = new Platform2_OSX_SDL2_Renderer_OpenGL3();
    renderer->gl_context = NULL;
    renderer->platform = NULL;
    renderer->width = width;
    renderer->height = height;
    renderer->gl_context_ready = false;
    renderer->pix3dgl = NULL;
    renderer->next_model_index = 1;
    renderer->font_program = 0;
    renderer->font_vao = 0;
    renderer->font_vbo = 0;
    renderer->font_uniform_projection = -1;
    renderer->font_uniform_tex = -1;
    return renderer;
}

void
PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_Free(struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer)
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
    if( renderer->font_vao )
        glDeleteVertexArrays(1, &renderer->font_vao);
    if( renderer->font_program )
        glDeleteProgram(renderer->font_program);

    if( renderer->pix3dgl )
    {
        pix3dgl_cleanup(renderer->pix3dgl);
        renderer->pix3dgl = NULL;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if( renderer->gl_context )
    {
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = NULL;
    }
    delete renderer;
}

bool
PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_Init(
    struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer,
    struct Platform2_OSX_SDL2* platform)
{
    if( !renderer || !platform || !platform->window )
        return false;

    renderer->platform = platform;
    renderer->gl_context = SDL_GL_CreateContext(platform->window);
    if( !renderer->gl_context )
    {
        printf("OpenGL3 init failed: could not create context: %s\n", SDL_GetError());
        return false;
    }
    if( SDL_GL_MakeCurrent(platform->window, renderer->gl_context) != 0 )
    {
        printf("OpenGL3 init failed: could not make context current: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetSwapInterval(0);
    renderer->gl_context_ready = true;
    sync_drawable_size(renderer);
    renderer->pix3dgl = pix3dgl_new(false, true);
    if( !renderer->pix3dgl )
    {
        printf("OpenGL3 init failed: Pix3DGL setup failed\n");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if( !ImGui_ImplSDL2_InitForOpenGL(platform->window, renderer->gl_context) )
    {
        printf("ImGui SDL2 init failed for OpenGL3\n");
        return false;
    }
#if defined(__APPLE__)
    if( !ImGui_ImplOpenGL3_Init("#version 150") )
#else
    if( !ImGui_ImplOpenGL3_Init("#version 130") )
#endif
    {
        printf("ImGui OpenGL3 init failed for OpenGL3 renderer\n");
        return false;
    }

    return true;
}

void
PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_Render(
    struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    static uint64_t s_frame = 0;
    if( !renderer || !renderer->gl_context_ready || !renderer->pix3dgl || !game ||
        !render_command_buffer || !renderer->platform || !renderer->platform->window )
    {
        return;
    }

    SDL_GL_MakeCurrent(renderer->platform->window, renderer->gl_context);
    sync_drawable_size(renderer);

    glViewport(0, 0, renderer->width, renderer->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int window_width = renderer->platform->game_screen_width;
    int window_height = renderer->platform->game_screen_height;
    if( window_width <= 0 || window_height <= 0 )
        SDL_GetWindowSize(renderer->platform->window, &window_width, &window_height);
    const LogicalViewportRect logical_viewport =
        compute_logical_viewport_rect(window_width, window_height, game);
    const GLViewportRect world_viewport = compute_world_viewport_rect(
        renderer->width, renderer->height, window_width, window_height, logical_viewport);
    glViewport(world_viewport.x, world_viewport.y, world_viewport.width, world_viewport.height);

    const float projection_width = (float)logical_viewport.width;
    const float projection_height = (float)logical_viewport.height;

    pix3dgl_set_animation_clock(renderer->pix3dgl, (float)(SDL_GetTicks64() / 20));
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
                gl3_font_load(renderer, cmd._font_load.font);
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
                if( !model || !model->lighting || !model->vertices_x || !model->vertices_y ||
                    !model->vertices_z || !model->face_indices_a || !model->face_indices_b ||
                    !model->face_indices_c || model->face_count <= 0 )
                {
                    break;
                }
                renderer->loaded_model_keys.insert(cmd._model_load.model_key);
                if( renderer->model_index_by_key.find(cmd._model_load.model_key) ==
                    renderer->model_index_by_key.end() )
                {
                    int model_idx = renderer->next_model_index++;
                    renderer->model_index_by_key[cmd._model_load.model_key] = model_idx;
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
                break;
            }

            case TORIRS_GFX_MODEL_DRAW:
            {
                struct DashModel* model = cmd._model_draw.model;
                if( !model || !model->lighting || !model->vertices_x || !model->vertices_y ||
                    !model->vertices_z || !model->face_indices_a || !model->face_indices_b ||
                    !model->face_indices_c || model->face_count <= 0 )
                {
                    break;
                }

                uint64_t model_key = cmd._model_draw.model_key;
                auto it = renderer->model_index_by_key.find(model_key);
                if( it == renderer->model_index_by_key.end() )
                {
                    int model_idx = renderer->next_model_index++;
                    renderer->model_index_by_key[model_key] = model_idx;
                    renderer->loaded_model_keys.insert(model_key);
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

            default:
                break;
            }
        }
    }

    glViewport(world_viewport.x, world_viewport.y, world_viewport.width, world_viewport.height);
    pix3dgl_end_3dframe(renderer->pix3dgl);

    /* Process sprite loads and unloads (update GPU texture cache). */
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

    glViewport(0, 0, renderer->width, renderer->height);
    pix3dgl_begin_2dframe(renderer->pix3dgl);

    for( const auto& sc : sprite_cmds )
    {
        if( sc.kind == TORIRS_GFX_SPRITE_DRAW )
        {
            struct DashSprite* sp = sc._sprite_draw.sprite;
            if( !sp )
                continue;
            pix3dgl_sprite_draw(
                renderer->pix3dgl,
                sp,
                sc._sprite_draw.x,
                sc._sprite_draw.y,
                window_width,
                window_height,
                sc._sprite_draw.rotation_r2pi2048);
        }
    }
    pix3dgl_end_2dframe(renderer->pix3dgl);

    /* Font draws: emit per-glyph quads from the pre-built atlas textures. */
    if( renderer->font_program && renderer->font_vao && renderer->font_vbo )
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

            glBindVertexArray(renderer->font_vao);
            glBindBuffer(GL_ARRAY_BUFFER, renderer->font_vbo);
            glBufferData(
                GL_ARRAY_BUFFER,
                (GLsizeiptr)(font_verts.size() * sizeof(float)),
                font_verts.data(),
                GL_STREAM_DRAW);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(
                1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(
                2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));

            glDrawArrays(GL_TRIANGLES, 0, vert_count);
            glBindVertexArray(0);
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
                gl3_font_load(renderer, f);
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
    render_imgui_overlay(renderer, game);
    SDL_GL_SwapWindow(renderer->platform->window);

    s_frame++;
}
