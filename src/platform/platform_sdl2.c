#include "platform/platform_window.h"

#include "cmd/cmdbus.h"
#include "input/torirs_input.h"
#include "input/torirs_touch.h"
#include "platform/platform_app_icon.h"

#include <SDL.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * One borderless window's state, and what SDL's hit test is handed as its
 * userdata.
 *
 * Per WINDOW rather than per platform, because the two windows differ in the
 * one thing the trampoline has to know: the game window letterboxes a fixed
 * canvas into itself and the aux window's surface IS the window, so a point
 * has to be mapped for one and not the other. A single callback with a flag
 * beats two near-identical ones -- the resize bands, the ordering and the
 * refusal are the same on both, and the only thing that would diverge is the
 * bug where one of the copies got fixed.
 */
struct SdlHitState
{
    struct PlatformWindow* platform;
    PlatformWindow_DragHandleFn fn;
    void* user;
    /** Skip the letterbox: this window's content is its own size. */
    int is_aux;
    /** The frame is off AND the hit test is installed. Both, or neither: the
     *  callback is what makes a frameless window movable. */
    int borderless;
};

/**
 * How far in from a borderless window's edge still resizes it, in window
 * points.
 *
 * Deliberately not scaled by the display density. This is a pointer target,
 * and a pointer target is a physical distance -- window points already are one
 * on every backend SDL reports them from, and multiplying by the density would
 * make the grab band twice as deep on a Retina display than on the panel
 * beside it.
 */
#define SDL_BORDERLESS_RESIZE 6

struct PlatformWindow
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
    /* Resizable mode: the backbuffer IS the window, so a window resize reallocs
     * pixels/texture and the client relayouts at the new size. Fixed mode keeps
     * a 765x503 backbuffer and letterboxes it into whatever the window is — the
     * fixed gameframe is 765x503 by definition, so scaling is the correct answer
     * there and reflowing is not. Default false: the boot size is the fixed
     * canvas until something (the window-mode op, or TORIRS_SIM_RESIZE) says
     * otherwise. */
    bool canvas_follows_window;
    /* The window size resizable mode was last in, remembered across a trip
     * through fixed mode — entering fixed snaps the window down to the fixed
     * frame, which would otherwise destroy the size the user chose. 0 = never
     * been resizable-and-larger, so there is nothing to go back to. */
    int resizable_w;
    int resizable_h;
    int interface_scale_mode;

    /*
     * The auxiliary window: one extra, optional, never a render target.
     *
     * Its own renderer and texture rather than a second viewport on the game's,
     * because they are separate OS windows with separate swap chains -- and its
     * own pixel buffer, because whoever draws into it draws a different picture
     * at a different size. Nothing here is touched when it is closed, which is
     * the state it is in for almost every run.
     */
    SDL_Window* aux_window;
    SDL_Renderer* aux_renderer;
    SDL_Texture* aux_texture;
    int* aux_pixels;
    /*
     * The SURFACE, in PIXELS -- what the chrome lays out and rasterises into.
     *
     * Pixels, not points, and that is the whole of what this window used to get
     * wrong. The chrome picks its baked font size from the display's density
     * (App_SetChromeScale off PlatformWindow_PixelDensity), so on a 2x display it
     * lays out 36px rows and a 208px label column. Sized in POINTS this surface
     * gave a 2x chrome half the room it was laid out for: labels ran under
     * their fields, the fields were clipped against the scrollbar, and half of
     * a plugin's settings page fell past the bottom edge -- where widgets get a
     * zero box and stop being hit-testable at all. Then SDL stretched the
     * half-resolution result over the drawable, so it was soft as well.
     */
    int aux_width;
    int aux_height;
    /* The same window in POINTS, which is the space SDL reports mouse events
     * and window sizes in. The two differ by the display's density, and every
     * point that crosses into surface space is scaled by the ratio of these
     * to the pair above. @see aux_point_to_pixel. */
    int aux_point_w;
    int aux_point_h;
    /* SDL's id for aux_window, so the event pump can tell which window an
     * event came from without comparing pointers on every event. */
    uint32_t aux_window_id;
    /* Latched by the pump, drained by AuxTakeCloseRequest -- see the header. */
    bool aux_close_requested;
    /* This frame's aux gesture, coalesced by the pump. */
    struct PlatformWindow_AuxInput aux_input;
    bool aux_have_input;

    /* The one plugin-chrome pane attached inside `window`. Unlike aux_* this
     * owns no SDL_Window or renderer: the main renderer composites its texture
     * beside the game texture, so opening it creates no second toplevel. */
    SDL_Texture* chrome_texture;
    int* chrome_pixels;
    int chrome_width;  /* drawable pixels */
    int chrome_height;
    int chrome_point_w;
    int chrome_saved_point_w;
    int chrome_saved_point_h;
    bool chrome_open;
    bool chrome_grew;
    bool chrome_focused;
    bool chrome_pointer_down;
    bool chrome_dirty;
    struct PlatformWindow_AuxInput chrome_input;
    bool chrome_have_input;

    /* Last title handed to SDL, so a per-frame caller does not repeat itself.
     * See PlatformWindow_SetTitle. Sized to hold main.c's readout whole: a title
     * that overflows would compare equal on its tail and stop updating. */
    char title[256];
    /* Drawable pixels per window point, from SDL: 1 on an ordinary display, 2
     * on a Retina/200% one. The framebuffer is sized in DRAWABLE pixels, so
     * this is not a scale anything multiplies by at draw time -- it is what the
     * chrome reads to choose which baked font size to lay out with. */
    int pixel_density;

    /* Borderless state, one per window. @see struct SdlHitState. */
    struct SdlHitState hit_main;
    struct SdlHitState hit_aux;

    /* Fingers. @see ToriRS_Touch, which holds the whole gesture policy so that
     * this backend and the Win32 one cannot drift apart about it. */
    struct ToriRS_Touch touch;
};

/*
 * Drawable size of the window, in real pixels.
 *
 * SDL reports window geometry in points and the drawable in pixels, and on a
 * HighDPI display those differ by the density factor. Everything this platform
 * hands the client is a PIXEL count -- the framebuffer it rasterises into, the
 * viewport GL draws to -- so this is the size that matters, and asking
 * SDL_GetWindowSize for it is the bug that makes a Retina window render at a
 * quarter of its resolution and then stretch.
 */
static void
sdl_drawable_size(
    struct PlatformWindow* platform,
    int* out_w,
    int* out_h)
{
    int w = 0;
    int h = 0;

    assert(platform);
    assert(platform->window);
    if( platform->use_opengl )
        SDL_GL_GetDrawableSize(platform->window, &w, &h);
    else if( platform->renderer )
        SDL_GetRendererOutputSize(platform->renderer, &w, &h);
    else
        SDL_GetWindowSize(platform->window, &w, &h);
    if( w <= 0 || h <= 0 )
        SDL_GetWindowSize(platform->window, &w, &h);
    if( out_w )
        *out_w = w;
    if( out_h )
        *out_h = h;
}

/*
 * Refresh the cached drawable-pixels-per-point ratio.
 *
 * Rounded to an integer, because it selects a BAKED chrome size and there is
 * no such thing as a 1.5x baked font here (see TORIRS_CHROME_SCALE_MAX). A 1.5x
 * display therefore lands on 2x chrome in a 1.5x-pixel framebuffer, which is
 * chrome slightly larger than nominal drawn at native resolution -- the
 * failure this whole path exists to avoid is the other one, chrome drawn at
 * one size and stretched to another.
 */
static void
sdl_refresh_pixel_density(struct PlatformWindow* platform)
{
    int window_w = 0;
    int window_h = 0;
    int drawable_w = 0;
    int drawable_h = 0;

    assert(platform);
    SDL_GetWindowSize(platform->window, &window_w, &window_h);
    sdl_drawable_size(platform, &drawable_w, &drawable_h);
    platform->pixel_density = 1;
    if( window_w > 0 && drawable_w > 0 )
        platform->pixel_density = (drawable_w + window_w / 2) / window_w;
    if( platform->pixel_density < 1 )
        platform->pixel_density = 1;
}

