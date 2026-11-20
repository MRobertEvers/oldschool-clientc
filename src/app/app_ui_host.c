/*
 * Publishing the retained tree's inputs, the inventory change hook, and the
 * CS1 evaluation task.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"
#include "mem/torirs_mem.h"

/* Wrapper protothread that owns one CS1 evaluation pass: awaits the eval
 * task (which may itself yield for pack loads), then clears the in-flight
 * gate and requests a redraw when a cached result changed. */
struct Task_AppCS1Eval
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
};

/* Private to this unit, declared up front so definition order is free. */
static int
Task_AppCS1Eval_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io);
static void
Task_AppCS1Eval_Free(struct ToriRS_Task* base);

/* Resolve a component id at the point an app-owned action is about to use it.
 * Interaction and minimenu models deliberately retain ids across frames, while
 * CC_DELETEALL may reclaim their old node and plugin layouts may suppress an
 * ancestor after the model was built.  A missing id is not visible -- the
 * public ComponentOrAncestorDisplayHidden helper cannot make that distinction
 * because it starts from an already-resolved node. */
int32_t
app_displayable_component_node(
    struct App const* app,
    int component_id)
{
    int32_t idx;

    if( !app || !app->tree || component_id < 0 )
        return -1;
    idx = UITree_FindByComponentId(app->tree, component_id);
    if( idx < 0 || (uint32_t)idx >= app->tree->component_count ||
        app->tree->components[idx].freed || UITree_NodeOrAncestorDisplayHidden(app->tree, idx) )
        return -1;
    return idx;
}

int
app_intent_targets_live(
    struct App const* app,
    struct UIIntent const* intent)
{
    if( intent->has_node_identity )
    {
        struct UITreeComponent const* node;
        if( !app || !app->tree || intent->node_index < 0 ||
            (uint32_t)intent->node_index >= app->tree->component_count )
            return 0;
        node = &app->tree->components[intent->node_index];
        if( node->freed || intent->node_incarnation == 0 ||
            node->incarnation != intent->node_incarnation ||
            (intent->component_id >= 0 && node->component_id != intent->component_id) ||
            UITree_NodeOrAncestorDisplayHidden(app->tree, intent->node_index) )
            return 0;
    }
    else if( app_displayable_component_node(app, intent->component_id) < 0 )
        return 0;
    /* -1 is the ordinary "no drop target" carried by onDrag. */
    if( intent->has_drag_target && intent->drag_target_id >= 0 && intent->has_drag_target_identity )
    {
        struct UITreeComponent const* target;
        if( !app || !app->tree || intent->drag_target_node_index < 0 ||
            (uint32_t)intent->drag_target_node_index >= app->tree->component_count )
            return 0;
        target = &app->tree->components[intent->drag_target_node_index];
        if( target->freed || intent->drag_target_node_incarnation == 0 ||
            target->incarnation != intent->drag_target_node_incarnation ||
            target->component_id != intent->drag_target_id ||
            UITree_NodeOrAncestorDisplayHidden(app->tree, intent->drag_target_node_index) )
            return 0;
    }
    else if(
        intent->has_drag_target && intent->drag_target_id >= 0 &&
        app_displayable_component_node(app, intent->drag_target_id) < 0 )
        return 0;
    return 1;
}

/* ---- Retained UITree host-input publication --------------------------- *
 *
 * Host requests copy ambient App state into otherwise-retainable descriptors.
 * The emit walk records which coarse domains it actually read; this publication
 * fence gives those domains semantic versions without making each state writer
 * know which nodes (or even which open interface) consumed it.
 *
 * These hashes are not render caches. They are compact compare-before-bump
 * snapshots of the values host requests can expose. Event-driven sources such
 * as InvManager also bump their domain directly, while the snapshot closes
 * over local selection state which is not owned by that manager. */
