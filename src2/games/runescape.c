#include "runescape.h"

#include "../ioqueue/libtorirs_io.h"
#include "../runescape/appearance.h"
#include "../runescape/player_body.h"
#include "../toriauxlib/c/toriauxlibcache.h"
#include "../toriauxlib/c/toriauxlibcache_submit.h"
#include "../toriauxlib/core/toriauxlibcore.h"
#include "../toriauxlib/td/toriauxlibtd.h"
#include "../toriauxlib/vm/toriauxlibvm.h"
#include "../ui/ui_click.h"
#include "../ui/ui_chat_minimenu.h"
#include "../vm/cs2vm.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "ui/ui_behavior.h"
#include "ui/ui_debug.h"
#include "ui/ui_input_adapter.h"
#include "ui/ui_scroll.h"
#include "ui/rs_inv_container.h"
#include "ui/ui_inv_data_service.h"
#include "ui/ui_inv_slot_view.h"
#include "toriauxlib/core/tasks/instance_revconfig_inv_bind.h"
#include "../world/heightmap.h"
#include "../world/minimap.h"
#include "../world/world_builder.h"
#include "3rd/minipt.h"
#include "osrs/colors.h"
#include "osrs/painters.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_animation.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_math.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_sprite.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNESCAPE_CAMERA_MOVEMENT_SPEED 70
#define RUNESCAPE_INV_TEXT_CELL_W 115
#define RUNESCAPE_INV_TEXT_CELL_H 12

static char const*
GameRunescape_ObjDisplayName(
    struct GameRunescape* game,
    int obj_id)
{
    if( !game || obj_id <= 0 )
        return NULL;

    struct ToriAuxLibCore_Objtype* obj = game->core
                                             ? ToriAuxLibCore_ObjtypeGet(game->core, obj_id)
                                             : NULL;
    if( !obj || obj->name[0] == '\0' )
        return "item";
    return obj->name;
}

static int
GameRunescape_UIInvGridSlotLimit(struct StaticUIComponent const* component)
{
    if( !component )
        return 0;

    int cols = 0;
    int rows = 0;
    if( component->type == UIELEM_INV_GRID )
    {
        cols = component->u.inv_grid.cols > 0 ? component->u.inv_grid.cols : 4;
        rows = component->u.inv_grid.rows > 0 ? component->u.inv_grid.rows : 7;
    }
    else if( component->type == UIELEM_RS_INV_TEXT )
    {
        cols = component->u.rs_inv_text.cols > 0 ? component->u.rs_inv_text.cols : 1;
        rows = component->u.rs_inv_text.rows > 0 ? component->u.rs_inv_text.rows : 1;
    }
    else
        return 0;

    return cols * rows;
}

static int
rs_cs2_get_varp(
    void* ud,
    int id)
{
    return ToriAuxLibVM_GetVarp(ud, id);
}

static int
rs_cs2_get_varbit(
    void* ud,
    int id)
{
    return ToriAuxLibVM_GetVarbit(ud, id);
}

static void
rs_ui_host_fill_cs2vm_state(
    struct ToriAuxLibVM* vm,
    struct CS2VM_State* out)
{
    if( !out )
        return;
    memset(out, 0, sizeof(*out));
    if( !vm )
        return;
    out->get_varp = rs_cs2_get_varp;
    out->get_varbit = rs_cs2_get_varbit;
    out->ud = vm;
}

static bool
rs_ui_host_is_active(
    void* user,
    struct StaticUIComponent const* component)
{
    struct GameRunescape* game = user;
    if( !game || !game->vm || !component )
        return false;

    struct CS2VM_State cs2_state;
    rs_ui_host_fill_cs2vm_state(game->vm, &cs2_state);
    return ToriAuxLibVM_IsActive(game->vm, game->cs2vm, &cs2_state, component);
}

static void
rs_ui_host_apply_button_click(
    void* user,
    struct StaticUIComponent const* component)
{
    struct GameRunescape* game = user;
    if( !game || !game->vm )
        return;

    ToriAuxLibVM_ApplyButtonClickOptimistic(game->vm, (struct StaticUIComponent*)component);

    if( game->ui_tree )
        uitree_mark_all_dirty(game->ui_tree);

    if( !game->core || !game->cs2vm || component->component_id < 0 )
        return;

    struct ToriAuxLibCache* cache = game->td ? ToriAuxLibTD_C(game->td) : NULL;
    struct ToriAuxLibCore_Component* core_comp =
        ToriAuxLibCore_ComponentGet(game->core, component->component_id);
    if( !core_comp )
        return;

    struct UITreeBehaviorHost host;
    ui_input_adapter_init_behavior_host_ex(&host, game->vm, game->cs2vm);
    uitree_behavior_run_hook(
        &host, game->core, cache, core_comp, UITREE_BEHAVIOR_HOOK_ON_CLICK);
}

static int
rs_ui_host_eval_text_placeholder(
    void* user,
    struct StaticUIComponent const* component,
    int script_idx)
{
    struct GameRunescape* game = user;
    if( !game || !game->vm )
        return 0;

    /* Mirror Client.ts getIfVar: skip eval when scripts are absent or out of range. */
    if( !component->behavior.scripts || script_idx < 0 ||
        script_idx >= component->behavior.scripts_count ||
        !component->behavior.scripts[script_idx] )
        return 0;

    struct CS2VM_State cs2_state;
    rs_ui_host_fill_cs2vm_state(game->vm, &cs2_state);
    return ToriAuxLibVM_EvalScript(
        game->vm, game->cs2vm, &cs2_state, (struct StaticUIComponent*)component, script_idx);
}

static int
rs_ui_host_get_selected_tab(void* user)
{
    struct GameRunescape* game = user;
    return game ? game->selected_tab : 0;
}

static void
rs_ui_host_set_selected_tab(
    void* user,
    int tabno)
{
    struct GameRunescape* game = user;
    if( game )
        game->selected_tab = tabno;
}

static int
rs_ui_host_get_camera_yaw(void* user)
{
    struct GameRunescape* game = user;
    return game && game->camera ? ToriDraw_NormalizeAngle(game->camera->yaw) : 0;
}

static void
rs_ui_host_get_minimap_anchor(
    void* user,
    int* out_src_anchor_x,
    int* out_src_anchor_y)
{
    struct GameRunescape* game = user;
    if( !out_src_anchor_x || !out_src_anchor_y )
        return;
    *out_src_anchor_x = 0;
    *out_src_anchor_y = 0;
    if( !game || !game->camera_position || !game->world || !game->world->minimap ||
        game->world_map_w <= 0 || game->world_map_h <= 0 )
        return;

    struct Minimap* mm = game->world->minimap;
    if( mm->width <= 0 || mm->height <= 0 )
        return;

    minimap_compute_camera_src_anchor(
        game->camera_position->x,
        game->camera_position->z,
        game->world_map_w,
        game->world_map_h,
        mm->width,
        mm->height,
        out_src_anchor_x,
        out_src_anchor_y);
}

static int
rs_ui_host_get_world_map_size(
    void* user,
    int* out_w,
    int* out_h)
{
    struct GameRunescape* game = user;
    if( out_w )
        *out_w = game ? game->world_map_w : 0;
    if( out_h )
        *out_h = game ? game->world_map_h : 0;
    return game ? 0 : -1;
}

static bool
rs_ui_host_scene_sprite_has(
    void* user,
    int scene_id)
{
    struct GameRunescape* game = user;
    return game && game->scene && ToriDraw_SceneSpriteHas(game->scene, scene_id);
}

static bool
rs_ui_host_scene_font_has(
    void* user,
    int font_id)
{
    struct GameRunescape* game = user;
    return game && game->scene && ToriDraw_SceneFontHas(game->scene, font_id);
}

static void
GameRunescape_AssertSceneFontReady(
    struct GameRunescape* game,
    int font_id,
    char const* context)
{
    if( font_id < 0 )
    {
        fprintf(stderr,
            "GameRunescape: font_id unset (context=%s)\n",
            context ? context : "(null)");
        assert(font_id >= 0);
    }
    if( !game || !game->scene )
    {
        fprintf(stderr,
            "GameRunescape: missing scene for font_id=%d (context=%s, game=%p)\n",
            font_id, context ? context : "(null)", (void*)game);
        assert(game && game->scene);
    }
    if( !ToriDraw_SceneFontHas(game->scene, font_id) )
    {
        fprintf(stderr,
            "GameRunescape: scene font not loaded font_id=%d (context=%s)\n",
            font_id, context ? context : "(null)");
        assert(ToriDraw_SceneFontHas(game->scene, font_id));
    }
}

static void
GameRunescape_AssertSceneSpriteReady(
    struct GameRunescape* game,
    int scene_id,
    int atlas_index,
    char const* context)
{
    if( scene_id < 0 )
    {
        fprintf(stderr,
            "GameRunescape: scene_id unset (context=%s)\n",
            context ? context : "(null)");
        assert(scene_id >= 0);
    }
    if( !game || !game->scene )
    {
        fprintf(stderr,
            "GameRunescape: missing scene for scene_id=%d (context=%s, game=%p)\n",
            scene_id, context ? context : "(null)", (void*)game);
        assert(game && game->scene);
    }
    if( !ToriDraw_SceneSpriteHas(game->scene, scene_id) )
    {
        fprintf(stderr,
            "GameRunescape: scene sprite not loaded scene_id=%d (context=%s)\n",
            scene_id, context ? context : "(null)");
        assert(ToriDraw_SceneSpriteHas(game->scene, scene_id));
    }
    int sprite_count = 0;
    struct ToriDraw_Sprite** sprites =
        ToriDraw_SceneSpriteGet(game->scene, scene_id, &sprite_count);
    if( atlas_index < 0 || atlas_index >= sprite_count || !sprites ||
        !sprites[atlas_index] || !sprites[atlas_index]->pixels_argb )
    {
        fprintf(stderr,
            "GameRunescape: sprite atlas invalid scene_id=%d atlas_index=%d count=%d "
            "(context=%s)\n",
            scene_id,
            atlas_index,
            sprite_count,
            context ? context : "(null)");
        assert(
            sprites && atlas_index >= 0 && atlas_index < sprite_count &&
            sprites[atlas_index] && sprites[atlas_index]->pixels_argb);
    }
}

static void
GameRunescape_AssertMinimenuFontReady(
    struct GameRunescape* game,
    int font_id,
    char const* context)
{
    GameRunescape_AssertSceneFontReady(game, font_id, context);
}

bool
GameRunescape_MinimenuPrepareShow(
    struct GameRunescape* game,
    struct UIMinimenuLayout* out_layout,
    int* out_content_width)
{
    if( !game )
        return false;

    int font_id = -1;
    if( game->ui_tree && game->ui_minimenu_node >= 0 )
    {
        struct StaticUIComponent* component =
            &game->ui_tree->components[game->ui_minimenu_node];
        font_id = component->u.minimenu.font_id;
    }

    struct ToriDraw_Font* font =
        game->scene && font_id >= 0 ? ToriDraw_SceneFontGet(game->scene, font_id) : NULL;
    return ui_minimenu_prepare_show(
        &game->minimenu, font, out_layout, out_content_width);
}

static int
game_runescape_minimenu_draw_line_height(
    struct GameRunescape* game,
    int font_id,
    struct UIMinimenuLayout const* layout)
{
    int lh =
        layout && layout->line_height > 0 ? layout->line_height : UI_MINIMENU_DEFAULT_LINE_HEIGHT;

    if( game && game->scene && font_id >= 0 )
    {
        struct ToriDraw_Font* font = ToriDraw_SceneFontGet(game->scene, font_id);
        if( font && font->line_height > 0 )
            lh = font->line_height;
    }
    return lh;
}

static bool
rs_ui_host_scene_model_has(
    void* user,
    int model_id)
{
    struct GameRunescape* game = user;
    return game && game->scene && ToriDraw_SceneModelHas(game->scene, model_id);
}

static bool
rs_ui_host_get_cross_active(void* user)
{
    struct GameRunescape* game = user;
    return game && game->cross_mode != RUNESCAPE_CROSS_MODE_OFF;
}

static void
rs_ui_host_get_cross_position(
    void* user,
    int* out_x,
    int* out_y)
{
    struct GameRunescape* game = user;
    if( out_x )
        *out_x = game ? game->cross_x : 0;
    if( out_y )
        *out_y = game ? game->cross_y : 0;
}

static int
rs_ui_host_get_cross_atlas_frame(void* user)
{
    struct GameRunescape* game = user;
    if( !game || game->cross_mode == RUNESCAPE_CROSS_MODE_OFF )
        return 0;

    int phase = game->cross_cycle / 100;
    if( game->cross_mode == RUNESCAPE_CROSS_MODE_INTERACT )
        return phase + 4;
    return phase;
}

static bool
rs_ui_host_get_minimenu_visible(void* user)
{
    struct GameRunescape* game = user;
    return game && game->minimenu.visible;
}

static void
rs_ui_host_get_minimenu_layout(
    void* user,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    struct GameRunescape* game = user;
    if( out_x )
        *out_x = game ? game->minimenu.x : 0;
    if( out_y )
        *out_y = game ? game->minimenu.y : 0;
    if( out_w )
        *out_w = game ? game->minimenu.width : 0;
    if( out_h )
        *out_h = game ? game->minimenu.height : 0;
}

static int
rs_ui_host_get_minimenu_hovered_option(void* user)
{
    struct GameRunescape* game = user;
    return game ? game->minimenu.hovered_option : -1;
}

static bool
rs_ui_host_get_inv_source_slot(
    void* user,
    int source_id,
    int slot,
    struct UIInvSlotData* out)
{
    struct GameRunescape* game = user;
    if( !game )
        return false;
    return ui_inv_data_service_get_slot(&game->inv_data, source_id, slot, out);
}

static bool
rs_ui_host_set_inv_source_slot(
    void* user,
    int source_id,
    int slot,
    struct UIInvSlotData const* data)
{
    struct GameRunescape* game = user;
    if( !game )
        return false;
    if( !ui_inv_data_service_set_slot(&game->inv_data, source_id, slot, data) )
        return false;
    instance_revconfig_inv_mark_dirty(game, source_id);
    return true;
}

static void
GameRunescape_FindSpecialUINodes(struct GameRunescape* game)
{
    if( !game )
        return;
    game->ui_minimenu_node = -1;
    game->ui_chat_node = -1;
    game->ui_chat_node = -1;
    if( !game->ui_tree )
        return;

    for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
    {
        if( game->ui_tree->components[i].type == UIELEM_BUILTIN_MINIMENU )
            game->ui_minimenu_node = (int32_t)i;
        if( game->ui_tree->components[i].type == UIELEM_BUILTIN_CHAT )
            game->ui_chat_node = (int32_t)i;
    }
}

static void
GameRunescape_InitUIHost(struct GameRunescape* game)
{
    if( !game )
        return;
    uitree_host_init(&game->ui_host);
    game->ui_host.user = game;
    game->ui_host.is_active = rs_ui_host_is_active;
    game->ui_host.apply_button_click = rs_ui_host_apply_button_click;
    game->ui_host.eval_text_placeholder = rs_ui_host_eval_text_placeholder;
    game->ui_host.get_selected_tab = rs_ui_host_get_selected_tab;
    game->ui_host.set_selected_tab = rs_ui_host_set_selected_tab;
    game->ui_host.get_camera_yaw = rs_ui_host_get_camera_yaw;
    game->ui_host.get_minimap_anchor = rs_ui_host_get_minimap_anchor;
    game->ui_host.get_world_map_size = rs_ui_host_get_world_map_size;
    game->ui_host.scene_sprite_has = rs_ui_host_scene_sprite_has;
    game->ui_host.scene_font_has = rs_ui_host_scene_font_has;
    game->ui_host.scene_model_has = rs_ui_host_scene_model_has;
    game->ui_host.get_cross_active = rs_ui_host_get_cross_active;
    game->ui_host.get_cross_position = rs_ui_host_get_cross_position;
    game->ui_host.get_cross_atlas_frame = rs_ui_host_get_cross_atlas_frame;
    game->ui_host.get_minimenu_visible = rs_ui_host_get_minimenu_visible;
    game->ui_host.get_minimenu_layout = rs_ui_host_get_minimenu_layout;
    game->ui_host.get_minimenu_hovered_option = rs_ui_host_get_minimenu_hovered_option;
    game->ui_host.get_inv_source_slot = rs_ui_host_get_inv_source_slot;
    game->ui_host.set_inv_source_slot = rs_ui_host_set_inv_source_slot;
    game->ui_input.hovered = -1;
    game->ui_input.pressed = -1;
    game->selected_tab = 3;
    game->selected_inv_source_id = -1;
    game->selected_inv_slot = -1;
    game->ui_minimenu_node = -1;
    game->ui_chat_node = -1;
    interaction_state_reset(&game->interaction);
    interaction_state_reset(&game->click_target);
    ui_minimenu_reset(&game->minimenu);
    game->cs2vm = cs2vm_new();
}

enum RsPhaseResult
{
    RS_PHASE_YIELD,
    RS_PHASE_ADVANCE,
};

static int
clamp_terrain_level(int level)
{
    if( level < 0 )
        return 0;
    if( level >= WORLD_MAP_TERRAIN_LEVELS )
        return WORLD_MAP_TERRAIN_LEVELS - 1;
    return level;
}

int
GameRunescape_CameraTerrainLevel(const struct GameRunescape* game)
{
    if( !game || !game->camera_position )
        return 0;
    return clamp_terrain_level(game->camera_position->y / 240);
}

static bool
GameRunescape_TranslateGCEvent(
    const struct ToriDraw_Event* ev,
    struct LibToriRS_RenderCommand* command)
{
    assert(ev && command);

    memset(command, 0, sizeof(*command));

