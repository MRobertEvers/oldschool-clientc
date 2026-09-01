/*
 * platform_android.c -- Android + ANativeWindow backend for the src/main.c
 * App front-end. Raw ANativeWindow and EGL; no windowing library.
 *
 * src/main.c programs to the PlatformWindow interface (platform_window.h). When
 * built without a GPU renderer -- or with one that was not asked for -- it
 * takes the software path:
 *
 *     App_Render(app, PlatformWindow_Pixels(p), W, H);   // CPU raster into pixels
 *     PlatformWindow_Present(p);                          // show the pixels
 *
 * This file is a drop-in implementation of those same PlatformWindow_* symbols
 * backed by ANativeWindow. It is the exact counterpart of platform_win32gdi.c,
 * which does the same job for raw Win32 + GDI, and the two are deliberately
 * shaped alike: hand out a 32bpp top-down ARGB buffer as the canvas, then
 * letterbox-blit it to the window.
 *
 * WHAT THIS FILE DOES NOT KNOW
 *
 * Java. Every jobject, JNIEnv and thread lives in platform_android_jni.c, and
 * the two meet at platform_android.h -- a handful of functions that move a
 * value across a mutex. That split is what lets the drawing code here be read
 * and reasoned about as ordinary C, and what keeps the JNI file free of any
 * opinion about how a frame is composed.
 *
 * THE ONE PIXEL-FORMAT SUBTLETY
 *
 * App_Render writes ARGB8888 -- on a little-endian machine, bytes B,G,R,A.
 * ANativeWindow's 32-bit formats are byte-order R,G,B,A. So the present pass
 * swaps R and B. It is not free, but it rides along inside a copy that is
 * already memory-bound, and the alternative -- building the whole client at
 * TORIDRAW_PF_ABGR8888 -- would change the format that every sprite, font and
 * texture is composed in, on the one lane least able to absorb a subtle
 * divergence. @see swizzle_argb_to_rgba.
 */

#include "platform/platform_window.h"
#include "platform/platform_android.h"

#include "cmd/cmdbus.h"
#include "input/torirs_input.h"
#include "input/torirs_keymap.h"
#include "input/torirs_touch.h"

#include <android/log.h>
#include <android/native_window.h>

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#define ANDROID_LOG_TAG "torirs"

/* ---- android.view.KeyEvent constants -------------------------------------
 *
 * Restated rather than included, for the same reason platform_win32gdi.c
 * restates the WM_TOUCH structures: these are stable public API integers with
 * no C header anywhere in the NDK. Only the keys this client actually binds are
 * listed -- the rest arrive, map to nothing, and are dropped.
 */
#define AKEY_0 7
#define AKEY_9 16
#define AKEY_A 29
#define AKEY_Z 54
#define AKEY_COMMA 55
#define AKEY_TAB 61
#define AKEY_SPACE 62
#define AKEY_ENTER 66
#define AKEY_DEL 67 /* KEYCODE_DEL is BACKSPACE; KEYCODE_FORWARD_DEL is delete */
#define AKEY_FORWARD_DEL 112
#define AKEY_SHIFT_LEFT 59
#define AKEY_SHIFT_RIGHT 60
#define AKEY_ALT_LEFT 57
#define AKEY_ALT_RIGHT 58
#define AKEY_CTRL_LEFT 113
#define AKEY_CTRL_RIGHT 114
#define AKEY_ESCAPE 111
#define AKEY_BACK 4
#define AKEY_DPAD_UP 19
#define AKEY_DPAD_DOWN 20
#define AKEY_DPAD_LEFT 21
#define AKEY_DPAD_RIGHT 22
#define AKEY_PAGE_UP 92
#define AKEY_PAGE_DOWN 93
#define AKEY_INSERT 124
#define AKEY_F1 131
#define AKEY_F12 142

/* ---- the shared state, and the one lock over it --------------------------
 *
 * Everything here is written by Android's main (UI) thread and read by the
 * frame thread, or the other way round. The lock is held only long enough to
 * move a value: never across a blit, never across a frame, and never while
 * calling anything that could re-enter.
 */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_window_arrived = PTHREAD_COND_INITIALIZER;

static ANativeWindow* g_window;
static int g_window_w;
static int g_window_h;
static int g_density = 1;
/** Surface rows the soft keyboard covers at the bottom; 0 = away.
 *  @see PlatformAndroid_SetKeyboardInset. */
static int g_keyboard_inset_px;
static int g_quit;

/* One queued input record. @see PlatformAndroid_PostTouch / _PostKey. */
enum android_event_kind
{
    ANDROID_EVENT_TOUCH = 0,
    ANDROID_EVENT_KEY
};

struct android_event
{
    int kind;
    /* touch */
    int action;
    int32_t pointer_id;
    int x;
    int y;
    /* key */
    int keycode;
    int down;
    int unicode;
};

/*
 * A ring, sized so that a slow frame cannot make the UI thread block or a
 * gesture tear. 256 records is several seconds of a finger moving at the
 * digitiser's full rate; overflowing it means the frame loop has stopped, and
 * the useful thing to do then is drop MOVEs, not stall the UI thread.
 */
#define ANDROID_EVENT_MAX 256
static struct android_event g_events[ANDROID_EVENT_MAX];
static int g_event_head;
static int g_event_tail;

