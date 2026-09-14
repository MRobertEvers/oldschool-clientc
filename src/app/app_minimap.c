/*
 * The minimap: the per-frame dots, the click-to-walk, and the level it draws
 * at.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_minimap_push_dot(
    struct App* app,
    int rel_fx,
    int rel_fz,
    int scene_id,
    int atlas_index);

/* One reference minimapDrawDot: rotate the entity's player-relative offset by
 * the camera yaw into widget pixels (4 px/tile => fine units / 32), cull past
 * the ring (dist^2 > 6400), store the sprite's top-left center-relative. */
static void
app_minimap_push_dot(
    struct App* app,
    int rel_fx,
    int rel_fz,
    int scene_id,
    int atlas_index)
{
    int yaw;
    int w = 4, h = 4;
    int dx = 0, dy = 0;
    struct MinimapRotation rotation;
    struct UITreeMinimapDot* dot;

    {
        int count = 0;
        struct ToriDraw_Sprite** frames = ToriDraw_SceneSpriteGet(app->scene, scene_id, &count);
        if( frames && atlas_index >= 0 && atlas_index < count && frames[atlas_index] )
        {
            w = frames[atlas_index]->width;
            h = frames[atlas_index]->height;
        }
    }
    yaw = ToriDraw_NormalizeAngle(app->world_camera.yaw);
    rotation.sin = ToriDraw_Sin(yaw);
    rotation.cos = ToriDraw_Cos(yaw);
    if( !MinimapView_PlaceDot(&rotation, rel_fx, rel_fz, w, h, &dx, &dy) )
        return;
    dot = MinimapDots_Push(&app->minimap_dots);
    if( !dot )
        return;
    dot->dx = dx;
    dot->dy = dy;
    dot->w = w;
    dot->h = h;
    dot->scene_id = scene_id;
    dot->atlas_index = atlas_index;
    dot->color = 0;
    /* The dots array persists across frames; only the hull-icon pass writes a
     * rotation, so an unset field here would inherit whatever spun the slot's
     * previous occupant. */
    dot->rotate = 0;
}

/* Reference minimapDraw overlay: ground objs (yellow), NPCs, other players
 * (white), the destination flag, then the local-player 3x3 white square.
 * mapdots frames: 0 obj, 1 npc, 2 player, 3 friend; mapmarker frame 0 flag. */
int
App_MinimapBuildDots(
    struct App* app,
    struct UITreeMinimapDot const** out_dots)
{
    struct WorldEntity_Player* local = app_local_player(app);
    struct World* world = app->world;
    struct World_EntityPool* pool;
    int px, pz;
    int cull_level;
    int dots_scene, marker_scene;

