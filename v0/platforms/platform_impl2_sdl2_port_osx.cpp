#include "platform_impl2_sdl2.h"
#include "platform_impl2_sdl2_internal.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool
PlatformImpl2_SDL2_Port_OSX_InitForSoft3D(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height)
{
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->display_scale = 1;

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
PlatformImpl2_SDL2_Port_OSX_InitForOpenGL3(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height)
{
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->display_scale = 1;

    int scaled_w = (int)(screen_width * platform->display_scale);
    int scaled_h = (int)(screen_height * platform->display_scale);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
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

bool
PlatformImpl2_SDL2_Port_OSX_InitForMetal(
    struct Platform2_SDL2* platform,
    int screen_width,
    int screen_height)
{
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window = SDL_CreateWindow(
        "Game",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        screen_width,
        screen_height,
        SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    platform->window_width = screen_width;
    platform->window_height = screen_height;
    platform->drawable_width = screen_width;
    platform->drawable_height = screen_height;

    platform->game_screen_width = screen_width;
    platform->game_screen_height = screen_height;
    platform->display_scale = 1.0f;
    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}
