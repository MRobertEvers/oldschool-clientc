/*
 * Ground-item stacks and the scene rebuild: the shift, the begin, and the
 * stack bookkeeping.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"
#include "boot_telemetry.h"

/* Tell the plugins about a ground-item stack, by pool index. One helper for
 * all three edges so the snapshot is filled the same way every time -- and so
 * a despawn can be announced BEFORE the entity is released, while there is
 * still something whole to describe. */
enum AppPluginGroundItemChange
{
    APP_PLUGIN_ITEM_SPAWN,
    APP_PLUGIN_ITEM_CHANGE,
    APP_PLUGIN_ITEM_DESPAWN
};

/* Private to this unit, declared up front so definition order is free. */
static void
app_plugin_obj_notify(
    struct App* app,
    int idx,
    enum AppPluginGroundItemChange which);
static struct ToriDraw_Model*
app_obj_stack_build_model(
    struct App* app,
    int obj_id,
    int count);
static void
app_obj_stack_refresh_model(
    struct App* app,
    struct World* world,
    int idx,
    int count);

static void
app_plugin_obj_notify(
    struct App* app,
    int idx,
    enum AppPluginGroundItemChange which)
{
    struct WorldEntity_ObjStack* stack;
    struct ToriRS_GroundItemSnapshot snap;

    assert(app);
    if( !app->plugins || !app->world || idx < 0 )
        return;
    stack = World_EntityPoolGet(&app->world->entities.obj_stack, idx);
    if( !stack )
        return;

    app_plugin_fill_obj(app, stack, &snap);
    switch( which )
    {
    case APP_PLUGIN_ITEM_SPAWN:
        PluginHost_ObjSpawn(app->plugins, &snap);
        break;
    case APP_PLUGIN_ITEM_CHANGE:
        PluginHost_ObjCount(app->plugins, &snap);
        break;
    default:
        PluginHost_ObjDespawn(app->plugins, &snap);
        break;
    }
}

/*
 * Ground-item model for `obj_id` at `count`.
 *
 * A stackable declares up to ten count variants (`count_obj`/`count_co`), each
 * its own objtype with its own model -- one coin, a small pile, a heap. The
 * reference's ObjType.getModel(count) resolves that variant before it looks at
 * any model id, so a dropped 100 coins draws the heap. Building straight from
 * the base objtype drew a single coin at every stack size.
 *
 * NULL when the variant's objtype or model is not resident yet -- the caller
 * leaves the element alone and the async load lands on a later packet.
 */
static struct ToriDraw_Model*
app_obj_stack_build_model(
    struct App* app,
    int obj_id,
    int count)
{
    struct ToriRS_Objtype* obj;
    int model_ids[1];

    assert(app);
    obj = CacheProvider_ObjtypeGet(
        app->provider, ObjModelLoad_RenderObjId(app->provider, obj_id, count));
    if( !obj || obj->inventory_model_id <= 0 )
        return NULL;
    model_ids[0] = obj->inventory_model_id;
    {
        struct AppModelRecolorSpec recolors = {
            .recolors_from = obj->recolors_from,
            .recolors_to = obj->recolors_to,
            .recolor_count = obj->recolor_count,
        };
        return app_world_build_model(
            app, model_ids, 1, &recolors, 128, 128, APP_LIGHT_SCENE, obj->contrast, obj->ambient);
    }
}

/* Re-point a live stack's element at the model its NEW count selects. The
 * variant only changes at the ten count_co thresholds, so this is a no-op for
 * most count edits. Call BEFORE World_ObjStackSetCount: the count still on the
 * entity is what decides whether anything has to change. */
static void
app_obj_stack_refresh_model(
    struct App* app,
    struct World* world,
    int idx,
    int count)
{
    struct WorldEntity_ObjStack* stack;
    struct ToriDraw_Model* model;
    struct ToriDraw_ModelHandle hnd;

    assert(app);
    assert(world);
    assert(idx >= 0);
    stack = World_EntityPoolGet(&world->entities.obj_stack, idx);
    assert(stack);
    if( ObjModelLoad_RenderObjId(app->provider, stack->obj_id, stack->count) ==
        ObjModelLoad_RenderObjId(app->provider, stack->obj_id, count) )
        return;
    if( stack->element_id < 0 || !ToriDraw_SceneElementIsLive(app->scene, stack->element_id) )
        return;
    model = app_obj_stack_build_model(app, stack->obj_id, count);
    if( !model )
        return;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;
    /* Disposes the model the element was holding. */
    ToriDraw_SceneElementSetModel(app->scene, stack->element_id, hnd);
    app_sync_textures(app);
}

