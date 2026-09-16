/*
 * The CS2 visual transaction: clientscript dispatch, notification flush,
 * settings mirrors, and the settle to a fixed point.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_dispatch_clientscript(
    struct App* app,
    struct PktRunClientScript const* request);

/** Hand one held payload to the CS2 dispatch. */
static void
app_dispatch_clientscript(
    struct App* app,
    struct PktRunClientScript const* request)
{
    char const* strp[PKT_RUNCLIENTSCRIPT_ARG_MAX];

    /*
     * The packet indexes strings by ARGUMENT, the CS2 dispatch wants them
     * COMPACTED — see `pkt_runclientscript_compact_strings` for which is which
     * and for what handing over the sparse array did.
     */
    int str_count = pkt_runclientscript_compact_strings(request, strp, PKT_RUNCLIENTSCRIPT_ARG_MAX);

    RS_CS2_RunScript(
        &app->host,
        &app->runner,
        request->script_id,
        request->intv,
        request->argc,
        request->str_mask,
        strp,
        str_count);
}

void
App_FlushPendingClientScripts(struct App* app)
{
    int count;

    assert(app);
    /*
     * A SNAPSHOT of the count, popped from the front.
     *
     * Dispatching a held script can push another -- CC_TRIGGEROP's queue drain
     * reaches this path -- so this loop is re-entrant, and a script pushed
     * inside it must wait for the next flush rather than be run by this one.
     * The snapshot is what says so; the ring is what makes it true without a
     * 900 KB copy.
     */
    count = RS_ClientScriptQueue_Count(&app->pending_clientscripts);
    for( int i = 0; i < count; i++ )
    {
        struct PktRunClientScript request;

        if( !RS_ClientScriptQueue_Pop(&app->pending_clientscripts, &request) )
            break;
        app_dispatch_clientscript(app, &request);
    }
}

void
App_RunClientScript(
    struct App* app,
    struct PktRunClientScript const* request)
{
    assert(app);
    assert(request);
    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "runclientscript: script=%d argc=%d str_mask=0x%x (held for tick fence)\n",
            request->script_id,
            request->argc,
            (unsigned)request->str_mask);

    /* Held, not run — see `pending_clientscripts` in app.h for why, and
     * `App_FlushPendingClientScripts` for where they go. A full queue runs it
     * now: degrading to the old ordering is a cosmetic bug, losing a script is
     * not. */
    if( !RS_ClientScriptQueue_Hold(
            &app->pending_clientscripts, request, (uint32_t)app->logic_cycle) )
        app_dispatch_clientscript(app, request);
}

/* Input-driven host effects are drained at the frame's CS2 fixed point.
 * They do not consume simulation time: a close or amount response must not
 * remain queued until a later boat command when the test clock is paused. */
/* Settings writes settle with the click, including while simulation is paused. */
int
app_cs2_flush_settings_mirrors(struct App* app)
{
    int sent = 0;
    if( !app->net || app->net->state != TORIRS_NET_GAME )
        return 0;
    {
        int mirror_varbit;
        int mirror_value;
        while( RS_CS2Host_TakeSettingsMirror(&app->host, &mirror_varbit, &mirror_value) )
        {
            char cmd[64];

            snprintf(cmd, sizeof(cmd), "setting %d %d", mirror_varbit, mirror_value);
            if( !App_SendCommand(app, cmd) )
            {
                RS_CS2Host_QueueSettingsMirror(&app->host, mirror_varbit, mirror_value);
                break;
            }
            sent = 1;
            if( getenv("TORIRS_SETTINGS_DEBUG") )
                TORIRS_LOG(
                    "settings: mirror varbit %d = %d -> server\n", mirror_varbit, mirror_value);
        }
    }

    return sent;
}

