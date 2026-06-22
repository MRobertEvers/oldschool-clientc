/*
 * Single-file model render test using rscache + toridraw unity builds.
 *
 * Usage: ./model_render_test [cache_dir] [model_id]
 *   cache_dir  default: ../../cache  (dat2 cache at repo root)
 *   model_id   default: 1234
 */

#include "osrs/rscache/rscache.h"
#include "toriauxlib/toriauxlib.h"
#include "toridraw/toridraw.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    VIEW_W = 765,
    VIEW_H = 503,
    DEFAULT_MODEL_ID = 1947,
    CLEAR_COLOR = 0x202020,
};

/* Textured faces need cache textures registered on the scene. For this smoke test,
 * drop texture metadata so faces render as lit gouraud/flat geometry. */
static void
strip_model_textures(struct ToriDraw_Model* model)
{
    if( !model )
        return;

    free(model->face_textures);
    free(model->face_texture_coords);
    free(model->textured_p_coordinate);
    free(model->textured_m_coordinate);
    free(model->textured_n_coordinate);

    model->face_textures = NULL;
    model->face_texture_coords = NULL;
    model->textured_p_coordinate = NULL;
    model->textured_m_coordinate = NULL;
    model->textured_n_coordinate = NULL;
    model->textured_face_count = 0;
}

static void
fill_background(
    int* pixel_buffer,
    size_t pixel_count,
    int rgb)
{
    for( size_t i = 0; i < pixel_count; i++ )
        pixel_buffer[i] = rgb;
}

static void
present_frame(
    SDL_Renderer* sdl_renderer,
    SDL_Texture* texture,
    int* pixel_buffer,
    int width,
    int height)
{
    int* pix_write = NULL;
    int texture_pitch = 0;
    if( SDL_LockTexture(texture, NULL, (void**)&pix_write, &texture_pitch) < 0 )
    {
        fprintf(stderr, "SDL_LockTexture failed: %s\n", SDL_GetError());
        return;
    }

    int texture_w = texture_pitch / (int)sizeof(int);

    for( int y = 0; y < height; y++ )
    {
        int* dst_row = &pix_write[y * texture_w];
        int* src_row = &pixel_buffer[y * width];
        for( int x = 0; x < width; x++ )
            dst_row[x] = src_row[x] | 0xFF000000;
    }

    SDL_UnlockTexture(texture);

    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, texture, NULL, NULL);
    SDL_RenderPresent(sdl_renderer);
}