/*
 * Ground item stacks (zone OBJ_* packets). The objtype + its inventory
 * model must already be cached (the packet task awaits the loads).
 *
 * EVERY call adds one row, even when the tile already holds a row of the same
 * obj. That is the reference's rule: OBJ_ADD always pushes a new ClientObj
 * onto the tile's list (Client-TS Client.ts OBJ_ADD, `objStacks[..].push`),
 * and the server says the same thing from its side -- a non-stackable obj,
 * or any private drop, gets its own ground slot and its own OBJ_ADD, while a
 * public stackable landing on its twin is announced as OBJ_COUNT old -> new
 * (torirs_server_world.c world_obj_add), never as a second OBJ_ADD. So an
 * OBJ_ADD for an id the tile already shows IS a second item.
 *
 * This used to look the id up first and overwrite that row's count. Two logs
 * dropped on one tile then read as one log on the client while the server
 * held two; picking one up sent OBJ_DEL, the client removed its only row,
 * and the second log was invisible and unclickable for as long as it lay
 * there (seam pass matthew-mbp-m4-b52-seam1 (a); raid seam
 * client_ground_obj_merge). It also lost a count: a second private coin drop
 * overwrote the first pile's count instead of standing beside it.
 */
int
App_WorldObjStackAdd(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id,
    int count)
{
    struct ToriRS_Objtype* obj;
    struct ToriDraw_Model* model;
    int world_x = scene_x * 128 + 64;
    int world_z = scene_z * 128 + 64;
    int world_y;
    int element_id;
    struct World* world;

    assert(app);
    /* Zone mutations act on whichever view the packet cursor addresses — the
     * root scene or a boat — never on app->world directly. See app.h
     * `active_world`. */
    world = App_ActiveWorldview(app)->world;

    /* The BASE objtype carries the name and the ground ops the minimenu reads;
     * the model comes from whichever count variant `count` selects. */
    obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
    /*
     * Not resident yet -- the objtype, its count variant, the inventory model
     * or a texture it wears: the stack exists NOW without a scene element and
     * a placeholder lands the model when it arrives (app_placeholder.c). This
     * used to be the packet handler's own wait, on the FIFO, one round trip
     * per first-seen drop with every packet and the frame behind it.
     */
    if( !obj || ObjModelLoad_NeedsWork(app->provider, obj_id, count) )
    {
        static const char none[5][32] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };
        int idx;
        if( obj )
        {
            char actions32[5][32];
            for( int a = 0; a < 5; a++ )
                snprintf(actions32[a], sizeof(actions32[a]), "%s", obj->ground_actions[a]);
            idx = World_ObjStackAdd(
                world, -1, scene_x, scene_z, level, obj_id, count, obj->name, actions32);
        }
        else
            idx = World_ObjStackAdd(world, -1, scene_x, scene_z, level, obj_id, count, "", none);
        if( idx < 0 )
            return -1;
        app_placeholder_obj_stack(app, scene_x, scene_z, level, obj_id, count);
        app_plugin_obj_notify(app, idx, APP_PLUGIN_ITEM_SPAWN);
        app_ground_items_mark(app, world, scene_x, scene_z, level);
        app->need_redraw = 1;
        return idx;
    }
    model = app_obj_stack_build_model(app, obj_id, count);
    if( !model )
        return -1;

    /* LocType.raiseobject: world Y is negative-up, so subtracting raise lifts
     * the stack onto the table (Client-TS objs.y - objs.height). */
    /* The heightmap of the world the stack lives in: app_world_height reads
     * the ROOT, and a deck stack's tile is deck-local (seam18 C). */
    world_y = World_HeightAt(world, world_x, world_z, level) -
              World_ObjRaiseGet(world, scene_x, scene_z, level);
    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_OBJSTACK, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return -1;

    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "objstack: obj=%d tile=%d,%d,%d element=%d\n",
            obj_id,
            scene_x,
            scene_z,
            level,
            element_id);
    app_sync_textures(app);
    app->need_redraw = 1;
    {
        /* ToriRS actions are [5][64]; the entity facet stores [5][32] —
         * repack at the matching stride (same gotcha as the scenery path). */
        char actions32[5][32];
        for( int a = 0; a < 5; a++ )
            snprintf(actions32[a], sizeof(actions32[a]), "%s", obj->ground_actions[a]);
        int const idx = World_ObjStackAdd(
            world, element_id, scene_x, scene_z, level, obj_id, count, obj->name, actions32);
        app_plugin_obj_notify(app, idx, APP_PLUGIN_ITEM_SPAWN);
        app_ground_items_mark(app, world, scene_x, scene_z, level);
        return idx;
    }
}

