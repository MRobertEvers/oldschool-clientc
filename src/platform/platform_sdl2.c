#include "platform/platform_sdl2.h"

#include "cmd/cmdbus.h"
#include "input/torirs_input.h"

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
    /* Escape is OSRS key 13 and the UI needs it (cancel dialog, close
     * interface), so it no longer quits by default. TORIRS_ESC_QUIT=1 restores
     * the old behaviour for headless and dev runs. */
    bool esc_quits;
    bool use_opengl;
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

/*
 * SDL keysym -> Java KeyEvent.VK_* / DOM keyCode, for the OSRS key table.
 *
 * Deliberately separate from sdl_keycode_to_torirs: that one feeds the
 * platform-neutral edge/held arrays and only covers the subset the camera and
 * debug paths use, while this one must cover everything OSRS_KEY_MAP maps
 * (F-keys, numpad, alt, home/end) without growing TORIRSK_COUNT, which sizes
 * two arrays inside both the prev and curr input snapshots.
 */
static int
sdl_keycode_to_vk(SDL_Keycode key_code)
{
    if( key_code >= SDLK_a && key_code <= SDLK_z )
        return 65 + (key_code - SDLK_a); /* VK_A .. VK_Z */
    if( key_code >= SDLK_0 && key_code <= SDLK_9 )
        return 48 + (key_code - SDLK_0); /* VK_0 .. VK_9 */
    if( key_code >= SDLK_F1 && key_code <= SDLK_F12 )
        return 112 + (key_code - SDLK_F1); /* VK_F1 .. VK_F12 */

    switch( key_code )
    {
    case SDLK_BACKSPACE:
        return TORIRS_VK_BACKSPACE;
    case SDLK_TAB:
        return TORIRS_VK_TAB;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return TORIRS_VK_ENTER;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        return TORIRS_VK_SHIFT;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        return TORIRS_VK_CTRL;
    case SDLK_LALT:
    case SDLK_RALT:
        return TORIRS_VK_ALT;
    case SDLK_ESCAPE:
        return TORIRS_VK_ESCAPE;
    case SDLK_SPACE:
        return TORIRS_VK_SPACE;
    case SDLK_PAGEUP:
        return 33;
    case SDLK_PAGEDOWN:
        return 34;
    case SDLK_END:
        return 35;
    case SDLK_HOME:
        return 36;
    case SDLK_LEFT:
        return 37;
    case SDLK_UP:
        return 38;
    case SDLK_RIGHT:
        return 39;
    case SDLK_DOWN:
        return 40;
    case SDLK_KP_0:
        return 96;
    case SDLK_KP_1:
        return 97;
    case SDLK_KP_2:
        return 98;
    case SDLK_KP_3:
        return 99;
    case SDLK_KP_4:
        return 100;
    case SDLK_KP_5:
        return 101;
    case SDLK_KP_6:
        return 102;
    case SDLK_KP_7:
        return 103;
    case SDLK_KP_8:
        return 104;
    case SDLK_KP_9:
        return 105;
    case SDLK_KP_MULTIPLY:
        return 106;
    case SDLK_KP_PLUS:
        return 107;
    case SDLK_KP_MINUS:
        return 109;
    case SDLK_KP_PERIOD:
        return 110;
    case SDLK_KP_DIVIDE:
        return 111;
    case SDLK_DELETE:
        return TORIRS_VK_DELETE;
    default:
        return -1;
    }
}

