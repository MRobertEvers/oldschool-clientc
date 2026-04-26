#ifndef PLATFORM_IMPL2_SDL2_RENDERER_FLAGS_H
#define PLATFORM_IMPL2_SDL2_RENDERER_FLAGS_H

#include <stdint.h>

/** Bit flags: which renderer backend to create / use (not all platforms build all backends). */
#define TORIRS_SDL2_RENDERER_SOFT3D (1u << 0)
#define TORIRS_SDL2_RENDERER_OPENGL3 (1u << 1)
#define TORIRS_SDL2_RENDERER_METAL (1u << 2)
#define TORIRS_SDL2_RENDERER_D3D11 (1u << 3)
#define TORIRS_SDL2_RENDERER_WEBGL1 (1u << 4)

#if defined(__EMSCRIPTEN__)
#define TORIRS_SDL2_RENDERER_SUPPORT_MASK \
    ((uint32_t)(TORIRS_SDL2_RENDERER_SOFT3D | TORIRS_SDL2_RENDERER_WEBGL1))
#elif defined(__APPLE__)
#define TORIRS_SDL2_RENDERER_SUPPORT_MASK \
    ((uint32_t)(TORIRS_SDL2_RENDERER_SOFT3D | TORIRS_SDL2_RENDERER_OPENGL3 | TORIRS_SDL2_RENDERER_METAL))
#elif defined(_WIN32)
#if defined(TORIRS_NO_D3D11)
#define TORIRS_SDL2_RENDERER_SUPPORT_MASK ((uint32_t)TORIRS_SDL2_RENDERER_SOFT3D)
#else
#define TORIRS_SDL2_RENDERER_SUPPORT_MASK \
    ((uint32_t)(TORIRS_SDL2_RENDERER_SOFT3D | TORIRS_SDL2_RENDERER_D3D11))
#endif
#else
#define TORIRS_SDL2_RENDERER_SUPPORT_MASK \
    ((uint32_t)(TORIRS_SDL2_RENDERER_SOFT3D | TORIRS_SDL2_RENDERER_OPENGL3))
#endif

/** Compile-time mask of renderers supported on this build. */
#define Platform2_SDL2_SupportedRendererMask() ((uint32_t)TORIRS_SDL2_RENDERER_SUPPORT_MASK)

/** Non-zero if every bit in `renderer_flags` is supported on this platform build. */
#define Platform2_SDL2_RendererFlagSupported(renderer_flags) \
    ((int)(((uint32_t)TORIRS_SDL2_RENDERER_SUPPORT_MASK & (uint32_t)(renderer_flags)) == \
           (uint32_t)(renderer_flags)))

#endif
