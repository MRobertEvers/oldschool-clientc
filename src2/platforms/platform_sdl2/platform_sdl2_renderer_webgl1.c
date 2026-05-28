#include "platform_sdl2_renderer_webgl1.h"

#include "ToriRSPlatformKit/src/backends/webgl1/webgl1_vertex.h"
#include "ToriRSPlatformKit/src/tools/trspk_batch16.h"
#include "libtorirs.h"
#include "platforms/trspk_toridraw.h"
#include "render/libtorirs_render.h"
#include <GLES2/gl2.h>

#include <stdlib.h>
#include <string.h>

struct LibToriPlatformSDL2_RendererWebGL1
{
    SDL_Window* window;
    SDL_GLContext gl_context;
    int width;
    int height;
};

static void
webgl1_handle_render_command(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command,
    struct LibToriPlatformSDL2_RendererWebGL1_FrameState* frame_state)
{
    switch( command->kind )
    {
    case TORIRSRC_MODEL_LOAD:

        break;
    case TORIRSRC_BEGIN_3D:
        break;
    case TORIRSRC_END_3D:
        break;
    case TORIRSRC_DRAW_MODEL:
        break;
    default:
        break;
    }
}

struct LibToriPlatformSDL2_RendererWebGL1*
LibToriPlatformSDL2_RendererWebGL1_New(
    int width,
    int height)
{
    struct LibToriPlatformSDL2_RendererWebGL1* renderer =
        malloc(sizeof(struct LibToriPlatformSDL2_RendererWebGL1));
    memset(renderer, 0, sizeof(struct LibToriPlatformSDL2_RendererWebGL1));

    renderer->width = width;
    renderer->height = height;

    return renderer;
}

void
LibToriPlatformSDL2_RendererWebGL1_Free(struct LibToriPlatformSDL2_RendererWebGL1* renderer)
{
    free(renderer);
}

bool
LibToriPlatformSDL2_RendererWebGL1_Init(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    SDL_Window* window)
{
    renderer->window = window;
    renderer->gl_context = SDL_GL_CreateContext(window);
    if( !renderer->gl_context )
        return false;
    SDL_GL_MakeCurrent(window, renderer->gl_context);
    SDL_GL_GetDrawableSize(window, &renderer->width, &renderer->height);

    return true;
}

void
LibToriPlatformSDL2_RendererWebGL1_Render(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    struct LibToriRS_Instance* instance)
{
    if( SDL_GL_MakeCurrent(renderer->window, renderer->gl_context) != 0 )
        return;

    SDL_GL_SwapWindow(renderer->window);
    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_GL_GetDrawableSize(renderer->window, &drawable_w, &drawable_h);
    renderer->width = drawable_w;
    renderer->height = drawable_h;

    glViewport(0, 0, drawable_w, drawable_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    LibToriRS_FrameBegin(instance);
    struct LibToriRS_RenderCommand command;
    while( LibToriRS_FrameNextCommand(instance, &command) )
        webgl1_handle_render_command(renderer, instance, &command);
    LibToriRS_FrameEnd(instance);

    SDL_GL_SwapWindow(renderer->window);
}