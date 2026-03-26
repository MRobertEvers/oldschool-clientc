#ifndef PLATFORM_IMPL2_OSX_SDL2_RENDERER_OPENGL3_H
#define PLATFORM_IMPL2_OSX_SDL2_RENDERER_OPENGL3_H

#include "platform_impl2_osx_sdl2.h"
#include <SDL.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#endif

extern "C" {
#include "osrs/game.h"
#include "osrs/pix3dgl.h"
#include "tori_rs.h"
}

struct GLFontAtlasEntry
{
    GLuint texture;
};

struct Platform2_OSX_SDL2_Renderer_OpenGL3
{
    SDL_GLContext gl_context;
    struct Platform2_OSX_SDL2* platform;
    int width;
    int height;
    bool gl_context_ready;
    struct Pix3DGL* pix3dgl;
    int next_model_index;
    std::unordered_map<uintptr_t, int> model_index_by_key;
    std::unordered_set<uintptr_t> loaded_model_keys;
    std::unordered_set<uintptr_t> loaded_scene_element_keys;
    std::unordered_set<int> loaded_texture_ids;

    std::unordered_map<struct DashPixFont*, GLFontAtlasEntry> font_atlas_cache;
    GLuint font_program;
    GLuint font_vao;
    GLuint font_vbo;
    GLint font_uniform_projection;
    GLint font_uniform_tex;
};

struct Platform2_OSX_SDL2_Renderer_OpenGL3*
PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_New(
    int width,
    int height);

void
PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_Free(
    struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer);

bool
PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_Init(
    struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer,
    struct Platform2_OSX_SDL2* platform);

void
PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_Render(
    struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

#endif
