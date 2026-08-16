/*
 * ev_wasm — the WebAssembly face of the render core.
 *
 * Nothing here but the export list: the work is in ev_render.c, which also
 * compiles natively so the same path can be tested outside a browser.
 */

#include "ev_render.h"

#include <emscripten/emscripten.h>

#define EV_EXPORT EMSCRIPTEN_KEEPALIVE

EV_EXPORT void ev_w_init(void) { ev_init(); }
EV_EXPORT void* ev_w_alloc(int n) { return ev_alloc(n); }
EV_EXPORT void ev_w_release(void* p) { ev_release(p); }
EV_EXPORT int ev_w_set_model(const uint8_t* d, int n) { return ev_set_model(d, n); }
EV_EXPORT int ev_w_set_anim(const uint8_t* d, int n) { return ev_set_anim(d, n); }
EV_EXPORT void ev_w_clear_anim(void) { ev_clear_anim(); }
EV_EXPORT int ev_w_frame_count(void) { return ev_frame_count(); }
EV_EXPORT int ev_w_frame_delay(int i) { return ev_frame_delay(i); }
EV_EXPORT int ev_w_model_height(void) { return ev_model_height(); }
EV_EXPORT int ev_w_last_cull(void) { return ev_last_cull(); }
EV_EXPORT void ev_w_set_pan(int x, int y) { ev_set_pan(x, y); }
EV_EXPORT void ev_w_set_zbuffer(int on) { ev_set_zbuffer(on); }
EV_EXPORT void ev_w_set_frame_height(int h) { ev_set_frame_height(h); }

/* The attached graphic. A page that never calls these gets exactly the previous
 * behaviour — the spot slot starts empty and an empty slot merges nothing. */
EV_EXPORT int ev_w_set_spot_model(const uint8_t* d, int n) { return ev_set_spot_model(d, n); }
EV_EXPORT int ev_w_set_spot_anim(const uint8_t* d, int n) { return ev_set_spot_anim(d, n); }
EV_EXPORT void ev_w_clear_spot(void) { ev_clear_spot(); }
EV_EXPORT void ev_w_set_spot_state(int h, int f) { ev_set_spot_state(h, f); }
EV_EXPORT int ev_w_spot_frame_count(void) { return ev_spot_frame_count(); }
EV_EXPORT int ev_w_spot_frame_delay(int i) { return ev_spot_frame_delay(i); }

/* The HD model: a file picked off disk, drawn through ToriDraw_RenderHD. */
EV_EXPORT int ev_w_set_model_hd(const uint8_t* d, int n) { return ev_set_model_hd(d, n); }
EV_EXPORT void ev_w_clear_model_hd(void) { ev_clear_model_hd(); }
EV_EXPORT int ev_w_model_hd_active(void) { return ev_model_hd_active(); }
EV_EXPORT const int* ev_w_hd_stats(void) { return ev_hd_stats(); }
EV_EXPORT int ev_w_hd_stats_count(void) { return ev_hd_stats_count(); }
EV_EXPORT void ev_w_set_hd_placeholder(int on) { ev_set_hd_placeholder(on); }

/* Textures, baked server-side and shipped as an EVT1 blob — the browser has no
 * cache to bake them from. */
EV_EXPORT int ev_w_set_textures(const uint8_t* d, int n) { return ev_set_textures(d, n); }
EV_EXPORT int ev_w_texture_count(void) { return ev_texture_count(); }

/* Free-fly movement. */
EV_EXPORT void ev_w_move(int f, int r, int u) { ev_move(f, r, u); }
EV_EXPORT void ev_w_move_reset(void) { ev_move_reset(); }

/* Face priorities off, to tell a bad priority from a bad depth sort. */
EV_EXPORT void ev_w_set_ignore_priorities(int on) { ev_set_ignore_priorities(on); }

EV_EXPORT uint8_t*
ev_w_render(int w, int h, int yaw, int pitch, int zoom, int frame)
{
    return ev_render(w, h, yaw, pitch, zoom, frame);
}
