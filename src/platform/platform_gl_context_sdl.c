/*
 * platform_gl_context_sdl.c -- the GL context seam, over SDL.
 *
 * The macos, linux and web lanes' implementation of platform_gl_context.h.
 * Android's counterpart is platform_android_gl.c, over EGL; between them they
 * are the only two files in this tree that know how a GL context is made, which
 * is what lets both GL renderers contain no windowing library at all.
 *
 * @see platform/platform_gl_context.h for why the seam exists.
 */

#include "platform/platform_gl_context.h"

#include <SDL.h>

/*
 * SDL_Window and ToriRS_GLWindow are the same object under two names, and this
 * file is the only place that says so. The cast is one-way plumbing: the
 * renderers never see either type's definition.
 */
static SDL_Window*
as_sdl_window(ToriRS_GLWindow* window)
{
    return (SDL_Window*)window;
}

ToriRS_GLContext
ToriRS_GLContext_Create(ToriRS_GLWindow* window, int depth_bits)
{
    SDL_GLContext context;

    if( depth_bits > 0 )
    {
        /*
         * Asked for BEFORE the context, which is the only time SDL reads it.
         *
         * On desktop GL the request is what selects a visual that has a depth
         * buffer at all. A depth request that arrives after the context is
         * silently ignored -- which is why depth_bits is a parameter of Create
         * rather than a call of its own. (In the browser it is earlier still:
         * SDL's emscripten backend fixes depth and stencil when the WINDOW is
         * created, so platform_sdl2.c asks for the depth buffer there and this
         * request is redundant but harmless.)
         */
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depth_bits);
    }

    context = SDL_GL_CreateContext(as_sdl_window(window));
    if( !context )
        return NULL;

    if( SDL_GL_MakeCurrent(as_sdl_window(window), context) != 0 )
    {
        SDL_GL_DeleteContext(context);
        return NULL;
    }
    return (ToriRS_GLContext)context;
}

int
ToriRS_GLContext_MakeCurrent(ToriRS_GLWindow* window, ToriRS_GLContext context)
{
    return SDL_GL_MakeCurrent(as_sdl_window(window), (SDL_GLContext)context);
}

void
ToriRS_GLContext_Delete(ToriRS_GLContext context)
{
    if( !context )
        return;
    SDL_GL_DeleteContext((SDL_GLContext)context);
}

void
ToriRS_GLContext_DrawableSize(ToriRS_GLWindow* window, int* out_width, int* out_height)
{
    SDL_GL_GetDrawableSize(as_sdl_window(window), out_width, out_height);
}

void
ToriRS_GLContext_SetSwapInterval(int interval)
{
#if defined(__EMSCRIPTEN__)
    /*
     * Deliberately nothing in the browser. SDL's emscripten backend routes this
     * to eglSwapInterval, and emscripten's EGL implements THAT by switching the
     * main loop's timing mode: interval 0 becomes setTimeout(0), which would
     * take the frame loop off requestAnimationFrame. main.c owns that decision
     * (it moves between RAF and setTimeout as the tab hides and shows), so a
     * renderer asking for "no vsync wait" here would fight it -- and there is
     * no vsync wait to remove: a browser presents at RAF and nowhere else.
     */
    (void)interval;
#else
    SDL_GL_SetSwapInterval(interval);
#endif
}

char const*
ToriRS_GLContext_LastError(void)
{
    char const* err = SDL_GetError();
    return err ? err : "";
}
