/*
 * The network session: link loss and reconnect, packet pumping, the outbound
 * packet helpers, and logout.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"
#include "boot_telemetry.h"

/* Private to this unit, declared up front so definition order is free. */
static bool
app_net_drop_requested(
    struct App const* app,
    uint64_t now_ms);
static void
app_net_tear_down_session(
    struct App* app,
    char const* why);

/*
 * The client-side cheats, in one place.
 *
 * `lootkill <source> <obj> [qty]` seeds the loot store as a kill would; it is
 * answered here because no server knows the client's loot store. Every path a
 * cheat can arrive by -- typed into the chat line, the login cheat list, or
 * the headless TORIRS_SIM_CMD sender -- asks this first, so a headless run and
 * a typed command reach the same code. True when the text was consumed.
 */
bool
app_client_cheat(
    struct App* app,
    char const* body)
{
    assert(app);
    assert(body);
    if( strncmp(body, "lootkill ", 9) == 0 )
    {
        char lk_source[64] = { 0 };
        int lk_obj = 0;
        int lk_qty = 1;
        if( sscanf(body + 9, "%63s %d %d", lk_source, &lk_obj, &lk_qty) >= 2 )
        {
            if( lk_qty <= 0 )
                lk_qty = 1;
            App_LootNotifyKill(app, lk_source, lk_obj, lk_qty);
        }
        return true;
    }
    return false;
}

bool
App_SendCommand(
    struct App* app,
    char const* text)
{
    assert(app);
    assert(text);
    if( !*text )
        return false;
    /* Reports whether it went out. The caller cannot know when login finishes
     * — the world renders before the connection reaches GAME — so a harness
     * that fires once at a chosen frame silently sends nothing. Returning the
     * verdict lets it retry until the send lands. */
    if( !app->net || app->net->state != TORIRS_NET_GAME )
        return false;
    if( app_client_cheat(app, text) )
        return true;
    APP_NET_SEND(
        app,
        net_out_client_cheat(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), text));
    return true;
}

/*
 * Boot / session-reset value for the two Attack options.
 *
 * Era-dependent, and the two answers are opposites. A settings-era client boots
 * both at Hidden and only leaves that state when varp clientcode 18/22 arrives
 * (rs_attack_option.h), so zeroing them there would left-click-attack against a
 * server that never sends the setting. A 2004-era client has no such setting to
 * send: Client-TS emits the Attack row unconditionally, so Hidden there hides
 * every Attack row on every NPC and player for the whole session.
 *
 * Called after the feature table is resolved, never from the memset above it.
 */
void
app_attack_options_reset(struct App* app)
{
    assert(app);
    assert(app->features);
    if( app->features->attack_option_model == TORIRS_ATTACK_OPTION_MODEL_SETTINGS )
    {
        app->player_attack_option = RS_ATTACK_OPTION_DEFAULT;
        app->npc_attack_option = RS_ATTACK_OPTION_DEFAULT;
    }
    else
    {
        app->player_attack_option = RS_ATTACK_OPTION_DEPENDS;
        app->npc_attack_option = RS_ATTACK_OPTION_DEPENDS;
    }
}

/* --- connection loss and re-establishment -------------------------------
 *
 * The reference shape, from Client-TS `lostCon` (Client.ts:2734) and the deob's
 * gameState 40: forget the world, say so over the viewport, and ask for the
 * session back. An exhausted retry budget leaves that message up rather than
 * dropping the player on the title screen, which is the one place this path
 * departs from the reference -- the message names something the player can act
 * on, and the title screen would replace it with a form that says nothing.
 *
 * A logout is the other half of the same reference pair and is NOT this: it is
 * deliberate, it does return to the login screen, and it disarms everything
 * below rather than arming it. @see App_Logout.
 */

