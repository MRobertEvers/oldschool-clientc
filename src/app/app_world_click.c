/*
 * Routing a click in the world: the mouse gate, the tryMove family, and the
 * pick finish.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static struct CollisionNearestOpts
app_op_nearest_opts(struct App const* app);
static int
app_try_move_op(
    struct App* app,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach,
    int ctrl_held);
static struct CollisionApproach
app_scenery_approach(
    struct App const* app,
    struct WorldEntity_Scenery const* scenery);

/* The world rectangle is retained from the last emit, but visibility is live.
 * A layout may suppress the viewport before the next emit refreshes that
 * rectangle, and a component-array index may have been reclaimed meanwhile.
 * Check both the tree's current world identity and effective display state
 * before an app-owned mouse gesture uses the retained box. */
int
app_world_viewport_component_live(struct App const* app)
{
    struct UITreeComponent const* node;
    int32_t idx;

    if( !app || !app->tree || !app->world_view_valid )
        return 0;
    idx = app->world_emit_desc.node_index;
    if( idx < 0 || (uint32_t)idx >= app->tree->component_count || idx != app->tree->world_index )
        return 0;
    node = &app->tree->components[idx];
    if( node->freed || node->type != UIELEM_BUILTIN_WORLD )
        return 0;
    if( app->world_emit_desc.component_id >= 0 &&
        node->component_id != app->world_emit_desc.component_id )
        return 0;
    return !UITree_NodeOrAncestorDisplayHidden(app->tree, idx);
}

/* "Only hittest the world if the mouse is over the world element": inside the
 * world emit clip rect with no *clickable* UI on top. Script-hover targets
 * (layers with only on_mouse_repeat / on_mouse_over — e.g. iface 548 child 40
 * on cache.643, which blankets the viewport and hammers a broken CS2) must NOT
 * block world pick: they are pass-through for clicks. Use HitTestInteractive,
 * not the CS2 hover walk (hover_com_id). */
int
app_world_mouse_gate(
    struct App* app,
    int mouse_x,
    int mouse_y)
{
    struct UITreeEmitClip const* clip;
    struct UITreeEmitDesc const* desc;

    if( !app->world_active || !app_world_viewport_component_live(app) )
        return 0;
    /* A chrome panel drawn over the viewport is as opaque to the world as an
     * interface is: no hover, no pick, no click-to-walk under the window. */
    if( app_chrome_wants_pointer(app, mouse_x, mouse_y) )
        return 0;
    /*
     * The world map, which is drawn into a surface box rather than out of
     * ordinary components: the tree holds one BUILTIN_WORLDMAP node and the
     * map itself -- its tiles, its icons, its blank margins -- is pixels this
     * client paints. So nothing in the walk below sees it, and a click that
     * missed the map's own chrome fell through to the world underneath and
     * WALKED THE PLAYER, on a screen where the world is not even visible.
     * The box is the same one app_worldmap_drag_tick arms its drag from.
     */
    if( app->worldmap.drag.box_w > 0 && app->worldmap.drag.box_h > 0 && mouse_x >= app->worldmap.drag.box_x &&
        mouse_x < app->worldmap.drag.box_x + app->worldmap.drag.box_w && mouse_y >= app->worldmap.drag.box_y &&
        mouse_y < app->worldmap.drag.box_y + app->worldmap.drag.box_h && app_worldmap_surface_live(app) )
        return 0;
    /* A viewport interface (reference mainModalId) owns the entire viewport
     * rect: buildMinimenu adds that modal's component options there and NEVER
     * world options (Client.ts:2772 `if (mainModalId === -1) addWorldOptions
     * else addComponentOptions`). So while one is mounted, world picking stops
     * and the mouse cannot hittest the scene through the gaps between the
     * modal's components (e.g. the empty space between a shop's item slots). */
    if( app->slots.main_modal_id != -1 )
        return 0;
    /*
     * The same rule for the era where that slot state does not exist.
     *
     * `slots.main_modal_id` is written by the IF1 packet path and seeded from a
     * revconfig-baked tree; a rev-230 tree is the cache's own IF3 gameframe and
     * has neither, so the check above is dead there and every interface the
     * server mounted was transparent to the world. `UITree_PointBlocksWorld`
     * asks the tree instead of the slot table: a type-0 mount owns its clipped
     * host rectangle (including blank space outside a smaller mounted root),
     * while a `noClickThrough` layer owns its own bounds. Overlay/tab mounts
     * stay transparent unless their own records raise that flag.
     */
    if( app->tree )
    {
        int ui_blocks_world = 0;
        int32_t ui_interactive_hit = -1;
        /* One collection, both answers. These are asked back to back at the same
         * point, and with any plugin widget anchor present each of the two entry
         * points is a whole ordered walk of the tree. */
        UITree_PointQuery(
            app->tree, &app->ui_host, mouse_x, mouse_y, &ui_blocks_world, &ui_interactive_hit);
        if( ui_blocks_world )
            return 0;
        /* Clickable UI wins over the world; pass-through layers with hover
         * scripts do not. */
        if( ui_interactive_hit >= 0 )
            return 0;
    }
    /* Gate on the world WIDGET rect, not just its clip: an unclipped world
     * node inherits a full-canvas clip, which let sidebar/chat clicks count
     * as "in world" — right-clicking an inventory item offered "Walk here"
     * (reference buildMinimenu adds world options only inside the viewport
     * rect 4..516 x 4..338). */
    desc = &app->world_emit_desc;
    if( desc->w > 0 && desc->h > 0 &&
        (mouse_x < desc->x || mouse_x >= desc->x + desc->w || mouse_y < desc->y ||
         mouse_y >= desc->y + desc->h) )
        return 0;
    clip = &app->world_emit_desc.clip;
    return mouse_x >= clip->x && mouse_x < clip->x + clip->w && mouse_y >= clip->y &&
           mouse_y < clip->y + clip->h;
}