static void
event_push(struct android_event const* ev)
{
    int next;

    pthread_mutex_lock(&g_lock);
    next = (g_event_head + 1) % ANDROID_EVENT_MAX;
    if( next != g_event_tail )
    {
        g_events[g_event_head] = *ev;
        g_event_head = next;
    }
    pthread_mutex_unlock(&g_lock);
}

/** @return 1 when one was taken. */
static int
event_pop(struct android_event* out)
{
    int got = 0;

    pthread_mutex_lock(&g_lock);
    if( g_event_tail != g_event_head )
    {
        *out = g_events[g_event_tail];
        g_event_tail = (g_event_tail + 1) % ANDROID_EVENT_MAX;
        got = 1;
    }
    pthread_mutex_unlock(&g_lock);
    return got;
}

/* ---- platform_android.h: the UI thread's side ---------------------------- */

void
PlatformAndroid_SetWindow(ANativeWindow* window, int width, int height)
{
    pthread_mutex_lock(&g_lock);
    /*
     * The reference is the JNI half's to hold and release -- it acquired the
     * window from the Surface and it is the only thing that knows when Android
     * has taken it back. This side only ever borrows the pointer, under the
     * lock, for the length of one present.
     */
    g_window = window;
    g_window_w = window ? width : 0;
    g_window_h = window ? height : 0;
    if( window )
        pthread_cond_broadcast(&g_window_arrived);
    pthread_mutex_unlock(&g_lock);
}

int
PlatformAndroid_AwaitWindow(void)
{
    int have;

    pthread_mutex_lock(&g_lock);
    while( !g_window && !g_quit )
        pthread_cond_wait(&g_window_arrived, &g_lock);
    have = g_window != NULL;
    pthread_mutex_unlock(&g_lock);
    return have;
}

ANativeWindow*
PlatformAndroid_Window(void)
{
    ANativeWindow* w;

    pthread_mutex_lock(&g_lock);
    w = g_window;
    pthread_mutex_unlock(&g_lock);
    return w;
}

void
PlatformAndroid_WindowSize(int* out_width, int* out_height)
{
    assert(out_width);
    assert(out_height);

    pthread_mutex_lock(&g_lock);
    *out_width = g_window_w;
    *out_height = g_window_h;
    pthread_mutex_unlock(&g_lock);
}

void
PlatformAndroid_SetDensity(int density)
{
    if( density < 1 )
        density = 1;
    if( density > 4 )
        density = 4;
    pthread_mutex_lock(&g_lock);
    g_density = density;
    pthread_mutex_unlock(&g_lock);
}

void
PlatformAndroid_SetKeyboardInset(int bottom_px)
{
    if( bottom_px < 0 )
        bottom_px = 0;
    pthread_mutex_lock(&g_lock);
    g_keyboard_inset_px = bottom_px;
    pthread_mutex_unlock(&g_lock);
}

int
PlatformAndroid_KeyboardInset(void)
{
    int px;

    pthread_mutex_lock(&g_lock);
    px = g_keyboard_inset_px;
    pthread_mutex_unlock(&g_lock);
    return px;
}

void
PlatformAndroid_RequestQuit(void)
{
    pthread_mutex_lock(&g_lock);
    g_quit = 1;
    /* A thread parked in AwaitWindow must come out, or onDestroy waits forever
     * for a loop that never started. */
    pthread_cond_broadcast(&g_window_arrived);
    pthread_mutex_unlock(&g_lock);
}

int
PlatformAndroid_QuitRequested(void)
{
    int q;

    pthread_mutex_lock(&g_lock);
    q = g_quit;
    pthread_mutex_unlock(&g_lock);
    return q;
}

void
PlatformAndroid_ResetForStart(void)
{
    pthread_mutex_lock(&g_lock);
    g_quit = 0;
    /* Drop whatever the previous run left queued: a finger that went down then
     * must not land as a click now. */
    g_event_head = 0;
    g_event_tail = 0;
    /* And a keyboard the previous run left up: the new run has not asked for
     * one, and a stale inset would boot the frame with its chat mid-air. */
    g_keyboard_inset_px = 0;
    pthread_mutex_unlock(&g_lock);
}

void
PlatformAndroid_PostTouch(int action, int32_t pointer_id, int x, int y)
{
    struct android_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = ANDROID_EVENT_TOUCH;
    ev.action = action;
    ev.pointer_id = pointer_id;
    ev.x = x;
    ev.y = y;
    event_push(&ev);
}

void
PlatformAndroid_PostKey(int android_keycode, int down, int unicode)
{
    struct android_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = ANDROID_EVENT_KEY;
    ev.keycode = android_keycode;
    ev.down = down;
    ev.unicode = unicode;
    event_push(&ev);
}

/* ---- key translation -----------------------------------------------------
 *
 * Two spaces, both reached from the one Android keycode:
 *
 *   enum LibToriRS_KeyCode   the platform-neutral edge/held API the client's
 *                            own input layer uses.
 *   OSRS internal code       the script ABI CS2 handlers see, reached through
 *                            the VK table in input/torirs_keymap.c so that this
 *                            lane shares the reference mapping rather than
 *                            restating it.
 */
