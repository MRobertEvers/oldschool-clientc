/*
 * The SDL chrome executor: the plugin shell attached to the game window.
 *
 * A SURFACE executor (see the two kinds in torirs_chrome_exec.h): the widgets
 * are still ToriRSChrome's, laid out by ToriRSChrome and rasterised by the same
 * software path that draws them in the game canvas. What changes is only where
 * the pixels land and where the pointer comes from. That is the whole reason it
 * can be pixel-identical to the in-canvas panel. By default the surface is a
 * pane inside the existing SDL window; TORIRS_CHROME_DETACHED=1 is the
 * explicit developer/user opt-in to the legacy auxiliary window.
 *
 * It is the first thing in this tree to want a second OS window, so the
 * platform API under it (PlatformWindow_Aux*) is deliberately the smallest one
 * that serves exactly this: open, close, a pixel buffer, present, and a close
 * request coming back. A backend that has none of that returns false from
 * begin() and the surface falls back to the buffer executor, which is why none
 * of this is load-bearing for a client that never opens a plugin window.
 *
 * This file is compiled only where the SDL platform is, and
 * TORIRS_CHROME_EXEC_SDL_AVAILABLE is what tells the chooser so.
 */

#include "torirs_chrome_exec.h"

#include "../platform/platform_window.h"
#include "uitree_debug_overlay.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Opening size, in window POINTS -- a physical size on a desk, not a pixel
 * count. The window is resizable; this is only where it starts.
 *
 * The SURFACE that comes up inside it is the DRAWABLE, which on a HighDPI
 * display is a multiple of this, and it is the surface -- not this -- that the
 * chrome lays out in (PlatformWindow_AuxWidth/Height, handed over by
 * chrome_sdl_surface_size). The chrome's scale is the display's density, so
 * the two rise together: a 2x display gets 2x rows in a 2x buffer, at the same
 * physical size as 1x rows in a 1x one. Sized in pixels here instead, a 2x
 * chrome would be laid out in half the room it needs -- labels under their
 * fields, and half of a settings page past the bottom edge where widgets get a
 * zero box and stop being clickable at all.
 */
#define CHROME_SDL_W 360
#define CHROME_SDL_H 420

struct ChromeSdl
{
    struct PlatformWindow* platform;
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
    /** The surface is inside the main native window, not the optional aux. */
    int attached;
    /**
     * The panel this window is showing, latched from PANEL_OPEN.
     *
     * The one thing a surface executor needs out of the command stream, and
     * only because a close has to name what closed. Everything else about the
     * widgets it draws it reads off the display list.
     */
    int panel;
    /** The window's own X was used; the model has not been told yet. */
    int close_pending;
    /**
     * This frame's window-move handles, published by the host after Build.
     *
     * The reason a copy lives here rather than the callback asking the model:
     * SDL calls the hit test from inside its event pump, mid-press, and the
     * model is the frame thread's. @see ToriRSChrome_WindowDragRegion.
     */
    struct ToriRSChromeDragRegion drag;
    /** The frame actually came off. Distinct from the wish below, which the
     *  video driver is allowed to refuse. */
    int borderless;
};

/* The one instance. A second plugin window is not a thing the sandbox allows,
 * so a registry of them would be a registry with one entry in it. */
static struct ChromeSdl g_chrome_sdl;

/* The WISH, outside the instance on purpose: ToriRSChromeExec_Sdl clears
 * g_chrome_sdl every time an executor is built, and the shell sets this once at
 * boot -- long before the button that opens the window is pressed. */
static int g_chrome_sdl_want_borderless;

/** The frameless window has been reported once. @see chrome_sdl_begin. */
static int g_chrome_sdl_reported_borderless;

/*
 * The point test SDL's hit test ends up in.
 *
 * Everything it touches is the published snapshot: a dozen rectangles and two
 * counts. It must stay that way -- this runs on the pump's stack while the
 * window manager is deciding what a press is.
 */
static int
chrome_sdl_drag_at(void* user, int x, int y)
{
    struct ChromeSdl* s = user;

    if( !s )
        return 0;
    return ToriRSChromeDragRegion_Contains(&s->drag, x, y);
}

static void
chrome_sdl_set_drag_region(void* user, struct ToriRSChromeDragRegion const* region)
{
    struct ChromeSdl* s = user;

    assert(s);
    assert(region);
    s->drag = *region;
}

/** The wish, with the env var over the top of it -- the precedence
 *  TORIRS_CHROME_EXECUTOR and TORIRS_CHROME_THEME already set. */
static int
chrome_sdl_borderless_wanted(void)
{
    char const* env = getenv("TORIRS_CHROME_BORDERLESS");
    if( env && env[0] )
        return env[0] != '0';
    return g_chrome_sdl_want_borderless != 0;
}