void
App_NetSessionReset(struct App* app)
{
    assert(app);
    RS_EntitySync_Clear(&app->esync, app->world);
    /* The reference's game-state reset puts both Attack options back to their
     * boot value rather than recomputing them from the varp table it is about
     * to clear (rs_attack_option.h): a re-established session onto an account
     * whose setting is the default 0 would otherwise keep the previous
     * session's choice until its own VARP arrived. */
    app_attack_options_reset(app);
    /* The reference drops the friends chat, both clans and the Grand Exchange
     * offers when the next login response arrives; doing it as the session
     * ends is the same state before that session's first packet, and cannot
     * race the login burst that refills them. */
    RS_CS2Host_ResetSocialForLogin(&app->host);
    app->need_redraw = 1;
}

void
App_Logout(struct App* app)
{
    assert(app);

    app->logout_requested = 0;
    app->logout_wait_cycles = 0;
    /*
     * The DISCONNECT is QUEUED, not performed: it goes into the same outbound
     * ring the logout button's IF_BUTTON is already sitting in, and the
     * transport drains that ring in order -- bytes appended and flushed, then
     * the socket closed. Closing here instead would take the request with it.
     */
    if( app->net )
        ToriRS_Network_Logout(app->net);
    App_NetSessionReset(app);
    /* Reference `stopMidi(false)` plus the effect queue: nothing the world was
     * playing belongs to the screen we are going back to. */
    RS_Audio_StopAll(&app->audio, &app->audio_out);

    /*
     * Disarm the reconnect watch.
     *
     * Every detector in app_net_link_watch is armed off net_last_recv_ms -- a
     * session that was heard from and then went quiet. Leaving it set would
     * make the socket this function just closed read as a connection that was
     * lost, and the client would spend its way back to the title screen
     * redialling the world the player just left.
     */
    NetLinkWatch_Reset(&app->net_link);

    /*
     * And forget the session a reloaded page would have come back into.
     *
     * Before the title-screen branch below, not inside it: the socket is gone
     * either way, and a profile with nowhere to send the player is not a
     * profile whose next boot should log back in. @see app_session_resume.c.
     */
    app_session_resume_forget();

    if( !App_HasTitleScreen(app) )
    {
        /* Undeclared means absent (App_HasTitleScreen): this profile boots
         * straight into the gameframe and has nowhere to send the player. The
         * session has still ended -- the socket is gone and the world is
         * cleared -- so say so rather than pretending the click did nothing. */
        TORIRS_LOG("logout: no [layout:title] in this profile; staying on the gameframe\n");
        app->need_redraw = 1;
        return;
    }

    /* Credentials cleared with the screen, the reference's own behaviour
     * (Client-TS logout clears loginUser/loginPass): a password left in a
     * buffer after the player has left is a password nobody asked us to keep.
     * Same reset RS_TITLE_ACTION_CANCEL performs. */
    RS_Title_SetFieldText(&app->title, RS_TITLE_FIELD_USERNAME, NULL);
    RS_Title_SetFieldText(&app->title, RS_TITLE_FIELD_PASSWORD, NULL);
    RS_Title_SetMessages(&app->title, NULL, NULL, NULL);
    RS_Title_SetScreen(&app->title, RS_TITLE_MAIN_MENU);
    /* The one automatic submit has already been spent on the session that just
     * ended; re-arming it here would dial straight back into the world the
     * player asked to leave. @see RS_TitleSession_Abandon. */
    RS_TitleSession_Abandon(&app->title_session);
    TORIRS_LOG("logout: session ended; back to the title screen\n");
    App_OpenTitleScreen(app);
}

/*
 * The logout the player asked for: send it, then WAIT.
 *
 * The click does not end the session. The reference arms `logoutTimer = 250`
 * and keeps playing until the server answers (Client.ts:11212), and everything
 * about the answer is the server's: a LOGOUT packet, or -- what every server
 * in this tree's own content actually does, `p_logout` being a session kill --
 * the socket simply going away. Ending it here instead is what put the title
 * screen up while the server was still deciding whether the player was allowed
 * to leave, which is the one moment the two can disagree: ten seconds of
 * combat logout delay looked, from the client, exactly like a logout.
 *
 * Run once per logic tick. True when the screen changed.
 */
