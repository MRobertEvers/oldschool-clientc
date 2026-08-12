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

EV_EXPORT uint8_t*
ev_w_render(int w, int h, int yaw, int pitch, int zoom, int frame)
{
    return ev_render(w, h, yaw, pitch, zoom, frame);
}
