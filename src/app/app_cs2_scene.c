/*
 * Scene facts the CS2 host asks for -- coords, routes, active tile -- and the
 * ground-item overlay dirty set.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_ground_items_mark_every_tile(struct App* app);
static bool
app_ground_items_settings_moved(struct App* app);

/*
 * ---------------------------------------------------------------------------
 * Scripted entity overlays (game/rs_entity_overlay.h).
 * ---------------------------------------------------------------------------
 *
 * The host owns the records and the UITree owns the layers; what is left is
 * the part that needs a camera and a scene, and that is here.
 */

/* LOC_FIND (6803): is a loc of this type on this tile, and on which layer.
 * Also the answer to "is that fishing spot still there" -- the scripts call it
 * before every rebuild of the overlay they put on one. */
int
app_cs2_loc_at_coord(
    void* user,
    int coord,
    int loc_type,
    int* out_layer,
    char* out_name,
    int name_cap)
{
    struct App* app = (struct App*)user;
    struct WorldEntity_Scenery* scenery;
    int x;
    int z;
    int level;

    assert(app);
    assert(out_layer);
    assert(out_name);

    /* "Is there a world yet" is this callback's question, not the conversion's:
     * a clientscript can ask about a coord on the title screen. */
    if( !app->world || !World_CoordToSceneTile(app->world, coord, &x, &z, &level) )
        return 0;
    scenery = World_SceneryFindByLocId(app->world, x, z, level, loc_type);
    if( !scenery )
        return 0;
    *out_layer = World_LocShapeToLayer(scenery->shape);
    snprintf(out_name, (size_t)name_cap, "%s", scenery->info->name);
    return 1;
}

/*
 * ACTIVEPLAYER_GETROUTELENGTH / ACTIVEPLAYER_GETROUTECOORD: one player's
 * queued route.
 *
 * The route is WorldEntityFacet_Pathing, which is the reference's
 * `ClientPlayer::m_routeLength` + its two `array<int,10>`s tile for tile --
 * index 0 is the newest entry, so it is the tile the server last put the
 * player on and the one that runs ahead of the rendered position while they
 * walk. The entries are scene-local, so the base tile makes them absolute.
 *
 * The LEVEL is the local player's, not the subject's: the reference builds the
 * coord with `client->m_plane` (the plane the scene is being rendered at) and
 * the two are the same number for every player the client can see.
 */
int
app_cs2_player_route(
    void* user,
    int player_uid,
    int index,
    int* out_coord)
{
    struct App* app = (struct App*)user;
    struct WorldEntity_Player* player;
    struct WorldEntity_Player* self;
    int level;

    assert(app);
    assert(out_coord);

    if( !app->world )
        return -1;
    player = World_PlayerGetByServerPid(app->world, player_uid);
    if( !player )
        return -1;

    self = World_PlayerGetByServerPid(app->world, app->world->local_pid);
    level = self ? (self->grid_position.level & 3) : (player->grid_position.level & 3);

    if( index >= 0 && index < (int)player->pathing.route_length )
        *out_coord = RS_CLIENTOP_COORD(
            level,
            app->world->_base_tile_x + player->pathing.route_x[index],
            app->world->_base_tile_z + player->pathing.route_z[index]);
    return (int)player->pathing.route_length;
}

/*
 * The reference's `ScriptRunner::SetActivePlayer` and `SetActiveTile`, which
 * every trigger dispatch calls before firing the script.
 *
 * A trigger script takes no arguments, so the context IS its argument list:
 * 5203 reads the active player's route through ACTIVEPLAYER_GETROUTELENGTH /
 * ACTIVEPLAYER_GETROUTECOORD and compares ACTIVEPLAYER_GETUID with
 * LOCALPLAYER_GETUID; 5197 and 5209 read the active tile through `_6950`.
 * Both registers are the persistent kind -- the reference's are two fields on
 * its ScriptRunner and are left set after the script returns -- which is safe
 * because every script that reads one is fired right after it is written, and
 * the scripts that go looking for their own subject (clientscript 5350 calls
 * MINIMENU_FINDPLAYER first) overwrite it before reading.
 */
