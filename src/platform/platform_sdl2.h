#ifndef SRC_PLATFORM_PLATFORM_SDL2_H
#define SRC_PLATFORM_PLATFORM_SDL2_H

#include <stdbool.h>
#include <stdint.h>

struct PlatformSDL2;
struct ToriRS_CmdBus;

struct PlatformSDL2*
PlatformSDL2_New(void);

bool
PlatformSDL2_Init(
    struct PlatformSDL2* platform,
    int width,
    int height,
    char const* title);

/** Create an SDL OpenGL window (no CPU pixel buffer / SDL_Renderer).
 *  macOS: GL 3.2 core + forward-compatible; elsewhere: GL 3.3 core. */
bool
PlatformSDL2_InitForOpenGL3(
    struct PlatformSDL2* platform,
    int width,
    int height,
    char const* title);

struct SDL_Window*
PlatformSDL2_Window(struct PlatformSDL2* platform);

/** Return the platform's native window handle without exposing OS headers.
 *  The SDL-free Win32 backend returns its HWND as a void pointer. */
void*
PlatformSDL2_NativeWindowHandle(struct PlatformSDL2* platform);

/* ---- the auxiliary window -------------------------------------------------
 *
 * ONE extra window, optional, and never a render target: it exists for chrome a
 * user may want beside the game rather than on top of it -- the plugin window,
 * today. The game's own window is untouched, which is what keeps the D3D9
 * contract ("consumes the existing HWND; must not create a second window",
 * docs/platform_quirks.md WINDOWS-HOST-001) true: nothing here ever hosts a
 * renderer.
 *
 * A backend that cannot provide one says so by returning false from Open, and
 * the caller falls back to drawing the same chrome in the game canvas. That is
 * the whole reason this is a small, refusable API rather than a general
 * multi-window layer: exactly one caller wants it, and every platform is
 * allowed to decline.
 *
 * Input from it arrives on the SAME command bus as the game's, tagged with the
 * aux window so the drain can tell them apart. Sharing the bus is deliberate:
 * record/replay, and the headless input simulator, then cover the second window
 * with no machinery of their own.
 */

/**
 * A frame of the aux window's input, in ITS coordinates.
 *
 * The platform's own POD rather than the chrome executor's, because platform/
 * sits below ui/ and must not include it -- the same reason the plugin
 * contract restates key codes instead of including the input header. The
 * executor copies across, and the two are three ints and a string apart.
 */
struct PlatformSDL2_AuxInput
{
    /*
     * The pointer in the SURFACE's pixels, not in SDL's points.
     *
     * The chrome that reads this laid its panels out in the surface, at a scale
     * the display's density chose, so that is the only space these can be in.
     * The conversion happens in the pump, where the window's two sizes are
     * known. @see aux_point_to_pixel.
     */
    int mouse_x;
    int mouse_y;
    int mouse_down;
    int mouse_up;
    int wheel;
    /** Printable bytes typed this frame, NUL-terminated. */
    char text[32];
    /** SDL scancode-derived editing key, or 0. @see PlatformSDL2_AuxEditKey. */
    int edit_key;
    int resized;
    /** The new surface size, in PIXELS -- the drawable, not the points SDL's
     *  own resize event carries. Feed it straight to PlatformSDL2_AuxResize. */
    int width;
    int height;
};

/**
 * Editing keys the aux window reports, spelled here so ui/ and platform/ can
 * agree without either including the other. Values match enum ToriRSChromeKey,
 * which a _Static_assert in the executor pins.
 */
enum PlatformSDL2_AuxEditKey
{
    PLATFORM_AUX_KEY_NONE = 0,
    PLATFORM_AUX_KEY_BACKSPACE,
    PLATFORM_AUX_KEY_DELETE,
    PLATFORM_AUX_KEY_LEFT,
    PLATFORM_AUX_KEY_RIGHT,
    PLATFORM_AUX_KEY_HOME,
    PLATFORM_AUX_KEY_END,
    PLATFORM_AUX_KEY_ENTER,
    PLATFORM_AUX_KEY_ESCAPE
};

/**
 * Take the aux window's accumulated gesture. @return true when there was one.
 *
 * Coalesced by the pump rather than queued: a settings form cares where the
 * pointer ended up and whether a button went down, not about the path it took.
 * Draining clears the EDGES (press, release, wheel, typed text) but keeps the
 * position, because a pointer that stopped moving is still where it was.
 */
bool
PlatformSDL2_AuxTakeInput(struct PlatformSDL2* platform, struct PlatformSDL2_AuxInput* out);

/**
 * Open the aux window at `width` x `height` POINTS.
 *
 * Points, because the size asked for is a physical one -- how big the window
 * should be on a desk. Its SURFACE comes up at the drawable, which on a
 * HighDPI display is a multiple of that; ask PlatformSDL2_AuxWidth/Height for
 * the size anything drawing into it must use.
 *
 * @return false when this backend has none.
 */
bool
PlatformSDL2_AuxOpen(struct PlatformSDL2* platform, int width, int height, char const* title);

