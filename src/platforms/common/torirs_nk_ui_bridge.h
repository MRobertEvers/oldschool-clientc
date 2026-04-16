#ifndef TORIRS_NK_UI_BRIDGE_H
#define TORIRS_NK_UI_BRIDGE_H

#include <SDL.h>
#include <stdbool.h>

struct nk_context;

#ifdef __cplusplus
extern "C" {
#endif

void
torirs_nk_ui_set_active(
    struct nk_context* ctx,
    int (*handle_sdl_event)(SDL_Event* evt),
    void (*handle_grab)(void));

void
torirs_nk_ui_clear_active(void);

void
torirs_nk_ui_pre_poll_events(void);

void
torirs_nk_ui_process_event(SDL_Event* evt);

void
torirs_nk_ui_post_poll_events(void);

/* Call after building UI for the frame (before nk_*_render). Uses last frame widget state. */
void
torirs_nk_ui_after_frame(struct nk_context* ctx);

bool
torirs_nk_ui_want_capture_keyboard_prev(void);

bool
torirs_nk_ui_want_capture_mouse_prev(void);

/* Sub-millisecond frame delta from SDL_GetPerformanceCounter (not SDL_GetTicks64). */
double
torirs_nk_ui_frame_delta_sec(Uint64* prev_counter);

#ifdef __cplusplus
}
#endif

#endif