/*
 * Ground-click fallback: the closest walkable-level tile to a click that hit no
 * terrain at all.
 *
 * Picking happens during rasterisation — a tile registers a hit only if it
 * DREW and the click landed inside one of its two triangles (torirs_pick.c,
 * reference World.ts insideTriangle -> World.groundX). So a click on the sky,
 * on the void outside an instance's floor (the Inferno arena is ringed by it),
 * or on a tile the level filter refuses leaves the pickset with no terrain
 * item, and "Walk here" is emitted with no destination. The reference drops
 * that click outright; we resolve it to the nearest tile instead, which is
 * what the player meant — the router's own unreachable fallback
 * (features->ground_click_nearest_model, client-side or server-side depending
 * on pathing_mode) then closes whatever gap is left.
 *
 * "Nearest" is measured in SCREEN space against the tile centres, using the
 * same camera the frame was drawn with: the tile that looks closest to the
 * cursor is the one the click meant, and that stays true above the horizon
 * (where no ground plane intersection exists) and over sloped ground.
 *
 * Only tiles that carry terrain are candidates, so the void never becomes a
 * destination; levels above the player's are excluded for the same reason the
 * pick classifier excludes them (that is a roof you are standing under).
 * Called once per world click — never per frame — because a full scene sweep
 * costs two divides a tile.
 */
