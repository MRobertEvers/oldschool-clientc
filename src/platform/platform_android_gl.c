/*
 * platform_android_gl.c -- the GL context seam, over EGL. No SDL.
 *
 * Android's implementation of platform_gl_context.h, and the counterpart of
 * platform_gl_context_sdl.c on the lanes that do run SDL. Between them they are
 * the only two files in this tree that know how a GL context is made, which is
 * what lets the ~11k-line GLES2 renderer (platform_sdl2_renderer_webgl1.c) be
 * compiled UNCHANGED for both a browser and a phone.
 *
 * WHY THE GLES2 RENDERER IS THE RIGHT ONE HERE
 *
 * WebGL1 *is* GLES2. That renderer was written to the GLES2 ceiling and
 * respects every part of it: no uniform blocks, no 32-bit element indices
 * (webgl1_index16.c splits an index range into 16-bit windows instead), no
 * GLES3/desktop pixel-store parameters. Those are not browser quirks -- they
 * are exactly the limits a 2013-era armv7 phone's driver still enforces. The
 * desktop GL3 renderer would have to have each of them added back.
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

#define ANDROID_LOG_TAG "torirs"

static EGLDisplay g_display = EGL_NO_DISPLAY;
static EGLSurface g_surface = EGL_NO_SURFACE;
static EGLContext g_context = EGL_NO_CONTEXT;
static EGLConfig g_config;
/** The ANativeWindow g_surface was made for, so a changed one is noticed. */
static ANativeWindow* g_surface_window;
static char const* g_error = "";

/** Rebuild the EGLSurface when the window it belongs to has changed or gone.
 *  @return 1 when there is a usable surface afterwards. */
static int
android_gl_sync_surface(void)
{
    ANativeWindow* window = PlatformAndroid_Window();

    if( window == g_surface_window && g_surface != EGL_NO_SURFACE )
        return 1;

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
    return 1;
}

ToriRS_GLContext
ToriRS_GLContext_Create(ToriRS_GLWindow* window, int depth_bits)
{
    /*
     * EGL_OPENGL_ES2_BIT, and a config whose depth size is what the caller
     * asked for. Requested in the CONFIG rather than afterwards because that is
     * the only place EGL will honour it -- there is no equivalent of setting an
     * attribute on an existing context.
     */
    EGLint const attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_DEPTH_SIZE,      depth_bits > 0 ? depth_bits : 0,
        EGL_NONE
    };
    EGLint const context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint config_count = 0;

    (void)window; /* there is exactly one Surface; @see platform_gl_context.h */

    if( g_context != EGL_NO_CONTEXT )
        return (ToriRS_GLContext)g_context; /* already up */

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
    if( !eglChooseConfig(g_display, attribs, &g_config, 1, &config_count) || config_count < 1 )
    {
        g_error = "no EGL config with GLES2 and the requested depth size";
        return NULL;
    }

    g_context = eglCreateContext(g_display, g_config, EGL_NO_CONTEXT, context_attribs);
    if( g_context == EGL_NO_CONTEXT )
    {
        g_error = "eglCreateContext failed";
        return NULL;
    }

    /*
     * The surface needs a window, and at this point there always is one:
     * PlatformSDL2_InitForOpenGL3 waited for it before the renderer was even
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

    __android_log_print(
        ANDROID_LOG_INFO,
        ANDROID_LOG_TAG,
        "EGL/GLES2 context up (depth %d)",
        depth_bits);
    return (ToriRS_GLContext)g_context;
}

int
ToriRS_GLContext_MakeCurrent(ToriRS_GLWindow* window, ToriRS_GLContext context)
{
    (void)window;
    (void)context;

    if( g_context == EGL_NO_CONTEXT )
        return -1;
    /* Reconciles the surface with the current window first: on Android "make
     * current" is also the moment a surface lost to a stop/resume comes back. */
    if( !android_gl_sync_surface() )
        return -1;
    if( !eglMakeCurrent(g_display, g_surface, g_surface, g_context) )
    {
        g_error = "eglMakeCurrent failed";
        return -1;
    }
    return 0;
}

void
ToriRS_GLContext_Delete(ToriRS_GLContext context)
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
}

void
ToriRS_GLContext_DrawableSize(ToriRS_GLWindow* window, int* out_width, int* out_height)
{
    EGLint w = 0;
    EGLint h = 0;

    (void)window;

    if( g_surface != EGL_NO_SURFACE )
    {
        eglQuerySurface(g_display, g_surface, EGL_WIDTH, &w);
        eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &h);
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
ToriRS_GLContext_SetSwapInterval(int interval)
{
    if( g_display != EGL_NO_DISPLAY )
        eglSwapInterval(g_display, interval);
}

char const*
ToriRS_GLContext_LastError(void)
{
    return g_error;
}

/* ---- the swap ------------------------------------------------------------
 *
 * Not part of platform_gl_context.h: presenting is the PLATFORM's job
 * (PlatformSDL2_PresentGL), and on the SDL lanes it is platform_sdl2.c that
 * calls SDL_GL_SwapWindow. This is the Android half of that, declared in
 * platform_android.h beside the rest of what platform_android.c calls.
 */
void
PlatformAndroidGL_SwapBuffers(void)
{
    if( g_display == EGL_NO_DISPLAY || g_surface == EGL_NO_SURFACE )
        return; /* stopped activity -- nothing to show it on */
    eglSwapBuffers(g_display, g_surface);
}
