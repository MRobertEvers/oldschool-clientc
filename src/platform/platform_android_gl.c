/*
 * platform_android_gl.c -- the GL context seam, over EGL.
 *
 * Android's implementation of platform_gl_context.h. The seam is the only
 * place in this tree that knows how a GL context is made; the renderers
 * program to it and never see EGL.
 *
 * The renderer on the other side of this seam here is the lane's own
 * platform_androidarmv7_renderer_opengles2_*.c: OpenGL ES 2.0 core with no extensions,
 * shaped after the Windows D3D9 renderer's retained model. This file's only
 * job is the context it draws into.
 *
 * SURFACE LIFETIME
 *
 * An EGLSurface is bound to an ANativeWindow, and Android destroys that window
 * whenever the activity stops. So the surface is rebuilt on demand rather than
 * held for the life of the context: the EGLContext and its config survive, and
 * only the surface follows the window. This is the one place where the Android
 * lane genuinely differs from every desktop host, where a window outlives the
 * app's interest in it.
 */

#include "platform/platform_gl_context.h"
#include "platform/platform_android.h"

#include <EGL/egl.h>
#include <android/log.h>
#include <android/native_window.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#define ANDROID_LOG_TAG "torirs"

static EGLDisplay g_display = EGL_NO_DISPLAY;
static EGLSurface g_surface = EGL_NO_SURFACE;
static EGLContext g_context = EGL_NO_CONTEXT;
static EGLConfig g_config;
/** The ANativeWindow g_surface was made for, so a changed one is noticed. */
static ANativeWindow* g_surface_window;
static char const* g_error = "";

/*
 * TORIRS_GLES2_EGL_LAZY (default on; =0 for the control arm): skip the
 * per-frame eglMakeCurrent and eglQuerySurface pair when nothing they would
 * change has changed. Read once at context creation. This seam has no
 * renderer struct to carry the lever, so it lives here.
 *
 * Why the skip is sound. EGL binds a context to the CALLING THREAD, and it
 * stays current on that thread until an eglMakeCurrent on that thread changes
 * it or the surface it is bound to is destroyed (EGL 1.4 §3.7.3). This file is
 * the only caller of eglMakeCurrent in the process, the render thread is the
 * only thread that draws, and the only event that changes the surface is the
 * ANativeWindow coming and going -- which android_gl_sync_surface is built to
 * notice and which clears g_is_current on every path that destroys or
 * rebuilds the surface. So "made current once, surface unchanged since" IS
 * "still current"; re-asserting it every frame bought nothing but the
 * driver's validation (the eglGetError in the profile).
 */
static int g_lazy = 1;
/** eglMakeCurrent(g_surface, g_context) succeeded and nothing has touched
 *  the surface since. */
static int g_is_current;
/*
 * The cached drawable size and the platform window size it was taken at. An
 * EGL window surface's EGL_WIDTH/EGL_HEIGHT track the ANativeWindow's buffer
 * size, which changes only through surfaceChanged -- the same callback that
 * updates PlatformAndroid_WindowSize. So the query is repeated only when the
 * platform's size differs from the one the cache was taken at, or when the
 * surface itself was rebuilt (g_drawable_valid cleared).
 */
static int g_drawable_valid;
static EGLint g_drawable_w;
static EGLint g_drawable_h;
static EGLint g_drawable_window_w;
static EGLint g_drawable_window_h;

/** Rebuild the EGLSurface when the window it belongs to has changed or gone.
 *  @return 1 when there is a usable surface afterwards. */
static int
android_gl_sync_surface(void)
{
    ANativeWindow* window = PlatformAndroid_Window();

    if( window == g_surface_window && g_surface != EGL_NO_SURFACE )
        return 1;

    /* The surface is about to change: nothing cached about it survives. */
    g_is_current = 0;
    g_drawable_valid = 0;
    if( g_surface != EGL_NO_SURFACE )
    {
        /* Unbind before destroying: eglDestroySurface on a current surface is
         * deferred, and the next eglCreateWindowSurface would then be racing a
         * surface the driver has not finished releasing. */
        eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(g_display, g_surface);
        g_surface = EGL_NO_SURFACE;
    }
    g_surface_window = window;
    if( !window )
        return 0; /* the activity is stopped; not an error */

    g_surface = eglCreateWindowSurface(g_display, g_config, window, NULL);
    if( g_surface == EGL_NO_SURFACE )
    {
        g_error = "eglCreateWindowSurface failed";
        return 0;
    }
    if( !eglMakeCurrent(g_display, g_surface, g_surface, g_context) )
    {
        g_error = "eglMakeCurrent failed after surface rebuild";
        return 0;
    }
    g_is_current = 1;
    return 1;
}

