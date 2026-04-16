#ifndef TORIRS_NUKLEAR_DEBUG_PANEL_H
#define TORIRS_NUKLEAR_DEBUG_PANEL_H

#include <SDL.h>
#include <stddef.h>

struct nk_context;
struct GGame;
struct Platform2_SDL2_Renderer_Soft3D;

typedef struct TorirsNkDebugPanelParams
{
    const char* window_title;
    double delta_time_sec;
    int view_w_cap;
    int view_h_cap;
    SDL_Window* sdl_window;
    struct Platform2_SDL2_Renderer_Soft3D* soft3d;
    int include_soft3d_extras;
    int include_load_counts;
    size_t loaded_models;
    size_t loaded_scenes;
    size_t loaded_textures;
    int include_gpu_frame_stats;
    unsigned gpu_model_draws;
    unsigned gpu_tris;
} TorirsNkDebugPanelParams;

#ifdef __cplusplus
extern "C" {
#endif

void
torirs_nk_debug_panel_draw(struct nk_context* nk, struct GGame* game, TorirsNkDebugPanelParams* p);

#ifdef __cplusplus
}
#endif

#endif
