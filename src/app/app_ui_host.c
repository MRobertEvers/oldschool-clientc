/*
 * Publishing the retained tree's inputs, the inventory icon reconcile, and the
 * CS1 evaluation task.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* ---- Inventory obj-icon reconcile ------------------------------------- *
 *
 * Server UPDATE_INV_FULL/PARTIAL (rs_gameproto_exec.c) write item ids into the
 * inv containers but leave scene_id = INV_MANAGER_NO_SCENE_ID, because the
 * inventory model may not be resident and rasterizing needs it loaded. The
 * emit path (emit_rs_inv_slots) only draws a slot when scene_id >= 0, so those
 * items never appear. This mirrors task_interface_open's seed-time icon step
 * (load the models, then UITreeSceneBridge_EnsureObjIcon and stamp the scene id
 * back) but is driven per tick off the live containers, so items that arrive
 * after the interface is open still get icons — the missing lazy path the
 * exec handlers' comment promised.
 *
 * A slot whose model can never be built is stamped with a distinct sentinel so
 * the per-tick scan stops re-enqueueing it; the server replacing the item
 * resets scene_id to NO_SCENE_ID and re-arms the reconcile. */
#define APP_INV_ICON_BATCH_MAX 64
#define APP_INV_ICON_SCENE_FAILED (-2)
/* Reconcile passes a slot may spend waiting for its objtype, model and
 * textures before the icon is given up on. One pass per tick, so this is a
 * two-second ceiling on a cache that answers over the network. */
#define APP_INV_ICON_ATTEMPT_MAX 100

struct Task_InvIconReconcile
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int obj_ids[APP_INV_ICON_BATCH_MAX];
    int counts[APP_INV_ICON_BATCH_MAX];
    int n;
    int published_change;
};

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
Task_InvIconReconcile_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io);
static void
Task_InvIconReconcile_Free(struct ToriRS_Task* base);
static int
Task_AppCS1Eval_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io);
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
        signature[UITREE_HOST_INPUT_CAMERA], ToriDraw_NormalizeAngle(app->world_camera.yaw));
    signature[UITREE_HOST_INPUT_CAMERA] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], app->world_camera.pitch);
    signature[UITREE_HOST_INPUT_CAMERA] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], app->world_camera_pos.x);
    signature[UITREE_HOST_INPUT_CAMERA] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], app->world_camera_pos.y);
    signature[UITREE_HOST_INPUT_CAMERA] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], app->world_camera_pos.z);
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
    menu = &app->interact.minimenu;
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
        signature[UITREE_HOST_INPUT_INVENTORY], &app->objsel, sizeof(app->objsel));
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
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->minimap_state);
    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->multiway);
    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->world_map_scene_id);
    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->world_map_w);
    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->world_map_h);
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
    signature[UITREE_HOST_INPUT_ANIMATION] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_ANIMATION], app->reboot_timer != 0);
    if( app->reboot_timer != 0 )
        signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_ANIMATION],
            app->reboot_timer / APP_LOGIC_CYCLES_PER_SECOND);
    /*
     * The login caret's blink phase, and only the phase.
     *
     * Hashing logic_cycle itself would bump the animation epoch every frame
     * and the title screen would never retain anything; hashing the half of
     * the period the caret is in flips the epoch exactly twice per blink,
     * which is how often the screen actually changes. The period is the
     * widget's (revconfig caret_blink=), so the client asks the tree for it.
     */
    if( app->screen == APP_SCREEN_TITLE || app->screen == APP_SCREEN_CONNECTING )
    {
        int blink = app_title_caret_blink(app);
        signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_ANIMATION],
            blink > 0 ? (int)(app->logic_cycle % (uint64_t)blink) < blink / 2 : 0);
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
            RS_UISlots_TabFlashHidden(&app->slots, app->slots.flash_tab, app->logic_cycle));

    /* Same-frame world/plugin overlay arrays are refreshed by source-tagged
     * standing records, including sources currently returning zero items.
     * Chrome is retained data, so its exact build serials participate here. */
    signature[UITREE_HOST_INPUT_OVERLAYS] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_OVERLAYS],
        &app->dbg_ui.build_serial,
        sizeof(app->dbg_ui.build_serial));
    signature[UITREE_HOST_INPUT_OVERLAYS] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_OVERLAYS],
        &app->plugin_ui.build_serial,
        sizeof(app->plugin_ui.build_serial));
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
    (void)container_id;

    UITree_HostInputsChanged(&app->ui_host, UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_INVENTORY));
    app->need_redraw = 1;
}

