#ifndef PLATFORM_IMPL_EMSCRIPTEN_SDL2_H
#define PLATFORM_IMPL_EMSCRIPTEN_SDL2_H

struct Platform;

struct Platform* PlatformImpl_Emscripten_SDL2_New(void);

bool PlatformImpl_Emscripten_SDL2_InitForSoft3D(
    struct Platform* platform,
    int canvas_width,
    int canvas_height,
    int max_canvas_width,
    int max_canvas_height);
void PlatformImpl_Emscripten_SDL2_Shutdown(struct Platform* platform);

void PlatformImpl_Emscripten_SDL2_PollEvents(struct Platform* platform, struct GameInput* input);

#endif