void
app_ui_host_publish_inputs(struct App* app)
{
    uint64_t signature[UITREE_HOST_INPUT_DOMAIN_COUNT];
    struct WorldEntity_Player const* local;
    struct UIMinimenu const* menu;
    int menu_count;
    int ghosting;

    assert(app);
    for( int domain = 0; domain < UITREE_HOST_INPUT_DOMAIN_COUNT; domain++ )
        signature[domain] = UITree_InputSignatureInt(UITREE_INPUT_SIGNATURE_OFFSET, domain + 1);

    /* CAMERA: everything used by yaw-based chrome and world projection. The
     * local player is the minimap anchor when present; free camera position is
     * the fallback. */
    signature[UITREE_HOST_INPUT_CAMERA] = UITree_InputSignatureInt(
        signature[UITREE_HOST_INPUT_CAMERA],
        ToriDraw_NormalizeAngle(app->world_views.frame->world_camera.yaw));
    signature[UITREE_HOST_INPUT_CAMERA] = UITree_InputSignatureInt(
        signature[UITREE_HOST_INPUT_CAMERA], app->world_views.frame->world_camera.pitch);
    signature[UITREE_HOST_INPUT_CAMERA] = UITree_InputSignatureInt(
        signature[UITREE_HOST_INPUT_CAMERA], app->world_views.frame->world_camera_pos.x);
    signature[UITREE_HOST_INPUT_CAMERA] = UITree_InputSignatureInt(
        signature[UITREE_HOST_INPUT_CAMERA], app->world_views.frame->world_camera_pos.y);
    signature[UITREE_HOST_INPUT_CAMERA] = UITree_InputSignatureInt(
        signature[UITREE_HOST_INPUT_CAMERA], app->world_views.frame->world_camera_pos.z);
    /* What the world letterbox is computed from (UITREE_HOST_GET_WORLD_LETTERBOX):
     * the SETFOV endpoints and the four CLAMPFOV bounds. A cutscene's clamp
     * changes nothing else the walk reads, so without these the gate would keep
     * drawing the uncut world into a box the scripts have already narrowed. */
    {
        int const viewport_inputs[6] = {
            app->host.viewport_zoom_near, app->host.viewport_zoom_far,
            app->host.viewport_fov_min,   app->host.viewport_fov_max_clamp,
            app->host.viewport_aspect_min, app->host.viewport_aspect_max,
        };
        for( int i = 0; i < 6; i++ )
            signature[UITREE_HOST_INPUT_CAMERA] = UITree_InputSignatureInt(
                signature[UITREE_HOST_INPUT_CAMERA], viewport_inputs[i]);
    }
    /* And the map editor's World panel, which cuts the same letterbox. */
    signature[UITREE_HOST_INPUT_CAMERA] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], App_MapEditorWorldSignature(app));
    local = app_local_player(app);
    signature[UITREE_HOST_INPUT_CAMERA] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], local != NULL);
    if( local )
    {
        signature[UITREE_HOST_INPUT_CAMERA] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], (int)local->draw_position.x);
        signature[UITREE_HOST_INPUT_CAMERA] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], (int)local->draw_position.z);
    }

    /* POINTER: hash only visible/observable phases. Inactive cross/menu/hover
     * scratch may move without changing a descriptor and should not defeat a
     * quiet retained frame. */
    signature[UITREE_HOST_INPUT_POINTER] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], UICross_IsActive(&app->cross));
    if( UICross_IsActive(&app->cross) )
    {
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->cross.x);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->cross.y);
        signature[UITREE_HOST_INPUT_POINTER] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_POINTER], UICross_AtlasFrame(&app->cross));
    }
    /* The runner camera split: the other view's menu is drawn too (tinted),
     * so its phases are this frame's as well. */
    if( app->world_views.split.attached )
    {
        struct UIMinimenu const* other =
            app->world_views.views[app->world_views.frame == &app->world_views.views[APP_VIEW_RUNNER] ? APP_VIEW_PLAYER_CLIENT : APP_VIEW_RUNNER]
                .minimenu;
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], other->visible);
        if( other->visible )
        {
            signature[UITREE_HOST_INPUT_POINTER] =
                UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], other->x);
            signature[UITREE_HOST_INPUT_POINTER] =
                UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], other->y);
            signature[UITREE_HOST_INPUT_POINTER] =
                UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], other->option_count);
            signature[UITREE_HOST_INPUT_POINTER] =
                UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], other->hovered_option);
        }
    }
    menu = app->world_views.frame->minimenu;
    signature[UITREE_HOST_INPUT_POINTER] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->visible);
    if( menu->visible )
    {
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->x);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->y);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->width);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->height);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->hovered_option);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->font_id);
        signature[UITREE_HOST_INPUT_POINTER] = UITree_InputSignatureBytes(
            signature[UITREE_HOST_INPUT_POINTER], &menu->layout, sizeof(menu->layout));
        menu_count = menu->option_count;
        if( menu_count < 0 )
            menu_count = 0;
        if( menu_count > UITREE_MINIMENU_MAX_OPTIONS )
            menu_count = UITREE_MINIMENU_MAX_OPTIONS;
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu_count);
        for( int i = 0; i < menu_count; i++ )
        {
            struct UIMinimenuOption const* option = &menu->options[i];
            signature[UITREE_HOST_INPUT_POINTER] =
                UITree_InputSignatureString(signature[UITREE_HOST_INPUT_POINTER], option->text);
            signature[UITREE_HOST_INPUT_POINTER] = UITree_InputSignatureBytes(
                signature[UITREE_HOST_INPUT_POINTER], &option->action, sizeof(option->action));
            signature[UITREE_HOST_INPUT_POINTER] = UITree_InputSignatureBytes(
                signature[UITREE_HOST_INPUT_POINTER],
                &option->action_index,
                sizeof(option->action_index));
            signature[UITREE_HOST_INPUT_POINTER] = UITree_InputSignatureBytes(
                signature[UITREE_HOST_INPUT_POINTER], &option->pick, sizeof(option->pick));
        }
    }
    signature[UITREE_HOST_INPUT_POINTER] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->hover_text.visible);
    if( app->hover_text.visible )
    {
        signature[UITREE_HOST_INPUT_POINTER] = UITree_InputSignatureBytes(
            signature[UITREE_HOST_INPUT_POINTER],
            &app->hover_text.x,
            sizeof(app->hover_text.x) * 5);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureString(signature[UITREE_HOST_INPUT_POINTER], app->hover_text.text);
    }
    ghosting = app_inv_drag_ghosting(app);
    signature[UITREE_HOST_INPUT_POINTER] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], ghosting);
    if( ghosting )
    {
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->inv_drag.component_id);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->inv_drag.source_id);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->inv_drag.from_slot);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->inv_drag.dx);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->inv_drag.dy);
    }

    /* CLIENT_STATE: selected/available tabs, chat presentation and the server
     * IF_SETEVENTS overrides copied into host-produced menu descriptors. */
    signature[UITREE_HOST_INPUT_CLIENT_STATE] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_CLIENT_STATE], &app->slots, sizeof(app->slots));
    signature[UITREE_HOST_INPUT_CLIENT_STATE] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_CLIENT_STATE], &app->chat_view, sizeof(app->chat_view));
    signature[UITREE_HOST_INPUT_CLIENT_STATE] = UITree_InputSignatureInt(
        signature[UITREE_HOST_INPUT_CLIENT_STATE], app->if_events.count);
    if( app->if_events.count > 0 )
        signature[UITREE_HOST_INPUT_CLIENT_STATE] = UITree_InputSignatureBytes(
            signature[UITREE_HOST_INPUT_CLIENT_STATE],
            app->if_events.ranges,
            (size_t)app->if_events.count * sizeof(*app->if_events.ranges));

    /* INVENTORY: container contents publish through the InvManager callback;
     * selection and drag addressing live on App and need this small snapshot. */
    signature[UITREE_HOST_INPUT_INVENTORY] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_INVENTORY], &app->invs.selection, sizeof(app->invs.selection));
    signature[UITREE_HOST_INPUT_INVENTORY] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_INVENTORY], &app->target_select.obj, sizeof(app->target_select.obj));
    {
        /* WHICH item the gesture owns, never how far it has been carried: the
         * offset already reaches emit through the POINTER lane, and hashing it
         * here would re-walk the whole inventory on every frame of a drag. */
        int drag_key[4];
        UIInvDrag_AddressingKey(&app->inv_drag, drag_key);
        signature[UITREE_HOST_INPUT_INVENTORY] = UITree_InputSignatureBytes(
            signature[UITREE_HOST_INPUT_INVENTORY], drag_key, sizeof(drag_key));
    }

    /* ASSETS: owner-side mutation revisions catch arrivals before a skipped
     * host request gets another chance to publish them, plus same-id registry
     * replacements which map cardinality cannot see. Provider model/sprite
     * streaming may conservatively cause an extra full UI walk; three scalar
     * reads are still cheaper and more reliable than scanning the registries. */
    signature[UITREE_HOST_INPUT_ASSETS] = UITree_InputSignatureU64(
        signature[UITREE_HOST_INPUT_ASSETS],
        app->provider ? CacheProvider_UIAssetRevision(app->provider) : 0);
    signature[UITREE_HOST_INPUT_ASSETS] = UITree_InputSignatureU64(
        signature[UITREE_HOST_INPUT_ASSETS], UITreeSceneBridge_AssetRevision(&app->bridge));
    signature[UITREE_HOST_INPUT_ASSETS] = UITree_InputSignatureU64(
        signature[UITREE_HOST_INPUT_ASSETS],
        app->scene ? ToriDraw_SceneUIAssetRevision(app->scene) : 0);

    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], MinimapView_ToggleState(&app->minimap));
    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], RS_SessionInfo_Multiway(&app->session_info));
    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], MinimapView_MapSceneId(&app->minimap));
    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], MinimapView_MapWidth(&app->minimap));
    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], MinimapView_MapHeight(&app->minimap));
    signature[UITREE_HOST_INPUT_WORLD] = UITree_InputSignatureInt(
        signature[UITREE_HOST_INPUT_WORLD], app->world && app->world->load_complete);
    if( app->world && app->world->minimap )
    {
        signature[UITREE_HOST_INPUT_WORLD] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->world->minimap->width);
        signature[UITREE_HOST_INPUT_WORLD] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->world->minimap->height);
    }

    /* Hash visible animation phases, not raw clocks: an inactive cross and a
     * non-flashing tab do not change their descriptors as cycles advance. */
    signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
        signature[UITREE_HOST_INPUT_ANIMATION], UICross_IsActive(&app->cross));
    if( UICross_IsActive(&app->cross) )
        signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_ANIMATION], UICross_AtlasFrame(&app->cross));
    signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
        signature[UITREE_HOST_INPUT_ANIMATION], app->session_info.reboot_timer != 0);
    if( app->session_info.reboot_timer != 0 )
        signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_ANIMATION],
            app->session_info.reboot_timer / APP_LOGIC_CYCLES_PER_SECOND);
    /*
     * The login caret's blink phase, and only the phase.
     *
     * Hashing logic_cycle itself would bump the animation epoch every frame
     * and the title screen would never retain anything; hashing the half of
     * the period the caret is in flips the epoch exactly twice per blink,
     * which is how often the screen actually changes. The period is the
     * widget's (revconfig caret_blink=), so the client asks the tree for it.
     */
    if( RS_ClientPhase_OnTitle(&app->phase) )
    {
        int blink = app_title_caret_blink(app);
        signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_ANIMATION],
            blink > 0 ? (int)(FrameClockSerial_LogicCycle(&app->frame_clock.serial) %
                              (uint64_t)blink) < blink / 2
                      : 0);
    }
    /*
     * The fire's step counter, while there is a fire.
     *
     * always_dirty on the node is only half of the contract and does not
     * work alone: it keeps the node's own descriptor out of the retained
     * list, but nothing rebuilds the FRAME unless an input epoch moves, so
     * a title screen whose only animation is the braziers retains the very
     * first frame forever. That is exactly what shipped -- the fire ran, the
     * sprite was re-uploaded every step, and the screen kept showing the
     * seed row from step one, a bright line at the bowl rim and nothing
     * above it.
     *
     * The counter and not the clock: it moves once per fixed 35 ms step, so
     * the epoch flips exactly when the pixels do rather than every frame.
     */
    if( app->flames )
        signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_ANIMATION], app->flames->update_index);
    signature[UITREE_HOST_INPUT_ANIMATION] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_ANIMATION], app->slots.flash_tab);
    if( app->slots.flash_tab >= 0 )
        signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_ANIMATION],
            RS_UISlots_TabFlashHidden(
                &app->slots,
                app->slots.flash_tab,
                FrameClockSerial_LogicCycle(&app->frame_clock.serial)));

    /* Same-frame world/plugin overlay arrays are refreshed by source-tagged
     * standing records, including sources currently returning zero items.
     * Chrome is retained data, so its exact build serials participate here. */
    signature[UITREE_HOST_INPUT_OVERLAYS] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_OVERLAYS],
        &app->dev_overlay->ui.build_serial,
        sizeof(app->dev_overlay->ui.build_serial));
    signature[UITREE_HOST_INPUT_OVERLAYS] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_OVERLAYS],
        &app->plugin_win->ui.build_serial,
        sizeof(app->plugin_win->ui.build_serial));
    for( int domain = 0; domain < UITREE_HOST_INPUT_DOMAIN_COUNT; domain++ )
        (void)UITree_HostPublishInputSignature(
            &app->ui_host, (enum UITreeHostInputDomain)domain, signature[domain]);
}

