#ifndef TORIRS_NK_METAL_SDL_H
#define TORIRS_NK_METAL_SDL_H

#include "torirs_nuklear.h"

#ifdef __cplusplus
extern "C" {
#endif

struct TorirsNkMetalUi;

struct TorirsNkMetalUi*
torirs_nk_metal_init(void* mtl_device, int width, int height, unsigned max_vertex_bytes, unsigned max_index_bytes);

void
torirs_nk_metal_shutdown(struct TorirsNkMetalUi* ui);

struct nk_context*
torirs_nk_metal_ctx(struct TorirsNkMetalUi* ui);

void
torirs_nk_metal_font_stash_begin(struct TorirsNkMetalUi* ui, struct nk_font_atlas** atlas);

void
torirs_nk_metal_font_stash_end(struct TorirsNkMetalUi* ui);

void
torirs_nk_metal_resize(struct TorirsNkMetalUi* ui, int width, int height);

/* encoder: id<MTLRenderCommandEncoder> */
/** `window_*` = Nuklear layout / clip space (often SDL window pixels). `drawable_*` = CAMetalLayer drawable (HiDPI). */
void
torirs_nk_metal_render(
    struct TorirsNkMetalUi* ui,
    void* mtl_render_command_encoder,
    int window_width,
    int window_height,
    int drawable_width,
    int drawable_height,
    enum nk_anti_aliasing aa);

#ifdef __cplusplus
}
#endif

#endif