/* First codepoint of a UTF-8 SDL_TEXTINPUT payload, or -1. */
static int
utf8_first_codepoint(char const* text)
{
    unsigned char const* bytes = (unsigned char const*)text;
    if( !bytes || !bytes[0] )
        return -1;
    if( bytes[0] < 0x80 )
        return bytes[0];
    if( (bytes[0] & 0xE0) == 0xC0 && (bytes[1] & 0xC0) == 0x80 )
        return ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
    if( (bytes[0] & 0xF0) == 0xE0 && (bytes[1] & 0xC0) == 0x80 && (bytes[2] & 0xC0) == 0x80 )
        return ((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
    return -1;
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

    platform->esc_quits = getenv("TORIRS_ESC_QUIT") != NULL;

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
    /* Headless backends (SDL_VIDEODRIVER=dummy) have no accelerated driver;
     * fall back to whatever is available so record/replay smoke runs work. */
    if( !platform->renderer )
        platform->renderer = SDL_CreateRenderer(platform->window, -1, 0);
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

    /* SDL_TEXTINPUT is how we get layout-resolved, shift-applied characters;
     * deriving them from keysyms would mean reimplementing shift tables per
     * keyboard layout. On by default on most desktop backends, but not
     * guaranteed. */
    SDL_StartTextInput();

    platform->width = width;
    platform->height = height;
    platform->quit = false;
    platform->use_opengl = false;
    return true;
}

bool
PlatformSDL2_InitForOpenGL3(
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

    platform->esc_quits = getenv("TORIRS_ESC_QUIT") != NULL;

#if defined(TORIRS_GL_ES2)
    /* WebGL1 is GLES 2.0. Asking for exactly that (and nothing above it) is
     * what keeps the renderer honest about the extension-free feature set it
     * was written against — a WebGL2 context would quietly accept more. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
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
        title ? title : "torirs",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        width,
        height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        fprintf(stderr, "SDL_CreateWindow (OpenGL) failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    /* No SDL_Renderer / streaming texture / CPU pixel buffer — GL draws directly. */
    platform->renderer = NULL;
    platform->texture = NULL;
    platform->pixels = NULL;

    SDL_StartTextInput();

    platform->width = width;
    platform->height = height;
    platform->quit = false;
    platform->use_opengl = true;
    return true;
}

struct SDL_Window*
PlatformSDL2_Window(struct PlatformSDL2* platform)
{
    assert(platform);
    return platform->window;
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
PlatformSDL2_PollCommands(
    struct PlatformSDL2* platform,
    struct ToriRS_CmdBus* bus)
{
    SDL_Event event;
    int lx;
    int ly;
    enum LibToriRS_KeyCode key;
    enum LibToriRS_MouseButton button;
    /*
     * The reference emits exactly one key event per keypress: a character event
     * when the key produces a printable char, otherwise a key-code event, never
     * both (InputManager.onKeyDown). SDL instead splits one keypress into
     * SDL_KEYDOWN followed -- only sometimes -- by SDL_TEXTINPUT, guaranteed in
     * that order within a poll batch. So hold the key-code event back until we
     * know whether a character followed: a printable SDL_TEXTINPUT cancels it,
     * anything else flushes it. Since the pending event is flushed at the top of
     * every KEYDOWN and once more after the loop, and nothing else in the loop
     * pushes key events, arrival order is preserved exactly. The resolution
     * happens here, before encoding, so the bus (and therefore recordings)
     * carries only resolved key events.
     */
    int pending_osrs = -1;
    int pending_mods = 0;
    int pending_repeat = 0;

    assert(platform);
    assert(bus);

    while( SDL_PollEvent(&event) )
    {
        switch( event.type )
        {
        case SDL_QUIT:
            platform->quit = true;
            break;
        case SDL_KEYDOWN:
        {
            int vk;
            int osrs;

            if( pending_osrs >= 0 )
            {
                CmdBus_PushKeyEvent(bus, pending_osrs, 0, pending_repeat);
                pending_osrs = -1;
            }

            key = sdl_keycode_to_torirs(event.key.keysym.sym);
            if( platform->esc_quits && key == TORIRSK_ESCAPE )
                platform->quit = true;

            /* OS auto-repeat feeds the event queue -- that is what makes a held
             * backspace delete repeatedly, matching the reference, which does
             * not filter repeats. The key_down[] edge array stays a true edge. */
            if( !event.key.repeat )
                CmdBus_PushKey(bus, TORIRS_CMD_INPUT_KEY_DOWN, (uint8_t)key);

            vk = sdl_keycode_to_vk(event.key.keysym.sym);
            osrs = LibToriRS_OsrsKeyFromVk(vk);
            if( osrs >= 0 )
            {
                CmdBus_PushOsrsKey(bus, osrs, 1, !event.key.repeat);
                pending_osrs = osrs;
                pending_mods = event.key.keysym.mod;
                pending_repeat = event.key.repeat ? 1 : 0;
            }
            break;
        }
        case SDL_TEXTINPUT:
        {
            int codepoint = utf8_first_codepoint(event.text.text);
            /* Reference guard: a character is only produced when no ctrl/alt/meta
             * modifier is active, so Ctrl+S stays a key-code event. */
            if( pending_mods & (KMOD_CTRL | KMOD_ALT | KMOD_GUI) )
                break;
            /* Clamped to latin-1: RS fonts have no glyphs beyond it. The
             * reference passes charCodeAt(0) unclamped, so this is a deliberate
             * divergence rather than an oversight. */
            if( codepoint >= 32 && codepoint <= 255 )
            {
                pending_osrs = -1; /* the character event replaces the code event */
                CmdBus_PushKeyEvent(bus, -1, codepoint, pending_repeat);
            }
            pending_mods = 0;
            break;
        }
        case SDL_KEYUP:
        {
            int osrs = LibToriRS_OsrsKeyFromVk(sdl_keycode_to_vk(event.key.keysym.sym));
            CmdBus_PushKey(
                bus, TORIRS_CMD_INPUT_KEY_UP, (uint8_t)sdl_keycode_to_torirs(event.key.keysym.sym));
            if( osrs >= 0 )
                CmdBus_PushOsrsKey(bus, osrs, 0, 0);
            break;
        }
        case SDL_WINDOWEVENT:
            /* Focus loss: the OS stops delivering key-ups, so anything held now
             * would latch forever. Reference InputManager.onFocusOut. */
            if( event.window.event == SDL_WINDOWEVENT_FOCUS_LOST )
            {
                pending_osrs = -1;
                pending_mods = 0;
                CmdBus_Push(bus, TORIRS_CMD_INPUT_CLEAR_KEYS, NULL, 0);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            PlatformSDL2_MapMouse(platform, event.button.x, event.button.y, &lx, &ly);
            button = sdl_mouse_button_to_torirs(event.button.button);
            CmdBus_PushMouseButton(
                bus, TORIRS_CMD_INPUT_MOUSE_DOWN, (uint8_t)button, (int16_t)lx, (int16_t)ly);
            break;
        case SDL_MOUSEBUTTONUP:
            PlatformSDL2_MapMouse(platform, event.button.x, event.button.y, &lx, &ly);
            button = sdl_mouse_button_to_torirs(event.button.button);
            CmdBus_PushMouseButton(
                bus, TORIRS_CMD_INPUT_MOUSE_UP, (uint8_t)button, (int16_t)lx, (int16_t)ly);
            break;
        case SDL_MOUSEMOTION:
            PlatformSDL2_MapMouse(platform, event.motion.x, event.motion.y, &lx, &ly);
            CmdBus_PushMouseMove(bus, (int16_t)lx, (int16_t)ly);
            break;
        case SDL_MOUSEWHEEL:
        {
            int wheel_y = event.wheel.y;
            if( event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED )
                wheel_y = -wheel_y;
            CmdBus_PushMouseWheel(bus, (int16_t)wheel_y);
            break;
        }
        default:
            break;
        }
    }

    /* No SDL_TEXTINPUT arrived for the last keydown, so it was a non-character
     * key after all. */
    if( pending_osrs >= 0 )
        CmdBus_PushKeyEvent(bus, pending_osrs, 0, pending_repeat);
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
    assert(!platform->use_opengl);
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

void
PlatformSDL2_PresentGL(struct PlatformSDL2* platform)
{
    assert(platform);
    assert(platform->use_opengl);
    assert(platform->window);
    SDL_GL_SwapWindow(platform->window);
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
