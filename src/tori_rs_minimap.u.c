#ifndef TORI_RS_MINIMAP_U_C
#define TORI_RS_MINIMAP_U_C

#include "bmp.h"
#include "graphics/dash.h"
#include "graphics/dash_minimap.h"
#include "osrs/minimap.h"
#include "osrs/revconfig/uiscene.h"
#include "tori_rs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
LibToriRS_WorldMinimapStaticRebuild(struct GGame* game)
{
    if( !game || !game->ui_scene )
        return;
    if( !game->world || !game->world->minimap )
        return;
    if( !game->ui_root_buffer )
        return;

    int minimap_cmd = -1;
    for( int i = 0; i < game->ui_root_buffer->component_count; i++ )
    {
        if( game->ui_root_buffer->components[i].type == UIELEM_BUILTIN_MINIMAP )
        {
            minimap_cmd = i;
            break;
        }
    }
    if( minimap_cmd < 0 )
    {
        fprintf(
            stderr,
            "[minimap] LibToriRS_WorldMinimapStaticRebuild: skipped (no UIELEM_BUILTIN_MINIMAP in "
            "ui_root_buffer)\n");
        return;
    }

    struct Minimap* mm = game->world->minimap;
    struct MinimapRenderCommandBuffer* tile_cmds =
        minimap_commands_new(mm->width * mm->height + 64);
    if( !tile_cmds )
        return;

    minimap_render_static_tiles(mm, 0, 0, mm->width, mm->height, tile_cmds);

    const int pw = mm->width * 4;
    const int ph = mm->height * 4;
    int* pixels = (int*)malloc((size_t)pw * (size_t)ph * sizeof(int));
    if( !pixels )
    {
        minimap_commands_free(tile_cmds);
        return;
    }
    memset(pixels, 0, (size_t)pw * (size_t)ph * sizeof(int));

    dash_minimap_raster_tile_commands(
        mm, tile_cmds, 0, 0, mm->width, mm->height, pixels, pw, pw, ph);
    minimap_commands_free(tile_cmds);

    struct DashSprite* sp = dashsprite_new_from_argb_owned((uint32_t*)pixels, pw, ph);
    if( !sp )
    {
        free(pixels);
        return;
    }

    struct DashSprite** sprites_array =
        (struct DashSprite**)malloc(sizeof(struct DashSprite*));
    if( !sprites_array )
    {
        dashsprite_free(sp);
        return;
    }
    sprites_array[0] = sp;

    int id = uiscene_element_acquire_with_sprites(
        game->ui_scene, -1, sprites_array, 1, false, "minimap_static");
    if( id < 0 )
    {
        dashsprite_free(sp);
        free(sprites_array);
        return;
    }

    game->ui_root_buffer->components[minimap_cmd].u.minimap.scene_id = id;

    fprintf(
        stderr,
        "[minimap] created: uiscene_element_id=%d sprite=%dx%d pixels=%dx%d scene_id bound to "
        "uitree[%d]\n",
        id,
        sp->width,
        sp->height,
        pw,
        ph,
        minimap_cmd);
}

#endif