static int
app_obj_stack_land_one(
    struct App* app,
    struct World* world,
    int idx)
{
    struct WorldEntity_ObjStack* stack;
    struct ToriRS_Objtype* obj;
    struct ToriDraw_Model* model;
    int world_x, world_z, world_y;
    int element_id;

    assert(app);
    assert(world);
    assert(idx >= 0);
    stack = World_EntityPoolGet(&world->entities.obj_stack, idx);
    assert(stack);
    assert(stack->element_id < 0);
    obj = CacheProvider_ObjtypeGet(app->provider, stack->obj_id);
    if( !obj || ObjModelLoad_NeedsWork(app->provider, stack->obj_id, stack->count) )
        return 0;
    model = app_obj_stack_build_model(app, stack->obj_id, stack->count);
    if( !model )
        return 0;
    world_x = stack->grid_position.x * 128 + 64;
    world_z = stack->grid_position.z * 128 + 64;
    /* LocType.raiseobject, as in App_WorldObjStackAdd. */
    world_y = World_HeightAt(world, world_x, world_z, stack->grid_position.level) -
              World_ObjRaiseGet(
                  world, stack->grid_position.x, stack->grid_position.z, stack->grid_position.level);
    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_OBJSTACK, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return 0;
    World_ObjStackSetElement(world, idx, element_id);
    {
        char actions32[5][32];
        for( int a = 0; a < 5; a++ )
            snprintf(actions32[a], sizeof(actions32[a]), "%s", obj->ground_actions[a]);
        World_ObjStackSetMenu(world, idx, obj->name, actions32);
    }
    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "objstack: obj=%d tile=%d,%d,%d element=%d (landed)\n",
            stack->obj_id,
            stack->grid_position.x,
            stack->grid_position.z,
            stack->grid_position.level,
            element_id);
    app_sync_textures(app);
    app_plugin_obj_notify(app, idx, APP_PLUGIN_ITEM_CHANGE);
    app_ground_items_mark(
        app, world, stack->grid_position.x, stack->grid_position.z, stack->grid_position.level);
    app->need_redraw = 1;
    return 1;
}

/*
 * Land `idx`, then every other row of the same obj on the same tile that is
 * still waiting for its model.
 *
 * Two identical items on one tile are two rows (App_WorldObjStackAdd), and
 * when both arrive before the objtype is resident each queues a placeholder.
 * The placeholder finds its stack by (tile, obj id) -- World_ObjStackFind,
 * the OLDEST row -- so the second placeholder finds the first row, sees it
 * already landed, and stops: the second row would stay without a scene
 * element (nothing drawn, nothing to click) until the next world load swept
 * it. Landing the siblings here, while the first placeholder holds the
 * resident objtype, closes that for every row whose count selects a resident
 * model. A sibling whose count variant is still loading is left to the sweep
 * (app_placeholder_obj_stacks_sweep); see the open issue in the raid seam
 * notes (app_placeholder.c should look up the first ELEMENT-LESS row).
 */
int
app_obj_stack_land(
    struct App* app,
    struct World* world,
    int idx)
{
    struct World_EntityPool* pool;
    struct WorldEntity_ObjStack const* landed;
    int scene_x, scene_z, level, obj_id;

    if( !app_obj_stack_land_one(app, world, idx) )
        return 0;
    pool = &world->entities.obj_stack;
    landed = World_EntityPoolGet(pool, idx);
    assert(landed);
    scene_x = landed->grid_position.x;
    scene_z = landed->grid_position.z;
    level = landed->grid_position.level;
    obj_id = landed->obj_id;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack const* sibling = World_EntityPoolGet(pool, i);
        if( i == idx || !sibling || sibling->element_id >= 0 || sibling->obj_id != obj_id ||
            sibling->grid_position.x != scene_x || sibling->grid_position.z != scene_z ||
            sibling->grid_position.level != level )
            continue;
        app_obj_stack_land_one(app, world, i);
    }
    return 1;
}