/** Close it. Safe when it was never opened. */
void
PlatformSDL2_AuxClose(struct PlatformSDL2* platform);

/** Is it up? */
bool
PlatformSDL2_AuxIsOpen(struct PlatformSDL2 const* platform);

/** Its ARGB staging buffer, or NULL when closed. Width * height ints. */
int*
PlatformSDL2_AuxPixels(struct PlatformSDL2* platform);

/** The SURFACE's size, in pixels -- the space its contents are laid out and
 *  rasterised in, and the space every gesture above is reported in. Not the
 *  window's size in points, which on a HighDPI display is smaller. */
int
PlatformSDL2_AuxWidth(struct PlatformSDL2 const* platform);
int
PlatformSDL2_AuxHeight(struct PlatformSDL2 const* platform);

/** Resize the aux surface, in PIXELS. @return false when it could not be
 *  resized. */
bool
PlatformSDL2_AuxResize(struct PlatformSDL2* platform, int width, int height);

/** Push the staging buffer to the aux window. */
void
PlatformSDL2_AuxPresent(struct PlatformSDL2* platform);

/**
 * Did the user close the aux window since the last ask? Clears the flag.
 *
 * A latch rather than an event, because the one thing a caller does with it is
 * take its own chrome down -- and a close that arrived on a frame nobody asked
 * would otherwise be lost, leaving a window the OS has destroyed still being
 * drawn into.
 */
bool
PlatformSDL2_AuxTakeCloseRequest(struct PlatformSDL2* platform);

/* ---- borderless windows, dragged by what is drawn in them -----------------
 *
 * Taking the OS frame off a window takes four things with it: the title bar
 * that moved it, the border that resized it, the buttons that minimised and
 * closed it, and the double-click that zoomed it. A window that hides its frame
 * has to answer for all of them, and the one this API covers is the first two:
 * the WM is told, per point, whether that pixel moves the window or resizes it
 * from an edge.
 *
 * Answering is a callback rather than a rectangle handed over once, because the
 * chrome that provides the handle is laid out every frame and moves whenever
 * the window is resized or a panel rebuilt. Answering is a callback into
 * PUBLISHED GEOMETRY rather than into a live model, because of when it runs:
 * SDL asks while it is deciding what a mouse press even is, from inside the
 * event pump, so the provider must be a cheap point test against a snapshot and
 * must not walk anything the frame thread mutates.
 *
 * The cost of a draggable region is that it SWALLOWS the press that begins the
 * drag -- the application is never told about a mouse-down there. Whatever
 * draws the handle therefore has to exclude every control inside it, or those
 * controls silently stop being clickable. @see ToriRSChrome_WindowDragRegion,
 * which is the one implementation of that rule in this tree.
 */

/**
 * Does this point drag the window? Asked in the window's own CONTENT
 * coordinates -- canvas pixels for the game window (the letterbox is already
 * undone), surface pixels for the aux one.
 *
 * @return non-zero to move the window, zero to let the press through.
 */
typedef int (*PlatformSDL2_DragHandleFn)(void* user, int x, int y);

/**
 * Where the game window may be grabbed. NULL clears it, which is a legitimate
 * state: a borderless window whose chrome has no handle this frame is dragged
 * from nowhere but its resize edges.
 */
void
PlatformSDL2_SetDragHandleProvider(
    struct PlatformSDL2* platform, PlatformSDL2_DragHandleFn fn, void* user);

/** The same, for the aux window. Survives the window itself being closed and
 *  reopened, so a caller sets it once. */
void
PlatformSDL2_AuxSetDragHandleProvider(
    struct PlatformSDL2* platform, PlatformSDL2_DragHandleFn fn, void* user);

/**
 * Take the OS frame off the game window, or give it back.
 * @return true when the window ended up in the state asked for.
 *
 * REFUSES to go borderless on a video driver with no hit test, and says so on
 * stderr. Every way a user has of moving or resizing a window goes through
 * either the frame or the hit test; a driver with neither leaves a window
 * pinned where it opened, at the size it opened, for the rest of the session --
 * which is a worse answer than the frame it was asked to hide.
 */
bool
PlatformSDL2_SetBorderless(struct PlatformSDL2* platform, bool borderless);

/** The same, for the aux window. Call it after PlatformSDL2_AuxOpen: the wish
 *  is not remembered across the window it applies to. */
bool
PlatformSDL2_AuxSetBorderless(struct PlatformSDL2* platform, bool borderless);

/** Is the frame currently off? */
bool
PlatformSDL2_IsBorderless(struct PlatformSDL2 const* platform);

bool
PlatformSDL2_AuxIsBorderless(struct PlatformSDL2 const* platform);

void
PlatformSDL2_Free(struct PlatformSDL2* platform);

int*
PlatformSDL2_Pixels(struct PlatformSDL2* platform);

int
PlatformSDL2_Width(struct PlatformSDL2* platform);

int
PlatformSDL2_Height(struct PlatformSDL2* platform);

