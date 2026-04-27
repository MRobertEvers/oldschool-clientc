#ifndef PLATFORMS_OPENGL3_OPENGL3_ATLAS_RESOURCES_H
#define PLATFORMS_OPENGL3_OPENGL3_ATLAS_RESOURCES_H

#    include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"

#    include <cstdint>

struct Platform2_SDL2_Renderer_OpenGL3;
struct DashTexture;

void
opengl3_refresh_atlas_texture(Platform2_SDL2_Renderer_OpenGL3* r);

void
opengl3_write_atlas_tile_slot(
    Platform2_SDL2_Renderer_OpenGL3* r,
    uint16_t tex_id,
    const struct DashTexture* tex_nullable);

void
opengl3_rgba64_nearest_to_128(const uint8_t* src64, uint8_t* dst128);

void
opengl3_delete_gl_buffer(GPUResourceHandle h);

#endif