/*
 * Whether to ask for a HighDPI drawable at all. ON by default; nothing has to
 * ask for it.
 *
 * There is nothing to decide in the common case, which is why this is not a
 * question the boot is made to answer. On a 1x display SDL_WINDOW_ALLOW_HIGHDPI
 * is a no-op -- the drawable already equals the window points -- so the flag
 * costs those machines nothing. On a 2x display the alternative is not "fewer
 * pixels", it is the window server magnifying the finished frame, chrome and
 * world alike. That is the ONE failure the baked chrome sizes exist to avoid,
 * arriving from underneath them where no amount of chrome work can reach it,
 * so defaulting it off buys a blurry frame rather than a cheap one.
 *
 * The other half of a HighDPI client -- a CANVAS that follows the drawable
 * rather than the window points -- is wired: SetCanvasFollowsWindow and the
 * SIZE_CHANGED branch both push sdl_drawable_size(), and MapMouse derives its
 * letterbox from the canvas against window points, so clicks land where they
 * are drawn.
 *
 * The web lane is the exception and defaults OFF. There the density is
 * devicePixelRatio, which is 3 on a phone rather than 2 on a desk, and the
 * renderer is a software rasteriser in wasm -- nine times the pixels is not a
 * sharper frame, it is no frame. That one is a real trade, so the web boot has
 * to opt in.
 *
 * `[ui:boot] hidpi=` overrides the default per boot, and TORIRS_HIDPI
 * overrides the manifest: =0 gives up the drawable on a machine that cannot
 * afford it, =1 turns it on for a lane whose default is off.
 */
#if defined(TORIRS_PLATFORM_WEB)
static bool g_want_highdpi = false;
#else
static bool g_want_highdpi = true;
#endif

void
PlatformWindow_SetWantHighDPI(bool want)
{
    g_want_highdpi = want;
}

static bool
sdl_want_highdpi(void)
{
    char const* env = getenv("TORIRS_HIDPI");
    if( env && env[0] )
        return env[0] != '0';
    return g_want_highdpi;
}

static SDL_ScaleMode
sdl_interface_scale_mode(int mode)
{
    if( mode <= 0 )
        return SDL_ScaleModeNearest;
    if( mode == 1 )
        return SDL_ScaleModeLinear;
    return SDL_ScaleModeBest;
}

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

struct PlatformWindow*
PlatformWindow_New(void)
{
    struct PlatformWindow* platform = malloc(sizeof(struct PlatformWindow));
    assert(platform);
    memset(platform, 0, sizeof(struct PlatformWindow));
    platform->interface_scale_mode = 2;
    /* A zeroed finger table would read as eight fingers all holding id 0. */
    ToriRS_TouchReset(&platform->touch);
    return platform;
}

/* The window icon, from the RGBA that tools/make_app_icons.py embedded.
 *
 * Embedded rather than loaded from res/icons: a desktop run started from any
 * working directory still gets an icon, with no path to resolve and no file to
 * ship beside the binary.
 *
 * SDL copies the pixels into the window, so the surface is freed immediately.
 * The const cast is safe for the same reason -- SDL only reads it. */
static void
sdl_set_window_icon(SDL_Window* window)
{
    assert(window);

    /* The generated array is R,G,B,A in memory order. The masks below are
     * over the native-endian 32-bit word, so they swap with the byte order. */
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    Uint32 const rmask = 0xFF000000u, gmask = 0x00FF0000u;
    Uint32 const bmask = 0x0000FF00u, amask = 0x000000FFu;
#else
    Uint32 const rmask = 0x000000FFu, gmask = 0x0000FF00u;
    Uint32 const bmask = 0x00FF0000u, amask = 0xFF000000u;
#endif

    SDL_Surface* icon = SDL_CreateRGBSurfaceFrom(
        (void*)platform_app_icon_rgba,
        platform_app_icon_width,
        platform_app_icon_height,
        32,
        platform_app_icon_width * 4,
        rmask,
        gmask,
        bmask,
        amask);
    assert(icon);

    SDL_SetWindowIcon(window, icon);
    SDL_FreeSurface(icon);
}