static enum LibToriRS_KeyCode
android_keycode_to_torirsk(int keycode)
{
    if( keycode >= AKEY_A && keycode <= AKEY_Z )
        return (enum LibToriRS_KeyCode)(TORIRSK_A + (keycode - AKEY_A));
    if( keycode >= AKEY_0 && keycode <= AKEY_9 )
        return (enum LibToriRS_KeyCode)(TORIRSK_0 + (keycode - AKEY_0));

    switch( keycode )
    {
    case AKEY_ESCAPE:
    /* The hardware/gesture Back key is this lane's Escape. A phone has no
     * Escape key, and Back is the gesture that means the same thing to a user:
     * close what is open. The activity does not consume it, so the client's own
     * "close the top interface" handling is what runs. */
    case AKEY_BACK:
        return TORIRSK_ESCAPE;
    case AKEY_ENTER:
        return TORIRSK_RETURN;
    case AKEY_DEL:
        return TORIRSK_BACKSPACE;
    case AKEY_FORWARD_DEL:
        return TORIRSK_DELETE;
    case AKEY_INSERT:
        return TORIRSK_INSERT;
    case AKEY_SHIFT_LEFT:
    case AKEY_SHIFT_RIGHT:
        return TORIRSK_SHIFT;
    case AKEY_CTRL_LEFT:
    case AKEY_CTRL_RIGHT:
        return TORIRSK_CTRL;
    case AKEY_TAB:
        return TORIRSK_TAB;
    case AKEY_SPACE:
        return TORIRSK_SPACE;
    case AKEY_DPAD_LEFT:
        return TORIRSK_LEFT;
    case AKEY_DPAD_RIGHT:
        return TORIRSK_RIGHT;
    case AKEY_DPAD_UP:
        return TORIRSK_UP;
    case AKEY_DPAD_DOWN:
        return TORIRSK_DOWN;
    case AKEY_PAGE_UP:
        return TORIRSK_PAGE_UP;
    case AKEY_PAGE_DOWN:
        return TORIRSK_PAGE_DOWN;
    case AKEY_COMMA:
        return TORIRSK_COMMA;
    default:
        return TORIRSK_UNKNOWN;
    }
}

/** Java KeyEvent.VK_* for an Android keycode, or -1. The VK space is what
 *  input/torirs_keymap.c indexes its OSRS table by. */
static int
android_keycode_to_vk(int keycode)
{
    if( keycode >= AKEY_A && keycode <= AKEY_Z )
        return 'A' + (keycode - AKEY_A);
    if( keycode >= AKEY_0 && keycode <= AKEY_9 )
        return '0' + (keycode - AKEY_0);
    if( keycode >= AKEY_F1 && keycode <= AKEY_F12 )
        return 112 + (keycode - AKEY_F1); /* VK_F1 == 112 */

    switch( keycode )
    {
    case AKEY_DEL:
        return TORIRS_VK_BACKSPACE;
    case AKEY_TAB:
        return TORIRS_VK_TAB;
    case AKEY_ENTER:
        return TORIRS_VK_ENTER;
    case AKEY_SHIFT_LEFT:
    case AKEY_SHIFT_RIGHT:
        return TORIRS_VK_SHIFT;
    case AKEY_CTRL_LEFT:
    case AKEY_CTRL_RIGHT:
        return TORIRS_VK_CTRL;
    case AKEY_ALT_LEFT:
    case AKEY_ALT_RIGHT:
        return TORIRS_VK_ALT;
    case AKEY_ESCAPE:
    case AKEY_BACK:
        return TORIRS_VK_ESCAPE;
    case AKEY_SPACE:
        return TORIRS_VK_SPACE;
    case AKEY_FORWARD_DEL:
        return TORIRS_VK_DELETE;
    case AKEY_DPAD_LEFT:
        return 37; /* VK_LEFT */
    case AKEY_DPAD_UP:
        return 38;
    case AKEY_DPAD_RIGHT:
        return 39;
    case AKEY_DPAD_DOWN:
        return 40;
    case AKEY_PAGE_UP:
        return 33;
    case AKEY_PAGE_DOWN:
        return 34;
    case AKEY_COMMA:
        return 188; /* VK_COMMA */
    default:
        return -1;
    }
}

/* ---- the platform object ------------------------------------------------- */

struct PlatformWindow
{
    /** The canvas: width * height ARGB8888 pixels, owned here. */
    int* pixels;
    int width;
    int height;

    /** Set by InitForOpenGL3. In GL mode there is no CPU canvas and Present is
     *  a no-op; the renderer draws into the EGL surface and PresentGL swaps. */
    int gl_mode;

    int quit;
    int interface_scale_mode;

    int canvas_follows_window;
    /** The surface size seen by the last poll, so a change can be noticed once
     *  rather than pushed every frame. */
    int last_seen_w;
    int last_seen_h;
    /** The keyboard inset (CANVAS rows) the last poll pushed, so the command
     *  goes out once per change rather than every frame. */
    int last_keyboard_inset;

    int present_dmg_x;
    int present_dmg_y;
    int present_dmg_w;
    int present_dmg_h;
    int present_dmg_rects[PLATFORM_PRESENT_DAMAGE_RECT_MAX][4];
    int present_dmg_rect_count;

