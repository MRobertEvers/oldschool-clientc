/*
 * The SDL chrome executor: the plugin window as a real, movable OS window.
 *
 * A SURFACE executor (see the two kinds in torirs_chrome_exec.h): the widgets
 * are still ToriRSChrome's, laid out by ToriRSChrome and rasterised by the same
 * software path that draws them in the game canvas. What changes is only where
 * the pixels land and where the pointer comes from. That is the whole reason it
 * can be pixel-identical to the in-canvas panel -- it IS the in-canvas panel,
 * in its own window.
 *
 * It is the first thing in this tree to want a second OS window, so the
 * platform API under it (PlatformSDL2_Aux*) is deliberately the smallest one
 * that serves exactly this: open, close, a pixel buffer, present, and a close
 * request coming back. A backend that has none of that returns false from
 * begin() and the surface falls back to the buffer executor, which is why none
 * of this is load-bearing for a client that never opens a plugin window.
 *
 * This file is compiled only where the SDL platform is, and
 * TORIRS_CHROME_EXEC_SDL_AVAILABLE is what tells the chooser so.
 */

#include "torirs_chrome_exec.h"

#include "../platform/platform_sdl2.h"
#include "uitree_debug_overlay.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/** Opening size. The window is resizable; this is only where it starts. */
#define CHROME_SDL_W 360
#define CHROME_SDL_H 420

struct ChromeSdl
{
    struct PlatformSDL2* platform;
    /*
     * How to turn a display list into pixels.
     *
     * Injected rather than called directly, because rasterising needs the
     * scene the baked fonts and the skin were registered in, the frame
     * translator and a software backend -- three things ui/ deliberately does
     * not depend on. The app owns all of them already for the game canvas, so
     * it lends the same one here and the second window is drawn by the same
     * code as the first rather than by a second copy of it.
     */
    ToriRSChromeRasteriseFn rasterise;
    void* rasterise_user;
    /** Set once begin() succeeded, so present/input on a refused executor are
     *  no-ops rather than calls into a window that was never made. */
    int open;
};

/* The one instance. A second plugin window is not a thing the sandbox allows,
 * so a registry of them would be a registry with one entry in it. */
static struct ChromeSdl g_chrome_sdl;

static int
chrome_sdl_begin(void* user)
{
    struct ChromeSdl* s = user;

    assert(s);
    if( !s->platform )
        return 0;
    if( !PlatformSDL2_AuxOpen(s->platform, CHROME_SDL_W, CHROME_SDL_H, "Plugins") )
        return 0;
    s->open = 1;
    return 1;
}

static void
chrome_sdl_end(void* user)
{
    struct ChromeSdl* s = user;

    assert(s);
    if( !s->open )
        return;
    PlatformSDL2_AuxClose(s->platform);
    s->open = 0;
}

static void
chrome_sdl_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    (void)user;
    (void)cmd;
    /* Nothing: a surface executor draws the chrome's own display list, so the
     * widget-level stream has nothing to tell it. See the two kinds of
     * executor in the header. */
}

/*
 * Rasterise the display list into the aux window's buffer.
 *
 * Deliberately NOT a second implementation of prim drawing: it hands the same
 * list to the same ToriRS_Frame translator and the same software backend the
 * in-canvas path uses. A second rasteriser here would be a second set of
 * rounding, a second baseline convention, and a second place for the chrome to
 * be almost right.
 */
static void
chrome_sdl_present(void* user, struct ToriRSChromePrim const* prims, int count)
{
    struct ChromeSdl* s = user;
    int* pixels;
    int w;
    int h;

    assert(s);
    if( !s->open || !prims )
        return;

    pixels = PlatformSDL2_AuxPixels(s->platform);
    w = PlatformSDL2_AuxWidth(s->platform);
    h = PlatformSDL2_AuxHeight(s->platform);
    if( !pixels || w <= 0 || h <= 0 )
        return;

    /*
     * Cleared, unlike the game canvas.
     *
     * The world viewport is never cleared because the 3D pass covers every
     * pixel of it; this window has nothing behind the chrome at all, so
     * whatever a panel stopped covering when it moved would otherwise smear.
     */
    memset(pixels, 0, (size_t)w * (size_t)h * sizeof(*pixels));

    if( s->rasterise )
        s->rasterise(s->rasterise_user, pixels, w, h, prims, count);
    PlatformSDL2_AuxPresent(s->platform);
}

