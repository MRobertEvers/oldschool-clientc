/*
 * World-entity views: decks, hulls, the sailing helm, and the actors aboard.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"
#include "game/sailing_paint_order.u.h"

struct WevDeckBox;

/* Private to this unit, declared up front so definition order is free. */
static int
app_sailing_at_helm(struct App* app);
static int
app_sailing_heading_at(
    struct App* app,
    int mouse_x,
    int mouse_y,
    int* heading);
static void
app_sailing_register_arrows(
    struct App* app,
    struct World* world);
static int
app_wev_debug_enabled(void);
static bool
app_wev_ground_below(
    void* userdata,
    const struct SailingPaintSpan* span,
    int x,
    int z,
    int level);
static bool
app_wev_actor_overlaps(
    void* userdata,
    const struct Wev* wev);
static void
app_wev_decide_flatten(struct App* app);
static void
app_wev_apply_placement(
    struct App* app,
    struct WorldEntityFacet_ViewPlacement* placement,
    int element_id,
    int view_id,
    int x,
    int z);
static void
app_wev_register_deck_actors(
    void* userdata,
    struct World* world);
static void
app_wev_evict_view_actors(
    struct App* app,
    int view_id);

/**
 * An aboard actor's position pushed out through its hull into ROOT scene-local
 * fine units — the transform the deob applies before anything main-world reads
 * an aboard actor's position (camera focus, minimap centre, minimap dots:
 * Statics.method8690). Returns 0 (out untouched) when the actor is not in a
 * live, root-parented view — the caller keeps its root-space position.
 */
int
app_wev_actor_root_fine(
    struct App* app,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int* out_fx,
    int* out_fz)
{
    struct Wev* wev;
    struct WevDeckBox box;

    assert(app);
    assert(placement);
    assert(out_fx);
    assert(out_fz);

    if( placement->view_id == WORLDVIEW_ROOT || !Wevs_IsLive(&app->wevs, placement->view_id) ||
        !WorldviewRegistry_IsLive(&app->worldviews, placement->view_id) )
        return 0;
    wev = Wevs_Get(&app->wevs, placement->view_id);
    if( wev->parent_view_id != WORLDVIEW_ROOT )
        return 0;
    app_wev_deck_box(app, wev, app->world, &box);
    Wev_ParentFromDeck(&box, placement->x, placement->z, out_fx, out_fz);
    return 1;
}

static int
app_sailing_at_helm(struct App* app)
{
    int id = app->sailing_at_helm_varbit;
    return id >= 0 && id < app->varps.varbit_count && app->aboard_view != WORLDVIEW_ROOT &&
           Wevs_IsLive(&app->wevs, app->aboard_view) &&
           WorldviewRegistry_IsLive(&app->worldviews, app->aboard_view) &&
           VarPManager_GetVarbit(&app->varps, id) != 0;
}

int
app_sailing_can_steer(struct App* app)
{
    if( app_sailing_at_helm(app) )
        return 1;
    int role = app->sailing_captain_role_varbit;
    if( app->aboard_view == WORLDVIEW_ROOT || !Wevs_IsLive(&app->wevs, app->aboard_view) ||
        !WorldviewRegistry_IsLive(&app->worldviews, app->aboard_view) || role < 0 ||
        role >= app->varps.varbit_count || VarPManager_GetVarbit(&app->varps, role) != 10 )
        return 0;
    for( int slot = 0; slot < 5; ++slot )
    {
        int duty = app->sailing_crew_duty_varbit[slot];
        int roster = app->sailing_crew_roster_varbit[slot];
        if( duty >= 0 && duty < app->varps.varbit_count && roster >= 0 &&
            roster < app->varps.varbit_count &&
            (VarPManager_GetVarbit(&app->varps, duty) == 3 ||
             VarPManager_GetVarbit(&app->varps, duty) == 4) &&
            VarPManager_GetVarbit(&app->varps, roster) > 0 )
            return 1;
    }
    return 0;
}

/* The native selector intersects the pointer ray with the boat's horizontal
 * plane (class108.method3786), so it works over deck geometry and open sea. */
static int
app_sailing_heading_at(
    struct App* app,
    int mouse_x,
    int mouse_y,
    int* heading)
{
    assert(heading);
    if( !app_sailing_can_steer(app) || !app->world_view_valid )
        return 0;
    struct Wev* vessel = Wevs_Get(&app->wevs, app->aboard_view);
    if( vessel->parent_view_id != WORLDVIEW_ROOT )
        return 0;
    double x, z;
    if( !ToriRS_WorldUnprojectPlane(
            &app->world_camera,
            &app->world_camera_pos,
            app->world_emit_desc.x,
            app->world_emit_desc.y,
            app->world_emit_desc.w,
            app->world_emit_desc.h,
            mouse_x,
            mouse_y,
            vessel->y,
            &x,
            &z) )
        return 0;
    x -= vessel->x - app->world->_base_tile_x * 128;
    z -= vessel->z - app->world->_base_tile_z * 128;
    if( fabs(x) < 0.001 && fabs(z) < 0.001 )
        return 0;
    *heading = SailingNavigation_Heading(x, z);
    return 1;
}

void
app_sailing_menu_context(
    struct App* app,
    struct RS_MinimenuBuildCtx* ctx,
    int mouse_x,
    int mouse_y)
{
    ctx->sailing_navigating = app_sailing_can_steer(app) != 0;
    /* With crew at the helm the captain is free to walk and work on deck.
     * Only open-water/root picks become bearings; a held player helm keeps
     * the native all-directions selector. */
    if( ctx->sailing_navigating && !app_sailing_at_helm(app) && ctx->world_pickset )
        for( int i = 0; i < ctx->world_pickset->count; ++i )
            if( ctx->world_pickset->items[i].type == WORLD_PICK_TERRAIN &&
                ctx->world_pickset->items[i].view_id == app->aboard_view )
                ctx->sailing_navigating = false;
    ctx->sailing_heading_valid =
        ctx->sailing_navigating &&
        app_sailing_heading_at(app, mouse_x, mouse_y, &ctx->sailing_heading);
}

int
app_sailing_send_heading(
    struct App* app,
    int heading)
{
    if( !app_sailing_can_steer(app) || !app->net || app->net->state != TORIRS_NET_GAME )
        return 0;
    APP_NET_SEND(
        app,
        net_out_set_heading(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), heading));
    app->sailing_selected_heading = heading;
    app->sailing_selected_until = app->logic_cycle + 30;
    app->need_redraw = 1;
    return 1;
}

/* The two native defaults meshes render through the world painter, including
 * perspective, terrain occlusion, and GPU depth. They have no interaction row.
 * Statics.method8694 places each four or more tiles along its chosen bearing. */