bool
PlatformWindow_Init(
    struct PlatformWindow* platform,
    int width,
    int height,
    char const* title)
{
    assert(platform);
    assert(width > 0 && height > 0);

    /*
     * SDL must not invent a mouse from a finger.
     *
     * By default it synthesises a full mouse press, move and release behind
     * every touch, so a client that handles BOTH sees each tap twice -- once as
     * the gesture layer decided, and once more as SDL guessed. That is not a
     * duplicate click so much as a contradictory one: a long press would come
     * through as a right click from here and a left click from SDL, and the
     * minimenu would open and be dismissed by its own gesture.
     *
     * Turned off before the video subsystem starts, because SDL reads it when
     * the touch device is opened.
     */
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
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
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
            (sdl_want_highdpi() ? SDL_WINDOW_ALLOW_HIGHDPI : 0));
    if( !platform->window )
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    sdl_set_window_icon(platform->window);

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

    /* From here on `width`/`height` are DRAWABLE pixels. On a HighDPI display
     * the window was asked for in points and SDL gave it a drawable twice that
     * on each axis; rasterising at the point size and letting the present
     * stretch it is exactly the blur this path exists to avoid. */
    sdl_drawable_size(platform, &width, &height);
    sdl_refresh_pixel_density(platform);

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
    SDL_SetTextureScaleMode(
        platform->texture, sdl_interface_scale_mode(platform->interface_scale_mode));

    size_t const pixel_count = (size_t)width * (size_t)height;
    platform->pixels = malloc(pixel_count * sizeof(int));
    assert(platform->pixels);
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
PlatformWindow_InitForOpenGL3(
    struct PlatformWindow* platform,
    int width,
    int height,
    char const* title)
{
    assert(platform);
    assert(width > 0 && height > 0);

    /*
     * SDL must not invent a mouse from a finger.
     *
     * By default it synthesises a full mouse press, move and release behind
     * every touch, so a client that handles BOTH sees each tap twice -- once as
     * the gesture layer decided, and once more as SDL guessed. That is not a
     * duplicate click so much as a contradictory one: a long press would come
     * through as a right click from here and a left click from SDL, and the
     * minimenu would open and be dismissed by its own gesture.
     *
     * Turned off before the video subsystem starts, because SDL reads it when
     * the touch device is opened.
     */
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->esc_quits = getenv("TORIRS_ESC_QUIT") != NULL;

#if defined(TORIRS_PLATFORM_WEB)
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
#if defined(TORIRS_PLATFORM_WEB)
    /* In the browser these are decided HERE, not at context creation: SDL's
     * emscripten video chooses its EGL config when the window is made, and
     * emscripten's EGL turns each nonzero size into a WebGL context attribute
     * (depth, stencil, antialias). No renderer in this tree touches a stencil
     * buffer, so asking for one would only allocate a full-screen attachment
     * the browser then has to clear and carry every frame. Depth stays at 24:
     * the depth-buffered world pass needs it, and the request in
     * ToriRS_GLContext_Create arrives too late to add it on this host. */
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
#endif

    platform->window = SDL_CreateWindow(
        title ? title : "torirs",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        width,
        height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
            (sdl_want_highdpi() ? SDL_WINDOW_ALLOW_HIGHDPI : 0));
    if( !platform->window )
    {
        fprintf(stderr, "SDL_CreateWindow (OpenGL) failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    sdl_set_window_icon(platform->window);

    /* No SDL_Renderer / streaming texture / CPU pixel buffer — GL draws directly. */
    platform->renderer = NULL;
    platform->texture = NULL;
    platform->pixels = NULL;

    SDL_StartTextInput();

    /* Drawable pixels, for the same reason the software path takes them: the
     * GL viewport is sized from platform->width, and a point-sized viewport on
     * a Retina window renders a quarter of the framebuffer and scales it up. */
    sdl_drawable_size(platform, &width, &height);
    sdl_refresh_pixel_density(platform);

    platform->width = width;
    platform->height = height;
    platform->quit = false;
    platform->use_opengl = true;
    return true;
}

ToriRS_GLWindow*
PlatformWindow_GLWindow(struct PlatformWindow* platform)
{
    assert(platform);
    /* SDL_Window and ToriRS_GLWindow are the same object under two names; this
     * and platform_gl_context_sdl.c are the only places that say so. */
    return (ToriRS_GLWindow*)platform->window;
}

void
PlatformWindow_Free(struct PlatformWindow* platform)
{
    if( !platform )
        return;
    PlatformWindow_ChromeClose(platform);
    /* Before the main window's teardown, so the aux one never outlives the
     * SDL_Quit that follows. */
    PlatformWindow_AuxClose(platform);
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
        /* Off before the window goes, as the aux teardown does: SDL holds the
         * callback and a pointer into `platform` on the window, and this is
         * the frame that is about to free `platform`. */
        SDL_SetWindowHitTest(platform->window, NULL, NULL);
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
    }
    SDL_Quit();
    free(platform);
}

int*
PlatformWindow_Pixels(struct PlatformWindow* platform)
{
    assert(platform);
    return platform->pixels;
}

/* ---- the auxiliary window ------------------------------------------------ */

/**
 * The aux window's drawable size, in pixels.
 *
 * The renderer's output size rather than SDL_GetWindowSize, for the reason
 * sdl_drawable_size gives for the game window: SDL reports window geometry in
 * points and the drawable in pixels, and on a HighDPI display those differ.
 */
static void
aux_drawable_size(struct PlatformWindow* platform, int* out_w, int* out_h)
{
    int w = 0;
    int h = 0;

    assert(platform);
    assert(platform->aux_window);
    if( platform->aux_renderer )
        SDL_GetRendererOutputSize(platform->aux_renderer, &w, &h);
    if( w <= 0 || h <= 0 )
        SDL_GetWindowSize(platform->aux_window, &w, &h);
    if( out_w )
        *out_w = w;
    if( out_h )
        *out_h = h;
}

/** Re-read the window's size in points, which is what every event arrives in. */
static void
aux_refresh_points(struct PlatformWindow* platform)
{
    int w = 0;
    int h = 0;

    assert(platform);
    assert(platform->aux_window);
    SDL_GetWindowSize(platform->aux_window, &w, &h);
    if( w > 0 && h > 0 )
    {
        platform->aux_point_w = w;
        platform->aux_point_h = h;
    }
}

/**
 * One event coordinate, from SDL's points into the surface's pixels.
 *
 * PlatformWindow_MapMouse's opposite number for this window: there is no
 * letterbox here -- the surface IS the window -- so the whole of the mapping is
 * the density. A window whose points have not been read yet (or a 1x display)
 * scales by one, which is the identity this used to assume everywhere.
 */
static void
aux_point_to_pixel(struct PlatformWindow const* platform, int x, int y, int* out_x, int* out_y)
{
    assert(platform);
    assert(out_x);
    assert(out_y);
    *out_x = platform->aux_point_w > 0 ? x * platform->aux_width / platform->aux_point_w : x;
    *out_y = platform->aux_point_h > 0 ? y * platform->aux_height / platform->aux_point_h : y;
}

/**
 * Notice a drawable that changed without a resize, and report it as one.
 *
 * A window dragged from a 2x display to a 1x one keeps its size in POINTS and
 * halves in PIXELS, and SDL does not have to send SIZE_CHANGED for that. The
 * surface would stay at the old resolution -- stretched, and with every click
 * scaled by a density the window no longer has. Watched rather than subscribed
 * to, for the reason PlatformWindow_PixelDensity re-reads instead of caching:
 * two SDL getters on a path that runs once a frame.
 */
static void
sdl_aux_sync_drawable(struct PlatformWindow* platform)
{
    int pixel_w = 0;
    int pixel_h = 0;

    assert(platform);
    if( !platform->aux_window )
        return;
    aux_drawable_size(platform, &pixel_w, &pixel_h);
    if( pixel_w <= 0 || pixel_h <= 0 )
        return;
    if( pixel_w == platform->aux_width && pixel_h == platform->aux_height )
        return;
    aux_refresh_points(platform);
    platform->aux_input.resized = 1;
    platform->aux_input.width = pixel_w;
    platform->aux_input.height = pixel_h;
    platform->aux_have_input = true;
}

/*
 * Route one event to the aux window, or report that it was not ours.
 *
 * The gesture is coalesced into aux_input rather than queued, so a frame that
 * saw twenty motion events costs one position. Coordinates are reported in
 * SDL's POINTS and handed on in the SURFACE's PIXELS -- the space the chrome
 * laid its panels out in. There is no letterbox to undo, because the aux
 * surface IS the window; the density is the whole of the mapping.
 */
static bool
sdl_aux_event(struct PlatformWindow* platform, SDL_Event const* event)
{
    uint32_t const id = platform->aux_window_id;

    switch( event->type )
    {
    case SDL_WINDOWEVENT:
        if( event->window.windowID != id )
            return false;
        if( event->window.event == SDL_WINDOWEVENT_CLOSE )
            platform->aux_close_requested = true;
        else if( event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED )
        {
            /* data1/data2 are POINTS. What the caller resizes is the SURFACE,
             * so the size reported is the drawable -- the same conversion the
             * game window's SIZE_CHANGED does through sdl_drawable_size, and
             * the points are latched here because every later event is scaled
             * against them. */
            platform->aux_point_w = event->window.data1;
            platform->aux_point_h = event->window.data2;
            aux_drawable_size(platform, &platform->aux_input.width, &platform->aux_input.height);
            platform->aux_input.resized = 1;
            platform->aux_have_input = true;
        }
        return true;

    case SDL_MOUSEBUTTONDOWN:
        if( event->button.windowID != id )
            return false;
        aux_point_to_pixel(
            platform, event->button.x, event->button.y, &platform->aux_input.mouse_x,
            &platform->aux_input.mouse_y);
        if( event->button.button == SDL_BUTTON_LEFT )
            platform->aux_input.mouse_down = 1;
        platform->aux_have_input = true;
        return true;

    case SDL_MOUSEBUTTONUP:
        if( event->button.windowID != id )
            return false;
        aux_point_to_pixel(
            platform, event->button.x, event->button.y, &platform->aux_input.mouse_x,
            &platform->aux_input.mouse_y);
        if( event->button.button == SDL_BUTTON_LEFT )
            platform->aux_input.mouse_up = 1;
        platform->aux_have_input = true;
        return true;

    case SDL_MOUSEMOTION:
        if( event->motion.windowID != id )
            return false;
        aux_point_to_pixel(
            platform, event->motion.x, event->motion.y, &platform->aux_input.mouse_x,
            &platform->aux_input.mouse_y);
        platform->aux_have_input = true;
        return true;

    case SDL_MOUSEWHEEL:
        if( event->wheel.windowID != id )
            return false;
        platform->aux_input.wheel +=
            event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
        platform->aux_have_input = true;
        return true;

    case SDL_TEXTINPUT:
        if( event->text.windowID != id )
            return false;
        {
            size_t const have = strlen(platform->aux_input.text);
            size_t const room = sizeof(platform->aux_input.text) - have - 1;
            strncat(platform->aux_input.text, event->text.text, room);
        }
        platform->aux_have_input = true;
        return true;

    case SDL_KEYDOWN:
        if( event->key.windowID != id )
            return false;
        /* Only the EDITING keys: printable bytes arrive on TEXTINPUT above,
         * which is the one that has already applied the keyboard layout and
         * any dead keys. */
        switch( event->key.keysym.sym )
        {
        case SDLK_BACKSPACE:
            platform->aux_input.edit_key = PLATFORM_AUX_KEY_BACKSPACE;
            break;
        case SDLK_DELETE:
            platform->aux_input.edit_key = PLATFORM_AUX_KEY_DELETE;
            break;
        case SDLK_LEFT:
            platform->aux_input.edit_key = PLATFORM_AUX_KEY_LEFT;
            break;
        case SDLK_RIGHT:
            platform->aux_input.edit_key = PLATFORM_AUX_KEY_RIGHT;
            break;
        case SDLK_HOME:
            platform->aux_input.edit_key = PLATFORM_AUX_KEY_HOME;
            break;
        case SDLK_END:
            platform->aux_input.edit_key = PLATFORM_AUX_KEY_END;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            platform->aux_input.edit_key = PLATFORM_AUX_KEY_ENTER;
            break;
        case SDLK_ESCAPE:
            platform->aux_input.edit_key = PLATFORM_AUX_KEY_ESCAPE;
            break;
        /* Up and down a LINE, which only a multiline field has any of. Sent
         * unconditionally rather than gated on what has the focus: the window
         * does not know what the model's focused widget is, and the model
         * ignores the key on every other kind. */
        case SDLK_UP:
            platform->aux_input.edit_key = PLATFORM_AUX_KEY_UP;
            break;
        case SDLK_DOWN:
            platform->aux_input.edit_key = PLATFORM_AUX_KEY_DOWN;
            break;
        default:
            return true;
        }
        platform->aux_have_input = true;
        return true;

    case SDL_KEYUP:
        /* Swallowed, not forwarded: a key pressed in this window must not
         * release one the game thinks is held. */
        return event->key.windowID == id;

    default:
        return false;
    }
}

bool
PlatformWindow_AuxTakeInput(struct PlatformWindow* platform, struct PlatformWindow_AuxInput* out)
{
    assert(platform);
    assert(out);
    if( !platform->aux_window || !platform->aux_have_input )
        return false;

    *out = platform->aux_input;
    /* Edges cleared, position kept: a press left in the buffer would be
     * delivered again every frame until the next one arrived, and a pointer
     * that stopped moving is still where it was. */
    platform->aux_input.mouse_down = 0;
    platform->aux_input.mouse_up = 0;
    platform->aux_input.wheel = 0;
    platform->aux_input.text[0] = '\0';
    platform->aux_input.edit_key = PLATFORM_AUX_KEY_NONE;
    platform->aux_input.resized = 0;
    platform->aux_have_input = false;
    return true;
}



/** Allocate (or reallocate) the aux surface and its texture at w x h. */
static bool
aux_make_surface(struct PlatformWindow* platform, int width, int height)
{
    int* pixels;
    SDL_Texture* texture;

    assert(platform);
    assert(platform->aux_renderer);
    if( width <= 0 || height <= 0 )
        return false;

    texture = SDL_CreateTexture(
        platform->aux_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height);
    if( !texture )
    {
        fprintf(stderr, "aux window: texture: %s\n", SDL_GetError());
        return false;
    }

    pixels = calloc((size_t)width * (size_t)height, sizeof(*pixels));
    assert(pixels);

    if( platform->aux_texture )
        SDL_DestroyTexture(platform->aux_texture);
    free(platform->aux_pixels);
    platform->aux_texture = texture;
    platform->aux_pixels = pixels;
    platform->aux_width = width;
    platform->aux_height = height;
    return true;
}

bool
PlatformWindow_AuxOpen(struct PlatformWindow* platform, int width, int height, char const* title)
{
    assert(platform);

    if( platform->aux_window )
        return true;
    if( width <= 0 || height <= 0 )
        return false;

    /*
     * Placed by the window manager rather than beside the game deliberately:
     * on a multi-monitor desktop "next to the main window" is as likely to be
     * off-screen as not, and every platform already has a policy for where a
     * new window goes.
     */
    platform->aux_window = SDL_CreateWindow(
        title ? title : "Plugins",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        width,
        height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if( !platform->aux_window )
    {
        fprintf(stderr, "aux window: %s\n", SDL_GetError());
        return false;
    }

    /* Software, and never accelerated: this window shows chrome the CPU
     * rasteriser already produced, and asking for a GPU context here would put
     * a second swap chain beside the game's for a panel of rectangles. */
    platform->aux_renderer = SDL_CreateRenderer(platform->aux_window, -1, SDL_RENDERER_SOFTWARE);
    if( !platform->aux_renderer )
    {
        fprintf(stderr, "aux window renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(platform->aux_window);
        platform->aux_window = NULL;
        return false;
    }

    /*
     * The window is asked for in POINTS -- it is a physical size on a desk --
     * and its surface is made at the DRAWABLE, which on a 2x display is twice
     * that. Both, because they are two different measurements of one window:
     * the chrome lays out in the surface's pixels at the scale the display's
     * density chose, and every event it is driven by arrives in points.
     */
    aux_refresh_points(platform);
    {
        int pixel_w = 0;
        int pixel_h = 0;

        aux_drawable_size(platform, &pixel_w, &pixel_h);
        if( !aux_make_surface(platform, pixel_w, pixel_h) )
        {
            SDL_DestroyRenderer(platform->aux_renderer);
            SDL_DestroyWindow(platform->aux_window);
            platform->aux_renderer = NULL;
            platform->aux_window = NULL;
            return false;
        }
    }

    platform->aux_window_id = SDL_GetWindowID(platform->aux_window);
    platform->aux_close_requested = false;
    /* A fresh window wears its frame until someone asks otherwise. The wish
     * belongs to the window, not to the platform: a caller that opened one
     * borderless and closed it must ask again for the next, so a frame is
     * never taken off a window nobody looked at. The PROVIDER above outlives
     * the window on purpose -- it is set once and answers for every window the
     * same chrome draws into. */
    platform->hit_aux.borderless = 0;
    return true;
}

void
PlatformWindow_AuxClose(struct PlatformWindow* platform)
{
    assert(platform);

    if( platform->aux_texture )
    {
        SDL_DestroyTexture(platform->aux_texture);
        platform->aux_texture = NULL;
    }
    if( platform->aux_renderer )
    {
        SDL_DestroyRenderer(platform->aux_renderer);
        platform->aux_renderer = NULL;
    }
    if( platform->aux_window )
    {
        /* Off before the window goes: SDL holds the callback and our userdata
         * on the window, and a hit test left installed on a window being
         * destroyed is a call into `hit_aux` from inside the teardown. */
        SDL_SetWindowHitTest(platform->aux_window, NULL, NULL);
        SDL_DestroyWindow(platform->aux_window);
        platform->aux_window = NULL;
    }
    platform->hit_aux.borderless = 0;
    free(platform->aux_pixels);
    platform->aux_pixels = NULL;
    platform->aux_width = 0;
    platform->aux_height = 0;
    platform->aux_point_w = 0;
    platform->aux_point_h = 0;
    platform->aux_window_id = 0;
    platform->aux_close_requested = false;
}

bool
PlatformWindow_AuxIsOpen(struct PlatformWindow const* platform)
{
    assert(platform);
    return platform->aux_window != NULL;
}

int*
PlatformWindow_AuxPixels(struct PlatformWindow* platform)
{
    assert(platform);
    return platform->aux_pixels;
}

int
PlatformWindow_AuxWidth(struct PlatformWindow const* platform)
{
    assert(platform);
    return platform->aux_width;
}

int
PlatformWindow_AuxHeight(struct PlatformWindow const* platform)
{
    assert(platform);
    return platform->aux_height;
}

bool
PlatformWindow_AuxResize(struct PlatformWindow* platform, int width, int height)
{
    assert(platform);
    if( !platform->aux_window )
        return false;
    if( width == platform->aux_width && height == platform->aux_height )
        return true;
    /* The window's points move with its pixels, and they are what the next
     * event is scaled against: a surface resized without them scales every
     * click by the ratio the window had before the drag. */
    aux_refresh_points(platform);
    return aux_make_surface(platform, width, height);
}

void
PlatformWindow_AuxPresent(struct PlatformWindow* platform)
{
    int* write = NULL;
    int pitch = 0;

    assert(platform);
    if( !platform->aux_window || !platform->aux_texture || !platform->aux_pixels )
        return;

    if( SDL_LockTexture(platform->aux_texture, NULL, (void**)&write, &pitch) < 0 )
    {
        fprintf(stderr, "aux window lock: %s\n", SDL_GetError());
        return;
    }
    /* Row by row rather than one memcpy: SDL's pitch is not required to equal
     * the row width, and assuming it does is the classic torn-diagonal bug. */
    {
        int const row_ints = pitch / (int)sizeof(int);
        for( int y = 0; y < platform->aux_height; y++ )
            memcpy(
                &write[y * row_ints],
                &platform->aux_pixels[y * platform->aux_width],
                (size_t)platform->aux_width * sizeof(int));
    }
    SDL_UnlockTexture(platform->aux_texture);

    SDL_RenderClear(platform->aux_renderer);
    SDL_RenderCopy(platform->aux_renderer, platform->aux_texture, NULL, NULL);
    SDL_RenderPresent(platform->aux_renderer);

}

bool
PlatformWindow_AuxTakeCloseRequest(struct PlatformWindow* platform)
{
    bool const asked = platform->aux_close_requested;
    assert(platform);
    platform->aux_close_requested = false;
    return asked;
}

/* ---- plugin chrome attached to the main window -------------------------- */

static bool
chrome_make_surface(struct PlatformWindow* platform, int width, int height)
{
    SDL_Texture* texture;
    int* pixels;

    assert(platform);
    if( !platform->renderer || width <= 0 || height <= 0 )
        return false;
    if( width == platform->chrome_width && height == platform->chrome_height &&
        platform->chrome_texture && platform->chrome_pixels )
        return true;

    texture = SDL_CreateTexture(
        platform->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height);
    if( !texture )
    {
        fprintf(stderr, "attached chrome texture: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    pixels = calloc((size_t)width * (size_t)height, sizeof(*pixels));
    assert(pixels);

    if( platform->chrome_texture )
        SDL_DestroyTexture(platform->chrome_texture);
    free(platform->chrome_pixels);
    platform->chrome_texture = texture;
    platform->chrome_pixels = pixels;
    platform->chrome_width = width;
    platform->chrome_height = height;
    platform->chrome_dirty = true;
    return true;
}

static void
chrome_drawable_size(struct PlatformWindow* platform, int* out_w, int* out_h)
{
    int drawable_w = 0;
    int drawable_h = 0;
    int point_w = 0;
    int point_h = 0;
    int width = 0;

    assert(platform);
    sdl_drawable_size(platform, &drawable_w, &drawable_h);
    SDL_GetWindowSize(platform->window, &point_w, &point_h);
    (void)point_w;
    if( platform->chrome_open && point_w > 0 && drawable_w > 0 )
        width = platform->chrome_point_w * drawable_w / point_w;
    if( width < 0 )
        width = 0;
    if( width > drawable_w )
        width = drawable_w;
    if( out_w )
        *out_w = width;
    if( out_h )
        *out_h = drawable_h;
}

static void
sdl_chrome_sync_drawable(struct PlatformWindow* platform)
{
    int w = 0;
    int h = 0;

    assert(platform);
    if( !platform->chrome_open )
        return;
    chrome_drawable_size(platform, &w, &h);
    if( w <= 0 || h <= 0 || (w == platform->chrome_width && h == platform->chrome_height) )
        return;
    platform->chrome_input.resized = 1;
    platform->chrome_input.width = w;
    platform->chrome_input.height = h;
    platform->chrome_have_input = true;
}

bool
PlatformWindow_ChromeOpen(
    struct PlatformWindow* platform, int width, int height, char const* title)
{
    int point_w = 0;
    int point_h = 0;
    int pixel_w = 0;
    int pixel_h = 0;

    (void)title; /* The pane shares the main window's title. */
    assert(platform);
    if( platform->chrome_open )
        return true;
    if( !platform->window || !platform->renderer || platform->use_opengl || width <= 0 )
        return false;

    SDL_GetWindowSize(platform->window, &point_w, &point_h);
    (void)point_h;
    if( point_w <= 0 || point_h <= 0 )
        return false;
    platform->chrome_saved_point_w = point_w;
    platform->chrome_saved_point_h = point_h;
    platform->chrome_point_w = width;
    platform->chrome_open = true;
    platform->chrome_grew = true;
    platform->chrome_focused = false;
    platform->chrome_pointer_down = false;
    memset(&platform->chrome_input, 0, sizeof(platform->chrome_input));
    platform->chrome_have_input = false;

    /* Attached-grow: add exactly the pane once. A repeated Open returns above,
     * and Close subtracts it, so toggling cannot ratchet the window wider. */
    SDL_SetWindowSize(
        platform->window,
        point_w + width,
        height > point_h ? height : point_h);
    chrome_drawable_size(platform, &pixel_w, &pixel_h);
    if( !chrome_make_surface(platform, pixel_w, pixel_h) )
    {
        platform->chrome_open = false;
        platform->chrome_grew = false;
        platform->chrome_point_w = 0;
        SDL_SetWindowSize(platform->window, point_w, point_h);
        return false;
    }
    return true;
}

void
PlatformWindow_ChromeClose(struct PlatformWindow* platform)
{
    int point_w = 0;
    int point_h = 0;
    int restore_w;

    assert(platform);
    if( !platform->chrome_open )
        return;
    SDL_GetWindowSize(platform->window, &point_w, &point_h);
    restore_w = point_w - platform->chrome_point_w;
    if( restore_w < platform->chrome_saved_point_w )
        restore_w = platform->chrome_saved_point_w;

    platform->chrome_open = false;
    platform->chrome_focused = false;
    platform->chrome_pointer_down = false;
    platform->chrome_have_input = false;
    platform->chrome_dirty = false;
    if( platform->chrome_texture )
        SDL_DestroyTexture(platform->chrome_texture);
    platform->chrome_texture = NULL;
    free(platform->chrome_pixels);
    platform->chrome_pixels = NULL;
    platform->chrome_width = 0;
    platform->chrome_height = 0;
    platform->chrome_point_w = 0;
    if( platform->chrome_grew && platform->window )
        SDL_SetWindowSize(platform->window, restore_w, point_h);
    platform->chrome_grew = false;
}

bool
PlatformWindow_ChromeIsOpen(struct PlatformWindow const* platform)
{
    assert(platform);
    return platform->chrome_open;
}

int*
PlatformWindow_ChromePixels(struct PlatformWindow* platform)
{
    assert(platform);
    return platform->chrome_pixels;
}

int
PlatformWindow_ChromeWidth(struct PlatformWindow const* platform)
{
    assert(platform);
    return platform->chrome_width;
}

int
PlatformWindow_ChromeHeight(struct PlatformWindow const* platform)
{
    assert(platform);
    return platform->chrome_height;
}

bool
PlatformWindow_ChromeResize(struct PlatformWindow* platform, int width, int height)
{
    assert(platform);
    return platform->chrome_open && chrome_make_surface(platform, width, height);
}

void
PlatformWindow_ChromePresent(struct PlatformWindow* platform)
{
    int* write = NULL;
    int pitch = 0;

    assert(platform);
    if( !platform->chrome_open || !platform->chrome_texture || !platform->chrome_pixels )
        return;
    if( SDL_LockTexture(platform->chrome_texture, NULL, (void**)&write, &pitch) < 0 )
    {
        fprintf(stderr, "attached chrome lock: %s\n", SDL_GetError());
        return;
    }
    for( int y = 0; y < platform->chrome_height; y++ )
        memcpy(
            (unsigned char*)write + (size_t)y * (size_t)pitch,
            &platform->chrome_pixels[y * platform->chrome_width],
            (size_t)platform->chrome_width * sizeof(*platform->chrome_pixels));
    SDL_UnlockTexture(platform->chrome_texture);
    platform->chrome_dirty = true;
}

bool
PlatformWindow_ChromeTakeInput(
    struct PlatformWindow* platform, struct PlatformWindow_AuxInput* out)
{
    assert(platform);
    assert(out);
    if( !platform->chrome_open || !platform->chrome_have_input )
        return false;
    *out = platform->chrome_input;
    platform->chrome_input.mouse_down = 0;
    platform->chrome_input.mouse_up = 0;
    platform->chrome_input.wheel = 0;
    platform->chrome_input.text[0] = '\0';
    platform->chrome_input.edit_key = PLATFORM_AUX_KEY_NONE;
    platform->chrome_input.resized = 0;
    platform->chrome_have_input = false;
    return true;
}

/* ---- borderless windows -------------------------------------------------- */

/*
 * What one point of a frameless window is, to the window manager.
 *
 * Called from inside SDL's event pump while it decides what a press is, so
 * everything here is a handful of comparisons and one call into a point test
 * over published rectangles. Nothing in this path may take a lock, allocate, or
 * read anything the frame thread writes.
 */
static SDL_HitTestResult SDLCALL
sdl_hit_test(SDL_Window* window, SDL_Point const* area, void* data)
{
    struct SdlHitState* st = data;
    int win_w = 0;
    int win_h = 0;
    int cx = 0;
    int cy = 0;

    /* Not asserts: SDL owns this call. A hit test still installed on a window
     * being torn down is SDL's business, and aborting inside the WM's own
     * press handling would be a crash with no frame of ours on the stack. */
    if( !st || !area || !st->borderless )
        return SDL_HITTEST_NORMAL;

    SDL_GetWindowSize(window, &win_w, &win_h);

    /*
     * The resize bands FIRST, and it has to be first.
     *
     * A frameless window has no border of its own to pull, and the chrome that
     * provides the drag handle runs right up to the top edge -- a tab strip
     * pinned under the title bar, in the case this was built for. Testing the
     * handle first takes that edge, and a window whose top edge drags instead
     * of resizing can never be made shorter from the top again.
     */
    if( win_w > 0 && win_h > 0 && (SDL_GetWindowFlags(window) & SDL_WINDOW_RESIZABLE) != 0 )
    {
        int const left = area->x < SDL_BORDERLESS_RESIZE;
        int const right = area->x >= win_w - SDL_BORDERLESS_RESIZE;
        int const top = area->y < SDL_BORDERLESS_RESIZE;
        int const bottom = area->y >= win_h - SDL_BORDERLESS_RESIZE;

        /* Corners before edges: a corner point is in two bands at once, and
         * the diagonal is the one the user aimed at. */
        if( top && left )
            return SDL_HITTEST_RESIZE_TOPLEFT;
        if( top && right )
            return SDL_HITTEST_RESIZE_TOPRIGHT;
        if( bottom && left )
            return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if( bottom && right )
            return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if( top )
            return SDL_HITTEST_RESIZE_TOP;
        if( bottom )
            return SDL_HITTEST_RESIZE_BOTTOM;
        if( left )
            return SDL_HITTEST_RESIZE_LEFT;
        if( right )
            return SDL_HITTEST_RESIZE_RIGHT;
    }

    if( !st->fn )
        return SDL_HITTEST_NORMAL;

    /*
     * Into the coordinates whatever drew the handle laid them out in.
     *
     * SDL reports the point in window POINTS. The aux window's surface is the
     * window, so its chrome is already in that space; the game window
     * letterboxes a fixed canvas into whatever the user dragged the frame to,
     * and a handle compared against raw window points there is out by the bars
     * and by the HighDPI factor at once.
     */
    if( st->is_aux )
    {
        /* Points into the surface's pixels: the handles were published by a
         * chrome that laid them out there, so a raw point compares a 2x
         * window's title bar against half of one.
         *
         * The null test is not an assert, for this path's own reason: SDL owns
         * the call, and a hit test still installed on a window being torn down
         * must answer rather than abort inside the WM's press handling. */
        cx = area->x;
        cy = area->y;
        if( st->platform )
            aux_point_to_pixel(st->platform, area->x, area->y, &cx, &cy);
    }
    else
    {
        /* MapMouse clamps into the canvas, so a point in a letterbox bar
         * arrives as the edge pixel beside it. Deliberately left that way: on
         * a frameless window the bars are bare background with nothing else to
         * do, and dragging one is the behaviour a user expects of them. */
        PlatformWindow_MapMouse(st->platform, area->x, area->y, &cx, &cy);
    }
    return st->fn(st->user, cx, cy) ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
}

/** Put one window into (or out of) the frameless state. @return true on the
 *  state asked for. */
static bool
sdl_set_borderless(
    struct PlatformWindow* platform,
    struct SdlHitState* st,
    SDL_Window* window,
    int is_aux,
    bool borderless)
{
    assert(platform);
    assert(st);

    if( !window )
        return false;

    st->platform = platform;
    st->is_aux = is_aux;

    if( !borderless )
    {
        SDL_SetWindowHitTest(window, NULL, NULL);
        st->borderless = 0;
        SDL_SetWindowBordered(window, SDL_TRUE);
        return true;
    }

    if( st->borderless )
        return true;

    /*
     * The hit test goes on BEFORE the frame comes off, and a driver that has
     * no hit test keeps its frame.
     *
     * Every way a user has of moving or resizing a window runs through one of
     * the two. Taking the frame off a window the WM will not ask us about
     * leaves it pinned where it opened, at the size it opened, with no drawn
     * affordance able to help -- for the rest of the session. Refusing is the
     * better failure, and saying so is what stops it reading as a chrome bug.
     */
    if( SDL_SetWindowHitTest(window, sdl_hit_test, st) != 0 )
    {
        fprintf(
            stderr,
            "borderless: this video driver has no window hit test (%s); keeping the frame\n",
            SDL_GetError());
        return false;
    }
    st->borderless = 1;
    SDL_SetWindowBordered(window, SDL_FALSE);
    return true;
}

void
PlatformWindow_SetDragHandleProvider(
    struct PlatformWindow* platform, PlatformWindow_DragHandleFn fn, void* user)
{
    assert(platform);
    platform->hit_main.platform = platform;
    platform->hit_main.is_aux = 0;
    platform->hit_main.fn = fn;
    platform->hit_main.user = user;
}

void
PlatformWindow_AuxSetDragHandleProvider(
    struct PlatformWindow* platform, PlatformWindow_DragHandleFn fn, void* user)
{
    assert(platform);
    platform->hit_aux.platform = platform;
    platform->hit_aux.is_aux = 1;
    platform->hit_aux.fn = fn;
    platform->hit_aux.user = user;
}

bool
PlatformWindow_SetBorderless(struct PlatformWindow* platform, bool borderless)
{
    assert(platform);
    return sdl_set_borderless(platform, &platform->hit_main, platform->window, 0, borderless);
}

bool
PlatformWindow_AuxSetBorderless(struct PlatformWindow* platform, bool borderless)
{
    assert(platform);
    return sdl_set_borderless(platform, &platform->hit_aux, platform->aux_window, 1, borderless);
}

bool
PlatformWindow_IsBorderless(struct PlatformWindow const* platform)
{
    assert(platform);
    return platform->hit_main.borderless != 0;
}

bool
PlatformWindow_AuxIsBorderless(struct PlatformWindow const* platform)
{
    assert(platform);
    return platform->hit_aux.borderless != 0;
}

int
PlatformWindow_Width(struct PlatformWindow* platform)
{
    assert(platform);
    return platform->width;
}

int
PlatformWindow_Height(struct PlatformWindow* platform)
{
    assert(platform);
    return platform->height;
}

int
PlatformWindow_PixelDensity(struct PlatformWindow* platform)
{
    assert(platform);
    /* Re-read rather than answer from the cache. The value taken at window
     * creation is stale on macOS -- the drawable is not backed at its device
     * size until the window is shown, so a HighDPI boot read 1 there and the
     * chrome picked its 1x face for a 2x framebuffer. Two SDL getters, on a
     * path that runs once a frame at most. */
    if( platform->window )
        sdl_refresh_pixel_density(platform);
    return platform->pixel_density > 0 ? platform->pixel_density : 1;
}

bool
PlatformWindow_QuitRequested(struct PlatformWindow* platform)
{
    assert(platform);
    return platform->quit;
}

/*
 * The window title, set only when it actually changed.
 *
 * The caller is a per-frame debug readout (main.c's update_window_title), so
 * this runs 120 times a second with the same string in it. On a desktop that
 * is a cheap strdup inside SDL; in the browser SDL_SetWindowTitle is
 * `document.title = ...`, which notifies the browser process every time and
 * measured 449.8 ms of main-thread time across a 23.5 s trace -- 4.8% of all
 * non-idle time, spent telling the page something it already said. Comparing
 * first costs a strcmp of a string that is already in cache.
 */
void
PlatformWindow_SetTitle(
    struct PlatformWindow* platform,
    char const* title)
{
    assert(platform);
    assert(platform->window);
    assert(title);
    if( strncmp(platform->title, title, sizeof(platform->title) - 1) == 0 )
        return;
    snprintf(platform->title, sizeof(platform->title), "%s", title);
    SDL_SetWindowTitle(platform->window, title);
}

bool
PlatformWindow_Resize(
    struct PlatformWindow* platform,
    int width,
    int height)
{
    SDL_Texture* texture;
    int* pixels;
    size_t pixel_count;

    assert(platform);
    if( width <= 0 || height <= 0 )
        return false;
    if( width == platform->width && height == platform->height )
        return false;
    /* GL mode has no CPU backbuffer to resize — the caller still updates the
     * renderer's viewport, which is the whole of a GL resize. */
    if( platform->use_opengl )
    {
        platform->width = width;
        platform->height = height;
        return true;
    }

    assert(platform->renderer);
    texture = SDL_CreateTexture(
        platform->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height);
    if( !texture )
    {
        fprintf(stderr, "SDL_CreateTexture (resize) failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(
        texture, sdl_interface_scale_mode(platform->interface_scale_mode));

    pixel_count = (size_t)width * (size_t)height;
    pixels = malloc(pixel_count * sizeof(int));
    assert(pixels);
    memset(pixels, 0, pixel_count * sizeof(int));

    /* Swap only after both allocations succeeded: a failed resize must leave a
     * drawable window behind, not a NULL backbuffer. */
    if( platform->texture )
        SDL_DestroyTexture(platform->texture);
    free(platform->pixels);
    platform->texture = texture;
    platform->pixels = pixels;
    platform->width = width;
    platform->height = height;
    return true;
}

void
PlatformWindow_SetInterfaceScaleMode(
    struct PlatformWindow* platform,
    int mode)
{
    assert(platform);
    if( mode < 0 )
        mode = 0;
    if( mode > 2 )
        mode = 2;
    if( platform->interface_scale_mode == mode )
        return;
    platform->interface_scale_mode = mode;
    if( platform->texture )
        SDL_SetTextureScaleMode(platform->texture, sdl_interface_scale_mode(mode));
}

void
PlatformWindow_SetTextInput(struct PlatformWindow* platform, int on)
{
    assert(platform);
    (void)platform;
    /* Asked every time rather than tracked: SDL already tracks it, and a second
     * copy here could only disagree. Both calls are idempotent. */
    if( on )
        SDL_StartTextInput();
    else
        SDL_StopTextInput();
}

void
PlatformWindow_SetTouchViewport(struct PlatformWindow* p, int x, int y, int w, int h)
{
    assert(p);
    ToriRS_TouchSetViewport(&p->touch, x, y, w, h);
}

void
PlatformWindow_SetTouchOverlayTest(struct PlatformWindow* p, ToriRS_TouchOverlayFn fn, void* user)
{
    assert(p);
    ToriRS_TouchSetOverlayTest(&p->touch, fn, user);
}

void
PlatformWindow_SetCanvasFollowsWindow(
    struct PlatformWindow* platform,
    struct ToriRS_CmdBus* bus,
    bool follow,
    int min_w,
    int min_h)
{
    int window_w = 0;
    int window_h = 0;
    bool was_following;

    assert(platform);
    was_following = platform->canvas_follows_window;
    platform->canvas_follows_window = follow;
    if( !platform->window )
        return;

    /* The floor is a window constraint in both modes — see the header. SDL
     * rejects a non-positive minimum, so a caller that has no floor to state
     * gets no constraint rather than an assert inside SDL. */
    if( min_w > 0 && min_h > 0 )
        SDL_SetWindowMinimumSize(platform->window, min_w, min_h);

    if( !follow )
    {
        /* Fixed mode is the floor-sized frame at 1:1. Snapping the window is
         * what makes "fixed" observable; the SIZE_CHANGED it raises is ignored
         * because the follow gate is already clear. Remember the size first:
         * the snap is the one window change the user did not ask for, so
         * coming back out of fixed has to be able to undo it. */
        SDL_GetWindowSize(platform->window, &window_w, &window_h);
        if( was_following && window_w > min_w && window_h > min_h )
        {
            platform->resizable_w = window_w;
            platform->resizable_h = window_h;
        }
        if( min_w > 0 && min_h > 0 )
            SDL_SetWindowSize(platform->window, min_w, min_h);
        return;
    }

    /* Back to resizable: restore the size fixed mode took away, so a round
     * trip through the Display dropdown is a no-op rather than a shrink. The
     * SIZE_CHANGED this raises is redundant with the push below (App_SetCanvasSize
     * no-ops on an unchanged size) but harmless. */
    if( !was_following && platform->resizable_w > 0 && platform->resizable_h > 0 )
    {
        SDL_SetWindowSize(
            platform->window, platform->resizable_w, platform->resizable_h);
        platform->resizable_w = 0;
        platform->resizable_h = 0;
    }

    /* Drawable pixels, not window points: the canvas the client lays out at is
     * the buffer it rasterises into, and on a HighDPI window those differ by
     * the density. Pushing points here is what leaves a Retina window drawing
     * a quarter-resolution canvas that the present then stretches back up. */
    sdl_refresh_pixel_density(platform);
    sdl_drawable_size(platform, &window_w, &window_h);
    if( bus && window_w > 0 && window_h > 0 )
        CmdBus_PushWindowResize(bus, window_w, window_h);
}

void
PlatformWindow_SetWindowSize(
    struct PlatformWindow* platform,
    int width,
    int height)
{
    assert(platform);
    if( !platform->window || width <= 0 || height <= 0 )
        return;
    SDL_SetWindowSize(platform->window, width, height);
}

void
PlatformWindow_MapMouse(
    struct PlatformWindow* platform,
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
    if( platform->chrome_open )
        window_w -= platform->chrome_point_w;
    if( window_w < 1 )
        window_w = 1;
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

static void
chrome_point_to_pixel(
    struct PlatformWindow const* platform, int x, int y, int* out_x, int* out_y)
{
    int point_w = 0;
    int point_h = 0;

    assert(platform);
    assert(out_x);
    assert(out_y);
    SDL_GetWindowSize(platform->window, &point_w, &point_h);
    *out_x = platform->chrome_point_w > 0
                 ? x * platform->chrome_width / platform->chrome_point_w
                 : x;
    *out_y = point_h > 0 ? y * platform->chrome_height / point_h : y;
    if( *out_x < 0 )
        *out_x = 0;
    if( *out_x >= platform->chrome_width )
        *out_x = platform->chrome_width > 0 ? platform->chrome_width - 1 : 0;
    if( *out_y < 0 )
        *out_y = 0;
    if( *out_y >= platform->chrome_height )
        *out_y = platform->chrome_height > 0 ? platform->chrome_height - 1 : 0;
}

/* Route one event inside the main window's attached pane. Returning true is a
 * hard ownership boundary: the game command bus never sees that event. */
static bool
sdl_chrome_event(struct PlatformWindow* platform, SDL_Event const* event)
{
    uint32_t const main_id = SDL_GetWindowID(platform->window);
    int point_w = 0;
    int point_h = 0;
    int pane_x;
    int x;
    int y;

    if( !platform->chrome_open )
        return false;
    SDL_GetWindowSize(platform->window, &point_w, &point_h);
    pane_x = point_w - platform->chrome_point_w;

    switch( event->type )
    {
    case SDL_MOUSEBUTTONDOWN:
        if( event->button.windowID != main_id )
            return false;
        if( event->button.x < pane_x )
        {
            if( event->button.button == SDL_BUTTON_LEFT )
                platform->chrome_focused = false;
            return false;
        }
        chrome_point_to_pixel(
            platform, event->button.x - pane_x, event->button.y, &x, &y);
        platform->chrome_input.mouse_x = x;
        platform->chrome_input.mouse_y = y;
        if( event->button.button == SDL_BUTTON_LEFT )
        {
            platform->chrome_input.mouse_down = 1;
            platform->chrome_pointer_down = true;
        }
        platform->chrome_focused = true;
        platform->chrome_have_input = true;
        return true;

    case SDL_MOUSEBUTTONUP:
        if( event->button.windowID != main_id ||
            (!platform->chrome_pointer_down && event->button.x < pane_x) )
            return false;
        chrome_point_to_pixel(
            platform, event->button.x - pane_x, event->button.y, &x, &y);
        platform->chrome_input.mouse_x = x;
        platform->chrome_input.mouse_y = y;
        if( event->button.button == SDL_BUTTON_LEFT )
        {
            platform->chrome_input.mouse_up = 1;
            platform->chrome_pointer_down = false;
        }
        platform->chrome_have_input = true;
        return true;

    case SDL_MOUSEMOTION:
        if( event->motion.windowID != main_id ||
            (!platform->chrome_pointer_down && event->motion.x < pane_x) )
            return false;
        chrome_point_to_pixel(
            platform, event->motion.x - pane_x, event->motion.y, &x, &y);
        platform->chrome_input.mouse_x = x;
        platform->chrome_input.mouse_y = y;
        platform->chrome_have_input = true;
        return true;

    case SDL_MOUSEWHEEL:
        if( event->wheel.windowID != main_id || !platform->chrome_focused )
            return false;
        platform->chrome_input.wheel +=
            event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
        platform->chrome_have_input = true;
        return true;

    case SDL_TEXTINPUT:
        if( event->text.windowID != main_id || !platform->chrome_focused )
            return false;
        {
            size_t const have = strlen(platform->chrome_input.text);
            size_t const room = sizeof(platform->chrome_input.text) - have - 1;
            strncat(platform->chrome_input.text, event->text.text, room);
        }
        platform->chrome_have_input = true;
        return true;

    case SDL_KEYUP:
        return event->key.windowID == main_id && platform->chrome_focused;

    case SDL_KEYDOWN:
        if( event->key.windowID != main_id || !platform->chrome_focused )
            return false;
        switch( event->key.keysym.sym )
        {
        case SDLK_BACKSPACE:
            platform->chrome_input.edit_key = PLATFORM_AUX_KEY_BACKSPACE;
            break;
        case SDLK_DELETE:
            platform->chrome_input.edit_key = PLATFORM_AUX_KEY_DELETE;
            break;
        case SDLK_LEFT:
            platform->chrome_input.edit_key = PLATFORM_AUX_KEY_LEFT;
            break;
        case SDLK_RIGHT:
            platform->chrome_input.edit_key = PLATFORM_AUX_KEY_RIGHT;
            break;
        case SDLK_HOME:
            platform->chrome_input.edit_key = PLATFORM_AUX_KEY_HOME;
            break;
        case SDLK_END:
            platform->chrome_input.edit_key = PLATFORM_AUX_KEY_END;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            platform->chrome_input.edit_key = PLATFORM_AUX_KEY_ENTER;
            break;
        case SDLK_ESCAPE:
            platform->chrome_input.edit_key = PLATFORM_AUX_KEY_ESCAPE;
            break;
        case SDLK_UP:
            platform->chrome_input.edit_key = PLATFORM_AUX_KEY_UP;
            break;
        case SDLK_DOWN:
            platform->chrome_input.edit_key = PLATFORM_AUX_KEY_DOWN;
            break;
        default:
            return true;
        }
        platform->chrome_have_input = true;
        return true;

    default:
        return false;
    }
}

void
PlatformWindow_PollCommands(
    struct PlatformWindow* platform,
    struct ToriRS_CmdBus* bus)
{
    SDL_Event event;
    int lx;
    int ly;
    enum LibToriRS_KeyCode key;
    enum LibToriRS_MouseButton button;
    /*
     * The reference emits BOTH a key-code event and a character event for a
     * printable keypress (KeyHandler.copy$keyPressed queues (code, char=0),
     * then copy$keyTyped queues (-1, char)). Scripts that match on event_key —
     * notably chatbox_keyinput_listener (57), which advances "Click here to
     * continue" on space = OSRS key 83 — need the code event; chat typing
     * inserts on the character event. SDL splits one keypress into KEYDOWN
     * then TEXTINPUT, so push the code event on KEYDOWN and the character
     * event on TEXTINPUT. Do not cancel one when the other arrives.
     */
    int pending_mods = 0;
    int pending_repeat = 0;
    /*
     * A window drag emits a SIZE_CHANGED per mouse-move, and each one would cost
     * a backbuffer realloc plus a whole-tree relayout plus every gameframe
     * onResize script. Coalesce them: remember the last size seen in this poll
     * batch and apply it once, after the loop. One apply per frame, which is the
     * rate the client can actually consume.
     */
    int pending_resize_w = -1;
    int pending_resize_h = -1;

    assert(platform);
    assert(bus);

    /* Before the events, so a density change that arrived without one is
     * reported on the same frame as anything else the window saw. */
    sdl_aux_sync_drawable(platform);
    sdl_chrome_sync_drawable(platform);

    while( SDL_PollEvent(&event) )
    {
        /*
         * The aux window's events first, and they never reach the game.
         *
         * Routed by SDL's window id rather than by "is the pointer over it",
         * because a drag that started in one window keeps delivering to that
         * window -- which is what makes a grip drag work when the cursor
         * leaves the frame, and what would make a click in the plugin window
         * also walk the player if this fell through.
         */
        if( platform->aux_window && sdl_aux_event(platform, &event) )
            continue;
        if( platform->chrome_open && sdl_chrome_event(platform, &event) )
            continue;

        switch( event.type )
        {
        case SDL_QUIT:
            platform->quit = true;
            break;
        case SDL_KEYDOWN:
        {
            int vk;
            int osrs;

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
                /* Code event now; a following TEXTINPUT may add a character
                 * event. Reference KeyHandler queues both for printable keys. */
                CmdBus_PushKeyEvent(bus, osrs, 0, event.key.repeat ? 1 : 0);
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
                CmdBus_PushKeyEvent(bus, -1, codepoint, pending_repeat);
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
                pending_mods = 0;
                CmdBus_Push(bus, TORIRS_CMD_INPUT_CLEAR_KEYS, NULL, 0);
            }
            /* SIZE_CHANGED, not RESIZED: RESIZED fires only for user-driven
             * resizes, and a programmatic SDL_SetWindowSize (what the window-mode
             * op does) has to relayout too. */
            else if(
                event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED &&
                platform->canvas_follows_window )
            {
                /* data1/data2 are POINTS. The canvas is pixels, and the two
                 * differ by the density -- and the density itself can change
                 * here, which is what a window dragged between a Retina and an
                 * ordinary display looks like from in here. */
                sdl_refresh_pixel_density(platform);
                sdl_drawable_size(platform, &pending_resize_w, &pending_resize_h);
                if( platform->chrome_open )
                {
                    int pane_w = 0;
                    chrome_drawable_size(platform, &pane_w, NULL);
                    pending_resize_w -= pane_w;
                }
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            PlatformWindow_MapMouse(platform, event.button.x, event.button.y, &lx, &ly);
            button = sdl_mouse_button_to_torirs(event.button.button);
            CmdBus_PushMouseButton(
                bus, TORIRS_CMD_INPUT_MOUSE_DOWN, (uint8_t)button, (int16_t)lx, (int16_t)ly);
            break;
        case SDL_MOUSEBUTTONUP:
            PlatformWindow_MapMouse(platform, event.button.x, event.button.y, &lx, &ly);
            button = sdl_mouse_button_to_torirs(event.button.button);
            CmdBus_PushMouseButton(
                bus, TORIRS_CMD_INPUT_MOUSE_UP, (uint8_t)button, (int16_t)lx, (int16_t)ly);
            break;
        case SDL_MOUSEMOTION:
            PlatformWindow_MapMouse(platform, event.motion.x, event.motion.y, &lx, &ly);
            CmdBus_PushMouseMove(bus, (int16_t)lx, (int16_t)ly);
            break;
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP:
        {
            /*
             * SDL reports a finger NORMALISED to the window -- 0..1 on each
             * axis -- because a touch device need not share the window's
             * resolution or even its aspect. So it is scaled back up to window
             * pixels and then run through the same letterbox inverse a mouse
             * takes, which is what puts a finger and a cursor on the same
             * canvas pixel.
             */
            int window_w = 0;
            int window_h = 0;
            enum ToriRS_TouchPhase phase = TORIRS_TOUCH_MOVED;

            SDL_GetWindowSize(platform->window, &window_w, &window_h);
            PlatformWindow_MapMouse(
                platform,
                (int)(event.tfinger.x * (float)window_w),
                (int)(event.tfinger.y * (float)window_h),
                &lx,
                &ly);
            if( event.type == SDL_FINGERDOWN )
                phase = TORIRS_TOUCH_BEGAN;
            else if( event.type == SDL_FINGERUP )
                phase = TORIRS_TOUCH_ENDED;
            ToriRS_TouchEvent(
                &platform->touch, bus, phase, (int64_t)event.tfinger.fingerId, lx, ly,
                (uint64_t)SDL_GetTicks());
            break;
        }
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

    /* The coalesced resize. Pushed after the input above so a click that landed
     * at the old size is applied at the old size, exactly as it was seen.
     *
     * Only the command goes out here — the backbuffer is NOT resized to the
     * window. The client clamps the canvas (APP_CANVAS_MIN_*), so the canvas is
     * the authority on the backbuffer's size and the caller reconciles the two
     * after draining. Resizing to the raw window size here would put the
     * backbuffer below the canvas whenever the user drags past the floor, and
     * App_Render would write off the end of it. */
    if( pending_resize_w > 0 && pending_resize_h > 0 )
        CmdBus_PushWindowResize(bus, pending_resize_w, pending_resize_h);

    /* A finger held perfectly still generates no events at all, so the long
     * press has to be given a chance to become a right click from out here. */
    ToriRS_TouchTick(&platform->touch, bus, (uint64_t)SDL_GetTicks());
}

void
PlatformWindow_SetPresentDamage(
    struct PlatformWindow* platform,
    int x,
    int y,
    int w,
    int h)
{
    /* Accepted and ignored. This backend hands the buffer to a texture upload
     * and a GPU present, where a partial copy is a different mechanism
     * (SDL_UpdateTexture sub-rects) and the win is not the same one. Ignoring
     * it presents more than asked, which is always correct. */
    assert(platform);
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void
PlatformWindow_SetPresentDamageRects(
    struct PlatformWindow* platform,
    int const (*rects)[4],
    int count)
{
    /* Accepted and ignored, for the same reason as the box above. */
    assert(platform);
    (void)rects;
    (void)count;
}

void
PlatformWindow_Present(struct PlatformWindow* platform)
{
    int* pix_write = NULL;
    int texture_pitch = 0;
    int texture_w;
    int window_w = 0;
    int window_h = 0;
    SDL_Rect dst;
    SDL_Rect chrome_dst;
    int pane_w = 0;
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

    /*
     * The renderer's OUTPUT size, not the window's.
     *
     * These are the same number until the window is HighDPI, and then they are
     * not: RenderCopy's destination rect is in the render target's own pixels,
     * while SDL_GetWindowSize answers in points. Sizing the letterbox from
     * points puts a full-size texture into a half-size rect in the top-left
     * corner and clears the rest to black -- which is what soft3d did the
     * moment `hidpi` was switched on, and what the GL path never showed
     * because its viewport is set from the canvas in drawable pixels already.
     *
     * MapMouse keeps SDL_GetWindowSize deliberately: SDL delivers mouse
     * positions in points, so its letterbox has to be built in points too. The
     * two call sites disagree because their inputs are in different units, not
     * because one of them is stale.
     */
    sdl_drawable_size(platform, &window_w, &window_h);
    if( platform->chrome_open )
    {
        chrome_drawable_size(platform, &pane_w, NULL);
        if( pane_w > window_w )
            pane_w = window_w;
        window_w -= pane_w;
    }
    letterbox_dst(platform->width, platform->height, window_w, window_h, &dst);

    /* When the letterbox fills the output, RenderCopy overwrites every pixel —
     * skip the clear (and the software-renderer SDL_FillRect4 it becomes under
     * the headless harness). */
    if( platform->chrome_open || dst.x != 0 || dst.y != 0 || dst.w != window_w ||
        dst.h != window_h )
    {
        SDL_SetRenderDrawColor(platform->renderer, 0, 0, 0, 255);
        SDL_RenderClear(platform->renderer);
    }
    SDL_RenderCopy(platform->renderer, platform->texture, NULL, &dst);
    if( platform->chrome_open && platform->chrome_texture && pane_w > 0 )
    {
        chrome_dst.x = window_w;
        chrome_dst.y = 0;
        chrome_dst.w = pane_w;
        chrome_dst.h = window_h;
        SDL_RenderCopy(platform->renderer, platform->chrome_texture, NULL, &chrome_dst);
    }
    SDL_RenderPresent(platform->renderer);
}

void
PlatformWindow_PresentGL(struct PlatformWindow* platform)
{
    assert(platform);
    assert(platform->use_opengl);
    assert(platform->window);
    SDL_GL_SwapWindow(platform->window);
}

uint64_t
PlatformWindow_Ticks64(void)
{
    return SDL_GetTicks64();
}

uint64_t
PlatformWindow_TicksUs(void)
{
    uint64_t const frequency = SDL_GetPerformanceFrequency();
    uint64_t const counter = SDL_GetPerformanceCounter();

    if( frequency == 0 )
        return SDL_GetTicks64() * 1000u;
    /* Split before multiplying: the counter is ticks since boot, and a direct
     * counter*1000000 overflows 64 bits after a few days of uptime on a
     * nanosecond-resolution clock even though the microsecond result fits. */
    return (counter / frequency) * 1000000u + (counter % frequency) * 1000000u / frequency;
}

void
PlatformWindow_SleepUntil(uint64_t deadline_ms)
{
    for( ;; )
    {
        uint64_t now = SDL_GetTicks64();
        uint64_t remaining;
        uint32_t delay;
        if( now >= deadline_ms )
            return;
        remaining = deadline_ms - now;
        delay = remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
        SDL_Delay(delay);
    }
}