void
App_WorldObjStackSetOwnership(
    struct App* app,
    int idx,
    int public_ticks,
    int despawn_ticks,
    int owner,
    int never_becomes_public)
{
    struct World* world;
    int const now = app->host.client_clock;

    assert(app);
    assert(idx >= 0);
    world = App_ActiveWorldview(app)->world;
    World_ObjStackSetOwnership(
        world,
        idx,
        public_ticks > 0 ? now + public_ticks * RS_CS2_HOST_CLOCKS_PER_TICK : -1,
        despawn_ticks > 0 ? now + despawn_ticks * RS_CS2_HOST_CLOCKS_PER_TICK : -1,
        owner,
        never_becomes_public);
}

void
App_WorldRebuildShift(
    struct App* app,
    int base_dx,
    int base_dz)
{
    struct World* world;
    struct World_EntityPool* pool;

    assert(app);
    world = app->world;
    if( !world )
        return;

    World_ShiftEntities(world, base_dx, base_dz);
    World_ClearProjectilesAndSpotanims(world);
    /* Every pile is on a different tile now, and some fell off the scene
     * entirely -- so every ground-items overlay has to be rebuilt against the
     * new origin, and the ones with nothing left under them destroyed. */
    RS_GroundItemsDirty_MarkAll(&app->ground_items_dirty);
    RS_GroundItemsDirty_Clear(&app->ground_items_dirty);
    /* Plugin objects are anchored to ABSOLUTE tiles, which the shift does not
     * move -- so they are torn down here and re-placed against the new origin
     * once the scene is up (app_plugin_objects_rebuild, from the world-loaded
     * seam). Shifting them instead would be the wrong operation: an object
     * whose tile is off the new scene has to stop drawing, not slide. */
    World_PluginObjectClear(world);

    /* Obj stacks: Client-TS shifts the groundObj grid and nulls entries that
     * fall off it; here the surviving stacks' elements also need their world
     * position re-derived from the new scene's heightmap. Deletion releases
     * the pool node, so grab next first. */
    pool = &world->entities.obj_stack;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; )
    {
        int next = World_EntityPoolNext(pool, i);
        struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, i);
        if( stack )
        {
            int scene_x = stack->grid_position.x;
            int scene_z = stack->grid_position.z;
            if( scene_x < 0 || scene_z < 0 || scene_x >= world->_scene_size ||
                scene_z >= world->_scene_size )
            {
                /* Off the new scene: the client stops tracking it, and a
                 * plugin drawing against it has to hear so. */
                app_plugin_obj_notify(app, i, APP_PLUGIN_ITEM_DESPAWN);
                World_ObjStackDel(world, i);
            }
            else if( stack->element_id >= 0 )
            {
                int world_x = scene_x * 128 + 64;
                int world_z = scene_z * 128 + 64;
                int level = stack->grid_position.level;
                int world_y = app_world_height(app, world_x, world_z, level) -
                              World_ObjRaiseGet(world, scene_x, scene_z, level);
                ToriDraw_SceneElementSetPosition(
                    app->scene, stack->element_id, world_x, world_y, world_z, 0);
            }
        }
        i = next;
    }

    /* Destination flag (reference minimapFlagX -= dx). */
    MinimapView_RebaseFlag(&app->minimap, base_dx, base_dz, world->_scene_size);

    /* Camera position + orbit focus (deob field3239/field161/field1545/field73
     * -= dx<<7). Fine coords move with the scene base. */
    if( base_dx != 0 || base_dz != 0 )
    {
        /* Every attached view moves with the scene base. */
        for( int view = 0; view < app->view_split.view_count; view++ )
        {
            app->views[view].world_camera_pos.x -= base_dx * 128;
            app->views[view].world_camera_pos.z -= base_dz * 128;
            app->views[view].orbit.anchor_x -= base_dx * 128;
            app->views[view].orbit.anchor_z -= base_dz * 128;
        }
    }

    /* Cutscene camera (deob field706 = false / Client-TS cinemaCam = false). */
    app->cam_script.scripted = 0;
    for( int i = 0; i < 5; i++ )
        app->cam_script.shake[i] = 0;

    /* Minimenu (deob field766 = 0): the rebuild closes an open popup, it does
     * not reconfigure it. Hide, never Reset — Reset also clears font_id, and
     * the id is boot-time chrome state nothing re-derives, so a reset here left
     * every later popup measuring against no font and sized by the character
     * estimate in UIMinimenu_PrepareShow (long rows drew past the border). */
    for( int view = 0; view < app->view_split.view_count; view++ )
        UIMinimenu_Hide(app->views[view].minimenu);

    /* Force a minimap rebake (deob field757 = -1 / Client-TS minimapLevel = -1). */
    app->world_map_level = -1;

    if( torirs_env_net_debug() )
        TORIRS_LOG("rebuild_shift: dx=%d dz=%d\n", base_dx, base_dz);
    /* Projectiles/spotanims/far stacks queued EntityRemoved above — free their
     * DYNAMIC scene elements now so the next frame does not race a full queue. */
    App_WorldDrainEntityRemoved(app);
    app->need_redraw = 1;
}