ToriPlatform_GLContext
ToriPlatform_GLContext_Create(ToriPlatform_GLWindow* window, int depth_bits, enum ToriPlatform_GLClient client)
{
    /*
     * The renderable type the caller's GL needs, and a config whose depth size
     * is what it asked for. Both go in the CONFIG rather than afterwards
     * because that is the only place EGL will honour them -- there is no
     * equivalent of setting an attribute on an existing context.
     *
     * The renderable type must follow the client version, not just the
     * context attribute below: EGL requires an ES3 context to come from a
     * config advertising EGL_OPENGL_ES3_BIT_KHR. Drivers that are lenient
     * about it hand back an ES3 context from an ES2 config anyway, which is
     * exactly what makes getting this wrong a trap -- it works on the device
     * in front of you and fails on the next one. 0x0040 is spelled out
     * because EGL_OPENGL_ES3_BIT_KHR is an extension token that an older
     * <EGL/egl.h> in an NDK sysroot may not declare, while every EGL 1.4
     * implementation that can make an ES3 context understands the value.
     *
     * The Android lane asks for ES3 now: platform_androidarmv7_renderer_opengles3.c names the
     * shared ES 3.0 core (platform_renderer_es3_*.c, which the browser binds
     * as WebGL2) and --gles3 selects it. The XT1060's Adreno 320 reports
     * ro.opengles.version 196608, which is ES 3.0 exactly.
     */
    EGLint const renderable = client == TORIPLATFORM_GL_CLIENT_ES3 ? 0x0040 : EGL_OPENGL_ES2_BIT;
    EGLint const attribs[] = {
        EGL_RENDERABLE_TYPE, renderable,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_DEPTH_SIZE,      depth_bits > 0 ? depth_bits : 0,
        EGL_NONE
    };
    EGLint const context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, client == TORIPLATFORM_GL_CLIENT_ES3 ? 3 : 2, EGL_NONE
    };
    EGLint config_count = 0;

    (void)window; /* there is exactly one Surface; @see platform_gl_context.h */

    if( g_context != EGL_NO_CONTEXT )
        return (ToriPlatform_GLContext)g_context; /* already up */

    {
        /* The lever, read once: unset or anything but "0" is on. */
        char const* lazy = getenv("TORIRS_GLES2_EGL_LAZY");
        g_lazy = !(lazy && lazy[0] == '0' && lazy[1] == '\0');
    }
    g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if( g_display == EGL_NO_DISPLAY )
    {
        g_error = "eglGetDisplay failed";
        return NULL;
    }
    if( !eglInitialize(g_display, NULL, NULL) )
    {
        g_error = "eglInitialize failed";
        return NULL;
    }
    /*
     * Choose the config BY HAND, because eglChooseConfig cannot be told to
     * refuse multisampling.
     *
     * EGL_SAMPLE_BUFFERS and EGL_SAMPLES are "AtLeast" attributes: leaving
     * them out defaults both to 0, and a default of 0 still MATCHES a config
     * with 4x MSAA. The sort is supposed to put fewer samples first, but the
     * order is the driver's and only the first config was ever read -- so
     * which config this lane got was the driver's choice, not ours. ES2 and
     * ES3 ask for different EGL_RENDERABLE_TYPE bits and therefore choose from
     * different lists, which is how the two lanes can end up on configs that
     * differ in more than the client version.
     *
     * This lane never wants MSAA: the client renders at a fixed canvas and
     * resolves nothing, so multisampling is pure cost -- and on the Adreno
     * 320 an unwanted multisampled window is also a correctness risk. So the
     * list is walked and a single-sample config is taken; config[0] is the
     * fallback, and the chosen config's real attributes are reported so this
     * is never again something to guess at.
     */
    {
        EGLConfig configs[32];
        EGLint chosen = -1;
        EGLint i;

        if( !eglChooseConfig(
                g_display, attribs, configs,
                (EGLint)(sizeof(configs) / sizeof(configs[0])), &config_count) ||
            config_count < 1 )
        {
            g_error = "no EGL config with the requested client version and depth size";
            return NULL;
        }
        for( i = 0; i < config_count; i++ )
        {
            EGLint sample_buffers = 0;
            EGLint samples = 0;
            eglGetConfigAttrib(g_display, configs[i], EGL_SAMPLE_BUFFERS, &sample_buffers);
            eglGetConfigAttrib(g_display, configs[i], EGL_SAMPLES, &samples);
            if( sample_buffers == 0 && samples == 0 )
            {
                chosen = i;
                break;
            }
        }
        g_config = configs[chosen >= 0 ? chosen : 0];
        if( chosen < 0 )
            __android_log_print(
                ANDROID_LOG_WARN,
                ANDROID_LOG_TAG,
                "EGL: no single-sample config among %d; taking the driver's first",
                config_count);
    }

    g_context = eglCreateContext(g_display, g_config, EGL_NO_CONTEXT, context_attribs);
    if( g_context == EGL_NO_CONTEXT )
    {
        g_error = "eglCreateContext failed";
        return NULL;
    }

    /*
     * The surface needs a window, and at this point there always is one:
     * PlatformWindow_InitForOpenGL3 waited for it before the renderer was even
     * constructed. Refusing here rather than limping on is deliberate -- a
     * context with no surface renders nowhere, and main.c has a working
     * software path to fall back to.
     */
    if( !android_gl_sync_surface() )
    {
        eglDestroyContext(g_display, g_context);
        g_context = EGL_NO_CONTEXT;
        return NULL;
    }

    {
        /* What the driver actually gave us, not what was asked for. */
        EGLint cfg_id = 0, r = 0, g = 0, b = 0, a = 0, d = 0, s = 0;
        EGLint sample_buffers = 0, samples = 0;
        eglGetConfigAttrib(g_display, g_config, EGL_CONFIG_ID, &cfg_id);
        eglGetConfigAttrib(g_display, g_config, EGL_RED_SIZE, &r);
        eglGetConfigAttrib(g_display, g_config, EGL_GREEN_SIZE, &g);
        eglGetConfigAttrib(g_display, g_config, EGL_BLUE_SIZE, &b);
        eglGetConfigAttrib(g_display, g_config, EGL_ALPHA_SIZE, &a);
        eglGetConfigAttrib(g_display, g_config, EGL_DEPTH_SIZE, &d);
        eglGetConfigAttrib(g_display, g_config, EGL_STENCIL_SIZE, &s);
        eglGetConfigAttrib(g_display, g_config, EGL_SAMPLE_BUFFERS, &sample_buffers);
        eglGetConfigAttrib(g_display, g_config, EGL_SAMPLES, &samples);
        __android_log_print(
            ANDROID_LOG_INFO,
            ANDROID_LOG_TAG,
            "EGL/ES%d context up: config %d rgba %d%d%d%d depth %d (asked %d) "
            "stencil %d sample_buffers %d samples %d",
            client == TORIPLATFORM_GL_CLIENT_ES3 ? 3 : 2,
            cfg_id, r, g, b, a, d, depth_bits, s, sample_buffers, samples);
    }
    return (ToriPlatform_GLContext)g_context;
}

