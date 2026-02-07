#ifndef PLATFORM_IMPL2_OSX_SDL2_RENDERER_SOFT3D_H
#define PLATFORM_IMPL2_OSX_SDL2_RENDERER_SOFT3D_H

#include "platform_impl2_osx_sdl2.h"

struct Platform2_OSX_SDL2_Renderer_Soft3D
{
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    struct Platform2_OSX_SDL2* platform;
    int* pixel_buffer;
    int width;
    int height;
    int max_width;
    int max_height;

    // Separate buffer for dash rendering
    int* dash_buffer;
    int dash_buffer_width;
    int dash_buffer_height;
    int dash_offset_x;
    int dash_offset_y;

    // Separate buffer for minimap rendering
    int* minimap_buffer;
    int minimap_buffer_width;
    int minimap_buffer_height;

    float time_delta_accumulator;

    // Clicked tile information
    int first_frame;
    int clicked_tile_x;
    int clicked_tile_z;
    int clicked_tile_level;

    // Outgoing message buffer for server communication
    uint8_t outgoing_message_buffer[256];
    int outgoing_message_size;

    // Client-side position interpolation
    int client_player_pos_tile_x;
    int client_player_pos_tile_z;
    int client_target_tile_x;
    int client_target_tile_z;
    uint64_t last_move_time_ms;
};

struct Platform2_OSX_SDL2_Renderer_Soft3D*
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_New(
    int width,
    int height,
    int max_width,
    int max_height);

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Free(struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer);

bool
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Init(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct Platform2_OSX_SDL2* platform);

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Shutdown(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer);

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Render(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_SetDashOffset(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    int offset_x,
    int offset_y);

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_InitExampleInterface(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game);

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_ProcessServer(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct Server* server,
    struct GGame* game,
    uint64_t timestamp_ms);

#endif