    MinimapDots_Reset(&app->minimap_dots);
    *out_dots = app->minimap_dots.dots;
    if( !world || !world->load_complete || !local )
        return 0;
    /* Aboard, the rider's own level is a deck plane — the ROOT things this
     * map shows (icons, ground items, shore actors) cull against the hull's
     * root level instead (see app_minimap_level). */
    cull_level = app_minimap_level(app, local);
    px = (int)local->draw_position.x;
    pz = (int)local->draw_position.z;
    /* Aboard, the local player's own coordinates are deck-local; the minimap
     * stays a MAIN-WORLD map centred on their position pushed out through the
     * hull (deob client.java:9343-9352) — that is what scrolls the sea past
     * while the boat sails. */
    app_wev_actor_root_fine(app, &local->view_placement, &px, &pz);
    dots_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPDOTS);
    marker_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPMARKER);

    /*
     * World-entity (hull) icons: the config's own minimap sprite (opcode 26 —
     * deob class387.field4866, drawn by client.method1819) at the hull's
     * root position, rotated by its yaw (the rotate written onto the pushed
     * dot below); position, sprite and the 20-tile cull ring (push_dot's 6400
     * check, the deob's own radius) are the reference's. First, so actor dots
     * draw over hulls.
     */
    {
        int count = Wevs_ViewListCount(&app->wevs, WORLDVIEW_ROOT);

        for( int i = 0; i < count; i++ )
        {
            struct Wev* wev = Wevs_ViewListAt(&app->wevs, WORLDVIEW_ROOT, i);
            struct ToriRS_Sprite* sprite;
            int scene_id;

            assert(wev);
            /* Every live Wev carries a config (Wevs_Spawn asserts it); a hull
             * without an authored icon is the legitimate absence here. */
            assert(wev->config);
            if( wev->config->minimap_sprite_id < 0 )
                continue;
            sprite = CacheProvider_SpriteGet(app->provider, wev->config->minimap_sprite_id);
            if( !sprite || sprite->frame_count <= 0 )
            {
                struct ToriRS_Task* task =
                    CreateTask_SpriteLoad(app->provider, wev->config->minimap_sprite_id);
                if( task )
                    ToriRS_TaskQueue_Add(app->runner.queue, task);
                continue;
            }
            scene_id = UITreeSceneBridge_EnsureSprite(&app->bridge, wev->config->minimap_sprite_id);
            if( scene_id <= 0 )
                continue;
            {
                int before = app->minimap_dots.count;

                app_minimap_push_dot(
                    app,
                    wev->x - (world->_base_tile_x << 7) - px,
                    wev->z - (world->_base_tile_z << 7) - pz,
                    scene_id,
                    0);
                /* The icon spins with the hull (deob client.method2412: yaw
                 * counter-rotated by the camera; the sprite is authored
                 * bow-up, and yaw 0 sails south = bow down-screen). Set on
                 * the dot the push actually produced — the cull ring may
                 * have swallowed it. */
                if( app->minimap_dots.count > before )
                    app->minimap_dots.dots[app->minimap_dots.count - 1].rotate =
                        (ToriDraw_NormalizeAngle(app->world_camera.yaw) - wev->angle + 1024) &
                        0x7ff;
            }
        }
    }

    /* Loc mapfunction icons first, so entity dots draw on top (reference
     * minimapDraw order). Gathered at scene build into world->mapfuncs.
     * dat1: frame index into the mapfunction atlas. dat2/OSRS: mapelement id
     * → sprite (same path as the world map). */
    if( app->cfg.cache_kind == APP_CACHE_DAT1 )
    {
        int mapfunc_scene =
            UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPFUNCTION);
        if( mapfunc_scene > 0 )
        {
            for( int i = 0; i < world->mapfunc_count; i++ )
            {
                struct World_MapFunctionIcon const* icon = &world->mapfuncs[i];
                if( icon->level != cull_level )
                    continue;
                app_minimap_push_dot(
                    app,
                    icon->x * 128 + 64 - px,
                    icon->z * 128 + 64 - pz,
                    mapfunc_scene,
                    icon->func);
            }
        }
    }
    else
    {
        for( int i = 0; i < world->mapfunc_count; i++ )
        {
            struct World_MapFunctionIcon const* icon = &world->mapfuncs[i];
            int scene_id;
            if( icon->level != cull_level )
                continue;
            scene_id = app_mapfunction_scene_id(app, icon->func, NULL);
            if( scene_id <= 0 )
                continue;
            app_minimap_push_dot(
                app, icon->x * 128 + 64 - px, icon->z * 128 + 64 - pz, scene_id, 0);
        }
    }

    if( dots_scene > 0 )
    {
        pool = &world->entities.obj_stack;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, i);
            if( !stack || stack->grid_position.level != cull_level )
                continue;
            app_minimap_push_dot(
                app,
                stack->grid_position.x * 128 + 64 - px,
                stack->grid_position.z * 128 + 64 - pz,
                dots_scene,
                0);
        }
        pool = &world->entities.npc;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
            /*
             * TWO config flags, not one, and both are copied onto the entity
             * when its type resolves. Rev 239's `method2403`:
             *
             *   if (var8 != null && var8.isMinimapVisible() && var8.isInteractible())
             *
             * — opcode 93 AND opcode 107, on the transformed composition. Only
             * the first was read here, which is why the Theatre of Blood's
             * Nylocas supports drew four dots on the minimap: 8358 states
             * `interactable=no` and says nothing at all about opcode 93, so
             * the cache was right and the gate was half of one.
             */
            if( !npc || npc->multinpc_hidden || !npc->minimap_visible || !npc->interactable )
                continue;
            /* An aboard actor's draw position is deck-local (wire homing) —
             * push it out through the hull before differencing against the
             * viewer (the deob's Statics.method8690 transform). Its level is
             * a deck plane, incomparable with root levels, so the same-level
             * cull only applies to actors standing in the root. */
            {
                int fx = (int)npc->draw_position.x;
                int fz = (int)npc->draw_position.z;

                if( !app_wev_actor_root_fine(app, &npc->view_placement, &fx, &fz) &&
                    npc->grid_position.level != cull_level )
                    continue;
                app_minimap_push_dot(app, fx - px, fz - pz, dots_scene, 1);
            }
        }
        pool = &world->entities.player;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
            if( !player || player == local )
                continue;
            {
                int fx = (int)player->draw_position.x;
                int fz = (int)player->draw_position.z;

                if( !app_wev_actor_root_fine(app, &player->view_placement, &fx, &fz) &&
                    player->grid_position.level != cull_level )
                    continue;
                app_minimap_push_dot(app, fx - px, fz - pz, dots_scene, 2);
            }
        }
    }

    if( marker_scene > 0 && MinimapView_HasFlag(&app->minimap) )
        app_minimap_push_dot(
            app,
            app->minimap.flag_tile_x * 128 + 64 - px,
            app->minimap.flag_tile_z * 128 + 64 - pz,
            marker_scene,
            0);

    /* Local player: white 3x3 square at the widget center (fillRect 97,78). */
    {
        struct UITreeMinimapDot* dot = MinimapDots_Push(&app->minimap_dots);
        if( dot )
        {
            dot->dx = -1;
            dot->dy = -1;
            dot->w = 3;
            dot->h = 3;
            dot->color = 0xFFFFFFFFu;
        }
    }
    return app->minimap_dots.count;
}