int
app_world_nearest_ground_tile(
    struct App* app,
    int click_x,
    int click_y,
    int* out_x,
    int* out_z,
    int* out_level)
{
    struct World* world = app->world;
    struct WorldEntity_Player* player;
    int max_level, level;
    /* 64-bit: a tile just past the near plane projects thousands of screen
     * widths out, and the square of that does not fit in an int. */
    long long best_d2 = LLONG_MAX;

    if( !world || !world->load_complete || !app->world_view_valid )
        return 0;

    player = app_local_player(app);
    max_level = player ? player->grid_position.level : 0;
    if( max_level < 0 )
        max_level = 0;
    if( max_level >= WORLD_MAP_TERRAIN_LEVELS )
        max_level = WORLD_MAP_TERRAIN_LEVELS - 1;

    /* Descending, with a strict improvement test, so the player's own level
     * wins a tie against the bridge deck / VIS_BELOW tile drawn beneath it. */
    for( level = max_level; level >= 0; level-- )
    {
        for( int x = 0; x < world->_scene_size; x++ )
        {
            for( int z = 0; z < world->_scene_size; z++ )
            {
                int fine_x, fine_z, sx, sy;
                long long dx, dy, d2;

                if( World_TerrainElementAt(world, x, z, level) < 0 )
                    continue;
                fine_x = x * 128 + 64;
                fine_z = z * 128 + 64;
                if( !app_world_project_at(
                        app,
                        fine_x,
                        fine_z,
                        app_world_height(app, fine_x, fine_z, level),
                        &sx,
                        &sy) )
                    continue; /* behind the near plane */
                dx = sx - click_x;
                dy = sy - click_y;
                d2 = dx * dx + dy * dy;
                if( d2 < best_d2 )
                {
                    best_d2 = d2;
                    *out_x = x;
                    *out_z = z;
                    *out_level = level;
                }
            }
        }
    }

    if( best_d2 == LLONG_MAX )
        return 0;
    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "groundfallback: click=%d,%d -> scene=%d,%d l%d dist2=%lld\n",
            click_x,
            click_y,
            *out_x,
            *out_z,
            *out_level,
            best_d2);
    return 1;
}

/* Reference Client.tryMove for ground (type 0) / minimap (type 1) clicks:
 * BFS route on the local player's level from the player's final route tile
 * (routeX[0]) to the clicked scene tile with the era's unreachable fallback
 * (features->ground_click_nearest_model), send the MOVE_* waypoint packet,
 * latch the minimap flag from the routed
 * destination (route[0]). The local player is NOT moved here — the
 * PLAYER_INFO echo drives movement, exactly like the reference; the old
 * World_PlayerPathJump prediction fought the echo and made the player jump
 * around. Offline keeps the jump as scripted-scene feedback. Returns 1 when
 * a route was found and a packet sent (or offline feedback applied). */
