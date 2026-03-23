#ifndef PLATFORM_IMPL2_EMSCRIPTEN_SDL2_RENDERER_WEBGL1_H
#define PLATFORM_IMPL2_EMSCRIPTEN_SDL2_RENDERER_WEBGL1_H

#include "platform_impl2_emscripten_sdl2.h"

#include <SDL.h>
#include <GLES2/gl2.h>

#include <unordered_set>
#include <vector>

extern "C" {
#include "osrs/game.h"
#include "tori_rs.h"
}

struct Platform2_Emscripten_SDL2_Renderer_WebGL1
{
    SDL_GLContext gl_context;
    struct Platform2_Emscripten_SDL2* platform;
    int width;
    int height;
    bool webgl_context_ready;
    GLuint blit_program;
    GLuint blit_vbo;
    GLuint blit_ebo;
    GLuint blit_texture;
    int blit_width;
    int blit_height;
    std::vector<uint32_t> blit_pixels;
    std::vector<uint8_t> blit_rgba;
    std::unordered_set<uintptr_t> loaded_model_keys;
    std::unordered_set<uintptr_t> loaded_scene_element_keys;
    std::unordered_set<int> loaded_texture_ids;
};

struct Platform2_Emscripten_SDL2_Renderer_WebGL1*
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_New(
    int width,
    int height);

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
