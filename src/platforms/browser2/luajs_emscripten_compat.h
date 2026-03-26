#ifndef LUAJS_EMSCRIPTEN_COMPAT_H
#define LUAJS_EMSCRIPTEN_COMPAT_H

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#endif