    /** Fingers. @see ToriRS_Touch, which holds the gesture policy this backend
     *  shares with every other one. */
    struct ToriRS_Touch touch;
};

/* ---- letterbox math (the same box every PlatformWindow backend computes) -- */

struct android_rect
{
    int x;
    int y;
    int w;
    int h;
};

static void
letterbox_dst(int logical_w, int logical_h, int win_w, int win_h, struct android_rect* dst)
{
    float src_aspect;
    float win_aspect;

    dst->x = 0;
    dst->y = 0;
    dst->w = logical_w;
    dst->h = logical_h;
    if( win_w <= 0 || win_h <= 0 )
        return;

    src_aspect = (float)logical_w / (float)logical_h;
    win_aspect = (float)win_w / (float)win_h;
    if( src_aspect > win_aspect )
    {
        int const h = (int)((float)win_w / src_aspect);
        dst->x = 0;
        dst->y = (win_h - h) / 2;
        dst->w = win_w;
        dst->h = h;
    }
    else
    {
        int const w = (int)((float)win_h * src_aspect);
        dst->x = (win_w - w) / 2;
        dst->y = 0;
        dst->w = w;
        dst->h = win_h;
    }
}

static void
map_surface_to_canvas(struct PlatformWindow* p, int win_x, int win_y, int* out_x, int* out_y)
{
    struct android_rect box;
    int win_w;
    int win_h;
    int x;
    int y;

    PlatformAndroid_WindowSize(&win_w, &win_h);
    if( win_w <= 0 || win_h <= 0 )
    {
        *out_x = win_x;
        *out_y = win_y;
        return;
    }
    letterbox_dst(p->width, p->height, win_w, win_h, &box);
    if( box.w <= 0 || box.h <= 0 )
    {
        *out_x = 0;
        *out_y = 0;
        return;
    }
    x = (win_x - box.x) * p->width / box.w;
    y = (win_y - box.y) * p->height / box.h;
    if( x < 0 )
        x = 0;
    else if( x >= p->width )
        x = p->width - 1;
    if( y < 0 )
        y = 0;
    else if( y >= p->height )
        y = p->height - 1;
    *out_x = x;
    *out_y = y;
}

/*
 * The soft keyboard's coverage, mapped from surface rows into canvas rows
 * through the same letterbox the touch mapping uses. The IME is a band across
 * the surface's bottom, so only the vertical mapping matters; the part of the
 * band lying on the letterbox bar (below the canvas) maps to zero.
 */
static int
keyboard_canvas_inset(struct PlatformWindow* p, int inset_px, int win_w, int win_h)
{
    struct android_rect box;
    int visible_rows;

    if( inset_px <= 0 || win_w <= 0 || win_h <= 0 || p->height <= 0 )
        return 0;
    letterbox_dst(p->width, p->height, win_w, win_h, &box);
    if( box.h <= 0 )
        return 0;
    /* The topmost covered surface row, as a canvas row. */
    visible_rows = (win_h - inset_px - box.y) * p->height / box.h;
    if( visible_rows < 0 )
        visible_rows = 0;
    if( visible_rows > p->height )
        visible_rows = p->height;
    return p->height - visible_rows;
}

/* ---- the blit -----------------------------------------------------------
 *
 * ARGB8888 (bytes B,G,R,A) to the ANativeWindow's RGBA_8888 (bytes R,G,B,A):
 * R and B trade places, A and G stay. Written as one expression so the scalar
 * path is three instructions, and given a NEON twin because on the armv7
 * devices this lane exists for, the present is a real fraction of the frame.
 */
static inline uint32_t
swizzle_argb_to_rgba(uint32_t argb)
{
    return (argb & 0xFF00FF00u) | ((argb >> 16) & 0x000000FFu) | ((argb & 0x000000FFu) << 16);
}

static void
swizzle_row(uint32_t* dst, uint32_t const* src, int count)
{
    int i = 0;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    /*
     * vld4/vst4 de-interleave the four channels into separate registers and
     * re-interleave them on the way out, so the swap is expressed by storing
     * the planes in a different order and costs no shuffle instruction at all.
     * Eight pixels per iteration.
     */
    for( ; i + 8 <= count; i += 8 )
    {
        uint8x8x4_t px = vld4_u8((uint8_t const*)(src + i));
        uint8x8x4_t out;
        out.val[0] = px.val[2]; /* R <- the byte that held B's position */
        out.val[1] = px.val[1];
        out.val[2] = px.val[0];
        out.val[3] = px.val[3];
        vst4_u8((uint8_t*)(dst + i), out);
    }
#endif
    for( ; i < count; i++ )
        dst[i] = swizzle_argb_to_rgba(src[i]);
}

/**
 * Copy the canvas into the locked window buffer, letterboxed and scaled.
 *
 * `stride` is in PIXELS, which is what ANativeWindow_Buffer reports and which
 * is very often larger than the width -- graphics buffers are aligned, and
 * treating stride as width is the classic way to get a sheared image on one
 * device and a correct one on another.
 */