static void
app_sailing_register_arrows(
    struct App* app,
    struct World* world)
{
    if( world != app->world || !app_sailing_can_steer(app) )
        return;
    struct Wev* vessel = Wevs_Get(&app->wevs, app->aboard_view);
    if( vessel->parent_view_id != WORLDVIEW_ROOT )
        return;
    int hover = -1;
    if( app->world_mouse_in_viewport && !app->pointer_absent && !app->interact.minimenu.visible &&
        !strcmp(app->host.clientop.mouseover_op, "Set heading") )
        app_sailing_heading_at(app, app->world_mouse_x, app->world_mouse_y, &hover);
    int selected =
        app->logic_cycle < app->sailing_selected_until ? app->sailing_selected_heading : -1;
    int scale = app->world_camera.projection_mode == TORIDRAW_PROJECTION_MODE_FOV
                    ? toridraw_projection_scale_from_fov(app->world_camera.fov_rpi2048)
                    : app->world_camera.projection_scale;
    if( scale <= 0 )
        scale = TORIDRAW_PROJECTION_SCALE_DEFAULT;
    int distance = app->world_emit_desc.h > 0
                       ? (int)(1400.0 - scale * 4.0 * 334.0 / app->world_emit_desc.h)
                       : 512;
    if( distance < 512 )
        distance = 512;
    for( int i = 0; i < 2; ++i )
    {
        int heading = i == 0 ? hover : selected;
        if( heading < 0 || (i == 0 && heading == selected) )
            continue;
        int model_id = app->sailing_arrow_model[i];
        if( model_id < 0 )
            continue;
        if( !CacheProvider_ModelHas(app->provider, model_id) )
        {
            if( !app->sailing_arrow_loading[i] )
            {
                ToriRS_TaskQueue_Add(
                    app->runner.queue, CreateTask_ModelLoad(app->provider, model_id));
                app->sailing_arrow_loading[i] = 1;
            }
            continue;
        }
        int element = app->sailing_arrow_element[i];
        if( element < 0 )
        {
            struct ToriRS_Model* source = CacheProvider_ModelGet(app->provider, model_id);
            assert(source);
            struct ToriDraw_Model* model = ToriDraw_ModelFromToriRS(source);
            assert(model);
            struct ToriDraw_ModelHandle handle = { .kind = TORIDRAWMK_MODEL };
            handle.u.model.model = model;
            ToriDraw_ModelSetBoundsCylinder(model);
            ToriDraw_LightModelScene(handle, 0, 0);
            element = ToriDraw_SceneElementAddPool(app->scene, TORIDRAW_SCENE_POOL_DYNAMIC);
            assert(element >= 0);
            ToriDraw_SceneElementSetModel(app->scene, element, handle);
            app->sailing_arrow_element[i] = element;
        }
        int angle = heading * 128;
        int x = vessel->x - world->_base_tile_x * 128 -
                (int)((int64_t)ToriDraw_Sin(angle) * distance / 65536);
        int z = vessel->z - world->_base_tile_z * 128 -
                (int)((int64_t)ToriDraw_Cos(angle) * distance / 65536);
        int gx = x >> 7, gz = z >> 7;
        if( gx < 0 || gz < 0 || gx >= world->_scene_size || gz >= world->_scene_size )
            continue;
        ToriDraw_SceneElementSetPosition(app->scene, element, x, vessel->y - 8, z, angle);
        painter_add_normal_scenery(
            world->painter,
            gx,
            gz,
            World_LocPaintLevel(world, gx, gz, vessel->parent_level),
            element,
            1,
            1,
            0);
    }
}

/*
 * World_ActorRootFrameFn: the facing math's cross-frame answer — an aboard
 * actor's position pushed out through the hull (app_wev_actor_root_fine) plus
 * the frame's yaw offset, the hull's live angle: a homed actor's element yaw
 * is deck-frame and the descent adds the hull's yaw at draw, so a direction
 * computed in the root must have it taken back out.
 */
int
app_wev_actor_root_frame(
    void* userdata,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int* io_fine_x,
    int* io_fine_z,
    int* out_frame_yaw)
{
    struct App* app = (struct App*)userdata;

    assert(app);
    assert(placement);
    assert(io_fine_x);
    assert(io_fine_z);
    assert(out_frame_yaw);

    *out_frame_yaw = 0;
    if( !app_wev_actor_root_fine(app, placement, io_fine_x, io_fine_z) )
        return 0;
    *out_frame_yaw = Wevs_Get(&app->wevs, placement->view_id)->angle & 0x7ff;
    return 1;
}

/* WevHeightFn: terrain under a hull, for the world-entity interpolator.
 *
 * Two things separate this from app_world_height. The sample belongs to the
 * view the boat floats IN (the root for a boat, a carrier's deck later), not
 * to app->world by assumption. And a Wev transform is absolute root-world fine
 * units off the wire, while every World samples its heightmap in scene-local
 * units — feeding the absolute value straight in puts every real boat outside
 * [0,scene_size) and the out-of-scene guard flattens it to y 0. */
int
app_wev_terrain_height(
    void* userdata,
    int view_id,
    int world_x,
    int world_z,
    int level)
{
    struct App* app = (struct App*)userdata;
    struct Worldview* view;

    assert(app);
    view = WorldviewRegistry_Get(&app->worldviews, view_id);
    /* Every live view owns a World (WorldviewRegistry_Register asserts it);
     * whether that World has a heightmap yet is the loaded-or-not question
     * World_HeightAt answers. */
    assert(view->world);
    return World_HeightAt(
        view->world,
        world_x - (view->world->_base_tile_x << 7),
        world_z - (view->world->_base_tile_z << 7),
        level);
}

/* --- SAILING_PLAN C3: world entities in the painter ---------------------- */

/**
 * `TORIRS_WEV_DEBUG=1` — trace every world entity the client is told about and
 * every frame's painter insertion.
 *
 * A boat that does not appear has one of three causes, and they are
 * indistinguishable from a blank patch of water: the spawn never arrived, the
 * spawn arrived but its own view never came live (no REBUILD_WORLDENTITY, so
 * there is no deck to descend into), or the hull's tile falls outside the
 * observer's scene. Each of those prints its own line here.
 *
 * Off by default and read once: this sits inside the per-frame painter
 * registration, where an unconditional write costs whole milliseconds
 * (docs — one stderr write per spawn was the entire "laggy scene" stutter).
 */
static int
app_wev_debug_enabled(void)
{
    static int cached = -1;

    if( cached < 0 )
    {
        char const* v = getenv("TORIRS_WEV_DEBUG");

        cached = (v && v[0] && v[0] != '0') ? 1 : 0;
    }
    return cached;
}

