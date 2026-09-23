/*
 * The logic tick.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/*
 * The 20 ms client tick.
 *
 * Included into app.c rather than compiled on its own. One tick touches the
 * clock, the widget timers, the animation loads and advance, the net link, the
 * audio and the CS2 flush -- it is the orchestration app.c exists to hold, and
 * the split is for READING. Same translation unit, same statics, same order.
 */

/* One 20ms client tick: clock, widget timers, animation loads + advance. */
int
app_logic_tick(struct App* app)
{
    int redraw = 0;

    app->logic_cycle++;

    /*
     * The logout the player asked for, drained here rather than at the click.
     *
     * Order on the wire is the whole reason. The click's own notification --
     * the logout button's IF_BUTTON -- is sent by the CALLER of the clientCode
     * handler, after it returns, and the CS2 lane's is sent before the script
     * that asks for the logout even runs. A logout performed inside either
     * would put its DISCONNECT into the outbound ring ahead of the request the
     * server is meant to act on, and the transport, which drains that ring in
     * order, would close the socket without ever writing those bytes.
     *
     * Draining it arms a wait rather than ending the session: the server is
     * what ends it. @see app_logout_tick.
     */
    if( app_logout_tick(app) )
        redraw = 1;

    if( app_title_tick(app) )
        redraw = 1;
    if( app->flames )
        app_title_flames_tick(
            app, (uint64_t)app->logic_cycle * (1000 / APP_LOGIC_CYCLES_PER_SECOND));

    /*
     * System-update countdown (reference gameLoop: `if (rebootTimer > 1)
     * rebootTimer--`). Stops at 1, never 0 — 0 is "no update pending", and
     * counting into it would erase the warning at the moment it matters most.
     * Ahead of the early return below, because a busy exec runner must not be
     * able to stall a clock the server started.
     */
    if( app->reboot_timer > 1 )
    {
        app->reboot_timer--;
        redraw = 1;
    }

    /* Before the packet pump, so a handler that samples world state sees the
     * cycle it was told about rather than one already half-advanced by this
     * tick's packets. The 600ms server cadence is a different event
     * (on_server_tick, raised at the tick fence). */
    PluginHost_LogicTick(app->plugins, (int)app->logic_cycle);

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TICK_PACKETS)
    {
        if( app_pump_net_packets(app) )
            redraw = 1;
    }

    /* No widget hook may observe a half-applied packet/interface transaction.
     * The next logic tick resumes the serial runner; the shell keeps presenting
     * the preceding committed framebuffer in the meantime. */
    if( app->exec_runner_had_work || app->server_tick_open || RS_ClientScriptQueue_Count(&app->pending_clientscripts) > 0 )
        return redraw;

    /* Rasterize inventory item icons that the server's inv packets left
     * unresolved (queued on the same serial pipeline, so it runs after the
     * packets that dirtied the slots). */
    app_inv_icon_reconcile_tick(app);

    /* And release a burst of the placeholders a clientscript asked for: the
     * settle it ran in drains the same runner, so this tick is the frame
     * boundary between the script and its loads. */
    app_placeholder_release_tick(app);

    if( app->net )
    {
        /* Keepalive while in the game world, to stop the connection idling
         * out. This is an idle timer, not a heartbeat: the reference only
         * sends NO_TIMEOUT when nothing has gone out for a full second, and
         * any real packet resets the wait, so during normal play it never
         * fires at all. Sending it every logic tick instead put a 1-byte
         * WebSocket frame on the wire 50 times a second -- 487 of the 502
         * sends in a 10s trace of the web client, for 586 bytes total. */
        if( app->net->state == TORIRS_NET_GAME &&
            app->last_frame_ms - app->net_last_send_ms >= APP_NET_KEEPALIVE_MS )
            APP_NET_SEND(
                app,
                net_out_no_timeout(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf)));

        /* TORIRS_NET_CHEAT="tele 0,50,50,21,21;give bronze_sword": send ::
         * commands (';'-separated) right after login — headless harness hook
         * (the dev server grants staffmod, so tele/give work). The manifest's
         * `[net:boot] cheat=` is the same hook with env > manifest precedence.
         * TORIRS_NET_CHEAT_EVERY=N: re-send the same cheats every N logic
         * cycles (menu open/close churn in drift-ui). N<=0 keeps one-shot.
         *
         * Gated on the first world load, not just on login: the rev-239 mock
         * holds a login scene barrier until the client acks with MAP_BUILD_
         * COMPLETE and *discards* what arrives before it. A human cannot type
         * before the scene is up either, so this is the reference timing —
         * without it the one-shot boot cheat is silently eaten every run. */
        if( app->net->state == TORIRS_NET_GAME && app->world_active )
        {
            static int cheat_every = -1;
            char const* cheat = torirs_env_net_cheat();
            int fire = 0;

            if( !cheat || !cheat[0] )
                cheat = app->cfg.net_cheat;

            if( cheat_every < 0 )
            {
                char const* every = getenv("TORIRS_NET_CHEAT_EVERY");
                cheat_every = (every && every[0]) ? atoi(every) : 0;
            }
            if( cheat && cheat[0] )
            {
                if( !app->net_cheat_sent )
                    fire = 1;
                else if( cheat_every > 0 && (app->logic_cycle % cheat_every) == 0 )
                    fire = 1;
            }
            if( fire )
            {
                static int cheat_rotate = -1;
                static int cheat_index = 0;
                int rotate;
                int part_i;
                char const* exit_bmp = getenv("TORIRS_EXIT_BMP");
                char const* series = getenv("TORIRS_BMP_SERIES");

                app->net_cheat_sent = 1;
                /* Named / series dumps must not die into Lumbridge. `::god 1`
                 * first; a death-case capture sends `god 0` in NET_CHEAT. */
                if( (exit_bmp && exit_bmp[0]) || (series && series[0]) )
                {
                    if( !app_client_cheat(app, "god 1") )
                        APP_NET_SEND(
                            app,
                            net_out_client_cheat(
                                app->net->rev,
                                app->net->random_out,
                                _nsbuf,
                                sizeof(_nsbuf),
                                "god 1"));
                }
                if( cheat_rotate < 0 )
                    cheat_rotate = getenv("TORIRS_NET_CHEAT_ROTATE") != NULL;
                rotate = cheat_rotate;

                /* Rotate mode: fire one semicolon-separated command per shot so
                 * soak-ui can open a different panel each EVERY cycle. Default
                 * still fires the whole list (tele;give;…). */
                part_i = 0;
                while( cheat && cheat[0] )
                {
                    char one[96] = { 0 };
                    char const* sep = strchr(cheat, ';');
                    size_t len = sep ? (size_t)(sep - cheat) : strlen(cheat);
                    char const* body;
                    int take = 1;
                    if( len >= sizeof(one) )
                        len = sizeof(one) - 1;
                    memcpy(one, cheat, len);
                    body = one;
                    if( body[0] == ':' && body[1] == ':' )
                        body += 2;
                    if( rotate )
                    {
                        take = (part_i == cheat_index);
                        part_i++;
                    }
                    if( take && body[0] )
                    {
                        if( app_client_cheat(app, body) )
                            ;
                        else
                        {
                            APP_NET_SEND(
                                app,
                                net_out_client_cheat(
                                    app->net->rev,
                                    app->net->random_out,
                                    _nsbuf,
                                    sizeof(_nsbuf),
                                    body));
                        }
                        if( rotate )
                            break;
                    }
                    cheat = sep ? sep + 1 : NULL;
                }
                if( rotate )
                {
                    int parts = 0;
                    char const* p = torirs_env_net_cheat();
                    if( !p || !p[0] )
                        p = app->cfg.net_cheat;
                    while( p && p[0] )
                    {
                        char const* sep = strchr(p, ';');
                        parts++;
                        p = sep ? sep + 1 : NULL;
                    }
                    if( parts > 0 )
                        cheat_index = (cheat_index + 1) % parts;
                }
            }
        }
    }

    /* Zone sub-packets queued during an async world load drain here once the
     * load completes — without this a queue with no follow-up zone traffic
     * would sit forever (the lazy flush only runs ahead of a live packet). */
    if( app->pending_zone_count > 0 && app->world && app->world->load_complete )
    {
        struct RS_GameProtoCtx flush_ctx = {
            .tree = app->tree,
            .invs = &app->invs,
            .varps = &app->varps,
            .stats = &app->stats,
            .chat = &app->chat,
            .app = app,
        };
        RS_GameProto_FlushPendingZone(&flush_ctx);
    }

    /* Sound queue on the client tick: the server's play delays are in ticks and
     * the reference runs its queue from the same clock (soundsDoQueue). */
    /*
     * THE LISTENER IS THE PLAYER, NOT THE EYE.
     *
     * This used to read `app->world_camera_pos`, on the reasoning that the
     * camera is what the stereo field is built around. The reference disagrees,
     * and the difference is not cosmetic. Its queued-effect drain measures
     *
     *     var24 = abs(sound_x - method7247())      // client.field1052
     *     var25 = abs(sound_z - client.field872)
     *     var26 = max(var24 + var25 - 128, 0)
     *
     * and `field1052`/`field872` are the camera's TARGET -- the local player's
     * position, the pair that `Statics.field2354`/`field1208` (the eye) lerp
     * toward one sixteenth at a time. The eye is the target pushed back along
     * the yaw by the zoom distance, and in this client that is about nine tiles
     * in a default follow view.
     *
     * Nine tiles of error is fatal rather than sloppy, because a sequence frame
     * sound carries its own audible radius in the seq's `location` field and
     * those radii are small: all four of Xarpus' are 7. Measured from the eye,
     * a boss the player is standing next to is sixteen tiles from the listener,
     * `falloff_volume` returns 0, and `play_entry` drops the voice as out of
     * earshot -- silently, and for every positional sound in the game at once.
     * Measured here: 23 of Xarpus' spit and wing sounds queued in one P2, and
     * not one of them ever became a voice, while every non-positional
     * `sound_synth` in the same run played.
     *
     * Falls back to the eye when there is no local player -- a cutscene camera
     * with nobody to follow -- and to the origin before a world exists.
     */
    {
        int listener_x = 0;
        int listener_z = 0;
        int listener_level = 0;
        if( app->world )
        {
            /* World units are tiles << 7 (the reference's coord scale). */
            struct WorldEntity_Player* local = app_local_player(app);
            if( local )
            {
                int lfx = (int)local->draw_position.x;
                int lfz = (int)local->draw_position.z;

                /* Aboard, the ears are where the boat carries them — the same
                 * push-out-through-the-hull the camera and minimap use. */
                app_wev_actor_root_fine(app, &local->view_placement, &lfx, &lfz);
                listener_x = lfx >> 7;
                listener_z = lfz >> 7;
            }
            else
            {
                listener_x = app->world_camera_pos.x >> 7;
                listener_z = app->world_camera_pos.z >> 7;
            }
            listener_level = app_cinema_level(app);
        }
        RS_Audio_Tick(
            &app->audio,
            app->provider,
            &app->runner,
            app->scene,
            app->world,
            listener_x,
            listener_z,
            listener_level,
            &app->audio_feedback,
            &app->audio_out);

        /* A music request the player has accepted but no loader has picked up
         * yet becomes a task here, once. The player refuses to hand out the
         * same request twice, so this cannot pile up. */
        if( app->provider && !app->audio.music_loading )
        {
            int song_id = -1;
            enum ToriRS_MusicSource source = TORIRS_MUSIC_SOURCE_TRACK;
            if( ToriRS_Music_TakeLoadRequest(&app->audio.music, &song_id, &source) )
            {
                struct ToriRS_Task* task =
                    CreateTask_MusicLoad(app->provider, &app->audio.music, song_id, (int)source);
                if( task )
                    ToriRS_TaskQueue_Add(app->runner.queue, task);
                else
                    ToriRS_Music_LoadFailed(&app->audio.music, song_id);
            }
        }
    }

    /* Where the player is standing, for the scripts that branch on it. Packed
     * as the cache's coord is - plane<<28 | x<<14 | z - from the local player's
     * scene tile plus the scene's base, because a script compares it against
     * absolute world regions. Refreshed here, once a frame, beside the clock
     * for the same reason: both are things the VM reads and neither belongs to
     * any one script's call. */
    {
        /* -1, not 0, when there is no local player: the reference's opcode 3308
         * pushes -1 when its coord source is absent or invalid. */
        int coord = -1;
        if( app->world && app->world->load_complete )
        {
            struct WorldEntity_Player* self =
                World_PlayerGetByServerPid(app->world, app->world->local_pid);
            if( self )
            {
                int const x = app->world->_base_tile_x + self->grid_position.x;
                int const z = app->world->_base_tile_z + self->grid_position.z;
                int const level = self->grid_position.level & 3;
                /* `Statics.method6754(plane, x, z) = plane << 28 | x << 14 | z`,
                 * over ABSOLUTE coords - the reference stores the player's
                 * absolute tile and subtracts the scene base where it wants a
                 * local one, so this is the same number its scripts read. In a
                 * dynamic instance that is the instance's own tile, exactly as
                 * the reference reports it there; no template translation. */
                coord = (level << 28) | ((x & 0x3fff) << 14) | (z & 0x3fff);
            }
        }
        app->host.local_coord = coord;
        /* LOCALPLAYER_GETUID, and the other half of every
         * ACTIVEPLAYER_GETUID = LOCALPLAYER_GETUID comparison a per-player
         * trigger script opens with. Published here beside the coord because
         * it is the same fact about the same player and neither belongs to a
         * script's call. */
        app->host.local_pid =
            (app->world && app->world->load_complete) ? app->world->local_pid : -1;
    }

    /*
     * The three tile-highlight TRIGGERS, and the edges that fire them.
     *
     * Clientscripts 5197 / 5203 / 5209 mark the hovered, current and
     * destination tile. Each is a `[trigger_4x]` that clears its own group and
     * re-adds one tile, reading that tile from the context the client sets
     * first -- so the client's whole job is (a) to notice the edge, (b) to set
     * the active player and the active tile, and (c) to fire the script. The
     * cache calls none of them; the reference fires each from one place:
     *
     *     trigger_48 / 5197  Client::GlUpdateMouseOverTile   the ground tile
     *                                                        under the pointer
     *                                                        changed
     *     trigger_49 / 5203  ReceivePlayerPositions and
     *                        Client::GlMovePlayers           a player's ROUTE
     *                                                        changed
     *     trigger_47 / 5209  Client::SetPlayerDestination    the minimap flag
     *                                                        moved
     *
     * Edge-triggered and not per-frame, which is the reference's shape too:
     * three dispatches and six highlight ops a frame to restate what has not
     * moved is not free, and both the pointer and the player sit still for
     * most frames.
     *
     * What this client does NOT do is fan trigger_49 out over every player.
     * The reference fires it for each player whose route the packet updated
     * and each one that consumed a step this cycle; 5203, the only script in
     * this cache that is a trigger_49, opens by comparing ACTIVEPLAYER_GETUID
     * with LOCALPLAYER_GETUID and returns for anyone but the local player. A
     * fan-out would be a script dispatch per moving player per cycle to reach
     * that same early return. The opcodes it would need are all here, so a cache
     * that starts marking other players' true tiles needs the loop and nothing
     * else.
     */
    {
        int dest_coord = -1;
        int hover_coord = -1;

        if( app->world && app->world->load_complete && app->minimap.flag_tile_x >= 0 )
        {
            int const x = app->world->_base_tile_x + app->minimap.flag_tile_x;
            int const z = app->world->_base_tile_z + app->minimap.flag_tile_z;
            struct WorldEntity_Player* self =
                World_PlayerGetByServerPid(app->world, app->world->local_pid);
            int const level = self ? (self->grid_position.level & 3) : 0;
            dest_coord = (level << 28) | ((x & 0x3fff) << 14) | (z & 0x3fff);
        }
        if( app->world && app->world_hover_tile_x >= 0 && app->world_hover_tile_z >= 0 )
        {
            int const x = app->world->_base_tile_x + app->world_hover_tile_x;
            int const z = app->world->_base_tile_z + app->world_hover_tile_z;
            int const level = World_TerrainWalkLevel(
                                  app->world,
                                  app->world_hover_tile_x,
                                  app->world_hover_tile_z,
                                  app->world_hover_tile_level) &
                              3;
            hover_coord = (level << 28) | ((x & 0x3fff) << 14) | (z & 0x3fff);
        }

        app->host.dest_coord = dest_coord;
        app->host.hover_coord = hover_coord;

        /*
         * What the pointer is on is the ACTING ROW's subject, and the row is
         * published where the rest of the row is -- app_minimenu_entry_publish,
         * off the same scratch menu the hover line is composed from.
         *
         * This used to walk `world_pickset` here and publish its nearest
         * non-terrain hit. That made `_7100` and the four FIND ops answer about
         * a DIFFERENT entry from the one `_7101` and `_7109` answer about, and
         * the two disagree whenever the priority sort moves a row: measured on
         * the Lumbridge fixture at 330,120 the hover line reads "Talk-to Romeo"
         * while the pickset's first hit is loc 7143 "Fountain", so the cache's
         * mouse-over highlighter outlined the fountain the pointer was not on.
         * RS_ClientOpState already says why the pickset is the wrong source for
         * an entry field; the subject is an entry field like the other three.
         *
         * `menu_open` stays here: it is a fact about the popup, not about a row.
         */
        app->host.clientop.menu_open = app->interact.minimenu.visible;

        /*
         * Publishing is ALL the client does here.
         *
         * The cache drives the mouseover highlighter itself: clientscript 4726
         * is re-armed on a gameframe component timer and calls 5350 every tick.
         * Measured -- with the client's own edge-triggered call removed, 5350
         * still ran 89 times over the same window. Adding one would be a second
         * driver for an idempotent script, which is only waste.
         *
         * What 5350 does with it is the CACHE's decision and not this client's:
         * it reads %varbit13088 ("Highlight entities on mouse-over", All
         * Settings > Activities row 190) and returns on the spot when that is
         * 0, which is its shipped default. A capture that wants to see the
         * highlighter has to turn the row on, exactly as the hovered-tile
         * captures turn %varbit12977 on.
         *
         * The three TILE refreshers are different and are driven below:
         * nothing in the cache calls those at all.
         */

        /*
         * Only once the world is up: before that the scripts would clear a
         * group and re-add a tile from a scene that is about to be replaced,
         * and the group is rebuilt by the login initialiser anyway.
         *
         * And only where those clientscripts exist. The three refreshers are
         * CS2 ids out of a dat2 cache; a dat1 boot has no clientscript table at
         * all, and asking its provider for one is a contract violation that
         * aborts (task_dat1_clientscript_load.c). The highlighter simply does
         * not apply to a CS1 world -- the cache that would drive it is not the
         * cache being read.
         */
        if( app->world && app->world->load_complete && App_UiLogic(app) == APP_UI_LOGIC_CS2 )
        {
            int const route = app_cs2_local_route_signature(app);

            /*
             * The hovered tile. Fired only for a REAL tile, which is the
             * reference's own rule -- GlUpdateMouseOverTile returns before the
             * dispatch when the pointer is not over ground, leaving the last
             * hovered tile marked while the pointer is over the interface.
             * Firing it with no tile would run 5197's `tile_on(_6950, 5, 0)`
             * on a coord of -1 and put tile (3, 16383, 16383) in the group.
             */
            if( hover_coord != app->highlight_last_hover_coord )
            {
                app->highlight_last_hover_coord = hover_coord;
                if( hover_coord >= 0 )
                {
                    app_cs2_set_active_player(app, app->world->local_pid);
                    app_cs2_set_active_tile(app, hover_coord);
                    RS_CS2_RunScript(
                        &app->host,
                        &app->runner,
                        app->host.script_highlight_hover_tile,
                        NULL,
                        0,
                        0,
                        NULL,
                        0);
                }
            }
            /* The current tile: the local player's route changed, which is
             * both of the reference's trigger_49 edges. 5203 reads the route
             * itself -- ACTIVEPLAYER_GETROUTECOORD(0) while walking, `coord`
             * while still -- so
             * the active player is the whole of what it needs. */
            if( route != app->highlight_last_route )
            {
                app->highlight_last_route = route;
                app_cs2_set_active_player(app, app->world->local_pid);
                RS_CS2_RunScript(
                    &app->host,
                    &app->runner,
                    app->host.script_highlight_current_tile,
                    NULL,
                    0,
                    0,
                    NULL,
                    0);
            }
            /*
             * The destination tile: the minimap flag moved.
             *
             * When it moves to NOWHERE -- arrival, or a teleport out from
             * under it -- the tile fired with is the player's own, which is
             * the truth (the walk ended where they are standing) and is the
             * case 5209 answers by clearing group 4 and adding nothing:
             * `if (... | _6950 = coord) return` sits after its `_7039(4)`.
             *
             * The reference gets there by clearing the flag to scene tile
             * (0, 0) and firing with THAT, which marks the corner of the map
             * square -- always ~50 tiles from a player who is always near the
             * middle of their own scene, so never a tile anyone sees. Firing
             * with the player's tile reaches the same cleared group without
             * putting a member nobody asked for in it.
             */
            if( dest_coord != app->highlight_last_dest_coord )
            {
                int const fire_coord = dest_coord >= 0 ? dest_coord : app->host.local_coord;
                app->highlight_last_dest_coord = dest_coord;
                if( fire_coord >= 0 )
                {
                    app_cs2_set_active_player(app, app->world->local_pid);
                    app_cs2_set_active_tile(app, fire_coord);
                    RS_CS2_RunScript(
                        &app->host,
                        &app->runner,
                        app->host.script_highlight_dest_tile,
                        NULL,
                        0,
                        0,
                        NULL,
                        0);
                }
            }
        }
    }

    app_ground_items_tick(app);

    RS_CS2Host_Tick(&app->host);

    app_cs2_flush_notifications(app);

    /* clientCode-populated components (friends rows, list sizes, design
     * preview) refresh from live state each tick (reference clientComponent
     * runs inside the draw; ours is a tick pass so emit stays pure). This is an
     * old-gen (IF1/CS1) mechanism; modern UI drives the same state via CS2. */
    if( App_UiLogic(app) == APP_UI_LOGIC_CS1 &&
        RS_ClientCode_Tick(app, app->tree, &app->social, app->logic_cycle) )
        redraw = 1;

    /* World map panning and element flashing advance on the client tick, the
     * same clock the map's own onTimer scripts run on. */
    if( RS_WorldMap_Cycle(app->host.worldmap) )
        redraw = 1;

    /* onTimer fires once per client tick for every component with a timer
     * hook (reference processWidgetTimers). Walk the live timer_hooks set —
     * maintained at ApplyRuntimeHook / reclaim — instead of scanning every
     * component every tick.
     *
     * Hidden / unmounted packs stay in the tree (IF_CLOSESUB hides rather than
     * reclaiming so a remount reuses dynamic children). Skip them the same way
     * inv/var/stat transmit dispatch does via ComponentOrAncestorHidden — a
     * closed bank's timers must not keep running every tick. */
    {
        int timer_n = app->tree->timer_hooks.count;
        if( timer_n > 256 )
            timer_n = 256;
        for( int i = 0; i < timer_n; i++ )
        {
            int32_t idx = app->tree->timer_hooks.slots[i];
            int com_id;
            assert(idx >= 0 && (uint32_t)idx < app->tree->component_count);
            com_id = app->tree->components[idx].component_id;
            if( com_id < 0 )
                continue;
            if( UITree_ComponentOrAncestorHidden(app->tree, com_id) )
                continue;
            RS_CS2_DispatchHook(
                &app->host,
                &app->runner,
                com_id,
                &UITree_Hooks(&app->tree->components[idx])->on_timer);
            /*
             * Unconditional, and it has to stay that way until the emit walk
             * reads the dirty bit.
             *
             * `need_redraw` is the sole gate on `UITree_EmitWalk`, and that walk
             * is dirty-unaware — uitree_emit.c never reads `is_dirty` and never
             * calls `UITree_NodeNeedsEmit`, so it rebuilds the whole list from
             * whatever the components currently say. Narrowing this to "the
             * dispatch changed something" therefore needs a signal covering
             * every input emit reads, not just the ones the appliers write.
             *
             * That is not, as this comment previously claimed, a problem of
             * unmarked writers: component writes outside ui/uitree.c come to 12
             * lines in 5 files, and the ones on the host path already call
             * `UITree_MarkNodeDirty` (rs_cs2_host.c:4789). The problem is that
             * emit reads well beyond the component struct — 32 `UITree_Host(...)`
             * calls in uitree_emit.c, plus inventory, hover, drag and varp state.
             * A node's emitted output goes stale when an inventory changes behind
             * it, with nothing written to the node at all, so no per-node dirty
             * bit can gate this walk however faithfully it is maintained. What
             * that needs is per-node dependency tracking (target 11), and a
             * partial version returns a stale panel rather than a missed
             * optimisation.
             *
             * The compare-first appliers (ui/uitree.c) still pay for themselves
             * here by not doing the strdup/free round trip, but they cannot make
             * a quiet frame skip the emit while the walk ignores what they
             * decided. That gate is the tree-side work, not this line.
             */
            redraw = 1;
        }
    }

    /* TORIRS_XPDROP_DEBUG=1: the XP-drop panel's decisive state, printed when it
     * changes. The panel (interface 122) draws each drop HIDDEN and relies on
     * the row's own onTimer (script1005) to reveal it, so "no drops" has three
     * distinguishable causes and this line separates them:
     *   - the stat hook stopped firing        (seen == serial while xp arrives)
     *   - the script declined to draw         (serial advances, no row children)
     *   - the row was drawn but never shown   (children present, timer=0/hidden)
     * vc71 is the panel's own throttle (next scheduled clientclock); a vc71 far
     * ahead of clock means every drop is being discarded by script1004. */
    if( app_xpdrop_debug() )
        app_xpdrop_debug_tick(app);

    /* Interface 116's four audio sliders arrive as CS2 option writes. The host
     * coalesces drag events; apply the latest complete snapshot here so VM
     * rollback cannot leak an audio side effect and the App remains the sole
     * owner of audio_out. Percentages are rounded onto the mixer's 0..255
     * domain, then master is multiplied into each bus by RS_Audio. */
    {
        struct RS_CS2AudioSettings settings;
        if( RS_CS2Host_TakeAudioSettings(&app->host, &settings) )
        {
            int master = (settings.master * TORIRS_AUDIO_VOLUME_MAX + 50) / 100;
            int music = (settings.music * TORIRS_AUDIO_VOLUME_MAX + 50) / 100;
            int sounds = (settings.sounds * TORIRS_AUDIO_VOLUME_MAX + 50) / 100;
            int area = (settings.area_sounds * TORIRS_AUDIO_VOLUME_MAX + 50) / 100;

            if( getenv("TORIRS_AUDIO_TRACE") || getenv("TORIRS_AUDIO_DEBUG") )
                TORIRS_LOG(
                    "audio settings: master %d%%, music %d%%, effects %d%%, area %d%% "
                    "-> buses %d/%d/%d\n",
                    settings.master,
                    settings.music,
                    settings.sounds,
                    settings.area_sounds,
                    music * master / TORIRS_AUDIO_VOLUME_MAX,
                    sounds * master / TORIRS_AUDIO_VOLUME_MAX,
                    area * master / TORIRS_AUDIO_VOLUME_MAX);
            RS_Audio_SetMasterVolume(&app->audio, master, &app->audio_out);
            RS_Audio_SetBusVolume(&app->audio, TORIRS_AUDIO_BUS_MUSIC, music, &app->audio_out);
            RS_Audio_SetBusVolume(&app->audio, TORIRS_AUDIO_BUS_EFFECTS, sounds, &app->audio_out);
            RS_Audio_SetBusVolume(&app->audio, TORIRS_AUDIO_BUS_AREA, area, &app->audio_out);
        }
    }

    /*
     * Mirror the option store to disk once the player has stopped moving it.
     *
     * Every path that changes a setting lands in the option store first (the
     * sliders through GAMEOPTION/DEVICEOPTION_SET, the mute icons through
     * their varps and RS_CS2Host_SyncAudioVarp), so one comparison here catches
     * all of them and nothing has to remember to call a save.
     *
     * The delay is what makes a drag one write rather than fifty: the bobble
     * reports a new value every 20ms tick, and each would otherwise be a
     * separate file rewrite. Anything still pending is flushed by App_Shutdown.
     */
    if( app->prefs_path )
    {
        if( RS_Prefs_CaptureFromHost(&app->prefs, &app->host) )
            app->prefs_dirty_cycle = app->logic_cycle;
        else if(
            app->prefs_dirty_cycle &&
            app->logic_cycle - app->prefs_dirty_cycle >= APP_PREFS_SAVE_SETTLE_TICKS )
        {
            /* Queued, not written here: the write is the platform's, and this
             * is the middle of a frame. */
            ToriRS_TaskQueue_Add(
                app->runner.queue, CreateTask_PrefsSave(&app->prefs, app->prefs_path));
            app->prefs_dirty_cycle = 0;
        }
    }

    /* Plugin settings, on the same settle delay and for the same reason: a
     * script writing state every tick (a tag list being edited, a counter)
     * must not be fifty file rewrites a second. */
    if( app->plugins && app->plugin_prefs_path )
    {
        if( PluginHost_ConfigDirty(app->plugins) )
        {
            PluginHost_ConfigClearDirty(app->plugins);
            app->plugin_prefs_dirty_cycle = app->logic_cycle;
        }
        else if(
            app->plugin_prefs_dirty_cycle &&
            app->logic_cycle - app->plugin_prefs_dirty_cycle >= APP_PREFS_SAVE_SETTLE_TICKS )
        {
            ToriRS_TaskQueue_Add(
                app->runner.queue, CreateTask_PluginSave(app->plugins, app->plugin_prefs_path));
            app->plugin_prefs_dirty_cycle = 0;
        }
    }

    /*
     * Sounds CS2 asked for this tick.
     *
     * Drained here rather than played from inside the VM so a script that
     * yields and rolls back has not already made a noise, and so the audio
     * queue is only touched from the App's own tick. Fades arrive in client
     * cycles and are converted here, the same 20ms/cycle the MIDI_* packet
     * readers use -- both paths reach the same music player, so a track a
     * script starts and one the server starts must behave identically.
     */
    {
        struct RS_CS2Sound sound;
        int guard = 0;

        while( guard++ < RS_CS2_HOST_SOUND_MAX * 2 && RS_CS2Host_TakeSound(&app->host, &sound) )
        {
            switch( sound.kind )
            {
            case RS_CS2_SOUND_SYNTH:
                RS_Audio_Synth(&app->audio, sound.id, sound.loops, sound.delay);
                break;
            case RS_CS2_SOUND_SONG:
                App_PlaySong(
                    app, sound.id, true, sound.fade_out_speed * 20, sound.fade_in_speed * 20);
                break;
            case RS_CS2_SOUND_JINGLE:
                App_PlayJingle(app, sound.id, sound.delay * 20);
                break;
            case RS_CS2_SOUND_SONG_WITHSECONDARY:
                App_PlaySongWithSecondary(
                    app,
                    sound.id,
                    sound.secondary_id,
                    sound.fade_out_speed * 20,
                    sound.fade_in_speed * 20);
                break;
            default:
                break;
            }
        }
    }

    /* if_callonresize / cc_triggerop requests are part of the CS2 visual
     * transaction that raised them.  Queue them here on ordinary ticks; the
     * pre-emit settle loop below repeats this pump to a fixed point. */
    if( app_cs2_enqueue_followups(app) )
        redraw = 1;

    app_cs2_flush_triggeroplocal(app);

    /* CS1 (IF1) value scripts drive active state and %N text. The reference
     * re-evaluates them at draw time; here a task does it once per tick so the
     * VM's asset yields can be serviced asynchronously, and the emit pass just
     * reads the cached results (the task sets need_redraw on change). */
    app_request_cs1_eval(app);

    /* TORIRS_STATS=1: periodic growth diagnostics — component_count must stay
     * flat under the CC_DELETEALL/CC_CREATE rebuild pattern (reclamation).
     * TORIRS_IFACE_STATS=1: per-group open/close ledger (names the panel). */
    {
        static int stats_enabled = -1;
        static int stats_tick = 0;
        if( stats_enabled < 0 )
            stats_enabled = getenv("TORIRS_STATS") != NULL;
        stats_tick++;
        /* Gauges are last-sample-wins, and this block walks the whole
         * component array. Sampling it every tick cost ~6% of the measured CS2
         * time on the XP baseline while producing values nothing read; the
         * perf module now says when a sample will actually be reported. */
        if( TorirsPerf_GaugeSampleDue(TORIRS_PERF_GAUGE_SITE_IFACE_STATS) ||
            (stats_enabled && stats_tick % 250 == 0) )
        {
            UITreeIfaceStats_SampleGauges(app->tree);
            TORIRS_PERF_COUNT_SET(
                TORIRS_PERF_CTR_HOST_INV_HOOKS, app->host.inv_transmit_hook_count);
            TORIRS_PERF_COUNT_SET(
                TORIRS_PERF_CTR_HOST_VAR_HOOKS, app->host.var_transmit_hook_count);
            TORIRS_PERF_COUNT_SET(
                TORIRS_PERF_CTR_HOST_STAT_HOOKS, app->host.stat_transmit_hook_count);
            if( app->bridge.sprite_map )
                TORIRS_PERF_COUNT_SET(
                    TORIRS_PERF_CTR_BRIDGE_SPRITE_MAP, (int64_t)app->bridge.sprite_map->size);
            if( app->bridge.model_map )
                TORIRS_PERF_COUNT_SET(
                    TORIRS_PERF_CTR_BRIDGE_MODEL_MAP, (int64_t)app->bridge.model_map->size);
            if( app->bridge.obj_icon_map )
                TORIRS_PERF_COUNT_SET(
                    TORIRS_PERF_CTR_BRIDGE_OBJ_ICON_MAP, (int64_t)app->bridge.obj_icon_map->size);
            if( app->provider && app->provider->clientscript_cache )
                TORIRS_PERF_COUNT_SET(
                    TORIRS_PERF_CTR_CACHE_CLIENTSCRIPT_SIZE,
                    (int64_t)app->provider->clientscript_cache->size);
        }
        UITreeIfaceStats_Tick(app->tree, stats_tick);
        if( stats_enabled && stats_tick % 250 == 0 )
        {
            uint32_t hidden = 0;
            uint32_t freed = 0;
            uint32_t live = 0;
            int timers = app->tree->timer_hooks.count;
            int timers_hidden = 0;
            UITREE_SCAN_METER(app->tree);
            for( uint32_t i = 0; i < app->tree->component_count; i++ )
            {
                struct UITreeComponent const* c = &app->tree->components[i];
                if( c->freed )
                {
                    freed++;
                    continue;
                }
                if( c->component_id < 0 )
                    continue;
                live++;
                if( c->behavior.hide )
                    hidden++;
            }
            for( int i = 0; i < timers; i++ )
            {
                int32_t tidx = app->tree->timer_hooks.slots[i];
                int com_id = app->tree->components[tidx].component_id;
                if( com_id >= 0 && UITree_ComponentOrAncestorHidden(app->tree, com_id) )
                    timers_hidden++;
            }
            TORIRS_LOG(
                "torirs_stats: tick=%d components=%u live=%u hidden=%u freed=%u "
                "free_head=%d inv_hooks=%d var_hooks=%d timers=%d timers_hidden=%d "
                "iface_parents=%d\n",
                stats_tick,
                app->tree->component_count,
                live,
                hidden,
                freed,
                app->tree->free_head,
                app->host.inv_transmit_hook_count,
                app->host.var_transmit_hook_count,
                timers,
                timers_hidden,
                app->tree->interface_parent_count);
        }
    }

    {
        static int anim_dbg = -1;
        static int anim_dbg_tick = 0;
        if( anim_dbg < 0 )
            anim_dbg = getenv("TORIRS_ANIM_DEBUG") != NULL;
        if( anim_dbg && ++anim_dbg_tick % 25 == 0 )
        {
            UITREE_SCAN_METER(app->tree);
            for( uint32_t i = 0; i < app->tree->component_count; i++ )
            {
                struct UITreeComponent const* node = &app->tree->components[i];
                if( node->freed || node->type != UIELEM_RS_MODEL )
                    continue;
                if( node->u.rs_model.anim_seq_id < 0 )
                    continue;
                TORIRS_LOG(
                    "anim_tick t=%d com=0x%x seq=%d frame=%d gen=%u\n",
                    anim_dbg_tick,
                    node->component_id,
                    node->u.rs_model.anim_seq_id,
                    node->u.rs_model.anim_frame,
                    app->tree->generation);
            }
        }
    }

    /* The live local-player figure's angles + animation frame, before the
     * advance below poses whatever it just (re)bound. */
    app_player_model_poll(app);

    /* Animations: request missing sequences (async), apply what's loaded.
     * In-flight sequences render at rest pose until they land. */
    UITreeAnim_RequestMissing(
        app->tree, app->scene, app->provider, app->runner.queue, &app->seq_loads);
    if( UITreeAnim_Advance(app->tree, app->scene, 1) )
        redraw = 1;

    /* CS2 hooks this tick may have ensured new textured models. */
    app_sync_textures(app);

    if( UICross_IsActive(&app->cross) )
    {
        UICross_Tick(&app->cross, APP_LOGIC_TICK_MS);
        redraw = 1;
    }

    /* The inkwell is NOT ticked here -- see App_RunOnce, where it is advanced
     * once per rendered frame by the real elapsed time. This function runs
     * 0..APP_MAX_CATCHUP_TICKS times per frame, which is right for the
     * simulation and wrong for anything the user watches. */

    return redraw;
}