int
app_try_move(
    struct App* app,
    int dst_x,
    int dst_z,
    int type,
    int click_x,
    int click_y,
    int yaw,
    int ctrl_held)
{
    /* Reference routeX/routeZ scratch is 4000 entries (Client.ts:409). */
    static int route_x[4000];
    static int route_z[4000];
    struct World* world = app->world;
    struct WorldEntity_Player* player;
    struct CollisionMap* cm;
    int level, route_len, nearest = 0;
    bool online = app->net && app->net->state == TORIRS_NET_GAME;

    if( !world || !world->load_complete )
        return 0;
    if( dst_x < 0 || dst_z < 0 || dst_x >= world->_scene_size || dst_z >= world->_scene_size )
        return 0;

    player = app_local_player(app);
    if( !player )
    {
        /* Offline / not yet pid-synced: keep the old local jump so scripted
         * scenes still move the first spawned player. */
        int head = World_EntityPoolHead(&world->entities.player);
        if( !online && head != WORLD_ENTITY_NIL )
        {
            World_PlayerPathJump(world, head, false, dst_x, dst_z);
            app->minimap.flag_tile_x = dst_x;
            app->minimap.flag_tile_z = dst_z;
            return 1;
        }
        return 0;
    }

    /*
     * The era's ceiling on how far a GROUND pick may be from the player
     * (features->ground_click_clamp_tiles). Deob class112.method4269 applies
     * it where the hittest records the tile; this client applies it where the
     * recorded tile is spent, which is the same tile — our pick has no
     * `field1664` of its own to rewrite, and the minimap click (type 1) must
     * not be caught by it, since the reference computes that tile from the
     * minimap's own geometry and never routes it through method4269.
     */
    if( type == 0 && app->features->ground_click_clamp_tiles > 0 )
    {
        int clamp = app->features->ground_click_clamp_tiles;
        int px = (int)player->draw_position.x >> 7;
        int pz = (int)player->draw_position.z >> 7;
        int dx = px - dst_x;
        int dz = pz - dst_z;
        /* (int) Math.hypot(...) - clamp: truncated, so a tile at exactly the
         * ceiling is left alone. */
        int over = (int)sqrt((double)(dx * dx + dz * dz)) - clamp;

        if( over > 0 )
        {
            int clamped_x = (px * over + dst_x * clamp) / (over + clamp);
            int clamped_z = (pz * over + dst_z * clamp) / (over + clamp);
            if( torirs_env_net_debug() )
                TORIRS_LOG(
                    "groundclamp: %d,%d -> %d,%d (player %d,%d; %d tiles past %d)\n",
                    dst_x,
                    dst_z,
                    clamped_x,
                    clamped_z,
                    px,
                    pz,
                    over,
                    clamp);
            dst_x = clamped_x;
            dst_z = clamped_z;
        }
    }

    level = player->grid_position.level;
    if( level < 0 )
        level = 0;
    if( level >= COLLISION_LEVELS )
        level = COLLISION_LEVELS - 1;
    cm = world->collision_maps[level];
    if( !cm )
        return 0;

    if( app->features->pathing_mode == TORIRS_PATHING_SERVER_AUTHORITATIVE )
    {
        /* The server owns the route and the map flag (SET_MAP_FLAG). Send the
         * destination alone — osrs230 MOVE_GAMECLICK is a fixed 5-byte body. */
        route_x[0] = dst_x;
        route_z[0] = dst_z;
        route_len = 1;
    }
    else
    {
        struct CollisionNearestOpts nearest_opts;

        collision_nearest_opts_from_model(app->features->ground_click_nearest_model, &nearest_opts);
        /* Ground/minimap clicks only — see the field. */
        nearest_opts.unbounded = app->features->ground_click_nearest_unbounded;
        route_len = collision_map_try_route(
            cm,
            player->pathing.route_x[0],
            player->pathing.route_z[0],
            dst_x,
            dst_z,
            &nearest_opts,
            route_x,
            route_z,
            (int)(sizeof(route_x) / sizeof(route_x[0])),
            &nearest);
    }
    if( route_len < 1 )
        return 0;

    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "trymove: type=%d src=%d,%d dst=%d,%d route_len=%d nearest=%d dest=%d,%d\n",
            type,
            player->pathing.route_x[0],
            player->pathing.route_z[0],
            dst_x,
            dst_z,
            route_len,
            nearest,
            route_x[0],
            route_z[0]);

    if( type == 1 )
        APP_NET_SEND(
            app,
            net_out_move_minimapclick(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                world->_base_tile_x,
                world->_base_tile_z,
                route_x,
                route_z,
                route_len,
                ctrl_held,
                click_x,
                click_y,
                yaw,
                0,
                0,
                (int)player->draw_position.x,
                (int)player->draw_position.z,
                nearest));
    else
        APP_NET_SEND(
            app,
            net_out_move_gameclick(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                world->_base_tile_x,
                world->_base_tile_z,
                route_x,
                route_z,
                route_len,
                ctrl_held));

    /* Client-BFS eras latch the flag from the routed destination. Under
     * SERVER_AUTHORITATIVE the server owns SET_MAP_FLAG — do not paint a
     * local guess that the clear packet would then fight. Offline still
     * wants the UI mark. */
    if( app->features->pathing_mode != TORIRS_PATHING_SERVER_AUTHORITATIVE || !online )
    {
        app->minimap.flag_tile_x = route_x[0];
        app->minimap.flag_tile_z = route_z[0];
        app->need_redraw = 1;
    }
    return 1;
}

/* The era's alternative-route settings for an interaction click. Client-TS
 * passes tryNearest = false to every type-2 tryMove, so `range` is 0 there and
 * an unreachable target produces no MOVE_OPCLICK at all; the OSRS era supplies
 * the rsmod 21x21 rect-ranked search its server always runs. */
static struct CollisionNearestOpts
app_op_nearest_opts(struct App const* app)
{
    struct CollisionNearestOpts opts = {
        .range = app->features->op_click_nearest_range,
        .max_dist = 100,
        .rank_by_rect_distance = app->features->nearest_ranks_by_rect_distance,
    };
    return opts;
}