struct Wev*
App_WevSpawn(
    struct App* app,
    int id,
    int config_id,
    int size_x_tiles,
    int size_z_tiles,
    int priority_group,
    int x,
    int z,
    int angle,
    unsigned op_mask)
{
    struct World* world;
    struct WorldBuilder* builder;
    struct WevConfig const* config;

    assert(app);
    if( app_wev_debug_enabled() )
        fprintf(
            stderr,
            "wev: SPAWN id=%d config=%d size=%dx%d prio=%d fine=%d,%d angle=%d "
            "op_mask=0x%x\n",
            id,
            config_id,
            size_x_tiles,
            size_z_tiles,
            priority_group,
            x,
            z,
            angle,
            op_mask);
    /* A spawn needs the full boot substrate: the shared scene the view's
     * builder writes into and the cache provider it reads from. A harness App
     * without them cannot spawn a boat — stop here, loudly. These two are
     * caller contracts and stay asserts. */
    assert(app->scene);
    assert(app->provider);
    /* Everything below is WIRE data: id, config and sizes come straight off
     * a packet, so a value the client cannot honour is a malformed or
     * lagging server, guarded, not asserted — the deob throws here, but our
     * NDEBUG release lane compiles an assert into nothing and then indexes
     * the config table (or the rebuild's 13x13 descriptor grid, one packet
     * later) out of bounds. Refusing returns NULL; the exec skips the
     * spawn. */
    if( id <= WORLDVIEW_ROOT || id >= WORLDVIEW_MAX ||
        !WevConfigTable_Has(&app->wev_configs, config_id) || size_x_tiles <= 0 ||
        size_z_tiles <= 0 || size_x_tiles / 8 > WORLD_INSTANCE_ZONES ||
        size_z_tiles / 8 > WORLD_INSTANCE_ZONES )
    {
        fprintf(
            stderr,
            "wev: SPAWN refused id=%d config=%d size=%dx%d (bad wire values)\n",
            id,
            config_id,
            size_x_tiles,
            size_z_tiles);
        return NULL;
    }
    config = WevConfigTable_Get(&app->wev_configs, config_id);

    /*
     * Recon OQ4: element ids are scene-global — every view's terrain and
     * scenery draws from the root's one element pool, so a deck's terrain
     * spends the root's headroom. Assert it now, at spawn, instead of
     * overflowing in the middle of the deck rebuild.
     *
     * Two separate claims, split so a failure names which one broke, and
     * counting terrain only: one element per tile per level. Scenery is
     * bounded by the deck's loc count, not by its tile count, so the old
     * doubling was arithmetic, not a bound — and with it a maximum view
     * needed 104*104*4*2 = 86,528 of a 65,536 pool, which no scene state
     * whatsoever could satisfy. The undoubled figure is 43,264: one
     * maximum-size view fits and two do not, so the old note about "~15 max-
     * size views" was wrong in the same direction.
     */
    assert(size_x_tiles * size_z_tiles * WORLD_MAP_TERRAIN_LEVELS <= TORIDRAW_SCENE_MAX_ELEMENTS);
    /* ...and the pool must still have that much left, which is the leak
     * check: a session that has been sailing all day should not have drifted
     * upward. A failure HERE means the scene is leaking elements. */
    assert(
        ToriDraw_SceneElementSlotCount(app->scene) +
            size_x_tiles * size_z_tiles * WORLD_MAP_TERRAIN_LEVELS <=
        TORIDRAW_SCENE_MAX_ELEMENTS);

    /* The entity's own simulation pair, same shape as the root's (Phase 4b
     * above): a World over the shared scene plus the builder that keeps it in
     * sync. The registry takes ownership and frees both on despawn. */
    world = World_New();
    assert(world);
    World_SetScene(world, app->scene);
    builder = WorldBuilder_New(world, app->provider, app->scene, &app->varps);
    assert(builder);
    /* One scene, one element namespace, one pool pair per view: the deck's
     * terrain and scenery are allocated in view `id`'s static pool, so its
     * rebuild frees the deck and nothing else, and the root's rebuild sweeps
     * only the root's. Bound before the first build — elements keep the pool
     * they were allocated in. */
    WorldBuilder_SetSceneView(builder, id);

    /* Eager scene allocation (plan C2, deob class100 allocating its heights
     * at construction): the boat's heightmap, collision maps, minimap and its
     * own Painter exist from spawn, so an entity can enter the view before
     * the first deck rebuild lands. Square scene of the larger side; parked
     * at base (0,0) — REBUILD_WORLDENTITY re-runs this reset with the wire's
     * staging base (base = (center - size/16)*8). */
    {
        int scene_size = size_x_tiles > size_z_tiles ? size_x_tiles : size_z_tiles;

        World_ResetScene(world, scene_size / 16, scene_size / 16, scene_size);
    }

    /* Nested entities ride this deck and register into ITS painter, so the boat
     * world carries the same hook the root does (SAILING_PLAN C3). */
    World_SetWorldEntityRegisterFn(world, app_wev_register_pseudo_locs, app);
    /* Actors standing on this deck are owned by the world that carries the
     * boat, so the deck needs both halves of the borrowing arrangement: who to
     * draw each frame, and whose elements its rebuild must not sweep
     * (SAILING_PLAN C5.1). */
    World_SetForeignActorRegisterFn(world, app_wev_register_deck_actors, app);
    World_SetForeignDynamicClaimFn(world, app_wev_claim_deck_actors, app);

    /* The per-view rebuild (REBUILD_WORLDENTITY, task_gameproto_exec.c) —
     * staging the deck map into the off-map rectangle this registers — fills
     * in base_x/base_z. Until then the view's membership box sits at (0,0)
     * with only its size known. */
    WorldviewRegistry_Register(
        &app->worldviews, id, world, builder, 0, 0, size_x_tiles, size_z_tiles, app->active_world);

    return Wevs_Spawn(
        &app->wevs, id, app->active_world, config, config_id, x, z, angle, priority_group, op_mask);
}

void
App_WevDespawn(
    struct App* app,
    int id)
{
    struct Worldview* view;

    assert(app);
    assert(app->scene);
    /* Leaves first: Wevs_Despawn contracts that the departing view hosts no
     * children, and a server despawning a carrier before its nested entities
     * (legal on the wire) must not turn that contract into an abort — or,
     * under NDEBUG, an orphaned child list entry. Children recurse through
     * this same function so their own decks unwind fully. */
    while( Wevs_ViewListCount(&app->wevs, id) > 0 )
        App_WevDespawn(app, Wevs_ViewListAt(&app->wevs, id, 0)->id);
    /* If this view is the tick's zone/rebuild cursor, the cursor is now a
     * dangling address — point it back at the root, exactly what the tick
     * fence would do. */
    if( app->active_world == id )
    {
        app->active_world = WORLDVIEW_ROOT;
        app->active_world_level = 0;
    }
    /* Entity first (asserts its own list is empty — nested entities despawn
     * leaves-first), then its view: Release frees the owned world/builder
     * pair the spawn built. */
    /* Anyone standing on the deck goes back to the root FIRST: the pool clear
     * below frees this view's dynamic elements, and an aboard actor's element
     * is one of them while the root world still holds its id (SAILING_PLAN
     * C5.1). */
    app_wev_evict_view_actors(app, id);

    Wevs_Despawn(&app->wevs, id);

    view = WorldviewRegistry_Get(&app->worldviews, id);
    /* The scene outlives the view. Freeing the World reclaims the deck's
     * entity records, not the SHARED scene's elements the builder placed for
     * them — so the view's two pools are swept here, at the one place a view
     * stops existing. Without it a boat that sails out of range leaves its
     * whole deck in the element pool, and the spawn-time headroom assert is
     * what eventually reports it, a dozen boats too late.
     *
     * Drain first: the departing world's EntityRemoved queue still names
     * DYNAMIC elements whose owner is already gone, and the drain is what
     * hands them to the plugins before they are freed. */
    App_WorldDrainEntityRemovedFor(app, view->world);
    ToriDraw_SceneClearPool(app->scene, TORIDRAW_SCENE_POOL_STATIC_VIEW(id));
    ToriDraw_SceneClearPool(app->scene, TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(id));

    WorldviewRegistry_Release(&app->worldviews, id);
    app->need_redraw = 1;
}
/*
 * World ENTITIES -- the sailing hulls that carry a world view around the map,
 * and everything the client has to do per frame to draw one inside another.
 *
 * A unity fragment of app.c, not a module. It is textually part of app.c's
 * translation unit and included at exactly the point it was cut from, so every
 * helper here stays static and every App field it reads stays where it was.
 * The split is for the reader: a boat is a world inside a world, and the
 * consequences of that are all here in one place instead of spread through a
 * file about everything else.
 *
 * What it does, in the order a frame does it: route every actor to the view
 * whose staging rectangle holds it, register each hull in its parent's painter
 * as a pseudo-loc so the ordering against real locs comes out right, point each
 * deck's own camera through the hull's transform, publish the descent
 * transforms the frame emitter composes, and decide which hulls are drawn flat
 * because the budget or the overlap rule says so.
 *
 * The geometry itself is not here. The deck transform and its box are
 * Wev_DeckBoxInParent's and Wev_DeckFromParent's, the view rectangles are the
 * Worldview registry's, and the interpolator is wev.c's. What stays is the
 * part that needs an App: which scene, which painter, which provider, and when.
 *
 * @see src/app.c, src/world/wev.h, docs/SAILING.md
 */

/**
 * World_WorldEntityRegisterFn: every entity floating in `world` goes in as a
 * transient pseudo-loc covering its rotated footprint, flagged
 * PNTR_SCENERY_WORLDENTITY so the drain descends instead of emitting a model.
 * Painter-correct ordering against real locs, actors and projectiles then comes
 * for free.
 */