/**
 * Drawable pixels per window point: 1 on an ordinary display, 2 on a Retina or
 * 200%-scaled one.
 *
 * The framebuffer is already sized in drawable pixels, so nothing multiplies
 * by this to draw. It exists for the chrome, which has to pick which BAKED
 * font size to lay itself out with -- a UI authored for 1x pixels, drawn into
 * a 2x framebuffer, is half the physical size it should be, and the fix is a
 * bigger authored font rather than a stretch.
 */
int
PlatformSDL2_PixelDensity(struct PlatformSDL2* platform);

/**
 * Ask the next window for a device-pixel (HighDPI) drawable.
 *
 * Must be called BEFORE PlatformSDL2_Init / _InitForOpenGL3: the flag becomes
 * SDL_WINDOW_ALLOW_HIGHDPI at creation and SDL has no way to add it after.
 * `[ui:boot] hidpi=` is what drives it; TORIRS_HIDPI overrides either way.
 */
void
PlatformSDL2_SetWantHighDPI(bool want);

bool
PlatformSDL2_QuitRequested(struct PlatformSDL2* platform);

void
PlatformSDL2_SetTitle(
    struct PlatformSDL2* platform,
    char const* title);

/**
 * Resizable mode: make the logical framebuffer track the window instead of
 * letterboxing a fixed one into it. While set, every window size change emits
 * TORIRS_CMD_WINDOW_RESIZE so the client relayouts; while clear, the window
 * only scales what is already drawn. Turning it ON pushes one resize command
 * for the current window size, so the caller does not have to wait for the user
 * to drag something.
 *
 * `min_w`/`min_h` are the client's canvas floor. They become the window's
 * minimum size in BOTH modes, because a window smaller than the floor is the
 * one case the client cannot answer with layout — the canvas clamps and the
 * present scales it down. Below the floor, "resize" is not available at any
 * layer, so the window is not allowed there.
 *
 * Turning it OFF also snaps the window back to exactly `min_w x min_h`: fixed
 * mode *is* that frame, and leaving a larger window behind would present it
 * upscaled, which is the behaviour the mode switch exists to leave. The size it
 * snapped away from is remembered and restored when it is turned back ON, so
 * resizable -> fixed -> resizable returns the user to the window they had
 * instead of leaving them at the floor.
 *
 * This does NOT resize the backbuffer. The client clamps the canvas to a floor
 * it owns, so the canvas — not the window — is what the backbuffer must match;
 * the caller reconciles them with PlatformSDL2_Resize after draining the bus.
 */
void
PlatformSDL2_SetCanvasFollowsWindow(
    struct PlatformSDL2* platform,
    struct ToriRS_CmdBus* bus,
    bool follow,
    int min_w,
    int min_h);

/**
 * Resize the OS window, as if the user had dragged its corner.
 *
 * The distinction from PlatformSDL2_Resize matters: this touches the window and
 * nothing else, so what happens next is decided by the follow gate exactly as it
 * would be for a real drag. It is the only way to exercise that gate headlessly
 * — pushing TORIRS_CMD_WINDOW_RESIZE straight onto the bus skips it.
 */
void
PlatformSDL2_SetWindowSize(
    struct PlatformSDL2* platform,
    int width,
    int height);

/**
 * Resize the logical framebuffer (pixels + streaming texture) in place. No-op
 * when the size is unchanged or the platform is in GL mode (GL draws straight
 * to the window and owns no CPU buffer). Returns true when the size changed.
 */
bool
PlatformSDL2_Resize(
    struct PlatformSDL2* platform,
    int width,
    int height);

/** Select how the logical interface framebuffer is sampled when it is scaled
 *  into the window: 0 nearest-neighbour, 1 linear, 2 best/bicubic. Backends
 *  use their closest supported high-quality filter for mode 2. */
void
PlatformSDL2_SetInterfaceScaleMode(
    struct PlatformSDL2* platform,
    int mode);

/** Map window-pixel mouse coords into the letterboxed logical framebuffer. */
void
PlatformSDL2_MapMouse(
    struct PlatformSDL2* platform,
    int win_x,
    int win_y,
    int* out_x,
    int* out_y);

/**
 * Poll SDL events into the command bus as TORIRS_CMD_INPUT_* commands (mouse
 * coords already mapped to logical space, so recordings are window-size
 * independent). Window-level events (quit, esc-quit) stay platform state.
 */
void
PlatformSDL2_PollCommands(
    struct PlatformSDL2* platform,
    struct ToriRS_CmdBus* bus);

void
PlatformSDL2_Present(struct PlatformSDL2* platform);

/** Swap the GL backbuffer. Only valid after InitForOpenGL3. */
void
PlatformSDL2_PresentGL(struct PlatformSDL2* platform);

uint64_t
PlatformSDL2_Ticks64(void);

/**
 * The same monotonic clock at microsecond resolution.
 *
 * Millisecond ticks are what the 20 ms frame budget is paced against, but they
 * are too coarse to measure a frame *with*: a few-millisecond frame quantises
 * to a couple of integers, and the quantisation survives averaging.
 */
uint64_t
PlatformSDL2_TicksUs(void);

/** Wait until an absolute PlatformSDL2_Ticks64() deadline. */
void
PlatformSDL2_SleepUntil(uint64_t deadline_ms);

#endif
