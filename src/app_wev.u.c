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
static void
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
static void
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
static void
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
#include "game/sailing_paint_order.u.h"

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

static void
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
static void
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
static int
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
static void
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
static int
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
static void
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

/* Defined with the seq loader further down. */
static void
app_request_entity_seq(
    struct App* app,
    int seq_id);

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
static void
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