void
app_wev_register_pseudo_locs(
    void* userdata,
    struct World* world)
{
    struct App* app = (struct App*)userdata;
    int view_id;
    int count;
    int debug = app_wev_debug_enabled();

    assert(app);
    assert(world);
    assert(world->painter);

    view_id = app_worldview_id_of(app, world);
    /* A world that is not a registered view has no entities floating in it -
     * an offline harness world, say. */
    if( view_id < 0 )
        return;

    /* C4: settle this frame's full-detail set before any hull registers.
     * Root only — nested hulls are outside the budget/overlap rules. */
    if( view_id == WORLDVIEW_ROOT )
        app_wev_decide_flatten(app);

    app_sailing_register_arrows(app, world);

    count = Wevs_ViewListCount(&app->wevs, view_id);
    int order[WORLDVIEW_MAX], ordered = 0;
    if( view_id == WORLDVIEW_ROOT )
    {
        static const int groups[] = { -1, 2, 0, 1 };
        for( int pass = 0; pass < 4; ++pass )
            for( int i = 0; i < count; ++i )
            {
                struct Wev* candidate = Wevs_ViewListAt(&app->wevs, view_id, i);
                bool aboard = candidate->id == app->aboard_view;
                if( pass == 0 ? aboard : !aboard && candidate->priority_group == groups[pass] )
                    order[ordered++] = i;
            }
    }
    else
        for( int i = 0; i < count; ++i )
            order[ordered++] = i;
    for( int i = 0; i < ordered; i++ )
    {
        struct Wev* wev = Wevs_ViewListAt(&app->wevs, view_id, order[i]);
        int id;
        int gx;
        int gz;
        int level;
        int fx;
        int fz;
        int fsx;
        int fsz;

        assert(wev);
        id = wev->id;

        /* Its own view must exist before the painter can descend into it. */
        if( !WorldviewRegistry_IsLive(&app->worldviews, id) )
        {
            if( debug )
                fprintf(
                    stderr,
                    "wev: view %d entity %d SKIPPED — its own view is not live\n",
                    view_id,
                    id);
            continue;
        }

        /* Wev transforms are absolute root-world fine units; the painter grid
         * is scene-local tiles. >>7, never /128: truncation toward zero
         * mis-seeds by a whole tile at negative coordinates. */
        gx = (wev->x >> 7) - world->_base_tile_x;
        gz = (wev->z >> 7) - world->_base_tile_z;
        if( gx < 0 || gz < 0 || gx >= world->_scene_size || gz >= world->_scene_size )
        {
            if( debug )
                fprintf(
                    stderr,
                    "wev: view %d entity %d SKIPPED — tile %d,%d outside the "
                    "%d-tile scene based at %d,%d (fine %d,%d)\n",
                    view_id,
                    id,
                    gx,
                    gz,
                    world->_scene_size,
                    world->_base_tile_x,
                    world->_base_tile_z,
                    wev->x,
                    wev->z);
            continue;
        }

        /* The carrier's level in THIS world, off SET_ACTIVE_WORLD — not
         * `config->plane`, which is where the deck sits inside the entity's
         * OWN world. A hull on open water is level 0 here and its deck is
         * still authored at plane 1 over in the staging region; painting it
         * at the config's plane put it above the root's draw mask, which is
         * clamped to the player's roof level, so it was culled every frame. */
        level = WorldviewRegistry_Get(&app->worldviews, id)->parent_level;
        if( level < 0 )
            level = 0;
        if( level >= COLLISION_LEVELS )
            level = COLLISION_LEVELS - 1;
        level = World_LocPaintLevel(world, gx, gz, level);

        /* Native Scene.addDynamic uses a radius-60 pseudo-loc. The actual
         * rotated hull bounds belong only to overlap/navigation decisions. */
        Wev_PainterFootprint(wev, &fx, &fz, &fsx, &fsz);
        fx -= world->_base_tile_x;
        fz -= world->_base_tile_z;
        if( fx < 0 || fz < 0 || fx + fsx > world->_scene_size || fz + fsz > world->_scene_size ||
            !wev->render_visible )
            continue;

        /* model_height 0: the pseudo-loc is never occlusion-tested (the drain
         * descends on it), and a hull has no single merged model to measure. */
        painter_add_world_entity(world->painter, level, fx, fz, id, 0, fsx, fsz);
        if( debug )
            fprintf(
                stderr,
                "wev: view %d entity %d PAINTED at scene tile %d,%d level %d "
                "(parent_level %d, config plane %d) (fine %d,%d "
                "angle %d)\n",
                view_id,
                id,
                gx,
                gz,
                level,
                WorldviewRegistry_Get(&app->worldviews, id)->parent_level,
                wev->config ? wev->config->plane : -1,
                wev->x,
                wev->z,
                wev->angle);
    }
}

/**
 * Bind every live entity's painter to its parent's world-entity table with the
 * camera already carried into that entity's own space, once per entity per
 * frame. Breadth-first over the view tree: a nested entity's camera derives
 * from its carrier's, so a parent resolves first.
 *
 * The inverse of the descent transform. Forward (SAILING.md 5.2) is
 * `root = R(angle) * (deck + T) + pos` with
 * `T = (-size_x*64 - pivot_x, 0, -size_z*64 - pivot_z)`, so
 * `deck = R(-angle) * (root - pos) - T`.
 */
void
app_wev_bind_view_cameras(
    struct App* app,
    int pitch,
    int root_yaw,
    int root_cam_x,
    int root_cam_y,
    int root_cam_z)
{
    struct
    {
        int view_id;
        int cam_x;
        int cam_y;
        int cam_z;
        int yaw;
    } queue[WORLDVIEW_MAX];
    int head = 0;
    int tail = 0;

    assert(app);

    queue[tail].view_id = WORLDVIEW_ROOT;
    queue[tail].cam_x = root_cam_x;
    queue[tail].cam_y = root_cam_y;
    queue[tail].cam_z = root_cam_z;
    queue[tail].yaw = root_yaw;
    tail++;

    while( head < tail )
    {
        int parent_view_id = queue[head].view_id;
        int cam_x = queue[head].cam_x;
        int cam_y = queue[head].cam_y;
        int cam_z = queue[head].cam_z;
        int parent_yaw = queue[head].yaw;
        struct World* parent_world;
        int count;
        head++;

        if( !WorldviewRegistry_IsLive(&app->worldviews, parent_view_id) )
            continue;
        parent_world = WorldviewRegistry_Get(&app->worldviews, parent_view_id)->world;
        assert(parent_world);
        if( !parent_world->painter )
            continue;

        /* Per frame: a despawned entity must not leave a dangling painter. */
        painter_clear_world_entity_views(parent_world->painter);

        count = Wevs_ViewListCount(&app->wevs, parent_view_id);
        for( int i = 0; i < count; i++ )
        {
            struct Wev* wev = Wevs_ViewListAt(&app->wevs, parent_view_id, i);
            int id;
            struct Worldview* view;
            struct WevDeckBox box;
            int dy;
            int deck_x;
            int deck_z;
            int boat_yaw;
            int sx;
            int sz;
            int max_tile;

            assert(wev);
            assert(wev->config);
            id = wev->id;
            if( !WorldviewRegistry_IsLive(&app->worldviews, id) )
                continue;
            view = WorldviewRegistry_Get(&app->worldviews, id);
            assert(view->world);
            if( !view->world->painter )
                continue;

            /* The eye, put through the transform an actor standing on this
             * deck goes through. Height is not in it: the deck is a plane and
             * the camera rides above the hull, not above the deck's own
             * heightmap. */
            Wev_DeckBoxInParent(
                wev,
                view->size_x_tiles,
                view->size_z_tiles,
                parent_world->_base_tile_x,
                parent_world->_base_tile_z,
                &box);
            dy = cam_y - wev->y;
            Wev_DeckFromParent(&box, cam_x, cam_z, &deck_x, &deck_z);

            boat_yaw = (parent_yaw - wev->angle) & 0x7ff;
            painter_set_camera_angles(view->world->painter, pitch, boat_yaw);
            painter_set_level_mask(view->world->painter, 0xF);
            /* The deck is a handful of zones; draw all of it. */
            painter_set_draw_distance(view->world->painter, view->world->_scene_size);

            max_tile = view->world->_scene_size - 1;
            sx = deck_x >> 7;
            sz = deck_z >> 7;
            if( sx < 0 )
                sx = 0;
            if( sx > max_tile )
                sx = max_tile;
            if( sz < 0 )
                sz = 0;
            if( sz > max_tile )
                sz = max_tile;

            painter_set_world_entity_view(
                parent_world->painter, id, view->world->painter, sx, sz, 0);

            /* Nested entities ride this deck; their cameras derive from it. */
            assert(tail < WORLDVIEW_MAX);
            queue[tail].view_id = id;
            queue[tail].cam_x = deck_x;
            queue[tail].cam_y = dy;
            queue[tail].cam_z = deck_z;
            queue[tail].yaw = boat_yaw;
            tail++;
        }
    }
}