/* Reference tryMove type 2 (interactWithLoc / obj doAction): pathfind toward a
 * loc/obj using its approach footprint and — when a route exists — emit
 * MOVE_OPCLICK. Unlike a ground click this arrives on an approach tile beside
 * the loc, not the loc tile itself. The caller sends the OP(LOC|OBJ|NPC)
 * afterwards regardless of the return, matching the reference (the walk is
 * best-effort; the interaction is always requested on the same click). A
 * reachable click always emits — even a zero-delta route when the player
 * already stands on an approach tile. Returns 1 when a route was found (so an
 * obj can skip its 1x1 fallback), 0 when unreachable.
 *
 * Under a server-authoritative era there is no route to compute and no
 * MOVE_OPCLICK to send: the interaction packet carries the target and the
 * server paths. The map flag comes from the server's SET_MAP_FLAG. */
static int
app_try_move_op(
    struct App* app,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach,
    int ctrl_held)
{
    static int route_x[4000];
    static int route_z[4000];
    struct World* world = app->world;
    struct WorldEntity_Player* player;
    struct CollisionMap* cm;
    struct CollisionNearestOpts nearest_opts;
    int level, route_len;

    if( !world || !world->load_complete )
        return 0;
    if( dst_x < 0 || dst_z < 0 || dst_x >= world->_scene_size || dst_z >= world->_scene_size )
        return 0;

    player = app_local_player(app);
    if( !player )
        return 0;

    if( app->features->pathing_mode == TORIRS_PATHING_SERVER_AUTHORITATIVE )
        return 1;

    level = player->grid_position.level;
    if( level < 0 )
        level = 0;
    if( level >= COLLISION_LEVELS )
        level = COLLISION_LEVELS - 1;
    cm = world->collision_maps[level];
    if( !cm )
        return 0;

    nearest_opts = app_op_nearest_opts(app);
    route_len = collision_map_try_route_op(
        cm,
        player->pathing.route_x[0],
        player->pathing.route_z[0],
        dst_x,
        dst_z,
        approach,
        &nearest_opts,
        route_x,
        route_z,
        (int)(sizeof(route_x) / sizeof(route_x[0])),
        NULL);
    if( route_len < 1 )
        return 0;

    APP_NET_SEND(
        app,
        net_out_move_opclick(
            app->net->rev,
            app->net->random_out,
            _nsbuf,
            sizeof(_nsbuf),
            world->_base_tile_x,
            world->_base_tile_z,
            route_x,
            route_z,
            route_len,
            ctrl_held));

    app->minimap.flag_tile_x = route_x[0];
    app->minimap.flag_tile_z = route_z[0];
    app->need_redraw = 1;
    return 1;
}

/*
 * Approach an NPC (Client.ts OP_NPC1..5 / USEHELD_ONNPC / TGT_NPC) at its
 * current route tile, so the player walks into interaction/cast range on the
 * same click. Best-effort — the OP(NPC|NPCU|NPCT) packet is sent by the caller
 * regardless of the walk result, matching the reference.
 *
 * Client-TS passes a literal 1x1 target here (`tryMove(..., npc.routeX[0],
 * npc.routeZ[0], 2, 1, 1, ...)`) whatever the NPC's size, so under the LostCity
 * era so do we. Every rsmod-derived server instead treats the NPC as its own
 * sizeXsize rectangle, and against one of those a 1x1 target flags a tile
 * *inside* a large NPC — hence the era switch.
 */
int
app_try_move_npc(
    struct App* app,
    struct WorldEntity_NPC const* npc,
    int ctrl_held)
{
    struct CollisionApproach approach = { 0 };
    int size;

    assert(npc);
    size = app->features->npc_approach_uses_size && npc->size > 0 ? npc->size : 1;
    if( app->features->approach_model == TORIRS_APPROACH_RECT )
        collision_approach_from_shape(-2, 0, size, size, 0, 1, &approach);
    else
    {
        approach.kind = COLL_APPROACH_LEGACY_SHAPE;
        approach.loc_width = size;
        approach.loc_length = size;
        approach.mover_size = 1;
    }
    return app_try_move_op(
        app, npc->pathing.route_x[0], npc->pathing.route_z[0], &approach, ctrl_held);
}