bool
app_logout_tick(struct App* app)
{
    assert(app);

    /* Ahead of this tick's request, so a wait armed below keeps its whole
     * window rather than spending the first cycle of it on the tick that
     * armed it. */
    if( app->logout_wait_cycles > 0 && --app->logout_wait_cycles == 0 )
    {
        /* Nothing answered. @see APP_LOGOUT_WAIT_CYCLES: the reference would
         * wait here forever, and a client pointed at more than one server
         * cannot. */
        TORIRS_LOG(
            "logout: no answer from the server in %d ms; ending the session anyway\n",
            APP_LOGOUT_WAIT_CYCLES * APP_LOGIC_TICK_MS);
        App_Logout(app);
        return true;
    }

    if( !app->logout_requested )
        return false;
    app->logout_requested = 0;

    /*
     * Nobody to wait for: an offline profile with no socket, or a session that
     * has already ended under the player. The request IS the ending, and
     * waiting five seconds to say so would be five seconds of a client that
     * looks like it ignored the button.
     */
    if( !app->net || app->net->state != TORIRS_NET_GAME )
    {
        App_Logout(app);
        return true;
    }

    /* The IF_BUTTON is already in the outbound ring -- the caller of the
     * clientCode handler put it there, which is the whole reason the request
     * is drained a tick late. Nothing more goes out; the answer comes back. */
    app->logout_wait_cycles = APP_LOGOUT_WAIT_CYCLES;
    TORIRS_LOG("logout: requested; waiting for the server\n");
    return false;
}

/*
 * TORIRS_NET_DROP_MS=<ms>: sever the connection this long after the first
 * packet of the session. The headless equivalent of the reference's
 * `::clientdrop` -- a harness has no chat box to type into, and the path it
 * exercises is otherwise reached only by genuinely losing a socket.
 *
 * Fires once. Read once, because this sits inside the per-frame watch.
 */
static bool
app_net_drop_requested(
    struct App const* app,
    uint64_t now_ms)
{
    static long drop_ms = -2;

    assert(app);
    if( drop_ms == -2 )
    {
        char const* value = getenv("TORIRS_NET_DROP_MS");
        drop_ms = value && *value ? strtol(value, NULL, 0) : -1;
    }
    if( drop_ms <= 0 || !app->net_link.first_recv_ms )
        return false;
    if( now_ms - app->net_link.first_recv_ms < (uint64_t)drop_ms )
        return false;
    drop_ms = -1;
    return true;
}

/*
 * What a lost session costs, once the watch has decided it is lost.
 *
 * Separate from the decision so that `::clientdrop` -- which severs the
 * connection from the chat handler, between frames -- pays the same price by
 * the same route.
 */
static void
app_net_tear_down_session(
    struct App* app,
    char const* why)
{
    assert(app);
    assert(app->net);

    /*
     * A session that goes away while the player is waiting to be logged out is
     * the server ANSWERING, not a connection that was lost -- most servers,
     * this tree's own included, answer the button by killing the session
     * rather than by sending LOGOUT. The reference draws the same line in
     * `lostCon` (Client.ts:2735): logoutTimer up, log out, do not redial.
     * Redialling here would put the player back into the world they asked to
     * leave, and would do it automatically.
     */
    if( app->logout_wait_cycles > 0 )
    {
        TORIRS_LOG("logout: server closed the session (%s)\n", why);
        App_Logout(app);
        return;
    }

    TORIRS_LOG("net: connection lost (%s) — attempting to reestablish\n", why);
    /* The next REBUILD must run even if it names the zone the client is
     * already standing in: it is the server's whole world state arriving
     * again, and its acknowledgement is what releases the rest of the burst. */
    app->net_force_rebuild = 1;
    /* Pushes NET_OUT_DISCONNECT, so the peer sees the FIN before the
     * re-established session asks for the character back. */
    ToriRS_Network_Logout(app->net);
    App_NetSessionReset(app);
}

/* Sever the session on demand, from outside the per-frame watch. */
void
app_net_lost(
    struct App* app,
    char const* why)
{
    assert(app);
    if( !app->net || !NetLinkWatch_Drop(&app->net_link) )
        return;
    app_net_tear_down_session(app, why);
}