static void
present_blit(struct PlatformWindow* p, uint32_t* dst, int stride, int win_w, int win_h)
{
    struct android_rect box;
    int y;

    letterbox_dst(p->width, p->height, win_w, win_h, &box);
    if( box.w <= 0 || box.h <= 0 )
        return;

    /*
     * The bars. Cleared every present rather than once, because the buffer that
     * comes back from ANativeWindow_lock is one of a rotating set and may hold
     * any previous frame -- there is nothing here that persists the way a GDI
     * window's contents do.
     */
    if( box.y > 0 || box.h < win_h || box.x > 0 || box.w < win_w )
    {
        for( y = 0; y < win_h; y++ )
            memset(dst + (size_t)y * (size_t)stride, 0, (size_t)win_w * sizeof(uint32_t));
    }

    if( box.w == p->width && box.h == p->height )
    {
        /* 1:1 -- the canvas already matches the surface, which is what the
         * resizable mode arranges and what a phone normally ends up in. */
        for( y = 0; y < box.h; y++ )
            swizzle_row(
                dst + (size_t)(box.y + y) * (size_t)stride + (size_t)box.x,
                (uint32_t const*)p->pixels + (size_t)y * (size_t)p->width,
                p->width);
        return;
    }

    /*
     * Nearest-neighbour, in fixed point. The source step is computed once per
     * axis; a per-pixel divide here would be the most expensive thing in the
     * frame on the hardware this lane targets.
     */
    {
        uint32_t const x_step = ((uint32_t)p->width << 16) / (uint32_t)box.w;
        uint32_t const y_step = ((uint32_t)p->height << 16) / (uint32_t)box.h;
        uint32_t src_y = 0;

        for( y = 0; y < box.h; y++, src_y += y_step )
        {
            uint32_t const* src_row =
                (uint32_t const*)p->pixels + (size_t)(src_y >> 16) * (size_t)p->width;
            uint32_t* dst_row = dst + (size_t)(box.y + y) * (size_t)stride + (size_t)box.x;
            uint32_t src_x = 0;
            int x;

            for( x = 0; x < box.w; x++, src_x += x_step )
                dst_row[x] = swizzle_argb_to_rgba(src_row[src_x >> 16]);
        }
    }
}

/* ---- lifecycle ----------------------------------------------------------- */

struct PlatformWindow*
PlatformWindow_New(void)
{
    struct PlatformWindow* p = (struct PlatformWindow*)malloc(sizeof(*p));
    assert(p);
    memset(p, 0, sizeof(*p));
    p->interface_scale_mode = 2;
    p->last_seen_w = -1;
    p->last_seen_h = -1;
    ToriRS_TouchReset(&p->touch);
    return p;
}

/** Allocate (or re-allocate) the ARGB canvas. */
static int
android_make_canvas(struct PlatformWindow* p, int width, int height)
{
    int* pixels;

    assert(width > 0);
    assert(height > 0);

    pixels = (int*)calloc((size_t)width * (size_t)height, sizeof(int));
    assert(pixels);
    free(p->pixels);
    p->pixels = pixels;
    p->width = width;
    p->height = height;
    return 1;
}

bool
PlatformWindow_Init(struct PlatformWindow* p, int width, int height, char const* title)
{
    assert(p);
    assert(width > 0);
    assert(height > 0);
    (void)title; /* an Android activity's label is set in the manifest */

    /*
     * Wait for the Surface before doing anything else. Nothing above this
     * knows a window can be absent, and on Android one always is for the first
     * moments of an activity's life -- the SurfaceView has not been measured
     * yet. Blocking here, once, is what keeps that fact out of main.c.
     */
    if( !PlatformAndroid_AwaitWindow() )
        return false;

    if( !android_make_canvas(p, width, height) )
        return false;

    __android_log_print(
        ANDROID_LOG_INFO, ANDROID_LOG_TAG, "software canvas %dx%d", width, height);
    return true;
}

bool
PlatformWindow_InitForOpenGL3(struct PlatformWindow* p, int width, int height, char const* title)
{
    assert(p);
    assert(width > 0);
    assert(height > 0);
    (void)title;

    if( !PlatformAndroid_AwaitWindow() )
        return false;

    /*
     * No CPU canvas in GL mode: the renderer draws into the EGL surface. The
     * logical size is still recorded, because it is the space every mouse and
     * touch coordinate is reported in and the space the client lays out in --
     * the GL path scales it to the drawable itself.
     */
    p->gl_mode = 1;
    p->width = width;
    p->height = height;

    /*
     * The context itself is created by the renderer's ToriRS_GLES2_Init,
     * through the platform_gl_context.h seam -- which on this lane is
     * platform_android_gl.c (EGL). It is not created here because the
     * renderer decides the attributes it needs (depth, above all), and it has
     * not been constructed yet.
     */
    __android_log_print(
        ANDROID_LOG_INFO, ANDROID_LOG_TAG, "GLES2 canvas %dx%d", width, height);
    return true;
}

ToriRS_GLWindow*
PlatformWindow_GLWindow(struct PlatformWindow* p)
{
    assert(p);
    /*
     * A token, not a pointer to anything the caller may read. The GLES2
     * renderer never dereferences the handle -- it only passes it back into
     * platform_gl_context.h, whose Android implementation resolves the single
     * Surface for itself. Non-NULL is the honest answer here: NULL would read
     * as "this platform has no window" to a caller checking for one.
     */
    return (ToriRS_GLWindow*)p;
}