/*
 * The editing-key values platform/ reports and the ones ui/ understands are the
 * same numbers, restated on each side of a layer boundary neither may cross.
 * Pinned here, where both headers are already included, so a value added to one
 * enum and not the other fails to COMPILE rather than silently mapping Home to
 * Delete.
 */
_Static_assert((int)PLATFORM_AUX_KEY_NONE == (int)TORIRS_CHROME_KEY_NONE, "aux key: none");
_Static_assert(
    (int)PLATFORM_AUX_KEY_BACKSPACE == (int)TORIRS_CHROME_KEY_BACKSPACE,
    "aux key: bksp");
_Static_assert((int)PLATFORM_AUX_KEY_DELETE == (int)TORIRS_CHROME_KEY_DELETE, "aux key: del");
_Static_assert((int)PLATFORM_AUX_KEY_LEFT == (int)TORIRS_CHROME_KEY_LEFT, "aux key: left");
_Static_assert((int)PLATFORM_AUX_KEY_RIGHT == (int)TORIRS_CHROME_KEY_RIGHT, "aux key: right");
_Static_assert((int)PLATFORM_AUX_KEY_HOME == (int)TORIRS_CHROME_KEY_HOME, "aux key: home");
_Static_assert((int)PLATFORM_AUX_KEY_END == (int)TORIRS_CHROME_KEY_END, "aux key: end");
_Static_assert((int)PLATFORM_AUX_KEY_ENTER == (int)TORIRS_CHROME_KEY_ENTER, "aux key: enter");
_Static_assert((int)PLATFORM_AUX_KEY_ESCAPE == (int)TORIRS_CHROME_KEY_ESCAPE, "aux key: esc");

static int
chrome_sdl_surface_input(void* user, struct ToriRSChromeSurfaceInput* out)
{
    struct ChromeSdl* s = user;
    struct PlatformSDL2_AuxInput aux;

    assert(s);
    assert(out);
    if( !s->open )
        return 0;

    /*
     * A close from the window's own title bar is answered by taking the whole
     * executor down, not by hiding a panel: the OS window is going away, and
     * continuing to present into it would be drawing into a window that no
     * longer exists.
     */
    if( PlatformSDL2_AuxTakeCloseRequest(s->platform) )
    {
        PlatformSDL2_AuxClose(s->platform);
        s->open = 0;
        return 0;
    }

    if( !PlatformSDL2_AuxTakeInput(s->platform, &aux) )
        return 0;

    /* The platform's POD across to the chrome's; see the _Static_asserts. */
    memset(out, 0, sizeof(*out));
    out->mouse_x = aux.mouse_x;
    out->mouse_y = aux.mouse_y;
    out->mouse_down = aux.mouse_down;
    out->mouse_up = aux.mouse_up;
    out->wheel = aux.wheel;
    out->edit_key = aux.edit_key;
    out->resized = aux.resized;
    out->width = aux.width;
    out->height = aux.height;
    memcpy(out->text, aux.text, sizeof(out->text) < sizeof(aux.text) ? sizeof(out->text)
                                                                    : sizeof(aux.text));
    out->text[sizeof(out->text) - 1] = '\0';

    /* A resize is applied to the SURFACE here rather than left to the caller:
     * the next present writes into a buffer that must already be the window's
     * size, and the chrome does not care -- it lays out where it was put. */
    if( out->resized )
        PlatformSDL2_AuxResize(s->platform, out->width, out->height);
    return 1;
}

struct ToriRSChromeExec
ToriRSChromeExec_Sdl(void* platform, ToriRSChromeRasteriseFn rasterise, void* rasterise_user)
{
    struct ToriRSChromeExec exec;

    memset(&exec, 0, sizeof(exec));
    memset(&g_chrome_sdl, 0, sizeof(g_chrome_sdl));
    g_chrome_sdl.platform = platform;
    g_chrome_sdl.rasterise = rasterise;
    g_chrome_sdl.rasterise_user = rasterise_user;

    exec.user = &g_chrome_sdl;
    exec.begin = chrome_sdl_begin;
    exec.apply = chrome_sdl_apply;
    exec.end = chrome_sdl_end;
    exec.present = chrome_sdl_present;
    exec.surface_input = chrome_sdl_surface_input;
    exec.is_surface = 1;
    return exec;
}

int
ToriRSChromeExecSdl_IsOpen(void)
{
    return g_chrome_sdl.open;
}
