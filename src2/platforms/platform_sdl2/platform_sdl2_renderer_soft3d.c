#include "platform_sdl2_renderer_soft3d.h"

#include "libtorirs.h"
#include "libtorirs_internal.h"
#include "render/libtorirs_render.h"
#include "toridraw/toridraw.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct LibToriPlatformSDL2_RendererSoft3D
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    int* pixel_buffer;
    int width;
    int height;
};

struct LibToriPlatformSDL2_RendererSoft3D*
LibToriPlatformSDL2_RendererSoft3D_New(
    int width,
    int height)
{
    struct LibToriPlatformSDL2_RendererSoft3D* renderer =
        malloc(sizeof(struct LibToriPlatformSDL2_RendererSoft3D));
    if( !renderer )
        return NULL;
    memset(renderer, 0, sizeof(struct LibToriPlatformSDL2_RendererSoft3D));
    renderer->width = width;
    renderer->height = height;
    return renderer;
}

void
LibToriPlatformSDL2_RendererSoft3D_Free(struct LibToriPlatformSDL2_RendererSoft3D* renderer)
{
    if( !renderer )
        return;
    if( renderer->pixel_buffer )
    {
        free(renderer->pixel_buffer);
        renderer->pixel_buffer = NULL;
    }
    if( renderer->texture )
    {
        SDL_DestroyTexture(renderer->texture);
        renderer->texture = NULL;
    }
    if( renderer->renderer )
    {
        SDL_DestroyRenderer(renderer->renderer);
        renderer->renderer = NULL;
    }
    free(renderer);
}

bool
LibToriPlatformSDL2_RendererSoft3D_Init(
    struct LibToriPlatformSDL2_RendererSoft3D* renderer,
    SDL_Window* window)
{
    if( !renderer || !window )
        return false;

    renderer->window = window;

    renderer->renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if( !renderer->renderer )
    {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    size_t const pixel_count = (size_t)renderer->width * (size_t)renderer->height;
    renderer->pixel_buffer = (int*)malloc(pixel_count * sizeof(int));
    if( !renderer->pixel_buffer )
    {
        printf("Failed to allocate pixel buffer\n");
        SDL_DestroyRenderer(renderer->renderer);
        renderer->renderer = NULL;
        return false;
    }
    memset(renderer->pixel_buffer, 0, pixel_count * sizeof(int));

    renderer->texture = SDL_CreateTexture(
        renderer->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        renderer->width,
        renderer->height);
    if( !renderer->texture )
    {
        printf("SDL_CreateTexture failed: %s\n", SDL_GetError());
        free(renderer->pixel_buffer);
        renderer->pixel_buffer = NULL;
        SDL_DestroyRenderer(renderer->renderer);
        renderer->renderer = NULL;
        return false;
    }

    return true;
}

void
LibToriPlatformSDL2_RendererSoft3D_Render(
    struct LibToriPlatformSDL2_RendererSoft3D* renderer,
    struct LibToriRS_Instance* instance)
{
    if( !renderer || !renderer->pixel_buffer || !renderer->texture || !renderer->renderer )
        return;

    size_t const pixel_count = (size_t)renderer->width * (size_t)renderer->height;
    memset(renderer->pixel_buffer, 0, pixel_count * sizeof(int));

    struct ToriDraw_Context* draw_context = NULL;
    if( instance && instance->model_viewer )
        draw_context = instance->model_viewer->context;

    bool has_3d = false;
    struct LibToriRS_RenderCommand_Begin3D cur_3d;

    LibToriRS_FrameBegin(instance);
    struct LibToriRS_RenderCommand command;
    while( LibToriRS_FrameNextCommand(instance, &command) )
    {
        switch( command.kind )
        {
        case TORIRSRC_BEGIN_3D:
            cur_3d = command.u.begin_3d;
            has_3d = true;
            break;
        case TORIRSRC_END_3D:
            has_3d = false;
            break;
        case TORIRSRC_DRAW_MODEL:
            if( !draw_context || !has_3d )
                break;
            {
                struct ToriDraw_Position tmp_pos = cur_3d.camera_position;
                struct ToriDraw_ViewPort tmp_vp = cur_3d.view_port;
                struct ToriDraw_Camera tmp_cam = cur_3d.camera;
                toridraw_render_model3_raster(
                    draw_context, &tmp_vp, &tmp_cam, renderer->pixel_buffer, false);
            }
            break;
        default:
            break;
        }
    }
    LibToriRS_FrameEnd(instance);

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        renderer->pixel_buffer,
        renderer->width,
        renderer->height,
        32,
        renderer->width * (int)sizeof(int),
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000);
    if( !surface )
        return;

    int* pix_write = NULL;
    int texture_pitch = 0;
    if( SDL_LockTexture(renderer->texture, NULL, (void**)&pix_write, &texture_pitch) < 0 )
    {
        SDL_FreeSurface(surface);
        return;
    }

    int* src_pixels = (int*)surface->pixels;
    int texture_w = texture_pitch / (int)sizeof(int);

    memset(pix_write, 0, (size_t)texture_pitch * (size_t)renderer->height);

    for( int src_y = 0; src_y < renderer->height; src_y++ )
    {
        int* row = &pix_write[src_y * texture_w];
        memcpy(row, &src_pixels[src_y * renderer->width], (size_t)renderer->width * sizeof(int));
    }

    SDL_UnlockTexture(renderer->texture);
    SDL_FreeSurface(surface);

    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize(renderer->window, &window_width, &window_height);

    SDL_Rect dst_rect;
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = renderer->width;
    dst_rect.h = renderer->height;

    if( window_width > 0 && window_height > 0 )
    {
        float src_aspect = (float)renderer->width / (float)renderer->height;
        float window_aspect = (float)window_width / (float)window_height;

        if( src_aspect > window_aspect )
        {
            dst_rect.w = window_width;
            dst_rect.h = (int)(window_width / src_aspect);
            dst_rect.x = 0;
            dst_rect.y = (window_height - dst_rect.h) / 2;
        }
        else
        {
            dst_rect.h = window_height;
            dst_rect.w = (int)(window_height * src_aspect);
            dst_rect.y = 0;
            dst_rect.x = (window_width - dst_rect.w) / 2;
        }
    }

    SDL_RenderClear(renderer->renderer);
    SDL_RenderCopy(renderer->renderer, renderer->texture, NULL, &dst_rect);
    SDL_RenderPresent(renderer->renderer);
}
