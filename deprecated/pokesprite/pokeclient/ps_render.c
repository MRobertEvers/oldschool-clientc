#include "ps_render.h"

#include "app.h"
#include "game/rs_cs2_host.h"
#include "platform/platform_renderer_soft3d.h"
#include "ps_world.h"
#include "render/torirs_frame.h"
#include "render/torirs_pick.h"
#include "render/torirs_render.h"
#include "toridraw_font.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <stdlib.h>

/* The 3D client's own clear colour, so a frame with nothing drawn looks like
 * one (see the gray-screen note: 0xFF202428 is an empty committed frame). */
#define PS_CLEAR 0xFF202428

void
PS_Render(
    struct App* app,
    struct PS_World* world,
    int* pixels,
    int width,
    int height,
    uint64_t now_ms)
{
    assert(app);
    assert(world);
    assert(pixels);
    assert(app->soft);

    /* Booting: the loading bar is App_Render's, unchanged. */
    if( App_IsBooting(app, NULL) )
    {
        App_Render(app, pixels, width, height);
        return;
    }

    struct ToriRS_Frame frame;
    App_NoteFrameDrawn(app);
    ToriPlatform_Renderer_Soft3D_Init(app->soft, app->scene, pixels, width, height);
    ToriPlatform_Renderer_Soft3D_SetLayout(app->soft, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    ToriPlatform_Renderer_Soft3D_SetInterfaceScaleMode(app->soft, RS_CS2Host_UiScaleMode(&app->host));
    if( !App_BuildFrame(app, &frame, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) )
    {
        App_Render(app, pixels, width, height);
        return;
    }

    for( int i = 0; i < width * height; i++ )
        pixels[i] = (int)PS_CLEAR;

    /*
     * The 3D client's projected world overlays -- health bars, hitsplats,
     * overhead text, the hover tile and destination markers -- are placed by
     * its camera, which this client does not have. They are blanked for this
     * frame only and put back after it, so the app never sees the edit.
     * TODO(pokeclient): draw them at the 2D actor positions instead.
     */
    static enum UITreeEmitKind* saved;
    static int saved_cap;
    int const ncmd = app->emit.count;
    if( saved_cap < ncmd )
    {
        free(saved);
        saved = (enum UITreeEmitKind*)malloc(sizeof(*saved) * (size_t)ncmd);
        assert(saved);
        saved_cap = ncmd;
    }
    for( int i = 0; i < ncmd; i++ )
    {
        saved[i] = app->emit.cmds[i].kind;
        if( saved[i] == UITREE_EMIT_ENTITY_OVERLAY )
            app->emit.cmds[i].kind = UITREE_EMIT_NONE;
    }
    ToriDraw2D_FontSetOutputScale(width, UITREE_LAYOUT_ROOT_W, height, UITREE_LAYOUT_ROOT_H);
    ToriRS_FrameBegin(&frame);
    {
        struct ToriRS_RenderCommand cmd;
        int in_world = 0;
        while( ToriRS_FrameNextCommand(&frame, &cmd) )
        {
            if( cmd.kind == TORIRSRC_BEGIN_3D )
            {
                /* Centred on the viewport's own rect: the 3D projection's
                 * centre is the 3D camera's business. */
                struct ToriDraw_ViewPort const* vp = &cmd.u.begin_3d.view_port;
                PS_WorldDraw(world, app, pixels, width, vp->clip_left, vp->clip_top,
                             vp->clip_right, vp->clip_bottom, (vp->clip_left + vp->clip_right) / 2,
                             (vp->clip_top + vp->clip_bottom) / 2, now_ms);
                in_world = 1;
                continue;
            }
            if( cmd.kind == TORIRSRC_END_3D )
            {
                in_world = 0;
                continue;
            }
            /* Nothing of the 3D pass is drawn: its models, and the overlays it
             * projects (the hover tile), are the 3D client's. */
            if( in_world )
                continue;
            ToriPlatform_Renderer_Soft3D_Execute(app->soft, &cmd);
        }
    }
    ToriRS_FrameEnd(&frame);
    ToriDraw2D_FontSetOutputScale(1, 1, 1, 1);
    for( int i = 0; i < ncmd; i++ )
        app->emit.cmds[i].kind = saved[i];

    /* World picking, where App_Render does it: the 3D client learns what is
     * under the mouse from its raster; this one asks the 2D world. */
    if( app->world_view_valid && app->frame_view->world_mouse_in_viewport )
    {
        static struct ToriRS_PickHits hits;
        PS_WorldPick(world, app->frame_view->world_mouse_x, app->frame_view->world_mouse_y, &hits);
        App_PickFinish(app, &hits);
    }
}