/* Approach another player (Client.ts OPPLAYER1..5 / OPPLAYERU / OPPLAYERT):
 * always a literal 1×1 at routeX[0]/routeZ[0] (PATHING_INTERACTION_PARITY D6).
 * Modern era: exclusive rectangle (shape -2). */
int
app_try_move_player(
    struct App* app,
    struct WorldEntity_Player const* player,
    int ctrl_held)
{
    struct CollisionApproach approach = { 0 };

    assert(player);
    if( app->features->approach_model == TORIRS_APPROACH_RECT )
        collision_approach_from_shape(-2, 0, 1, 1, 0, 1, &approach);
    else
    {
        approach.kind = COLL_APPROACH_LEGACY_SHAPE;
        approach.loc_width = 1;
        approach.loc_length = 1;
        approach.mover_size = 1;
    }
    return app_try_move_op(
        app, player->pathing.route_x[0], player->pathing.route_z[0], &approach, ctrl_held);
}

/*
 * Build the op-click approach for a loc.
 *
 * LEGACY_SHAPE is the reference interactWithLoc (Client.ts:5963-5984):
 * centrepieces and ground decor approach by footprint (testLoc, size and
 * forceapproach already angle-rotated at register time); walls and wall
 * decorations approach an adjacent facing tile (testWall/testWDecor, locShape =
 * shape + 1).
 *
 * RECT is rsmod ReachStrategy keyed off the **placed shape** via
 * collision_exit_strategy / collision_approach_from_shape (wall 0–3/9,
 * wall-decor 4–8, rectangle 10/11/22). The XRSPS clipType/action heuristic
 * that used to live here was deleted — see docs/OSRS_PATHING_LOS.md §1.1.
 */
static struct CollisionApproach
app_scenery_approach(
    struct App const* app,
    struct WorldEntity_Scenery const* scenery)
{
    struct CollisionApproach approach = { 0 };
    int shape = scenery->shape;
    bool sized = shape == RSCACHE_LOC_SHAPE_SCENERY ||
                 shape == RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL ||
                 shape == RSCACHE_LOC_SHAPE_FLOOR_DECORATION;

    approach.mover_size = 1;

    if( app->features->approach_model != TORIRS_APPROACH_RECT )
    {
        approach.kind = COLL_APPROACH_LEGACY_SHAPE;
        if( sized )
        {
            approach.loc_width = scenery->size_x;
            approach.loc_length = scenery->size_z;
            approach.forceapproach = scenery->force_approach;
        }
        else
        {
            approach.loc_angle = scenery->angle;
            approach.loc_shape = shape + 1;
        }
        return approach;
    }

    collision_approach_from_shape(
        shape,
        scenery->angle,
        scenery->size_x,
        scenery->size_z,
        scenery->force_approach,
        1,
        &approach);
    return approach;
}

/*
 * Approach a loc (Client.ts interactWithLoc, shared by OP_LOC1..5 /
 * USEHELD_ONLOC / TGT_LOC): pathfind to the loc's approach and emit
 * MOVE_OPCLICK. The walk is best-effort, but the *lookup* is not: the reference
 * resolves the placed loc through `typecode2` and returns early on -1, sending
 * no walk, no cross and no OPLOC. Returns 0 for that case so the caller can
 * drop the whole click; 1 when the loc exists (whether or not it was
 * reachable).
 */
int
app_try_move_loc(
    struct App* app,
    int element_id,
    int tile_x,
    int tile_z,
    int ctrl_held)
{
    struct WorldEntity_Scenery const* scenery = World_SceneryGetByElementId(app->world, element_id);
    struct CollisionApproach approach;

    if( !scenery )
        return 0;
    approach = app_scenery_approach(app, scenery);
    app_try_move_op(app, tile_x, tile_z, &approach, ctrl_held);
    return 1;
}

/* Reference tryMove type 2 toward a ground obj (Client.ts OP_OBJ1..5 /
 * USEHELD_ONOBJ / TGT_OBJ): pathfind to the exact tile, and on failure retry a
 * 1x1 approach so an adjacent tile still arrives, then emit MOVE_OPCLICK.
 * Best-effort — the caller sends the OP packet regardless of the route. */