static int
chrome_sdl_begin(void* user)
{
    struct ChromeSdl* s = user;
    int const detached = getenv("TORIRS_CHROME_DETACHED") != NULL;

    assert(s);
    if( !s->platform )
        return 0;
    if( detached )
    {
        if( !PlatformWindow_AuxOpen(s->platform, CHROME_SDL_W, CHROME_SDL_H, "Plugins") )
            return 0;
        s->attached = 0;
    }
    else
    {
        if( !PlatformWindow_ChromeOpen(s->platform, CHROME_SDL_W, CHROME_SDL_H, "Plugins") )
            return 0;
        s->attached = 1;
    }
    s->open = 1;
    s->panel = -1;
    s->close_pending = 0;
    s->borderless = 0;
    /* Whatever the last window was told about is gone with it. A region left
     * standing would be a band of the NEW window swallowing presses over
     * whatever the old one had a strip at. */
    memset(&s->drag, 0, sizeof(s->drag));

    if( !s->attached && chrome_sdl_borderless_wanted() )
    {
        /*
         * The provider goes on before the frame comes off, and the frame is
         * allowed not to come off: PlatformWindow_AuxSetBorderless refuses on a
         * video driver with no hit test, because a frameless window nobody can
         * move is worse than the frame it was asked to hide. The window is
         * usable either way -- what changes is which title bar drags it.
         */
        PlatformWindow_AuxSetDragHandleProvider(s->platform, chrome_sdl_drag_at, s);
        s->borderless = PlatformWindow_AuxSetBorderless(s->platform, true) ? 1 : 0;

        /*
         * Once per ANSWER, not once per open -- the executor comes down with
         * the window, so every show runs this, and a line per open makes a
         * session that toggles the window say the same sentence twenty times.
         * The same rule the executor's own bind line follows.
         *
         * Only the success is said here. A refusal already printed its reason
         * from the platform, which is the layer that knows what the driver
         * said, and repeating it would be two lines for one fact.
         */
        if( s->borderless && !g_chrome_sdl_reported_borderless )
        {
            g_chrome_sdl_reported_borderless = 1;
            fprintf(
                stderr,
                "chrome: plugin window has no OS frame; its title bar and tab strip move it\n");
        }
    }
    return 1;
}

static void
chrome_sdl_end(void* user)
{
    struct ChromeSdl* s = user;

    assert(s);
    if( !s->open )
        return;
    if( s->attached )
        PlatformWindow_ChromeClose(s->platform);
    else
        PlatformWindow_AuxClose(s->platform);
    s->open = 0;
    s->attached = 0;
    s->borderless = 0;
}

static void
chrome_sdl_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    struct ChromeSdl* s = user;

    assert(s);
    assert(cmd);
    /* Almost nothing: a surface executor draws the chrome's own display list,
     * so the widget-level stream has nothing to tell it. See the two kinds of
     * executor in the header.
     *
     * The exception is WHICH PANEL is in the window. A close coming back the
     * other way has to name one -- an intent addresses the model, and "the
     * panel that was showing" is not something the model can infer -- and the
     * only place that fact crosses this seam is PANEL_OPEN. */
    if( cmd->kind == TORIRS_CHROME_CMD_PANEL_OPEN )
        s->panel = cmd->panel;
    else if( cmd->kind == TORIRS_CHROME_CMD_PANEL_CLOSE && cmd->panel == s->panel )
        s->panel = -1;
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

    pixels = s->attached ? PlatformWindow_ChromePixels(s->platform)
                         : PlatformWindow_AuxPixels(s->platform);
    w = s->attached ? PlatformWindow_ChromeWidth(s->platform)
                    : PlatformWindow_AuxWidth(s->platform);
    h = s->attached ? PlatformWindow_ChromeHeight(s->platform)
                    : PlatformWindow_AuxHeight(s->platform);
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
    if( s->attached )
        PlatformWindow_ChromePresent(s->platform);
    else
        PlatformWindow_AuxPresent(s->platform);
}

/*
 * The window's size, which is what makes the chrome fill it.
 *
 * This window holds the panel and nothing else -- no game canvas behind it, no
 * strip beside it -- so the panel is stretched over the whole of it rather than
 * floating at the coordinates it uses in the canvas, where it had something to
 * float over. Answering this is the whole of that opt-in; the rule itself is
 * ToriRSChromeSync_FillSurface's, shared with every other window-owning
 * presentation.
 *
 * Zero while the window is down, so a closed aux window leaves the panel's
 * geometry alone instead of collapsing it to nothing.
 */
