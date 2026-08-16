#include "runescape.h"

#include "../ioqueue/libtorirs_io.h"
#include "../runescape/appearance.h"
#include "../runescape/player_body.h"
#include "../toriauxlib/cache/toriauxlibcache.h"
#include "../toriauxlib/cache/toriauxlibcache_submit.h"
#include "../toriauxlib/core/toriauxlibcore.h"
#include "../toriauxlib/td/toriauxlibtd.h"
#include "../toriauxlib/vm/toriauxlibvm.h"
#include "../ui/ui_chat_minimenu.h"
#include "../ui/ui_click.h"
#include "../vm/cs1vm_host.h"
#include "../vm/cs1vm_level.h"
#include "../vm/cs1vm_opcode.h"
#include "runescape_cs2_host.h"
#include "runescape_cs2_queue.h"
#include "../toriauxlib/core/tasks/dat2/task_dat2_cs2_run.h"
#include "../libtorirs.h"
#include "../world/heightmap.h"
#include "../world/minimap.h"
#include "../world/world_builder.h"
#include "3rd/minipt.h"
#include "osrs/colors.h"
#include "osrs/painters.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/varp_varbit_manager.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/core/tasks/toriauxlib_tasks.h"
#include "toriauxlib/core/tasks/instance_revconfig_inv_bind.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_animation.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_math.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"
#include "toridraw/toridraw_sprite.h"
#include "ui/rs_inv_container.h"
#include "ui/ui_behavior.h"
#include "ui/ui_debug.h"
#include "ui/ui_input_adapter.h"
#include "ui/ui_inv_data_service.h"
#include "ui/ui_inv_slot_view.h"
#include "ui/ui_scroll.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNESCAPE_CAMERA_MOVEMENT_SPEED 70
#define RUNESCAPE_INV_TEXT_CELL_W 115
#define RUNESCAPE_INV_TEXT_CELL_H 12

static bool s_cs1vm_level_ready;

static bool
rs_cs2_host_resolve_obj_icon(
    void* ud,
    int obj_id,
    int* out_scene_id,
    int* out_atlas_index);

static int
rs_cs2_host_inv_get_obj(
    void* ud,
    int inv_id,
    int slot);

static int
rs_cs2_host_inv_get_num(
    void* ud,
    int inv_id,
    int slot);

static int
rs_cs2_host_inv_size(
    void* ud,
    int inv_id);

static void
rs_ui_host_run_hooks(
    struct GameRunescape* game,
    enum UITreeBehaviorHookKind hook_kind);

static void
rs_cs1vm_ensure_level_table(void)
{
    if( !s_cs1vm_level_ready )
    {
        cs1vm_level_experience_init();
        s_cs1vm_level_ready = true;
    }
}

static void
rs_ui_note_varp_change(
    struct GameRunescape* game,
    int varp_id)
{
    assert(game);
    if( varp_id < 0 )
        return;
    for( int i = 0; i < game->varp_change_count; i++ )
    {
        if( game->varp_change_ids[i] == varp_id )
            return;
    }
    if( game->varp_change_count >= RS_UI_VARP_CHANGE_MAX )
        return;
    game->varp_change_ids[game->varp_change_count++] = varp_id;
}

static void
rs_ui_on_cs2_varp_change(
    void* ud,
    int varp_id)
{
    rs_ui_note_varp_change((struct GameRunescape*)ud, varp_id);
}

static void
rs_ui_cs2_enqueue(
    void* ud,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count)
{
    GameRunescape_CS2Enqueue(
        (struct GameRunescape*)ud, script_id, component_id, int_args, int_arg_count, NULL, 0);
}

static void
rs_ui_init_cs2_host(struct GameRunescape* game)
{
    assert(game);

    if( !game->cs2_vm_bound )
    {
        GameRunescape_CS2HostInit(&game->cs2_host, game);
        GameRunescape_CS2QueueInit(&game->cs2_queue);
        memset(&game->cs2vm, 0, sizeof(game->cs2vm));
        CS2VMX_BindHost(&game->cs2vm, &game->cs2_host, GameRunescape_CS2HostExec);
        game->cs2vm.canvas_w = UITREE_LAYOUT_ROOT_W;
        game->cs2vm.canvas_h = UITREE_LAYOUT_ROOT_H;
        game->cs2_vm_bound = true;
    }
}

static void
rs_ui_build_behavior_host(
    struct GameRunescape* game,
    struct UITreeBehaviorHost* out)
{
    assert(game);
    assert(out);
    rs_ui_init_cs2_host(game);
    ui_input_adapter_init_behavior_host_ex(out, game->vm, rs_ui_cs2_enqueue, game);
}

static void
rs_ui_flush_varp_transmits(struct GameRunescape* game)
{
    assert(game);
    assert(game->core);
    assert(game->cs2_vm_bound);
    assert(game->ui_tree);

    struct ToriAuxLibCache* cache = game->td ? ToriAuxLibTD_C(game->td) : NULL;
    struct UITreeBehaviorHost host;
    rs_ui_build_behavior_host(game, &host);

    for( int i = 0; i < game->varp_change_count; i++ )
    {
        uitree_behavior_dispatch_varp_transmit(
            &host, game->core, cache, game->ui_tree, game->varp_change_ids[i]);
    }
    game->varp_change_count = 0;

    if( game->ui_tree )
        uitree_mark_all_dirty(game->ui_tree);
}

void
GameRunescape_DispatchInvTransmit(
    struct GameRunescape* game,
    int container_id)
{
    if( !game || !game->core || !game->cs2_vm_bound || !game->ui_tree ||
        game->ui_tree->component_count == 0 || container_id < 0 )
        return;

    struct ToriAuxLibCache* cache = game->td ? ToriAuxLibTD_C(game->td) : NULL;
    struct UITreeBehaviorHost host;
    rs_ui_build_behavior_host(game, &host);
    uitree_behavior_dispatch_inv_transmit(&host, game->core, cache, game->ui_tree, container_id);

    if( game->ui_tree )
        uitree_mark_all_dirty(game->ui_tree);
}

void
GameRunescape_RunOnLoadHooks(struct GameRunescape* game)
{
    if( !game || game->ui_on_load_hooks_ran || !game->core || !game->cs2_vm_bound || !game->ui_tree ||
        game->ui_tree->component_count == 0 )
        return;

    rs_ui_host_run_hooks(game, UITREE_BEHAVIOR_HOOK_ON_LOAD);
    game->ui_on_load_hooks_ran = true;
}

static char const*
GameRunescape_ObjDisplayName(
    struct GameRunescape* game,
    int obj_id)
{
    assert(game);
    if( obj_id <= 0 )
        return NULL;

    struct ToriAuxLibCore_Objtype* obj =
        game->core ? ToriAuxLibCore_ObjtypeGet(game->core, obj_id) : NULL;
    assert(obj);
    if( obj->name[0] == '\0' )
        return "item";
    return obj->name;
}