int
app_cs2_flush_notifications(struct App* app)
{
    int pending = app->host.close_modal_requested || app->host.logout_requested ||
                  app->host.keyboard_request || app->host.resume_pausebutton_component_id != -1 ||
                  app->host.social_send_count > 0;
    /*
     * An interface asked to close itself.
     *
     * `if_close` is what every framed interface's X runs (steelborder binds op 1
     * to clientscript 29, whose whole body is that one opcode). Rev-230's
     * method9167 both sends CLOSE_MODAL and locally unmounts every open modal
     * / sidemodal sub (type 0 / 3); overlays (type 1) stay. The deferred flag
     * is drained here rather than inside the hook so the CS2 host stays free
     * of the socket — same split every other host request has.
     */
    if( app->host.close_modal_requested )
    {
        app->host.close_modal_requested = false;
        if( !app->closing_modals )
        {
            int uids[UITREE_INTERFACE_PARENT_MAX];
            int n = 0;

            app->closing_modals = 1;
            if( app->button_sink.close_modal )
                app->button_sink.close_modal(app->button_sink.user);

            /* Snapshot first: App_CloseSubInterface mutates interface_parents. */
            if( app->tree )
            {
                for( int i = 0; i < app->tree->interface_parent_count; i++ )
                {
                    int t = app->tree->interface_parents[i].type;
                    if( t == 0 || t == 3 )
                        uids[n++] = app->tree->interface_parents[i].container_uid;
                }
                for( int i = 0; i < n; i++ )
                    App_CloseSubInterface(app, uids[i]);
            }

            /* CS1 IF1 slots: 2004 closeModal() cleared these locally too. */
            if( App_UiLogic(app) == APP_UI_LOGIC_CS1 )
                RS_UISlots_CloseModal(app);

            app->closing_modals = 0;
            app->need_redraw = 1;
        }
    }

    /*
     * A CS2 script ran LOGOUT (5630) -- the modern lane's "Click here to
     * logout", whose button is script-driven and carries no cache op.
     *
     * Parked by the host and turned into a request here, the same split
     * if_close above takes: the CS2 host knows nothing about the socket.
     * Handed to the tick's own drain rather than performed here so it lands
     * behind whatever this tick has already queued -- and the drain only asks;
     * the server's answer is what ends the session. @see app_logout_tick.
     */
    if( app->host.logout_requested )
    {
        app->host.logout_requested = false;
        app->logout_requested = 1;
    }
    /* The mobile scripts' soft keyboard: "Start chatting" shows it over the
     * chat line, its second press hides it. The chat line's focus is what the
     * platform keyboard follows (app_wants_text_input), so this is the same
     * focus a tap on the chat area gives. */
    if( app->host.keyboard_request )
    {
        app->vm_keyboard_open = app->host.keyboard_request > 0;
        /* The push to the platform is edge-triggered on "wanted" (see
         * App_TakeTextInputChange), and the person may have dismissed the
         * keyboard with the system's own button, which changes nothing here.
         * A request is a deliberate ask: forget what was last pushed so the
         * next take pushes the current answer again, as a tap on a login
         * field does. */
        app->text_input_effective = -1;
        app->host.keyboard_request = 0;
        app->need_redraw = 1;
    }

    if( app->host.resume_pausebutton_component_id != -1 )
    {
        int const com_id = app->host.resume_pausebutton_component_id;
        app->host.resume_pausebutton_component_id = -1;
        if( app->button_sink.resume_pausebutton )
            app->button_sink.resume_pausebutton(app->button_sink.user, com_id);
    }

    /*
     * Social requests a CS2 script queued this tick (friend_add, ignore_del,
     * chat_setfilter, chat_sendprivate — all reached from clientscript 681,
     * which is what the name prompt's Enter key runs) plus docheat, the
     * chatbox's own "::foo" handler (distinct from this function's
     * TORIRS_NET_CHEAT hook above).
     *
     * Same split as if_close above: the CS2 host knows nothing about the
     * socket, so it parks the request and this is where it becomes a packet.
     * The host has already applied the local half (the store, the filter
     * modes), because the server answers nothing at all on a delete.
     */
    {
        struct RS_CS2SocialSend send;

        while( RS_CS2Host_TakeSocialSend(&app->host, &send) )
        {
            int64_t name37 = (int64_t)strtobase37(send.name);

            if( !app->net )
                continue;
            switch( send.kind )
            {
            case RS_CS2_SOCIAL_SEND_FRIEND_ADD:
                APP_NET_SEND(
                    app,
                    net_out_friendlist_add(
                        app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), name37));
                break;
            case RS_CS2_SOCIAL_SEND_FRIEND_DEL:
                APP_NET_SEND(
                    app,
                    net_out_friendlist_del(
                        app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), name37));
                break;
            case RS_CS2_SOCIAL_SEND_IGNORE_ADD:
                APP_NET_SEND(
                    app,
                    net_out_ignorelist_add(
                        app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), name37));
                break;
            case RS_CS2_SOCIAL_SEND_IGNORE_DEL:
                APP_NET_SEND(
                    app,
                    net_out_ignorelist_del(
                        app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), name37));
                break;
            case RS_CS2_SOCIAL_SEND_CHAT_SETMODE:
                APP_NET_SEND(
                    app,
                    net_out_chat_setmode(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        send.modes[0],
                        send.modes[1],
                        send.modes[2]));
                break;
            case RS_CS2_SOCIAL_SEND_MESSAGE_PRIVATE:
                APP_NET_SEND(
                    app,
                    net_out_message_private(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        name37,
                        send.text));
                /*
                 * Local echo of the sent line, the reference's own behaviour
                 * (Client.ts socialInputType 3): the "To Bob: ..." row appears
                 * on send, not on a server round trip — the server never
                 * echoes a private message back to its sender.
                 */
                {
                    char shown[RS_SOCIAL_NAME_LEN];

                    RS_Social_DisplayName(send.name, shown, (int)sizeof(shown));
                    RS_CS2Host_ChatAdd(
                        &app->host, RS_CHAT_TYPE_PRIVATE_TO, shown, send.name, send.text);
                }
                app->need_redraw = 1;
                break;
            /*
             * chat_sendpublic from the chatbox's own submit path (script 73 ->
             * ~script5517 at rev 230).
             *
             * No local echo, unlike the private send above: a public line comes
             * back in the sender's own PLAYER_INFO extended info, and
             * task_exec_entity_info's PKT_PLAYER_INFO_OP_CHAT arm is what adds
             * the chatbox row and the overhead bubble for it. Echoing here as
             * well would print every line the player says twice.
             */
            case RS_CS2_SOCIAL_SEND_MESSAGE_PUBLIC:
                APP_NET_SEND(
                    app,
                    net_out_message_public(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        send.text,
                        send.colour_effect));
                break;
            case RS_CS2_SOCIAL_SEND_CHEAT:
                if( strncmp(send.text, "lootkill ", 9) == 0 )
                {
                    char lk_source[64] = { 0 };
                    int lk_obj = 0;
                    int lk_qty = 1;
                    if( sscanf(send.text + 9, "%63s %d %d", lk_source, &lk_obj, &lk_qty) >= 2 )
                    {
                        if( lk_qty <= 0 )
                            lk_qty = 1;
                        App_LootNotifyKill(app, lk_source, lk_obj, lk_qty);
                    }
                    break;
                }
                APP_NET_SEND(
                    app,
                    net_out_client_cheat(
                        app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), send.text));
                break;
            /* resume_countdialog(text) from a CS2 script — the bank PIN
             * keypad's fourth digit. Same packet the chatbox's own "Enter
             * amount" prompt sends below, and the same atol: the opcode pops
             * a string, the wire carries an int. */
            case RS_CS2_SOCIAL_SEND_RESUME_COUNTDIALOG:
                APP_NET_SEND(
                    app,
                    net_out_resume_countdialog(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        (int)atol(send.text)));
                break;
            default:
                break;
            }
        }
    }

    return pending;
}