/*
 * Watch a live session, and drive the re-establishment of a dead one.
 *
 * The policy is NetLinkWatch's. This is the half that has a socket: what the
 * transport can be seen to be doing, and what each answer costs.
 *
 * Called once per App_RunOnce with the wall clock, ahead of the logic ticks:
 * a frame that decides the backlog is stale must not first spend five ticks
 * draining it.
 */
void
app_net_link_watch(
    struct App* app,
    uint64_t now_ms)
{
    struct NetLinkSighting seen;
    char const* why = NULL;

    assert(app);
    if( !app->net || !app->net_enabled )
        return;

    memset(&seen, 0, sizeof(seen));
    seen.now_ms = now_ms;
    seen.frame_gap_ms =
        app->last_frame_ms && now_ms > app->last_frame_ms ? now_ms - app->last_frame_ms : 0;
    seen.in_game = app->net->state == TORIRS_NET_GAME;
    seen.logging_in = app->net->state == TORIRS_NET_LOGIN;
    seen.socket_closed = (app->net->conn_status == TORIRS_NET_STATUS_DISCONNECTED ||
                          app->net->conn_status == TORIRS_NET_STATUS_FAILED) &&
                         app->net->state == TORIRS_NET_DISCONNECTED;
    seen.drop_requested = app_net_drop_requested(app, now_ms);

    switch( NetLinkWatch_Step(&app->net_link, &seen, &why) )
    {
    case NET_LINK_LOST:
        app_net_tear_down_session(app, why);
        break;
    case NET_LINK_REESTABLISHED:
        TORIRS_LOG(
            "net: session re-established after %d attempt(s)\n",
            app->net_link.reconnect_attempts);
        /* A new seed: the page's token named the session this one replaced. */
        app_session_resume_remember_net(app->net);
        app->need_redraw = 1;
        break;
    case NET_LINK_RECONNECT:
        TORIRS_LOG("net: reconnect attempt %d\n", app->net_link.reconnect_attempts);
        if( !ToriRS_Network_Reconnect(app->net) )
        {
            NetLinkWatch_NoteReconnectRefused(&app->net_link);
            app->need_redraw = 1;
        }
        break;
    case NET_LINK_GAVE_UP:
        TORIRS_LOG(
            "net: giving up after %d reconnect attempts\n", app->net_link.reconnect_attempts);
        app->need_redraw = 1;
        break;
    case NET_LINK_IDLE:
        break;
    }
}

/* Settle the serial game-action pipeline, then pop the next packet.  Wire
 * order is preserved because a packet and all of the mount/CS2 work it
 * awaits finish before its successor starts.  Ready cooperative yields
 * are not spread over visual frames; only real external IO can pause the
 * transaction, and that pause is covered by exec_runner_had_work.
 *
 * Called from two places, and the second is a frame-rate matter. Every 20ms
 * logic tick runs it as the tick's packet phase. But when the pipeline parks
 * mid-tick on an asynchronous read — on web every post-READY cache read is
 * one — its response lands between animation frames, long before the next
 * logic tick. Waiting for that tick meant each parked read held the visual
 * latch (exec_runner_had_work / server_tick_open) for a full 20ms, and a
 * hitsplat whose sprite+sound chain was three reads deep froze the world for
 * three ticks every server cycle. App_RunOnce therefore also resumes a parked
 * pipeline once per frame; with nothing parked and nothing queued the call
 * settles an idle runner and pops nothing, so the extra call is free. */
