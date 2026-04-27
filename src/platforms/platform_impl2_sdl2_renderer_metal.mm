#if defined(__APPLE__)

#include "platforms/platform_impl2_sdl2_renderer_metal.h"

#import <QuartzCore/CAMetalLayer.h>
#include <SDL.h>

extern "C" {
#include "graphics/dash.h"
#include "osrs/game.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

Platform2_SDL2_Renderer_Metal*
PlatformImpl2_SDL2_Renderer_Metal_New(
    int width,
    int height)
{
    Platform2_SDL2_Renderer_Metal* renderer = new Platform2_SDL2_Renderer_Metal();
    renderer->width = width;
    renderer->height = height;
    return renderer;
}

void
PlatformImpl2_SDL2_Renderer_Metal_Free(Platform2_SDL2_Renderer_Metal* renderer)
{
    if( !renderer )
        return;
    if( renderer->trspk )
        TRSPK_Metal_Shutdown(renderer->trspk);
    if( renderer->metal_view )
        SDL_Metal_DestroyView(renderer->metal_view);
    delete renderer;
}

bool
PlatformImpl2_SDL2_Renderer_Metal_Init(
    Platform2_SDL2_Renderer_Metal* renderer,
    struct Platform2_SDL2* platform)
{
    if( !renderer || !platform || !platform->window )
        return false;
    renderer->platform = platform;
    renderer->metal_view = SDL_Metal_CreateView(platform->window);
    if( !renderer->metal_view )
        return false;
    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(renderer->metal_view);
    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_Metal_GetDrawableSize(platform->window, &drawable_w, &drawable_h);
    renderer->width = drawable_w;
    renderer->height = drawable_h;
    renderer->trspk = TRSPK_Metal_Init((__bridge void*)layer, (uint32_t)drawable_w, (uint32_t)drawable_h);
    renderer->ready = renderer->trspk != nullptr;
    return renderer->ready;
}

void
PlatformImpl2_SDL2_Renderer_Metal_Render(
    Platform2_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->ready || !renderer->trspk || !game || !render_command_buffer )
        return;
    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_Metal_GetDrawableSize(renderer->platform->window, &drawable_w, &drawable_h);
    if( drawable_w != renderer->width || drawable_h != renderer->height )
    {
        renderer->width = drawable_w;
        renderer->height = drawable_h;
        TRSPK_Metal_Resize(renderer->trspk, (uint32_t)drawable_w, (uint32_t)drawable_h);
    }

    TRSPK_ResourceCache* cache = TRSPK_Metal_GetCache(renderer->trspk);
    TRSPK_Batch32* staging = TRSPK_Metal_GetBatchStaging(renderer->trspk);
    TRSPK_MetalEventContext events = {};
    events.renderer = renderer->trspk;
    events.cache = cache;
    events.staging = staging;
    events.current_batch_id = renderer->current_model_batch_id;
    events.batch_active = renderer->current_model_batch_active;
    LibToriRS_FrameBegin(game, render_command_buffer);
    TRSPK_Metal_FrameBegin(renderer->trspk);

    struct ToriRSRenderCommand cmd = { 0 };
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, true) )
    {
        switch( cmd.kind )
        {
        case TORIRS_GFX_RES_TEX_LOAD:
            trspk_metal_event_tex_load(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_BEGIN:
            trspk_metal_event_batch3d_begin(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_MODEL_ADD:
            trspk_metal_event_batch3d_model_add(&events, &cmd);
            break;
        case TORIRS_GFX_RES_ANIM_LOAD:
        case TORIRS_GFX_BATCH3D_ANIM_ADD:
            trspk_metal_event_batch3d_anim_add(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_END:
            trspk_metal_event_batch3d_end(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_CLEAR:
            trspk_metal_event_batch3d_clear(&events, &cmd);
            break;
        case TORIRS_GFX_DRAW_MODEL:
            renderer->sorted_indices.resize(cmd._model_draw.model ? (size_t)dashmodel_face_count(cmd._model_draw.model) * 3u : 0u);
            trspk_metal_event_draw_model(
                &events,
                game,
                &cmd,
                renderer->sorted_indices.data(),
                (uint32_t)renderer->sorted_indices.size());
            break;
        case TORIRS_GFX_STATE_BEGIN_3D:
            trspk_metal_event_state_begin_3d(&events, &cmd);
            break;
        case TORIRS_GFX_STATE_CLEAR_RECT:
            trspk_metal_event_state_clear_rect(&events, &cmd);
            break;
        case TORIRS_GFX_STATE_END_3D:
            trspk_metal_event_state_end_3d(&events, game, (double)(SDL_GetTicks64() / 20u));
            break;
        default:
            break;
        }
    }
    renderer->current_model_batch_id = events.current_batch_id;
    renderer->current_model_batch_active = events.batch_active;
    TRSPK_Metal_FrameEnd(renderer->trspk);
    LibToriRS_FrameEnd(game);
}

#endif