void*
PlatformWindow_NativeWindowHandle(struct PlatformWindow* p)
{
    assert(p);
    return (void*)PlatformAndroid_Window();
}

void
PlatformWindow_Free(struct PlatformWindow* p)
{
    if( !p )
        return;
    free(p->pixels);
    free(p);
}

int*
PlatformWindow_Pixels(struct PlatformWindow* p)
{
    assert(p);
    return p->pixels;
}

int
PlatformWindow_Width(struct PlatformWindow* p)
{
    assert(p);
    return p->width;
}

int
PlatformWindow_Height(struct PlatformWindow* p)
{
    assert(p);
    return p->height;
}

int
PlatformWindow_PixelDensity(struct PlatformWindow* p)
{
    int density;

    assert(p);
    pthread_mutex_lock(&g_lock);
    density = g_density;
    pthread_mutex_unlock(&g_lock);
    return density;
}

void
PlatformWindow_SetWantHighDPI(bool want)
{
    /*
     * Nothing to ask for. An Android Surface is ALWAYS device pixels -- there
     * is no points layer to opt out of the way there is on macOS, so the flag
     * has no counterpart here. Accepted and ignored rather than refused,
     * because `[ui:boot] hidpi=` is a manifest key shared with every other lane
     * and a manifest must not have to know which host is reading it.
     */
    (void)want;
}

bool
PlatformWindow_QuitRequested(struct PlatformWindow* p)
{
    assert(p);
    return p->quit || PlatformAndroid_QuitRequested();
}

void
PlatformWindow_SetTitle(struct PlatformWindow* p, char const* title)
{
    assert(p);
    assert(title);
    /* An activity's label belongs to the system UI, not to the app's surface.
     * Logged so a title the client thought it set is still traceable. */
    __android_log_print(ANDROID_LOG_DEBUG, ANDROID_LOG_TAG, "title: %s", title);
}

void
PlatformWindow_SetTextInput(struct PlatformWindow* p, int on)
{
    assert(p);
    PlatformAndroidJni_SetSoftKeyboard(on);
}

void
PlatformWindow_SetTouchViewport(struct PlatformWindow* p, int x, int y, int w, int h)
{
    assert(p);
    ToriRS_TouchSetViewport(&p->touch, x, y, w, h);
}

void
PlatformWindow_SetInterfaceScaleMode(struct PlatformWindow* p, int mode)
{
    assert(p);
    if( mode < 0 )
        mode = 0;
    if( mode > 2 )
        mode = 2;
    p->interface_scale_mode = mode;
}

void
PlatformWindow_SetCanvasFollowsWindow(
    struct PlatformWindow* p, struct ToriRS_CmdBus* bus, bool follow, int min_w, int min_h)
{
    assert(p);
    (void)min_w;
    (void)min_h;

    p->canvas_follows_window = follow ? 1 : 0;
    /*
     * Turning it on pushes one resize for the size the surface already is, so
     * the client relayouts without waiting for a change that may never come --
     * a phone's surface does not get dragged. There is no "snap back to the
     * floor" arm: an Android window has exactly one size, the display's, and
     * fixed mode simply letterboxes the canvas inside it.
     */
    if( p->canvas_follows_window && bus )
    {
        int win_w;
        int win_h;

        PlatformAndroid_WindowSize(&win_w, &win_h);
        if( win_w > 0 && win_h > 0 )
        {
            CmdBus_PushWindowResize(bus, win_w, win_h);
            p->last_seen_w = win_w;
            p->last_seen_h = win_h;
        }
    }
}

void
PlatformWindow_SetWindowSize(struct PlatformWindow* p, int width, int height)
{
    assert(p);
    (void)width;
    (void)height;
    /*
     * An Android window is the display. There is no corner to drag, so this is
     * a no-op rather than a refusal -- the headless resize harness that drives
     * it on the desktop simply has nothing to exercise here.
     */
}

bool
PlatformWindow_Resize(struct PlatformWindow* p, int width, int height)
{
    assert(p);
    assert(width > 0);
    assert(height > 0);

    if( p->width == width && p->height == height )
        return false;
    if( p->gl_mode )
    {
        /*
         * GL draws to the surface and owns no CPU buffer -- but the logical
         * size is still the space every touch is mapped INTO
         * (map_surface_to_canvas) and the keyboard inset is measured in, so
         * it must follow the canvas exactly as it does in software mode.
         * Returning before recording it left the mapper letterboxing a
         * 765x503 canvas that no longer existed once the mobile layout had
         * grown the canvas to the whole surface: every finger landed at
         * two thirds of where it was put, and the touch marker drew there
         * to prove it.
         */
        p->width = width;
        p->height = height;
        return true;
    }
    return android_make_canvas(p, width, height) != 0;
}

void
PlatformWindow_MapMouse(struct PlatformWindow* p, int win_x, int win_y, int* out_x, int* out_y)
{
    assert(p);
    assert(out_x);
    assert(out_y);
    map_surface_to_canvas(p, win_x, win_y, out_x, out_y);
}

/* ---- the pump ------------------------------------------------------------ */

static uint64_t
android_now_ms(void)
{
    return PlatformWindow_Ticks64();
}