    switch( ev->kind )
    {
    case TORIDRAW_EVENT_MODEL_LOAD:
        command->kind = TORIRSRC_MODEL_LOAD;
        command->u.model_load.element_id = ev->element_id;
        command->u.model_load.model = ev->model;
        command->u.model_load.world_position = ev->world_position;
        return true;
    case TORIDRAW_EVENT_MODEL_UNLOAD:
        command->kind = TORIRSRC_MODEL_UNLOAD;
        command->u.model_load.element_id = ev->element_id;
        return true;
    case TORIDRAW_EVENT_BATCH_BEGIN:
        command->kind = TORIRSRC_BATCH3D_BEGIN;
        command->u.batch.batch_id = ev->batch_id;
        return true;
    case TORIDRAW_EVENT_BATCH_MODEL_ADD:
        command->kind = TORIRSRC_BATCH3D_MODEL_ADD;
        command->u.batch.batch_id = ev->batch_id;
        command->u.batch.element_id = ev->element_id;
        command->u.batch.pose_id = ev->pose_id;
        command->u.batch.model = ev->model;
        command->u.batch.world_position = ev->world_position;
        return true;
    case TORIDRAW_EVENT_BATCH_ANIM_ADD:
        command->kind = TORIRSRC_BATCH3D_ANIM_ADD;
        command->u.batch.batch_id = ev->batch_id;
        command->u.batch.element_id = ev->element_id;
        command->u.batch.pose_id = ev->pose_id;
        command->u.batch.anim_index = ev->anim_index;
        command->u.batch.model = ev->model;
        command->u.batch.world_position = ev->world_position;
        return true;
    case TORIDRAW_EVENT_BATCH_END:
        command->kind = TORIRSRC_BATCH3D_END;
        command->u.batch.batch_id = ev->batch_id;
        return true;
    case TORIDRAW_EVENT_BATCH_CLEAR:
        command->kind = TORIRSRC_BATCH3D_CLEAR;
        command->u.batch.batch_id = ev->batch_id;
        return true;
    case TORIDRAW_EVENT_ANIM_LOAD:
        command->kind = TORIRSRC_ANIM_LOAD;
        command->u.anim_load.element_id = ev->element_id;
        command->u.anim_load.animation = ev->animation;
        command->u.anim_load.model = ev->model;
        command->u.anim_load.world_position = ev->world_position;
        return true;
    case TORIDRAW_EVENT_ANIM_UNLOAD:
        command->kind = TORIRSRC_ANIM_UNLOAD;
        command->u.anim_load.element_id = ev->element_id;
        return true;
    case TORIDRAW_EVENT_TEX_LOAD:
        command->kind = TORIRSRC_TEX_LOAD;
        command->u.tex_load.texture_id = ev->texture_id;
        command->u.tex_load.texture = ev->texture;
        return true;
    case TORIDRAW_EVENT_TEX_UNLOAD:
        command->kind = TORIRSRC_TEX_UNLOAD;
        command->u.tex_load.texture_id = ev->texture_id;
        command->u.tex_load.texture = NULL;
        return true;
    case TORIDRAW_EVENT_SPRITE_LOAD:
        command->kind = TORIRSRC_SPRITE_LOAD;
        command->u.sprite_load.element_id = ev->element_id;
        command->u.sprite_load.sprites = ev->sprites;
        command->u.sprite_load.count = ev->sprite_count;
        return true;
    case TORIDRAW_EVENT_SPRITE_UNLOAD:
        command->kind = TORIRSRC_SPRITE_UNLOAD;
        command->u.sprite_load.element_id = ev->element_id;
        return true;
    case TORIDRAW_EVENT_FONT_LOAD:
        command->kind = TORIRSRC_FONT_LOAD;
        command->u.font_load.font_id = ev->texture_id;
        command->u.font_load.font = ev->font;
        return true;
    case TORIDRAW_EVENT_FONT_UNLOAD:
        command->kind = TORIRSRC_FONT_UNLOAD;
        command->u.font_load.font_id = ev->texture_id;
        return true;
    default:
        return false;
    }
}

void
GameRunescape_UpdateWorldViewport(struct GameRunescape* game)
{
    int vw = game->view_port ? game->view_port->width : 800;
    int vh = game->view_port ? game->view_port->height : 600;
    int stride = game->view_port ? game->view_port->stride : vw;

    game->world_view_port.width = vw;
    game->world_view_port.height = vh;
    game->world_view_port.stride = stride;
    game->world_view_port.x_center = vw / 2;
    game->world_view_port.y_center = vh / 2;
    game->world_view_port.clip_left = 0;
    game->world_view_port.clip_top = 0;
    game->world_view_port.clip_right = vw;
    game->world_view_port.clip_bottom = vh;

    if( game->ui_tree )
    {
        for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
        {
            struct StaticUIComponent* world = &game->ui_tree->components[i];
            if( world->type != UIELEM_BUILTIN_WORLD )
                continue;

            int wx = world->position.x;
            int wy = world->position.y;
            int ww = world->position.width;
            int wh = world->position.height;
            if( ww <= 0 || wh <= 0 )
                break;

            game->world_view_port.width = ww;
            game->world_view_port.height = wh;
            game->world_view_port.x_center = wx + ww / 2;
            game->world_view_port.y_center = wy + wh / 2;
            game->world_view_port.clip_left = wx;
            game->world_view_port.clip_top = wy;
            game->world_view_port.clip_right = wx + ww;
            game->world_view_port.clip_bottom = wy + wh;
            break;
        }
    }
}

static enum WorldPickType
rs_classify_dynamic_pick_type(
    struct GameRunescape* game,
    int element_id)
{
    if( !game || !game->entity_registry )
        return WORLD_PICK_PROJECTILE;

    for( int i = 0; i < game->entity_registry_count; i++ )
    {
        if( game->entity_registry[i].element_id != element_id )
            continue;
        if( RS_ENTITY_KIND_OF(game->entity_registry[i].entity_id) == RS_ENTITY_KIND_NPC )
            return WORLD_PICK_NPC;
        break;
    }

    return WORLD_PICK_PROJECTILE;
}

static bool
game_runescape_mouse_in_world_viewport(
    struct GameRunescape* game,
    int mouse_x,
    int mouse_y)
{
    return game && mouse_x >= game->world_view_port.clip_left &&
           mouse_x < game->world_view_port.clip_right &&
           mouse_y >= game->world_view_port.clip_top &&
           mouse_y < game->world_view_port.clip_bottom;
}