static int
chrome_sdl_surface_size(void* user, int* out_w, int* out_h)
{
    struct ChromeSdl* s = user;
    int w;
    int h;

    assert(s);
    assert(out_w);
    assert(out_h);
    if( !s->open )
        return 0;
    w = s->attached ? PlatformWindow_ChromeWidth(s->platform)
                    : PlatformWindow_AuxWidth(s->platform);
    h = s->attached ? PlatformWindow_ChromeHeight(s->platform)
                    : PlatformWindow_AuxHeight(s->platform);
    if( w <= 0 || h <= 0 )
        return 0;
    *out_w = w;
    *out_h = h;
    return 1;
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
_Static_assert((int)PLATFORM_AUX_KEY_UP == (int)TORIRS_CHROME_KEY_UP, "aux key: up");
_Static_assert((int)PLATFORM_AUX_KEY_DOWN == (int)TORIRS_CHROME_KEY_DOWN, "aux key: down");

static int
chrome_sdl_surface_input(void* user, struct ToriRSChromeSurfaceInput* out)
{
    struct ChromeSdl* s = user;
    struct PlatformWindow_AuxInput aux;

    assert(s);
    assert(out);
    if( !s->open )
        return 0;

    /*
     * A close from the window's own title bar drops the OS window at once --
     * continuing to present into it would be drawing into a window that no
     * longer exists -- and is REPORTED, so the model hides the panel and the
     * host learns the window went away.
     *
     * Both halves are needed. Dropping it silently leaves the host convinced
     * the window is still up, and its toggle then spends a press "closing"
     * something the user already closed. Reporting it without dropping it is
     * the GDI rule, which can afford to wait for the model because its window
     * is still there to wait in; this one is not.
     */
    if( !s->attached && PlatformWindow_AuxTakeCloseRequest(s->platform) )
    {
        PlatformWindow_AuxClose(s->platform);
        s->open = 0;
        s->borderless = 0;
        s->close_pending = 1;
        return 0;
    }

    if( !(s->attached ? PlatformWindow_ChromeTakeInput(s->platform, &aux)
                      : PlatformWindow_AuxTakeInput(s->platform, &aux)) )
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
     * size, and the chrome does not care -- it lays out where it was put.
     *
     * The platform reports the new size in PIXELS (the drawable), which is
     * what the surface is measured in, so this is a straight handover -- and
     * it also fires when only the DENSITY changed, which is a window dragged
     * to a display of another kind. */
    if( out->resized )
    {
        if( s->attached )
            PlatformWindow_ChromeResize(s->platform, out->width, out->height);
        else
            PlatformWindow_AuxResize(s->platform, out->width, out->height);
    }
    return 1;
}

/*
 * The window's X, on its way to the model.
 *
 * The only intent this executor has: every other gesture is a pointer in the
 * chrome's own space, which surface_input hands over raw for the chrome to hit
 * test itself. A window closing is the one thing that happens to the
 * PRESENTATION rather than inside it, so it is the one thing that has to be
 * said in the model's own vocabulary.
 */
static int
chrome_sdl_poll(void* user, struct ToriRSChromeIntent* out, int max)
{
    struct ChromeSdl* s = user;

    assert(s);
    assert(out);
    if( max <= 0 || !s->close_pending )
        return 0;
    s->close_pending = 0;
    if( s->panel < 0 )
        return 0;

    memset(out, 0, sizeof(*out));
    out[0].kind = TORIRS_CHROME_INTENT_CLOSE;
    out[0].panel = s->panel;
    out[0].widget = -1;
    s->panel = -1;
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
    exec.poll = chrome_sdl_poll;
    exec.end = chrome_sdl_end;
    exec.present = chrome_sdl_present;
    exec.surface_input = chrome_sdl_surface_input;
    exec.surface_size = chrome_sdl_surface_size;
    /*
     * Offered unconditionally, not only when the frame is off.
     *
     * Whether this window ends up frameless is not known until begin() has
     * asked the video driver, and the entry is what a host looks at to decide
     * whether to publish at all. Answering "no handles" by never being told
     * about them is the same answer as being told an empty region, at the cost
     * of a table entry that changes under the host.
     */
    exec.set_drag_region = chrome_sdl_set_drag_region;
    exec.is_surface = 1;
    return exec;
}

int
ToriRSChromeExecSdl_IsOpen(void)
{
    return g_chrome_sdl.open;
}

void
ToriRSChromeExecSdl_SetBorderless(int borderless)
{
    g_chrome_sdl_want_borderless = borderless ? 1 : 0;
}

int
ToriRSChromeExecSdl_IsBorderless(void)
{
    return g_chrome_sdl.borderless;
}