void
app_inv_ui_host_change(
    void* userdata,
    int container_id)
{
    struct App* app = (struct App*)userdata;
    assert(app);

    /* DRIVE_STAMP: inv_changed -- a=container_id. inv.await filters by
     * container, so the id has to make it into the ring, not just get read
     * and dropped the way it was before this stamp existed. */
    App_DriveEvent(app, RS_EVENT_INV_CHANGED, container_id, 0, 0, 0);

    /* Every slot of it whose item has no icon is wanted now (a task each,
     * app/app_tasks_if_assets.c); nothing scans the inventories later. */
    App_InvIconsWant(app, container_id);

    UITree_HostInputsChanged(&app->ui_host, UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_INVENTORY));
    RedrawRequest_Raise(&app->redraw_request);
}

static int
Task_AppCS1Eval_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_AppCS1Eval* self = (struct Task_AppCS1Eval*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);
    app->cs1_host.eval_dirty = false;
    PT_TASK_AWAITSELF_IF(CreateTask_CS1Eval(&app->cs1_host));
    RS_CS1Host_SetEvalInflight(&app->cs1_host, false);
    if( app->cs1_host.eval_dirty )
        RedrawRequest_Raise(&app->redraw_request);
    PT_END(&self->pt);
}

static void
Task_AppCS1Eval_Free(struct ToriRS_Task* base)
{
    ToriMem_Free(ToriMem_Script(), base);
}