static bool
game_runescape_project_and_pick_element(
    struct GameRunescape* game,
    int element_id,
    enum WorldPickType pick_type,
    int tile_x,
    int tile_z,
    int tile_level,
    bool allow_pick)
{
    assert(ToriDraw_SceneElementIsLive(game->scene, element_id));

    struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(game->scene, element_id);
    assert(!(!element || element->model.kind != TORIDRAWMK_MODEL));
    if( !ToriDraw_ModelGetBoundsCylinder(element->model) )
        return false;

    struct ToriDraw_Position rel_pos = element->world_position;
    rel_pos.x -= game->camera_position->x;
    rel_pos.y -= game->camera_position->y;
    rel_pos.z -= game->camera_position->z;
    rel_pos.pitch = ToriDraw_NormalizeAngle(element->world_position.pitch);
    rel_pos.yaw = ToriDraw_NormalizeAngle(element->world_position.yaw);

    if( element->anim_seq_id != -1 )
        ToriDraw_SceneElementApplyAnimation(game->scene, element_id, true, element->anim_frame);

    if( !game->scene )
        return false;

    const int cull = ToriDraw_RenderModel1Project(
        element->model, game->scene, &rel_pos, &game->world_view_port, game->camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return false;

    if( allow_pick && game->mouse_in_viewport &&
        ToriDraw_ProjectedModelContainsPoint(
            game->scene, element->model, &game->world_view_port, game->mouse_x, game->mouse_y) )
    {
        world_pickset_add(&game->pickset, element_id, pick_type, tile_x, tile_z, tile_level);
        if( pick_type == WORLD_PICK_TERRAIN )
        {
            game->last_tile_sx = tile_x;
            game->last_tile_sz = tile_z;
            game->last_tile_level = tile_level;
            game->last_tile_valid = true;
        }
    }

    return true;
}

static bool
GameRunescape_EmitDrawElement(
    struct GameRunescape* game,
    int element_id,
    enum WorldPickType pick_type,
    int tile_x,
    int tile_z,
    int tile_level,
    struct LibToriRS_RenderCommand* command)
{
    struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(game->scene, element_id);
    if( !game_runescape_project_and_pick_element(
            game, element_id, pick_type, tile_x, tile_z, tile_level, true) )
        return false;

    struct ToriDraw_Position rel_pos = element->world_position;
    rel_pos.x -= game->camera_position->x;
    rel_pos.y -= game->camera_position->y;
    rel_pos.z -= game->camera_position->z;
    rel_pos.pitch = ToriDraw_NormalizeAngle(element->world_position.pitch);
    rel_pos.yaw = ToriDraw_NormalizeAngle(element->world_position.yaw);

    if( ToriDraw_RenderModel2SortFaces(element->model, game->scene) <= 0 )
        return false;

    command->kind = TORIRSRC_DRAW_MODEL;
    command->u.model.model = element->model;
    command->u.model.element_id = element_id;
    command->u.model.position = rel_pos;
    command->u.model.world_position = element->world_position;
    command->u.model.animation = element->animation;
    command->u.model.anim_index = 0;
    command->u.model.anim_frame = element->anim_frame;
    command->u.model.dynamic = element->dynamic;

    return true;
}

static void
GameRunescape_MoveForward(
    struct GameRunescape* game,
    int amount)
{
    int direction_x = ToriDraw_Sin(game->camera->yaw);
    int direction_z = ToriDraw_Cos(game->camera->yaw);
    game->camera_position->x -= (direction_x * amount) >> 16;
    game->camera_position->z += (direction_z * amount) >> 16;
}

static void
GameRunescape_MoveLeft(
    struct GameRunescape* game,
    int amount)
{
    int direction_x = ToriDraw_Cos(game->camera->yaw);
    int direction_z = ToriDraw_Sin(game->camera->yaw);
    game->camera_position->x += (direction_x * amount) >> 16;
    game->camera_position->z += (direction_z * amount) >> 16;
}

static void
GameRunescape_MoveRight(
    struct GameRunescape* game,
    int amount)
{
    GameRunescape_MoveLeft(game, -amount);
}

static void
GameRunescape_CameraTile(
    const struct GameRunescape* game,
    int* out_sx,
    int* out_sz,
    int* out_slevel)
{
    *out_sx = game->camera_position->x / 128;
    *out_sz = game->camera_position->z / 128;
    *out_slevel = game->camera_position->y / 240;
}

struct GameRunescape*
GameRunescape_New(
    struct LibToriRS_ScriptQueue* script_queue,
    struct ToriDraw_Scene* scene)
{
    struct GameRunescape* game = calloc(1, sizeof(struct GameRunescape));
    assert(game && "GameRunescape_New: failed to allocate game");

    game->script_queue = script_queue;
    game->scene = scene;
    game->zone_center_x = RUNESCAPE_ZONE_CENTER_X;
    game->zone_center_z = RUNESCAPE_ZONE_CENTER_Z;

    game->camera_position = calloc(1, sizeof(struct ToriDraw_Position));
    game->camera = calloc(1, sizeof(struct ToriDraw_Camera));
    game->view_port = calloc(1, sizeof(struct ToriDraw_ViewPort));
    assert(game->camera_position && "GameRunescape_New: failed to allocate camera position");
    assert(game->camera && "GameRunescape_New: failed to allocate camera");
    assert(game->view_port && "GameRunescape_New: failed to allocate view port");

    game->camera_position->z = -800;
    game->camera->fov_rpi2048 = 512;
    game->camera->near_plane_z = 50;
    game->camera->pitch = 148;
    game->view_port->width = 765;
    game->view_port->height = 503;
    game->view_port->stride = 765;
    game->view_port->x_center = 382;
    game->view_port->y_center = 251;

    assert(game->scene && "GameRunescape_New: failed to allocate context");

    game->world = world_new();
    assert(game->world && "GameRunescape_New: failed to allocate world");

    game->painter_buffer = painter_buffer_new();
    assert(game->painter_buffer && "GameRunescape_New: failed to allocate painter buffer");

    game->ui_tree = uitree_new(64);
    assert(game->ui_tree && "GameRunescape_New: failed to allocate ui tree");
    ui_inv_data_service_init(&game->inv_data);
    GameRunescape_InitUIHost(game);
    game->ui_hovered_node = -1;
    game->ui_over_main_com_id = -1;
    game->ui_over_side_com_id = -1;
    game->ui_over_chat_com_id = -1;
    game->ui_over_main_com_id_prev = -1;
    game->ui_over_side_com_id_prev = -1;
    game->ui_over_chat_com_id_prev = -1;
    game->ui_minimenu_node = -1;
    game->ui_chat_node = -1;
    game->ui_scroll_drag_layer_id = -1;
    game->ui_scroll_drag_kind = UITREE_SCROLLBAR_NONE;

    game->world_map_scene_id = -1;
    game->world_map_w = 0;
    game->world_map_h = 0;
    game->ui_scrollbar0_scene_id = -1;
    game->ui_scrollbar1_scene_id = -1;
    game->entity_registry_cap = RUNESCAPE_ENTITY_REGISTRY_INITIAL_CAP;
    game->entity_registry =
        calloc((size_t)game->entity_registry_cap, sizeof(struct GameRunescape_EntityRecord));

    return game;
}

void
GameRunescape_Free(struct GameRunescape* game)
{
    if( !game )
        return;
    if( game->painter_buffer )
    {
        free(game->painter_buffer->commands);
        free(game->painter_buffer);
    }
    if( game->world )
        world_free(game->world);
    if( game->ui_tree )
        uitree_free(game->ui_tree);
    if( game->ui_inv_pool )
        uitree_inv_pool_free(game->ui_inv_pool);
    if( game->cs2vm )
        cs2vm_free(game->cs2vm);
    free(game->entity_registry);
    free(game->camera_position);
    free(game->camera);
    free(game->view_port);
    free(game);
}

void
GameRunescape_SetCore(
    struct GameRunescape* game,
    struct ToriAuxLibCore* gamecache)
{
    if( !game )
        return;
    game->core = gamecache;
}

void
GameRunescape_SetTD(
    struct GameRunescape* game,
    struct ToriAuxLibTD* td)
{
    if( !game )
        return;
    game->td = td;
}

void
GameRunescape_SetVM(
    struct GameRunescape* game,
    struct ToriAuxLibVM* vm)
{
    if( !game )
        return;
    game->vm = vm;
}

static void
GameRunescape_AttachWorldMapToUITree(struct GameRunescape* game)
{
    if( !game || game->world_map_scene_id < 0 || !game->ui_tree )
        return;

    for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
    {
        if( game->ui_tree->components[i].type == UIELEM_BUILTIN_MINIMAP )
        {
            game->ui_tree->components[i].u.minimap.scene_id = game->world_map_scene_id;
            break;
        }
    }
}

void
GameRunescape_SetUITree(
    struct GameRunescape* game,
    struct UITree* ui_tree)
{
    if( !game )
        return;
    game->ui_tree = ui_tree;
    GameRunescape_AttachWorldMapToUITree(game);
    GameRunescape_FindSpecialUINodes(game);
}

void
GameRunescape_IF3InvSetContainerSlot(
    struct GameRunescape* game,
    int container_id,
    int slot,
    int obj_id,
    int obj_count,
    int scene_id,
    int atlas_index)
{
    if( !game || container_id < 0 )
        return;
    struct RSInvContainer* container =
        rs_inv_container_get_or_create(&game->inv_data.store, container_id, 0);
    if( !container )
        return;
    rs_inv_container_set_slot(
        container, slot, obj_id, obj_count, scene_id, atlas_index);
    instance_revconfig_inv_mark_dirty(game, UI_INV_SOURCE_INVALID);
}

void
GameRunescape_IF3InvApplyFull(
    struct GameRunescape* game,
    int container_id,
    int const* obj_ids,
    int const* obj_counts,
    int count)
{
    if( !game || container_id < 0 )
        return;
    struct RSInvContainer* container =
        rs_inv_container_get_or_create(&game->inv_data.store, container_id, 0);
    if( !container )
        return;
    rs_inv_container_apply_full(container, obj_ids, obj_counts, count);
    if( game->ui_tree )
        uitree_mark_all_dirty(game->ui_tree);
}

void
GameRunescape_IF3InvApplyPartial(
    struct GameRunescape* game,
    int container_id,
    int const* slots,
    int const* obj_ids,
    int const* obj_counts,
    int count)
{
    if( !game || container_id < 0 )
        return;
    struct RSInvContainer* container =
        rs_inv_container_get_or_create(&game->inv_data.store, container_id, 0);
    if( !container )
        return;
    rs_inv_container_apply_partial(container, slots, obj_ids, obj_counts, count);
    if( game->ui_tree )
        uitree_mark_all_dirty(game->ui_tree);
}

void
GameRunescape_SetUIInvPool(
    struct GameRunescape* game,
    struct UIInventoryPool* pool)
{
    if( !game )
        return;
    if( game->ui_inv_pool && game->ui_inv_pool != pool )
        uitree_inv_pool_free(game->ui_inv_pool);
    game->ui_inv_pool = pool;
}

void
GameRunescape_SyncUISpritesFromScene(struct GameRunescape* game)
{
    if( !game || !game->scene )
        return;
    ToriDraw_SceneSpritesReemitLoads(game->scene);
    game->ui_sprites_synced = true;
}

void
GameRunescape_SetUITreeReady(
    struct GameRunescape* game,
    bool ready)
{
    if( !game )
        return;
    game->ui_tree_ready = ready;
    if( ready )
    {
        game->ui_fonts_synced = false;
        GameRunescape_AttachWorldMapToUITree(game);
        GameRunescape_FindSpecialUINodes(game);
    }
}

void
GameRunescape_RebuildWorldMap(struct GameRunescape* game)
{
    assert(game->world);

    struct Minimap* mm = game->world->minimap;
    int pw = 0;
    int ph = 0;
    uint32_t* argb = minimap_bake_argb(mm, &pw, &ph);
    if( !argb )
        return;

    struct ToriDraw_Sprite* sp = ToriDraw_SpriteNewFromArgbOwned(argb, pw, ph);
    if( !sp )
    {
        free(argb);
        return;
    }

    struct ToriDraw_Sprite** sprites_array =
        (struct ToriDraw_Sprite**)malloc(sizeof(*sprites_array));
    if( !sprites_array )
    {
        ToriDraw_SpriteFree(sp);
        return;
    }
    sprites_array[0] = sp;

    ToriDraw_SceneSpriteAdd(game->scene, RUNESCAPE_WORLD_MAP_SCENE_ID, sprites_array, 1);
    game->world_map_scene_id = RUNESCAPE_WORLD_MAP_SCENE_ID;
    game->world_map_w = pw;
    game->world_map_h = ph;
    GameRunescape_AttachWorldMapToUITree(game);
}

static void
game_runescape_clear_gamecache_map_chunks_except(
    struct ToriAuxLibCore* core,
    struct World* world)
{
    if( !core || !world )
        return;

    int const width = world->_chunk_ne_x - world->_chunk_sw_x + 1;
    int const height = world->_chunk_ne_z - world->_chunk_sw_z + 1;
    if( width <= 0 || height <= 0 )
        return;

    int const count = width * height;
    int* map_ids = malloc((size_t)count * sizeof(int));
    if( !map_ids )
        return;

    int idx = 0;
    for( int mapx = world->_chunk_sw_x; mapx <= world->_chunk_ne_x; mapx++ )
    {
        for( int mapz = world->_chunk_sw_z; mapz <= world->_chunk_ne_z; mapz++ )
            map_ids[idx++] = (mapx << 16) | (mapz & 0xFFFF);
    }

    ToriAuxLibCore_MapTerrainClearExcept(core, map_ids, count);
    ToriAuxLibCore_MapSceneryClearExcept(core, map_ids, count);
    free(map_ids);
}

void
GameRunescape_BuildWorldCenterzone(
    struct GameRunescape* game,
    int center_x,
    int center_z,
    int scene_size)
{
    assert(game && game->world && game->scene);

    struct WorldBuilder* builder = world_builder_new(
        game->world, game->core, game->scene, game->td, ToriAuxLibVM_VarPVarBit(game->vm));
    assert(builder && "GameRunescape_BuildWorld: failed to allocate world builder");
    world_builder_rebuild_centerzone(builder, center_x, center_z, scene_size);
    world_builder_free(builder);
    if( game->core )
        game_runescape_clear_gamecache_map_chunks_except(game->core, game->world);
    if( game->td )
        ToriAuxLibCache_PruneBuildCaches(ToriAuxLibTD_C(game->td));
    game->world_built = true;

    if( game->camera_position && game->camera )
    {
        int const scene_center = (game->world->_scene_size / 2) * 128;
        game->camera_position->x = scene_center;
        game->camera_position->z = scene_center - 1500;
        game->camera_position->y = -2000;
        game->camera->pitch = 450;
        game->camera->yaw = 0;
    }

    GameRunescape_RebuildWorldMap(game);
}

void
GameRunescape_BuildWorldChunkList(
    struct GameRunescape* game,
    int* chunks_xz,
    int count)
{
    assert(game && game->world && game->scene);

    struct WorldBuilder* builder = world_builder_new(
        game->world, game->core, game->scene, game->td, ToriAuxLibVM_VarPVarBit(game->vm));
    assert(builder && "GameRunescape_BuildWorldChunkList: failed to allocate world builder");
    world_builder_rebuild_chunklist(builder, chunks_xz, count);
    world_builder_free(builder);
    if( game->core )
        game_runescape_clear_gamecache_map_chunks_except(game->core, game->world);
    if( game->td )
        ToriAuxLibCache_PruneBuildCaches(ToriAuxLibTD_C(game->td));
    game->world_built = true;

    if( game->camera_position && game->camera )
    {
        int const scene_center = (game->world->_scene_size / 2) * 128;
        game->camera_position->x = scene_center;
        game->camera_position->z = scene_center - 1500;
        game->camera_position->y = -2000;
        game->camera->pitch = 450;
        game->camera->yaw = 0;
    }

    GameRunescape_RebuildWorldMap(game);
}

/** Client.ts main viewport hover region (buildMinimenu). */
#define RS_UI_HOVER_MAIN_X 4
#define RS_UI_HOVER_MAIN_Y 4
#define RS_UI_HOVER_MAIN_W 512
#define RS_UI_HOVER_MAIN_H 334
/** Client.ts sidebar hover region. */
#define RS_UI_HOVER_SIDE_X 553
#define RS_UI_HOVER_SIDE_Y 205
#define RS_UI_HOVER_SIDE_W 190
#define RS_UI_HOVER_SIDE_H 261
/** Client.ts chatbox hover region. */
#define RS_UI_HOVER_CHAT_X 17
#define RS_UI_HOVER_CHAT_Y 357
#define RS_UI_HOVER_CHAT_W 409
#define RS_UI_HOVER_CHAT_H 96

static struct UITreeHoverIds
GameRunescape_UIHoverIds(struct GameRunescape const* game)
{
    struct UITreeHoverIds ids = {
        .main_com_id = -1,
        .side_com_id = -1,
        .chat_com_id = -1,
    };
    if( !game )
        return ids;
    ids.main_com_id = game->ui_over_main_com_id;
    ids.side_com_id = game->ui_over_side_com_id;
    ids.chat_com_id = game->ui_over_chat_com_id;
    return ids;
}

int32_t
GameRunescape_UISelectedSidebarIndex(struct GameRunescape const* game)
{
    if( !game || !game->ui_tree )
        return -1;

    int const tab = game->selected_tab;
    for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
    {
        struct StaticUIComponent const* c = &game->ui_tree->components[i];
        if( c->type == UIELEM_BUILTIN_SIDEBAR && c->u.sidebar.tabno == tab )
            return (int32_t)i;
    }
    return -1;
}

static bool
game_runescape_world_clip_is_builtin_widget(
    struct GameRunescape const* game)
{
    if( !game )
        return false;

    int const vw = game->view_port ? game->view_port->width : UITREE_LAYOUT_ROOT_W;
    int const vh = game->view_port ? game->view_port->height : UITREE_LAYOUT_ROOT_H;
    int const clip_w =
        game->world_view_port.clip_right - game->world_view_port.clip_left;
    int const clip_h =
        game->world_view_port.clip_bottom - game->world_view_port.clip_top;

    if( clip_w <= 0 || clip_h <= 0 )
        return false;

    return game->world_view_port.clip_left > 0 || game->world_view_port.clip_top > 0 ||
           game->world_view_port.clip_right < vw || game->world_view_port.clip_bottom < vh;
}

bool
GameRunescape_PointInMainHoverRegion(
    struct GameRunescape const* game,
    int px,
    int py)
{
    if( game && game_runescape_world_clip_is_builtin_widget(game) )
    {
        return px >= game->world_view_port.clip_left &&
               px < game->world_view_port.clip_right &&
               py >= game->world_view_port.clip_top &&
               py < game->world_view_port.clip_bottom;
    }

    return px > RS_UI_HOVER_MAIN_X && py > RS_UI_HOVER_MAIN_Y &&
           px < RS_UI_HOVER_MAIN_X + RS_UI_HOVER_MAIN_W &&
           py < RS_UI_HOVER_MAIN_Y + RS_UI_HOVER_MAIN_H;
}

bool
GameRunescape_PointInSidebarHoverRegion(
    int px,
    int py)
{
    return px > RS_UI_HOVER_SIDE_X && py > RS_UI_HOVER_SIDE_Y &&
           px < RS_UI_HOVER_SIDE_X + RS_UI_HOVER_SIDE_W &&
           py < RS_UI_HOVER_SIDE_Y + RS_UI_HOVER_SIDE_H;
}

bool
GameRunescape_PointInChatHoverRegion(
    int px,
    int py)
{
    return px > RS_UI_HOVER_CHAT_X && py > RS_UI_HOVER_CHAT_Y &&
           px < RS_UI_HOVER_CHAT_X + RS_UI_HOVER_CHAT_W &&
           py < RS_UI_HOVER_CHAT_Y + RS_UI_HOVER_CHAT_H;
}

int32_t
GameRunescape_UITreeIndexForComponentId(
    struct GameRunescape const* game,
    int component_id)
{
    if( !game || !game->ui_tree || component_id < 0 )
        return -1;

    for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
    {
        if( game->ui_tree->components[i].component_id == component_id )
            return (int32_t)i;
    }
    return -1;
}

static bool
GameRunescape_UINodeVisible(
    struct GameRunescape* game,
    struct StaticUIComponent const* c,
    int32_t node_index)
{
    (void)node_index;
    struct UITreeHoverIds const hover_ids = GameRunescape_UIHoverIds(game);
    return uitree_component_visible_host(c, &hover_ids, &game->ui_host);
}

static void
GameRunescape_UpdateUIHover(
    struct GameRunescape* game)
{
    game->ui_hovered_node = -1;
    game->ui_over_main_com_id = -1;
    game->ui_over_side_com_id = -1;
    game->ui_over_chat_com_id = -1;
    if( !game->ui_tree || !game->ui_tree_ready )
        return;

    if( game->minimenu.visible )
        goto done;

    struct UITreeScrollState scroll = {
        .scroll_x = game->ui_scroll_x,
        .scroll_y = game->ui_scroll_y,
    };

    uitree_find_hovered_component_id_for_region(
        game->ui_tree,
        &game->ui_host,
        &scroll,
        game->mouse_x,
        game->mouse_y,
        RS_UI_HOVER_MAIN_X,
        RS_UI_HOVER_MAIN_Y,
        RS_UI_HOVER_MAIN_W,
        RS_UI_HOVER_MAIN_H,
        -1,
        &game->ui_over_main_com_id);
    ui_hover_debug_log(
        "main region -> over_main_com_id=%d",
        game->ui_over_main_com_id);

    {
        int32_t const sidebar_idx = GameRunescape_UISelectedSidebarIndex(game);
        if( sidebar_idx >= 0 )
        {
            uitree_find_hovered_component_id_for_region(
                game->ui_tree,
                &game->ui_host,
                &scroll,
                game->mouse_x,
                game->mouse_y,
                RS_UI_HOVER_SIDE_X,
                RS_UI_HOVER_SIDE_Y,
                RS_UI_HOVER_SIDE_W,
                RS_UI_HOVER_SIDE_H,
                sidebar_idx,
                &game->ui_over_side_com_id);
            ui_hover_debug_log(
                "side region -> over_side_com_id=%d",
                game->ui_over_side_com_id);
        }
    }

    if( game->ui_chat_node >= 0 )
    {
        uitree_find_hovered_component_id_for_region(
            game->ui_tree,
            &game->ui_host,
            &scroll,
            game->mouse_x,
            game->mouse_y,
            RS_UI_HOVER_CHAT_X,
            RS_UI_HOVER_CHAT_Y,
            RS_UI_HOVER_CHAT_W,
            RS_UI_HOVER_CHAT_H,
            game->ui_chat_node,
            &game->ui_over_chat_com_id);
        ui_hover_debug_log(
            "chat region -> over_chat_com_id=%d",
            game->ui_over_chat_com_id);
    }

done:
    game->ui_hovered_node =
        GameRunescape_UIHitTest(game, game->mouse_x, game->mouse_y);
}

int32_t
GameRunescape_UIHitTest(
    struct GameRunescape* game,
    int px,
    int py)
{
    if( !game || !game->ui_tree )
        return -1;
    struct UITreeScrollState scroll = {
        .scroll_x = game->ui_scroll_x,
        .scroll_y = game->ui_scroll_y,
    };
    return uitree_hit_test_interactive(
        game->ui_tree, &game->ui_host, &scroll, px, py);
}

void
GameRunescape_GetScrollPos(
    struct GameRunescape const* game,
    int component_id,
    int* sx,
    int* sy)
{
    if( !game )
        return;
    struct UITreeScrollState scroll = {
        .scroll_x = (int*)game->ui_scroll_x,
        .scroll_y = (int*)game->ui_scroll_y,
    };
    uitree_scroll_get_pos(&scroll, component_id, sx, sy);
}

void
GameRunescape_ClampScroll(
    struct GameRunescape* game,
    struct StaticUIComponent const* layer)
{
    if( !game || !layer || layer->component_id < 0 )
        return;
    struct UITreeScrollState scroll = {
        .scroll_x = game->ui_scroll_x,
        .scroll_y = game->ui_scroll_y,
    };
    uitree_scroll_clamp_pos(layer, &scroll, layer->component_id);
}

#define RUNESCAPE_UI_SCROLL_DRAG_PADDING 32

static void
GameRunescape_UIScrollDragClear(struct GameRunescape* game)
{
    if( !game )
        return;
    game->ui_scroll_drag_layer_id = -1;
    game->ui_scroll_drag_kind = UITREE_SCROLLBAR_NONE;
    game->ui_scroll_drag_anchor = 0;
    game->ui_scroll_grabbed = false;
    game->frame.ui_scroll_cycle = 0;
}

static int32_t
GameRunescape_UIFindLayerIndexById(
    struct GameRunescape const* game,
    int component_id)
{
    if( !game || !game->ui_tree || component_id < 0 )
        return -1;

    for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
    {
        struct StaticUIComponent const* component = &game->ui_tree->components[i];
        if( component->type == UIELEM_RS_LAYER && component->component_id == component_id )
            return (int32_t)i;
    }
    return -1;
}

static bool
GameRunescape_ProcessUIScroll(
    struct GameRunescape* game,
    struct LibToriRS_Input* input)
{
    if( !game || !game->ui_tree || !game->ui_tree_ready )
        return false;

    bool const mouse_held = LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);
    if( !mouse_held || LibToriRS_Input_IsDragEnd(input, TORIRSM_LEFT) )
    {
        GameRunescape_UIScrollDragClear(game);
        return false;
    }

    game->frame.ui_scroll_cycle++;

    struct UITreeScrollState scroll = {
        .scroll_x = game->ui_scroll_x,
        .scroll_y = game->ui_scroll_y,
    };
    struct UITreeScrollbarHitInfo hit = { 0 };
    bool changed = false;
    bool consume_click = false;

    if( game->ui_scroll_grabbed && game->ui_scroll_drag_layer_id >= 0 &&
        uitree_scrollbar_is_grip_kind(game->ui_scroll_drag_kind) )
    {
        int32_t layer_idx =
            GameRunescape_UIFindLayerIndexById(game, game->ui_scroll_drag_layer_id);
        if( layer_idx >= 0 &&
             uitree_scrollbar_hit_for_layer(game->ui_tree, &scroll, layer_idx, &hit) )
        {
            hit.kind = game->ui_scroll_drag_kind;
            if( uitree_scrollbar_handle(
                    game->ui_tree,
                    &scroll,
                    &hit,
                    game->mouse_x,
                    game->mouse_y,
                    UITREE_SCROLLBAR_ACTION_GRIP_DRAG,
                    0) )
            {
                changed = true;
                consume_click = true;
            }
        }
    }
    else
    {
        int const padding = game->ui_scroll_grabbed ? RUNESCAPE_UI_SCROLL_DRAG_PADDING : 0;
        if( uitree_find_scrollbar_at_padded(
                game->ui_tree,
                &game->ui_host,
                &scroll,
                game->mouse_x,
                game->mouse_y,
                padding,
                &hit) )
        {
            struct StaticUIComponent* layer = &game->ui_tree->components[hit.layer_index];
            if( uitree_scrollbar_is_arrow_kind(hit.kind) )
            {
                int const step = game->frame.ui_scroll_cycle * UITREE_SCROLLBAR_ARROW_DELTA;
                if( uitree_scrollbar_handle(
                        game->ui_tree,
                        &scroll,
                        &hit,
                        game->mouse_x,
                        game->mouse_y,
                        UITREE_SCROLLBAR_ACTION_ARROW_STEP,
                        step) )
                {
                    changed = true;
                    consume_click = true;
                    game->ui_scroll_drag_layer_id = layer->component_id;
                    game->ui_scroll_drag_kind = hit.kind;
                }
            }
            else if( uitree_scrollbar_is_grip_kind(hit.kind) && game->frame.ui_scroll_cycle > 0 )
            {
                if( uitree_scrollbar_handle(
                        game->ui_tree,
                        &scroll,
                        &hit,
                        game->mouse_x,
                        game->mouse_y,
                        UITREE_SCROLLBAR_ACTION_GRIP_DRAG,
                        0) )
                {
                    changed = true;
                    consume_click = true;
                    game->ui_scroll_drag_layer_id = layer->component_id;
                    game->ui_scroll_drag_kind = hit.kind;
                    game->ui_scroll_grabbed = true;
                }
            }
        }
    }

    if( changed )
    {
        uitree_mark_all_dirty(game->ui_tree);
        if( game->ui_scroll_drag_layer_id >= 0 )
        {
            int32_t layer_idx =
                GameRunescape_UIFindLayerIndexById(game, game->ui_scroll_drag_layer_id);
            if( layer_idx >= 0 )
            {
                struct StaticUIComponent* layer = &game->ui_tree->components[layer_idx];
                uitree_scroll_clamp_pos(layer, &scroll, layer->component_id);
            }
        }
    }

    return consume_click && LibToriRS_Input_IsClick(input, TORIRSM_LEFT);
}

static int
GameRunescape_UIRectColor(
    struct GameRunescape* game,
    struct StaticUIComponent* component,
    int32_t node_index)
{
    (void)node_index;
    struct UITreeHoverIds const hover_ids = GameRunescape_UIHoverIds(game);
    return uitree_component_rect_color_host(
        component,
        &hover_ids,
        &game->ui_host,
        component->u.rs_rect.color);
}

void
GameRunescape_ProcessInput(
    struct GameRunescape* game,
    struct LibToriRS_Input* input)
{
    game->mouse_x = input->curr.mouse_x;
    game->mouse_y = input->curr.mouse_y;

    if( game->ui_tree && game->ui_tree_ready )
        GameRunescape_UpdateWorldViewport(game);