/* Minimap click-to-walk (reference minimapLoop, Client.ts 2990-3032): map the
 * click through the same rotation the blit drew with into a player-relative
 * tile, then MOVE_MINIMAPCLICK with the 14-byte anticheat trailer. The sin/cos
 * tables are 16.16, so >>11 leaves fine units directly (32 fine units per
 * minimap pixel at 4 px/tile). Returns 1 when the click was consumed. */
int
app_minimap_click(
    struct App* app,
    int mouse_x,
    int mouse_y,
    int ctrl_held)
{
    struct WorldEntity_Player* player;
    struct MinimapRotation rotation;
    int center_x, center_y, yaw, rel_x, rel_y;
    int tile_x, tile_z;

    if( !app->world || !app->world->load_complete )
        return 0;
    /* Native permission, independent of whether the map is currently painted. */
    if( !(RS_MinimapPermissions(app->minimap_state) & RS_MINIMAP_WALK) )
        return 0;
    yaw = MinimapView_Yaw(&app->minimap);
    rotation.sin = ToriDraw_Sin(yaw);
    rotation.cos = ToriDraw_Cos(yaw);
    if( !MinimapView_ClickToFineOffset(
            &app->minimap, &rotation, mouse_x, mouse_y, &center_x, &center_y, &rel_x, &rel_y) )
        return 0;
    player = app_local_player(app);
    if( !player )
        return 0;

    if( app_sailing_can_steer(app) )
    {
        if( rel_x == 0 && rel_y == 0 )
            return 0;
        return app_sailing_send_heading(app, SailingNavigation_Heading(rel_x, -rel_y));
    }
    int player_x = (int)player->draw_position.x;
    int player_z = (int)player->draw_position.z;
    app_wev_actor_root_fine(app, &player->view_placement, &player_x, &player_z);
    tile_x = (player_x + rel_x) >> 7;
    tile_z = (player_z - rel_y) >> 7;
    if( tile_x < 0 || tile_z < 0 || tile_x >= app->world->_scene_size ||
        tile_z >= app->world->_scene_size )
        return 0;

    if( torirs_env_net_debug() )
        TORIRS_REPORT(
            "minimap: click=%d,%d rel=%d,%d scene=%d,%d abs=%d,%d\n",
            center_x,
            center_y,
            rel_x,
            rel_y,
            tile_x,
            tile_z,
            app->world->_base_tile_x + tile_x,
            app->world->_base_tile_z + tile_z);
    if( app->aboard_view != WORLDVIEW_ROOT && app->net )
    {
        int route_x[] = { tile_x }, route_z[] = { tile_z };
        /* As with viewport shore clicks, the server chooses a reachable deck
         * edge. Root BFS cannot start from a passenger's staging coordinates. */
        APP_NET_SEND(
            app,
            net_out_move_minimapclick(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                app->world->_base_tile_x,
                app->world->_base_tile_z,
                route_x,
                route_z,
                1,
                ctrl_held,
                center_x,
                center_y,
                yaw,
                0,
                0,
                player_x,
                player_z,
                0));
    }
    else
        app_try_move(app, tile_x, tile_z, 1, center_x, center_y, yaw, ctrl_held);
    app->need_redraw = 1;
    return 1;
}