int
app_pump_net_packets(struct App* app)
{
    int redraw = 0;

    int drained = 0;
    int fence_queued = 0;
    int last_exec_packet_type = -1;

    for( ;; )
    {
        enum TaskRunnerStat stat;

        /* A root remount (IF_OPENTOP) tears the tree down and rebuilds it
         * on app->runner. Every packet behind it targets components that
         * do not exist yet, so hold the whole pipeline rather than feed it
         * a tree mid-rebuild. App_RunOnce's own boot check cannot cover
         * this: it runs at the top of the frame, and the rebuild starts
         * here, below it — including on a later catch-up tick in the same
         * App_RunOnce. */
        if( app->app_state == APP_STATE_BOOTING )
        {
            app->exec_runner_had_work = 1;
            break;
        }

        {
            /* TORIRS_PKT_SLOW_MS=<n>: name the packet whose handler blew a
             * frame. The pipeline is serial, so this settle is the packet
             * queued by the previous iteration and nothing else. */
            static int slow_ms = -1;
            uint64_t t0;
            extern uint64_t PlatformWindow_TicksUs(void);

            if( slow_ms < 0 )
            {
                char const* v = getenv("TORIRS_PKT_SLOW_MS");
                slow_ms = (v && v[0]) ? atoi(v) : 0;
            }
            t0 = slow_ms > 0 ? PlatformWindow_TicksUs() : 0;
            stat = TaskRunner_SettleFrame(&app->exec_runner);
            if( slow_ms > 0 && last_exec_packet_type >= 0 )
            {
                uint64_t dt = PlatformWindow_TicksUs() - t0;
                if( dt >= (uint64_t)slow_ms * 1000u )
                    TORIRS_REPORT(
                        "pkt_slow: type=%d %.2f ms cycle=%llu\n",
                        last_exec_packet_type,
                        dt / 1000.0,
                        (unsigned long long)app->logic_cycle);
            }
        }

        /*
         * Parked on state this queue does not own -- in practice an asset the
         * ASSET queue is loading: Task_NpcMultiLoad waiting for an npc's body
         * parts, Task_WorldLoad waiting for a region's map squares. Resolve
         * that io here rather than ending the frame.
         *
         * A task is only ever resumed against reads that have LANDED, and the
         * budgets those waits carry are counted in resumes, not in frames.
         * Ending the frame instead spends one pass of the budget per frame per
         * waiting task -- which, on a cold region rebuild that parks hundreds
         * of them behind one serial pipeline, is how every map square in the
         * region reported "missing archive" and every npc in it "models failed
         * to load" out of a cache that held all of them.
         *
         * A render request is the one thing that outranks finishing the io:
         * the whole point of that request is that this frame is seen. Anything
         * else keeps going until the asset queue can no longer move, which is
         * the honest end of "resolve the io" -- and is bounded, because a wait
         * that outlives its own budget gives up on its own.
         */
        if( stat == TASK_RUNNER_BLOCKED )
        {
            enum TaskRunnerStat assets = TaskRunner_SettleFrame(&app->runner);

            if( getenv("TORIRS_FRAME_LATCH") )
            {
                int n = 0;
                for( struct ToriRS_Task* t = app->runner.queue->head; t; t = t->next )
                    n++;
                TORIRS_REPORT(
                    "frame_latch: blocked exec -> assets stat=%d progressed=%d queued=%d "
                    "reads_out=%d cycle=%d\n",
                    (int)assets,
                    app->runner.progressed,
                    n,
                    TaskRunner_ReadsOutstanding(&app->runner),
                    (int)app->logic_cycle);
            }
            if( assets != TASK_RUNNER_RENDER && app->runner.progressed )
                continue;
        }

        if( stat != TASK_RUNNER_IDLE )
        {
            if( getenv("TORIRS_FRAME_LATCH") )
            {
                struct ToriRS_Task* head = app->exec_runner.queue->head;
                /* The head's own read, when it has one: which table and
                 * which archive the pipeline is waiting on. That is the
                 * difference between "a packet parked" and knowing which
                 * loader to take off the FIFO next. */
                TORIRS_LOG(
                    "frame_latch: exec parked stat=%d head=%s blocked=%d reads_out=%d "
                    "read=%d/%d/%d cycle=%d\n",
                    (int)stat,
                    head ? head->name : "(none)",
                    head ? head->blocked : -1,
                    TaskRunner_ReadsOutstanding(&app->exec_runner),
                    head ? (int)head->io.kind : -1,
                    head && head->io.kind == TORIRS_IOK_CACHE ? head->io.u.cache.table_id : -1,
                    head && head->io.kind == TORIRS_IOK_CACHE ? head->io.u.cache.archive_id : -1,
                    (int)app->logic_cycle);
            }
            app->exec_runner_had_work = 1;
            break;
        }
        app->exec_runner_had_work = 0;
        /* Every packet is added with TaskRunner_AddRenderBlockingSerialTask, which arms this;
         * the FIFO's transaction is closed the moment it settles, and nothing
         * else on this runner reads the flag. */
        app->exec_runner.frame_settle_pending = 0;

        /* Do not cross a server-tick fence before that tick's newly
         * dispatched client scripts have settled against its final state. */
        if( fence_queued )
            break;

        {
            struct RevPacket packet;

            if( !app->net || !ToriRS_Network_PopPacket(app->net, &packet) )
            {
                drained = 1;
                break;
            }
            /* Liveness, for app_net_link_watch's 15s bound. Stamped on the
             * packet rather than on the byte read: a socket that delivers
             * bytes the framer never completes is not a live session. */
            NetLinkWatch_NotePacket(&app->net_link, app->last_frame_ms);

            /* Once a revision has demonstrated explicit tick fences,
             * retain only packets that participate in an atomic UI/CS2
             * transaction. World feedback is valid between those ticks.
             * SERVER_TICK_END clears this after its exec task has run. */
            if( app->server_tick_fence_seen && gameproto_packet_may_mutate_ui(packet.packet_type) &&
                packet.packet_type != PKT_NAME_SERVER_TICK_END )
            {
                if( !app->server_tick_open )
                {
                    app->server_tick_open_cycle = app->logic_cycle;
                    if( getenv("TORIRS_FRAME_LATCH") )
                        TORIRS_LOG(
                            "frame_latch: tick opened by packet %d at cycle %d\n",
                            (int)packet.packet_type,
                            (int)app->logic_cycle);
                }
                app->server_tick_open = 1;
            }
            if( packet.packet_type == PKT_NAME_SERVER_TICK_END )
                fence_queued = 1;
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PROTO_PACKETS, 1);
            last_exec_packet_type = packet.packet_type;
            /* The first packet of the session, for the boot report: the
             * gap between login_ok and this is the server's, not ours. */
            if( !app->net_first_packet_marked )
            {
                app->net_first_packet_marked = 1;
                ToriRS_BootTelemetry_Markf("net:first_packet:%d", (int)packet.packet_type);
            }
            TaskRunner_AddRenderBlockingSerialTask(&app->exec_runner, CreateTask_GameProtoExec(app, &packet));
            redraw = 1;
        }
    }
    /*
     * The fence for revisions that send none, plus a bounded backstop.
     *
     * A dry pipeline is NOT a tick boundary on a revision that has one: a
     * tick's packets arrive over several reads, so the queue runs dry mid-
     * tick and flushing there is the early dispatch this whole mechanism
     * exists to prevent. Once a SERVER_TICK_END has been seen, only that
     * fence dispatches — except after APP_CLIENTSCRIPT_FENCE_MAX_CYCLES,
     * so a tick cut short by a disconnect cannot strand a script forever.
     */
    if( drained && RS_ClientScriptQueue_Count(&app->pending_clientscripts) &&
        (!app->server_tick_fence_seen ||
         RS_ClientScriptQueue_FenceOverdue(
             &app->pending_clientscripts,
             (uint32_t)app->logic_cycle,
             APP_CLIENTSCRIPT_FENCE_MAX_CYCLES)) )
    {
        App_FlushPendingClientScripts(app);
        /* Same recovery fence for a connection whose tick was cut short:
         * once we intentionally fall back to the held scripts, allow the
         * resulting fully-settled state to publish too. */
        app->server_tick_open = 0;
        redraw = 1;
    }
    else if(
        drained && app->server_tick_open &&
        app->logic_cycle - app->server_tick_open_cycle >= APP_CLIENTSCRIPT_FENCE_MAX_CYCLES )
    {
        /* A fence can be lost without a RUNCLIENTSCRIPT in the tick.  The
         * same bounded disconnect recovery must release the visual latch
         * or the last committed frame would be retained forever. */
        app->server_tick_open = 0;
        redraw = 1;
    }

    return redraw;
}