/**
 * Publish every live entity's descent transform to the frame emitter, once per
 * frame. The painter's BEGIN_WORLD / END_WORLD markers pick them up and compose
 * them; nothing here needs the tree order, because each entry is expressed
 * purely in its own PARENT's space.
 */
void
app_wev_bind_frame_xforms(
    struct App* app,
    struct ToriRS_Frame* frame)
{
    assert(app);
    assert(frame);

    ToriRS_FrameClearViewXforms(frame);

    for( int parent = 0; parent < WORLDVIEW_MAX; parent++ )
    {
        struct World* parent_world;
        int count;

        if( !WorldviewRegistry_IsLive(&app->worldviews, parent) )
            continue;
        parent_world = WorldviewRegistry_Get(&app->worldviews, parent)->world;
        assert(parent_world);

        count = Wevs_ViewListCount(&app->wevs, parent);
        for( int i = 0; i < count; i++ )
        {
            struct Wev* wev = Wevs_ViewListAt(&app->wevs, parent, i);
            struct Worldview* view;
            struct WevDeckBox box;

            assert(wev);
            assert(wev->config);
            if( !WorldviewRegistry_IsLive(&app->worldviews, wev->id) )
                continue;
            view = WorldviewRegistry_Get(&app->worldviews, wev->id);
            assert(view->world);

            Wev_DeckBoxInParent(
                wev,
                view->size_x_tiles,
                view->size_z_tiles,
                parent_world->_base_tile_x,
                parent_world->_base_tile_z,
                &box);
            ToriRS_FrameSetViewXform(
                frame,
                wev->id,
                view->world,
                box.recenter_x,
                box.recenter_z,
                box.pos_x,
                /* + the bob: the animaya root-bone Y multiplied into the
                 * whole sub-scene (app_wev_advance_bobs), the deob's
                 * class112.method4034 matrix chain reduced to its
                 * translation term. */
                wev->y + wev->bob_y,
                box.pos_z,
                box.angle);
            if( wev->flattened )
            {
                frame->views[wev->id].flatten_scale = 0.01f;
                frame->views[wev->id].flatten_y_offset = -1200;
                frame->views[wev->id].flat_hsl = wev->config->flat_hsl;
            }
        }
    }
}

/* --- SAILING_PLAN C4: flatten (budget / priority / overlap) -------------- */

/* Native actor overlaps use the drawn root position and nearest-16 oriented
 * hull bounds. NPCs opt in only when their resolved type exposes an action. */

static bool
app_wev_ground_below(
    void* userdata,
    const struct SailingPaintSpan* span,
    int x,
    int z,
    int level)
{
    struct App* app = userdata;
    assert(app);
    assert(span);
    struct World* world = WorldviewRegistry_Get(&app->worldviews, span->parent)->world;
    if( !world->heightmap || x < 0 || z < 0 || x + 1 >= world->heightmap->size_x ||
        z + 1 >= world->heightmap->size_z )
        return false;
    /* Negative Y is up. Preserve any tile with a corner above the hull's
     * parent surface: a cliff or raised shore must still occlude the boat. */
    for( int dz = 0; dz < 2; ++dz )
        for( int dx = 0; dx < 2; ++dx )
            if( heightmap_get(world->heightmap, x + dx, z + dz, level) < span->surface_y )
                return false;
    return true;
}

void
app_wev_order_parent_ground(struct App* app)
{
    assert(app);
    struct SailingPaintSpan spans[WORLDVIEW_MAX];
    int count = 0;
    for( int id = 1; id < WORLDVIEW_MAX; ++id )
    {
        if( !Wevs_IsLive(&app->wevs, id) || !WorldviewRegistry_IsLive(&app->worldviews, id) )
            continue;
        struct Wev* wev = Wevs_Get(&app->wevs, id);
        if( !wev->render_visible )
            continue;
        struct Worldview* view = WorldviewRegistry_Get(&app->worldviews, id);
        if( !WorldviewRegistry_IsLive(&app->worldviews, wev->parent_view_id) )
            continue;
        struct World* parent = WorldviewRegistry_Get(&app->worldviews, wev->parent_view_id)->world;
        struct WevDeckBox box;
        app_wev_deck_box(app, wev, parent, &box);
        int x, z, width, height;
        Wev_FootprintTiles(wev, 0, &x, &z, &width, &height);
        x -= parent->_base_tile_x;
        z -= parent->_base_tile_z;
        int max_x = x + width - 1, max_z = z + height - 1;
        for( int corner = 0; corner < 4; ++corner )
        {
            int px, pz;
            Wev_ParentFromDeck(
                &box,
                corner & 1 ? view->size_x_tiles * 128 - 1 : 0,
                corner & 2 ? view->size_z_tiles * 128 - 1 : 0,
                &px,
                &pz);
            px >>= 7;
            pz >>= 7;
            if( px < x )
                x = px;
            if( pz < z )
                z = pz;
            if( px > max_x )
                max_x = px;
            if( pz > max_z )
                max_z = pz;
        }
        spans[count] = (struct SailingPaintSpan){ .view = id,
                                                  .parent = wev->parent_view_id,
                                                  .level = view->parent_level,
                                                  .x = x,
                                                  .z = z,
                                                  .width = max_x - x + 1,
                                                  .height = max_z - z + 1,
                                                  .surface_y = wev->y,
                                                  .flat = wev->flattened };
        Wev_RenderBounds(wev, spans[count].bounds);
        ++count;
    }
    /* Zero boats returns before allocation or command scanning. */
    if( count )
    {
        sailing_paint_order_flat(app->painter_buffer, spans, count);
        sailing_paint_order_ground(app->painter_buffer, spans, count, app_wev_ground_below, app);
    }
}

