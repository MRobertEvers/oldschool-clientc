#include "platforms/platform_impl2_emscripten_sdl2.h"
#include "tori_rs.h"

#include <emscripten.h>

void
signal_browser_ready()
{
    emscripten_run_script(
        "window.dispatchEvent(new CustomEvent('rs_client_ready', { "
        "detail: { timestamp: Date.now() } "
        "}));");
}

void
signal_browser_looped()
{
    emscripten_run_script(
        "window.dispatchEvent(new CustomEvent('rs_client_looped', { "
        "detail: { timestamp: Date.now() } "
        "}));");
}

void
emscripten_main_loop(void* arg)
{
    struct Platform2_Emscripten_SDL2* platform = (struct Platform2_Emscripten_SDL2*)arg;

    uint64_t timestamp_ms = SDL_GetTicks64();

    // Poll backend
    // Platform2_OSX_SDL2_PollIO(platform, io);
    Platform2_Emscripten_SDL2_PollEvents(platform);

    printf("F pressed: %d \n", platform->input->f_pressed);

    Platform2_Emscripten_SDL2_RunLuaScripts(platform);

    signal_browser_looped();
}

int
main(
    int argc,
    char* argv[])
{
    struct Platform2_Emscripten_SDL2* platform = Platform2_Emscripten_SDL2_New();

    if( !platform )
    {
        printf("Failed to create platform\n");
        return 1;
    }

    Platform2_Emscripten_SDL2_InitForSoft3D(platform, 1024, 768);

    signal_browser_ready();

    emscripten_set_main_loop_arg(emscripten_main_loop, platform, 0, 1);

    return 0;
}