void
PlatformWindow_PollCommands(struct PlatformWindow* p, struct ToriRS_CmdBus* bus)
{
    struct android_event ev;
    uint64_t const now = android_now_ms();
    int win_w;
    int win_h;

    assert(p);
    assert(bus);

    /* A finger held perfectly still sends no further MotionEvent, so the long
     * press is given its chance to become a right click from out here. */
    ToriRS_TouchTick(&p->touch, bus, now);

    while( event_pop(&ev) )
    {
        if( ev.kind == ANDROID_EVENT_TOUCH )
        {
            enum ToriRS_TouchPhase phase = TORIRS_TOUCH_MOVED;
            int cx;
            int cy;

            if( ev.action == PLATFORM_ANDROID_TOUCH_DOWN )
                phase = TORIRS_TOUCH_BEGAN;
            else if( ev.action == PLATFORM_ANDROID_TOUCH_UP )
                phase = TORIRS_TOUCH_ENDED;

            /* Mapped HERE, not where the event was posted: the letterbox that
             * decides the mapping belongs to the frame thread and can change
             * between the finger landing and this drain. */
            map_surface_to_canvas(p, ev.x, ev.y, &cx, &cy);
            /* TORIRS_TOUCH_DEBUG=1: the finger, the window it was measured in,
             * the canvas it was mapped into, and the letterbox that did it --
             * the four numbers that decide whether a tap lands where it was
             * made, printed for the tap rather than argued about. */
            {
                static int dbg = -1;
                if( dbg < 0 )
                    dbg = getenv("TORIRS_TOUCH_DEBUG") != NULL;
                if( dbg && ev.action != PLATFORM_ANDROID_TOUCH_MOVE )
                {
                    struct android_rect box;
                    int ww = 0;
                    int wh = 0;
                    PlatformAndroid_WindowSize(&ww, &wh);
                    letterbox_dst(p->width, p->height, ww, wh, &box);
                    __android_log_print(
                        ANDROID_LOG_INFO,
                        ANDROID_LOG_TAG,
                        "touch: surface=%d,%d window=%dx%d canvas=%dx%d "
                        "box=%d,%d %dx%d -> canvas=%d,%d",
                        ev.x, ev.y, ww, wh, p->width, p->height,
                        box.x, box.y, box.w, box.h, cx, cy);
                }
            }
            ToriRS_TouchEvent(&p->touch, bus, phase, (int64_t)ev.pointer_id, cx, cy, now);
            continue;
        }

        /* A key. Both spaces, exactly as the Win32 backend pushes them. */
        {
            enum LibToriRS_KeyCode const key = android_keycode_to_torirsk(ev.keycode);
            int const vk = android_keycode_to_vk(ev.keycode);
            int const osrs = vk >= 0 ? LibToriRS_OsrsKeyFromVk(vk) : -1;

            CmdBus_PushKey(
                bus,
                ev.down ? TORIRS_CMD_INPUT_KEY_DOWN : TORIRS_CMD_INPUT_KEY_UP,
                (uint8_t)key);
            if( osrs >= 0 )
            {
                CmdBus_PushOsrsKey(bus, osrs, (uint8_t)(ev.down != 0), (uint8_t)(ev.down != 0));
                if( ev.down )
                    CmdBus_PushKeyEvent(bus, osrs, 0, 0);
            }
            /* The printable half. Latin-1 only, matching the Win32 backend's
             * WM_CHAR arm -- the client's fonts are 8-bit and a code point
             * above 255 has no glyph to draw. */
            if( ev.down && ev.unicode >= 32 && ev.unicode <= 255 )
                CmdBus_PushKeyEvent(bus, -1, ev.unicode, 0);
        }
    }

    /*
     * One coalesced resize per poll, after the input above -- a touch seen at
     * the old size is applied at the old size, matching every other backend.
     * The surface changes size when the device rotates or the system bars come
     * and go, which on Android is the only way a resize ever happens.
     */
    PlatformAndroid_WindowSize(&win_w, &win_h);
    if( win_w > 0 && win_h > 0 && (win_w != p->last_seen_w || win_h != p->last_seen_h) )
    {
        p->last_seen_w = win_w;
        p->last_seen_h = win_h;
        if( p->canvas_follows_window )
            CmdBus_PushWindowResize(bus, win_w, win_h);
    }

    /*
     * The keyboard's coverage, likewise coalesced -- recomputed rather than
     * change-detected on the raw report, because the CANVAS answer also moves
     * when the letterbox or the canvas size does (an interface-scaling change
     * with the keyboard up), and those never touch g_keyboard_inset_px.
     */
    {
        int const inset =
            keyboard_canvas_inset(p, PlatformAndroid_KeyboardInset(), win_w, win_h);
        if( inset != p->last_keyboard_inset )
        {
            p->last_keyboard_inset = inset;
            CmdBus_PushKeyboardInset(bus, inset);
        }
    }
}

/* ---- damage -------------------------------------------------------------- */

