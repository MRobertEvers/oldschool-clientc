#include "rs_component_gfx.h"

#include "bmp.h"
#include "graphics/dash.h"
#include "osrs/dash_utils.h"
#include "osrs/game.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/scene2.h"

#include <string.h>

static uint64_t
rs_model_cache_key_u64(struct Scene2* scene2, struct Scene2Element const* element)
{
    if( !element || !scene2 )
        return 0;
    int element_id = scene2_element_id(scene2, element);
    return ((uint64_t)(uint32_t)element_id << 24) |
           ((uint64_t)scene2_element_active_anim_id(element) << 8) |
           (uint64_t)scene2_element_active_frame(element);
}

static void
queue_sprite_draw(
    struct ToriRSRenderCommandBuffer* buf,
    struct DashSprite* sprite,
    int x,
    int y)
{
    if( !buf || !sprite )
        return;
    int src_bb_x = 0;
    int src_bb_y = 0;
    int src_bb_w = sprite->width;
    int src_bb_h = sprite->height;
    if( sprite->crop_width > 0 && sprite->crop_height > 0 )
    {
        src_bb_x = sprite->crop_x;
        src_bb_y = sprite->crop_y;
        src_bb_w = sprite->crop_width;
        src_bb_h = sprite->crop_height;
    }
    int src_anchor_x = sprite->crop_x;
    int src_anchor_y = sprite->crop_y;

    LibToriRS_RenderCommandBufferAddCommand(
        buf,
        (struct ToriRSRenderCommand){
            .kind = TORIRS_GFX_SPRITE_DRAW,
            ._sprite_draw = {
                .sprite = sprite,
                .dst_bb_x = x,
                .dst_bb_y = y,
                .src_anchor_x = src_anchor_x,
                .src_anchor_y = src_anchor_y,
                .rotation_r2pi2048 = 0,
                .src_bb_x = src_bb_x,
                .src_bb_y = src_bb_y,
                .src_bb_w = src_bb_w,
                .src_bb_h = src_bb_h,
                .rotated = false,
            },
        });
}

bool
rs_gfx_graphic_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct ToriRSRenderCommandBuffer* queued_commands,
    int cur)
{
    if( !game || !component || !game->ui_scene || !queued_commands )
        return true;
    int sid = component->u.rs_graphic.scene_id;
    int ai = component->u.rs_graphic.atlas_index;
    if( sid < 0 )
        return true;
    struct UISceneElement* el = uiscene_element_at(game->ui_scene, sid);
    if( !el || !el->dash_sprites || ai < 0 || ai >= el->dash_sprites_count )
        return true;
    struct DashSprite* sp = el->dash_sprites[ai];
    if( !sp )
        return true;
    queue_sprite_draw(queued_commands, sp, component->position.x, component->position.y);
    return true;
}

bool
rs_gfx_text_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct ToriRSRenderCommandBuffer* queued_commands)
{
    if( !game || !component || !game->ui_scene || !queued_commands )
        return true;
    int fid = component->u.rs_text.font_id;
    char const* text = component->u.rs_text.text;
    if( fid < 0 || !text || text[0] == '\0' )
        return true;
    struct DashPixFont* font = uiscene_font_get(game->ui_scene, fid);
    if( !font )
        return true;
    int draw_x = component->position.x;
    int draw_y = component->position.y;
    if( component->u.rs_text.center )
    {
        int tw = dashfont_text_width(font, (uint8_t*)text);
        draw_x = component->position.x + (component->position.width / 2) - (tw / 2);
    }

    LibToriRS_RenderCommandBufferAddCommand(
        queued_commands,
        (struct ToriRSRenderCommand){
            .kind = TORIRS_GFX_FONT_DRAW,
            ._font_draw = {
                .font = font,
                .text = (const uint8_t*)text,
                .x = draw_x,
                .y = draw_y,
                .color_rgb = component->u.rs_text.color,
            },
        });
    (void)component->u.rs_text.shadowed;
    return true;
}