static struct ToriRS_TaskVTable Task_AppCS1Eval_VTable = {
    .run = Task_AppCS1Eval_Run,
    .free = Task_AppCS1Eval_Free,
};

/* Request a CS1 evaluation pass; at most one is ever in flight (the tick
 * re-requests every 20ms anyway, so a busy pass simply coalesces). Never
 * blocks — the frame pump drives it. */
void
app_request_cs1_eval(struct App* app)
{
    struct Task_AppCS1Eval* task;

    if( RS_CS1Host_EvalInflight(&app->cs1_host) )
        return;
    task = ToriMem_Calloc(ToriMem_Script(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppCS1Eval_VTable;
    strncpy(task->task.name, "AppCS1Eval", sizeof(task->task.name) - 1);
    task->app = app;
    PT_INIT(&task->pt);
    RS_CS1Host_SetEvalInflight(&app->cs1_host, true);
    ToriRS_TaskQueue_Add(app->frame_runner.queue, &task->task);
}

int
app_measure_text_cb(
    void* user,
    int font_id,
    char const* text)
{
    struct App* app = (struct App*)user;
    struct ToriDraw_Font* font = ToriDraw_SceneFontGet(app->scene, font_id);
    if( !font )
        return 0;
    assert(text);
    return ToriDraw2D_MeasureString(font, text);
}