int
ToriPlatform_GLContext_MakeCurrent(ToriPlatform_GLWindow* window, ToriPlatform_GLContext context)
{
    (void)window;
    (void)context;

    if( g_context == EGL_NO_CONTEXT )
        return -1;
    /* The lazy arm: still current on this thread and the window the surface
     * was made for is still the platform's window -- nothing to do. The
     * window test is the one android_gl_sync_surface would make; see the
     * note at g_lazy for why "current once" is "current still". */
    if( g_lazy && g_is_current && g_surface != EGL_NO_SURFACE &&
        PlatformAndroid_Window() == g_surface_window )
        return 0;
    /* Reconciles the surface with the current window first: on Android "make
     * current" is also the moment a surface lost to a stop/resume comes back. */
    if( !android_gl_sync_surface() )
        return -1;
    if( !eglMakeCurrent(g_display, g_surface, g_surface, g_context) )
    {
        g_error = "eglMakeCurrent failed";
        g_is_current = 0;
        return -1;
    }
    g_is_current = 1;
    return 0;
}

void
ToriPlatform_GLContext_Delete(ToriPlatform_GLContext context)
{
    (void)context;

    if( g_display == EGL_NO_DISPLAY )
        return;
    eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if( g_surface != EGL_NO_SURFACE )
        eglDestroySurface(g_display, g_surface);
    if( g_context != EGL_NO_CONTEXT )
        eglDestroyContext(g_display, g_context);
    eglTerminate(g_display);
    g_surface = EGL_NO_SURFACE;
    g_context = EGL_NO_CONTEXT;
    g_display = EGL_NO_DISPLAY;
    g_surface_window = NULL;
    g_is_current = 0;
    g_drawable_valid = 0;
}

