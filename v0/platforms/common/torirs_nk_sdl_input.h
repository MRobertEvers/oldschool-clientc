#ifndef TORIRS_NK_SDL_INPUT_H
#define TORIRS_NK_SDL_INPUT_H

#include <SDL.h>

struct nk_context;

#ifdef __cplusplus
extern "C" {
#endif

/* Feed one SDL event into Nuklear (call between nk_input_begin / nk_input_end). Returns 1 if handled. */
int
torirs_nk_sdl_translate_event(struct nk_context* ctx, SDL_Event* evt);

#ifdef __cplusplus
}
#endif

#endif
