#include "platform_impl2_sdl2.h"
#include "platform_impl2_sdl2_internal.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float
detect_display_scale()
{
    float ddpi = 0.0f;
    if( SDL_GetDisplayDPI(0, &ddpi, NULL, NULL) == 0 && ddpi > 0.0f )
    {
        float scale = ddpi / 96.0f;
        if( scale >= 1.25f )
            return scale;
    }
    const char* gdk = getenv("GDK_SCALE");
    if( gdk )
    {
        float s = (float)atof(gdk);
        if( s >= 1.0f )
            return s;
    }
    return 1.0f;
}

bool
PlatformImpl2_SDL2_Port_Linux_InitForSoft3D(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height)
{
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->display_scale = detect_display_scale();

    int scaled_w = (int)(screen_width * platform->display_scale);
    int scaled_h = (int)(screen_height * platform->display_scale);

    platform->window = SDL_CreateWindow(
        "Game",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        scaled_w,
        scaled_h,
        SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    platform->window_width = scaled_w;
    platform->window_height = scaled_h;
    platform->drawable_width = scaled_w;
    platform->drawable_height = scaled_h;

    platform->game_screen_width = screen_width;
    platform->game_screen_height = screen_height;

    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}

bool
PlatformImpl2_SDL2_Port_Linux_InitForOpenGL3(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height)
{
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->display_scale = detect_display_scale();

    int scaled_w = (int)(screen_width * platform->display_scale);
    int scaled_h = (int)(screen_height * platform->display_scale);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    Uint32 gl_window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;

    platform->window = SDL_CreateWindow(
        "Game",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        scaled_w,
        scaled_h,
        gl_window_flags);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    platform->window_width = scaled_w;
    platform->window_height = scaled_h;
    platform->drawable_width = scaled_w;
    platform->drawable_height = scaled_h;
    SDL_GL_GetDrawableSize(platform->window, &platform->drawable_width, &platform->drawable_height);

    platform->game_screen_width = screen_width;
    platform->game_screen_height = screen_height;
    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}
