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
    int aux_width;
    int aux_height;
    /* SDL's id for aux_window, so the event pump can tell which window an
     * event came from without comparing pointers on every event. */
    uint32_t aux_window_id;
    /* Latched by the pump, drained by AuxTakeCloseRequest -- see the header. */
    bool aux_close_requested;
    /* This frame's aux gesture, coalesced by the pump. */
    struct PlatformSDL2_AuxInput aux_input;
    bool aux_have_input;

    /* Last title handed to SDL, so a per-frame caller does not repeat itself.
     * See PlatformSDL2_SetTitle. Sized to hold main.c's readout whole: a title
     * that overflows would compare equal on its tail and stop updating. */
    char title[256];
    /* Drawable pixels per window point, from SDL: 1 on an ordinary display, 2
     * on a Retina/200% one. The framebuffer is sized in DRAWABLE pixels, so
     * this is not a scale anything multiplies by at draw time -- it is what the
     * chrome reads to choose which baked font size to lay out with. */
    int pixel_density;
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
    struct PlatformSDL2* platform,
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
 * no such thing as a 1.5x baked font here (see TORIDBG_SCALE_MAX). A 1.5x
 * display therefore lands on 2x chrome in a 1.5x-pixel framebuffer, which is
 * chrome slightly larger than nominal drawn at native resolution -- the
 * failure this whole path exists to avoid is the other one, chrome drawn at
 * one size and stretched to another.
 */
static void
sdl_refresh_pixel_density(struct PlatformSDL2* platform)
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
PlatformSDL2_SetWantHighDPI(bool want)
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

struct PlatformSDL2*
PlatformSDL2_New(void)
{
    struct PlatformSDL2* platform = malloc(sizeof(struct PlatformSDL2));
    assert(platform);
    memset(platform, 0, sizeof(struct PlatformSDL2));
    platform->interface_scale_mode = 2;
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
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
            (sdl_want_highdpi() ? SDL_WINDOW_ALLOW_HIGHDPI : 0));
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
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
            (sdl_want_highdpi() ? SDL_WINDOW_ALLOW_HIGHDPI : 0));
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
    /* Before the main window's teardown, so the aux one never outlives the
     * SDL_Quit that follows. */
    PlatformSDL2_AuxClose(platform);
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

/* ---- the auxiliary window ------------------------------------------------ */

/*
 * Route one event to the aux window, or report that it was not ours.
 *
 * The gesture is coalesced into aux_input rather than queued, so a frame that
 * saw twenty motion events costs one position. Coordinates are the window's
 * own -- the chrome laid its panels out in that space, and there is no
 * letterbox to undo here because the aux surface IS the window.
 */
static bool
sdl_aux_event(struct PlatformSDL2* platform, SDL_Event const* event)
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
            platform->aux_input.resized = 1;
            platform->aux_input.width = event->window.data1;
            platform->aux_input.height = event->window.data2;
            platform->aux_have_input = true;
        }
        return true;

    case SDL_MOUSEBUTTONDOWN:
        if( event->button.windowID != id )
            return false;
        platform->aux_input.mouse_x = event->button.x;
        platform->aux_input.mouse_y = event->button.y;
        if( event->button.button == SDL_BUTTON_LEFT )
            platform->aux_input.mouse_down = 1;
        platform->aux_have_input = true;
        return true;

    case SDL_MOUSEBUTTONUP:
        if( event->button.windowID != id )
            return false;
        platform->aux_input.mouse_x = event->button.x;
        platform->aux_input.mouse_y = event->button.y;
        if( event->button.button == SDL_BUTTON_LEFT )
            platform->aux_input.mouse_up = 1;
        platform->aux_have_input = true;
        return true;

    case SDL_MOUSEMOTION:
        if( event->motion.windowID != id )
            return false;
        platform->aux_input.mouse_x = event->motion.x;
        platform->aux_input.mouse_y = event->motion.y;
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
PlatformSDL2_AuxTakeInput(struct PlatformSDL2* platform, struct PlatformSDL2_AuxInput* out)
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
aux_make_surface(struct PlatformSDL2* platform, int width, int height)
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
PlatformSDL2_AuxOpen(struct PlatformSDL2* platform, int width, int height, char const* title)
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

    if( !aux_make_surface(platform, width, height) )
    {
        SDL_DestroyRenderer(platform->aux_renderer);
        SDL_DestroyWindow(platform->aux_window);
        platform->aux_renderer = NULL;
        platform->aux_window = NULL;
        return false;
    }

    platform->aux_window_id = SDL_GetWindowID(platform->aux_window);
    platform->aux_close_requested = false;
    return true;
}

void
PlatformSDL2_AuxClose(struct PlatformSDL2* platform)
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
        SDL_DestroyWindow(platform->aux_window);
        platform->aux_window = NULL;
    }
    free(platform->aux_pixels);
    platform->aux_pixels = NULL;
    platform->aux_width = 0;
    platform->aux_height = 0;
    platform->aux_window_id = 0;
    platform->aux_close_requested = false;
}

bool
PlatformSDL2_AuxIsOpen(struct PlatformSDL2 const* platform)
{
    assert(platform);
    return platform->aux_window != NULL;
}

int*
PlatformSDL2_AuxPixels(struct PlatformSDL2* platform)
{
    assert(platform);
    return platform->aux_pixels;
}

int
PlatformSDL2_AuxWidth(struct PlatformSDL2 const* platform)
{
    assert(platform);
    return platform->aux_width;
}

int
PlatformSDL2_AuxHeight(struct PlatformSDL2 const* platform)
{
    assert(platform);
    return platform->aux_height;
}

bool
PlatformSDL2_AuxResize(struct PlatformSDL2* platform, int width, int height)
{
    assert(platform);
    if( !platform->aux_window )
        return false;
    if( width == platform->aux_width && height == platform->aux_height )
        return true;
    return aux_make_surface(platform, width, height);
}

void
PlatformSDL2_AuxPresent(struct PlatformSDL2* platform)
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
PlatformSDL2_AuxTakeCloseRequest(struct PlatformSDL2* platform)
{
    bool const asked = platform->aux_close_requested;
    assert(platform);
    platform->aux_close_requested = false;
    return asked;
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

int
PlatformSDL2_PixelDensity(struct PlatformSDL2* platform)
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
PlatformSDL2_QuitRequested(struct PlatformSDL2* platform)
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
PlatformSDL2_SetTitle(
    struct PlatformSDL2* platform,
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
PlatformSDL2_Resize(
    struct PlatformSDL2* platform,
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
PlatformSDL2_SetInterfaceScaleMode(
    struct PlatformSDL2* platform,
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
PlatformSDL2_SetCanvasFollowsWindow(
    struct PlatformSDL2* platform,
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
PlatformSDL2_SetWindowSize(
    struct PlatformSDL2* platform,
    int width,
    int height)
{
    assert(platform);
    if( !platform->window || width <= 0 || height <= 0 )
        return;
    SDL_SetWindowSize(platform->window, width, height);
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
    letterbox_dst(platform->width, platform->height, window_w, window_h, &dst);

    /* When the letterbox fills the output, RenderCopy overwrites every pixel —
     * skip the clear (and the software-renderer SDL_FillRect4 it becomes under
     * the headless harness). */
    if( dst.x != 0 || dst.y != 0 || dst.w != window_w || dst.h != window_h )
    {
        SDL_SetRenderDrawColor(platform->renderer, 0, 0, 0, 255);
        SDL_RenderClear(platform->renderer);
    }
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

uint64_t
PlatformSDL2_TicksUs(void)
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
PlatformSDL2_SleepUntil(uint64_t deadline_ms)
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