int
main(
    int argc,
    char* argv[])
{
    const char* cache_dir = "../../cache";
    int model_id = DEFAULT_MODEL_ID;

    if( argc > 1 )
        cache_dir = argv[1];
    if( argc > 2 )
        model_id = atoi(argv[2]);

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("Loading cache from %s, model %d\n", cache_dir, model_id);

    struct RSCacheDat2Disk* cache = RSCacheDat2Disk_NewFromDirectory(cache_dir);
    if( !cache )
    {
        fprintf(stderr, "Failed to open cache: %s\n", cache_dir);
        return 1;
    }

    struct RSCacheDat2A_Model* cache_model = RSCacheDat2A_ModelNewFromCache(cache, model_id);
    if( !cache_model )
    {
        fprintf(stderr, "Failed to load model %d from cache\n", model_id);
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    struct ToriDraw_Model* td_model = ToriDraw_ModelNewFromCacheModel(cache_model);
    RSCacheDat2A_ModelFree(cache_model);
    if( !td_model )
    {
        fprintf(stderr, "Failed to convert model to ToriDraw\n");
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    struct ToriDraw_ModelHandle model_handle = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td_model,
    };

    ToriDraw_Init();

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL);
    if( !scene )
    {
        fprintf(stderr, "ToriDraw_SceneNew failed\n");
        ToriDraw_ModelFree(td_model);
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    ToriDraw_LightModelDefault(model_handle, 0, 0);
    strip_model_textures(td_model);

    struct ToriDraw_ViewPort view_port = {
        .width = VIEW_W,
        .height = VIEW_H,
        .stride = VIEW_W,
        .x_center = VIEW_W / 2,
        .y_center = VIEW_H / 2,
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = VIEW_W,
        .clip_bottom = VIEW_H,
    };

    struct ToriDraw_Camera camera = {
        .fov_rpi2048 = 512,
        .near_plane_z = 50,
        .pitch = 0,
        .yaw = 0,
        .roll = 0,
    };

    struct ToriDraw_Position position = {
        .x = 0,
        .y = 0,
        .z = 300,
        .pitch = 0,
        .yaw = 0,
        .roll = 0,
    };

    if( td_model->bounds_cylinder )
        position.y = -(td_model->bounds_cylinder->min_y + td_model->bounds_cylinder->max_y) / 2;

    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        ToriDraw_SceneFree(scene);
        ToriDraw_ModelFree(td_model);
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "model_render_test",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        VIEW_W,
        VIEW_H,
        SDL_WINDOW_SHOWN);
    if( !window )
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        ToriDraw_SceneFree(scene);
        ToriDraw_ModelFree(td_model);
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    SDL_Renderer* sdl_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if( !sdl_renderer )
        sdl_renderer = SDL_CreateRenderer(window, -1, 0);
    if( !sdl_renderer )
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        ToriDraw_SceneFree(scene);
        ToriDraw_ModelFree(td_model);
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(
        sdl_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, VIEW_W, VIEW_H);
    if( !texture )
    {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(sdl_renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        ToriDraw_SceneFree(scene);
        ToriDraw_ModelFree(td_model);
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    size_t const pixel_count = (size_t)VIEW_W * (size_t)VIEW_H;
    int* pixel_buffer = (int*)calloc(pixel_count, sizeof(int));
    if( !pixel_buffer )
    {
        fprintf(stderr, "Failed to allocate pixel buffer\n");
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(sdl_renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        ToriDraw_SceneFree(scene);
        ToriDraw_ModelFree(td_model);
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    printf(
        "Rendering model %d (%d verts, %d faces). Close window or press ESC to quit.\n",
        model_id,
        td_model->vertex_count,
        td_model->face_count);

    bool running = true;
    int frame = 0;
    while( running )
    {
        SDL_Event event;
        while( SDL_PollEvent(&event) )
        {
            if( event.type == SDL_QUIT )
                running = false;
            else if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE )
                running = false;
        }

        fill_background(pixel_buffer, pixel_count, CLEAR_COLOR);

        position.yaw = ToriDraw_AddAngle(position.yaw, 4);

        int cull =
            ToriDraw_RenderModel1Project(model_handle, scene, &position, &view_port, &camera);
        if( cull == TORIDRAW_CULL_VISIBLE )
        {
            int face_count = ToriDraw_RenderModel2SortFaces(model_handle, scene);
            if( face_count > 0 )
                ToriDraw_RenderModel3Raster(scene, &view_port, &camera, pixel_buffer, false);
            else if( frame == 0 )
                fprintf(stderr, "warning: model visible but 0 faces sorted\n");
        }
        else if( frame == 0 )
        {
            fprintf(stderr, "warning: model culled (code %d)\n", cull);
        }

        if( frame == 0 )
        {
            int lit_pixels = 0;
            for( size_t i = 0; i < pixel_count; i++ )
            {
                if( pixel_buffer[i] != CLEAR_COLOR )
                    lit_pixels++;
            }
            printf("frame 0: %d non-background pixels\n", lit_pixels);
        }

        present_frame(sdl_renderer, texture, pixel_buffer, VIEW_W, VIEW_H);
        SDL_Delay(16);
        frame++;
    }

    free(pixel_buffer);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    ToriDraw_SceneFree(scene);
    ToriDraw_ModelFree(td_model);
    RSCacheDat2Disk_Free(cache);

    return 0;
}
