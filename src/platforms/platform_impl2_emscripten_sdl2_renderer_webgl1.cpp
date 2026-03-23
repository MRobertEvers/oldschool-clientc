#include "platform_impl2_emscripten_sdl2_renderer_webgl1.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <GLES2/gl2.h>
#include <emscripten/html5.h>

#include <cstdio>
#include <cstring>

static GLuint
compile_shader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char log[512] = { 0 };
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        printf("WebGL shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool
init_blit_pipeline(struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer)
{
    const char* vs_src =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main(){ v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    const char* fs_src =
        "precision mediump float;\n"
        "varying vec2 v_uv;\n"
        "uniform sampler2D u_tex;\n"
        "void main(){ gl_FragColor = texture2D(u_tex, v_uv); }\n";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if( !vs || !fs )
        return false;

    renderer->blit_program = glCreateProgram();
    glAttachShader(renderer->blit_program, vs);
    glAttachShader(renderer->blit_program, fs);
    glBindAttribLocation(renderer->blit_program, 0, "a_pos");
    glBindAttribLocation(renderer->blit_program, 1, "a_uv");
    glLinkProgram(renderer->blit_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(renderer->blit_program, GL_LINK_STATUS, &ok);
    if( !ok )
    {
        char log[512] = { 0 };
        glGetProgramInfoLog(renderer->blit_program, sizeof(log), NULL, log);
        printf("WebGL program link failed: %s\n", log);
        return false;
    }

    const float quad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
        1.0f,  1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 1.0f,
    };
    const uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };

    glGenBuffers(1, &renderer->blit_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->blit_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    glGenBuffers(1, &renderer->blit_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->blit_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glGenTextures(1, &renderer->blit_texture);
    glBindTexture(GL_TEXTURE_2D, renderer->blit_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    renderer->blit_width = 0;
    renderer->blit_height = 0;
    return true;
}

static void
draw_blit_texture(struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer)
{
    glUseProgram(renderer->blit_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer->blit_texture);
    GLint tex_loc = glGetUniformLocation(renderer->blit_program, "u_tex");
    glUniform1i(tex_loc, 0);

    glBindBuffer(GL_ARRAY_BUFFER, renderer->blit_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->blit_ebo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
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
    renderer->blit_program = 0;
    renderer->blit_vbo = 0;
    renderer->blit_ebo = 0;
    renderer->blit_texture = 0;
    renderer->blit_width = 0;
    renderer->blit_height = 0;
    return renderer;
}

void
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_Free(
    struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer)
{
    if( !renderer )
        return;
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
    if( !init_blit_pipeline(renderer) )
    {
        printf("WebGL1 init failed: blit pipeline setup failed\n");
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
    if( !renderer || !renderer->webgl_context_ready || !game || !render_command_buffer )
        return;

    emscripten_webgl_make_context_current((EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)renderer->gl_context);
    sync_canvas_size(renderer);

    glViewport(0, 0, renderer->width, renderer->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const int fb_w = game->view_port ? game->view_port->width : 513;
    const int fb_h = game->view_port ? game->view_port->height : 335;
    if( renderer->blit_width != fb_w || renderer->blit_height != fb_h )
    {
        renderer->blit_width = fb_w;
        renderer->blit_height = fb_h;
        renderer->blit_pixels.resize((size_t)fb_w * (size_t)fb_h);
        renderer->blit_rgba.resize((size_t)fb_w * (size_t)fb_h * 4u);
        glBindTexture(GL_TEXTURE_2D, renderer->blit_texture);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            renderer->blit_width,
            renderer->blit_height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            renderer->blit_rgba.data());
    }
    memset(renderer->blit_pixels.data(), 0, renderer->blit_pixels.size() * sizeof(uint32_t));

    LibToriRS_FrameBegin(game, render_command_buffer);
    struct ToriRSRenderCommand command = { 0 };
    int command_count = 0;
    int model_draw_count = 0;
    int model_draw_not_ready = 0;
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &command) )
    {
        command_count++;
        switch( command.kind )
        {
        case TORIRS_GFX_MODEL_LOAD:
            renderer->loaded_model_keys.insert(command._model_load.model_key);
            break;
        case TORIRS_GFX_SCENE_ELEMENT_LOAD:
            renderer->loaded_scene_element_keys.insert(
                command._scene_element_load.scene_element_key);
            break;
        case TORIRS_GFX_TEXTURE_LOAD:
            renderer->loaded_texture_ids.insert(command._texture_load.texture_id);
            break;
        case TORIRS_GFX_MODEL_DRAW:
        {
            uintptr_t model_key = (uintptr_t)command._model_draw.model;
            bool ready =
                renderer->loaded_model_keys.find(model_key) != renderer->loaded_model_keys.end();
            model_draw_count++;
            if( !ready )
                model_draw_not_ready++;
            dash3d_raster_projected_model(
                game->sys_dash,
                command._model_draw.model,
                &command._model_draw.position,
                game->view_port,
                game->camera,
                (int*)renderer->blit_pixels.data(),
                false);
            break;
        }
        default:
            break;
        }
    }
    LibToriRS_FrameEnd(game);

    // dash3d writes 0xAARRGGBB-style packed ints; convert explicitly to RGBA bytes for WebGL.
    for( size_t i = 0; i < renderer->blit_pixels.size(); ++i )
    {
        uint32_t argb = renderer->blit_pixels[i];
        uint8_t a = (uint8_t)((argb >> 24) & 0xFF);
        uint8_t r = (uint8_t)((argb >> 16) & 0xFF);
        uint8_t g = (uint8_t)((argb >> 8) & 0xFF);
        uint8_t b = (uint8_t)(argb & 0xFF);
        size_t out = i * 4u;
        renderer->blit_rgba[out + 0] = r;
        renderer->blit_rgba[out + 1] = g;
        renderer->blit_rgba[out + 2] = b;
        renderer->blit_rgba[out + 3] = (a == 0 ? 255 : a);
    }

    s_frame++;
    if( (s_frame % 120) == 0 )
    {
        printf(
            "WebGL1 frame stats: cmds=%d draws=%d not_ready=%d loaded_models=%zu\n",
            command_count,
            model_draw_count,
            model_draw_not_ready,
            renderer->loaded_model_keys.size());
    }

    glBindTexture(GL_TEXTURE_2D, renderer->blit_texture);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        renderer->blit_width,
        renderer->blit_height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        renderer->blit_rgba.data());
    draw_blit_texture(renderer);

    if( renderer->platform && renderer->platform->window )
        SDL_GL_SwapWindow(renderer->platform->window);
}