static bool
app_wev_actor_overlaps(
    void* userdata,
    const struct Wev* wev)
{
    struct App* app = userdata;
    assert(app);
    assert(wev);
    struct World* root = app->world;
    if( !root )
        return false;
    struct World_EntityPool* pool = &root->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
        if( !player || player->element_id < 0 )
            continue;
        int x = (int)player->draw_position.x, z = (int)player->draw_position.z;
        app_wev_actor_root_fine(app, &player->view_placement, &x, &z);
        if( Wev_OverlapsActor(wev, x + root->_base_tile_x * 128, z + root->_base_tile_z * 128, 1) )
            return true;
    }
    pool = &root->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        if( !npc || npc->element_id < 0 || npc->multinpc_hidden )
            continue;
        struct ToriRS_Npctype* type = CacheProvider_NpctypeGet(app->provider, npc->npc_id);
        bool actionable = false;
        if( type )
            for( int op = 0; op < 5; ++op )
                if( type->actions[op] && type->actions[op][0] )
                    actionable = true;
        if( !actionable )
            continue;
        int x = (int)npc->draw_position.x, z = (int)npc->draw_position.z;
        app_wev_actor_root_fine(app, &npc->view_placement, &x, &z);
        if( Wev_OverlapsActor(
                wev,
                x + root->_base_tile_x * 128,
                z + root->_base_tile_z * 128,
                npc->size > 0 ? npc->size : 1) )
            return true;
    }
    return false;
}

static void
app_wev_decide_flatten(struct App* app)
{
    assert(app);
    Wevs_SelectRenderStates(
        &app->wevs,
        app->aboard_view,
        app->host.world_entity_draw_limit,
        app_wev_actor_overlaps,
        app);
    for( int id = 1; id < WORLDVIEW_MAX; ++id )
        if( Wevs_IsLive(&app->wevs, id) && WorldviewRegistry_IsLive(&app->worldviews, id) )
        {
            struct Wev* wev = Wevs_Get(&app->wevs, id);
            WorldviewRegistry_Get(&app->worldviews, id)->world->suppress_dynamic_population =
                wev->flattened || !wev->render_visible;
        }
}

/* --- SAILING_PLAN C5: actors aboard ------------------------------------- */

/**
 * The deck box of one live entity, expressed in `parent_world`'s scene-local
 * fine units. The arithmetic is Wev_DeckBoxInParent's; this is the spelling
 * that looks the view up, which is the only part of it that needs an App.
 */
void
app_wev_deck_box(
    struct App* app,
    struct Wev const* wev,
    struct World const* parent_world,
    struct WevDeckBox* out_box)
{
    struct Worldview const* view;

    assert(app);
    assert(wev);
    assert(wev->config);
    assert(parent_world);
    assert(out_box);
    assert(WorldviewRegistry_IsLive(&app->worldviews, wev->id));

    view = WorldviewRegistry_Get(&app->worldviews, wev->id);
    Wev_DeckBoxInParent(
        wev,
        view->size_x_tiles,
        view->size_z_tiles,
        parent_world->_base_tile_x,
        parent_world->_base_tile_z,
        out_box);
}

/**
 * The plane, inside a world entity's OWN world, that its deck is authored at.
 *
 * This is `WevConfig.plane`, and it is NOT `Worldview.parent_level`: the
 * parent level is the level the hull floats on out in the carrier's world,
 * whereas an actor aboard stands on the deck, over in the staging region the
 * deck was authored in. Both the height sample and the painter registration
 * for a deck actor have to use this one.
 *
 * Sampling level 0 instead is what put a player on the BOTTOM of the hull:
 * config 9's ship is authored with the lower deck at plane 0, the railed main
 * deck at plane 1 and the quarterdeck at plane 2, so a level-0 actor stood
 * below the planking with the ship's own geometry drawn over them.
 */
int
app_wev_deck_level(
    struct App* app,
    int view_id)
{
    struct Wev const* wev;
    int level;

    assert(app);
    assert(Wevs_IsLive(&app->wevs, view_id));

    wev = Wevs_Get(&app->wevs, view_id);
    assert(wev->config);
    level = wev->config->plane;
    if( level < 0 )
        level = 0;
    if( level >= COLLISION_LEVELS )
        level = COLLISION_LEVELS - 1;
    return level;
}

int
App_WevHomeViewForAbsTile(
    struct App* app,
    int abs_tile_x,
    int abs_tile_z,
    int* out_local_x,
    int* out_local_z)
{
    assert(app);
    return WorldviewRegistry_HomeViewForAbsTile(
        &app->worldviews, abs_tile_x, abs_tile_z, out_local_x, out_local_z);
}

/**
 * Move one actor's scene element between view pools when its membership
 * changes, and record the new placement.
 *
 * The element is retagged, never freed and reallocated: its id is what the
 * entity record, the painter's scenery chains and the plugin-facing
 * EntityRemoved queue all hold. Retagging is what makes a boarding leak
 * nothing and strand nothing — the old pool loses a member, the new pool
 * gains one, and the sweep on either side then sees the truth.
 */
static void
app_wev_apply_placement(
    struct App* app,
    struct WorldEntityFacet_ViewPlacement* placement,
    int element_id,
    int view_id,
    int x,
    int z)
{
    assert(app);
    assert(app->scene);
    assert(placement);
    assert(element_id >= 0);
    assert(view_id >= 0);
    assert(view_id < WORLDVIEW_MAX);

    if( placement->view_id != view_id )
    {
        ToriDraw_SceneElementSetPool(
            app->scene, element_id, TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(view_id));
        placement->view_id = view_id;
        app->need_redraw = 1;
    }
    placement->x = x;
    placement->z = z;
}

/**
 * Re-route every actor in the root world to the view whose base rectangle
 * holds it, once per tick, before the registration passes read the answer.
 *
 * Ordered after World_MoversAdvance so the point tested is where the actor is
 * NOW; ordered before World_Cycle so the root's own registration pass already
 * knows to skip whoever just boarded. A tick's lag either way would draw a
 * boarding player twice or not at all for a frame.
 */