void
app_cs2_set_active_player(
    struct App* app,
    int pid)
{
    struct RS_ClientOpContext ctx;
    struct WorldEntity_Player* player;

    assert(app);

    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = RS_CLIENTOP_PLAYER;
    ctx.script_id = -1;
    ctx.uid = pid;
    ctx.type = -1;
    ctx.count = -1;
    ctx.layer = -1;
    ctx.coord = -1;

    player = app->world ? World_PlayerGetByServerPid(app->world, pid) : NULL;
    if( player )
    {
        ctx.coord = RS_CLIENTOP_COORD(
            player->grid_position.level,
            app->world->_base_tile_x + player->grid_position.x,
            app->world->_base_tile_z + player->grid_position.z);
        snprintf(ctx.name, sizeof(ctx.name), "%s", player->name);
    }
    RS_ClientOpActiveSet(&app->host.clientop, RS_CLIENTOP_PLAYER, &ctx);
}

void
app_cs2_set_active_tile(
    struct App* app,
    int coord)
{
    struct RS_ClientOpContext ctx;

    assert(app);
    /* A trigger is fired ABOUT a tile. No caller has one to give when the
     * answer would be "nowhere", and `_6950` already has a standing answer for
     * that case (the mouseover fallback in rs_cs2_host.c). */
    assert(coord >= 0);

    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = RS_CLIENTOP_TILE;
    ctx.script_id = -1;
    ctx.uid = -1;
    ctx.type = -1;
    ctx.count = -1;
    ctx.layer = -1;
    ctx.coord = coord;
    RS_ClientOpActiveSet(&app->host.clientop, RS_CLIENTOP_TILE, &ctx);
}

/*
 * What trigger_49 fires on: the local player's ROUTE, folded to one int.
 *
 * The length alone is not the edge -- a step consumed and a step added in the
 * same cycle leaves it unchanged while the true tile moves -- and the newest
 * tile alone is not either, since arriving empties the queue without moving
 * it. The reference watches both halves in its two dispatch sites (the packet
 * that rewrites the route, and the mover that consumes a step), so this
 * carries both. -1 when there is no local player.
 */
int
app_cs2_local_route_signature(struct App* app)
{
    struct WorldEntity_Player* self;

    assert(app);

    if( !app->world || !app->world->load_complete )
        return -1;
    self = World_PlayerGetByServerPid(app->world, app->world->local_pid);
    if( !self )
        return -1;
    return ((int)self->pathing.route_length << 28) | ((int)self->pathing.route_x[0] << 14) |
           (int)self->pathing.route_z[0];
}

/* COORD_INSCENE (6951). */
int
app_cs2_coord_in_scene(
    void* user,
    int coord)
{
    struct App* app = (struct App*)user;
    int x;
    int z;
    int level;

    assert(app);
    if( !app->world )
        return 0;
    return World_CoordToSceneTile(app->world, coord, &x, &z, &level) ? 1 : 0;
}

/*
 * OBJSTACK_COUNT / OBJSTACK_ID / OBJSTACK_QUANTITY, and OBJ_FIND's lookup:
 * the ground-item pile on one absolute coord.
 *
 * `Client::GetObjectsOnTile` in the reference, which answers an EMPTY pile for
 * a coord outside the build area rather than failing -- so a tile that has
 * scrolled off the scene reads as "nothing there", and the overlay script
 * destroys its overlay instead of drawing a stale row.
 *
 * The walk is the same one the right-click builder does (add_objs_on_tile in
 * rs_minimenu_world.c): this client keys a stack by (tile, obj id), where the
 * reference keeps one entry per add, so a pile of three different items is
 * three entries here and the overlay lists three rows -- which is what the
 * script's own run-merge produces from the reference's list anyway.
 */
int
app_cs2_objs_on_coord(
    void* user,
    int coord,
    int index,
    struct RS_CS2GroundObj* out)
{
    struct App* app = (struct App*)user;
    struct WorldEntity_ObjStack const* stack = NULL;
    int tile_x;
    int tile_z;
    int level;
    int count;