static int
Task_InvIconReconcile_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_InvIconReconcile* self = (struct Task_InvIconReconcile*)base;
    struct App* app = self->app;
    (void)io;

    PT_BEGIN(&self->pt);

    self->published_change = 0;
    /* Collect the batch that still needs a model load (bounded; leftovers are
     * caught by the next tick's scan once this pass stamps its slots). */
    self->n = 0;
    for( int ci = 0; ci < app->invs.container_count && self->n < APP_INV_ICON_BATCH_MAX; ci++ )
    {
        struct InvContainer const* c = &app->invs.containers[ci];
        if( !c->slots )
            continue;
        for( int s = 0; s < c->slot_count && self->n < APP_INV_ICON_BATCH_MAX; s++ )
        {
            struct InvSlot const* slot = &c->slots[s];
            if( slot->obj_id > 0 && slot->scene_id == INV_MANAGER_NO_SCENE_ID )
            {
                self->obj_ids[self->n] = slot->obj_id;
                self->counts[self->n] = slot->obj_count > 0 ? slot->obj_count : 1;
                self->n++;
            }
        }
    }
    if( self->n > 0 )
        PT_TASK_AWAITSELF_IF(
            CreateTask_ObjModelLoad(app->provider, self->obj_ids, self->counts, self->n));

    /*
     * Rasterize the pending slots whose parts have actually landed, and leave
     * the rest pending for a later pass.
     *
     * Asking `ObjModelLoad_NeedsWork` first is the whole fix for icons that go
     * missing at random. The batch above is capped, so on a bank (1410 slots)
     * or a busy boot most pending slots never had their models requested; an
     * UPDATE_INV that lands while this task awaits adds slots it never asked
     * for either; and against an on-demand cache the answer arrives over the
     * network whenever it arrives. In all three cases the raster below used to
     * find nothing resident, stamp APP_INV_ICON_SCENE_FAILED, and the per-tick
     * scan — which only looks at INV_MANAGER_NO_SCENE_ID — would never come
     * back. The slot stayed blank until the server replaced the item.
     *
     * Baking too early is the same bug wearing the other face: an icon rastered
     * before its model's textures arrive is cached blank against that
     * (obj, count) key for the rest of the session, which draws as a stack
     * count with no item under it.
     *
     * The attempt counter bounds the wait. An obj whose model genuinely never
     * resolves keeps NeedsWork true forever, and without a bound it would
     * re-arm this reconcile every tick for the rest of the session.
     */
    for( int ci = 0; ci < app->invs.container_count; ci++ )
    {
        struct InvContainer* c = &app->invs.containers[ci];
        if( !c->slots )
            continue;
        for( int s = 0; s < c->slot_count; s++ )
        {
            struct InvSlot* slot = &c->slots[s];
            int count;
            int scene_id;
            if( slot->obj_id <= 0 || slot->scene_id != INV_MANAGER_NO_SCENE_ID )
                continue;
            count = slot->obj_count > 0 ? slot->obj_count : 1;
            scene_id = UITreeSceneBridge_EnsureObjIcon(&app->bridge, slot->obj_id, count);
            if( scene_id >= 0 )
            {
                slot->scene_id = scene_id;
                slot->atlas_index = 0;
                self->published_change = 1;
                continue;
            }
            /* Could not build it *yet*: leave the slot pending so the next
             * pass batches it, until the attempts run out. */
            if( ObjModelLoad_NeedsWork(app->provider, slot->obj_id, count) &&
                ++slot->icon_attempts < APP_INV_ICON_ATTEMPT_MAX )
                continue;
            slot->scene_id = APP_INV_ICON_SCENE_FAILED;
            slot->atlas_index = 0;
            self->published_change = 1;
        }
    }

    if( self->published_change )
        UITree_HostInputsChanged(
            &app->ui_host,
            UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_INVENTORY) |
                UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_ASSETS));
    app->inv_icon_reconcile_inflight = 0;
    app->need_redraw = 1;
    PT_END(&self->pt);
}

static void
Task_InvIconReconcile_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_InvIconReconcile_VTable = {
    .run = Task_InvIconReconcile_Run,
    .free = Task_InvIconReconcile_Free,
};

/* Per-tick hook: enqueue one reconcile if any item icon is still unresolved and
 * none is already running. Serial on the exec pipeline so it applies after the
 * inventory packets that dirtied the slots. */
void
app_inv_icon_reconcile_tick(struct App* app)
{
    struct Task_InvIconReconcile* task;

    if( app->inv_icon_reconcile_inflight || !InvManager_HasUnbakedIcon(&app->invs) )
        return;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_InvIconReconcile_VTable;
    strncpy(task->task.name, "InvIconReconcile", sizeof(task->task.name) - 1);
    task->app = app;
    PT_INIT(&task->pt);
    app->inv_icon_reconcile_inflight = 1;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

static int
Task_AppCS1Eval_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppCS1Eval* self = (struct Task_AppCS1Eval*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);
    app->cs1_host.eval_dirty = false;
    PT_TASK_AWAITSELF_IF(CreateTask_CS1Eval(&app->cs1_host));
    app->cs1_eval_inflight = 0;
    if( app->cs1_host.eval_dirty )
        app->need_redraw = 1;
    PT_END(&self->pt);
}

static void
Task_AppCS1Eval_Free(struct ToriRS_Task* base)
{
    free(base);
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

    if( app->cs1_eval_inflight )
        return;
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppCS1Eval_VTable;
    strncpy(task->task.name, "AppCS1Eval", sizeof(task->task.name) - 1);
    task->app = app;
    PT_INIT(&task->pt);
    app->cs1_eval_inflight = 1;
    ToriRS_TaskQueue_Add(app->runner.queue, &task->task);
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