struct Worldview*
App_ActiveWorldview(struct App* app)
{
    assert(app);
    /* Get() asserts the cursor names a live view — the same failure the deob
     * client throws when a packet addresses a despawned world entity. */
    return WorldviewRegistry_Get(&app->worldviews, app->active_world);
}

int
App_WorldRebuildBegin(
    struct App* app,
    int zone_x,
    int zone_z,
    int force)
{
    assert(app);

    /* deob method3310 checkSame / Client-TS mapBuildCenterZone early-out. See
     * app.h on why an instanced rebuild opts out of it. */
    if( !force && app->world_active && app->world && app->world->load_complete &&
        app->rebuild_zone_x == zone_x && app->rebuild_zone_z == zone_z )
        return 0;

    app->rebuild_zone_x = zone_x;
    app->rebuild_zone_z = zone_z;
    app->world_load_attempted = 1;
    app->world_load_inflight = 1;
    ToriRS_BootTelemetry_Mark("world_load:rebuild");
    app->world_load_server_driven = 1;
    app->need_redraw = 1;
    return 1;
}

void
App_WorldObjStackDel(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id)
{
    int idx;
    struct World* world;
    assert(app);
    world = App_ActiveWorldview(app)->world;
    /* OBJ_DEL names a tile and an id, nothing more, and removes ONE row: the
     * first match in arrival order (Client-TS OBJ_DEL walks the tile's list
     * from its head and unlinks the first `obj.id === type`, then breaks).
     * World_ObjStackFind walks the pool from its head, and the pool appends
     * at its tail, so its first match is that same oldest row. Two identical
     * items on one tile and one pickup leave the other row standing. */
    idx = World_ObjStackFind(world, scene_x, scene_z, level, obj_id);
    if( idx >= 0 )
    {
        /* Ahead of the release, so a plugin's last look at the stack is a
         * whole one -- the same ordering the npc despawn path uses. */
        app_plugin_obj_notify(app, idx, APP_PLUGIN_ITEM_DESPAWN);
        World_ObjStackDel(world, idx);
        app_ground_items_mark(app, world, scene_x, scene_z, level);
        app->need_redraw = 1;
    }
}

void
App_WorldObjStackSetCount(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id,
    int count)
{
    int idx;
    struct World* world;
    assert(app);
    world = App_ActiveWorldview(app)->world;
    /* The first row of this id. The reference matches id AND the packet's old
     * count (Client-TS OBJ_COUNT: `obj.id === type && obj.count === ocount`),
     * which picks the right pile when one tile holds two piles of one
     * stackable (two private drops); this signature carries no old count, so
     * on such a tile this can retarget the wrong pile. The server sends
     * OBJ_COUNT only when a public stackable add merges into a pile already
     * on the tile (torirs_server_world.c world_obj_add), so a twin needs two
     * private piles of that stackable there first. */
    idx = World_ObjStackFind(world, scene_x, scene_z, level, obj_id);
    if( idx < 0 )
        return;
    app_obj_stack_refresh_model(app, world, idx, count);
    World_ObjStackSetCount(world, idx, count);
    app_plugin_obj_notify(app, idx, APP_PLUGIN_ITEM_CHANGE);
    app_ground_items_mark(app, world, scene_x, scene_z, level);
    app->need_redraw = 1;
}

void
App_WorldObjStackClearTile(
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    int idx;
    struct World* world;
    assert(app);
    world = App_ActiveWorldview(app)->world;
    /* obj_id -1 = any, so this drains the tile one stack at a time. */
    while( (idx = World_ObjStackFind(world, scene_x, scene_z, level, -1)) >= 0 )
    {
        app_plugin_obj_notify(app, idx, APP_PLUGIN_ITEM_DESPAWN);
        World_ObjStackDel(world, idx);
        app_ground_items_mark(app, world, scene_x, scene_z, level);
        app->need_redraw = 1;
    }
}