void
App_SendIdkDesign(
    struct App* app,
    int gender,
    int const kits[RS_IDK_DESIGN_PARTS],
    int const colours[RS_IDK_DESIGN_COLOURS])
{
    assert(app && kits && colours);
    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "idk_savedesign: gender=%d kits=[%d,%d,%d,%d,%d,%d,%d] colours=[%d,%d,%d,%d,%d]\n",
            gender,
            kits[0],
            kits[1],
            kits[2],
            kits[3],
            kits[4],
            kits[5],
            kits[6],
            colours[0],
            colours[1],
            colours[2],
            colours[3],
            colours[4]);
    APP_NET_SEND(
        app,
        net_out_idk_savedesign(
            app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), gender, kits, colours));
}

void
App_IfTextSet(
    struct App* app,
    int com_id,
    char const* text)
{
    assert(app);
    UIIfTextStore_Set(&app->if_texts, com_id, text);
    {
        bool applied = UITree_ApplyText(app->tree, com_id, text);
        if( torirs_env_net_debug() )
            TORIRS_LOG(
                "if_settext: com=%d text='%s' applied=%d\n",
                com_id,
                text ? text : "",
                (int)applied);
    }
    app->need_redraw = 1;
}

void
App_IfColourSet(
    struct App* app,
    int com_id,
    int colour)
{
    assert(app);
    UIIfIntStore_Set(&app->if_colours, com_id, colour);
    {
        bool applied = UITree_ApplyColour(app->tree, com_id, colour);
        if( torirs_env_net_debug() )
            TORIRS_LOG(
                "if_setcolour: com=%d colour=%06x applied=%d\n", com_id, colour, (int)applied);
    }
    app->need_redraw = 1;
}

