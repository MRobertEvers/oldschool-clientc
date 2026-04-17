#include "torirs_nk_ui_bridge.h"

#include "nuklear/torirs_nuklear.h"
#include "torirs_nk_sdl_input.h"

#include <string.h>

double
torirs_nk_ui_frame_delta_sec(Uint64* prev_counter)
{
    Uint64 const now = SDL_GetPerformanceCounter();
    Uint64 const freq = SDL_GetPerformanceFrequency();
    if( !prev_counter || freq == 0 )
        return 1.0 / 60.0;
    if( *prev_counter == 0 )
    {
        *prev_counter = now;
        return 1.0 / 60.0;
    }
    double const dt = (double)(now - *prev_counter) / (double)freq;
    *prev_counter = now;
    return dt > 1e-12 ? dt : 1e-12;
}

static struct nk_context* s_ctx;
static int (*s_handle_sdl_event)(SDL_Event* evt);
static void (*s_handle_grab)(void);
static bool s_want_kb;
static bool s_want_mouse;

void
torirs_nk_ui_set_active(
    struct nk_context* ctx,
    int (*handle_sdl_event)(SDL_Event* evt),
    void (*handle_grab)(void))
{
    s_ctx = ctx;
    s_handle_sdl_event = handle_sdl_event;
    s_handle_grab = handle_grab;
}

void
torirs_nk_ui_clear_active(void)
{
    s_ctx = NULL;
    s_handle_sdl_event = NULL;
    s_handle_grab = NULL;
}

void
torirs_nk_ui_pre_poll_events(void)
{
    if( !s_ctx )
        return;
    nk_input_begin(s_ctx);
}

void
torirs_nk_ui_process_event(SDL_Event* evt)
{
    if( !s_ctx || !evt )
        return;
    if( s_handle_sdl_event )
        s_handle_sdl_event(evt);
    else
        torirs_nk_sdl_translate_event(s_ctx, evt);
}

void
torirs_nk_ui_post_poll_events(void)
{
    if( !s_ctx )
        return;
    nk_input_end(s_ctx);
}

void
torirs_nk_ui_after_frame(struct nk_context* ctx)
{
    if( !ctx )
        return;
    int any = nk_item_is_any_active(ctx);
    s_want_kb = any != 0;
    s_want_mouse = any != 0;
    if( s_handle_grab )
        s_handle_grab();
}

bool
torirs_nk_ui_want_capture_keyboard_prev(void)
{
    return s_want_kb;
}

bool
torirs_nk_ui_want_capture_mouse_prev(void)
{
    return s_want_mouse;
}