    assert(app);
    assert(out);

    /* "Is there a world yet" is this callback's question, not the walk's: a
     * clientscript can ask about a coord on the title screen. */
    if( !app->world || !World_CoordToSceneTile(app->world, coord, &tile_x, &tile_z, &level) )
        return 0;

    count = World_ObjStackCountAt(app->world, tile_x, tile_z, level, index, &stack);
    if( stack )
    {
        out->obj_id = stack->obj_id;
        out->count = stack->count;
        out->public_clock = stack->public_clock;
        out->despawn_clock = stack->despawn_clock;
        out->owner = stack->owner;
        out->never_becomes_public = stack->never_becomes_public;
    }
    return count;
}

/*
 * "The pile on this tile changed" -- queue the tile for the ground-items
 * overlay rebuild that runs once per logic tick.
 *
 * Scene-local in, absolute out, because the coord is what the script reads and
 * `_6950` answers absolutes. A view that is NOT the root scene is skipped: a
 * boat deck has its own tile space, and an absolute coord built from its base
 * would name a tile somewhere else entirely.
 */
void
app_ground_items_mark(
    struct App* app,
    struct World const* world,
    int scene_x,
    int scene_z,
    int level)
{
    int coord;

    assert(app);
    assert(world);
    if( world != app->world )
        return;
    coord =
        RS_CLIENTOP_COORD(level & 3, world->_base_tile_x + scene_x, world->_base_tile_z + scene_z);
    RS_GroundItemsDirty_Mark(&app->ground_items_dirty, coord);
}

/* Every tile in the scene that currently holds a pile, appended to the dirty
 * list. The overflow path, and the one a rebuild shift takes. */
static void
app_ground_items_mark_every_tile(struct App* app)
{
    struct World_EntityPool* pool;

    assert(app);
    assert(app->world);
    pool = &app->world->entities.obj_stack;
    for( int oi = World_EntityPoolHead(pool); oi != WORLD_ENTITY_NIL;
         oi = World_EntityPoolNext(pool, oi) )
    {
        struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, oi);
        if( !stack || stack->element_id < 0 )
            continue;
        /* Straight into the list rather than through the mark above, which
         * would see refresh_all set and decline every one of them. */
        int const coord = RS_CLIENTOP_COORD(
            stack->grid_position.level & 3,
            app->world->_base_tile_x + stack->grid_position.x,
            app->world->_base_tile_z + stack->grid_position.z);
        /* The list is a scratchpad here, not a budget: drain what fits and
         * come back next tick for the rest. A scene cannot gain piles faster
         * than the list holds without the burst that put them there having
         * already asked for the whole scene again. */
        if( !RS_GroundItemsDirty_Append(&app->ground_items_dirty, coord) )
            break;
    }
}

/*
 * Have the ground-items settings moved?
 *
 * Watched as VARPS, because a varp is what a change is visible on: the two
 * carriers hold every one of the overlay's own toggles between them, and the
 * overlay script's `cc_setonvartransmit` list covers the colour and threshold
 * varps but not these. Without this, switching the row off in All Settings
 * left every overlay on screen exactly as it was until its tile changed.
 *
 * The varp ids are resolved lazily: the varbit table arrives with the cache,
 * which is after RS_CS2Host_Init has read the revconfig.
 */
static bool
app_ground_items_settings_moved(struct App* app)
{
    static int const WATCHED = 2;
    bool moved = false;

    assert(app);
    if( !app->host.varps )
        return false;
    for( int i = 0; i < WATCHED; i++ )
    {
        int const varbit = i == 0 ? app->host.varbit_ground_items_enabled
                                  : app->host.varbit_ground_items_modifier_key;
        int value;
        if( varbit <= 0 )
            continue;
        if( app->ground_items_settings_varp[i] < 0 )
        {
            app->ground_items_settings_varp[i] = VarPManager_VarbitBaseVar(app->host.varps, varbit);
            if( app->ground_items_settings_varp[i] < 0 )
                continue;
            /* Seed rather than fire: the value a carrier comes up with is not
             * a change, and firing here would rebuild every overlay on the
             * first tick after the varbit table lands. */
            app->ground_items_settings_seen[i] =
                VarPManager_GetVarp(app->host.varps, app->ground_items_settings_varp[i]);
            continue;
        }
        value = VarPManager_GetVarp(app->host.varps, app->ground_items_settings_varp[i]);
        if( value == app->ground_items_settings_seen[i] )
            continue;
        app->ground_items_settings_seen[i] = value;
        moved = true;
    }
    return moved;
}