void
App_IfHideSet(
    struct App* app,
    int com_id,
    int hide)
{
    assert(app);
    UIIfIntStore_Set(&app->if_hides, com_id, hide ? 1 : 0);
    {
        bool applied = UITree_ApplyHide(app->tree, com_id, hide);
        if( torirs_env_net_debug() )
            TORIRS_REPORT("if_sethide: com=%d hide=%d applied=%d\n", com_id, hide, (int)applied);
    }
    app->need_redraw = 1;
}

void
app_send_if_button(
    void* user,
    int com_id)
{
    struct App* app = (struct App*)user;
    int target;
    int sub;

    /* A dynamic child is addressed as (container, sub) on the wire — its own
     * runtime id is a client allocation the server has never heard of. EVENT_CLICK
     * on chatmenu rows (and any other IF_SETEVENTS-armed list) must use that
     * pair, which is IF_BUTTON1 with the sub-id, not plain IF_BUTTON. */
    UIIfEventTable_ButtonTarget(app->tree, com_id, &target, &sub);
    if( torirs_env_net_debug() )
        TORIRS_LOG("if_button: com=%d target=%d sub=%d\n", com_id, target, sub);
    if( sub >= 0 )
    {
        APP_NET_SEND(
            app,
            net_out_if_button_op(
                app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), 1, target, sub));
        return;
    }
    APP_NET_SEND(
        app,
        net_out_if_button(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), com_id));
}

void
app_send_resume_pausebutton(
    void* user,
    int com_id)
{
    struct App* app = (struct App*)user;
    int target;
    int sub;

    /* Rev 239 action 30 and CC_RESUME_PAUSEBUTTON both write the static parent
     * uid plus the dynamic child's sub-id. The child runtime uid exists only
     * in this client, so resolve it at the wire boundary. */
    UIIfEventTable_ButtonTarget(app->tree, com_id, &target, &sub);
    if( torirs_env_net_debug() )
        TORIRS_LOG("resume_pausebutton: com=%d target=%d sub=%d\n", com_id, target, sub);
    APP_NET_SEND(
        app,
        net_out_resume_pausebutton(
            app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), target, sub));
}

void
app_send_close_modal(void* user)
{
    struct App* app = (struct App*)user;
    APP_NET_SEND(
        app, net_out_close_modal(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf)));
}

/* Server ack after a REBUILD_NORMAL-driven world load finishes. */
void
App_SendMapBuildComplete(struct App* app)
{
    APP_NET_SEND(
        app,
        net_out_map_build_complete(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf)));
}