void
PlatformWindow_SetPresentDamage(struct PlatformWindow* p, int x, int y, int w, int h)
{
    assert(p);

    if( w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > p->width || y + h > p->height )
    {
        /* Out of range is "present everything" rather than an assert: a
         * caller's damage box is a claim about what it drew, and the safe
         * response to a claim this code cannot honour is to copy more than
         * asked, never less. */
        p->present_dmg_w = 0;
        p->present_dmg_h = 0;
        p->present_dmg_rect_count = 0;
        return;
    }
    p->present_dmg_x = x;
    p->present_dmg_y = y;
    p->present_dmg_w = w;
    p->present_dmg_h = h;
    p->present_dmg_rect_count = 0;
}

void
PlatformWindow_SetPresentDamageRects(struct PlatformWindow* p, int const (*rects)[4], int count)
{
    assert(p);
    assert(rects);
    assert(count > 0);

    if( p->present_dmg_w <= 0 || p->present_dmg_h <= 0 )
        return;
    if( count > PLATFORM_PRESENT_DAMAGE_RECT_MAX )
        return;
    for( int i = 0; i < count; i++ )
    {
        if( rects[i][2] <= 0 || rects[i][3] <= 0 || rects[i][0] < p->present_dmg_x ||
            rects[i][1] < p->present_dmg_y ||
            rects[i][0] + rects[i][2] > p->present_dmg_x + p->present_dmg_w ||
            rects[i][1] + rects[i][3] > p->present_dmg_y + p->present_dmg_h )
            return; /* not inside the box -- present the box instead */
        p->present_dmg_rects[i][0] = rects[i][0];
        p->present_dmg_rects[i][1] = rects[i][1];
        p->present_dmg_rects[i][2] = rects[i][2];
        p->present_dmg_rects[i][3] = rects[i][3];
    }
    p->present_dmg_rect_count = count;
}

/* ---- present ------------------------------------------------------------- */

void
PlatformWindow_Present(struct PlatformWindow* p)
{
    ANativeWindow* window;
    ANativeWindow_Buffer buffer;

    assert(p);
    if( p->gl_mode )
        return;
    assert(p->pixels);

    window = PlatformAndroid_Window();
    if( !window )
    {
        /*
         * The activity is stopped and Android has taken the Surface back. Not
         * an error and not a stall: the frame loop keeps running (the world is
         * still ticking, the network is still draining) and simply has nowhere
         * to put the picture until a surface returns.
         */
        p->present_dmg_w = 0;
        p->present_dmg_h = 0;
        p->present_dmg_rect_count = 0;
        return;
    }

    /*
     * The geometry is set every present rather than once at surface time.
     * ANativeWindow_setBuffersGeometry with the surface's own size is a no-op
     * after the first call, and stating the FORMAT is the part that matters:
     * without it the buffer's format is whatever the Surface was created with,
     * which is not something this file should have to trust the Java side for.
     *
     * WINDOW_FORMAT_RGBX_8888, not RGBA: the client's frame is fully opaque,
     * and telling the compositor so lets it skip blending the whole surface.
     */
    ANativeWindow_setBuffersGeometry(window, 0, 0, WINDOW_FORMAT_RGBX_8888);

    if( ANativeWindow_lock(window, &buffer, NULL) != 0 )
    {
        /* A lock can fail while the surface is being torn down. Dropping the
         * frame is right; retrying would spin against a window that is going
         * away. */
        p->present_dmg_w = 0;
        p->present_dmg_h = 0;
        p->present_dmg_rect_count = 0;
        return;
    }

    /*
     * The damage box is deliberately NOT used to narrow this copy.
     *
     * ANativeWindow hands back one of a rotating set of buffers, so the pixels
     * outside a damage box are not last frame's -- they are some older frame's,
     * or uninitialised. A partial copy would leave those visible, which is the
     * exact class of bug the box exists to avoid on a platform that DOES retain
     * its window contents (GDI). Keeping the state and ignoring it here means
     * the interface above needs no per-platform arm.
     */
    present_blit(p, (uint32_t*)buffer.bits, buffer.stride, buffer.width, buffer.height);

    ANativeWindow_unlockAndPost(window);

    p->present_dmg_w = 0;
    p->present_dmg_h = 0;
    p->present_dmg_rect_count = 0;
}

void
PlatformWindow_PresentGL(struct PlatformWindow* p)
{
    assert(p);
    PlatformAndroidGL_SwapBuffers();
}

/* ---- time ---------------------------------------------------------------- */

uint64_t
PlatformWindow_Ticks64(void)
{
    struct timespec ts;

    /* CLOCK_MONOTONIC, not REALTIME: the frame pacer measures intervals, and a
     * clock the user or the network can step backwards produces a negative one. */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

uint64_t
PlatformWindow_TicksUs(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)(ts.tv_nsec / 1000);
}

void
PlatformWindow_SleepUntil(uint64_t deadline_ms)
{
    for( ;; )
    {
        uint64_t const now = PlatformWindow_Ticks64();
        struct timespec req;
        uint64_t remain;

        if( now >= deadline_ms )
            return;
        remain = deadline_ms - now;
        req.tv_sec = (time_t)(remain / 1000u);
        req.tv_nsec = (long)((remain % 1000u) * 1000000u);
        /* Looped because nanosleep returns early on a signal, and the pacer's
         * contract is an absolute deadline. */
        if( nanosleep(&req, NULL) == 0 )
            return;
    }
}