    game->mouse_in_viewport =
        game->mouse_x >= game->world_view_port.clip_left &&
        game->mouse_x < game->world_view_port.clip_right &&
        game->mouse_y >= game->world_view_port.clip_top &&
        game->mouse_y < game->world_view_port.clip_bottom;

    if( game->ui_tree && game->ui_tree_ready )
    {
        bool scroll_consumed_click = GameRunescape_ProcessUIScroll(game, input);

        if( LibToriRS_Input_IsClick(input, TORIRSM_LEFT) && !scroll_consumed_click )
        {
            ui_click_handle_left(
                game,
                input,
                input->last_click_x[TORIRSM_LEFT],
                input->last_click_y[TORIRSM_LEFT]);
        }
    }

    if( LibToriRS_Input_IsClick(input, TORIRSM_RIGHT) )
    {
        bool const can_right_click = game->ui_tree &&
            (game->ui_tree_ready ||
             (game->world && game->world->load_complete));
        if( can_right_click )
        {
            ui_click_handle_right(
                game, input, input->last_click_x[TORIRSM_RIGHT], input->last_click_y[TORIRSM_RIGHT]);
        }
    }

    const int move = RUNESCAPE_CAMERA_MOVEMENT_SPEED;
    const int rotate = 10;

    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_W) )
        GameRunescape_MoveForward(game, move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_S) )
        GameRunescape_MoveForward(game, -move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_A) )
        GameRunescape_MoveRight(game, move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_D) )
        GameRunescape_MoveLeft(game, move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_R) )
        game->camera_position->y -= move;
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_F) )
        game->camera_position->y += move;
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_LEFT) )
        game->camera->yaw = ToriDraw_AddAngle(game->camera->yaw, rotate);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT) )
        game->camera->yaw = ToriDraw_AddAngle(game->camera->yaw, -rotate);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP) )
        game->camera->pitch = ToriDraw_AddAngle(game->camera->pitch, rotate);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN) )
        game->camera->pitch = ToriDraw_AddAngle(game->camera->pitch, -rotate);
}

static void
GameRunescape_DrainWorldEvents(struct GameRunescape* game)
{
    struct World* world = game->world;
    if( !world || !game->scene )
        return;

    int count = world_events_count(world);
    for( int i = 0; i < count; i++ )
    {
        const struct WorldEvent* ev = world_events_peek(world, i);
        if( !ev )
            continue;
        if( ev->kind == WORLD_EVENT_ENTITY_REMOVED && ev->element_id >= 0 )
            ToriDraw_SceneElementRemove(game->scene, ev->element_id);
    }
    world_events_clear(world);
}

static void
GameRunescape_SyncProjectilesToScene(struct GameRunescape* game)
{
    struct World* world = game->world;
    if( !world || !game->scene || !world->load_complete )
        return;

    struct World_EntityPool* pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Projectile* p = World_EntityPoolGet(pool, i);
        if( !p || p->element_id < 0 || !p->launched )
            continue;

        ToriDraw_SceneElementSetPositionPitchYaw(
            game->scene,
            p->element_id,
            (int)p->x,
            (int)p->y,
            (int)p->z,
            p->orientation.pitch,
            p->orientation.yaw);
    }
}

static void
GameRunescape_TickAnimations(struct GameRunescape* game)
{
    if( !game->scene )
        return;

    int slot_count = ToriDraw_SceneElementSlotCount(game->scene);
    for( int element_id = 0; element_id < slot_count; element_id++ )
    {
        if( !ToriDraw_SceneElementIsLive(game->scene, element_id) )
            continue;

        struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(game->scene, element_id);
        if( !element || element->anim_seq_id == -1 )
            continue;

        if( element->is_skeletal )
        {
            /* Skeletal animation: advance by frame_count ticks per cycle */
            const struct ToriDraw_SkeletalAnim* skeletal = element->skeletal_animation;
            if( !skeletal || skeletal->frame_count <= 0 )
                continue;

            int play_frames = element->skeletal_play_frames;
            if( play_frames <= 0 || play_frames > skeletal->frame_count )
                play_frames = skeletal->frame_count;

            element->anim_cycle++;
            if( element->anim_cycle >= 1 )
            {
                element->anim_frame = (element->anim_frame + 1) % play_frames;
                element->anim_cycle = 0;
            }
        }
        else
        {
            if( !element->animation )
                continue;

            const struct ToriDraw_Animation* anim = element->animation;
            if( anim->frame_count <= 0 || !anim->frames )
                continue;

            element->anim_cycle++;
            const int delay = anim->frames[element->anim_frame].delay;
            if( element->anim_cycle >= delay )
            {
                element->anim_frame = (element->anim_frame + 1) % anim->frame_count;
                element->anim_cycle = 0;
            }
        }
    }
}

static void
GameRunescape_UIAdvance(
    struct GameRunescape* game,
    int32_t stepped_index)
{
    struct UITree* tree = game->ui_tree;
    struct StaticUIComponent* c = &tree->components[stepped_index];

    if( c->first_child >= 0 && GameRunescape_UINodeVisible(game, c, stepped_index) )
    {
        if( game->frame.ui_stack_top + 1 < RUNESCAPE_UI_TRAVERSAL_STACK_MAX )
        {
            game->frame.ui_stack[++game->frame.ui_stack_top] =
                (struct GameRunescape_UITraversalFrame){ .parent_index = stepped_index };
            game->frame.ui_current = c->first_child;
            return;
        }
    }

    if( c->next_sibling >= 0 )
    {
        game->frame.ui_current = c->next_sibling;
        return;
    }

    while( game->frame.ui_stack_top >= 0 )
    {
        struct GameRunescape_UITraversalFrame frame =
            game->frame.ui_stack[game->frame.ui_stack_top--];
        struct StaticUIComponent* parent = &tree->components[frame.parent_index];
        if( parent->next_sibling >= 0 )
        {
            game->frame.ui_current = parent->next_sibling;
            return;
        }
    }
    game->frame.ui_current = -1;
}

static void
GameRunescape_ApplyScissorToSprite(
    struct LibToriRS_RenderCommand* command,
    struct UITreeScrollClip const* clip)
{
    if( !command || !clip || clip->clip_w <= 0 || clip->clip_h <= 0 )
        return;
    command->u.sprite.scissor_x = clip->clip_x;
    command->u.sprite.scissor_y = clip->clip_y;
    command->u.sprite.scissor_w = clip->clip_w;
    command->u.sprite.scissor_h = clip->clip_h;
}

static void
GameRunescape_ApplyScissorToFillRect(
    struct LibToriRS_RenderCommand* command,
    struct UITreeScrollClip const* clip)
{
    if( !command || !clip || clip->clip_w <= 0 || clip->clip_h <= 0 )
        return;
    command->u.fill_rect.scissor_x = clip->clip_x;
    command->u.fill_rect.scissor_y = clip->clip_y;
    command->u.fill_rect.scissor_w = clip->clip_w;
    command->u.fill_rect.scissor_h = clip->clip_h;
}

static void
GameRunescape_ApplyScissorToFont(
    struct LibToriRS_RenderCommand* command,
    struct UITreeScrollClip const* clip)
{
    if( !command || !clip || clip->clip_w <= 0 || clip->clip_h <= 0 )
        return;
    command->u.font.scissor_x = clip->clip_x;
    command->u.font.scissor_y = clip->clip_y;
    command->u.font.scissor_w = clip->clip_w;
    command->u.font.scissor_h = clip->clip_h;
}

static struct UITreeScrollState
GameRunescape_UIScrollState(struct GameRunescape* game)
{
    struct UITreeScrollState scroll = {
        .scroll_x = game->ui_scroll_x,
        .scroll_y = game->ui_scroll_y,
    };
    return scroll;
}

static int
GameRunescape_UIGetAncestors(
    struct GameRunescape* game,
    int32_t* ancestors,
    int max_ancestors)
{
    if( !game || !ancestors || max_ancestors <= 0 )
        return 0;

    int count = 0;
    for( int i = 0; i <= game->frame.ui_stack_top && count < max_ancestors; i++ )
        ancestors[count++] = game->frame.ui_stack[i].parent_index;
    return count;
}

static void
GameRunescape_UIGetDrawContext(
    struct GameRunescape* game,
    struct StaticUIComponent* component,
    int* bx,
    int* by,
    int* bw,
    int* bh,
    struct UITreeScrollClip* clip)
{
    uitree_layout_get_bounds(&component->position, bx, by, bw, bh);
    clip->clip_x = 0;
    clip->clip_y = 0;
    clip->clip_w = 0;
    clip->clip_h = 0;

    int32_t ancestors[RUNESCAPE_UI_TRAVERSAL_STACK_MAX];
    int ancestor_count = GameRunescape_UIGetAncestors(game, ancestors, RUNESCAPE_UI_TRAVERSAL_STACK_MAX);
    struct UITreeScrollState scroll_state = GameRunescape_UIScrollState(game);
    uitree_scroll_apply_ancestors(
        game->ui_tree,
        &scroll_state,
        ancestors,
        ancestor_count,
        bx,
        by,
        clip);
}

static void
GameRunescape_EmitScrollbarFill(
    struct LibToriRS_RenderCommand* command,
    int x,
    int y,
    int w,
    int h,
    int argb)
{
    command->kind = TORIRSRC_FILL_RECT;
    command->u.fill_rect.x = x;
    command->u.fill_rect.y = y;
    command->u.fill_rect.w = w;
    command->u.fill_rect.h = h;
    command->u.fill_rect.argb = argb;
    command->u.fill_rect.scissor_x = 0;
    command->u.fill_rect.scissor_y = 0;
    command->u.fill_rect.scissor_w = 0;
    command->u.fill_rect.scissor_h = 0;
}

static void
GameRunescape_EmitSpriteCommand(
    struct LibToriRS_RenderCommand* command,
    int scene_id,
    int atlas_index,
    int x,
    int y,
    int w,
    int h)
{
    command->kind = TORIRSRC_SPRITE;
    command->u.sprite.element_id = scene_id;
    command->u.sprite.atlas_index = atlas_index;
    command->u.sprite.x = x;
    command->u.sprite.y = y;
    command->u.sprite.w = w;
    command->u.sprite.h = h;
    command->u.sprite.alpha = 255;
    command->u.sprite.rotated = 0;
    command->u.sprite.rotation = 0;
    command->u.sprite.dst_anchor_x = 0;
    command->u.sprite.dst_anchor_y = 0;
    command->u.sprite.src_anchor_x = 0;
    command->u.sprite.src_anchor_y = 0;
    command->u.sprite.scissor_x = 0;
    command->u.sprite.scissor_y = 0;
    command->u.sprite.scissor_w = 0;
    command->u.sprite.scissor_h = 0;
    command->u.sprite.mask_element_id = -1;
    command->u.sprite.mask_atlas_index = 0;
    command->u.sprite.tiled = 0;
}

static void
GameRunescape_EmitSpriteCommandRotated(
    struct LibToriRS_RenderCommand* command,
    int scene_id,
    int atlas_index,
    int x,
    int y,
    int w,
    int h,
    int rotation_r2pi2048)
{
    GameRunescape_EmitSpriteCommand(command, scene_id, atlas_index, x, y, w, h);
    if( rotation_r2pi2048 != 0 )
    {
        command->u.sprite.rotated = 1;
        command->u.sprite.rotation = rotation_r2pi2048;
    }
}

static void
GameRunescape_EmitScrollbarArrow(
    struct LibToriRS_RenderCommand* command,
    int scene_id,
    int x,
    int y,
    int rotation_r2pi2048)
{
    if( scene_id >= 0 )
    {
        GameRunescape_EmitSpriteCommandRotated(
            command,
            scene_id,
            0,
            x,
            y,
            UITREE_SCROLLBAR_THICKNESS,
            UITREE_SCROLLBAR_THICKNESS,
            rotation_r2pi2048);
    }
    else
    {
        GameRunescape_EmitScrollbarFill(
            command,
            x,
            y,
            UITREE_SCROLLBAR_THICKNESS,
            UITREE_SCROLLBAR_THICKNESS,
            UITREE_SCROLLBAR_GRIP_ARGB);
    }
}

static bool
GameRunescape_VerticalScrollbarGrip(
    struct StaticUIComponent* layer,
    int vh,
    int lh,
    int sy,
    int* grip_y,
    int* grip_size,
    int* track_h)
{
    *track_h = vh - 32;
    if( *track_h <= 0 )
        return false;
    *grip_size = (*track_h * lh) / layer->u.rs_layer.scroll_height;
    if( *grip_size < 8 )
        *grip_size = 8;
    if( *grip_size > *track_h )
        *grip_size = *track_h;
    int range = uitree_scroll_max_y(layer);
    *grip_y = range > 0 ? ((*track_h - *grip_size) * sy) / range : 0;
    return true;
}

static bool
GameRunescape_HorizontalScrollbarGrip(
    struct StaticUIComponent* layer,
    int sw,
    int lw,
    int sx,
    int* grip_x,
    int* grip_size,
    int* track_w)
{
    *track_w = sw - 32;
    if( *track_w <= 0 )
        return false;
    *grip_size = (*track_w * lw) / layer->u.rs_layer.scroll_width;
    if( *grip_size < 8 )
        *grip_size = 8;
    if( *grip_size > *track_w )
        *grip_size = *track_w;
    int range = uitree_scroll_max_x(layer);
    *grip_x = range > 0 ? ((*track_w - *grip_size) * sx) / range : 0;
    return true;
}

static bool
GameRunescape_EmitLayerScrollbars(
    struct GameRunescape* game,
    struct StaticUIComponent* layer,
    int step,
    struct LibToriRS_RenderCommand* command)
{
    if( !layer || layer->type != UIELEM_RS_LAYER || !command )
        return false;

    bool vscroll = uitree_scroll_layer_needs_vertical(layer);
    bool hscroll = uitree_scroll_layer_needs_horizontal(layer);
    if( !vscroll && !hscroll )
        return false;

    int lx = 0;
    int ly = 0;
    int lw = 0;
    int lh = 0;
    struct UITreeScrollClip clip = { 0 };
    GameRunescape_UIGetDrawContext(game, layer, &lx, &ly, &lw, &lh, &clip);

    int sx = 0;
    int sy = 0;
    if( layer->component_id >= 0 )
    {
        struct UITreeScrollState scroll_state = GameRunescape_UIScrollState(game);
        uitree_scroll_get_pos(&scroll_state, layer->component_id, &sx, &sy);
    }

    int const v_steps = vscroll ? UITREE_SCROLLBAR_V_DRAW_STEPS : 0;
    int const h_steps = hscroll ? UITREE_SCROLLBAR_H_DRAW_STEPS : 0;
    int const total_steps = v_steps + h_steps;
    if( step >= total_steps )
        return false;

    if( step < v_steps )
    {
        int sb_x = lx + lw;
        int vh = hscroll ? lh - UITREE_SCROLLBAR_THICKNESS : lh;
        int grip_y = 0;
        int grip_size = 0;
        int track_h = 0;
        int grip_y0 = 0;

        switch( step )
        {
        case 0:
            GameRunescape_EmitScrollbarArrow(
                command, game->ui_scrollbar0_scene_id, sb_x, ly, 0);
            return true;
        case 1:
            GameRunescape_EmitScrollbarArrow(
                command,
                game->ui_scrollbar1_scene_id,
                sb_x,
                ly + vh - UITREE_SCROLLBAR_THICKNESS,
                0);
            return true;
        case 2:
            if( !GameRunescape_VerticalScrollbarGrip(layer, vh, lh, sy, &grip_y, &grip_size, &track_h) )
                return false;
            GameRunescape_EmitScrollbarFill(
                command,
                sb_x,
                ly + UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_THICKNESS,
                track_h,
                UITREE_SCROLLBAR_TRACK_ARGB);
            return true;
        case 3:
            if( !GameRunescape_VerticalScrollbarGrip(layer, vh, lh, sy, &grip_y, &grip_size, &track_h) )
                return false;
            GameRunescape_EmitScrollbarFill(
                command,
                sb_x,
                ly + UITREE_SCROLLBAR_THICKNESS + grip_y,
                UITREE_SCROLLBAR_THICKNESS,
                grip_size,
                UITREE_SCROLLBAR_GRIP_ARGB);
            return true;
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
            if( !GameRunescape_VerticalScrollbarGrip(layer, vh, lh, sy, &grip_y, &grip_size, &track_h) )
                return false;
            grip_y0 = ly + UITREE_SCROLLBAR_THICKNESS + grip_y;
            switch( step )
            {
            case 4:
                GameRunescape_EmitScrollbarFill(
                    command, sb_x, grip_y0, 2, grip_size, UITREE_SCROLLBAR_GRIP_HI_ARGB);
                return true;
            case 5:
                GameRunescape_EmitScrollbarFill(
                    command, sb_x, grip_y0, UITREE_SCROLLBAR_THICKNESS, 2, UITREE_SCROLLBAR_GRIP_HI_ARGB);
                return true;
            case 6:
                GameRunescape_EmitScrollbarFill(
                    command,
                    sb_x + UITREE_SCROLLBAR_THICKNESS - 1,
                    grip_y0,
                    1,
                    grip_size,
                    UITREE_SCROLLBAR_GRIP_LO_ARGB);
                return true;
            case 7:
                if( grip_size > 1 )
                {
                    GameRunescape_EmitScrollbarFill(
                        command,
                        sb_x + UITREE_SCROLLBAR_THICKNESS - 2,
                        grip_y0 + 1,
                        1,
                        grip_size - 1,
                        UITREE_SCROLLBAR_GRIP_LO_ARGB);
                }
                return true;
            case 8:
                GameRunescape_EmitScrollbarFill(
                    command,
                    sb_x,
                    grip_y0 + grip_size - 1,
                    UITREE_SCROLLBAR_THICKNESS,
                    1,
                    UITREE_SCROLLBAR_GRIP_LO_ARGB);
                return true;
            default:
                break;
            }
            break;
        default:
            break;
        }
        return false;
    }

