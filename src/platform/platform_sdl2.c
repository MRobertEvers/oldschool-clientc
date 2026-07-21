#include "platform/platform_sdl2.h"

#include <SDL.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PlatformSDL2
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    int* pixels;
    int width;
    int height;
    bool quit;
};

static enum LibToriRS_KeyCode
sdl_keycode_to_torirs(SDL_Keycode key_code)
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
    case SDLK_RSHIFT:
        return TORIRSK_SHIFT;
    case SDLK_LCTRL:
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
}

static enum LibToriRS_MouseButton
sdl_mouse_button_to_torirs(int mouse_button)
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
}

static void
letterbox_dst(
    int logical_w,
    int logical_h,
    int window_w,
    int window_h,
    SDL_Rect* dst)
{
    assert(dst);
    dst->x = 0;
    dst->y = 0;
    dst->w = logical_w;
    dst->h = logical_h;

    if( window_w <= 0 || window_h <= 0 )
        return;

    float src_aspect = (float)logical_w / (float)logical_h;
    float window_aspect = (float)window_w / (float)window_h;

    if( src_aspect > window_aspect )
    {
        dst->w = window_w;
        dst->h = (int)(window_w / src_aspect);
        dst->x = 0;
        dst->y = (window_h - dst->h) / 2;
    }
    else
    {
        dst->h = window_h;
        dst->w = (int)(window_h * src_aspect);
        dst->y = 0;
        dst->x = (window_w - dst->w) / 2;
    }
}

struct PlatformSDL2*
PlatformSDL2_New(void)
{
    struct PlatformSDL2* platform = malloc(sizeof(struct PlatformSDL2));
    if( !platform )
        return NULL;
    memset(platform, 0, sizeof(struct PlatformSDL2));
    return platform;
}

bool
PlatformSDL2_Init(
    struct PlatformSDL2* platform,
    int width,
    int height,
    char const* title)
{
    assert(platform);
    assert(width > 0 && height > 0);

    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window = SDL_CreateWindow(
        title ? title : "torirs",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        width,
        height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    platform->renderer = SDL_CreateRenderer(platform->window, -1, SDL_RENDERER_ACCELERATED);
    if( !platform->renderer )
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
        SDL_Quit();
        return false;
    }

    platform->texture = SDL_CreateTexture(
        platform->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height);
    if( !platform->texture )
    {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(platform->renderer);
        platform->renderer = NULL;
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
        SDL_Quit();
        return false;
    }

    size_t const pixel_count = (size_t)width * (size_t)height;
    platform->pixels = malloc(pixel_count * sizeof(int));
    if( !platform->pixels )
    {
        fprintf(stderr, "Failed to allocate pixel buffer\n");
        SDL_DestroyTexture(platform->texture);
        platform->texture = NULL;
        SDL_DestroyRenderer(platform->renderer);
        platform->renderer = NULL;
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
        SDL_Quit();
        return false;
    }
    memset(platform->pixels, 0, pixel_count * sizeof(int));

    platform->width = width;
    platform->height = height;
    platform->quit = false;
    return true;
}

void
PlatformSDL2_Free(struct PlatformSDL2* platform)
{
    if( !platform )
        return;
    free(platform->pixels);
    platform->pixels = NULL;
    if( platform->texture )
    {
        SDL_DestroyTexture(platform->texture);
        platform->texture = NULL;
    }
    if( platform->renderer )
    {
        SDL_DestroyRenderer(platform->renderer);
        platform->renderer = NULL;
    }
    if( platform->window )
    {
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
    }
    SDL_Quit();
    free(platform);
}

int*
PlatformSDL2_Pixels(struct PlatformSDL2* platform)
{
    assert(platform);
    return platform->pixels;
}

int
PlatformSDL2_Width(struct PlatformSDL2* platform)
{
    assert(platform);
    return platform->width;
}

int
PlatformSDL2_Height(struct PlatformSDL2* platform)
{
    assert(platform);
    return platform->height;
}

bool
PlatformSDL2_QuitRequested(struct PlatformSDL2* platform)
{
    assert(platform);
    return platform->quit;
}

void
PlatformSDL2_SetTitle(
    struct PlatformSDL2* platform,
    char const* title)
{
    assert(platform);
    assert(platform->window);
    assert(title);
    SDL_SetWindowTitle(platform->window, title);
}