void
app_try_move_obj(
    struct App* app,
    int tile_x,
    int tile_z,
    int ctrl_held)
{
    struct CollisionApproach exact = { .kind = COLL_APPROACH_EXACT, .mover_size = 1 };
    if( !app_try_move_op(app, tile_x, tile_z, &exact, ctrl_held) )
    {
        struct CollisionApproach one = { 0 };
        if( app->features->approach_model == TORIRS_APPROACH_RECT )
            collision_approach_from_shape(-2, 0, 1, 1, 0, 1, &one);
        else
        {
            one.kind = COLL_APPROACH_LEGACY_SHAPE;
            one.loc_width = 1;
            one.loc_length = 1;
            one.mover_size = 1;
        }
        app_try_move_op(app, tile_x, tile_z, &one, ctrl_held);
    }
}

/* Classify the raw hits the render pass collected into the app pickset +
 * hover tile. Runs after ToriRS_Soft3D_RenderFrame when the pick was armed. */
void
app_world_pick_finish(
    struct App* app,
    struct ToriRS_PickHits const* hits)
{
    struct ToriRS_PickResult result;
    struct WorldEntity_Player* player = app_local_player(app);
    /* The effective ROOT plane (aboard: the hull's, not the deck plane the
     * rider stands at) — the reach filter compares root scenery/terrain
     * levels against it, and the deck plane would filter the whole shore
     * out of every click. */
    int player_level = player ? app_cinema_level(app) : -1;

    ToriRS_PickHitsClassifyViews(
        app->world,
        &app->worldviews,
        &app->wevs,
        app->aboard_view,
        hits,
        player_level,
        &app->world_pickset,
        &result);
    if( result.hover_tile_valid )
    {
        app->world_hover_tile_x = result.hover_tile_x;
        app->world_hover_tile_z = result.hover_tile_z;
        app->world_hover_tile_level = result.hover_tile_level;
    }
    else
    {
        app->world_hover_tile_x = -1;
        app->world_hover_tile_z = -1;
    }
    if( result.hover_view_valid )
    {
        app->world_hover_view = result.hover_view;
        app->world_hover_view_x = result.hover_view_x;
        app->world_hover_view_z = result.hover_view_z;
        app->world_hover_view_level = result.hover_view_level;
    }
    else
        app->world_hover_view = 0;

    if( getenv("TORIRS_WORLD_PICK_DEBUG") )
    {
        TORIRS_LOG(
            "world_pick: mouse=%d,%d count=%d hover_tile=%d,%d,%d\n",
            app->world_mouse_x,
            app->world_mouse_y,
            app->world_pickset.count,
            result.hover_tile_valid ? result.hover_tile_x : -1,
            result.hover_tile_valid ? result.hover_tile_z : -1,
            result.hover_tile_valid ? result.hover_tile_level : -1);
        for( int i = 0; i < app->world_pickset.count; i++ )
        {
            /* Loc id/name/footprint turn an element id into something you can look up in the
             * cache — the difference between "element 4345 draws late" and "the plinth is a
             * separate 1x1 loc one tile nearer than the statue". */
            struct WorldEntity_Scenery* scenery =
                World_SceneryGetByElementId(app->world, app->world_pickset.items[i].element_id);
            TORIRS_LOG(
                "world_pick:  [%d] element=%d type=%d tile=%d,%d,%d loc=%d size=%dx%d "
                "origin=%d,%d,%d '%s'\n",
                i,
                app->world_pickset.items[i].element_id,
                (int)app->world_pickset.items[i].type,
                app->world_pickset.items[i].tile_x,
                app->world_pickset.items[i].tile_z,
                app->world_pickset.items[i].tile_level,
                scenery ? scenery->loc_id : -1,
                scenery ? scenery->size_x : -1,
                scenery ? scenery->size_z : -1,
                scenery ? scenery->grid_position.x : -1,
                scenery ? scenery->grid_position.z : -1,
                scenery ? scenery->grid_position.level : -1,
                scenery ? scenery->info->name : "");
        }
    }
}