void
app_wev_route_actors(struct App* app)
{
    struct World* world;
    struct World_EntityPool* pool;

    assert(app);
    world = app->world;
    if( !world || !app->scene )
        return;

    app->aboard_view = WORLDVIEW_ROOT;

    pool = &world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        int view_id;
        int x;
        int z;

        if( !player || player->element_id < 0 )
            continue;
        if( player->view_placement.home_view != 0 &&
            !WorldviewRegistry_IsLive(&app->worldviews, player->view_placement.home_view) )
            /* Their hull despawned. The stored view-local coordinates mean
             * nothing now; the server always follows a vessel free with an
             * absolute placement (VesselFree disembarks every rider), which
             * re-homes them. Until it lands, park in the root. */
            player->view_placement.home_view = 0;
        if( player->view_placement.home_view != 0 )
        {
            /* Homed by the wire: the executor already rebased this actor's
             * coordinates into the view's own space, so the draw position IS
             * the deck placement — no footprint test, no inverse transform,
             * and no per-tick wobble as the hull glides. */
            view_id = player->view_placement.home_view;
            x = (int)player->draw_position.x;
            z = (int)player->draw_position.z;
        }
        else
        {
            /* NOT aboard. Membership is the deob's STAGING-RECT test alone
             * (field768 recomputed per tick from the wire coordinates) —
             * never the hull's world-space footprint. Footprint capture used
             * to route anyone standing where a hull was PARKED into the
             * boat's view: a villager strolling under a land-spawned hull
             * jumped to the deck template's terrain and height. A rider seen
             * by ANOTHER client (wire coords projected, not staging) now
             * draws at their projected root position instead of aboard —
             * degraded but truthful, until the encoder sends riders' staging
             * coordinates to every observer. */
            view_id = WORLDVIEW_ROOT;
            x = (int)player->draw_position.x;
            z = (int)player->draw_position.z;
        }
        app_wev_apply_placement(app, &player->view_placement, player->element_id, view_id, x, z);
        /* SAILING_PLAN C5.1's "one int": which view the local player is in.
         * Nothing steers off it yet — the camera still follows their ROOT
         * position, which the server keeps projected onto the hull — but the
         * aboard scene-mode flip and the deck-height focus both key off it. */
        if( app->esync.local_pid >= 0 && player->server_pid == app->esync.local_pid )
        {
            app->aboard_view = view_id;
            /* Only on a change. This runs every tick for every player, and
             * "still where they were" is the answer on all but the one tick a
             * boarding capture is actually about. */
            if( app_wev_debug_enabled() && app->dbg_aboard_view != view_id )
            {
                app->dbg_aboard_view = view_id;
                fprintf(
                    stderr,
                    "wev: local player ABOARD view %d at view-local %d,%d "
                    "(root %d,%d)\n",
                    view_id,
                    x,
                    z,
                    (int)player->draw_position.x,
                    (int)player->draw_position.z);
            }
        }
    }

    pool = &world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);

        if( !npc || npc->element_id < 0 )
            continue;
        int view_id = WORLDVIEW_ROOT;
        int x = (int)npc->draw_position.x;
        int z = (int)npc->draw_position.z;
        struct ToriRS_Npctype* type = NULL;
        if( app->sailing_crew_category > 0 && Wevs_ViewListCount(&app->wevs, WORLDVIEW_ROOT) > 0 )
        {
            type = CacheProvider_NpctypeGet(
                app->provider, npc->base_npc_id >= 0 ? npc->base_npc_id : npc->npc_id);
            if( !type )
                type = CacheProvider_NpctypeGet(app->provider, npc->npc_id);
        }
        /* Only native ship crew opt into projected rendering. Ordinary shore
         * NPCs, sea monsters and dock recruits retain their root-world pose.
         * NPC_INFO continues to track the original root coordinates; changing
         * those would corrupt later relative movement and target packets. */
        if( app->sailing_crew_category > 0 && type && type->category == app->sailing_crew_category )
            for( int pass = 0; pass < WORLDVIEW_MAX; ++pass )
            {
                int candidate = pass == 0 ? npc->view_placement.view_id : pass;
                if( candidate <= 0 || !Wevs_IsLive(&app->wevs, candidate) ||
                    !WorldviewRegistry_IsLive(&app->worldviews, candidate) )
                    continue;
                struct Wev* wev = Wevs_Get(&app->wevs, candidate);
                struct Worldview* view = WorldviewRegistry_Get(&app->worldviews, candidate);
                if( wev->parent_view_id != WORLDVIEW_ROOT || !view->world->load_complete )
                    continue;
                struct WevDeckBox box;
                int dx, dz;
                app_wev_deck_box(app, wev, world, &box);
                Wev_DeckFromWireTarget(wev, &box, x, z, &dx, &dz);
                if( !Wev_DeckContainsDeckPoint(&box, dx, dz) )
                    continue;
                /* The wire rounds projected feet to a whole tile. One half
                 * tile of tolerance keeps rail-side crew inside the authored
                 * hull, without treating its whole staging zone as planking. */
                const struct WevConfig* cfg = wev->config;
                int hx = dx + box.recenter_x - cfg->bounds_off_x;
                int hz = dz + box.recenter_z - cfg->bounds_off_z;
                if( cfg->bounds_w > 0 && abs(hx) > cfg->bounds_w / 2 + 64 )
                    continue;
                if( cfg->bounds_h > 0 && abs(hz) > cfg->bounds_h / 2 + 64 )
                    continue;
                view_id = candidate;
                x = dx;
                z = dz;
                break;
            }
        app_wev_apply_placement(app, &npc->view_placement, npc->element_id, view_id, x, z);
    }
}

/**
 * World_ForeignActorRegisterFn for a boat deck: register the root world's
 * actors that this frame's routing put on THIS deck.
 *
 * The tier order inside the pass reproduces the root's — local player, then
 * alwaysontop NPCs, then other players, then the rest — because the painter's
 * one-actor-per-tile claim is decided by registration order and a deck is not
 * a reason for two players sharing a tile to swap.
 */
static void
app_wev_register_deck_actors(
    void* userdata,
    struct World* world)
{
    struct App* app = (struct App*)userdata;
    struct World* owner;
    struct World_EntityPool* pool;
    int view_id;
    int deck_level;

    assert(app);
    assert(world);
    assert(world->painter);

    view_id = app_worldview_id_of(app, world);
    /* Not a registered view (an offline harness world) — nobody can be aboard
     * something the registry has never heard of. */
    if( view_id < 0 || view_id == WORLDVIEW_ROOT )
        return;
    /* C4: a flattened hull draws no actors at all — the deob short-circuits
     * its whole population pass (docs/SAILING.md §5.3). */
    if( Wevs_IsLive(&app->wevs, view_id) && Wevs_Get(&app->wevs, view_id)->flattened )
        return;
    owner = app->world;
    if( !owner )
        return;
    deck_level = app_wev_deck_level(app, view_id);

    for( int pass = 0; pass < 4; pass++ )
    {
        int want_local = (pass == 0);
        int want_alwaysontop = (pass == 1);

        if( pass == 0 || pass == 2 )
        {
            pool = &owner->entities.player;
            for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
                 pi = World_EntityPoolNext(pool, pi) )
            {
                struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
                int is_local;

                if( !player || player->element_id < 0 )
                    continue;
                if( player->view_placement.view_id != view_id )
                    continue;
                is_local = owner->local_pid >= 0 && player->server_pid == owner->local_pid ? 1 : 0;
                if( is_local != want_local )
                    continue;
                /* A player registers at their OWN wire plane, not the deck's
                 * config plane — see app_world_sync_placement's rule. */
                {
                    int player_level = player->grid_position.level;

                    if( player_level < 0 )
                        player_level = 0;
                    if( player_level >= COLLISION_LEVELS )
                        player_level = COLLISION_LEVELS - 1;
                    World_RegisterForeignActor(
                        world,
                        player->element_id,
                        player_level,
                        player->view_placement.x,
                        player->view_placement.z,
                        WORLD_MOVER_PAINTER_PADDING,
                        player->orientation.yaw,
                        player->animation.needs_forward_draw_padding);
                }
            }
        }
        else
        {
            pool = &owner->entities.npc;
            for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
                 ni = World_EntityPoolNext(pool, ni) )
            {
                struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
                int size;

                if( !npc || npc->multinpc_hidden || npc->element_id < 0 )
                    continue;
                if( npc->view_placement.view_id != view_id )
                    continue;
                if( (npc->alwaysontop ? 1 : 0) != want_alwaysontop )
                    continue;
                size = npc->size > 0 ? npc->size : 1;
                World_RegisterForeignActor(
                    world,
                    npc->element_id,
                    deck_level,
                    npc->view_placement.x,
                    npc->view_placement.z,
                    WORLD_MOVER_PAINTER_PADDING + (size - 1) * 64,
                    npc->orientation.yaw,
                    npc->animation.needs_forward_draw_padding);
            }
        }
    }
}

/**
 * World_ForeignDynamicClaimFn for both halves of the borrowing arrangement:
 * on a boat deck, the actor elements standing on it that its own rebuild
 * sweep must not free; on the ROOT world, the hulls' C4 flatten-bake elements
 * — root-dynamic-pool elements no root entity pool claims, so an unclaiming
 * root rebuild frees them (model and all) while the Wev still holds the id
 * and pointer. @see World_ForeignDynamicClaimFn.
 */
