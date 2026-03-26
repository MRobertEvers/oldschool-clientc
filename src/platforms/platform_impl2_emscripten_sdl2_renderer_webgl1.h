#ifndef PLATFORM_IMPL2_EMSCRIPTEN_SDL2_RENDERER_WEBGL1_H
#define PLATFORM_IMPL2_EMSCRIPTEN_SDL2_RENDERER_WEBGL1_H

#include "platform_impl2_emscripten_sdl2.h"
#include <GLES2/gl2.h>
#include <unordered_map>
#include <unordered_set>

#include <SDL.h>
#include <vector>

extern "C" {
#include "osrs/game.h"
#include "osrs/pix3dgl.h"
#include "tori_rs.h"
}

struct GLFontAtlasEntryES2
{
    GLuint texture;
};

struct Platform2_Emscripten_SDL2_Renderer_WebGL1
{
    SDL_GLContext gl_context;
    struct Platform2_Emscripten_SDL2* platform;
    int width;
    int height;
    int max_width;
    int max_height;
    bool webgl_context_ready;
    struct Pix3DGL* pix3dgl;
    int next_model_index;
    std::unordered_map<uintptr_t, int> model_index_by_key;
    std::unordered_set<uintptr_t> loaded_model_keys;
    std::unordered_set<uintptr_t> loaded_scene_element_keys;
    std::unordered_set<int> loaded_texture_ids;

    std::unordered_map<struct DashPixFont*, GLFontAtlasEntryES2> font_atlas_cache;
    GLuint font_program;
    GLuint font_vbo;
    GLint font_attrib_pos;
    GLint font_attrib_uv;
    GLint font_attrib_color;
    GLint font_uniform_projection;
    GLint font_uniform_tex;
};

struct Platform2_Emscripten_SDL2_Renderer_WebGL1*
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_New(
    int width,
    int height,
    int max_width,
    int max_height);

void
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_Free(
    struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer);

bool
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_Init(
    struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer,
    struct Platform2_Emscripten_SDL2* platform);

void
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_Render(
    struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

#endif
