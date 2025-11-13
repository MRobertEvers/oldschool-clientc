#include "platform_impl_emscripten_sdl2.h"

#include "platform_impl_emscripten_asyncio.u.cpp"

#include <SDL.h>
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include ImGui SDL backend to process events (C++)
#include "imgui_impl_sdl2.h"

extern "C" {

struct Platform*
PlatformImpl_Emscripten_SDL2_New(void)
{
    struct Platform* platform = (struct Platform*)malloc(sizeof(struct Platform));
    memset(platform, 0, sizeof(struct Platform));

    return platform;
}

void
PlatformImpl_Emscripten_SDL2_Free(struct Platform* platform)
{
    free(platform);
}

bool
PlatformImpl_Emscripten_SDL2_InitForSoft3D(
    struct Platform* platform,
    int canvas_width,
    int canvas_height,
    int max_canvas_width,
    int max_canvas_height)
{
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window = SDL_CreateWindow(
        "3D Raster - WebGL1",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        canvas_width,
        canvas_height,
        SDL_WINDOW_SHOWN |
            SDL_WINDOW_RESIZABLE); // Use SHOWN instead of OPENGL for Emscripten - context created
                                   // separately, RESIZABLE allows full-width canvas
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}

bool
PlatformImpl_Emscripten_SDL2_InitForWebGL1(
    struct Platform* platform, int canvas_width, int canvas_height)
{
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window = SDL_CreateWindow(
        "3D Raster - WebGL1",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        canvas_width,
        canvas_height,
        SDL_WINDOW_SHOWN |
            SDL_WINDOW_RESIZABLE); // Use SHOWN instead of OPENGL for Emscripten - context created
                                   // separately, RESIZABLE allows full-width canvas
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}

void
PlatformImpl_Emscripten_SDL2_Shutdown(struct Platform* platform)
{
    if( platform->window )
    {
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
    }
    SDL_Quit();
}

// clang-format off
EM_JS(
    void,
    emscripten_request_archive,
    ( int request_id, int table_id, int archive_id),
    {
        
        console.log(
            "emscripten_request_archive: request_id=",
            request_id,
            "table_id=",
            table_id,
            "archive_id=",
            archive_id);
        Module.requestArchive(request_id, table_id, archive_id);
    });

EM_JS(int, emscripten_request_archive_read, (void* buffer, int request_id), {
    console.log("emscripten_request_archive_read: request_id=", request_id);
    return Module.requestArchiveRead(buffer, request_id);
});
//  clang-format on


void
PlatformImpl_Emscripten_SDL2_PollEvents(struct Platform* platform, struct GameIO* input)
{
    uint64_t current_frame_time = SDL_GetTicks64();
    input->time_delta_accumulator_seconds +=
        (double)(current_frame_time - platform->last_frame_time_ticks) / 1000.0f;
    platform->last_frame_time_ticks = current_frame_time;


    struct GameIORequest* request_nullable = NULL;
    while( gameio_next(input, E_GAMEIO_STATUS_PENDING, &request_nullable) )
    {
        if( !request_nullable )
            break;

        
        switch( request_nullable->kind )
        {
        case E_GAMEIO_REQUEST_ARCHIVE_LOAD:
        {
            void* buffer = javascript_archive_request_new_buffer();
            request_nullable->sidecar_nullable = buffer;

            emscripten_request_archive(
                request_nullable->request_id,
                request_nullable->_archive_load.table_id,
                request_nullable->_archive_load.archive_id);
            request_nullable->status = E_GAMEIO_STATUS_WAIT;
            break;
        }
        }
    }

    int success = 0;
    request_nullable = NULL;
    while( gameio_next(input, E_GAMEIO_STATUS_WAIT, &request_nullable) )
    {

        if (request_nullable == NULL)
            break;

        switch( request_nullable->kind )
        {
        case E_GAMEIO_REQUEST_ARCHIVE_LOAD:
        {
            success = emscripten_request_archive_read(request_nullable->sidecar_nullable, request_nullable->request_id);
            printf("emscripten_request_archive_read: success=%d\n", success);
            if( success == 1 )
            {
                struct JavascriptArchiveRequest deserialized = {0};
                javascript_archive_request_deserialize(&deserialized, request_nullable->sidecar_nullable);
                assert(deserialized.magic == JAVASCRIPT_ARCHIVE_REQUEST_MAGIC);

                struct CacheArchive* archive = (struct CacheArchive*)malloc(sizeof(struct CacheArchive));
                archive->data = (char*)deserialized.data;
                archive->data_size = deserialized.size;
                archive->archive_id = deserialized.archive_id;
                archive->table_id = deserialized.table_id;
                archive->revision = 0;
                archive->file_count = 0;
                
                request_nullable->_archive_load.out_archive_nullable = archive;
                request_nullable->status = E_GAMEIO_STATUS_OK;
            }
            else if (success == -1)
            {
                request_nullable->status = E_GAMEIO_STATUS_ERROR;
            }

            if( success != 0 )
            {
                javascript_archive_request_free_buffer(request_nullable->sidecar_nullable);
                request_nullable->sidecar_nullable = NULL;
            }
            break;
        }
        }
    }


    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        // Forward events to ImGui for processing (mouse, keyboard, etc.)
        ImGui_ImplSDL2_ProcessEvent(&event);

        switch( event.type )
        {
        case SDL_QUIT:
            input->quit = 1;
            break;

        case SDL_KEYDOWN:
        {
            SDL_Keycode key = event.key.keysym.sym;
            switch( key )
            {
            case SDLK_w:
                input->w_pressed = 1;
                break;
            case SDLK_a:
                input->a_pressed = 1;
                break;
            case SDLK_s:
                input->s_pressed = 1;
                break;
            case SDLK_d:
                input->d_pressed = 1;
                break;
            case SDLK_q:
                input->q_pressed = 1;
                break;
            case SDLK_e:
                input->e_pressed = 1;
                break;
            case SDLK_SPACE:
                input->space_pressed = 1;
                break;
            case SDLK_UP:
                input->up_pressed = 1;
                break;
            case SDLK_DOWN:
                input->down_pressed = 1;
                break;
            case SDLK_LEFT:
                input->left_pressed = 1;
                break;
            case SDLK_RIGHT:
                input->right_pressed = 1;
                break;
            case SDLK_f:
                input->f_pressed = 1;
                break;
            case SDLK_r:
                input->r_pressed = 1;
                break;
            case SDLK_m:
                input->m_pressed = 1;
                break;
            case SDLK_n:
                input->n_pressed = 1;
                break;
            case SDLK_i:
                input->i_pressed = 1;
                break;
            case SDLK_k:
                input->k_pressed = 1;
                break;
            case SDLK_l:
                input->l_pressed = 1;
                break;
            case SDLK_j:
                input->j_pressed = 1;
                break;
            case SDLK_COMMA:
                input->comma_pressed = 1;
                break;
            case SDLK_PERIOD:
                input->period_pressed = 1;
                break;
            case SDLK_ESCAPE:
                input->quit = 1;
                break;
            default:
                break;
            }
            break;
        }

        case SDL_KEYUP:
        {
            SDL_Keycode key = event.key.keysym.sym;
            switch( key )
            {
            case SDLK_w:
                input->w_pressed = 0;
                break;
            case SDLK_a:
                input->a_pressed = 0;
                break;
            case SDLK_s:
                input->s_pressed = 0;
                break;
            case SDLK_d:
                input->d_pressed = 0;
                break;
            case SDLK_q:
                input->q_pressed = 0;
                break;
            case SDLK_e:
                input->e_pressed = 0;
                break;
            case SDLK_SPACE:
                input->space_pressed = 0;
                break;
            case SDLK_UP:
                input->up_pressed = 0;
                break;
            case SDLK_DOWN:
                input->down_pressed = 0;
                break;
            case SDLK_LEFT:
                input->left_pressed = 0;
                break;
            case SDLK_RIGHT:
                input->right_pressed = 0;
                break;
            case SDLK_f:
                input->f_pressed = 0;
                break;
            case SDLK_r:
                input->r_pressed = 0;
                break;
            case SDLK_m:
                input->m_pressed = 0;
                break;
            case SDLK_n:
                input->n_pressed = 0;
                break;
            case SDLK_i:
                input->i_pressed = 0;
                break;
            case SDLK_k:
                input->k_pressed = 0;
                break;
            case SDLK_l:
                input->l_pressed = 0;
                break;
            case SDLK_j:
                input->j_pressed = 0;
                break;
            case SDLK_COMMA:
                input->comma_pressed = 0;
                break;
            case SDLK_PERIOD:
                input->period_pressed = 0;
                break;
            default:
                break;
            }
            break;
        }

        default:
            break;
        }
    }
}

} // extern "C"
