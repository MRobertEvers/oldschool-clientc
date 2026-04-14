#ifndef TORI_RS_MINIMAP_U_C
#define TORI_RS_MINIMAP_U_C

#include "bmp.h"
#include "graphics/dash.h"
#include "graphics/dash_minimap.h"
#include "osrs/minimap.h"
#include "osrs/revconfig/uiscene.h"
#include "tori_rs.h"

#include <stdlib.h>
#include <string.h>

void
LibToriRS_WorldMinimapStaticRebuild(struct GGame* game)
{
    if( !game || !game->ui_scene )
        return;
    if( !game->world || !game->world->minimap )
        return;

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

    int id = uiscene_element_acquire(game->ui_scene, -1);
    if( id < 0 )
    {
        dashsprite_free(sp);
        return;
    }

    struct UISceneElement* el = uiscene_element_at(game->ui_scene, id);
    el->dash_sprites = (struct DashSprite**)malloc(sizeof(struct DashSprite*));
    if( !el->dash_sprites )
    {
        dashsprite_free(sp);
        uiscene_element_release(game->ui_scene, id);
        return;
    }
    el->dash_sprites[0] = sp;
    el->dash_sprites_count = 1;
    strncpy(el->name, "minimap_static", sizeof(el->name) - 1);
    el->name[sizeof(el->name) - 1] = '\0';

    int minimap_cmd = -1;
    for( int i = 0; i < game->ui_root_buffer->component_count; i++ )
    {
        if( game->ui_root_buffer->components[i].type == UIELEM_BUILTIN_MINIMAP )
        {
            minimap_cmd = i;
            break;
        }
    }
    if( minimap_cmd >= 0 )
    {
        game->ui_root_buffer->components[minimap_cmd].u.minimap.scene_id = id;
    }
}

#endif