void
ToriPlatform_GLContext_DrawableSize(ToriPlatform_GLWindow* window, int* out_width, int* out_height)
{
    EGLint w = 0;
    EGLint h = 0;
    int window_w = 0;
    int window_h = 0;

    (void)window;

    /* The lazy arm: the cached answer stands while the surface it was
     * measured on stands and the platform has reported no new size (see
     * g_drawable_valid). */
    PlatformAndroid_WindowSize(&window_w, &window_h);
    if( g_lazy && g_drawable_valid && g_surface != EGL_NO_SURFACE &&
        window_w == g_drawable_window_w && window_h == g_drawable_window_h )
    {
        if( out_width )
            *out_width = (int)g_drawable_w;
        if( out_height )
            *out_height = (int)g_drawable_h;
        return;
    }
    if( g_surface != EGL_NO_SURFACE )
    {
        eglQuerySurface(g_display, g_surface, EGL_WIDTH, &w);
        eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &h);
        if( w > 0 && h > 0 )
        {
            g_drawable_w = w;
            g_drawable_h = h;
            g_drawable_window_w = window_w;
            g_drawable_window_h = window_h;
            g_drawable_valid = 1;
        }
    }
    if( w <= 0 || h <= 0 )
    {
        /* No surface right now (a stopped activity). The Surface's last known
         * size is still the right answer for anything sizing a framebuffer --
         * it is what the surface will come back as. */
        PlatformAndroid_WindowSize(&w, &h);
    }
    if( out_width )
        *out_width = (int)w;
    if( out_height )
        *out_height = (int)h;
}

void
ToriPlatform_GLContext_SetSwapInterval(int interval)
{
    if( g_display != EGL_NO_DISPLAY )
        eglSwapInterval(g_display, interval);
}

char const*
ToriPlatform_GLContext_LastError(void)
{
    return g_error;
}

/* ---- the swap ------------------------------------------------------------
 *
 * Not part of platform_gl_context.h: presenting is the PLATFORM's job
 * (PlatformWindow_PresentGL) on every lane, and the seam covers only making
 * a context. This is the Android swap, declared in platform_android.h beside
 * the rest of what platform_android.c calls.
 */
void
PlatformAndroidGL_SwapBuffers(void)
{
    /* TORIRS_SWAP_DEBUG=1: how long eglSwapBuffers blocks, averaged over 300
     * swaps and printed to logcat. With the swap interval at 0 the only thing
     * this can wait on is the GPU being behind -- the driver stalling the
     * dequeue until an earlier frame finishes -- so it is the number that
     * separates "the CPU is slow" from "the GPU is", which no CPU profile
     * can. */
    static int debug = -1;
    static int64_t accumulated_ns = 0;
    static int64_t worst_ns = 0;
    static int swaps = 0;
    struct timespec before;
    struct timespec after;

    if( g_display == EGL_NO_DISPLAY || g_surface == EGL_NO_SURFACE )
        return; /* stopped activity -- nothing to show it on */
    if( debug < 0 )
        debug = getenv("TORIRS_SWAP_DEBUG") != NULL;
    if( !debug )
    {
        eglSwapBuffers(g_display, g_surface);
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &before);
    eglSwapBuffers(g_display, g_surface);
    clock_gettime(CLOCK_MONOTONIC, &after);
    {
        int64_t elapsed_ns = ((int64_t)(after.tv_sec - before.tv_sec) * 1000000000LL) +
            (int64_t)(after.tv_nsec - before.tv_nsec);
        accumulated_ns += elapsed_ns;
        if( elapsed_ns > worst_ns )
            worst_ns = elapsed_ns;
        if( ++swaps == 300 )
        {
            __android_log_print(
                ANDROID_LOG_INFO,
                ANDROID_LOG_TAG,
                "swap: mean %.2f ms, worst %.2f ms over %d swaps",
                (double)accumulated_ns / swaps / 1e6,
                (double)worst_ns / 1e6,
                swaps);
            accumulated_ns = 0;
            worst_ns = 0;
            swaps = 0;
        }
    }
}