    int hstep = step - v_steps;
    int sb_y = ly + lh - UITREE_SCROLLBAR_THICKNESS;
    int sw = vscroll ? lw - UITREE_SCROLLBAR_THICKNESS : lw;
    int grip_x = 0;
    int grip_size = 0;
    int track_w = 0;
    int grip_x0 = 0;
    int const h_arrow_rotation = 512;

    switch( hstep )
    {
    case 0:
        GameRunescape_EmitScrollbarArrow(
            command, game->ui_scrollbar0_scene_id, lx, sb_y, h_arrow_rotation);
        return true;
    case 1:
        GameRunescape_EmitScrollbarArrow(
            command,
            game->ui_scrollbar1_scene_id,
            lx + sw - UITREE_SCROLLBAR_THICKNESS,
            sb_y,
            h_arrow_rotation);
        return true;
    case 2:
        if( !GameRunescape_HorizontalScrollbarGrip(layer, sw, lw, sx, &grip_x, &grip_size, &track_w) )
            return false;
        GameRunescape_EmitScrollbarFill(
            command,
            lx + UITREE_SCROLLBAR_THICKNESS,
            sb_y,
            track_w,
            UITREE_SCROLLBAR_THICKNESS,
            UITREE_SCROLLBAR_TRACK_ARGB);
        return true;
    case 3:
        if( !GameRunescape_HorizontalScrollbarGrip(layer, sw, lw, sx, &grip_x, &grip_size, &track_w) )
            return false;
        GameRunescape_EmitScrollbarFill(
            command,
            lx + UITREE_SCROLLBAR_THICKNESS + grip_x,
            sb_y,
            grip_size,
            UITREE_SCROLLBAR_THICKNESS,
            UITREE_SCROLLBAR_GRIP_ARGB);
        return true;
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
        if( !GameRunescape_HorizontalScrollbarGrip(layer, sw, lw, sx, &grip_x, &grip_size, &track_w) )
            return false;
        grip_x0 = lx + UITREE_SCROLLBAR_THICKNESS + grip_x;
        switch( hstep )
        {
        case 4:
            GameRunescape_EmitScrollbarFill(
                command, grip_x0, sb_y, 2, UITREE_SCROLLBAR_THICKNESS, UITREE_SCROLLBAR_GRIP_HI_ARGB);
            return true;
        case 5:
            GameRunescape_EmitScrollbarFill(
                command, grip_x0, sb_y, grip_size, 2, UITREE_SCROLLBAR_GRIP_HI_ARGB);
            return true;
        case 6:
            GameRunescape_EmitScrollbarFill(
                command,
                grip_x0 + grip_size - 1,
                sb_y,
                1,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_GRIP_LO_ARGB);
            return true;
        case 7:
            if( UITREE_SCROLLBAR_THICKNESS > 1 )
            {
                GameRunescape_EmitScrollbarFill(
                    command,
                    grip_x0 + grip_size - 2,
                    sb_y + 1,
                    1,
                    UITREE_SCROLLBAR_THICKNESS - 1,
                    UITREE_SCROLLBAR_GRIP_LO_ARGB);
            }
            return true;
        case 8:
            GameRunescape_EmitScrollbarFill(
                command,
                grip_x0,
                sb_y + UITREE_SCROLLBAR_THICKNESS - 1,
                grip_size,
                1,
                UITREE_SCROLLBAR_GRIP_LO_ARGB);
            return true;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return false;
}

static void
runescape_ui_model_compute_position(
    struct ToriDraw_ModelHandle hnd,
    int zoom,
    int xan,
    int yan,
    int viewport_h,
    struct ToriDraw_Position* out)
{
    assert(out);

    if( zoom <= 0 )
        zoom = 2000;

    int sin_pitch = (ToriDraw_Sin(xan) * zoom) >> 16;
    int cos_pitch = (ToriDraw_Cos(xan) * zoom) >> 16;
    struct ToriDraw_BoundsCylinder* bounds = ToriDraw_ModelGetBoundsCylinder(hnd);
    int model_height = bounds ? (bounds->max_y - bounds->min_y) : 0;

    memset(out, 0, sizeof(*out));
    out->yaw = yan;
    out->y = sin_pitch - (viewport_h / 2) + (model_height / 2);
    out->z = cos_pitch;
}

static bool
GameRunescape_EmitUIComponent(
    struct GameRunescape* game,
    struct StaticUIComponent* component,
    int32_t node_index,
    struct LibToriRS_RenderCommand* command)
{
    assert(!(!component || !command));

    if( !uitree_component_should_emit(component, &game->ui_host) )
        return false;

    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    struct UITreeScrollClip clip = { 0 };
    GameRunescape_UIGetDrawContext(game, component, &bx, &by, &bw, &bh, &clip);

    switch( component->type )
    {
    case UIELEM_RS_LAYER:
    {
        return GameRunescape_EmitLayerScrollbars(
            game, component, game->frame.ui_scrollbar_step, command);
    }
    case UIELEM_BUILTIN_SPRITE:
    case UIELEM_BUILTIN_TAB_ICONS:
    case UIELEM_RS_GRAPHIC:
    {
        int scene_id = component->u.sprite.scene_id;
        int atlas_index = component->u.sprite.atlas_index;
        if( component->type == UIELEM_BUILTIN_TAB_ICONS )
        {
            scene_id = component->u.tab_icon.scene_id;
            atlas_index = component->u.tab_icon.atlas_index;
        }
        else if( component->type == UIELEM_RS_GRAPHIC )
        {
            if( component->u.rs_graphic.graphic_hitbox_only )
                return false;
            scene_id = component->u.rs_graphic.scene_id;
            atlas_index = component->u.rs_graphic.atlas_index;
            bool const active = uitree_component_is_active_host(&game->ui_host, component);
            if( active )
            {
                if( component->u.rs_graphic.scene_id_active >= 0 )
                {
                    scene_id = component->u.rs_graphic.scene_id_active;
                    atlas_index = component->u.rs_graphic.atlas_index_active;
                }
            }
            ui_active_debug_log(
                "emit rs_graphic id=%d is_active=%d scene_id=%d atlas=%d "
                "inactive_scene=%d active_scene=%d",
                component->component_id,
                active ? 1 : 0,
                scene_id,
                atlas_index,
                component->u.rs_graphic.scene_id,
                component->u.rs_graphic.scene_id_active);
            if( scene_id < 0 )
                return false;
        }
        bool const tiled =
            component->type == UIELEM_RS_GRAPHIC && component->u.rs_graphic.tiled;
        if( !tiled && (bw <= 0 || bh <= 0) && scene_id >= 0 && game->scene )
        {
            int sprite_count = 0;
            struct ToriDraw_Sprite** sprites =
                ToriDraw_SceneSpriteGet(game->scene, scene_id, &sprite_count);
            struct ToriDraw_Sprite* sp =
                (sprites && atlas_index >= 0 && atlas_index < sprite_count)
                    ? sprites[atlas_index]
                    : NULL;
            if( sp )
            {
                if( bw <= 0 )
                    bw = sp->crop_width > 0 ? sp->crop_width : sp->width;
                if( bh <= 0 )
                    bh = sp->crop_height > 0 ? sp->crop_height : sp->height;
            }
        }
        GameRunescape_AssertSceneSpriteReady(
            game, scene_id, atlas_index, "ui_sprite");
        GameRunescape_EmitSpriteCommand(command, scene_id, atlas_index, bx, by, bw, bh);
        if( tiled )
            command->u.sprite.tiled = 1;
        GameRunescape_ApplyScissorToSprite(command, &clip);
        return true;
    }
    case UIELEM_BUILTIN_COMPASS:
    {
        int scene_id = component->u.sprite.scene_id;
        int atlas_index = component->u.sprite.atlas_index;
        GameRunescape_AssertSceneSpriteReady(
            game, scene_id, atlas_index, "compass");
        GameRunescape_EmitSpriteCommand(command, scene_id, atlas_index, bx, by, bw, bh);
        command->u.sprite.rotated = 1;
        command->u.sprite.rotation = uitree_component_sprite_rotation(component, &game->ui_host);
        command->u.sprite.dst_anchor_x = component->position.anchor_x;
        command->u.sprite.dst_anchor_y = component->position.anchor_y;
        int sprite_count = 0;
        struct ToriDraw_Sprite** sprites =
            game->scene ? ToriDraw_SceneSpriteGet(game->scene, scene_id, &sprite_count) : NULL;
        struct ToriDraw_Sprite* sp = (sprites && atlas_index >= 0 && atlas_index < sprite_count)
                                         ? sprites[atlas_index]
                                         : NULL;
        int sw = sp ? (sp->crop_width > 0 ? sp->crop_width : sp->width) : bw;
        int sh = sp ? (sp->crop_height > 0 ? sp->crop_height : sp->height) : bh;
        command->u.sprite.src_anchor_x = sw >> 1;
        command->u.sprite.src_anchor_y = sh >> 1;
        command->u.sprite.scissor_x = bx;
        command->u.sprite.scissor_y = by;
        command->u.sprite.scissor_w = bw;
        command->u.sprite.scissor_h = bh;
        return true;
    }
    case UIELEM_BUILTIN_CROSS:
    {
        int scene_id = component->u.sprite.scene_id;

        int cx = bx;
        int cy = by;
        if( game->ui_host.get_cross_position )
            game->ui_host.get_cross_position(game->ui_host.user, &cx, &cy);
        cx -= component->position.anchor_x;
        cy -= component->position.anchor_y;

        int atlas_index = 0;
        if( game->ui_host.get_cross_atlas_frame )
            atlas_index = game->ui_host.get_cross_atlas_frame(game->ui_host.user);
        GameRunescape_AssertSceneSpriteReady(game, scene_id, atlas_index, "cross");

        GameRunescape_EmitSpriteCommand(command, scene_id, atlas_index, cx, cy, bw, bh);
        return true;
    }
    case UIELEM_BUILTIN_MINIMENU:
    {
        if( !game->minimenu.visible )
            return false;

        int const mx = game->minimenu.x;
        int const my = game->minimenu.y;
        int const mw = game->minimenu.width;
        int const mh = game->minimenu.height;
        int const font_id = component->u.minimenu.font_id;
        struct UIMinimenuLayout const layout = game->minimenu.layout;
        int step = game->frame.ui_minimenu_step;

        if( step == 0 )
        {
            command->kind = TORIRSRC_FILL_RECT;
            command->u.fill_rect.x = mx;
            command->u.fill_rect.y = my;
            command->u.fill_rect.w = mw;
            command->u.fill_rect.h = mh;
            command->u.fill_rect.argb = 0xFF000000u | (uint32_t)OPTIONS_MENU;
            game->frame.ui_minimenu_step = 1;
            return true;
        }
        if( step == 1 )
        {
            command->kind = TORIRSRC_FILL_RECT;
            command->u.fill_rect.x = mx + 1;
            command->u.fill_rect.y = my + 1;
            command->u.fill_rect.w = mw - 2;
            command->u.fill_rect.h = layout.header_bar_h;
            command->u.fill_rect.argb = 0xFF000000u;
            game->frame.ui_minimenu_step = 2;
            return true;
        }
        if( step == 2 )
        {
            command->kind = TORIRSRC_FILL_RECT;
            command->u.fill_rect.x = mx + 1;
            command->u.fill_rect.y = my + layout.separator_y;
            command->u.fill_rect.w = mw - 2;
            command->u.fill_rect.h = 1;
            command->u.fill_rect.argb = 0xFF000000u;
            game->frame.ui_minimenu_step = 3;
            return true;
        }
        if( step == 3 )
        {
            command->kind = TORIRSRC_FILL_RECT;
            command->u.fill_rect.x = mx + 1;
            command->u.fill_rect.y = my + mh - 2;
            command->u.fill_rect.w = mw - 2;
            command->u.fill_rect.h = 1;
            command->u.fill_rect.argb = 0xFF000000u;
            game->frame.ui_minimenu_step = 4;
            return true;
        }
        if( step == 4 )
        {
            command->kind = TORIRSRC_FILL_RECT;
            command->u.fill_rect.x = mx + 1;
            command->u.fill_rect.y = my + layout.separator_y;
            command->u.fill_rect.w = 1;
            command->u.fill_rect.h = mh - layout.border_inset;
            command->u.fill_rect.argb = 0xFF000000u;
            game->frame.ui_minimenu_step = 5;
            return true;
        }
        if( step == 5 )
        {
            command->kind = TORIRSRC_FILL_RECT;
            command->u.fill_rect.x = mx + mw - 2;
            command->u.fill_rect.y = my + layout.separator_y;
            command->u.fill_rect.w = 1;
            command->u.fill_rect.h = mh - layout.border_inset;
            command->u.fill_rect.argb = 0xFF000000u;
            game->frame.ui_minimenu_step = 6;
            return true;
        }
        if( step == 6 )
        {
            int const lh = game_runescape_minimenu_draw_line_height(game, font_id, &layout);

            GameRunescape_AssertMinimenuFontReady(game, font_id, "minimenu header");
            command->kind = TORIRSRC_FONT;
            command->u.font.font_id = font_id;
            command->u.font.x = mx + 3;
            command->u.font.y = my + lh;
            command->u.font.color = OPTIONS_MENU;
            command->u.font.center = 0;
            command->u.font.shadowed = 0;
            command->u.font.width = mw - 6;
            command->u.font.height = layout.header_bar_h;
            command->u.font.text = "Choose Option";
            game->frame.ui_minimenu_step = 7;
            return true;
        }

        int const opt_draw = step - 7;
        if( opt_draw < game->minimenu.option_count )
        {
            int const i = opt_draw;
            int const lh = game_runescape_minimenu_draw_line_height(game, font_id, &layout);
            struct UIMinimenuLayout const draw_layout =
                ui_minimenu_layout_from_line_height(lh);
            int const row = (game->minimenu.option_count - 1 - i) * draw_layout.row_stride;
            int const row_top = my + row + draw_layout.option_base_y;
            int const hovered = game->minimenu.hovered_option == i;

            GameRunescape_AssertMinimenuFontReady(game, font_id, "minimenu option");
            command->kind = TORIRSRC_FONT;
            command->u.font.font_id = font_id;
            command->u.font.x = mx + 3;
            command->u.font.y = row_top;
            command->u.font.color = hovered ? YELLOW : WHITE;
            command->u.font.center = 0;
            command->u.font.shadowed = 1;
            command->u.font.width = mw - 6;
            command->u.font.height = layout.row_stride;
            command->u.font.text = game->minimenu.options[i].text;
            game->frame.ui_minimenu_step = step + 1;
            return true;
        }

        game->frame.ui_minimenu_step = 0;
        return false;
    }
    case UIELEM_BUILTIN_CHAT_BUTTON:
    {
        int font_id = component->u.chat_button.font_id;
        if( font_id < 0 || font_id > 3 )
            font_id = 1;
        GameRunescape_AssertSceneFontReady(game, font_id, "chat_button");
        int const step = game->frame.ui_chat_button_step;
        if( ui_chat_button_emit(
                game,
                component,
                step,
                command,
                game->ui_text_scratch,
                sizeof(game->ui_text_scratch)) )
        {
            GameRunescape_ApplyScissorToFont(command, &clip);
            if( step + 1 < ui_chat_button_emit_step_count(component) )
                game->frame.ui_chat_button_step = step + 1;
            else
                game->frame.ui_chat_button_step = 0;
            return true;
        }
        game->frame.ui_chat_button_step = 0;
        return false;
    }
    case UIELEM_BUILTIN_MINIMAP:
    {
        int scene_id = component->u.minimap.scene_id;
        GameRunescape_AssertSceneSpriteReady(game, scene_id, 0, "minimap");
        GameRunescape_EmitSpriteCommand(command, scene_id, 0, bx, by, bw, bh);
        command->u.sprite.rotated = 1;
        command->u.sprite.rotation = uitree_component_sprite_rotation(component, &game->ui_host);
        command->u.sprite.dst_anchor_x = component->position.anchor_x;
        command->u.sprite.dst_anchor_y = component->position.anchor_y;
        if( game->ui_host.get_minimap_anchor )
        {
            game->ui_host.get_minimap_anchor(
                game->ui_host.user,
                &command->u.sprite.src_anchor_x,
                &command->u.sprite.src_anchor_y);
        }
        command->u.sprite.scissor_x = bx;
        command->u.sprite.scissor_y = by;
        command->u.sprite.scissor_w = bw;
        command->u.sprite.scissor_h = bh;
        return true;
    }
    case UIELEM_BUILTIN_REDSTONE_TAB:
    {
        int scene_id = component->u.redstone_tab.scene_id;
        int atlas_index = component->u.redstone_tab.atlas_index;
        if( game->ui_host.get_selected_tab &&
            game->ui_host.get_selected_tab(game->ui_host.user) == component->u.redstone_tab.tabno )
        {
            if( component->u.redstone_tab.scene_id_active >= 0 )
            {
                scene_id = component->u.redstone_tab.scene_id_active;
                atlas_index = component->u.redstone_tab.atlas_index_active;
            }
        }
        GameRunescape_AssertSceneSpriteReady(
            game, scene_id, atlas_index, "redstone_tab");
        GameRunescape_EmitSpriteCommand(command, scene_id, atlas_index, bx, by, bw, bh);
        return true;
    }
    case UIELEM_RS_TEXT:
        if( !uitree_component_text_source_host(&game->ui_host, component) )
            return false;
        GameRunescape_AssertSceneFontReady(
            game, component->u.rs_text.font_id, "rs_text");
        {
            struct ToriDraw_Font* font =
                ToriDraw_SceneFontGet(game->scene, component->u.rs_text.font_id);
            assert(font && "rs_text: font missing after AssertSceneFontReady");
            int const lh = font->line_height > 0 ? font->line_height : 1;
            int draw_x = bx;
            if( component->u.rs_text.center && bw > 0 )
                draw_x = bx + bw / 2;
            command->kind = TORIRSRC_FONT;
            command->u.font.font_id = component->u.rs_text.font_id;
            command->u.font.x = draw_x;
            command->u.font.y = by + lh;
            {
                struct UITreeHoverIds const hover_ids = GameRunescape_UIHoverIds(game);
                command->u.font.color = uitree_component_text_color_host(
                    component,
                    &hover_ids,
                    &game->ui_host,
                    component->u.rs_text.color);
            }
            command->u.font.center = component->u.rs_text.center;
            command->u.font.shadowed = component->u.rs_text.shadowed;
            command->u.font.width = bw;
            command->u.font.height = bh;
            command->u.font.text = uitree_expand_text_host(
                &game->ui_host, component, game->ui_text_scratch, sizeof(game->ui_text_scratch));
            command->u.font.scissor_x = 0;
            command->u.font.scissor_y = 0;
            command->u.font.scissor_w = 0;
            command->u.font.scissor_h = 0;
            GameRunescape_ApplyScissorToFont(command, &clip);
        }
        return true;
    case UIELEM_RS_RECT:
    {
        int color = GameRunescape_UIRectColor(game, component, node_index);
        if( color == 0 && !component->u.rs_rect.filled )
            return false;
        command->kind = TORIRSRC_FILL_RECT;
        command->u.fill_rect.x = bx;
        command->u.fill_rect.y = by;
        command->u.fill_rect.w = bw;
        command->u.fill_rect.h = bh;
        command->u.fill_rect.argb = color;
        command->u.fill_rect.scissor_x = 0;
        command->u.fill_rect.scissor_y = 0;
        command->u.fill_rect.scissor_w = 0;
        command->u.fill_rect.scissor_h = 0;
        GameRunescape_ApplyScissorToFillRect(command, &clip);
        return true;
    }
    case UIELEM_RS_LINE:
    {
        int color = component->u.rs_line.color;
        if( color == 0 )
            return false;
        command->kind = TORIRSRC_FILL_RECT;
        command->u.fill_rect.scissor_x = 0;
        command->u.fill_rect.scissor_y = 0;
        command->u.fill_rect.scissor_w = 0;
        command->u.fill_rect.scissor_h = 0;
        if( component->u.rs_line.horizontal )
        {
            int lh = component->u.rs_line.line_width > 0 ? component->u.rs_line.line_width : 1;
            command->u.fill_rect.x = bx;
            command->u.fill_rect.y = by + (bh - lh) / 2;
            command->u.fill_rect.w = bw;
            command->u.fill_rect.h = lh;
        }
        else
        {
            int lw = component->u.rs_line.line_width > 0 ? component->u.rs_line.line_width : 1;
            command->u.fill_rect.x = bx + (bw - lw) / 2;
            command->u.fill_rect.y = by;
            command->u.fill_rect.w = lw;
            command->u.fill_rect.h = bh;
        }
        command->u.fill_rect.argb = color;
        GameRunescape_ApplyScissorToFillRect(command, &clip);
        return true;
    }
    case UIELEM_INV_GRID:
    {
        int cols = component->u.inv_grid.cols > 0 ? component->u.inv_grid.cols : 4;
        int rows = component->u.inv_grid.rows > 0 ? component->u.inv_grid.rows : 7;
        struct UIInvGridLayout layout = {
            .cols = cols,
            .rows = rows,
            .margin_x = component->u.inv_grid.margin_x,
            .margin_y = component->u.inv_grid.margin_y,
            .offset_x = component->u.inv_grid.inv_slot_offset_x,
            .offset_y = component->u.inv_grid.inv_slot_offset_y,
        };
        int const slot_limit = ui_inv_slot_view_grid_slot_limit(&layout);
        int slot = game->frame.ui_inv_slot;

        if( slot >= slot_limit )
        {
            game->frame.ui_inv_slot = 0;
            return false;
        }

        int slot_x = 0;
        int slot_y = 0;
        ui_inv_slot_view_grid_rect(bx, by, &layout, slot, &slot_x, &slot_y, NULL, NULL);

        struct UIInvSlotData slot_data;
        int obj_id = 0;
        int scene_id = -1;
        int atlas_index = 0;
        if( game->ui_host.get_inv_source_slot )
        {
            game->ui_host.get_inv_source_slot(
                game->ui_host.user,
                component->u.inv_grid.inv_source_id,
                slot,
                &slot_data);
            obj_id = slot_data.obj_id;
            scene_id = slot_data.scene_id;
            atlas_index = slot_data.atlas_index;
        }

        if( obj_id > 0 && scene_id >= 0 )
        {
            GameRunescape_EmitSpriteCommand(
                command, scene_id, atlas_index, slot_x, slot_y, 32, 32);
            game->frame.ui_inv_slot = slot + 1;
            return true;
        }

        if( slot < UI_INV_SLOT_OFFSET_MAX )
        {
            int bg_scene = component->u.inv_grid.inv_slot_bg_scene_id[slot];
            int bg_atlas = component->u.inv_grid.inv_slot_bg_atlas_index[slot];
            if( bg_scene >= 0 )
            {
                GameRunescape_EmitSpriteCommand(
                    command, bg_scene, bg_atlas, slot_x, slot_y, 32, 32);
                game->frame.ui_inv_slot = slot + 1;
                return true;
            }
        }

        game->frame.ui_inv_slot = slot + 1;
        return false;
    }
    case UIELEM_INV_SLOT:
    {
        struct UIInvSlotData slot_data;
        int obj_id = 0;
        int scene_id = -1;
        int atlas_index = 0;
        if( game->ui_host.get_inv_source_slot )
        {
            game->ui_host.get_inv_source_slot(
                game->ui_host.user,
                component->u.inv_slot.inv_source_id,
                component->u.inv_slot.slot,
                &slot_data);
            obj_id = slot_data.obj_id;
            scene_id = slot_data.scene_id;
            atlas_index = slot_data.atlas_index;
        }

        if( obj_id > 0 && scene_id >= 0 )
        {
            int ix = bx;
            int iy = by;
            if( component->u.inv_slot.center_icon )
            {
                ui_inv_slot_view_centered_rect(
                    bx, by, bw, bh, UI_INV_SLOT_ICON_SIZE, &ix, &iy, NULL, NULL);
            }
            GameRunescape_EmitSpriteCommand(command, scene_id, atlas_index, ix, iy, 32, 32);
            return true;
        }
        return false;
    }
    case UIELEM_RS_INV_TEXT:
    {
        int cols = component->u.rs_inv_text.cols > 0 ? component->u.rs_inv_text.cols : 1;
        int rows = component->u.rs_inv_text.rows > 0 ? component->u.rs_inv_text.rows : 1;
        int margin_x = component->u.rs_inv_text.margin_x;
        int margin_y = component->u.rs_inv_text.margin_y;
        int const total_slots = cols * rows;
        int slot = game->frame.ui_inv_slot;

        if( slot >= total_slots )
        {
            game->frame.ui_inv_slot = 0;
            return false;
        }

        int col = slot % cols;
        int row = slot / cols;
        int slot_x = bx + col * (margin_x + RUNESCAPE_INV_TEXT_CELL_W);
        int slot_y = by + row * (margin_y + RUNESCAPE_INV_TEXT_CELL_H);

        game->frame.ui_inv_slot = slot + 1;

        int obj_id = 0;
        if( game->ui_host.get_inv_source_slot )
        {
            struct UIInvSlotData slot_data;
            if( game->ui_host.get_inv_source_slot(
                    game->ui_host.user, component->u.rs_inv_text.inv_source_id, slot, &slot_data) )
                obj_id = slot_data.obj_id;
        }
        if( obj_id <= 0 )
            return false;

        char const* label = GameRunescape_ObjDisplayName(game, obj_id);
        if( !label || label[0] == '\0' )
            return false;

        GameRunescape_AssertSceneFontReady(
            game, component->u.rs_inv_text.font_id, "rs_inv_text");
        struct ToriDraw_Font* font =
            ToriDraw_SceneFontGet(game->scene, component->u.rs_inv_text.font_id);
        if( !font )
            return false;

        int const lh = font->line_height > 0 ? font->line_height : RUNESCAPE_INV_TEXT_CELL_H;
        int draw_x = slot_x;
        if( component->u.rs_inv_text.center && bw > 0 )
            draw_x = slot_x + bw / 2;

        command->kind = TORIRSRC_FONT;
        command->u.font.font_id = component->u.rs_inv_text.font_id;
        command->u.font.x = draw_x;
        command->u.font.y = slot_y + lh;
        command->u.font.color = component->u.rs_inv_text.color;
        command->u.font.center = component->u.rs_inv_text.center;
        command->u.font.shadowed = component->u.rs_inv_text.shadowed;
        command->u.font.width = RUNESCAPE_INV_TEXT_CELL_W;
        command->u.font.height = RUNESCAPE_INV_TEXT_CELL_H;
        command->u.font.text = label;
        return true;
    }
    case UIELEM_RS_MODEL:
    {
        int const model_id = component->u.rs_model.gamecache_model_id;
        int const zoom = component->u.rs_model.zoom;
        int const xan = component->u.rs_model.xan;
        int const yan = component->u.rs_model.yan;

        if( !game->scene || !ToriDraw_SceneModelHas(game->scene, model_id) )
        {
            fprintf(stderr,
                "rs_emit_ui_component_command: model not in scene model_id=%d component_id=%d\n",
                model_id,
                component->component_id);
            assert(
                game->scene && ToriDraw_SceneModelHas(game->scene, model_id) &&
                "UIELEM_RS_MODEL draw requested but model not in scene");
            return false;
        }

        struct ToriDraw_ModelHandle hnd = ToriDraw_SceneModelGet(game->scene, model_id);
        int const step = game->frame.ui_model_step;

        if( step == 0 )
        {
            command->kind = TORIRSRC_END_2D;
            game->frame.ui_model_step = 1;
            return true;
        }
        if( step == 1 )
        {
            struct ToriDraw_ViewPort ui_vp = { 0 };
            ui_vp.width = bw;
            ui_vp.height = bh;
            ui_vp.x_center = bx + bw / 2;
            ui_vp.y_center = by + bh / 2;
            ui_vp.clip_left = bx;
            ui_vp.clip_top = by;
            ui_vp.clip_right = bx + bw;
            ui_vp.clip_bottom = by + bh;

            struct ToriDraw_Camera ui_cam = { 0 };
            ui_cam.pitch = xan;
            ui_cam.yaw = 0;
            ui_cam.roll = 0;
            ui_cam.fov_rpi2048 = 512;
            ui_cam.near_plane_z = 1;

            command->kind = TORIRSRC_BEGIN_3D;
            command->u.begin_3d.view_port = ui_vp;
            command->u.begin_3d.camera = ui_cam;
            memset(&command->u.begin_3d.camera_position, 0, sizeof(struct ToriDraw_Position));
            game->frame.ui_model_step = 2;
            return true;
        }
        if( step == 2 )
        {
            struct ToriDraw_ViewPort ui_vp = { 0 };
            ui_vp.width = bw;
            ui_vp.height = bh;
            ui_vp.x_center = bx + bw / 2;
            ui_vp.y_center = by + bh / 2;
            ui_vp.clip_left = bx;
            ui_vp.clip_top = by;
            ui_vp.clip_right = bx + bw;
            ui_vp.clip_bottom = by + bh;

            struct ToriDraw_Camera ui_cam = { 0 };
            ui_cam.pitch = xan;
            ui_cam.yaw = 0;
            ui_cam.roll = 0;
            ui_cam.fov_rpi2048 = 512;
            ui_cam.near_plane_z = 1;

            struct ToriDraw_Position position = { 0 };
            runescape_ui_model_compute_position(hnd, zoom, xan, yan, bh, &position);

            if( ToriDraw_RenderModel1Project(hnd, game->scene, &position, &ui_vp, &ui_cam) !=
                    TORIDRAW_CULL_VISIBLE ||
                ToriDraw_RenderModel2SortFaces(hnd, game->scene) <= 0 )
            {
                fprintf(stderr,
                    "rs_emit_ui_component_command: UI model project failed model_id=%d "
                    "component_id=%d\n",
                    model_id,
                    component->component_id);
                assert(false && "UIELEM_RS_MODEL project/cull failed");
                game->frame.ui_model_step = 0;
                return false;
            }

            command->kind = TORIRSRC_DRAW_MODEL;
            command->u.model.model = hnd;
            command->u.model.position = position;
            command->u.model.world_position = position;
            command->u.model.element_id = -1;
            command->u.model.animation = NULL;
            command->u.model.anim_index = 0;
            command->u.model.anim_frame = 0;
            command->u.model.dynamic = false;
            game->frame.ui_model_step = 3;
            return true;
        }
        if( step == 3 )
        {
            command->kind = TORIRSRC_END_3D;
            game->frame.ui_model_step = 4;
            return true;
        }

        command->kind = TORIRSRC_BEGIN_2D;
        game->frame.ui_model_step = 0;
        return true;
    }
    default:
        return false;
    }
}

static enum RsPhaseResult
rs_phase_gc_events(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    struct ToriDraw_EventQueue* eq = game->scene ? ToriDraw_SceneEvents(game->scene) : NULL;
    if( eq )
    {
        while( game->frame.event_index < eq->count )
        {
            const struct ToriDraw_Event* ev = &eq->events[game->frame.event_index++];
            if( GameRunescape_TranslateGCEvent(ev, command) )
                return RS_PHASE_YIELD;
        }
    }
    game->frame.phase = RS_FRAME_PHASE_BEGIN_3D;
    game->frame.event_index = 0;
    return RS_PHASE_ADVANCE;
}

static enum RsPhaseResult
rs_phase_begin_3d(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    if( game->frame.world_emitted )
    {
        game->frame.phase = RS_FRAME_PHASE_END_3D;
        return RS_PHASE_ADVANCE;
    }

    command->kind = TORIRSRC_BEGIN_3D;
    command->u.begin_3d.view_port = game->world_view_port;
    command->u.begin_3d.camera = *game->camera;
    command->u.begin_3d.camera_position.x = -game->camera_position->x;
    command->u.begin_3d.camera_position.y = -game->camera_position->y;
    command->u.begin_3d.camera_position.z = -game->camera_position->z;
    game->frame.world_emitted = true;
    game->frame.phase = RS_FRAME_PHASE_MODELS;
    game->frame.element_index = 0;
    game->frame.painter_command_index = 0;
    game->frame.painter_paint_done = false;
    return RS_PHASE_YIELD;
}

static bool
rs_resolve_painter_command(
    struct GameRunescape* game,
    struct World* world,
    const struct PaintersElementCommand* cmd,
    int* element_id,
    enum WorldPickType* pick_type,
    int* tile_x,
    int* tile_z,
    int* tile_level)
{
    *element_id = -1;
    *pick_type = WORLD_PICK_SCENERY;
    *tile_x = -1;
    *tile_z = -1;
    *tile_level = -1;

    switch( cmd->_bf_kind )
    {
    case PNTR_CMD_ELEMENT:
        *element_id = (int)cmd->_entity._bf_entity;
        if( ToriDraw_SceneElementIsLive(game->scene, *element_id) )
        {
            struct ToriDraw_SceneElement* element =
                ToriDraw_SceneElementGet(game->scene, *element_id);
            if( element && element->dynamic )
                *pick_type = rs_classify_dynamic_pick_type(game, *element_id);
            else
                *pick_type = WORLD_PICK_SCENERY;
        }
        break;
    case PNTR_CMD_TERRAIN:
        *tile_x = (int)cmd->_terrain._bf_terrain_x;
        *tile_z = (int)cmd->_terrain._bf_terrain_z;
        *tile_level = (int)cmd->_terrain._bf_terrain_y;
        *element_id = world_terrain_element_at(world, *tile_x, *tile_z, *tile_level);
        *pick_type = WORLD_PICK_TERRAIN;
        break;
    default:
        break;
    }

    return *element_id >= 0;
}

void
GameRunescape_RefreshPicksetAtMouse(
    struct GameRunescape* game,
    int mouse_x,
    int mouse_y)
{
    struct World* world;
    int saved_mouse_x;
    int saved_mouse_y;
    bool saved_mouse_in_viewport;

    if( !game || !game->scene || !game->world )
        return;

    world = game->world;
    saved_mouse_x = game->mouse_x;
    saved_mouse_y = game->mouse_y;
    saved_mouse_in_viewport = game->mouse_in_viewport;

    GameRunescape_UpdateWorldViewport(game);
    game->mouse_x = mouse_x;
    game->mouse_y = mouse_y;
    game->mouse_in_viewport = game_runescape_mouse_in_world_viewport(game, mouse_x, mouse_y);
    world_pickset_reset(&game->pickset);

    if( world->load_complete && world->painter && game->painter_buffer )
    {
        painter_set_camera_angles(world->painter, game->camera->pitch, game->camera->yaw);
        painter_set_level_mask(world->painter, 0xF);
        int camera_sx;
        int camera_sz;
        int camera_slevel;
        GameRunescape_CameraTile(game, &camera_sx, &camera_sz, &camera_slevel);
        painter_paint_bucket(
            world->painter, game->painter_buffer, camera_sx, camera_sz, camera_slevel);

        for( int i = 0; i < game->painter_buffer->command_count; i++ )
        {
            const struct PaintersElementCommand* cmd = &game->painter_buffer->commands[i];
            int element_id;
            enum WorldPickType pick_type;
            int tile_x;
            int tile_z;
            int tile_level;

            if( !rs_resolve_painter_command(
                    game, world, cmd, &element_id, &pick_type, &tile_x, &tile_z, &tile_level) )
                continue;

            (void)game_runescape_project_and_pick_element(
                game, element_id, pick_type, tile_x, tile_z, tile_level, true);
        }
    }

    {
        int const slot_count = ToriDraw_SceneElementSlotCount(game->scene);
        for( int element_id = 0; element_id < slot_count; element_id++ )
        {
            enum WorldPickType pick_type = WORLD_PICK_SCENERY;
            if( !ToriDraw_SceneElementIsLive(game->scene, element_id) )
                continue;

            struct ToriDraw_SceneElement* element =
                ToriDraw_SceneElementGet(game->scene, element_id);
            if( element && element->dynamic )
                pick_type = rs_classify_dynamic_pick_type(game, element_id);

            (void)game_runescape_project_and_pick_element(
                game, element_id, pick_type, -1, -1, -1, true);
        }
    }

    game->mouse_x = saved_mouse_x;
    game->mouse_y = saved_mouse_y;
    game->mouse_in_viewport = saved_mouse_in_viewport;
}

static enum RsPhaseResult
rs_phase_models(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    struct World* world = game->world;
    if( world && world->load_complete && world->painter && game->painter_buffer &&
        !game->frame.painter_paint_done )
    {
        painter_set_camera_angles(world->painter, game->camera->pitch, game->camera->yaw);
        painter_set_level_mask(world->painter, 0xF);
        int camera_sx;
        int camera_sz;
        int camera_slevel;
        GameRunescape_CameraTile(game, &camera_sx, &camera_sz, &camera_slevel);
        // painter_paint_world3d(
        //     world->painter, game->painter_buffer, camera_sx, camera_sz, camera_slevel);
        painter_paint_bucket(
            world->painter, game->painter_buffer, camera_sx, camera_sz, camera_slevel);
        game->frame.painter_paint_done = true;
    }

    if( world && world->load_complete && world->painter && game->painter_buffer &&
        game->frame.painter_paint_done )
    {
        while( game->frame.painter_command_index < game->painter_buffer->command_count )
        {
            const struct PaintersElementCommand* cmd =
                &game->painter_buffer->commands[game->frame.painter_command_index++];
            int element_id;
            enum WorldPickType pick_type;
            int tile_x;
            int tile_z;
            int tile_level;

            if( !rs_resolve_painter_command(
                    game, world, cmd, &element_id, &pick_type, &tile_x, &tile_z, &tile_level) )
                continue;
            if( GameRunescape_EmitDrawElement(
                    game, element_id, pick_type, tile_x, tile_z, tile_level, command) )
                return RS_PHASE_YIELD;
        }

        game->frame.phase = RS_FRAME_PHASE_END_3D;
        return RS_PHASE_ADVANCE;
    }

    int slot_count = ToriDraw_SceneElementSlotCount(game->scene);
    while( game->frame.element_index < slot_count )
    {
        int element_id = game->frame.element_index++;
        enum WorldPickType pick_type = WORLD_PICK_SCENERY;
        if( ToriDraw_SceneElementIsLive(game->scene, element_id) )
        {
            struct ToriDraw_SceneElement* element =
                ToriDraw_SceneElementGet(game->scene, element_id);
            if( element && element->dynamic )
                pick_type = rs_classify_dynamic_pick_type(game, element_id);
        }
        if( GameRunescape_EmitDrawElement(game, element_id, pick_type, -1, -1, -1, command) )
            return RS_PHASE_YIELD;
    }

    game->frame.phase = RS_FRAME_PHASE_END_3D;
    return RS_PHASE_ADVANCE;
}

static enum RsPhaseResult
rs_phase_end_3d(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    command->kind = TORIRSRC_END_3D;
    game->frame.phase = game->ui_tree_ready ? RS_FRAME_PHASE_UI_2D_BEGIN : RS_FRAME_PHASE_DONE;
    return RS_PHASE_YIELD;
}

static enum RsPhaseResult
rs_phase_ui_begin(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    command->kind = TORIRSRC_BEGIN_2D;
    game->frame.ui_2d_begun = true;
    game->frame.ui_current =
        game->ui_tree && game->ui_tree->root_index >= 0 ? game->ui_tree->root_index : -1;
    game->frame.ui_stack_top = -1;
    game->frame.ui_inv_slot = 0;
    game->frame.ui_minimenu_step = 0;
    game->frame.ui_chat_button_step = 0;
    game->frame.ui_scrollbar_step = 0;
    game->frame.ui_model_step = 0;
    game->frame.phase = RS_FRAME_PHASE_UI_2D;
    return RS_PHASE_YIELD;
}

static enum RsPhaseResult
rs_phase_ui_step(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    if( !game->ui_tree || game->frame.ui_current < 0 )
    {
        game->frame.phase = RS_FRAME_PHASE_UI_2D_END;
        return RS_PHASE_ADVANCE;
    }

    struct StaticUIComponent* component = &game->ui_tree->components[game->frame.ui_current];
    if( !GameRunescape_UINodeVisible(game, component, game->frame.ui_current) )
    {
        GameRunescape_UIAdvance(game, game->frame.ui_current);
        return RS_PHASE_ADVANCE;
    }

    bool const layer_scroll =
        component->type == UIELEM_RS_LAYER &&
        (uitree_scroll_layer_needs_vertical(component) ||
         uitree_scroll_layer_needs_horizontal(component));
    if( (component->is_dirty || layer_scroll) &&
        GameRunescape_EmitUIComponent(game, component, game->frame.ui_current, command) )
    {
        int32_t cur = game->frame.ui_current;
        int const inv_slot_limit = GameRunescape_UIInvGridSlotLimit(component);
        bool const inv_more = inv_slot_limit > 0 && game->frame.ui_inv_slot > 0 &&
                              game->frame.ui_inv_slot < inv_slot_limit;
        bool const minimenu_more = component->type == UIELEM_BUILTIN_MINIMENU &&
                                   game->minimenu.visible && game->frame.ui_minimenu_step > 0;
        bool const chat_button_more = component->type == UIELEM_BUILTIN_CHAT_BUTTON &&
                                      game->frame.ui_chat_button_step > 0;
        bool const ui_model_more =
            component->type == UIELEM_RS_MODEL && game->frame.ui_model_step > 0;
        if( component->type == UIELEM_RS_LAYER && layer_scroll )
        {
            bool vscroll = uitree_scroll_layer_needs_vertical(component);
            bool hscroll = uitree_scroll_layer_needs_horizontal(component);
            int total_steps = (vscroll ? UITREE_SCROLLBAR_V_DRAW_STEPS : 0) +
                              (hscroll ? UITREE_SCROLLBAR_H_DRAW_STEPS : 0);
            if( total_steps > 0 && game->frame.ui_scrollbar_step + 1 < total_steps )
            {
                game->frame.ui_scrollbar_step++;
                return RS_PHASE_YIELD;
            }
            game->frame.ui_scrollbar_step = 0;
        }
        if( !inv_more && !minimenu_more && !chat_button_more && !ui_model_more )
            GameRunescape_UIAdvance(game, cur);
        return RS_PHASE_YIELD;
    }

    if( component->type == UIELEM_BUILTIN_MINIMENU && game->minimenu.visible &&
        game->frame.ui_minimenu_step > 0 )
        return RS_PHASE_ADVANCE;

    if( component->type == UIELEM_BUILTIN_CHAT_BUTTON && game->frame.ui_chat_button_step > 0 )
        return RS_PHASE_ADVANCE;

    if( component->type == UIELEM_RS_MODEL && game->frame.ui_model_step > 0 )
        return RS_PHASE_ADVANCE;

    if( (component->type == UIELEM_INV_GRID || component->type == UIELEM_RS_INV_TEXT) &&
        game->frame.ui_inv_slot > 0 )
    {
        int slot_limit = GameRunescape_UIInvGridSlotLimit(component);
        if( component->type == UIELEM_INV_GRID && slot_limit > UI_INV_SLOT_OFFSET_MAX )
            slot_limit = UI_INV_SLOT_OFFSET_MAX;
        if( game->frame.ui_inv_slot < slot_limit )
            return RS_PHASE_ADVANCE;
        game->frame.ui_inv_slot = 0;
    }

    GameRunescape_UIAdvance(game, game->frame.ui_current);
    return RS_PHASE_ADVANCE;
}

static enum RsPhaseResult
rs_phase_ui_end(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    command->kind = TORIRSRC_END_2D;
    game->frame.phase = RS_FRAME_PHASE_DONE;
    return RS_PHASE_YIELD;
}

void
GameRunescape_ApplyChatFilterSettings(
    struct GameRunescape* game,
    int public_mode,
    int private_mode,
    int trade_mode)
{
    if( !game )
        return;
    game->chat_public_mode = public_mode;
    game->chat_private_mode = private_mode;
    game->chat_trade_mode = trade_mode;
}

void
GameRunescape_SendChatSetMode(struct GameRunescape* game)
{
    if( !game )
        return;
    (void)game;
    /* Outbound PKTOUT_LC245_2_CHAT_SETMODE when live connection is wired. */
}

void
GameRunescape_FrameBegin(
    struct GameRunescape* game,
    int cycles_elapsed)
{
    game->frame.phase = RS_FRAME_PHASE_GC_EVENTS;
    game->frame.event_index = 0;
    game->frame.element_index = 0;
    game->frame.painter_command_index = 0;
    game->frame.world_emitted = false;
    game->frame.painter_paint_done = false;
    game->frame.ui_2d_begun = false;
    game->frame.ui_current = -1;
    game->frame.ui_stack_top = -1;
    game->frame.ui_inv_slot = 0;
    game->frame.ui_minimenu_step = 0;
    game->frame.ui_chat_button_step = 0;
    game->frame.ui_scrollbar_step = 0;
    game->frame.ui_model_step = 0;
    game->frame.ui_scrollbar_layer = -1;
    game->ui_hovered_node = -1;
    game->ui_over_main_com_id = -1;
    game->ui_over_side_com_id = -1;
    game->ui_over_chat_com_id = -1;
    if( game->scene && game->ui_tree_ready && !game->ui_fonts_synced )
    {
        ToriDraw_SceneFontsReemitLoads(game->scene);
        game->ui_fonts_synced = true;
    }
    world_pickset_reset(&game->pickset);
    if( game->scene )
    {
        struct ToriDraw_TextureState* tex_state = ToriDraw_SceneTexState(game->scene);
        if( tex_state )
            ToriDraw_TextureMapAnimate(&tex_state->texture_map, cycles_elapsed);
    }
    if( game->world )
        world_cycle(game->world, cycles_elapsed);
    GameRunescape_DrainWorldEvents(game);
    for( int i = 0; i < cycles_elapsed; i++ )
        GameRunescape_TickAnimations(game);
    GameRunescape_SyncProjectilesToScene(game);
    GameRunescape_UpdateWorldViewport(game);
    if( game->ui_tree && game->ui_tree->component_count > 0 )
    {
        bool hover_dirty = false;
        bool const first_ready = !game->ui_tree_ready;

        uitree_layout_resolve(game->ui_tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        if( !game->ui_tree_ready && !game->ui_sprites_synced )
            GameRunescape_SyncUISpritesFromScene(game);
        game->ui_tree_ready = true;
        GameRunescape_FindSpecialUINodes(game);
        GameRunescape_UpdateUIHover(game);

        if( game->ui_over_main_com_id != game->ui_over_main_com_id_prev ||
            game->ui_over_side_com_id != game->ui_over_side_com_id_prev ||
            game->ui_over_chat_com_id != game->ui_over_chat_com_id_prev )
        {
            hover_dirty = true;
            game->ui_over_main_com_id_prev = game->ui_over_main_com_id;
            game->ui_over_side_com_id_prev = game->ui_over_side_com_id;
            game->ui_over_chat_com_id_prev = game->ui_over_chat_com_id;
        }

        if( hover_dirty || first_ready )
            uitree_mark_all_dirty(game->ui_tree);
    }

    if( game->cross_mode != RUNESCAPE_CROSS_MODE_OFF )
    {
        game->cross_cycle += 20;
        if( game->cross_cycle >= 400 )
            game->cross_mode = RUNESCAPE_CROSS_MODE_OFF;
    }

    ui_minimenu_update_hover(&game->minimenu, game->mouse_x, game->mouse_y);
}

bool
GameRunescape_FrameNextCommand(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command)
{
    memset(command, 0, sizeof(*command));

    for( ;; )
    {
        enum RsPhaseResult r;
        switch( game->frame.phase )
        {
        case RS_FRAME_PHASE_GC_EVENTS:
            r = rs_phase_gc_events(game, command);
            break;
        case RS_FRAME_PHASE_BEGIN_3D:
            r = rs_phase_begin_3d(game, command);
            break;
        case RS_FRAME_PHASE_MODELS:
            r = rs_phase_models(game, command);
            break;
        case RS_FRAME_PHASE_END_3D:
            r = rs_phase_end_3d(game, command);
            break;
        case RS_FRAME_PHASE_UI_2D_BEGIN:
            r = rs_phase_ui_begin(game, command);
            break;
        case RS_FRAME_PHASE_UI_2D:
            r = rs_phase_ui_step(game, command);
            break;
        case RS_FRAME_PHASE_UI_2D_END:
            r = rs_phase_ui_end(game, command);
            break;
        default:
            return false;
        }
        if( r == RS_PHASE_YIELD )
            return true;
    }
}

void
GameRunescape_FrameEnd(struct GameRunescape* game)
{
    ToriDraw_SceneFrameEnd(game->scene);

    game->frame.phase = RS_FRAME_PHASE_DONE;
}

static struct GameRunescape_EntityRecord*
GameRunescape_EntityFind(
    struct GameRunescape* game,
    int entity_id)
{
    if( !game || !game->entity_registry )
        return NULL;

    for( int i = 0; i < game->entity_registry_count; i++ )
    {
        if( game->entity_registry[i].entity_id == entity_id )
            return &game->entity_registry[i];
    }
    return NULL;
}

static bool
GameRunescape_EntityRegister(
    struct GameRunescape* game,
    int entity_id,
    int element_id,
    int world_index)
{
    struct GameRunescape_EntityRecord* existing;

    if( !game )
        return false;

    existing = GameRunescape_EntityFind(game, entity_id);
    if( existing )
    {
        existing->element_id = element_id;
        existing->world_index = world_index;
        return true;
    }

    if( game->entity_registry_count >= game->entity_registry_cap )
    {
        int new_cap = game->entity_registry_cap ? game->entity_registry_cap * 2
                                                : RUNESCAPE_ENTITY_REGISTRY_INITIAL_CAP;
        struct GameRunescape_EntityRecord* grown =
            realloc(game->entity_registry, (size_t)new_cap * sizeof(*grown));
        if( !grown )
            return false;
        game->entity_registry = grown;
        game->entity_registry_cap = new_cap;
    }

    game->entity_registry[game->entity_registry_count++] = (struct GameRunescape_EntityRecord){
        .entity_id = entity_id,
        .element_id = element_id,
        .world_index = world_index,
    };
    return true;
}

static struct ToriDraw_ModelHandle
GameRunescape_BuildSceneModelFromCache(
    struct GameRunescape* game,
    int model_id);

static struct ToriDraw_ModelHandle
GameRunescape_BuildSceneModelFromCache(
    struct GameRunescape* game,
    int model_id)
{
    struct ToriDraw_ModelHandle cached;
    struct ToriDraw_Model* model;
    struct ToriDraw_ModelHandle hnd;

    cached = ToriAuxLibTD_Model(game->td, model_id);
    if( !ToriDraw_ModelGetBoundsCylinder(cached) )
        return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };

    model = ToriDraw_ModelCopy(cached.u.model.model);
    if( !model )
        return (struct ToriDraw_ModelHandle){ .kind = TORIDRAWMK_NONE };

    hnd = (struct ToriDraw_ModelHandle){
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = model,
    };

    if( ToriDraw_ModelIsLightable(model) )
    {
        ToriDraw_LightModelDefault(hnd, 0, 0);
        ToriDraw_ModelFreeNormals(model);
    }

    return hnd;
}

static bool
GameRunescape_ApplyEntityAnimation(
    struct GameRunescape* game,
    int element_id,
    int anim_id,
    int primary_secondary)
{
    struct ToriDraw_Animation* resolved;

    if( !game || !game->td || !game->scene )
        return false;
    if( !ToriDraw_SceneElementIsLive(game->scene, element_id) )
        return false;

    if( primary_secondary == 0 )
        return ToriAuxLibTD_ElementSetSequenceId(game->td, element_id, anim_id);

    resolved = ToriAuxLibTD_SequenceAnimation(game->td, anim_id);
    if( !resolved )
        return false;

    ToriDraw_SceneElementSetAnimation(game->scene, element_id, resolved, false);
    return true;
}

static void
runescape_preload_idle_animations(
    struct GameRunescape* game,
    struct WorldEntityFacet_IdleAnimations const* idle)
{
    int const anims[] = {
        idle->readyanim,  idle->walkanim,   idle->turnanim,   idle->runanim,
        idle->walkanim_b, idle->walkanim_r, idle->walkanim_l,
    };

    for( int i = 0; i < (int)(sizeof(anims) / sizeof(anims[0])); i++ )
    {
        if( anims[i] != -1 )
            (void)ToriAuxLibTD_SequenceAnimation(game->td, anims[i]);
    }
}

int
GameRunescape_WorldEntityAddPlayer(
    struct GameRunescape* game,
    int entity_id,
    const int appearance[12],
    int x,
    int z,
    int level,
    int readyanim,
    int walkanim,
    int turnanim,
    int runanim,
    int walkanim_b,
    int walkanim_r,
    int walkanim_l)
{
    struct ToriDraw_ModelHandle hnd;
    int element_id;
    int player_idx;
    int world_y;

    if( !game || !game->world || !game->scene || !game->td )
        return -1;
    if( RS_ENTITY_KIND_OF(entity_id) != RS_ENTITY_KIND_PLAYER )
        return -1;

    level = clamp_terrain_level(level);

    hnd = runescape_player_body_build(game, appearance);
    if( hnd.kind != TORIDRAWMK_MODEL )
        return -1;

    element_id = ToriDraw_SceneElementAdd(game->scene);
    if( element_id < 0 )
        return -1;

    ToriDraw_SceneElementSetModel(game->scene, element_id, hnd);

    world_y = 0;
    if( game->world->heightmap )
    {
        int const wx = x * 128 + 64;
        int const wz = z * 128 + 64;
        world_y = heightmap_get_interpolated(game->world->heightmap, wx, wz, level);
    }

    ToriDraw_SceneElementSetPosition(
        game->scene, element_id, x * 128 + 64, world_y, z * 128 + 64, 0);

    {
        struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(game->scene, element_id);
        if( element )
            element->dynamic = true;
    }

    if( readyanim != -1 )
        ToriAuxLibTD_ElementSetSequenceId(game->td, element_id, readyanim);

    {
        struct WorldEntityFacet_IdleAnimations const idle_animations = {
            .readyanim = readyanim,
            .walkanim = walkanim,
            .turnanim = turnanim,
            .runanim = runanim,
            .walkanim_b = walkanim_b,
            .walkanim_r = walkanim_r,
            .walkanim_l = walkanim_l,
        };

        player_idx = world_player_spawn(game->world, element_id, level, x, z, idle_animations);
    }
    if( player_idx < 0 )
        return -1;

    if( !GameRunescape_EntityRegister(game, entity_id, element_id, player_idx) )
        return -1;

    return entity_id;
}

bool
GameRunescape_WorldEntityAnimate(
    struct GameRunescape* game,
    int entity_id,
    int anim_id,
    int primary_secondary)
{
    struct GameRunescape_EntityRecord* record;

    if( !game || RS_ENTITY_KIND_OF(entity_id) == RS_ENTITY_KIND_NONE )
        return false;

    record = GameRunescape_EntityFind(game, entity_id);
    if( !record || record->element_id < 0 )
        return false;

    return GameRunescape_ApplyEntityAnimation(game, record->element_id, anim_id, primary_secondary);
}

int
GameRunescape_WorldEntityAddProjectile(
    struct GameRunescape* game,
    int entity_id,
    int projectile_id,
    int anim_id,
    int src_sx,
    int src_sz,
    int dst_sx,
    int dst_sz,
    int level,
    int startheight,
    int endheight,
    int delay,
    int angle,
    int length,
    int offset,
    int step)
{
    struct ToriDraw_ModelHandle hnd;
    int element_id;
    int projectile_idx;

    if( !game || !game->world || !game->scene || !game->td )
        return -1;
    if( RS_ENTITY_KIND_OF(entity_id) != RS_ENTITY_KIND_PROJECTILE )
        return -1;

    level = clamp_terrain_level(level);

    int const range_x = dst_sx - src_sx;
    int const range_z = dst_sz - src_sz;
    int const range = abs(range_x) > abs(range_z) ? abs(range_x) : abs(range_z);
    int const flight = length + range * step;
    int const t1 = delay;
    int const t2 = delay + flight;

    int const src_x = src_sx * 128 + 64;
    int const src_z = src_sz * 128 + 64;
    int const dst_x = dst_sx * 128 + 64;
    int const dst_z = dst_sz * 128 + 64;

    int h1 = 0;
    if( game->world->heightmap )
        h1 = heightmap_get_interpolated(game->world->heightmap, src_x, src_z, level) -
             startheight * 4;

    hnd = GameRunescape_BuildSceneModelFromCache(game, projectile_id);
    if( hnd.kind != TORIDRAWMK_MODEL )
        return -1;

    element_id = ToriDraw_SceneElementAdd(game->scene);
    if( element_id < 0 )
        return -1;

    ToriDraw_SceneElementSetModel(game->scene, element_id, hnd);

    {
        struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(game->scene, element_id);
        if( element )
            element->dynamic = true;
    }

    if( anim_id != -1 )
        ToriAuxLibTD_ElementSetSequenceId(game->td, element_id, anim_id);

    projectile_idx = world_projectile_spawn(
        game->world,
        element_id,
        level,
        src_x,
        src_z,
        dst_x,
        dst_z,
        h1,
        endheight * 4,
        t1,
        t2,
        angle,
        offset);
    if( projectile_idx < 0 )
        return -1;

    if( !GameRunescape_EntityRegister(game, entity_id, element_id, projectile_idx) )
        return -1;

    return entity_id;
}

static void
game_runescape_npc_apply_npctype(
    struct WorldEntity_NPC* npc,
    struct ToriAuxLibCore_Npctype const* npctype)
{
    if( !npc || !npctype )
        return;

    strncpy(npc->name, npctype->name, sizeof(npc->name) - 1);
    npc->name[sizeof(npc->name) - 1] = '\0';
    npc->combat_level = npctype->combat_level;

    for( int i = 0; i < 5; i++ )
    {
        npc->actions[i].code = (uint16_t)i;
        npc->actions[i].name[0] = '\0';
        if( npctype->actions[i][0] != '\0' )
        {
            strncpy(npc->actions[i].name, npctype->actions[i], sizeof(npc->actions[i].name) - 1);
            npc->actions[i].name[sizeof(npc->actions[i].name) - 1] = '\0';
        }
    }
}

int
GameRunescape_WorldEntityAddNPC(
    struct GameRunescape* game,
    int entity_id,
    int npc_id,
    int x,
    int z,
    int level)
{
    struct ToriDraw_ModelHandle hnd;
    struct WorldEntityFacet_IdleAnimations idle_animations;
    int element_id;
    int npc_idx;
    int npc_size;
    int world_y;

    assert(game && game->world && game->scene && game->td);
    assert(RS_ENTITY_KIND_OF(entity_id) == RS_ENTITY_KIND_NPC);

    level = clamp_terrain_level(level);

    hnd = runescape_npc_body_build(game, npc_id);
    if( hnd.kind != TORIDRAWMK_MODEL )
        return -1;

    idle_animations = runescape_npc_animation_from_config(game, npc_id);
    npc_size = runescape_npc_size_from_config(game, npc_id);

    element_id = ToriDraw_SceneElementAdd(game->scene);
    if( element_id < 0 )
        return -1;

    ToriDraw_SceneElementSetModel(game->scene, element_id, hnd);

    world_y = 0;
    if( game->world->heightmap )
    {
        int const wx = x * 128 + (64 * npc_size);
        int const wz = z * 128 + (64 * npc_size);
        world_y = heightmap_get_interpolated(game->world->heightmap, wx, wz, level);
    }

    ToriDraw_SceneElementSetPosition(
        game->scene, element_id, x * 128 + (64 * npc_size), world_y, z * 128 + (64 * npc_size), 0);

    {
        struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(game->scene, element_id);
        if( element )
            element->dynamic = true;
    }

    if( idle_animations.readyanim != -1 )
        ToriAuxLibTD_ElementSetSequenceId(game->td, element_id, idle_animations.readyanim);

    npc_idx =
        world_npc_spawn(game->world, element_id, npc_id, level, x, z, npc_size, idle_animations);
    if( npc_idx < 0 )
        return -1;

    {
        struct ToriAuxLibCache* cache = ToriAuxLibTD_C(game->td);
        if( cache )
            ToriAuxLibCache_EnsureNpctype(cache, npc_id);
        struct ToriAuxLibCore* core = ToriAuxLibCache_Core(cache);
        struct ToriAuxLibCore_Npctype* npctype =
            core ? ToriAuxLibCore_NpctypeGet(core, npc_id) : NULL;
        struct WorldEntity_NPC* npc_entity =
            World_EntityPoolGet(&game->world->entities.npc, npc_idx);
        if( npctype && npc_entity )
            game_runescape_npc_apply_npctype(npc_entity, npctype);
    }

    if( !GameRunescape_EntityRegister(game, entity_id, element_id, npc_idx) )
        return -1;

    return entity_id;
}

struct Task_GameRunescape_WorldEntityAddPlayer
{
    struct pt thread;
    struct GameRunescape* game;
    int entity_id;
    int appearance[12];
    int x;
    int z;
    int level;
    int readyanim;
    int walkanim;
    int turnanim;
    int runanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
    struct Task_ToriAuxLibCache_PlayerAdd* load;
};

struct Task_GameRunescape_WorldEntityAddPlayer*
Task_GameRunescape_WorldEntityAddPlayer_New(
    struct GameRunescape* game,
    int entity_id,
    const int appearance[12],
    int x,
    int z,
    int level,
    int readyanim,
    int walkanim,
    int turnanim,
    int runanim,
    int walkanim_b,
    int walkanim_r,
    int walkanim_l)
{
    struct Task_GameRunescape_WorldEntityAddPlayer* task =
        calloc(1, sizeof(struct Task_GameRunescape_WorldEntityAddPlayer));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->game = game;
    task->entity_id = entity_id;
    if( appearance )
        memcpy(task->appearance, appearance, sizeof(task->appearance));
    task->x = x;
    task->z = z;
    task->level = level;
    task->readyanim = readyanim;
    task->walkanim = walkanim;
    task->turnanim = turnanim;
    task->runanim = runanim;
    task->walkanim_b = walkanim_b;
    task->walkanim_r = walkanim_r;
    task->walkanim_l = walkanim_l;
    task->load = Task_ToriAuxLibCache_PlayerAdd_New(
        ToriAuxLibTD_C(game->td),
        appearance,
        readyanim,
        walkanim,
        turnanim,
        runanim,
        walkanim_b,
        walkanim_r,
        walkanim_l);
    if( !task->load )
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_GameRunescape_WorldEntityAddPlayer_Free(struct Task_GameRunescape_WorldEntityAddPlayer* task)
{
    if( !task )
        return;
    Task_ToriAuxLibCache_PlayerAdd_Free(task->load);
    free(task);
}

int
Task_GameRunescape_WorldEntityAddPlayer_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_GameRunescape_WorldEntityAddPlayer* task =
        (struct Task_GameRunescape_WorldEntityAddPlayer*)task_state;
    int result;
    int load_state;

    PT_BEGIN(&task->thread);

    assert(!(!task->game || !task->game->td || !task->game->world || !task->load));

    for( ;; )
    {
        load_state = Task_ToriAuxLibCache_PlayerAdd_Run(task->load, ctx);
        if( load_state == PT_ENDED || load_state == PT_EXITED )
            break;
        PT_YIELD(&task->thread);
    }

    if( load_state == PT_EXITED )
        PT_EXIT(&task->thread);

    {
        runescape_preload_idle_animations(
            task->game,
            &(struct WorldEntityFacet_IdleAnimations){
                .readyanim = task->readyanim,
                .walkanim = task->walkanim,
                .turnanim = task->turnanim,
                .runanim = task->runanim,
                .walkanim_b = task->walkanim_b,
                .walkanim_r = task->walkanim_r,
                .walkanim_l = task->walkanim_l,
            });
    }

    result = GameRunescape_WorldEntityAddPlayer(
        task->game,
        task->entity_id,
        task->appearance,
        task->x,
        task->z,
        task->level,
        task->readyanim,
        task->walkanim,
        task->turnanim,
        task->runanim,
        task->walkanim_b,
        task->walkanim_r,
        task->walkanim_l);
    (void)result;

    PT_END(&task->thread);
}

struct Task_GameRunescape_WorldEntityAddNPC
{
    struct pt thread;
    struct GameRunescape* game;
    int entity_id;
    int npc_id;
    int x;
    int z;
    int level;
    struct Task_ToriAuxLibCache_NpcAdd* load;
};

struct Task_GameRunescape_WorldEntityAddNPC*
Task_GameRunescape_WorldEntityAddNPC_New(
    struct GameRunescape* game,
    int entity_id,
    int npc_id,
    int x,
    int z,
    int level)
{
    struct Task_GameRunescape_WorldEntityAddNPC* task =
        calloc(1, sizeof(struct Task_GameRunescape_WorldEntityAddNPC));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->game = game;
    task->entity_id = entity_id;
    task->npc_id = npc_id;
    task->x = x;
    task->z = z;
    task->level = level;
    task->load = Task_ToriAuxLibCache_NpcAdd_New(ToriAuxLibTD_C(game->td), npc_id);
    if( !task->load )
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_GameRunescape_WorldEntityAddNPC_Free(struct Task_GameRunescape_WorldEntityAddNPC* task)
{
    if( !task )
        return;
    Task_ToriAuxLibCache_NpcAdd_Free(task->load);
    free(task);
}

int
Task_GameRunescape_WorldEntityAddNPC_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_GameRunescape_WorldEntityAddNPC* task =
        (struct Task_GameRunescape_WorldEntityAddNPC*)task_state;
    struct WorldEntityFacet_IdleAnimations idle_animations;
    int result;
    int load_state;

    PT_BEGIN(&task->thread);

    assert(!(!task->game || !task->game->td || !task->game->world || !task->load));

    for( ;; )
    {
        load_state = Task_ToriAuxLibCache_NpcAdd_Run(task->load, ctx);
        if( load_state == PT_ENDED || load_state == PT_EXITED )
            break;
        PT_YIELD(&task->thread);
    }

    if( load_state == PT_EXITED )
        PT_EXIT(&task->thread);

    assert(load_state == PT_ENDED && "npc cache load task exited without completing");

    idle_animations = runescape_npc_animation_from_config(task->game, task->npc_id);
    runescape_preload_idle_animations(task->game, &idle_animations);

    result = GameRunescape_WorldEntityAddNPC(
        task->game, task->entity_id, task->npc_id, task->x, task->z, task->level);
    (void)result;

    PT_END(&task->thread);
}

struct Task_GameRunescape_WorldEntityAnimate
{
    struct pt thread;
    struct GameRunescape* game;
    int entity_id;
    int anim_id;
    int primary_secondary;
    struct Task_ToriAuxLibCache_Animate* load;
};

struct Task_GameRunescape_WorldEntityAnimate*
Task_GameRunescape_WorldEntityAnimate_New(
    struct GameRunescape* game,
    int entity_id,
    int anim_id,
    int primary_secondary)
{
    struct Task_GameRunescape_WorldEntityAnimate* task =
        calloc(1, sizeof(struct Task_GameRunescape_WorldEntityAnimate));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->game = game;
    task->entity_id = entity_id;
    task->anim_id = anim_id;
    task->primary_secondary = primary_secondary;
    task->load = Task_ToriAuxLibCache_Animate_New(ToriAuxLibTD_C(game->td), anim_id);
    if( !task->load )
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_GameRunescape_WorldEntityAnimate_Free(struct Task_GameRunescape_WorldEntityAnimate* task)
{
    if( !task )
        return;
    Task_ToriAuxLibCache_Animate_Free(task->load);
    free(task);
}

int
Task_GameRunescape_WorldEntityAnimate_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_GameRunescape_WorldEntityAnimate* task =
        (struct Task_GameRunescape_WorldEntityAnimate*)task_state;
    bool ok;
    int load_state;

    PT_BEGIN(&task->thread);

    assert(!(!task->game || !task->game->td || !task->load));

    for( ;; )
    {
        load_state = Task_ToriAuxLibCache_Animate_Run(task->load, ctx);
        if( load_state == PT_ENDED || load_state == PT_EXITED )
            break;
        PT_YIELD(&task->thread);
    }

    if( load_state == PT_EXITED )
        PT_EXIT(&task->thread);

    if( task->anim_id != -1 )
        (void)ToriAuxLibTD_SequenceAnimation(task->game->td, task->anim_id);

    ok = GameRunescape_WorldEntityAnimate(
        task->game, task->entity_id, task->anim_id, task->primary_secondary);
    (void)ok;

    PT_END(&task->thread);
}

struct Task_GameRunescape_WorldEntityAddProjectile
{
    struct pt thread;
    struct GameRunescape* game;
    int entity_id;
    int projectile_id;
    int anim_id;
    int src_sx;
    int src_sz;
    int dst_sx;
    int dst_sz;
    int level;
    int startheight;
    int endheight;
    int delay;
    int angle;
    int length;
    int offset;
    int step;
    struct Task_ToriAuxLibCache_ProjectileAdd* load;
};

struct Task_GameRunescape_WorldEntityAddProjectile*
Task_GameRunescape_WorldEntityAddProjectile_New(
    struct GameRunescape* game,
    int entity_id,
    int projectile_id,
    int anim_id,
    int src_sx,
    int src_sz,
    int dst_sx,
    int dst_sz,
    int level,
    int startheight,
    int endheight,
    int delay,
    int angle,
    int length,
    int offset,
    int step)
{
    struct Task_GameRunescape_WorldEntityAddProjectile* task =
        calloc(1, sizeof(struct Task_GameRunescape_WorldEntityAddProjectile));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->game = game;
    task->entity_id = entity_id;
    task->projectile_id = projectile_id;
    task->anim_id = anim_id;
    task->src_sx = src_sx;
    task->src_sz = src_sz;
    task->dst_sx = dst_sx;
    task->dst_sz = dst_sz;
    task->level = level;
    task->startheight = startheight;
    task->endheight = endheight;
    task->delay = delay;
    task->angle = angle;
    task->length = length;
    task->offset = offset;
    task->step = step;
    task->load =
        Task_ToriAuxLibCache_ProjectileAdd_New(ToriAuxLibTD_C(game->td), projectile_id, anim_id);
    if( !task->load )
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_GameRunescape_WorldEntityAddProjectile_Free(
    struct Task_GameRunescape_WorldEntityAddProjectile* task)
{
    if( !task )
        return;
    Task_ToriAuxLibCache_ProjectileAdd_Free(task->load);
    free(task);
}

int
Task_GameRunescape_WorldEntityAddProjectile_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_GameRunescape_WorldEntityAddProjectile* task =
        (struct Task_GameRunescape_WorldEntityAddProjectile*)task_state;
    int result;
    int load_state;

    PT_BEGIN(&task->thread);

    assert(!(!task->game || !task->game->td || !task->game->world || !task->load));

    for( ;; )
    {
        load_state = Task_ToriAuxLibCache_ProjectileAdd_Run(task->load, ctx);
        if( load_state == PT_ENDED || load_state == PT_EXITED )
            break;
        PT_YIELD(&task->thread);
    }

    if( load_state == PT_EXITED )
        PT_EXIT(&task->thread);

    if( task->anim_id != -1 )
        (void)ToriAuxLibTD_SequenceAnimation(task->game->td, task->anim_id);

    result = GameRunescape_WorldEntityAddProjectile(
        task->game,
        task->entity_id,
        task->projectile_id,
        task->anim_id,
        task->src_sx,
        task->src_sz,
        task->dst_sx,
        task->dst_sz,
        task->level,
        task->startheight,
        task->endheight,
        task->delay,
        task->angle,
        task->length,
        task->offset,
        task->step);
    (void)result;

    PT_END(&task->thread);
}
