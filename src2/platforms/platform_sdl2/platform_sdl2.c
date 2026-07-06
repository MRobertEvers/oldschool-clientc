#include "platform_sdl2.h"

#include "libtorirs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct LibToriPlatformSDL2
{
    SDL_Window* window;
    int width;
    int height;
};

struct LibToriPlatformSDL2*
LibToriPlatformSDL2_New(void)
{
    struct LibToriPlatformSDL2* platform = malloc(sizeof(struct LibToriPlatformSDL2));
    if( !platform )
        return NULL;
    memset(platform, 0, sizeof(struct LibToriPlatformSDL2));
    return platform;
}

void
LibToriPlatformSDL2_Free(struct LibToriPlatformSDL2* platform)
{
    if( !platform )
        return;
    if( platform->window )
    {
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
    }
    SDL_Quit();
    free(platform);
}

bool
LibToriPlatformSDL2_InitForSoft3D(
    struct LibToriPlatformSDL2* platform,
    int screen_width,
    int screen_height)
{
    if( !platform )
        return false;
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
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    platform->width = screen_width;
    platform->height = screen_height;

    return true;
}

bool
LibToriPlatformSDL2_InitForOpenGL3(
    struct LibToriPlatformSDL2* platform,
    int screen_width,
    int screen_height)
{
    if( !platform )
        return false;
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    platform->window = SDL_CreateWindow(
        "Game",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        screen_width,
        screen_height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    platform->width = screen_width;
    platform->height = screen_height;

    return true;
}

bool
LibToriPlatformSDL2_InitForWebGL1(
    struct LibToriPlatformSDL2* platform,
    int screen_width,
    int screen_height)
{
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    /* WebGL1 API = GLES2 shaders (Emscripten EGL requires client version 2, not 1). */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    platform->window = SDL_CreateWindow(
        "Game",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        screen_width,
        screen_height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    return true;
}

bool
LibToriPlatformSDL2_InitForD3D9(
    struct LibToriPlatformSDL2* platform,
    int screen_width,
    int screen_height)
{
    if( !platform )
        return false;
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    /* D3D9 owns its own swap chain; no OpenGL flag needed on the window. */
    platform->window = SDL_CreateWindow(
        "Game",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        screen_width,
        screen_height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    platform->width = screen_width;
    platform->height = screen_height;

    return true;
}

SDL_Window*
LibToriPlatformSDL2_GetWindow(struct LibToriPlatformSDL2* platform)
{
    if( !platform )
        return NULL;
    return platform->window;
}

void
LibToriPlatformSDL2_PollEvents(
    struct LibToriPlatformSDL2* platform,
    struct LibToriRS_CommandQueue* command_queue)
{
    if( !platform )
        return;
    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        switch( event.type )
        {
        case SDL_QUIT:
            LibToriRS_CommandQueue_PushQuitEvent(command_queue);
            break;
        case SDL_KEYDOWN:
            if( event.key.repeat )
                break;
            LibToriRS_CommandQueue_PushKeyEvent(
                command_queue,
                LibToriPlatformSDL2_SDLKeyCodeToKeyCode(event.key.keysym.sym),
                TORIRSEV_KEY_DOWN);
            break;
        case SDL_KEYUP:
            LibToriRS_CommandQueue_PushKeyEvent(
                command_queue,
                LibToriPlatformSDL2_SDLKeyCodeToKeyCode(event.key.keysym.sym),
                TORIRSEV_KEY_UP);
            break;
        case SDL_MOUSEBUTTONDOWN:
            LibToriRS_CommandQueue_PushMouseButtonEvent(
                command_queue,
                LibToriPlatformSDL2_SDLMouseButtonToMouseButton(event.button.button),
                TORIRSEV_MOUSE_DOWN,
                event.button.x,
                event.button.y);
            break;
        case SDL_MOUSEBUTTONUP:
            LibToriRS_CommandQueue_PushMouseButtonEvent(
                command_queue,
                LibToriPlatformSDL2_SDLMouseButtonToMouseButton(event.button.button),
                TORIRSEV_MOUSE_UP,
                event.button.x,
                event.button.y);
            break;
        case SDL_MOUSEMOTION:
            LibToriRS_CommandQueue_PushMouseMoveEvent(
                command_queue,
                event.motion.x,
                event.motion.y,
                event.motion.xrel,
                event.motion.yrel);
            break;
        case SDL_MOUSEWHEEL:
        {
            int wheel_y = event.wheel.y;
            if( event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED )
                wheel_y = -wheel_y;
            LibToriRS_CommandQueue_PushMouseWheelEvent(command_queue, wheel_y, 0, 0);
            break;
        }
        }
    }
}

enum LibToriRS_KeyCode
LibToriPlatformSDL2_SDLKeyCodeToKeyCode(SDL_Keycode key_code)
{
    switch( key_code )
    {
    case SDLK_a:
        return TORIRSK_A;
    case SDLK_b:
        return TORIRSK_B;
    case SDLK_c:
        return TORIRSK_C;
    case SDLK_d:
        return TORIRSK_D;
    case SDLK_e:
        return TORIRSK_E;
    case SDLK_f:
        return TORIRSK_F;
    case SDLK_g:
        return TORIRSK_G;
    case SDLK_h:
        return TORIRSK_H;
    case SDLK_i:
        return TORIRSK_I;
    case SDLK_j:
        return TORIRSK_J;
    case SDLK_k:
        return TORIRSK_K;
    case SDLK_l:
        return TORIRSK_L;
    case SDLK_m:
        return TORIRSK_M;
    case SDLK_n:
        return TORIRSK_N;
    case SDLK_o:
        return TORIRSK_O;
    case SDLK_p:
        return TORIRSK_P;
    case SDLK_q:
        return TORIRSK_Q;
    case SDLK_r:
        return TORIRSK_R;
    case SDLK_s:
        return TORIRSK_S;
    case SDLK_t:
        return TORIRSK_T;
    case SDLK_u:
        return TORIRSK_U;
    case SDLK_v:
        return TORIRSK_V;
    case SDLK_w:
        return TORIRSK_W;
    case SDLK_x:
        return TORIRSK_X;
    case SDLK_y:
        return TORIRSK_Y;
    case SDLK_z:
        return TORIRSK_Z;
    case SDLK_0:
        return TORIRSK_0;
    case SDLK_1:
        return TORIRSK_1;
    case SDLK_2:
        return TORIRSK_2;
    case SDLK_3:
        return TORIRSK_3;
    case SDLK_4:
        return TORIRSK_4;
    case SDLK_5:
        return TORIRSK_5;
    case SDLK_6:
        return TORIRSK_6;
    case SDLK_7:
        return TORIRSK_7;
    case SDLK_8:
        return TORIRSK_8;
    case SDLK_9:
        return TORIRSK_9;
    case SDLK_ESCAPE:
        return TORIRSK_ESCAPE;
    case SDLK_RETURN:
        return TORIRSK_RETURN;
    case SDLK_BACKSPACE:
        return TORIRSK_BACKSPACE;
    case SDLK_INSERT:
        return TORIRSK_INSERT;
    case SDLK_DELETE:
        return TORIRSK_DELETE;
    case SDLK_LSHIFT:
        return TORIRSK_SHIFT;
    case SDLK_RSHIFT:
        return TORIRSK_SHIFT;
    case SDLK_LCTRL:
        return TORIRSK_CTRL;
    case SDLK_RCTRL:
        return TORIRSK_CTRL;
    case SDLK_TAB:
        return TORIRSK_TAB;
    case SDLK_SPACE:
        return TORIRSK_SPACE;
    case SDLK_LEFT:
        return TORIRSK_LEFT;
    case SDLK_RIGHT:
        return TORIRSK_RIGHT;
    case SDLK_UP:
        return TORIRSK_UP;
    case SDLK_DOWN:
        return TORIRSK_DOWN;
    case SDLK_PAGEUP:
        return TORIRSK_PAGE_UP;
    case SDLK_PAGEDOWN:
        return TORIRSK_PAGE_DOWN;
    case SDLK_COMMA:
        return TORIRSK_COMMA;
    default:
        return TORIRSK_UNKNOWN;
    }
    return TORIRSK_UNKNOWN;
}

enum LibToriRS_MouseButton
LibToriPlatformSDL2_SDLMouseButtonToMouseButton(int mouse_button)
{
    switch( mouse_button )
    {
    case SDL_BUTTON_LEFT:
        return TORIRSM_LEFT;
    case SDL_BUTTON_MIDDLE:
        return TORIRSM_MIDDLE;
    case SDL_BUTTON_RIGHT:
        return TORIRSM_RIGHT;
    default:
        return TORIRSM_UNKNOWN;
    }
    return TORIRSM_UNKNOWN;
}
