#include "platform_impl_osx_sdl2_renderer_soft3d.h"

extern "C" {
#include "graphics/render.h"
#include "libgame.u.h"
}
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <SDL.h>
#include <stdio.h>

static void
render_imgui(struct Renderer* renderer, struct Game* game)
{}

static void
render_game(struct Renderer* renderer, struct Game* game)
{
    int scene_x = game->scene_model->region_x - game->camera_world_x;
    int scene_y = game->scene_model->region_height - game->camera_world_y;
    int scene_z = game->scene_model->region_z - game->camera_world_z;

    struct AABB aabb;
    render_model_frame(
        renderer->pixel_buffer,
        renderer->width,
        renderer->height,
        50,
        0,
        game->scene_model->yaw,
        0,
        scene_x,
        scene_y,
        scene_z,
        game->camera_pitch,
        game->camera_yaw,
        game->camera_roll,
        game->camera_fov,
        &aabb,
        game->scene_model->model,
        game->scene_model->lighting,
        game->scene_model->bounds_cylinder,
        NULL,
        NULL,
        NULL,
        game->textures_cache);
}

struct Renderer*
PlatformImpl_OSX_SDL2_Renderer_Soft3D_New(int width, int height)
{
    struct Renderer* renderer = (struct Renderer*)malloc(sizeof(struct Renderer));
    memset(renderer, 0, sizeof(struct Renderer));

    renderer->pixel_buffer = (int*)malloc(width * height * sizeof(int));
    if( !renderer->pixel_buffer )
    {
        printf("Failed to allocate pixel buffer\n");
        return NULL;
    }
    memset(renderer->pixel_buffer, 0, width * height * sizeof(int));

    renderer->width = width;
    renderer->height = height;

    return renderer;
}

void
PlatformImpl_OSX_SDL2_Renderer_Soft3D_Free(struct Renderer* renderer)
{
    free(renderer);
}

bool
PlatformImpl_OSX_SDL2_Renderer_Soft3D_Init(struct Renderer* renderer, struct Platform* platform)
{
    renderer->renderer = SDL_CreateRenderer(platform->window, -1, SDL_RENDERER_ACCELERATED);
    if( !renderer->renderer )
    {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        return false;
    }

    renderer->texture = SDL_CreateTexture(
        renderer->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        renderer->width,
        renderer->height);
    if( !renderer->texture )
    {
        printf("Failed to create texture\n");
        return false;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    if( !ImGui_ImplSDL2_InitForSDLRenderer(platform->window, renderer->renderer) )
    {
        printf("ImGui SDL2 init failed\n");
        return false;
    }
    if( !ImGui_ImplSDLRenderer2_Init(renderer->renderer) )
    {
        printf("ImGui Renderer init failed\n");
        return false;
    }

    return true;
}

void
PlatformImpl_OSX_SDL2_Renderer_Soft3D_Shutdown(struct Renderer* renderer)
{
    SDL_DestroyRenderer(renderer->renderer);
    renderer->renderer = NULL;
}

void
PlatformImpl_OSX_SDL2_Renderer_Soft3D_Render(
    struct Renderer* renderer, struct Game* game, struct GameGfxOpList* gfx_op_list)
{
    memset(renderer->pixel_buffer, 0, renderer->width * renderer->height * sizeof(int));
    SDL_RenderClear(renderer->renderer);

    render_game(renderer, game);

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        renderer->pixel_buffer,
        renderer->width,
        renderer->height,
        32,
        renderer->width * sizeof(int),
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000);

    // Copy the pixels into the texture
    int* pix_write = NULL;
    int _pitch_unused = 0;
    if( SDL_LockTexture(renderer->texture, NULL, (void**)&pix_write, &_pitch_unused) < 0 )
        return;

    int row_size = renderer->width * sizeof(int);
    int* src_pixels = (int*)surface->pixels;
    for( int src_y = 0; src_y < (renderer->height); src_y++ )
    {
        // Calculate offset in texture to write a single row of pixels
        int* row = &pix_write[(src_y * renderer->width)];
        // Copy a single row of pixels
        memcpy(row, &src_pixels[(src_y - 0) * renderer->width], row_size);
    }

    // Unlock the texture so that it may be used elsewhere
    SDL_UnlockTexture(renderer->texture);

    // Calculate destination rectangle to scale the texture to the current drawable size
    SDL_Rect dst_rect;
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = renderer->width;
    dst_rect.h = renderer->height;

    // Use bilinear scaling when copying texture to renderer
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); // Enable bilinear filtering

    // Calculate aspect ratio preserving dimensions
    float src_aspect = (float)renderer->width / renderer->height;
    float dst_aspect = (float)renderer->width / renderer->height;

    if( src_aspect > dst_aspect )
    {
        // Source is wider - fit to width
        dst_rect.w = renderer->width;
        dst_rect.h = (int)(renderer->width / src_aspect);
        dst_rect.x = 0;
        dst_rect.y = (renderer->height - dst_rect.h) / 2;
    }
    else
    {
        // Source is taller - fit to height
        dst_rect.h = renderer->height;
        dst_rect.w = (int)(renderer->height * src_aspect);
        dst_rect.y = 0;
        dst_rect.x = (renderer->width - dst_rect.w) / 2;
    }

    SDL_RenderCopy(renderer->renderer, renderer->texture, NULL, &dst_rect);

    SDL_FreeSurface(surface);

    SDL_RenderPresent(renderer->renderer);
}