/* Typed server notifications belong to the same settled transaction as
 * their CS2 click. Paused test clocks must not hold them until a logic tick. */
int
app_cs2_flush_triggeroplocal(struct App* app)
{
    int sent = 0;
    {
        struct RS_CS2TriggerOpLocal trig;
        int guard = 0;

        while( guard++ < RS_CS2_HOST_TRIGGEROPLOCAL_MAX * 4 &&
               RS_CS2Host_TakeTriggerOpLocal(&app->host, &trig) )
        {
            sent = 1;
            if( !app->net )
                continue;
            if( app->net->rev->packetout_code(PKTOUT_NAME_IF_SCRIPT_TRIGGER) >= 0 )
            {
                const char* strings[16];
                for( int i = 0; i < 16; ++i )
                    strings[i] = trig.strings[i];
                int object_id = -1;
                int node = UITree_FindByComponentId(app->tree, trig.component_id);
                if( node >= 0 && trig.child >= 0 )
                    node = UITree_FindChildBySubid(app->tree, node, trig.component_id, trig.child);
                if( node >= 0 && app->tree->components[node].item_id > 0 )
                    object_id = app->tree->components[node].item_id;
                APP_NET_SEND(
                    app,
                    net_out_if_script_trigger(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        trig.crc,
                        trig.component_id,
                        trig.child,
                        object_id,
                        trig.signature,
                        trig.values,
                        strings));
            }
            else
                APP_NET_SEND(
                    app,
                    net_out_if_button_op(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        1,
                        trig.component_id,
                        trig.sub));
        }
    }

    return sent;
}

/* Queue the CS2 work a just-completed script deferred back to the host.
 *
 * These requests cannot be dispatched from inside the VM which raised them:
 * doing so would recursively run a second script on the first script's stack.
 * They are therefore host queues, but that does not make them a later visual
 * transaction.  A frame is not settled until these listeners, and any widget
 * transmit listeners made dirty by the first script, have run too. */