/*
 * The ground-items overlay driver: one clientscript per tile whose pile moved.
 *
 * The script (7226 in this cache) reads its tile from `_6950` and either
 * rebuilds the coord-anchored overlay listing what is lying there or destroys
 * it, so the client's whole job is to set the active tile and fire -- exactly
 * like the three tile refreshers beside it in app_logic_tick.
 */
void
app_ground_items_tick(struct App* app)
{
    assert(app);
    if( app->host.script_ground_items_overlay <= 0 )
        return;
    if( !app->world || !app->world->load_complete || App_UiLogic(app) != APP_UI_LOGIC_CS2 )
        return;
    if( app_ground_items_settings_moved(app) )
        RS_GroundItemsDirty_MarkAll(&app->ground_items_dirty);
    /* Aux lists 3/4 are the native Ignore/Highlight inputs. A time-window
     * timer notification can be missed; their revisions cannot. */
    for( int i = 0; i < 2; ++i )
    {
        uint64_t revision = LootStore_AuxRevision(&app->loot, 3 + i);
        if( revision != app->ground_items_aux_seen[i] )
        {
            app->ground_items_aux_seen[i] = revision;
            RS_GroundItemsDirty_MarkAll(&app->ground_items_dirty);
        }
    }
    if( RS_GroundItemsDirty_TakeRefreshAll(&app->ground_items_dirty) )
        app_ground_items_mark_every_tile(app);
    if( app->ground_items_dirty.count <= 0 )
        return;
    app_cs2_set_active_player(app, app->world->local_pid);
    for( int i = 0; i < app->ground_items_dirty.count; i++ )
    {
        int const coord = app->ground_items_dirty.coords[i];
        /* fprintf rather than TORIRS_LOG: the interesting runs are the
         * optimized ones, which -DNDEBUG strips every TORIRS_LOG out of --
         * and "the overlay never appeared" reads identically whether the
         * script ran or the tile was never queued. Same choice as
         * TORIRS_WEV_DEBUG. */
        if( getenv("TORIRS_GROUND_ITEMS_DEBUG") )
        {
            struct RS_CS2GroundObj entry;
            /* `enabled` is the overlay script's OWN first gate: it reads
             * ground_items_enabled and, when that is not 1, jumps clean over
             * the whole per-row loop -- so it still creates the coordinate
             * layer and puts nothing in it. Without this number "the overlay
             * ran and built no captions" and "the overlay is switched off"
             * are the same line, and one of them is a bug. -1 when the varbit
             * table has not landed yet. */
            int const enabled_varbit = app->host.varbit_ground_items_enabled;
            int enabled = -1;
            if( app->host.varps && enabled_varbit > 0 &&
                VarPManager_VarbitBaseVar(app->host.varps, enabled_varbit) >= 0 )
                enabled = VarPManager_GetVarbit(app->host.varps, enabled_varbit);
            fprintf(
                stderr,
                "ground_items: tile %d,%d level %d -> %d obj(s), script %d, enabled=%d\n",
                (coord >> 14) & 0x3fff,
                coord & 0x3fff,
                (coord >> 28) & 3,
                app_cs2_objs_on_coord(app, coord, -1, &entry),
                app->host.script_ground_items_overlay,
                enabled);
        }
        app_cs2_set_active_tile(app, coord);
        RS_CS2_RunScript(
            &app->host, &app->runner, app->host.script_ground_items_overlay, NULL, 0, 0, NULL, 0);
    }
    RS_GroundItemsDirty_Clear(&app->ground_items_dirty);
}