static int
GameRunescape_UIInvGridSlotLimit(struct StaticUIComponent const* component)
{
    assert(component);

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
rs_cs1vm_inv_count_in_service(
    struct UIInvDataService const* svc,
    int obj_id)
{
    if( !svc || obj_id <= 0 )
        return 0;

    int total = 0;
    for( int src = 0; src < svc->source_count; src++ )
    {
        if( !svc->sources[src].used )
            continue;
        int const slot_limit = svc->sources[src].slot_count;
        for( int slot = 0; slot < slot_limit; slot++ )
        {
            struct UIInvSlotData data;
            if( !ui_inv_data_service_get_slot(svc, src, slot, &data) )
                continue;
            if( data.obj_id == obj_id )
                total += data.obj_count > 0 ? data.obj_count : 1;
        }
    }
    return total;
}

static bool
rs_cs1vm_inv_contains_in_service(
    struct UIInvDataService const* svc,
    int obj_id)
{
    return rs_cs1vm_inv_count_in_service(svc, obj_id) > 0;
}

static int
rs_cs1vm_get_inv_count(
    void* ud,
    int iface_id,
    int obj_id)
{
    (void)iface_id;
    struct GameRunescape* game = ud;
    assert(game);

    return rs_cs1vm_inv_count_in_service(&game->inv_data, obj_id);
}

static int
rs_cs1vm_inv_contains(
    void* ud,
    int iface_id,
    int obj_id)
{
    (void)iface_id;
    struct GameRunescape* game = ud;
    assert(game);

    return rs_cs1vm_inv_contains_in_service(&game->inv_data, obj_id) ? CS1VM_INV_CONTAINS_PRESENT
                                                                     : 0;
}

static int
rs_cs1vm_get_stat_level(
    void* ud,
    int skill)
{
    (void)ud;
    (void)skill;
    return 0;
}

static int
rs_cs1vm_get_stat_base_level(
    void* ud,
    int skill)
{
    (void)ud;
    (void)skill;
    return 0;
}

static int
rs_cs1vm_get_stat_xp(
    void* ud,
    int skill)
{
    (void)ud;
    (void)skill;
    return 0;
}

static int
rs_cs1vm_get_stat_xp_remaining(
    void* ud,
    int skill)
{
    (void)ud;
    rs_cs1vm_ensure_level_table();
    int const base = rs_cs1vm_get_stat_base_level(ud, skill);
    int const xp = rs_cs1vm_get_stat_xp(ud, skill);
    int const target = cs1vm_level_experience_for_base_level(base + 1);
    if( target <= xp )
        return 0;
    return target - xp;
}

static int
rs_cs1vm_get_combat_level(void* ud)
{
    (void)ud;
    return 0;
}

static int
rs_cs1vm_get_total_level(void* ud)
{
    (void)ud;
    return 0;
}

static int
rs_cs1vm_get_runenergy(void* ud)
{
    (void)ud;
    return 10000;
}

static int
rs_cs1vm_get_runweight(void* ud)
{
    (void)ud;
    return 0;
}

static int
rs_cs1vm_get_coord_x(void* ud)
{
    struct GameRunescape* game = ud;
    assert(game);
    assert(game);
    return game->camera_position->x / 128;
}

static int
rs_cs1vm_get_coord_z(void* ud)
{
    struct GameRunescape* game = ud;
    assert(game);
    assert(game);
    return game->camera_position->z / 128;
}

static void
rs_ui_host_fill_cs1host(
    struct GameRunescape* game,
    struct CS1Host* out)
{
    assert(out);

    memset(out, 0, sizeof(*out));
    assert(game);
    assert(game);

    cs1vm_host_fill_varp_varbit(out, game->vm);
    out->ud = game;
    out->get_stat_level = rs_cs1vm_get_stat_level;
    out->get_stat_base_level = rs_cs1vm_get_stat_base_level;
    out->get_stat_xp = rs_cs1vm_get_stat_xp;
    out->get_inv_count = rs_cs1vm_get_inv_count;
    out->inv_contains = rs_cs1vm_inv_contains;
    out->get_stat_xp_remaining = rs_cs1vm_get_stat_xp_remaining;
    out->get_combat_level = rs_cs1vm_get_combat_level;
    out->get_total_level = rs_cs1vm_get_total_level;
    out->get_runenergy = rs_cs1vm_get_runenergy;
    out->get_runweight = rs_cs1vm_get_runweight;
    out->get_coord_x = rs_cs1vm_get_coord_x;
    out->get_coord_z = rs_cs1vm_get_coord_z;
}

static int
rs_ui_host_request(
    void* user,
    struct UITreeHostRequest* req)
{
    struct GameRunescape* game = user;
    assert(req);

    switch( req->kind )
    {
    case UITREE_HOST_IS_ACTIVE:
    {
        struct StaticUIComponent const* component = req->u.is_active.component;
        assert(game);
        assert(game->vm);
        assert(component);

        struct CS1Host cs1host;
        rs_ui_host_fill_cs1host(game, &cs1host);
        return ToriAuxLibVM_IsActive(game->vm, &cs1host, (struct StaticUIComponent*)component) ? 1
                                                                                               : 0;
    }
    case UITREE_HOST_APPLY_BUTTON_CLICK:
    {
        struct StaticUIComponent const* component = req->u.apply_button_click.component;
        assert(game);
        assert(game->vm);
        assert(component);

        ToriAuxLibVM_ApplyButtonClickOptimistic(game->vm, (struct StaticUIComponent*)component);

        if( component->behavior.scripts && component->behavior.scripts_count > 0 &&
            component->behavior.scripts[0] )
        {
            int opcode = component->behavior.scripts[0][0];
            if( opcode == 5 && component->behavior.scripts[0][1] >= 0 )
                rs_ui_note_varp_change(game, component->behavior.scripts[0][1]);
            else if( opcode == 14 && component->behavior.scripts[0][1] >= 0 && game->vm )
            {
                struct VarPVarBitManager* mgr = ToriAuxLibVM_VarPVarBit(game->vm);
                if( mgr )
                {
                    int varbit_id = component->behavior.scripts[0][1];
                    if( varbit_id >= 0 && varbit_id < mgr->varbit_count )
                    {
                        int basevar = mgr->varbit_types[varbit_id].basevar;
                        if( basevar >= 0 )
                            rs_ui_note_varp_change(game, basevar);
                    }
                }
            }
        }

        if( game->ui_tree )
            uitree_mark_all_dirty(game->ui_tree);

        if( !game->core || !game->cs2_vm_bound || component->component_id < 0 )
            return 0;

        struct ToriAuxLibCache* cache = game->td ? ToriAuxLibTD_C(game->td) : NULL;
        struct ToriAuxLibCore_Component* core_comp =
            ToriAuxLibCore_ComponentGet(game->core, component->component_id);
        if( !core_comp )
            return 0;

        struct UITreeBehaviorHost host;
        rs_ui_build_behavior_host(game, &host);
        uitree_behavior_run_hook(
            &host, game->core, cache, core_comp, UITREE_BEHAVIOR_HOOK_ON_CLICK);

        rs_ui_flush_varp_transmits(game);
        return 0;
    }
    case UITREE_HOST_EVAL_TEXT_PLACEHOLDER:
    {
        struct StaticUIComponent const* component = req->u.eval_text_placeholder.component;
        int script_idx = req->u.eval_text_placeholder.script_idx;
        assert(game);
        assert(game->vm);
        assert(component);

        if( !component->behavior.scripts || script_idx < 0 ||
            script_idx >= component->behavior.scripts_count ||
            !component->behavior.scripts[script_idx] )
            return 0;

        return ToriAuxLibVM_EvalScript(game->vm, (struct StaticUIComponent*)component, script_idx);
    }
    case UITREE_HOST_GET_SELECTED_TAB:
        assert(game);
        return game->selected_tab;
    case UITREE_HOST_SET_SELECTED_TAB:
        assert(game);
        game->selected_tab = req->u.set_selected_tab.tabno;
        return 0;
    case UITREE_HOST_GET_CAMERA_YAW:
        assert(game);
        assert(game->camera);
        return ToriDraw_NormalizeAngle(game->camera->yaw);
    case UITREE_HOST_GET_MINIMAP_ANCHOR:
    {
        int* out_src_anchor_x = req->u.get_minimap_anchor.out_x;
        int* out_src_anchor_y = req->u.get_minimap_anchor.out_y;
        assert(game);
        assert(out_src_anchor_x);
        assert(out_src_anchor_y);
        assert(game->camera_position);
        assert(game->world);
        assert(game->world->minimap);
        assert(game->world_map.world_map_w > 0);
        assert(game->world_map.world_map_h > 0);

        *out_src_anchor_x = 0;
        *out_src_anchor_y = 0;
        struct Minimap* mm = game->world->minimap;
        if( mm->width <= 0 || mm->height <= 0 )
            return 0;

        minimap_compute_camera_src_anchor(
            game->camera_position->x,
            game->camera_position->z,
            game->world_map.world_map_w,
            game->world_map.world_map_h,
            mm->width,
            mm->height,
            out_src_anchor_x,
            out_src_anchor_y);
        return 0;
    }
    case UITREE_HOST_GET_WORLD_MAP_SIZE:
    {
        int* out_w = req->u.get_world_map_size.out_w;
        int* out_h = req->u.get_world_map_size.out_h;
        assert(game);
        assert(out_w);
        assert(out_h);
        *out_w = game->world_map.world_map_w;
        *out_h = game->world_map.world_map_h;
        return 0;
    }
    case UITREE_HOST_GET_CROSS_ACTIVE:
        return game && ui_cross_cursor_is_active(&game->cross) ? 1 : 0;
    case UITREE_HOST_GET_CROSS_POSITION:
        ui_cross_cursor_get_position(
            game ? &game->cross : NULL,
            req->u.get_cross_position.out_x,
            req->u.get_cross_position.out_y);
        return 0;
    case UITREE_HOST_GET_CROSS_ATLAS_FRAME:
        return ui_cross_cursor_atlas_frame(game ? &game->cross : NULL);
    case UITREE_HOST_GET_MINIMENU_VISIBLE:
        return game && game->minimenu.visible ? 1 : 0;
    case UITREE_HOST_GET_MINIMENU_LAYOUT:
    {
        int* out_x = req->u.get_minimenu_layout.out_x;
        int* out_y = req->u.get_minimenu_layout.out_y;
        int* out_w = req->u.get_minimenu_layout.out_w;
        int* out_h = req->u.get_minimenu_layout.out_h;
        if( out_x )
            *out_x = game ? game->minimenu.x : 0;
        if( out_y )
            *out_y = game ? game->minimenu.y : 0;
        if( out_w )
            *out_w = game ? game->minimenu.width : 0;
        if( out_h )
            *out_h = game ? game->minimenu.height : 0;
        return 0;
    }
    case UITREE_HOST_GET_MINIMENU_HOVERED_OPTION:
        return game ? game->minimenu.hovered_option : -1;
    case UITREE_HOST_SCENE_SPRITE_HAS:
        assert(game);
        assert(game->scene);
        return ToriDraw_SceneSpriteHas(game->scene, req->u.scene_sprite_has.scene_id) ? 1 : 0;
    case UITREE_HOST_SCENE_FONT_HAS:
        assert(game);
        assert(game->scene);
        return ToriDraw_SceneFontHas(game->scene, req->u.scene_font_has.font_id) ? 1 : 0;
    case UITREE_HOST_SCENE_MODEL_HAS:
        return game && game->scene &&
                       ToriDraw_SceneModelHas(game->scene, req->u.scene_model_has.model_id)
                   ? 1
                   : 0;
    case UITREE_HOST_GET_INV_SOURCE_SLOT:
        assert(game);

        return ui_inv_data_service_get_slot(
                   &game->inv_data,
                   req->u.get_inv_source_slot.source_id,
                   req->u.get_inv_source_slot.slot,
                   req->u.get_inv_source_slot.out)
                   ? 1
                   : 0;
    case UITREE_HOST_SET_INV_SOURCE_SLOT:
        assert(game);

        if( !ui_inv_data_service_set_slot(
                &game->inv_data,
                req->u.set_inv_source_slot.source_id,
                req->u.set_inv_source_slot.slot,
                req->u.set_inv_source_slot.data) )
            return 0;
        instance_revconfig_inv_mark_dirty(game, req->u.set_inv_source_slot.source_id);
        GameRunescape_DispatchInvTransmit(
            game,
            ui_inv_data_service_container_for_source(
                &game->inv_data, req->u.set_inv_source_slot.source_id));
        return 0;
    }
    return 0;
}

static void
rs_ui_host_run_hooks(
    struct GameRunescape* game,
    enum UITreeBehaviorHookKind hook_kind)
{
    assert(game);
    assert(game->core);
    assert(game->cs2_vm_bound);
    assert(game->ui_tree);

    struct ToriAuxLibCache* cache = game->td ? ToriAuxLibTD_C(game->td) : NULL;
    struct UITreeBehaviorHost host;
    rs_ui_build_behavior_host(game, &host);

    for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
    {
        struct StaticUIComponent const* node = &game->ui_tree->components[i];
        if( node->component_id < 0 )
            continue;
        struct ToriAuxLibCore_Component* core_comp =
            ToriAuxLibCore_ComponentGet(game->core, node->component_id);
        if( !core_comp )
            continue;
        uitree_behavior_run_hook(&host, game->core, cache, core_comp, hook_kind);
    }

    uitree_mark_all_dirty(game->ui_tree);
}

static void
GameRunescape_AssertSceneFontReady(
    struct GameRunescape* game,
    int font_id,
    char const* context)
{
    if( font_id < 0 )
    {
        fprintf(
            stderr, "GameRunescape: font_id unset (context=%s)\n", context ? context : "(null)");
        assert(font_id >= 0);
    }
    if( !game || !game->scene )
    {
        fprintf(
            stderr,
            "GameRunescape: missing scene for font_id=%d (context=%s, game=%p)\n",
            font_id,
            context ? context : "(null)",
            (void*)game);
        assert(game && game->scene);
    }
    if( !ToriDraw_SceneFontHas(game->scene, font_id) )
    {
        fprintf(
            stderr,
            "GameRunescape: scene font not loaded font_id=%d (context=%s)\n",
            font_id,
            context ? context : "(null)");
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
        fprintf(
            stderr, "GameRunescape: scene_id unset (context=%s)\n", context ? context : "(null)");
        assert(scene_id >= 0);
    }
    if( !game || !game->scene )
    {
        fprintf(
            stderr,
            "GameRunescape: missing scene for scene_id=%d (context=%s, game=%p)\n",
            scene_id,
            context ? context : "(null)",
            (void*)game);
        assert(game && game->scene);
    }
    if( !ToriDraw_SceneSpriteHas(game->scene, scene_id) )
    {
        fprintf(
            stderr,
            "GameRunescape: scene sprite not loaded scene_id=%d (context=%s)\n",
            scene_id,
            context ? context : "(null)");
        assert(ToriDraw_SceneSpriteHas(game->scene, scene_id));
    }
    int sprite_count = 0;
    struct ToriDraw_Sprite** sprites =
        ToriDraw_SceneSpriteGet(game->scene, scene_id, &sprite_count);
    if( atlas_index < 0 || atlas_index >= sprite_count || !sprites || !sprites[atlas_index] ||
        !sprites[atlas_index]->pixels_argb )
    {
        fprintf(
            stderr,
            "GameRunescape: sprite atlas invalid scene_id=%d atlas_index=%d count=%d "
            "(context=%s)\n",
            scene_id,
            atlas_index,
            sprite_count,
            context ? context : "(null)");
        assert(
            sprites && atlas_index >= 0 && atlas_index < sprite_count && sprites[atlas_index] &&
            sprites[atlas_index]->pixels_argb);
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
    assert(game);

    int font_id = -1;
    if( game->ui_tree && game->ui_hover.minimenu_node >= 0 )
    {
        struct StaticUIComponent* component =
            &game->ui_tree->components[game->ui_hover.minimenu_node];
        font_id = component->u.minimenu.font_id;
    }

    struct ToriDraw_Font* font =
        game->scene && font_id >= 0 ? ToriDraw_SceneFontGet(game->scene, font_id) : NULL;
    return ui_minimenu_prepare_show(&game->minimenu, font, out_layout, out_content_width);
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

static int
rs_cs2_host_inv_get_obj(
    void* ud,
    int inv_id,
    int slot)
{
    struct GameRunescape* game = ud;
    assert(game);
    if( inv_id < 0 || slot < 0 )
        return 0;
    struct RSInvContainer const* container = rs_inv_container_find(&game->inv_data.store, inv_id);
    assert(container);
    if( slot >= container->slot_count )
        return 0;
    return container->obj_id[slot];
}

static int
rs_cs2_host_inv_get_num(
    void* ud,
    int inv_id,
    int slot)
{
    struct GameRunescape* game = ud;
    assert(game);
    if( inv_id < 0 || slot < 0 )
        return 0;
    struct RSInvContainer const* container = rs_inv_container_find(&game->inv_data.store, inv_id);
    assert(container);
    if( slot >= container->slot_count )
        return 0;
    return container->obj_count[slot];
}

static int
rs_cs2_host_inv_size(
    void* ud,
    int inv_id)
{
    struct GameRunescape* game = ud;
    assert(game);
    if( inv_id < 0 )
        return 0;
    struct RSInvContainer const* container = rs_inv_container_find(&game->inv_data.store, inv_id);
    return container ? container->slot_count : 0;
}

static bool
rs_cs2_host_resolve_obj_icon(
    void* ud,
    int obj_id,
    int* out_scene_id,
    int* out_atlas_index)
{
    struct GameRunescape* game = ud;
    if( out_scene_id )
        *out_scene_id = -1;
    if( out_atlas_index )
        *out_atlas_index = 0;
    assert(game);
    if( obj_id <= 0 )
        return false;

    for( int src = 0; src < game->inv_data.source_count; src++ )
    {
        if( !game->inv_data.sources[src].used )
            continue;
        int const container_id = game->inv_data.sources[src].container_id;
        struct RSInvContainer const* container =
            rs_inv_container_find(&game->inv_data.store, container_id);
        assert(container);

        for( int slot = 0; slot < container->slot_count; slot++ )
        {
            if( container->obj_id[slot] != obj_id )
                continue;
            if( container->scene_id[slot] < 0 )
                continue;
            if( out_scene_id )
                *out_scene_id = container->scene_id[slot];
            if( out_atlas_index )
                *out_atlas_index = container->atlas_index[slot];
            return true;
        }
    }

    if( game->ui_inv_pool )
    {
        for( int pi = 0; pi < game->ui_inv_pool->count; pi++ )
        {
            struct UIInventory const* inv = &game->ui_inv_pool->inventories[pi];
            for( int ii = 0; ii < inv->item_count; ii++ )
            {
                if( inv->items[ii].obj_id != obj_id )
                    continue;
                if( inv->items[ii].scene_id < 0 )
                    continue;
                if( out_scene_id )
                    *out_scene_id = inv->items[ii].scene_id;
                if( out_atlas_index )
                    *out_atlas_index = inv->items[ii].atlas_index;
                return true;
            }
        }
    }
    return false;
}

static void
GameRunescape_FindSpecialUINodes(struct GameRunescape* game)
{
    assert(game);

    game->ui_hover.minimenu_node = -1;
    game->ui_hover.chat_node = -1;
    game->ui_hover.chat_node = -1;
    assert(game);

    for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
    {
        if( game->ui_tree->components[i].type == UIELEM_BUILTIN_MINIMENU )
            game->ui_hover.minimenu_node = (int32_t)i;
        if( game->ui_tree->components[i].type == UIELEM_BUILTIN_CHAT )
            game->ui_hover.chat_node = (int32_t)i;
    }
}

static void
GameRunescape_InitUIHost(struct GameRunescape* game)
{
    assert(game);

    uitree_host_init(&game->ui_host);
    game->ui_host.user = game;
    game->ui_host.request = rs_ui_host_request;
    game->ui_input.hovered = -1;
    game->ui_input.pressed = -1;
    game->selected_tab = 3;
    game->inv_selection.source_id = -1;
    game->inv_selection.slot = -1;
    game->ui_hover.minimenu_node = -1;
    game->ui_hover.chat_node = -1;
    interaction_state_reset(&game->interaction);
    interaction_state_reset(&game->click_target);
    ui_minimenu_reset(&game->minimenu);
    rs_ui_init_cs2_host(game);
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
    assert(game);
    assert(game);
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
    assert(game);
    assert(game);

    for( int i = 0; i < game->entities.count; i++ )
    {
        if( game->entities.records[i].element_id != element_id )
            continue;
        if( RS_ENTITY_KIND_OF(game->entities.records[i].entity_id) == RS_ENTITY_KIND_NPC )
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
           mouse_y >= game->world_view_port.clip_top && mouse_y < game->world_view_port.clip_bottom;
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

    assert(game);

    const int cull = ToriDraw_RenderModel1Project(
        element->model, game->scene, &rel_pos, &game->world_view_port, game->camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return false;

    if( allow_pick && game->world_pick.mouse_in_viewport &&
        ToriDraw_ProjectedModelContainsPoint(
            game->scene,
            element->model,
            &game->world_view_port,
            game->world_pick.mouse_x,
            game->world_pick.mouse_y) )
    {
        world_pickset_add(
            &game->world_pick.pickset, element_id, pick_type, tile_x, tile_z, tile_level);
        if( pick_type == WORLD_PICK_TERRAIN )
        {
            game->world_pick.last_tile_sx = tile_x;
            game->world_pick.last_tile_sz = tile_z;
            game->world_pick.last_tile_level = tile_level;
            game->world_pick.last_tile_valid = true;
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
    game->world_map.zone_center_x = RUNESCAPE_ZONE_CENTER_X;
    game->world_map.zone_center_z = RUNESCAPE_ZONE_CENTER_Z;

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
    ui_hover_routing_reset(&game->ui_hover);
    ui_scroll_runtime_reset(&game->ui_scroll);
    ui_cross_cursor_reset(&game->cross);
    chat_state_reset(&game->chat);
    ui_inv_selection_reset(&game->inv_selection);
    entity_registry_init(&game->entities, RUNESCAPE_ENTITY_REGISTRY_INITIAL_CAP);

    return game;
}

void
GameRunescape_Free(struct GameRunescape* game)
{
    assert(game);

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
    GameRunescape_CS2QueueFree(&game->cs2_queue);
    entity_registry_free(&game->entities);
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
    assert(game);

    game->core = gamecache;
}

void
GameRunescape_SetTD(
    struct GameRunescape* game,
    struct ToriAuxLibTD* td)
{
    assert(game);

    game->td = td;
}

void
GameRunescape_SetVM(
    struct GameRunescape* game,
    struct ToriAuxLibVM* vm)
{
    assert(game);

    game->vm = vm;
}

static void
GameRunescape_AttachWorldMapToUITree(struct GameRunescape* game)
{
    assert(game);
    if( game->world_map.world_map_scene_id < 0 || !game->ui_tree )
        return;

    for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
    {
        if( game->ui_tree->components[i].type == UIELEM_BUILTIN_MINIMAP )
        {
            game->ui_tree->components[i].u.minimap.scene_id = game->world_map.world_map_scene_id;
            break;
        }
    }
}

void
GameRunescape_SetUITree(
    struct GameRunescape* game,
    struct UITree* ui_tree)
{
    assert(game);

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
    assert(game);
    if( container_id < 0 )
        return;
    struct RSInvContainer* container =
        rs_inv_container_get_or_create(&game->inv_data.store, container_id, 0);
    assert(container);

    rs_inv_container_set_slot(container, slot, obj_id, obj_count, scene_id, atlas_index);
    instance_revconfig_inv_mark_dirty(game, UI_INV_SOURCE_INVALID);
    GameRunescape_DispatchInvTransmit(game, container_id);
}

void
GameRunescape_IF3InvApplyFull(
    struct GameRunescape* game,
    int container_id,
    int const* obj_ids,
    int const* obj_counts,
    int count)
{
    assert(game);
    if( container_id < 0 )
        return;
    struct RSInvContainer* container =
        rs_inv_container_get_or_create(&game->inv_data.store, container_id, 0);
    assert(container);

    rs_inv_container_apply_full(container, obj_ids, obj_counts, count);
    if( game->ui_tree )
        uitree_mark_all_dirty(game->ui_tree);
    GameRunescape_DispatchInvTransmit(game, container_id);
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
    assert(game);
    if( container_id < 0 )
        return;
    struct RSInvContainer* container =
        rs_inv_container_get_or_create(&game->inv_data.store, container_id, 0);
    assert(container);

    rs_inv_container_apply_partial(container, slots, obj_ids, obj_counts, count);
    if( game->ui_tree )
        uitree_mark_all_dirty(game->ui_tree);
    GameRunescape_DispatchInvTransmit(game, container_id);
}

void
GameRunescape_SetUIInvPool(
    struct GameRunescape* game,
    struct UIInventoryPool* pool)
{
    assert(game);

    if( game->ui_inv_pool && game->ui_inv_pool != pool )
        uitree_inv_pool_free(game->ui_inv_pool);
    game->ui_inv_pool = pool;
}

void
GameRunescape_SyncUISpritesFromScene(struct GameRunescape* game)
{
    assert(game);
    assert(game);
    ToriDraw_SceneSpritesReemitLoads(game->scene);
    game->ui_sprites_synced = true;
}

void
GameRunescape_SetUITreeReady(
    struct GameRunescape* game,
    bool ready)
{
    assert(game);

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
    game->world_map.world_map_scene_id = RUNESCAPE_WORLD_MAP_SCENE_ID;
    game->world_map.world_map_w = pw;
    game->world_map.world_map_h = ph;
    GameRunescape_AttachWorldMapToUITree(game);
}

static void
game_runescape_clear_gamecache_map_chunks_except(
    struct ToriAuxLibCore* core,
    struct World* world)
{
    assert(core);
    if( !world )
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
    game->world_map.world_built = true;

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
    game->world_map.world_built = true;

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
    return ui_hover_routing_to_ids(game ? &game->ui_hover : NULL);
}

int32_t
GameRunescape_UISelectedSidebarIndex(struct GameRunescape const* game)
{
    assert(game);
    assert(game);

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
game_runescape_world_clip_is_builtin_widget(struct GameRunescape const* game)
{
    assert(game);

    int const vw = game->view_port ? game->view_port->width : UITREE_LAYOUT_ROOT_W;
    int const vh = game->view_port ? game->view_port->height : UITREE_LAYOUT_ROOT_H;
    int const clip_w = game->world_view_port.clip_right - game->world_view_port.clip_left;
    int const clip_h = game->world_view_port.clip_bottom - game->world_view_port.clip_top;

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
        return px >= game->world_view_port.clip_left && px < game->world_view_port.clip_right &&
               py >= game->world_view_port.clip_top && py < game->world_view_port.clip_bottom;
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
    assert(game);
    if( !game->ui_tree || component_id < 0 )
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
GameRunescape_UpdateUIHover(struct GameRunescape* game)
{
    ui_hover_routing_begin_frame(&game->ui_hover);
    if( !game->ui_tree || !game->ui_tree_ready )
        return;

    if( game->minimenu.visible )
        goto done;

    struct UITreeScrollState scroll = {
        .scroll_x = game->ui_scroll.scroll_x,
        .scroll_y = game->ui_scroll.scroll_y,
    };

    uitree_find_hovered_component_id_for_region(
        game->ui_tree,
        &game->ui_host,
        &scroll,
        game->world_pick.mouse_x,
        game->world_pick.mouse_y,
        RS_UI_HOVER_MAIN_X,
        RS_UI_HOVER_MAIN_Y,
        RS_UI_HOVER_MAIN_W,
        RS_UI_HOVER_MAIN_H,
        -1,
        &game->ui_hover.over_main_com_id);
    ui_hover_debug_log("main region -> over_main_com_id=%d", game->ui_hover.over_main_com_id);

    {
        int32_t const sidebar_idx = GameRunescape_UISelectedSidebarIndex(game);
        if( sidebar_idx >= 0 )
        {
            uitree_find_hovered_component_id_for_region(
                game->ui_tree,
                &game->ui_host,
                &scroll,
                game->world_pick.mouse_x,
                game->world_pick.mouse_y,
                RS_UI_HOVER_SIDE_X,
                RS_UI_HOVER_SIDE_Y,
                RS_UI_HOVER_SIDE_W,
                RS_UI_HOVER_SIDE_H,
                sidebar_idx,
                &game->ui_hover.over_side_com_id);
            ui_hover_debug_log(
                "side region -> over_side_com_id=%d", game->ui_hover.over_side_com_id);
        }
    }

    if( game->ui_hover.chat_node >= 0 )
    {
        uitree_find_hovered_component_id_for_region(
            game->ui_tree,
            &game->ui_host,
            &scroll,
            game->world_pick.mouse_x,
            game->world_pick.mouse_y,
            RS_UI_HOVER_CHAT_X,
            RS_UI_HOVER_CHAT_Y,
            RS_UI_HOVER_CHAT_W,
            RS_UI_HOVER_CHAT_H,
            game->ui_hover.chat_node,
            &game->ui_hover.over_chat_com_id);
        ui_hover_debug_log("chat region -> over_chat_com_id=%d", game->ui_hover.over_chat_com_id);
    }

done:
    game->ui_hover.hovered_node =
        GameRunescape_UIHitTest(game, game->world_pick.mouse_x, game->world_pick.mouse_y);
}

int32_t
GameRunescape_UIHitTest(
    struct GameRunescape* game,
    int px,
    int py)
{
    assert(game);
    assert(game);
    struct UITreeScrollState scroll = {
        .scroll_x = game->ui_scroll.scroll_x,
        .scroll_y = game->ui_scroll.scroll_y,
    };
    return uitree_hit_test_interactive(game->ui_tree, &game->ui_host, &scroll, px, py);
}

void
GameRunescape_GetScrollPos(
    struct GameRunescape const* game,
    int component_id,
    int* sx,
    int* sy)
{
    assert(game);

    struct UITreeScrollState scroll = {
        .scroll_x = (int*)game->ui_scroll.scroll_x,
        .scroll_y = (int*)game->ui_scroll.scroll_y,
    };
    uitree_scroll_get_pos(&scroll, component_id, sx, sy);
}

void
GameRunescape_ClampScroll(
    struct GameRunescape* game,
    struct StaticUIComponent const* layer)
{
    assert(game);
    if( !layer || layer->component_id < 0 )
        return;
    struct UITreeScrollState scroll = {
        .scroll_x = game->ui_scroll.scroll_x,
        .scroll_y = game->ui_scroll.scroll_y,
    };
    uitree_scroll_clamp_pos(layer, &scroll, layer->component_id);
}

#define RUNESCAPE_UI_SCROLL_DRAG_PADDING 32

static void
GameRunescape_UIScrollDragClear(struct GameRunescape* game)
{
    assert(game);

    ui_scroll_runtime_end_drag(&game->ui_scroll);
    game->frame.ui_scroll_cycle = 0;
}

static int32_t
GameRunescape_UIFindLayerIndexById(
    struct GameRunescape const* game,
    int component_id)
{
    assert(game);
    if( !game->ui_tree || component_id < 0 )
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
    assert(game);
    if( !game->ui_tree || !game->ui_tree_ready )
        return false;

    bool const mouse_held = LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);
    if( !mouse_held || LibToriRS_Input_IsDragEnd(input, TORIRSM_LEFT) )
    {
        GameRunescape_UIScrollDragClear(game);
        return false;
    }

    game->frame.ui_scroll_cycle++;

    struct UITreeScrollState scroll = {
        .scroll_x = game->ui_scroll.scroll_x,
        .scroll_y = game->ui_scroll.scroll_y,
    };
    struct UITreeScrollbarHitInfo hit = { 0 };
    bool changed = false;
    bool consume_click = false;

    if( game->ui_scroll.grabbed && game->ui_scroll.drag_layer_id >= 0 &&
        uitree_scrollbar_is_grip_kind(game->ui_scroll.drag_kind) )
    {
        int32_t layer_idx = GameRunescape_UIFindLayerIndexById(game, game->ui_scroll.drag_layer_id);
        if( layer_idx >= 0 &&
            uitree_scrollbar_hit_for_layer(game->ui_tree, &scroll, layer_idx, &hit) )
        {
            hit.kind = game->ui_scroll.drag_kind;
            if( uitree_scrollbar_handle(
                    game->ui_tree,
                    &scroll,
                    &hit,
                    game->world_pick.mouse_x,
                    game->world_pick.mouse_y,
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
        int const padding = game->ui_scroll.grabbed ? RUNESCAPE_UI_SCROLL_DRAG_PADDING : 0;
        if( uitree_find_scrollbar_at_padded(
                game->ui_tree,
                &game->ui_host,
                &scroll,
                game->world_pick.mouse_x,
                game->world_pick.mouse_y,
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
                        game->world_pick.mouse_x,
                        game->world_pick.mouse_y,
                        UITREE_SCROLLBAR_ACTION_ARROW_STEP,
                        step) )
                {
                    changed = true;
                    consume_click = true;
                    game->ui_scroll.drag_layer_id = layer->component_id;
                    game->ui_scroll.drag_kind = hit.kind;
                }
            }
            else if( uitree_scrollbar_is_grip_kind(hit.kind) && game->frame.ui_scroll_cycle > 0 )
            {
                if( uitree_scrollbar_handle(
                        game->ui_tree,
                        &scroll,
                        &hit,
                        game->world_pick.mouse_x,
                        game->world_pick.mouse_y,
                        UITREE_SCROLLBAR_ACTION_GRIP_DRAG,
                        0) )
                {
                    changed = true;
                    consume_click = true;
                    game->ui_scroll.drag_layer_id = layer->component_id;
                    game->ui_scroll.drag_kind = hit.kind;
                    game->ui_scroll.grabbed = true;
                }
            }
        }
    }

    if( changed )
    {
        uitree_mark_all_dirty(game->ui_tree);
        if( game->ui_scroll.drag_layer_id >= 0 )
        {
            int32_t layer_idx =
                GameRunescape_UIFindLayerIndexById(game, game->ui_scroll.drag_layer_id);
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
        component, &hover_ids, &game->ui_host, component->u.rs_rect.color);
}

void
GameRunescape_ProcessInput(
    struct GameRunescape* game,
    struct LibToriRS_Input* input)
{
    game->world_pick.mouse_x = input->curr.mouse_x;
    game->world_pick.mouse_y = input->curr.mouse_y;

    if( game->ui_tree && game->ui_tree_ready )
        GameRunescape_UpdateWorldViewport(game);

    game->world_pick.mouse_in_viewport =
        game->world_pick.mouse_x >= game->world_view_port.clip_left &&
        game->world_pick.mouse_x < game->world_view_port.clip_right &&
        game->world_pick.mouse_y >= game->world_view_port.clip_top &&
        game->world_pick.mouse_y < game->world_view_port.clip_bottom;

    if( game->ui_tree && game->ui_tree_ready )
    {
        bool scroll_consumed_click = GameRunescape_ProcessUIScroll(game, input);

        if( LibToriRS_Input_IsClick(input, TORIRSM_LEFT) && !scroll_consumed_click )
        {
            ui_click_handle_left(
                game, input, input->last_click_x[TORIRSM_LEFT], input->last_click_y[TORIRSM_LEFT]);
        }
    }

    if( LibToriRS_Input_IsClick(input, TORIRSM_RIGHT) )
    {
        bool const can_right_click =
            game->ui_tree && (game->ui_tree_ready || (game->world && game->world->load_complete));
        if( can_right_click )
        {
            ui_click_handle_right(
                game,
                input,
                input->last_click_x[TORIRSM_RIGHT],
                input->last_click_y[TORIRSM_RIGHT]);
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
        assert(ev);

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
    assert(game);

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
    bool const current_visible = GameRunescape_UINodeVisible(game, c, stepped_index);

    game->frame.ui_current = stepped_index;
    uitree_walk_advance(
        tree,
        &game->frame.ui_current,
        game->frame.ui_stack,
        &game->frame.ui_stack_top,
        RUNESCAPE_UI_TRAVERSAL_STACK_MAX,
        current_visible);
}

static void
GameRunescape_ApplyScissorToSprite(
    struct LibToriRS_RenderCommand* command,
    struct UITreeScrollClip const* clip)
{
    assert(command);
    if( !clip || clip->clip_w <= 0 || clip->clip_h <= 0 )
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
    assert(command);
    if( !clip || clip->clip_w <= 0 || clip->clip_h <= 0 )
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
    assert(command);
    if( !clip || clip->clip_w <= 0 || clip->clip_h <= 0 )
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
        .scroll_x = game->ui_scroll.scroll_x,
        .scroll_y = game->ui_scroll.scroll_y,
    };
    return scroll;
}

static int
GameRunescape_UIGetAncestors(
    struct GameRunescape* game,
    int32_t* ancestors,
    int max_ancestors)
{
    assert(game);
    if( !ancestors || max_ancestors <= 0 )
        return 0;

    int count = 0;
    for( int i = 0; i <= game->frame.ui_stack_top && count < max_ancestors; i++ )
        ancestors[count++] = game->frame.ui_stack[i];
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
    int ancestor_count =
        GameRunescape_UIGetAncestors(game, ancestors, RUNESCAPE_UI_TRAVERSAL_STACK_MAX);
    struct UITreeScrollState scroll_state = GameRunescape_UIScrollState(game);
    uitree_scroll_apply_ancestors(
        game->ui_tree, &scroll_state, ancestors, ancestor_count, bx, by, clip);
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
    assert(command);
    if( !layer || layer->type != UIELEM_RS_LAYER )
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
                command, game->ui_scroll.scrollbar0_scene_id, sb_x, ly, 0);
            return true;
        case 1:
            GameRunescape_EmitScrollbarArrow(
                command,
                game->ui_scroll.scrollbar1_scene_id,
                sb_x,
                ly + vh - UITREE_SCROLLBAR_THICKNESS,
                0);
            return true;
        case 2:
            if( !GameRunescape_VerticalScrollbarGrip(
                    layer, vh, lh, sy, &grip_y, &grip_size, &track_h) )
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
            if( !GameRunescape_VerticalScrollbarGrip(
                    layer, vh, lh, sy, &grip_y, &grip_size, &track_h) )
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
            if( !GameRunescape_VerticalScrollbarGrip(
                    layer, vh, lh, sy, &grip_y, &grip_size, &track_h) )
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
                    command,
                    sb_x,
                    grip_y0,
                    UITREE_SCROLLBAR_THICKNESS,
                    2,
                    UITREE_SCROLLBAR_GRIP_HI_ARGB);
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
            command, game->ui_scroll.scrollbar0_scene_id, lx, sb_y, h_arrow_rotation);
        return true;
    case 1:
        GameRunescape_EmitScrollbarArrow(
            command,
            game->ui_scroll.scrollbar1_scene_id,
            lx + sw - UITREE_SCROLLBAR_THICKNESS,
            sb_y,
            h_arrow_rotation);
        return true;
    case 2:
        if( !GameRunescape_HorizontalScrollbarGrip(
                layer, sw, lw, sx, &grip_x, &grip_size, &track_w) )
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
        if( !GameRunescape_HorizontalScrollbarGrip(
                layer, sw, lw, sx, &grip_x, &grip_size, &track_w) )
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
        if( !GameRunescape_HorizontalScrollbarGrip(
                layer, sw, lw, sx, &grip_x, &grip_size, &track_w) )
            return false;
        grip_x0 = lx + UITREE_SCROLLBAR_THICKNESS + grip_x;
        switch( hstep )
        {
        case 4:
            GameRunescape_EmitScrollbarFill(
                command,
                grip_x0,
                sb_y,
                2,
                UITREE_SCROLLBAR_THICKNESS,
                UITREE_SCROLLBAR_GRIP_HI_ARGB);
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
        bool const tiled = component->type == UIELEM_RS_GRAPHIC && component->u.rs_graphic.tiled;
        if( !tiled && (bw <= 0 || bh <= 0) && scene_id >= 0 && game->scene )
        {
            int sprite_count = 0;
            struct ToriDraw_Sprite** sprites =
                ToriDraw_SceneSpriteGet(game->scene, scene_id, &sprite_count);
            struct ToriDraw_Sprite* sp = (sprites && atlas_index >= 0 && atlas_index < sprite_count)
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
        GameRunescape_AssertSceneSpriteReady(game, scene_id, atlas_index, "ui_sprite");
        GameRunescape_EmitSpriteCommand(command, scene_id, atlas_index, bx, by, bw, bh);
        {
            int alpha = 255 - component->trans;
            if( alpha < 0 )
                alpha = 0;
            else if( alpha > 255 )
                alpha = 255;
            command->u.sprite.alpha = alpha;
        }
        if( tiled )
            command->u.sprite.tiled = 1;
        GameRunescape_ApplyScissorToSprite(command, &clip);
        return true;
    }
    case UIELEM_BUILTIN_COMPASS:
    {
        int scene_id = component->u.sprite.scene_id;
        int atlas_index = component->u.sprite.atlas_index;
        GameRunescape_AssertSceneSpriteReady(game, scene_id, atlas_index, "compass");
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
        {
            struct UITreeHostRequest req = {
                .kind = UITREE_HOST_GET_CROSS_POSITION,
                .u.get_cross_position.out_x = &cx,
                .u.get_cross_position.out_y = &cy,
            };
            uitree_host(&game->ui_host, &req);
        }
        cx -= component->position.anchor_x;
        cy -= component->position.anchor_y;

        struct UITreeHostRequest atlas_req = { .kind = UITREE_HOST_GET_CROSS_ATLAS_FRAME };
        int atlas_index = uitree_host(&game->ui_host, &atlas_req);
        GameRunescape_AssertSceneSpriteReady(game, scene_id, atlas_index, "cross");

        GameRunescape_EmitSpriteCommand(command, scene_id, atlas_index, cx, cy, bw, bh);
        return true;
    }
    case UIELEM_BUILTIN_MINIMENU:
    {
        assert(game);

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
            struct UIMinimenuLayout const draw_layout = ui_minimenu_layout_from_line_height(lh);
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
        {
            struct UITreeHostRequest req = {
                .kind = UITREE_HOST_GET_MINIMAP_ANCHOR,
                .u.get_minimap_anchor.out_x = &command->u.sprite.src_anchor_x,
                .u.get_minimap_anchor.out_y = &command->u.sprite.src_anchor_y,
            };
            uitree_host(&game->ui_host, &req);
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
        {
            struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
            if( uitree_host(&game->ui_host, &req) == component->u.redstone_tab.tabno )
            {
                if( component->u.redstone_tab.scene_id_active >= 0 )
                {
                    scene_id = component->u.redstone_tab.scene_id_active;
                    atlas_index = component->u.redstone_tab.atlas_index_active;
                }
            }
        }
        GameRunescape_AssertSceneSpriteReady(game, scene_id, atlas_index, "redstone_tab");
        GameRunescape_EmitSpriteCommand(command, scene_id, atlas_index, bx, by, bw, bh);
        return true;
    }
    case UIELEM_RS_TEXT:
        if( !uitree_component_text_source_host(&game->ui_host, component) )
            return false;
        GameRunescape_AssertSceneFontReady(game, component->u.rs_text.font_id, "rs_text");
        {
            command->kind = TORIRSRC_FONT;
            command->u.font.font_id = component->u.rs_text.font_id;
            command->u.font.x = bx;
            command->u.font.y = by;
            {
                struct UITreeHoverIds const hover_ids = GameRunescape_UIHoverIds(game);
                command->u.font.color = uitree_component_text_color_host(
                    component, &hover_ids, &game->ui_host, component->u.rs_text.color);
            }
            command->u.font.center = component->u.rs_text.center;
            command->u.font.y_align = component->u.rs_text.y_align;
            command->u.font.line_height = component->u.rs_text.line_height;
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
        {
            struct UITreeHostRequest req = {
                .kind = UITREE_HOST_GET_INV_SOURCE_SLOT,
                .u.get_inv_source_slot.source_id = component->u.inv_grid.inv_source_id,
                .u.get_inv_source_slot.slot = slot,
                .u.get_inv_source_slot.out = &slot_data,
            };
            if( uitree_host(&game->ui_host, &req) )
            {
                obj_id = slot_data.obj_id;
                scene_id = slot_data.scene_id;
                atlas_index = slot_data.atlas_index;
            }
        }

        if( obj_id > 0 && scene_id >= 0 )
        {
            GameRunescape_EmitSpriteCommand(command, scene_id, atlas_index, slot_x, slot_y, 32, 32);
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
        {
            struct UITreeHostRequest req = {
                .kind = UITREE_HOST_GET_INV_SOURCE_SLOT,
                .u.get_inv_source_slot.source_id = component->u.inv_slot.inv_source_id,
                .u.get_inv_source_slot.slot = component->u.inv_slot.slot,
                .u.get_inv_source_slot.out = &slot_data,
            };
            if( uitree_host(&game->ui_host, &req) )
            {
                obj_id = slot_data.obj_id;
                scene_id = slot_data.scene_id;
                atlas_index = slot_data.atlas_index;
            }
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
    case UIELEM_CC_OBJ:
    {
        int obj_id = component->u.cc_obj.obj_id;
        int scene_id = component->u.cc_obj.scene_id;
        int atlas_index = component->u.cc_obj.atlas_index;
        if( obj_id > 0 && scene_id < 0 && game )
        {
            rs_cs2_host_resolve_obj_icon(game, obj_id, &scene_id, &atlas_index);
            component->u.cc_obj.scene_id = scene_id;
            component->u.cc_obj.atlas_index = atlas_index;
        }
        if( obj_id > 0 && scene_id >= 0 )
        {
            int ix = bx;
            int iy = by;
            if( component->u.cc_obj.center_icon )
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
        {
            struct UIInvSlotData slot_data;
            struct UITreeHostRequest req = {
                .kind = UITREE_HOST_GET_INV_SOURCE_SLOT,
                .u.get_inv_source_slot.source_id = component->u.rs_inv_text.inv_source_id,
                .u.get_inv_source_slot.slot = slot,
                .u.get_inv_source_slot.out = &slot_data,
            };
            if( uitree_host(&game->ui_host, &req) )
                obj_id = slot_data.obj_id;
        }
        if( obj_id <= 0 )
            return false;

        char const* label = GameRunescape_ObjDisplayName(game, obj_id);
        if( !label || label[0] == '\0' )
            return false;

        GameRunescape_AssertSceneFontReady(game, component->u.rs_inv_text.font_id, "rs_inv_text");
        struct ToriDraw_Font* font =
            ToriDraw_SceneFontGet(game->scene, component->u.rs_inv_text.font_id);
        assert(font);

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
            fprintf(
                stderr,
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
                fprintf(
                    stderr,
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

    assert(game);
    if( !game->scene || !game->world )
        return;

    world = game->world;
    saved_mouse_x = game->world_pick.mouse_x;
    saved_mouse_y = game->world_pick.mouse_y;
    saved_mouse_in_viewport = game->world_pick.mouse_in_viewport;

    GameRunescape_UpdateWorldViewport(game);
    game->world_pick.mouse_x = mouse_x;
    game->world_pick.mouse_y = mouse_y;
    game->world_pick.mouse_in_viewport =
        game_runescape_mouse_in_world_viewport(game, mouse_x, mouse_y);
    world_pickset_reset(&game->world_pick.pickset);

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

    game->world_pick.mouse_x = saved_mouse_x;
    game->world_pick.mouse_y = saved_mouse_y;
    game->world_pick.mouse_in_viewport = saved_mouse_in_viewport;
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
        component->type == UIELEM_RS_LAYER && (uitree_scroll_layer_needs_vertical(component) ||
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
        bool const chat_button_more =
            component->type == UIELEM_BUILTIN_CHAT_BUTTON && game->frame.ui_chat_button_step > 0;
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
    assert(game);

    chat_state_set_filter_modes(&game->chat, public_mode, private_mode, trade_mode);
}

void
GameRunescape_SendChatSetMode(struct GameRunescape* game)
{
    assert(game);

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
    ui_hover_routing_begin_frame(&game->ui_hover);
    if( game->scene && game->ui_tree_ready && !game->ui_fonts_synced )
    {
        ToriDraw_SceneFontsReemitLoads(game->scene);
        game->ui_fonts_synced = true;
    }
    world_pickset_reset(&game->world_pick.pickset);
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
    {
        GameRunescape_TickAnimations(game);
        GameRunescape_CS2HostTick(&game->cs2_host);
    }
    GameRunescape_SyncProjectilesToScene(game);
    GameRunescape_UpdateWorldViewport(game);
    if( game->ui_tree && game->ui_tree->component_count > 0 )
    {
        bool const first_ready = !game->ui_tree_ready;

        uitree_layout_resolve(game->ui_tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        if( !game->ui_tree_ready && !game->ui_sprites_synced )
            GameRunescape_SyncUISpritesFromScene(game);
        game->ui_tree_ready = true;
        GameRunescape_FindSpecialUINodes(game);
        GameRunescape_UpdateUIHover(game);

        if( ui_hover_routing_commit_frame(&game->ui_hover) || first_ready )
            uitree_mark_all_dirty(game->ui_tree);

        if( first_ready && !game->ui_on_load_hooks_ran )
            GameRunescape_RunOnLoadHooks(game);

        rs_ui_flush_varp_transmits(game);
    }

    ui_cross_cursor_tick(&game->cross, 20);

    ui_minimenu_update_hover(&game->minimenu, game->world_pick.mouse_x, game->world_pick.mouse_y);
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

static struct EntityRecord*
GameRunescape_EntityFind(
    struct GameRunescape* game,
    int entity_id)
{
    return entity_registry_find(game ? &game->entities : NULL, entity_id);
}

static bool
GameRunescape_EntityRegister(
    struct GameRunescape* game,
    int entity_id,
    int element_id,
    int world_index)
{
    return entity_registry_register(
        game ? &game->entities : NULL, entity_id, element_id, world_index);
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

    assert(game);
    if( !game->td || !game->scene )
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

    assert(game);
    if( !game->world || !game->scene || !game->td )
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
    struct EntityRecord* record;

    assert(game);
    if( RS_ENTITY_KIND_OF(entity_id) == RS_ENTITY_KIND_NONE )
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

    assert(game);
    if( !game->world || !game->scene || !game->td )
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
    return task;
}

void
Task_GameRunescape_WorldEntityAddPlayer_Free(struct Task_GameRunescape_WorldEntityAddPlayer* task)
{
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

    PT_BEGIN(&task->thread);

    assert(task->game && task->game->td && task->game->world);

    TASK_AWAITEX(
        &task->thread,
        ctx,
        Task_CachePlayerAdd_New(
            ToriAuxLibTD_C(task->game->td),
            task->appearance,
            task->readyanim,
            task->walkanim,
            task->turnanim,
            task->runanim,
            task->walkanim_b,
            task->walkanim_r,
            task->walkanim_l));

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
    return task;
}

void
Task_GameRunescape_WorldEntityAddNPC_Free(struct Task_GameRunescape_WorldEntityAddNPC* task)
{
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

    PT_BEGIN(&task->thread);

    assert(task->game && task->game->td && task->game->world);

    TASK_AWAITEX(
        &task->thread,
        ctx,
        Task_CacheNpcAdd_New(ToriAuxLibTD_C(task->game->td), task->npc_id));

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
    return task;
}

void
Task_GameRunescape_WorldEntityAnimate_Free(struct Task_GameRunescape_WorldEntityAnimate* task)
{
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

    PT_BEGIN(&task->thread);

    assert(task->game && task->game->td);

    TASK_AWAITEX(
        &task->thread,
        ctx,
        Task_CacheAnimate_New(ToriAuxLibTD_C(task->game->td), task->anim_id));

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
    return task;
}

void
Task_GameRunescape_WorldEntityAddProjectile_Free(
    struct Task_GameRunescape_WorldEntityAddProjectile* task)
{
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

    PT_BEGIN(&task->thread);

    assert(task->game && task->game->td && task->game->world);

    TASK_AWAITEX(
        &task->thread,
        ctx,
        Task_CacheProjectileAdd_New(
            ToriAuxLibTD_C(task->game->td), task->projectile_id, task->anim_id));

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


bool
GameRunescape_CS2Enqueue(
    struct GameRunescape* game,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count,
    char const* const* str_args,
    int str_arg_count)
{
    assert(game);
    if( script_id <= 0 )
        return false;
    rs_ui_init_cs2_host(game);
    return GameRunescape_CS2QueueEnqueue(
        &game->cs2_queue,
        script_id,
        component_id,
        int_args,
        int_arg_count,
        str_args,
        str_arg_count);
}

bool
GameRunescape_CS2FlushQueueToTasks(
    struct GameRunescape* game,
    struct LibToriRS_Instance* instance)
{
    assert(game);
    if( !instance )
        return false;
    bool added = false;
    while( game->cs2_queue.count > 0 )
    {
        struct GameRunescapeCS2Invoke const* inv = GameRunescape_CS2QueuePeek(&game->cs2_queue);
        assert(inv);

        struct LibToriRS_Task* task = Task_Dat2CS2Run_New(
            game,
            &game->cs2vm,
            &game->cs2_host,
            inv->script_id,
            inv->component_id,
            inv->int_args,
            inv->int_arg_count,
            (char const* const*)inv->str_args,
            inv->str_arg_count);
        if( !task )
        {
            fprintf(stderr, "GameRunescape_CS2FlushQueueToTasks: Task_Dat2CS2Run_New failed\n");
            GameRunescape_CS2QueuePop(&game->cs2_queue);
            continue;
        }
        LibToriRS_TasksAdd(instance, task);
        GameRunescape_CS2QueuePop(&game->cs2_queue);
        added = true;
    }
    return added;
}

bool
GameRunescape_CS2IsIdle(struct GameRunescape const* game)
{
    assert(game);

    return game->cs2_queue.count == 0;
}
