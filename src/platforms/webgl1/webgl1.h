#ifndef PLATFORMS_WEBGL1_WEBGL1_H
#define PLATFORMS_WEBGL1_WEBGL1_H

#include "platforms/webgl1/webgl1_vertex.h"

#include "platforms/gpu_3d_cache.h"
#include "platforms/gpu_font_cache.h"
#include "platforms/gpu_sprite_cache.h"
#include "platforms/gpu_texture_cache.h"
#include "platforms/platform_impl2_sdl2.h"

#include <GLES2/gl2.h>
#include <SDL.h>
#include <unordered_map>
#include <vector>

extern "C" {
#include "osrs/game.h"
#include "tori_rs.h"
}

/** GPU world texture tile metadata (matches Metal `AtlasTile` / Shaders.metal). */
struct WebGL1AtlasTile
{
    float u0, v0, du, dv;
    float anim_u, anim_v;
    float opaque;
    float _pad;
};

static constexpr int kWebGL1WorldAtlasPageW = 2048;
static constexpr int kWebGL1WorldAtlasPageH = 2048;

struct Platform2_SDL2_Renderer_WebGL1
{
    SDL_GLContext gl_context;
    struct Platform2_SDL2* platform;
    int width;
    int height;
    int max_width;
    int max_height;
    bool webgl_context_ready;

    GpuTextureCache<GLuint> texture_cache;
    Gpu3DCache<GLuint> model_cache;
    GpuSpriteCache<GLuint> sprite_cache;
    GpuFontCache<GLuint> font_cache;

    GLuint world_atlas_tex;
    GLuint world_tile_meta_tex;
    int world_atlas_shelf_x;
    int world_atlas_shelf_y;
    int world_atlas_shelf_h;
    WebGL1AtlasTile world_tiles_cpu[256];

    GLuint program_world;
    GLint loc_world_uClockPad;
    GLint loc_world_uAtlas;
    GLint loc_world_uTileMeta;

    GLuint program_ui_sprite;
    GLint loc_ui_uProj;
    GLint loc_ui_uTex;

    GLuint program_ui_sprite_invrot;
    GLint loc_inv_uProj;
    GLint loc_inv_uTex;
    GLint loc_inv_uParams;

    GLuint font_program;
    GLuint font_vbo;
    GLint font_attrib_pos;
    GLint font_attrib_uv;
    GLint font_attrib_color;
    GLint font_uniform_projection;
    GLint font_uniform_tex;

    /** CPU mirror of uploaded model/chunk VBOs for WebGL1 flush (no texture buffer fetch). */
    std::unordered_map<unsigned int, std::vector<WgVertex>> geometry_mirror;

    uint32_t current_model_batch_id;
    uint32_t current_sprite_batch_id;
    uint32_t current_font_batch_id;

    std::unordered_map<int, struct DashVertexArray*> va_staging;

    std::vector<uint8_t> rgba_scratch;
    std::vector<float> float_tile_scratch;

    bool has_float_tex;
    bool has_instanced_arrays;

    /** Single GL_STREAM_DRAW buffer rebuilt each `wg1_flush_3d`. */
    GLuint exp_stream_vbo;

    unsigned int debug_model_draws;
    unsigned int debug_triangles;

    int texture_id_ever_loaded_count;
    bool texture_id_ever_loaded[256];
};

struct Platform2_SDL2_Renderer_WebGL1*
PlatformImpl2_SDL2_Renderer_WebGL1_New(
    int width,
    int height,
    int max_width,
    int max_height);

void
PlatformImpl2_SDL2_Renderer_WebGL1_Free(struct Platform2_SDL2_Renderer_WebGL1* renderer);

bool
PlatformImpl2_SDL2_Renderer_WebGL1_Init(
    struct Platform2_SDL2_Renderer_WebGL1* renderer,
    struct Platform2_SDL2* platform);

void
PlatformImpl2_SDL2_Renderer_WebGL1_Render(
    struct Platform2_SDL2_Renderer_WebGL1* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

#endif
