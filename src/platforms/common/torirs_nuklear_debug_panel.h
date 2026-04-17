#ifndef TORIRS_NUKLEAR_DEBUG_PANEL_H
#define TORIRS_NUKLEAR_DEBUG_PANEL_H

#include <stddef.h>

struct nk_context;
struct GGame;

typedef struct TorirsNkDebugPanelParams
{
    const char* window_title;
    double delta_time_sec;
    int view_w_cap;
    int view_h_cap;
    /** If non-zero, window_mouse_x/y are valid (caller pre-queries, e.g. SDL or Win32). */
    int window_mouse_valid;
    int window_mouse_x;
    int window_mouse_y;
    /** If non-NULL, show soft3d-style extras (trap, dynamic pixel, render size, tile copy).
     *  Checkbox reads/writes 0 or 1 through this pointer. */
    int* pixel_size_dynamic_inout;
    int render_width;
    int render_height;
    int max_width;
    int max_height;
    int clicked_tile_x;
    int clicked_tile_z;
    int clicked_tile_level;
    void (*set_clipboard_text)(const char* text);
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