int
app_cs2_enqueue_followups(struct App* app)
{
    int queued = app_cs2_flush_notifications(app);
    queued |= app_cs2_flush_triggeroplocal(app);
    queued |= app_cs2_flush_settings_mirrors(app);

    {
        int com_id;
        int guard = 0;

        while( guard++ < RS_CS2_HOST_CALL_ON_RESIZE_MAX * 4 &&
               RS_CS2Host_TakeCallOnResize(&app->host, &com_id) )
        {
            int32_t idx = UITree_FindByComponentId(app->tree, com_id);
            if( idx < 0 )
                continue;
            RS_CS2_DispatchHook(
                &app->host,
                &app->runner,
                com_id,
                &UITree_Hooks(&app->tree->components[idx])->on_resize);
            queued = 1;
        }
    }

    {
        struct RS_CS2TriggerOp trig;
        int guard = 0;

        while( guard++ < RS_CS2_HOST_TRIGGER_OP_MAX * 4 &&
               RS_CS2Host_TakeTriggerOp(&app->host, &trig) )
        {
            int32_t idx = UITree_FindByComponentId(app->tree, trig.component_id);
            if( idx < 0 )
                continue;
            RS_CS2_SetEventOp(&app->host, trig.op_index, 0);
            RS_CS2_DispatchHook(
                &app->host,
                &app->runner,
                trig.component_id,
                &UITree_Hooks(&app->tree->components[idx])->on_op);
            /*
             * ...and then answer the server, exactly as a picked menu row
             * would (reference method3476, which is the *shared* body: run the
             * on_op listener, then send IF_BUTTON<op> when that op's bit is
             * armed). Dispatching the hook alone made cc_triggerop a purely
             * visual call, which is what left shift-click drop doing nothing:
             * script6012 moves "Drop" onto op 1 — a slot the server never arms
             * — and script6014's cc_triggerop is the only thing that names the
             * real op. Clicking it ran the script and sent nothing.
             */
            if( trig.op_index >= 1 && trig.op_index <= 10 &&
                (App_IfEventsGetEffective(app, trig.component_id) & (1u << trig.op_index)) )
            {
                int target = trig.component_id;
                int sub = -1;
                int obj_id = app->tree->components[idx].item_id;
                UIIfEventTable_ButtonTarget(app->tree, trig.component_id, &target, &sub);
                if( obj_id > 0 )
                    APP_NET_SEND(
                        app,
                        net_out_if_button_obj_op(
                            app->net->rev,
                            app->net->random_out,
                            _nsbuf,
                            sizeof(_nsbuf),
                            trig.op_index,
                            target,
                            sub,
                            obj_id));
                else
                    APP_NET_SEND(
                        app,
                        net_out_if_button_op(
                            app->net->rev,
                            app->net->random_out,
                            _nsbuf,
                            sizeof(_nsbuf),
                            trig.op_index,
                            target,
                            sub));
            }
            queued = 1;
        }
    }

    if( RS_CS2_TransmitsPending(&app->host) )
    {
        RS_CS2_PumpTransmits(&app->host, &app->runner);
        queued = 1;
    }

    return queued;
}

/* Run one CS2 visual transaction to a fixed point.  Cooperative yields are
 * never frame boundaries: TaskRunner_SettleFrame crosses as many as needed.
 * A real outstanding platform read is the only reason this can return
 * PENDING, in which case App_RunOnce retains the previous emit list/frame and
 * resumes settlement on a later host turn. */
enum TaskRunnerStat
app_settle_cs2_frame(struct App* app)
{
    for( ;; )
    {
        enum TaskRunnerStat stat = TaskRunner_SettleFrame(&app->runner);

        if( stat != TASK_RUNNER_IDLE )
            return stat;
        /* Resize listeners and transmit painters must observe the geometry
         * produced by the script which queued them, not the last committed
         * frame's bounds. */
        {
            /* Scoped separately from the enclosing cs2_settle: the loop's cost is
             * either script dispatch above or this full-tree resolve, and the two
             * have different fixes. cs2_settle minus cs2_settle_layout is the
             * task pump. */
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_CS2_SETTLE_LAYOUT);
            UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        }
        {
            int more;
            /* The pump/followups split. Timed with the explicit begin/end form
             * rather than TORIRS_PERF_SCOPE because the measured region produces
             * a value the loop then branches on, and a `for`-shaped scope macro
             * cannot carry one out. */
            TORIRS_PERF_STAGE_BEGIN(TORIRS_PERF_STAGE_CS2_SETTLE_FOLLOWUPS);
            more = app_cs2_enqueue_followups(app);
            TORIRS_PERF_STAGE_END(TORIRS_PERF_STAGE_CS2_SETTLE_FOLLOWUPS);
            if( !more )
            {
                app->runner.frame_settle_pending = 0;
                return TASK_RUNNER_IDLE;
            }
        }
    }
}
