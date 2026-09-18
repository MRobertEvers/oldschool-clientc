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

/* Ground item stacks (zone OBJ_* packets). The objtype + its inventory
 * model must already be cached (the packet task awaits the loads). */
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
    int existing;
    struct World* world;

    assert(app);
    /* Zone mutations act on whichever view the packet cursor addresses — the
     * root scene or a boat — never on app->world directly. See app.h
     * `active_world`. */
    world = App_ActiveWorldview(app)->world;
    existing = World_ObjStackFind(world, scene_x, scene_z, level, obj_id);
    if( existing >= 0 )
    {
        app_obj_stack_refresh_model(app, world, existing, count);
        World_ObjStackSetCount(world, existing, count);
        app_plugin_obj_notify(app, existing, APP_PLUGIN_ITEM_CHANGE);
        app_ground_items_mark(app, world, scene_x, scene_z, level);
        app->need_redraw = 1;
        return existing;
    }

    /* The BASE objtype carries the name and the ground ops the minimenu reads;
     * the model comes from whichever count variant `count` selects. */
    obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
    if( !obj )
        return -1;
    model = app_obj_stack_build_model(app, obj_id, count);
    if( !model )
        return -1;

    /* LocType.raiseobject: world Y is negative-up, so subtracting raise lifts
     * the stack onto the table (Client-TS objs.y - objs.height). */
    world_y = app_world_height(app, world_x, world_z, level) -
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
        app->world_camera_pos.x -= base_dx * 128;
        app->world_camera_pos.z -= base_dz * 128;
        app->orbit.anchor_x -= base_dx * 128;
        app->orbit.anchor_z -= base_dz * 128;
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
    UIMinimenu_Hide(&app->interact.minimenu);

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
