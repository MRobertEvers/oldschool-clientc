#ifndef SRC_PLATFORM_PLATFORM_GL_CONTEXT_H
#define SRC_PLATFORM_PLATFORM_GL_CONTEXT_H

/*
 * The GL context, in terms no renderer has to know the windowing library for.
 *
 * ## Why this exists
 *
 * platform/platform_sdl2_renderer_webgl1.c is ~11k lines of plain GLES2, and
 * platform_sdl2_renderer_gl3.c the same for desktop GL 3.3. Neither has any
 * real dependency on a windowing library: every GL call comes from the GL
 * header, and the ONLY thing either needed SDL for was the handful of calls
 * that make and swap a context. That was fine while every host with a GPU path
 * ran SDL. Android does not, and must not -- it has EGL, and pulling SDL onto
 * the device to reach an EGL context that is already there would be a large
 * dependency bought for nine function calls.
 *
 * So the nine are named here, and each lane declares which implementation it
 * links (PLATFORM_SRCS in platform/platform.mk):
 *
 *   platform_gl_context_sdl.c   macos, linux, web -- SDL_GL_*.
 *   platform_android_gl.c       android           -- EGL.
 *
 * The renderers then contain no SDL at all, on any lane, which is what makes
 * the GLES2 renderer shareable between the browser and a phone WITHOUT a fork.
 * A second GLES2 renderer would be the real cost here; this header is what
 * avoids it.
 *
 * ## The window handle
 *
 * Opaque, and deliberately incomplete: a renderer receives one and does nothing
 * with it but hand it back to the functions below. The cast to whatever the
 * host's window really is happens inside the one implementation file that knows
 * -- platform_gl_context_sdl.c casts it to SDL_Window*, and the Android one
 * ignores it entirely because there is exactly one Surface.
 *
 * Declaring it incomplete rather than as void* is what makes a mistaken
 * dereference a compile error instead of a cast that happens to work.
 */

typedef struct ToriRS_GLWindow ToriRS_GLWindow;

/** A host GL context. NULL means "none"/"failed". */
typedef void* ToriRS_GLContext;

/**
 * Create a GL context for `window` and make it current.
 *
 * `depth_bits` is a REQUEST, and the one attribute either renderer ever sets.
 * It is a creation parameter rather than a separate "set attribute" call
 * because that is what it actually is on both backends -- SDL wants it before
 * the context, and EGL wants it in the config -- and a two-call form invites
 * setting it after the context exists, where it silently does nothing.
 * Pass 0 for "no depth buffer needed".
 *
 * @return NULL on failure, with ToriRS_GLContext_LastError() naming the cause.
 */
ToriRS_GLContext
ToriRS_GLContext_Create(ToriRS_GLWindow* window, int depth_bits);

/**
 * Make `context` current on `window`.
 *
 * Called before each phase that touches GL, because the renderers are written
 * to survive sharing a thread with something else that has its own context.
 *
 * @return 0 on success, non-zero on failure.
 */
int
ToriRS_GLContext_MakeCurrent(ToriRS_GLWindow* window, ToriRS_GLContext context);

/** Destroy it. Accepts NULL, like every other deallocator in this tree. */
void
ToriRS_GLContext_Delete(ToriRS_GLContext context);

/**
 * The drawable's size in REAL DEVICE PIXELS.
 *
 * Not the window's size in points: the renderer sizes its framebuffers and its
 * viewport from this, and on a HighDPI display those differ by the density.
 * Android has no points layer at all, so there the two are the same number.
 */
void
ToriRS_GLContext_DrawableSize(ToriRS_GLWindow* window, int* out_width, int* out_height);

/** Swap interval; 0 disables vsync-blocking. Failures are not reported --
 *  a backend that refuses just leaves the display's own pacing in place. */
void
ToriRS_GLContext_SetSwapInterval(int interval);

/** The last failure from this module, as text. Never NULL. */
char const*
ToriRS_GLContext_LastError(void);

#endif