void
PlatformSDL2_MapMouse(
    struct PlatformSDL2* platform,
    int win_x,
    int win_y,
    int* out_x,
    int* out_y)
{
    int window_w = 0;
    int window_h = 0;
    SDL_Rect dst;
    int x;
    int y;

    assert(platform);
    assert(out_x);
    assert(out_y);
    assert(platform->window);

    SDL_GetWindowSize(platform->window, &window_w, &window_h);
    letterbox_dst(platform->width, platform->height, window_w, window_h, &dst);

    if( dst.w <= 0 || dst.h <= 0 )
    {
        *out_x = 0;
        *out_y = 0;
        return;
    }

    x = (win_x - dst.x) * platform->width / dst.w;
    y = (win_y - dst.y) * platform->height / dst.h;

    if( x < 0 )
        x = 0;
    else if( x >= platform->width )
        x = platform->width - 1;
    if( y < 0 )
        y = 0;
    else if( y >= platform->height )
        y = platform->height - 1;

    *out_x = x;
    *out_y = y;
}

void
PlatformSDL2_PollInput(
    struct PlatformSDL2* platform,
    struct LibToriRS_Input* input)
{
    SDL_Event event;
    int lx;
    int ly;
    enum LibToriRS_KeyCode key;
    enum LibToriRS_MouseButton button;

    assert(platform);
    assert(input);

    while( SDL_PollEvent(&event) )
    {
        switch( event.type )
        {
        case SDL_QUIT:
            platform->quit = true;
            break;
        case SDL_KEYDOWN:
            if( event.key.repeat )
                break;
            key = sdl_keycode_to_torirs(event.key.keysym.sym);
            if( key == TORIRSK_ESCAPE )
                platform->quit = true;
            LibToriRS_Input_PushKeyDown(input, key);
            break;
        case SDL_KEYUP:
            LibToriRS_Input_PushKeyUp(input, sdl_keycode_to_torirs(event.key.keysym.sym));
            break;
        case SDL_MOUSEBUTTONDOWN:
            PlatformSDL2_MapMouse(platform, event.button.x, event.button.y, &lx, &ly);
            button = sdl_mouse_button_to_torirs(event.button.button);
            LibToriRS_Input_PushMouseDown(input, button, lx, ly);
            break;
        case SDL_MOUSEBUTTONUP:
            PlatformSDL2_MapMouse(platform, event.button.x, event.button.y, &lx, &ly);
            button = sdl_mouse_button_to_torirs(event.button.button);
            LibToriRS_Input_PushMouseUp(input, button, lx, ly);
            break;
        case SDL_MOUSEMOTION:
            PlatformSDL2_MapMouse(platform, event.motion.x, event.motion.y, &lx, &ly);
            LibToriRS_Input_PushMouseMove(input, lx, ly);
            break;
        case SDL_MOUSEWHEEL:
        {
            int wheel_y = event.wheel.y;
            if( event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED )
                wheel_y = -wheel_y;
            LibToriRS_Input_PushMouseWheel(input, wheel_y);
            break;
        }
        default:
            break;
        }
    }
}

void
PlatformSDL2_Present(struct PlatformSDL2* platform)
{
    int* pix_write = NULL;
    int texture_pitch = 0;
    int texture_w;
    int window_w = 0;
    int window_h = 0;
    SDL_Rect dst;
    int y;

    assert(platform);
    assert(platform->renderer);
    assert(platform->texture);
    assert(platform->pixels);
    assert(platform->window);

    if( SDL_LockTexture(platform->texture, NULL, (void**)&pix_write, &texture_pitch) < 0 )
    {
        fprintf(stderr, "SDL_LockTexture failed: %s\n", SDL_GetError());
        return;
    }

    texture_w = texture_pitch / (int)sizeof(int);
    for( y = 0; y < platform->height; y++ )
    {
        memcpy(
            &pix_write[y * texture_w],
            &platform->pixels[y * platform->width],
            (size_t)platform->width * sizeof(int));
    }
    SDL_UnlockTexture(platform->texture);

    SDL_GetWindowSize(platform->window, &window_w, &window_h);
    letterbox_dst(platform->width, platform->height, window_w, window_h, &dst);

    SDL_SetRenderDrawColor(platform->renderer, 0, 0, 0, 255);
    SDL_RenderClear(platform->renderer);
    SDL_RenderCopy(platform->renderer, platform->texture, NULL, &dst);
    SDL_RenderPresent(platform->renderer);
}

uint64_t
PlatformSDL2_Ticks64(void)
{
    return SDL_GetTicks64();
}

void
PlatformSDL2_Delay(uint32_t ms)
{
    SDL_Delay(ms);
}
