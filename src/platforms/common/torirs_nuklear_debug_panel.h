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
    /** Optional WebGL1 / extended GPU stats (zero if unused). */
    unsigned gpu_submitted_model_draws;
    unsigned gpu_pose_invalid_skips;
    unsigned gpu_dynamic_index_draws;
    /** WebGL1 world 3D: `glDrawElements` calls after merge (same frame as other gpu_*). */
    unsigned gpu_gl_index_draw_calls;
    /** WebGL1: subdraw records accumulated (`draw_add_model`); compare to gpu_gl_index_draw_calls. */
    unsigned gpu_gl_pass_subdraws;
    unsigned gpu_gl_merge_brk_chunk;
    unsigned gpu_gl_merge_brk_vbo;
    unsigned gpu_gl_merge_brk_pool;
    unsigned gpu_gl_merge_brk_invalid;
    unsigned gpu_gl_merge_outer_skips;
    /** When set, show `frame_work_avg_ms` (rolling avg CPU work, see torirs_nk_ui_frame_work_*). */
    int include_frame_work_timing;
    double frame_work_avg_ms;
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