bool
rs_gfx_model_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct ToriRSRenderCommandBuffer* queued_commands,
    bool project_models)
{
    if( !game || !component || !game->world || !game->world->scene2 || !queued_commands )
        return true;
    int eid = component->u.rs_model.scene2_element_id;
    if( eid < 0 )
        return true;
    struct Scene2Element* se = scene2_element_at(game->world->scene2, eid);
    if( !se )
        return true;
    struct DashModel* mod = scene2_element_dash_model(se);
    struct DashPosition* sepos = scene2_element_dash_position(se);
    if( !mod || !sepos )
        return true;

    struct DashPosition position = { 0 };
    memcpy(&position, sepos, sizeof(struct DashPosition));
    position.x = position.x - game->camera_world_x;
    position.y = position.y - game->camera_world_y;
    position.z = position.z - game->camera_world_z;

    if( project_models )
    {
        int cull = dash3d_project_model(
            game->sys_dash, mod, &position, game->view_port, game->camera);
        if( cull != DASHCULL_VISIBLE )
            return true;
    }

    {
        struct ToriRSRenderCommand cmd = { 0 };
        cmd.kind = TORIRS_GFX_MODEL_DRAW;
        cmd._model_draw.model = mod;
        cmd._model_draw.model_key = rs_model_cache_key_u64(game->world->scene2, se);
        cmd._model_draw.model_id = -1;
        memcpy(&cmd._model_draw.position, &position, sizeof(struct DashPosition));
        LibToriRS_RenderCommandBufferAddCommand(queued_commands, cmd);
    }
    return true;
}

bool
rs_gfx_inv_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct ToriRSRenderCommandBuffer* queued_commands)
{
    if( !game || !component || !game->ui_scene || !queued_commands || !game->inv_pool )
        return true;
    int inv_i = component->u.rs_inv.inv_index;
    if( inv_i < 0 || inv_i >= game->inv_pool->count )
        return true;
    struct UIInventory* inv = &game->inv_pool->inventories[inv_i];
    int cols = component->u.rs_inv.cols;
    int rows = component->u.rs_inv.rows;
    int margin_x = component->u.rs_inv.margin_x;
    int margin_y = component->u.rs_inv.margin_y;
    if( cols <= 0 )
        cols = 4;
    int base_x = component->position.x;
    int base_y = component->position.y;

    int i = 0;
    for( int sx = 0; sx < cols; sx++ )
    {
        for( int sy = 0; sy < rows; sy++, i++ )
        {
            int col = i % cols;
            int row = i / cols;
            int slot_x = base_x + col * (margin_x + 32);
            int slot_y = base_y + row * (margin_y + 32);
            if( i < UI_INV_SLOT_OFFSET_MAX )
            {
                slot_x += component->u.rs_inv.inv_slot_offset_x[i];
                slot_y += component->u.rs_inv.inv_slot_offset_y[i];
            }

            struct UIInventoryItem* it = &inv->items[i];
            if( it->obj_id > 0 )
            {
                if( it->scene_id < 0 )
                    continue;
                struct UISceneElement* el = uiscene_element_at(game->ui_scene, it->scene_id);
                if( !el || !el->dash_sprites )
                    continue;
                int ai = it->atlas_index;
                if( ai < 0 || ai >= el->dash_sprites_count )
                    continue;
                struct DashSprite* sp = el->dash_sprites[ai];
                if( !sp )
                    continue;

                queue_sprite_draw(queued_commands, sp, slot_x, slot_y);
            }
            else if(
                i < UI_INV_SLOT_OFFSET_MAX && component->u.rs_inv.inv_slot_bg_scene_id[i] >= 0 )
            {
                int bg_sid = component->u.rs_inv.inv_slot_bg_scene_id[i];
                int bg_ai = component->u.rs_inv.inv_slot_bg_atlas_index[i];
                struct UISceneElement* bg_el = uiscene_element_at(game->ui_scene, bg_sid);
                if( bg_el && bg_el->dash_sprites && bg_ai >= 0 &&
                    bg_ai < bg_el->dash_sprites_count )
                {
                    struct DashSprite* bg_sp = bg_el->dash_sprites[bg_ai];
                    if( bg_sp )
                        queue_sprite_draw(queued_commands, bg_sp, slot_x, slot_y);
                }
            }
        }
    }

    return true;
}