int
app_wev_claim_deck_actors(
    void* userdata,
    struct World* world,
    int* out_element_ids,
    int max)
{
    struct App* app = (struct App*)userdata;
    struct World* owner;
    struct World_EntityPool* pool;
    int view_id;
    int n = 0;

    assert(app);
    assert(world);
    assert(out_element_ids);
    assert(max >= 0);

    view_id = app_worldview_id_of(app, world);
    if( view_id < 0 )
        return 0;
    if( view_id == WORLDVIEW_ROOT )
    {
        for( int i = 0; i < 2 && n < max; ++i )
            if( app->sailing_arrow_element[i] >= 0 )
                out_element_ids[n++] = app->sailing_arrow_element[i];
        return n;
    }
    owner = app->world;
    if( !owner )
        return 0;

    pool = &owner->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL && n < max;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        if( player && player->element_id >= 0 && player->view_placement.view_id == view_id )
            out_element_ids[n++] = player->element_id;
    }
    pool = &owner->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL && n < max;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        if( npc && npc->element_id >= 0 && npc->view_placement.view_id == view_id )
            out_element_ids[n++] = npc->element_id;
    }
    return n;
}

/**
 * Put every actor currently aboard `view_id` back in the root, element pool
 * included, before that view stops existing.
 *
 * App_WevDespawn clears the view's two pools. An aboard actor's element lives
 * in the dynamic half, and the root world still holds its id — so without this
 * the boat sinks and takes a live player's element with it, leaving the entity
 * record pointing at a freed slot that the allocator will hand to somebody
 * else. Their root position is authoritative and unchanged (the server
 * projects it), so re-homing is exactly a retag plus a placement reset; the
 * next routing pass re-decides where they are for real.
 */
static void
app_wev_evict_view_actors(
    struct App* app,
    int view_id)
{
    struct World* owner;
    struct World_EntityPool* pool;

    assert(app);
    assert(app->scene);
    assert(view_id > 0);
    assert(view_id < WORLDVIEW_MAX);

    owner = app->world;
    if( !owner )
        return;

    pool = &owner->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        int fx;
        int fz;

        if( !player || player->element_id < 0 || player->view_placement.view_id != view_id )
            continue;
        /* A wire-homed rider's draw position is DECK-LOCAL — project it out
         * through the hull (still live here; despawn runs evict first) so the
         * root placement is a real root coordinate, not deck units misread as
         * one. And drop the homing itself: a new hull reusing this view id in
         * the same packet must not inherit this rider. */
        fx = (int)player->draw_position.x;
        fz = (int)player->draw_position.z;
        app_wev_actor_root_fine(app, &player->view_placement, &fx, &fz);
        app_wev_apply_placement(
            app, &player->view_placement, player->element_id, WORLDVIEW_ROOT, fx, fz);
        player->view_placement.home_view = 0;
    }
    pool = &owner->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        int fx;
        int fz;

        if( !npc || npc->element_id < 0 || npc->view_placement.view_id != view_id )
            continue;
        fx = (int)npc->draw_position.x;
        fz = (int)npc->draw_position.z;
        app_wev_actor_root_fine(app, &npc->view_placement, &fx, &fz);
        app_wev_apply_placement(app, &npc->view_placement, npc->element_id, WORLDVIEW_ROOT, fx, fz);
        npc->view_placement.home_view = 0;
    }
    if( app->aboard_view == view_id )
        app->aboard_view = WORLDVIEW_ROOT;
}

/**
 * Re-publish every live non-root view's painter dynamics, once per tick.
 *
 * Only the root world is cycled (App_WorldTick); a deck advances no simulation
 * of its own. But `painter_reset_to_static` is what clears last frame's actors
 * and nested hulls off a deck's painter, so a view that never gets this pass
 * accumulates every actor that ever stood on it.
 */
void
app_wev_cycle_views(struct App* app)
{
    assert(app);

    for( int id = 1; id < WORLDVIEW_MAX; id++ )
    {
        struct Worldview* view;

        if( !WorldviewRegistry_IsLive(&app->worldviews, id) )
            continue;
        view = WorldviewRegistry_Get(&app->worldviews, id);
        assert(view->world);
        if( !view->world->painter )
            continue;
        World_CycleRegisterDynamics(view->world);
    }
}

/**
 * The hull bob, per frame (deob class467.method10419 + the client-tick
 * advance at client.java:9218-9241): pick each hull's ACTIVE seq — the wire
 * one-shot once its delay has elapsed, else the config's looping idle — and
 * sample its skeletal (animaya) root bone at the cursor's frame. The bone-0
 * Y translation, negated (the deob flips its Y-up pose into Y-down), becomes
 * wev->bob_y, which app_wev_bind_frame_xforms adds to the descent Y so the
 * deck, its locs and everyone aboard bob together.
 *
 * One animaya frame per 20 ms client cycle (the Wevs clock). A completed
 * one-shot clears itself and restarts the idle at frame 0, exactly the
 * deob's completion rule. Seqs still loading request themselves and bob 0
 * until the load lands. Native class467.method10419 applies its current
 * animation matrix to both full and flattened scenes.
 */
void
app_wev_advance_bobs(struct App* app)
{
    assert(app);

    for( int id = WORLDVIEW_ROOT + 1; id < WORLDVIEW_MAX; id++ )
    {
        struct Wev* wev;
        struct ToriDraw_Animation* anim;
        struct ToriDraw_SkeletalAnim* sk;
        int active;
        double start;
        int one_shot;
        int frame;

        if( !Wevs_IsLive(&app->wevs, id) )
            continue;
        wev = Wevs_Get(&app->wevs, id);
        wev->bob_y = 0;
        assert(wev->config);

        if( wev->seq_id >= 0 && app->wevs.clock >= wev->seq_start_cycle )
        {
            active = wev->seq_id;
            start = wev->seq_start_cycle;
            one_shot = 1;
        }
        else if( wev->config->anim_id >= 0 )
        {
            active = wev->config->anim_id;
            start = wev->anim_start_cycle;
            one_shot = 0;
        }
        else
            continue;

        anim = WorldSeqSourceToriDraw_Animation(&app->seq_source, active);
        if( !anim || !anim->skeletal )
        {
            app_request_entity_seq(app, active);
            continue;
        }
        /* Playback is bounded by anim->frame_count — the seq's mayarange
         * span (the deob's playable window), which the loader clamps to the
         * bake. The raw bake can run longer (curves keep authoring range the
         * game never shows: the 2x5 idle bakes 661 ticks, plays 240). */
        sk = anim->skeletal;
        if( anim->frame_count <= 0 || sk->frame_count <= 0 || sk->bone_count <= 0 )
            continue;

        frame = (int)(app->wevs.clock - start);
        if( frame < 0 )
            frame = 0;
        if( one_shot && frame >= anim->frame_count )
        {
            /* One-shot complete: clear it and restart the idle from frame 0
             * (deob: field5698 cleared, method9990(field5697)). */
            wev->seq_id = -1;
            wev->anim_start_cycle = app->wevs.clock;
            if( wev->config->anim_id < 0 )
                continue;
            anim = WorldSeqSourceToriDraw_Animation(&app->seq_source, wev->config->anim_id);
            if( !anim || !anim->skeletal )
            {
                app_request_entity_seq(app, wev->config->anim_id);
                continue;
            }
            sk = anim->skeletal;
            if( anim->frame_count <= 0 || sk->frame_count <= 0 || sk->bone_count <= 0 )
                continue;
            frame = 0;
        }
        else if( !one_shot )
            frame %= anim->frame_count;
        if( frame >= sk->frame_count )
            frame = sk->frame_count - 1;

        /* Column-major 4x4: the translation column is elements 12..14. */
        wev->bob_y = -(int)lroundf(sk->matrices[(size_t)(frame * sk->bone_count) * 16 + 13]);
        if( app_wev_debug_enabled() && frame % 60 == 0 )
            fprintf(
                stderr,
                "wev: BOB view %d seq %d frame %d/%d y %d\n",
                id,
                active,
                frame,
                anim->frame_count,
                wev->bob_y);
    }
}

