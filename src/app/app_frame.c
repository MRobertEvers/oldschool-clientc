/*
 * The frame: App_RunOnce and the command drain, input ownership, and the
 * frame-time accessors.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"
#include "perf_audit.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_frame_latch_note(
    struct App* app,
    char const* reason);

/* Did the last App_RunOnce leave async work queued?
 *
 * The frame loop asks so it can decline to sleep. See app.h for why the frame
 * cap must not pace the pipeline. */
int
App_AsyncPending(const struct App* app)
{
    assert(app);
    return app->async_pending;
}

void
App_NoteFrameTime(
    struct App* app,
    uint64_t frame_us)
{
    assert(app);

    PerfAudit_EndFrame(frame_us);
    FrameTimeRing_Add(&app->dbg_frame_times, frame_us);
}

void
App_NoteFrameDrawn(struct App* app)
{
    assert(app);

    app->frames_rendered++;
}

uint64_t
App_LastFrameUs(struct App const* app)
{
    assert(app);

    return FrameTimeRing_NewestUs(&app->dbg_frame_times);
}

int
App_InputFrameConsumed(struct App const* app)
{
    assert(app);
    return app->input_frame_consumed;
}

int
App_PointerOwnedByUi(
    struct App* app,
    int x,
    int y)
{
    assert(app);
    /*
     * Everything drawn over the 3D world that owns what lands on it: the
     * client's own chrome, AND the game's interfaces, which
     * App_ChromePointerOwned knows nothing about.
     *
     * The touch layer asks this to decide whether a one-finger drag turns the
     * CAMERA or presses a widget. Chrome alone was not enough: a finger that
     * came down on the All Settings window, an inventory list or a dropdown
     * still started a camera drag, because the interface it landed on is not
     * chrome -- so the widget never saw a press and nothing could be dragged
     * or swiped anywhere over the viewport.
     *
     * The question "is this point the world" already has one answer in this
     * file, and it is the one the click-to-walk and the minimenu use; asking
     * it here is what keeps the drag and the click agreeing about who owns a
     * pixel.
     */
    if( app_chrome_wants_pointer(app, x, y) )
        return 1;
    return !app_world_mouse_gate(app, x, y);
}

int
App_ChromePointerOwned(
    struct App const* app,
    int x,
    int y)
{
    assert(app);
    /* Asked LIVE rather than answered from app->chrome_pointer_owned: that
     * field is this frame's pointer, latched once, and the caller here is the
     * touch layer asking about a point of its own. */
    return app_chrome_wants_pointer(app, x, y);
}
/*
 * The frame: draining the command bus, and one pass of App_RunOnce.
 *
 * Included into app.c rather than compiled on its own. This is the client's
 * per-frame top level -- it reads most of App and calls most of the file -- so
 * it is not a module and the split is for READING, not for linkage. The
 * translation unit, the statics and the definition order are unchanged.
 */

void
App_DrainCommands(
    struct App* app,
    struct ToriRS_CmdBus* bus,
    struct LibToriRS_Input* input)
{
    struct ToriRS_CmdHeader header;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];

    assert(app);
    assert(bus);
    assert(input);

    while( CmdBus_Pop(bus, &header, payload) )
    {
        if( ToriRS_Input_ApplyCmd(input, &header, payload) )
            continue;

        switch( header.type )
        {
        case TORIRS_CMD_FRAME:
            break; /* record/replay delimiter only */
        case TORIRS_CMD_NET_CONNECT:
        case TORIRS_CMD_NET_RECV:
        case TORIRS_CMD_NET_STATUS:
            if( app->net )
                ToriRS_Network_HandleCmd(app->net, header.type, payload, header.length);
            break;
        case TORIRS_CMD_WINDOW_RESIZE:
            if( header.length >= sizeof(struct ToriRS_CmdWindowResize) )
            {
                struct ToriRS_CmdWindowResize const* cmd =
                    (struct ToriRS_CmdWindowResize const*)payload;
                /* A resizable push is latched before scaling divides it: the
                 * canvas is what the client draws, but the window is what a
                 * later setting change has to be recomputed from. Leaving fixed
                 * pushes the real window size through this same command. */
                if( getenv("TORIRS_RESIZE_DEBUG") )
                    TORIRS_REPORT("resize: game area %dx%d (%s)\n", (int)cmd->width, (int)cmd->height,
                        App_WindowMode(app) == CS2VM_WINDOW_MODE_RESIZABLE ? "resizable" : "fixed");
                if( App_WindowMode(app) == CS2VM_WINDOW_MODE_RESIZABLE && cmd->width > 0 &&
                    cmd->height > 0 )
                {
                    /* The resize re-derives the canvas from every setting, so a
                     * pending change is applied by it. */
                    app->host.client_scale_dirty = false;
                    App_ApplyWindowLayout(app, cmd->width, cmd->height);
                }
                else
                {
                    /* Fixed: the shell pushes the frame's own size on the way
                     * in, and scaling that would only be undone by the floor.
                     * The flag stays for the shell's fixed-window branch. */
                    App_SetCanvasSize(app, cmd->width, cmd->height);
                }
            }
            break;
        case TORIRS_CMD_KEYBOARD_INSET:
            if( header.length >= sizeof(struct ToriRS_CmdKeyboardInset) )
            {
                struct ToriRS_CmdKeyboardInset const* cmd =
                    (struct ToriRS_CmdKeyboardInset const*)payload;
                if( app->keyboard_inset != (int)cmd->bottom )
                {
                    /* The keyboard went away under us -- the person pressed
                     * the system's Back, which the client is never told about
                     * any other way. A script's standing request to show it
                     * does not survive that; leaving it standing kept the
                     * client asking for a keyboard nobody wanted, and the
                     * inset with it. */
                    if( app->keyboard_inset > 0 && (int)cmd->bottom == 0 )
                        app->vm_keyboard_open = 0;
                    app->keyboard_inset = (int)cmd->bottom;
                    if( getenv("TORIRS_RESIZE_DEBUG") )
                        TORIRS_REPORT("keyboard inset -> %d canvas rows\n", app->keyboard_inset);
                    /* The band the layout hands to every row whose profile
                     * declared `safe_area=os:bottom` -- the login box on the
                     * profiles that state it, and nothing at all on the ones
                     * that do not. The same number the plugin engine's
                     * platform_safe_rect answers from, so a provided frame's
                     * strip and a profile's panel cannot disagree about where
                     * the keyboard starts. */
                    UITree_LayoutSetSafeBottomInset(app->keyboard_inset);
                    /* A frame-build invalidation, exactly like a resize: the
                     * provided frame is asked again with the new
                     * ToriRS_GameframeEvent.safe and slides its chatbox above
                     * (or back under) the keyboard. The host's frame boundary
                     * polls the band as well; both collapse into the one
                     * PluginHost_Layout at the next fence. */
                    app->plugin_layout_dirty = 1;
                    if( app->tree )
                        UITree_LayoutInvalidate(app->tree);
                    /* And the title screen's, whose emit walk would otherwise
                     * reuse last frame's command buffer and draw the box at
                     * its old place. */
                    app_title_state_changed(app);
                    app->need_redraw = 1;
                }
            }
            break;
        case TORIRS_CMD_DEVICE_STATUS:
        {
            /* The CS2 scripts read the battery and the link through three
             * opcodes; without this the host answers with the constants it was
             * seeded with, and a phone on cellular at 4% still reads as
             * plugged in on wifi. Pushed on change by the touch backends
             * only. */
            struct ToriRS_CmdDeviceStatus const* cmd =
                (struct ToriRS_CmdDeviceStatus const*)payload;
            assert(header.length >= sizeof(*cmd));
            RS_CS2Host_SetDeviceStatus(
                &app->host,
                (int)cmd->battery_percent,
                (int)cmd->battery_charging,
                (int)cmd->network_kind);
            if( getenv("TORIRS_DEVICE_DEBUG") )
                TORIRS_REPORT(
                    "device status -> battery %d%% charging %d network %d\n",
                    (int)cmd->battery_percent,
                    (int)cmd->battery_charging,
                    (int)cmd->network_kind);
            break;
        }
        case TORIRS_CMD_PLUGIN_CHROME_TOGGLE:
            assert(header.length == 0);
            app_plugin_window_set_open(app, !app->plugin_panel_visible);
            break;
        /*
         * Host commands. Each is the same call the equivalent TORIRS_SIM_*
         * harness makes, reached from the drain instead of from a pre-loop
         * environment read, so an embedded client can be driven after boot and
         * on the web lane at all (docs/web_build.md: the SIM harnesses call
         * App_BootWait, which never returns against an async IO backend).
         *
         * A short frame is a producer bug — every one of these is written by
         * CmdBus_Push*, which sizes it — so the length is asserted rather than
         * quietly skipped. It is the same reasoning as anywhere else here: a
         * dropped command surfaces later as an interface that did not open.
         */
        case TORIRS_CMD_UI_OPEN_ROOT:
        {
            struct ToriRS_CmdUiOpenRoot const* cmd = (struct ToriRS_CmdUiOpenRoot const*)payload;
            assert(header.length >= sizeof(*cmd));
            App_OpenRootInterface(app, cmd->interface_id);
            break;
        }
        case TORIRS_CMD_UI_SET_VARP:
        case TORIRS_CMD_UI_SET_VARBIT:
        {
            struct ToriRS_CmdUiSetVar const* cmd = (struct ToriRS_CmdUiSetVar const*)payload;
            assert(header.length >= sizeof(*cmd));
            /* Optimistic, which is what a panel's own write is: the value
             * stands until the server says otherwise, and offline it never
             * does. A varbit notifies with -1 because the transmit pump keys on
             * the BASE varp, which the caller does not know. */
            if( header.type == TORIRS_CMD_UI_SET_VARP )
            {
                VarPManager_SetVarpOptimistic(&app->varps, cmd->id, cmd->value);
                RS_CS2Host_NotifyVarChanged(&app->host, cmd->id);
            }
            else
            {
                VarPManager_SetVarbitOptimistic(&app->varps, cmd->id, cmd->value);
                RS_CS2Host_NotifyVarChanged(&app->host, -1);
            }
            break;
        }
        case TORIRS_CMD_UI_RUNSCRIPT:
        {
            struct ToriRS_CmdUiRunScript const* cmd = (struct ToriRS_CmdUiRunScript const*)payload;
            assert(header.length >= offsetof(struct ToriRS_CmdUiRunScript, args));
            assert(cmd->argc >= 0);
            assert(cmd->argc <= TORIRS_CMD_UI_RUNSCRIPT_MAX_ARGS);
            assert(header.length >= CmdBus_UiRunScriptBytes(cmd->argc));
            RS_CS2_RunScript(
                &app->host,
                &app->runner,
                cmd->script_id,
                cmd->argc > 0 ? cmd->args : NULL,
                cmd->argc,
                0,
                NULL,
                0);
            break;
        }
        case TORIRS_CMD_EXEC_TEXT:
        {
            /* Not NUL-terminated on the wire; the header's length is the
             * string's. App_SendCommand answers false until the connection
             * reaches TORIRS_NET_GAME — a debugproc is a server-side script
             * call, so before login there is nothing to send it to, and saying
             * so is more use than a silent drop. */
            char text[TORIRS_CMD_MAX_PAYLOAD + 1];
            size_t length = header.length;
            assert(length <= TORIRS_CMD_MAX_PAYLOAD);
            memcpy(text, payload, length);
            text[length] = '\0';
            if( !App_SendCommand(app, text) )
                fprintf(stderr, "cmdbus: exec_text '%s' dropped, not in game yet\n", text);
            break;
        }
        default:
            TORIRS_LOG("cmdbus: unhandled command type %u\n", header.type);
            break;
        }
    }
}

/*
 * TORIRS_FRAME_LATCH=1 -- report the visual latch, one line per episode.
 *
 * App_RunOnce withholds a frame (returns 0) whenever a server-tick UI
 * transaction is mid-flight, and the shell then re-presents the last committed
 * frame. One or two frames of that is the mechanism working; a run of them is a
 * visible freeze, and the trace alone cannot say which of the three exits held
 * it. This counts the run and names the exits that made it up.
 */
static void
app_frame_latch_note(
    struct App* app,
    char const* reason)
{
    static int enabled = -1;
    static int frames;
    static int logic_at_open;
    static char const* reasons[8];
    static int counts[8];
    static int nreasons;

    if( enabled < 0 )
    {
        char const* v = getenv("TORIRS_FRAME_LATCH");
        enabled = (v && v[0] && v[0] != '0') ? 1 : 0;
    }
    if( !enabled )
        return;

    if( reason )
    {
        int i;

        if( !frames )
        {
            nreasons = 0;
            logic_at_open = (int)app->logic_cycle;
        }
        frames++;
        for( i = 0; i < nreasons; i++ )
            if( reasons[i] == reason )
                break;
        if( i == nreasons && nreasons < (int)(sizeof reasons / sizeof reasons[0]) )
        {
            reasons[nreasons] = reason;
            counts[nreasons] = 0;
            nreasons++;
        }
        if( i < (int)(sizeof counts / sizeof counts[0]) )
            counts[i]++;
        return;
    }

    if( frames )
    {
        char line[256];
        int n = snprintf(
            line,
            sizeof line,
            "frame_latch: held %d frames, %d logic ticks:",
            frames,
            (int)app->logic_cycle - logic_at_open);
        for( int i = 0; i < nreasons && n < (int)sizeof line; i++ )
            n += snprintf(line + n, sizeof line - n, " %s=%d", reasons[i], counts[i]);
        TORIRS_LOG("%s\n", line);
        frames = 0;
    }
}

/*
 * One pump of the asset runner: every task that can make progress, stepped.
 *
 * Extracted from App_RunOnce so it has two callers. The frame calls it once,
 * as it always has. The browser platform calls it again between frames, from
 * the executor's landed hook (torirs_web_io_pump, main.c), the moment a batch
 * of reads has been answered -- so a task whose answer arrived does not wait
 * for the next animation frame, or the next 4 ms setTimeout, to be resumed.
 * On a cold boot the difference is the whole of a serial chain's latency:
 * a CS2 script resolving its sprites one per yield paid a frame per sprite.
 *
 * `from_frame` says which caller this is. Both step exactly the same way and
 * update the same flags; only the per-frame accounting (boot frames, busy
 * frames) belongs to the frame, because a pump is not a frame.
 *
 * Safe to run outside a frame for the same reason a task may yield at any
 * pass: nothing a task does assumes it is inside App_RunOnce. The CS2 settle
 * does its layout resolve here when it reaches its fixed point, which is
 * what the frame would do at the same point; publication of the tree stays
 * with the frame, which sees runner_had_work cleared and refreshes.
 */
enum TaskRunnerStat
App_PumpAsync(
    struct App* app,
    int from_frame)
{
    enum TaskRunnerStat result = TASK_RUNNER_IDLE;

    assert(app);
    app->runner.telemetry.in_pump = !from_frame;
    /* Pump ordinary async work with a frame budget.  A CS2 transaction is the
     * exception: cooperative yields are drained to completion, and a genuine
     * external wait retains the last settled frame until it can resume. */
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_ASYNC)
    {
        int booting = app->app_state == APP_STATE_BOOTING;
        /*
         * Drain it. The bound this replaces came in with 8f3028ede (2026-07-22)
         * under the note "once READY a small budget keeps frame pacing", and
         * that had the relationship backwards: frame pacing was never what
         * needed protecting. What the bound actually did was cap the async
         * pipeline at budget-times-framerate -- 32 x 50 = 1600 steps a second
         * once past boot -- and this client streams its entire world through
         * that pipeline, 516 containers on a cold rev-289 boot. The frame cap
         * was deciding how fast the game could load.
         *
         * A step is cooperative and returns; the loop below still exits the
         * moment the runner goes idle. The guard is a runaway backstop, not a
         * pacing device, which is why it is large enough that no real frame
         * reaches it -- and main() no longer sleeps while work is queued, so a
         * frame that does hit it resumes immediately instead of waiting out
         * the cap.
         */
        int budget = APP_ASYNC_STEP_LIMIT;
        enum TaskRunnerStat stat = TASK_RUNNER_IDLE;
        int steps = 0;

        /* Cleared here and set below, so it describes THIS frame. The caller
         * uses it to decide whether to sleep: work still queued means the
         * frame cap would be pacing the pipeline rather than the screen. */
        app->async_pending = 0;
        int settling_cs2 = !booting && (app->runner_had_work || app->runner.frame_settle_pending);

        if( settling_cs2 )
        {
            stat = app_settle_cs2_frame(app);
        }
        else
        {
            /*
             * Step while passes make progress. Every other answer a pass can
             * give ends the frame's pumping: IDLE (nothing left), RENDER (a
             * task asked for this frame to be seen -- spending the budget
             * here is exactly what the request exists to interrupt), WAITING
             * (the answers are not here yet; the frame has to end for them
             * to arrive) and BLOCKED (the other queue has to run). Stepping
             * past any of them is a busy-wait into the tripwire below.
             */
            for( int i = 0; i < budget; i++ )
            {
                steps++;
                if( booting )
                    app->boot_steps++;
                stat = TaskRunner_Step(&app->runner);
                if( stat != TASK_RUNNER_PROGRESSED )
                    break;
            }
            /*
             * Reaching the limit is not a cap doing its job -- it is a task
             * that will not converge, a runner that never returns IDLE.
             *
             * abort() rather than assert(): OPT=1 compiles -DNDEBUG, and this
             * has to fail the same way in the build people actually run. A
             * client that silently capped here would present as "slow to
             * load" with nothing anywhere saying why, which is the failure
             * this whole change exists to remove.
             */
            if( steps >= budget )
            {
                TORIRS_ERR(
                    "app: the async pipeline ran %d steps in one frame without "
                    "going idle (limit %d, booting=%d). A task is not "
                    "converging.\n",
                    steps,
                    budget,
                    booting);
                fflush(stderr);
                abort();
            }
        }
        if( !from_frame )
        {
            /* A pump is not a frame: none of the per-frame counts below. */
        }
        else if( booting )
        {
            app->boot_frames++;
            if( steps >= budget )
                app->boot_frames_budget_capped++;
        }
        else if( !settling_cs2 && steps >= budget )
        {
            /* Post-boot frame that used its whole budget with work still
             * queued: the async pipeline is being drip-fed rather than run. */
            app->busy_frames++;
            app->busy_steps += steps;
        }

        /* Anything left to do -- a budget that ran out, or a runner that is
         * simply not idle -- means the next frame should start immediately
         * instead of waiting out the cap. */
        if( stat != TASK_RUNNER_IDLE )
            app->async_pending = 1;
        if( settling_cs2 && stat != TASK_RUNNER_IDLE )
            app->runner_had_work = 1;
        /* Tree-affecting async work (CS2 hooks/transmits) finished: refresh. */
        if( settling_cs2 && stat == TASK_RUNNER_IDLE )
        {
            app->runner_had_work = 0;
            app->pending_tree_refresh = 1;
        }
        result = stat;
    }
    app->runner.telemetry.in_pump = 0;
    return result;
}

int
App_RunOnce(
    struct App* app,
    uint64_t now_ms,
    struct LibToriRS_Input* input)
{
    /* Set below: the login form drained this frame's keys, so the in-game
     * key passes must not also act on them. */
    int title_captures_keys = 0;
    struct UIInteractOut out;
    int ran_cs2 = 0;
    int plugin_pointer_consumed = 0;

    assert(app);
    assert(input);

    /* The shell keeps this input frame intact when settlement returns before
     * interaction. Mark it consumed only after the stable-tree gate below; a
     * post-interaction async yield must not replay the same click. */
    app->input_frame_consumed = 0;

    /* Park this frame's input where the plugin api can reach it: a handler
     * fires from inside the overlay and menu builds, far below the frame that
     * owns the pointer. Cleared at the end of the frame, so a plugin can never
     * read a stale one. */
    app->plugin_input = input;

    /* Plugins before the built-in developer tools, for the same reason those
     * run first: a plugin panel's toggle has to latch during a boot, and
     * anything it changes has to be visible to this frame's emit rebuild. */
    app->overlays.batch_started = false;
    PluginHost_FrameStart(app->plugins, now_ms, app->frames_rendered);
    /* After the frame handlers, not before: a plugin that re-authors its
     * geometry from on_frame gets it on screen this frame rather than next. */
    app_plugin_geometry_settle(app);
    /*
     * All Settings rows, drained here and not inside the VM.
     *
     * The builtins that implement the Activities category read their own
     * varbits and need nothing from this; what needs it is the pair of BUTTON
     * rows, which have no varbit to read. See RS_CS2Host_TakeSettingsAction.
     */
    {
        int setting_id;
        int setting_value;
        while( RS_CS2Host_TakeSettingsAction(&app->host, &setting_id, &setting_value) )
        {
            /*
             * The two BUTTON rows, which the cache does not act on itself.
             *
             * "Clear your highlighted tiles" and "Clear your highlighted
             * NPCs" both reach the settings apply hub, whose switch has no
             * case for either -- the only thing it does for them is write the
             * setting id to the settings-changed varbit, which is how they get
             * here at all. The reference clears the group natively; so does
             * this.
             *
             * Which group each is, and why they are different groups even
             * though both are 6, is stated once in rs_highlight.h beside the
             * two constants -- a cache id this client has to know by number
             * belongs where it can be checked against the cache.
             */
            int const clear_tiles = app_setting_id(app, APP_SETTING_CLEAR_TILE_MARKERS);
            int const clear_npcs = app_setting_id(app, APP_SETTING_CLEAR_NPC_TAGS);
            /* Both tested for >= 0 first: an undeclared row is -1, and the id
             * on the wire must never be allowed to match "there is no such
             * row". */
            if( clear_tiles >= 0 && setting_id == clear_tiles )
                RS_HighlightClear(
                    &app->host.highlight, RS_HIGHLIGHT_TILE, RS_HIGHLIGHT_GROUP_TILE_MARKERS);
            else if( clear_npcs >= 0 && setting_id == clear_npcs )
                RS_HighlightClear(
                    &app->host.highlight, RS_HIGHLIGHT_NPC, RS_HIGHLIGHT_GROUP_NPC_TAGS);
        }
    }

    app_cs2_flush_settings_mirrors(app);

    /* Developer overlay first: its toggle key has to latch during a boot too,
     * and its readout has to be current before this frame's emit rebuild. */
    app_debug_overlay_tick(app, input);
    /* Right after the developer overlay, because it shares that instance: the
     * tick above is what routed this frame's mouse into dbg_ui, and the
     * picker's own drain has to run against the activation that produced. */
    app_settings_colour_tick(app);
    app_settings_number_tick(app);
    /* Plugin settings beside the other developer chrome, and before the loc
     * editor for the same reason it runs before the map editor: whoever is
     * open drains the shared activation latch, so order decides who sees a
     * click first. This one only takes activations it owns. */
    app_plugin_panel_tick(app, input);
    /* Loc editor next, same reasoning -- and it has to run before anything
     * downstream reads input_frame_consumed for click-to-walk. */
    app_loc_editor_tick(app, input);
    /* Who owns this frame's pointer, latched after the chrome ticks that may
     * have moved, opened or closed a panel and before anything else acts on
     * it -- so every consumer below answers the question the same way. */
    app->chrome_pointer_owned =
        app_chrome_wants_pointer(app, input->curr.mouse_x, input->curr.mouse_y);
    /* Map editor panel after the loc editor, for the same reason and in the
     * same order it is drawn: both read this frame's hover, and the map editor
     * acts on activations the overlay latched during the two calls above. */
    if( app->editor )
    {
        Editor_PanelTick(&app->editor_panel, app);
        app_map_editor_world_click(app, input);
        app_map_editor_ghost_update(app);
        app_map_editor_preview_update(app);
        app_map_editor_open_pending_square(app);
        app_map_editor_drain(app);
    }

    /*
     * Is the link still worth talking to?
     *
     * Ahead of everything else, and ahead of the BOOTING early-out, because
     * one of the things it measures is the gap since the last frame — a frame
     * that concludes the client was asleep must reach that conclusion before
     * it spends the frame draining what arrived while it was.
     */
    app_net_link_watch(app, now_ms);
    app->last_frame_ms = now_ms;
    /* The monotonic clock the idle-time and trading-post age commands read. */
    app->host.client.now_ms = (int64_t)now_ms;

    App_PumpAsync(app, 1);

    /* Before the BOOTING test: the swap puts the session straight back into
     * BOOTING for the title bake, so the frame that saw the warm gameframe
     * bake settle still renders a loading screen, never the gameframe. */
    app_title_swap_if_pending(app);

    if( app->app_state == APP_STATE_BOOTING )
    {
        /* Loading screen frame; no logic/interaction until the tree exists. */
        app->last_logic_ms = now_ms;
        /* Unless this bake has a frame worth keeping behind it: no commit, so
         * the host presents the one it already has, and need_redraw stays set
         * for the frame that finds the tree finished. @see
         * App_BootHoldsLastFrame. */
        if( App_BootHoldsLastFrame(app) )
            return 0;
        app->need_redraw = 0;
        return 1;
    }

    /* A browser/cache request can be the one legitimate pause in a CS2 visual
     * transaction.  Do not run input against the partially-mutated tree, and
     * do not rebuild the emit list from it. */
    if( app->runner_had_work )
    {
        app_frame_latch_note(app, "runner_had_work");
        return 0;
    }

    /* Async completions: world load finish, texture publish, deferred seq
     * binds, and any queued tree refresh (relayout + CS1 + redraw). */
    app_async_polls(app);

    /* Logic ticks at 20ms with bounded catch-up after a stall. */
    if( app->last_logic_ms == 0 )
        app->last_logic_ms = now_ms;
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_LOGIC)
    {
        /*
         * One clock, and it pays out in whole ticks while keeping the change.
         *
         * The simulation runs at a fixed 50 Hz. The frame's elapsed time goes
         * into an accumulator; whole 20 ms cycles are drawn out of it and the
         * remainder is carried. That is what makes the rate exactly 50 Hz no
         * matter how fast frames arrive -- an uncapped frame rate cannot run
         * the world faster than real time, because time it did not spend stays
         * in the accumulator instead of being rounded into a tick.
         *
         * This replaces a round-to-nearest divide. That existed to stop a
         * 19.6ms frame beating 0,2,0,2 against a 20ms tick, and it did -- but
         * it bought that by counting a short frame as a whole cycle, which is
         * time the simulation was given and had not elapsed. The accumulator
         * fixes the beat properly: 19.6ms pays 1,1,1... once the remainder
         * carries, and never pays more than really elapsed.
         *
         * `frame_cycles` for the movers is taken from the SAME elapsed value
         * below. Two clocks with different rounding and different clamps is
         * how an npc's legs get ahead of where its body actually is.
         */
        uint64_t elapsed_ms = now_ms > app->last_logic_ms ? now_ms - app->last_logic_ms : 0;
        int ticks = 0;

        app->last_logic_ms = now_ms;
        /* One clamp, shared: a stall must cost the movers and the animations
         * the same amount of time, or they resume out of step. */
        if( elapsed_ms > (uint64_t)APP_MAX_CATCHUP_TICKS * APP_LOGIC_TICK_MS )
        {
            elapsed_ms = (uint64_t)APP_MAX_CATCHUP_TICKS * APP_LOGIC_TICK_MS;
            /* Dropped on the floor rather than owed: catching up a long stall
             * at full speed is the lurch the bound exists to prevent. */
            app->cycle_accum_ms = 0.0;
        }
        app->cycle_accum_ms += (double)elapsed_ms;
        while( app->cycle_accum_ms >= (double)APP_LOGIC_TICK_MS && ticks < APP_MAX_CATCHUP_TICKS )
        {
            app->cycle_accum_ms -= (double)APP_LOGIC_TICK_MS;
            ticks++;
        }
        app->logic_frame_ms = elapsed_ms;
        /*
         * A bounded headless run ticks ONCE per frame, on the frame and not on
         * the wall clock.
         *
         * TORIRS_MAX_FRAMES=N exists to produce a comparable artefact -- a
         * screenshot, an emit dump -- and wall-clock pacing makes that artefact
         * non-reproducible for anything that reads `clientclock`. Two runs of
         * `ge_pricechecker` at three frames landed on cycle 102 and cycle 104,
         * so its pulsing overlay dumped a different transparency each time and
         * no comparison against it could ever be exact. One tick per frame
         * makes "three frames" mean the same three cycles every run.
         *
         * The accumulator is emptied rather than carried: leaving a remainder
         * would let a later frame pay a second tick and put the run back on
         * the wall clock this exists to leave. The movers are handed exactly
         * that one tick for the same reason.
         */
        if( g_torirs_max_frames > 0 )
        {
            ticks = 1;
            app->cycle_accum_ms = 0.0;
            app->logic_frame_ms = APP_LOGIC_TICK_MS;
        }
        int const ticks_paid = ticks;

        /*
         * The touch marker, advanced ONCE per rendered frame by the time that
         * really elapsed -- not by the 20 ms simulation cycle above.
         *
         * It used to ride app_logic_tick, and that is why it "often doesn't
         * render at all". That loop runs `ticks` times per frame, and `ticks`
         * is whatever the accumulator paid out: 0 on a frame shorter than a
         * cycle, and a whole handful on a slow one. So on a phone drawing ~9
         * frames a second the marker's entire 400 ms life was spent inside one
         * or two frames -- five cycles charged at once, the marker retired
         * before the frame after it was ever drawn. It was on screen for two
         * frames, both showing frame 0, which is a flicker and not an
         * animation.
         *
         * A marker exists to be looked at, so its clock is the clock of the
         * frames it is looked at in. Advancing by logic_frame_ms (the same
         * clamped elapsed the movers are paid from, so a stall costs it the
         * same) plays the whole 400 ms across however many frames the device
         * manages, and guarantees at least one drawn frame per touch.
         */
        if( UIInk_IsActive(&app->ink) )
        {
            UIInk_Tick(&app->ink, (int)app->logic_frame_ms);
            app->need_redraw = 1;
        }

        /* The chosen minimenu row's afterimage: the same clock as the marker,
         * for the same reason -- it exists to be looked at. */
        if( UIMinimenu_AfterimageActive(&app->interact.minimenu) )
        {
            UIMinimenu_AfterimageTick(&app->interact.minimenu, (int)app->logic_frame_ms);
            app->need_redraw = 1;
        }

        if( ticks > 0 )
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_LOGIC_TICKS, ticks);
            for( int t = 0; t < ticks; t++ )
            {
                TORIRS_PERF_SCOPE_CS2()
                {
                    if( app_logic_tick(app) )
                        app->need_redraw = 1;
                }

                /* A catch-up pass can process several server ticks in one
                 * App_RunOnce.  Settle tick N's client scripts before tick
                 * N+1 is allowed to apply, or the fence would be semantically
                 * crossed even though the packet runner stopped at it. */
                if( app->runner.frame_settle_pending )
                {
                    enum TaskRunnerStat stat;

                    TORIRS_PERF_SCOPE_CS2()
                    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_CS2_SETTLE)
                    {
                        stat = app_settle_cs2_frame(app);
                    }
                    if( stat != TASK_RUNNER_IDLE )
                    {
                        app->runner_had_work = 1;
                        ticks = t + 1;
                        break;
                    }
                    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_LAYOUT)
                    {
                        UITree_LayoutResolve(
                            app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
                    }
                }
            }
            /* The settle fence above can stop the catch-up part way through.
             * The accumulator has already been charged for every tick it paid
             * out, so hand back the ones that did not run -- otherwise a frame
             * that stopped at the fence silently swallows simulation time and
             * the world falls behind the clock it is supposed to keep. */
            if( ticks < ticks_paid )
                app->cycle_accum_ms += (double)(ticks_paid - ticks) * (double)APP_LOGIC_TICK_MS;
        }
        else
        {
            ticks = 0;
        }

        /* The movers interpolate the same elapsed time the tick accumulator
         * was just given -- already clamped, and measured off the same clock.
         * They read the fraction where the logic reads whole cycles, which is
         * the only difference between the two that should exist. */
        app_world_frame(app, ticks, (float)app->logic_frame_ms / (float)APP_LOGIC_TICK_MS);
    }

    /* A logout inside one of those ticks took the gameframe down with it: the
     * tree below is empty and the bake that replaces it has asked for the last
     * frame to stand. Leave before the interaction pass rebuilds an emit list
     * from nothing -- this is the frame that would otherwise draw the boot bar
     * the hold exists to avoid, the early-out above catching only the frames
     * after it. @see App_BootHoldsLastFrame. */
    if( App_BootHoldsLastFrame(app) )
    {
        /* The frame still TOOK this input: the click it carried is the one
         * that logged out. Leaving it unconsumed would hold it over (a press
         * still held survives a BOOTING frame by design) and replay it on the
         * login form that is about to appear. */
        app->input_frame_consumed = 1;
        return 0;
    }

    /* Resume a parked packet pipeline at frame rate, not tick rate. A packet
     * task that yielded for an asynchronous cache read (the norm on web past
     * READY) has its response delivered by the platform pump at the top of
     * the very next frame; leaving the resume to the next 20ms logic tick
     * held the visual latch a full tick per read, and a hitsplat whose
     * sprite+sound chain was several reads deep froze the world for that
     * many ticks — every server cycle, in combat. When nothing is parked
     * and no new packet is queued, this does not run. Newly received packets
     * must also enter this pipeline: a paused client has no next logic tick to
     * start it, so publish-only server responses otherwise wait forever. This
     * drains the existing serial protocol queue without advancing simulation. */
    if( app->exec_runner_had_work || app->server_tick_open || RS_ClientScriptQueue_Count(&app->pending_clientscripts) > 0 ||
        (app->net && app->net->packets_head) )
    {
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TICK_PACKETS)
        {
            if( app_pump_net_packets(app) )
                app->need_redraw = 1;
        }
    }

    /* Timer hooks, packet-fence RUNCLIENTSCRIPTs, and other tick work enqueue
     * CS2 before interaction.  Settle them now so hit testing sees one coherent
     * tree rather than the intermediate state of a yielding script. */
    if( ran_cs2 || app->runner.frame_settle_pending )
    {
        enum TaskRunnerStat stat;

        TORIRS_PERF_SCOPE_CS2()
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_CS2_SETTLE)
        {
            stat = app_settle_cs2_frame(app);
        }
        if( stat != TASK_RUNNER_IDLE )
        {
            app->runner_had_work = 1;
            app_frame_latch_note(app, stat == TASK_RUNNER_BLOCKED ? "cs2_blocked" : "cs2_pending");
            return 0;
        }
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_LAYOUT)
        {
            UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        }
        ran_cs2 = 0;
    }

    /* A server-pushed script is held until its tick fence so it observes all
     * state packets from that tick.  Its accompanying IF_SETHIDE/IF_SETTEXT
     * packets may already have mutated the live tree; retain the prior frame
     * until the script has actually been dispatched and settled. */
    if( !App_FrameSettled(app) )
    {
        app_frame_latch_note(
            app,
            app->runner_had_work               ? "settled:runner_had_work"
            : app->runner.frame_settle_pending ? "settled:frame_settle_pending"
            : app->exec_runner_had_work        ? "settled:exec_runner_had_work"
            : app->server_tick_open            ? "settled:server_tick_open"
                                               : "settled:pending_clientscripts");
        return 0;
    }
    /* Reconcile the gameframe only after the pre-interaction CS2 transaction
     * is complete. Applying it at the head of App_RunOnce made every later
     * CC_DELETEALL/CC_CREATE hand interaction a tree one generation newer
     * than the semantic bindings it was using. */
    { uint64_t const pa_w0 = PerfAudit_Now(); App_PluginLayoutTick(app);
      PA_ADD(layout_tick_ns, PerfAudit_Now() - pa_w0); }
    /* A popup retained from the previous frame must not keep native rows live
     * after that reconciliation suppressed or rebuilt their component. */
    app_minimenu_close_if_stale(app);
    app_frame_latch_note(app, NULL);

    app->input_frame_consumed = 1;

    /* Per-frame interaction: returns intents; the app applies event context
     * and dispatches each hook through the game layer. */
    /* Publish this frame's key state before any hook runs, so KEYHELD and
     * KEYPRESSED answer about the frame the script is reacting to. */
    RS_CS2_SyncKeyState(&app->host, input);
    RS_CS2_SyncMouseState(&app->host, input);

    /* Reference keyHeld[5]: read inside tryMove, so it applies to ground,
     * minimap AND interaction clicks. Latched here because the minimenu action
     * path that runs the last two has no input handle. */
    app->ctrl_held = LibToriRS_Input_IsKeyHeld(input, TORIRSK_CTRL) ? 1 : 0;

    /* The cycle the widget timers just ran on. onMouseRepeat has to be paired
     * with them: the cache's mouseover container is torn down and rebuilt by a
     * per-cycle timer, and the repeat is what puts the tooltip back. */
    app->interact.client_cycle = app->logic_cycle;

    /*
     * The touch marker, shown for every press that is not a drag.
     *
     * Here, and not where the cross is set, because that is the point: the
     * cross is shown by the paths that DID something, and a tap that hits a
     * widget, misses every target, or lands during a modal shows nothing at
     * all. On a touchscreen there is no pointer to prove the device saw it, so
     * the marker goes up before anything has decided what the press meant.
     *
     * The colour is the walk one at this stage -- "a touch happened". If the
     * press turns out to be an interaction, RS_Minimenu's cross path refines it
     * through UIInk_SetColour a moment later, in the same frame, without
     * restarting the animation.
     *
     * BEFORE the interact walk, and the order is load-bearing: a touch tap
     * delivers its press and release in ONE command batch (touch_click), so
     * the frame that shows the marker is also the frame whose click dispatch
     * refines it. SetColour on an ink not yet shown is a no-op, and a Show
     * after the dispatch repaints the refinement yellow -- either wrong order
     * is a tap that never turns red, however red the cross says it was.
     *
     * Costs nothing on a lane with no inkwell component: the state ticks and
     * the emit never asks for it.
     *
     * ## Not for a drag
     *
     * A drag is not a tap: the finger scrolling a list, turning the camera or
     * carrying an inventory slot is being followed continuously and needs no
     * proof the glass saw it, and a ripple parked at the grab point for the
     * rest of the gesture is noise.
     *
     * The question is asked of the input layer's gesture state rather than
     * re-derived here, because that is where a MOUSE drag is already decided
     * (press origin, the 5px deadzone, the dead time -- try_start_drag in
     * input/torirs_input.c) and both pointers must answer it the same way.
     *
     * Both halves are needed and they catch different lanes:
     *
     *  - The CANCEL is the mouse's. Its press is a tap until it isn't: the
     *    marker goes up on the press edge and comes back down some frames
     *    later, when the hand has travelled far enough to be a drag.
     *  - The SUPPRESSION is the finger's. torirs_touch.c holds the button
     *    back until the finger passes the 12px touch slop, then pushes the
     *    down at the START point followed by a move to where the finger now
     *    is -- both in one batch, so the deadzone is already crossed and the
     *    press arrives ALREADY dragging. There is no later edge to cancel on;
     *    the marker simply must not go up.
     */
    if( LibToriRS_Input_IsDragging(input, TORIRSM_LEFT) ||
        LibToriRS_Input_IsDragging(input, TORIRSM_RIGHT) ||
        LibToriRS_Input_IsDragging(input, TORIRSM_MIDDLE) )
    {
        UIInk_Cancel(&app->ink);
    }
    else if(
        input->curr.mouse_button_down[TORIRSM_LEFT] ||
        input->curr.mouse_button_down[TORIRSM_RIGHT] )
    {
        UIInk_Show(&app->ink, TORIRS_INKWELL_YELLOW, input->curr.mouse_x, input->curr.mouse_y);
    }

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_INTERACT)
    {
        UITree_InteractFrameWithPointerOwner(
            &app->interact,
            app->tree,
            &app->ui_host,
            input,
            now_ms,
            0,
            app->chrome_pointer_owned,
            &out);
    }
    plugin_pointer_consumed = out.minimenu_consumed_pointer;

    /* World hover: gate on the mouse being over the world element. The pick
     * itself runs inside App_Render (hittest right after each visible model
     * projects), so here we only latch the mouse point the next render picks
     * at; hover tile and pickset are the last rendered frame's (world frames
     * always mark need_redraw, so at most one frame stale). */
    app_update_world_viewport(app);
    /* A viewport that only appeared now (mounted interface, unhidden layer)
     * pulls the map in on first sight — the map is loaded iff the tree has a
     * world element, never eagerly. Networked boots wait for the server's
     * REBUILD_NORMAL instead (a default region load would race/clobber it). */
    if( app->world_view_valid && !app->world_load_attempted && !app->net_enabled )
        app_world_load_begin(app, NULL, 0);
    /*
     * `mouse_pointer_absent` is the touch host saying the finger that WAS the
     * pointer has lifted (LibToriRS_Input_PushMouseLeave). The position it left
     * behind is still needed -- the popup it opened is anchored there -- but
     * nothing is hovering it any more, so the hover markers and the pick that
     * feeds them go quiet. Without this the tile indicator keeps outlining the
     * tile of the last tap for as long as the client runs, and the world pick
     * keeps re-answering it every frame.
     */
    app->pointer_absent = input->mouse_pointer_absent;
    app->world_mouse_in_viewport =
        !app->pointer_absent && app_world_mouse_gate(app, input->curr.mouse_x, input->curr.mouse_y);
    app->world_mouse_x = input->curr.mouse_x;
    app->world_mouse_y = input->curr.mouse_y;
    if( !app->world_mouse_in_viewport )
    {
        app->world_hover_tile_x = -1;
        app->world_hover_tile_z = -1;
        app->world_hover_view = 0;
        World_PickSetReset(&app->world_pickset);
    }

    /* Mouseover text before any click handling: the reference recomputes it
     * every cycle from the same menu the click paths build. */
    app_hover_text_update(app, input->curr.mouse_x, input->curr.mouse_y);

    /*
     * A chat filter button, if nothing above took the click.
     *
     * Dispatched HERE and not in the interact walk that found it, because a
     * plugin layout may own that rectangle: the modern frames make these
     * buttons open and close the chatbox and leave the filter to the right
     * click, and a walk that cycled on the way past would have changed the
     * setting before the layout ever saw the press.
     */
    if( out.chat_button_filter >= 0 )
    {
        struct UITreeHostRequest cycle_req = {
            .kind = UITREE_HOST_CYCLE_CHAT_FILTER_MODE,
            .u.chat_filter.filter = out.chat_button_filter,
        };
        UITree_Host(&app->ui_host, &cycle_req);
    }

    /* Minimenu gesture results (see interact_minimenu): option selected on
     * mousedown -> dispatch; right press with no menu open -> build + show. */
    if( out.minimenu_select >= 0 )
    {
        /* Before the dispatch hides the popup: the afterimage is cut from the
         * popup's geometry. Only where the pointer is a finger -- a mouse has
         * the hover it watched turn yellow, and the reference shows nothing. */
        if( app->touch_ui )
            UIMinimenu_AfterimageShow(&app->interact.minimenu, out.minimenu_select);
        if( app_minimenu_use_option(
                app, out.minimenu_select, input->curr.mouse_x, input->curr.mouse_y) )
            ran_cs2 = 1;
    }
    if( out.right_click )
    {
        /* The menu ctx reads app->world_pickset — the set the last rendered
         * frame hittested at the hover point (v1-style pickset-during-draw). */
        int click_in_world = app_world_mouse_gate(app, out.right_click_x, out.right_click_y);
        if( !click_in_world || !app_world_drawable(app) )
            World_PickSetReset(&app->world_pickset);
        app_minimenu_open(app, out.right_click_x, out.right_click_y, click_in_world);
    }

    /* Left click executes the DEFAULT menu entry (reference
     * chooseDefaultMenuEntry): build the same menu the right click would show
     * and run its top normal-priority row, suppressing the legacy click
     * intent. Components with no menu rows keep the legacy hook path — for
     * them the scratch menu is Cancel-only and default_idx is -1. */
    /* Minimap click-to-walk (chrome gesture from interact_click). */
    if( out.minimap_click && !out.minimenu_closed && out.minimenu_select < 0 )
    {
        app_minimap_click(
            app,
            out.minimap_click_x,
            out.minimap_click_y,
            LibToriRS_Input_IsKeyHeld(input, TORIRSK_CTRL));
        /* The minimap is a builtin widget with no component id, so this click
         * reaches neither the component default-row path nor the world/empty
         * space paths below — it is the one left click that bypasses every
         * doAction tail. Clicking it with a "Use"/spell armed is still clicking
         * off the selection, so disarm here. Done unconditionally, not only when
         * the walk was consumed: a click on the minimap frame or compass is just
         * as much a click away from the target. */
        app_selection_clear(app);
    }

    /* A left press over a filled inventory slot is owned by the slot machine
     * (app_inv_drag_tick): the reference freezes mouseLoop while objDragArea
     * is armed, so the release must not ALSO fire the generic default-entry
     * path here — the machine runs the default row itself on a short click. */
    struct UITreeObjCell pressed_cell;
    int const pressed_filled_obj =
        out.clicked_com_id >= 0 &&
        app_obj_cell_at(app, out.clicked_x, out.clicked_y, &pressed_cell);

    /*
     * An IF3 text-entry field takes the caret, and any other click gives it up.
     *
     * Ahead of the minimenu because a click on the field IS the whole action:
     * the box carries no op and no hook (`~torirs_cc_search_box` sets nothing
     * but a font, a colour and its input limits), so letting the click continue
     * would build a Cancel-only menu over it and, on a miss, walk the player.
     *
     * The blur half is not an afterthought: it fires the field's
     * `on_input_focus_changed` hook. (The hiscores panel used to search from
     * it, because that slot held the SUBMIT handler -- the two were swapped.
     * The client searches on Enter; a blur runs only its guard script.) Both
     * halves go through the host, which owns the hook dispatch -- see
     * RS_CS2_InputSetFocus.
     */
    int input_took_click = 0;
    if( (out.clicked_com_id >= 0 || out.clicked_node >= 0 || out.left_click_miss) &&
        !out.minimenu_closed && out.minimenu_select < 0 )
    {
        int const field =
            out.clicked_com_id >= 0
                ? UITree_InputHitTest(app->tree, &app->ui_host, out.clicked_x, out.clicked_y)
                : -1;
        int const had_focus = RS_CS2_InputFocusId(&app->host) >= 0;
        if( field >= 0 )
        {
            RS_CS2_InputSetFocus(&app->host, &app->runner, field);
            input_took_click = 1;
            app->input_frame_consumed = 1;
            app->need_redraw = 1;
        }
        else if( had_focus )
        {
            RS_CS2_InputSetFocus(&app->host, &app->runner, -1);
            app->need_redraw = 1;
        }
        if( field >= 0 || had_focus )
            RS_CS2_PumpTransmits(&app->host, &app->runner);
    }

    /* Keyed on the clicked NODE, not only its component id: a plugin-owned
     * control has no component id, and keying on the id alone dropped its click
     * on the floor -- neither a UI click here nor a world click below, because
     * the interactive hit had already closed the world gate. */
    if( !input_took_click && app->inv_drag.component_id < 0 &&
        !pressed_filled_obj && (out.clicked_com_id >= 0 || out.clicked_node >= 0) &&
        !out.minimenu_closed && out.minimenu_select < 0 )
    {
        struct RS_MinimenuBuildCtx mctx = {
            .tree = app->tree,
            .ui_host = &app->ui_host,
            .provider = app->provider,
            .runner = &app->runner,
            .invs = &app->invs,
            .chat = &app->chat_source,
            .events_for_component = app_minimenu_events_for_component,
            .events_user = app,
            .player_ops = (char const(*)[40])app->player_ops,
            .player_ops_primary = app->player_ops_primary,
            .player_attack_option = app->player_attack_option,
            .npc_attack_option = app->npc_attack_option,
            .attack_option_model = app->features->attack_option_model,
            .world = app->world,
            .world_pickset = NULL, /* UI hit: mouse was over a component */
            .click_in_world = false,
            /* Honour an armed "Use"/spell selection so the left-click default
             * row matches the right-click menu (reference doAction runs the
             * same chooseDefaultMenuEntry over the useMode/targetMode menu). */
            .selection = app_minimenu_selection(app),
            .locedit_active = app->locedit.visible != 0,
            .mapedit_select_active = app_mapedit_select_active(app),
            .plugin_io_down = app_plugin_io_down(app) != 0,
            .wevs = &app->wevs,
            .view_world_fn = app_minimenu_view_world,
            .view_world_user = app,
        };
        struct UIMinimenu scratch;
        int default_idx;

        UIMinimenu_Reset(&scratch);
        scratch.font_id = app->interact.minimenu.font_id;
        app_sailing_menu_context(app, &mctx, out.clicked_x, out.clicked_y);
        RS_Minimenu_Build(&mctx, out.clicked_x, out.clicked_y, &scratch);
        app_minimenu_stamp_node_identities(app, &scratch);
        app_plugin_menu_build(app, &scratch, 0);
        default_idx = RS_Minimenu_DefaultOptionIndex(&scratch);
        /*
         * TORIRS_CLICK_DEBUG=1: what the left click resolved to.
         *
         * A left click runs the *default row of the menu the right click would
         * have shown*, and every step of that is invisible: which rows were
         * built, which one is the default, and which component and op the row
         * points at. When a widget "does nothing", the question is always which
         * of those four went wrong, and the right-click menu looking correct
         * rules out only the first.
         */
        if( getenv("TORIRS_CLICK_DEBUG") )
        {
            TORIRS_LOG(
                "clickdbg: com=0x%x rows=%d default=%d\n",
                out.clicked_com_id,
                scratch.option_count,
                default_idx);
            for( int i = 0; i < scratch.option_count; i++ )
                TORIRS_LOG(
                    "  row[%d] '%s' action=%d idx=%d pick=%d id=0x%x\n",
                    i,
                    scratch.options[i].text,
                    scratch.options[i].action,
                    scratch.options[i].action_index,
                    (int)scratch.options[i].pick.kind,
                    scratch.options[i].pick.id);
        }
        if( default_idx >= 0 )
        {
            /* Steal the row set: use_option consumes interact.minimenu. */
            struct UIMinimenu saved = app->interact.minimenu;
            app->interact.minimenu = scratch;
            if( app_minimenu_use_option(app, default_idx, out.clicked_x, out.clicked_y) )
                ran_cs2 = 1;
            app->interact.minimenu = saved;

            /* Drop the legacy click intent so the hook does not run twice;
             * hover/wheel/hold intents pass through untouched. Clicks carry
             * event_mouse too (slider tracks need it), so intent kind rather
             * than event context distinguishes them. */
            {
                int kept = 0;
                for( int i = 0; i < out.intent_count; i++ )
                {
                    struct UIIntent const* intent = &out.intents[i];
                    if( intent->is_click &&
                        (intent->component_id == out.clicked_com_id || intent->component_id < 0) )
                        continue;
                    out.intents[kept++] = out.intents[i];
                }
                out.intent_count = kept;
            }
        }
        else if( app->objsel.active || app->targetsel.active )
        {
            /* The click landed on a component that offers no menu row (an empty
             * inventory slot, sidebar chrome — the general hit test resolves
             * these to the pass-through RS_INV/panel, so clicked_com_id is set
             * but the scratch menu is Cancel-only, default_idx < 0). The
             * reference still runs doAction on that Cancel row and its tail
             * (Client.ts:9506) clears useMode/targetMode; torirs's
             * DefaultOptionIndex returns -1 for a Cancel-only menu, so nothing
             * ran. Drop the armed selection here — clicking off any surface that
             * can't be a "use" target cancels and clears the white outline. */
            app_selection_clear(app);
        }
    }

    /* Left click over bare world (no UI component hit): run the default menu
     * entry from world rows only — Walk here / nearest entity op (reference
     * chooseDefaultMenuEntry over the last rendered frame's pickset). */
    if( torirs_env_net_debug() && (out.left_click_miss || out.clicked_com_id >= 0) )
        TORIRS_LOG(
            "click: miss=%d (%d,%d) com=0x%x gate=%d drawable=%d picks=%d\n",
            out.left_click_miss,
            out.left_click_miss_x,
            out.left_click_miss_y,
            out.clicked_com_id,
            out.left_click_miss
                ? app_world_mouse_gate(app, out.left_click_miss_x, out.left_click_miss_y)
                : -1,
            app_world_drawable(app),
            app->world_pickset.count);
    if( app->inv_drag.component_id < 0 && out.left_click_miss && !out.minimenu_closed &&
        out.minimenu_select < 0 &&
        app_world_mouse_gate(app, out.left_click_miss_x, out.left_click_miss_y) &&
        app_world_drawable(app) )
    {
        struct RS_MinimenuBuildCtx mctx = {
            .tree = app->tree,
            .ui_host = &app->ui_host,
            .provider = app->provider,
            .runner = &app->runner,
            .invs = &app->invs,
            .chat = &app->chat_source,
            .events_for_component = app_minimenu_events_for_component,
            .events_user = app,
            .player_ops = (char const(*)[40])app->player_ops,
            .player_ops_primary = app->player_ops_primary,
            .player_attack_option = app->player_attack_option,
            .npc_attack_option = app->npc_attack_option,
            .attack_option_model = app->features->attack_option_model,
            .world = app->world,
            .world_pickset = &app->world_pickset,
            .click_in_world = true,
            /* Honour an armed "Use"/spell selection so a left-click on a world
             * target casts/uses (the TGT and USEHELD_ON rows) instead of
             * falling back to the target's default op. Without this a left-click
             * on an NPC with a spell armed built the plain ops and defaulted to
             * Attack (walk-to-melee "run up"), while the right-click menu cast. */
            .selection = app_minimenu_selection(app),
            .locedit_active = app->locedit.visible != 0,
            .mapedit_select_active = app_mapedit_select_active(app),
            .plugin_io_down = app_plugin_io_down(app) != 0,
            .wevs = &app->wevs,
            .view_world_fn = app_minimenu_view_world,
            .view_world_user = app,
        };
        struct UIMinimenu scratch;
        int default_idx;

        UIMinimenu_Reset(&scratch);
        scratch.font_id = app->interact.minimenu.font_id;
        app_minimenu_ctx_ground_fallback(app, &mctx, out.left_click_miss_x, out.left_click_miss_y);
        app_sailing_menu_context(app, &mctx, out.left_click_miss_x, out.left_click_miss_y);
        RS_Minimenu_Build(&mctx, out.left_click_miss_x, out.left_click_miss_y, &scratch);
        app_minimenu_stamp_node_identities(app, &scratch);
        app_plugin_menu_build(app, &scratch, 0);
        default_idx = RS_Minimenu_DefaultOptionIndex(&scratch);
        if( default_idx >= 0 )
        {
            struct UIMinimenu saved = app->interact.minimenu;
            app->interact.minimenu = scratch;
            if( app_minimenu_use_option(
                    app, default_idx, out.left_click_miss_x, out.left_click_miss_y) )
                ran_cs2 = 1;
            app->interact.minimenu = saved;
        }
        else if( app->objsel.active || app->targetsel.active )
        {
            /* A world click with a use/spell mode armed but no valid target:
             * "Walk here" is suppressed while armed (rs_minimenu_world.c), so
             * the scratch menu is Cancel-only and default_idx < 0 — nothing
             * ran. The reference still runs doAction on that Cancel row, whose
             * tail (Client.ts:9506) clears useMode/targetMode. Without this the
             * selection stays armed forever: Walk here never returns, so every
             * later world click is also inert and the world reads as
             * "unclickable". Drop the armed selection and its white outline. */
            app_selection_clear(app);
        }
    }

    /* Left click on empty, non-world space — an inventory gap, the sidebar
     * chrome, the chat area — hits no component (RS_INV is pass-through, §21.4)
     * and no world default row runs, so none of the doAction paths above fire.
     * The reference still runs doAction there on a Cancel-only menu, whose tail
     * (Client.ts:9506) clears useMode/targetMode. Mirror just that: a plain left
     * click off anything that can be a "use" target drops the armed selection
     * and its white outline. (A world miss with a default row is consumed above
     * and already cleared; a filled slot is owned by the drag machine.) */
    if( app->inv_drag.component_id < 0 && out.left_click_miss && !out.minimenu_closed &&
        out.minimenu_select < 0 && (app->objsel.active || app->targetsel.active) &&
        !(app_world_mouse_gate(app, out.left_click_miss_x, out.left_click_miss_y) &&
          app_world_drawable(app)) )
    {
        app_selection_clear(app);
    }

    app->hover_com_id = out.hover_com_id;
    if( out.clicked_com_id >= 0 )
        app->clicked_com_id = out.clicked_com_id;
    if( out.need_redraw )
        app->need_redraw = 1;

    /* Inventory slot machine owns the press (reference freezes mouseLoop while
     * objDragArea != 0). InteractFrame still emits a deferred-click on_op
     * intent for IF3 cells that carry cc_setonop(cc_settrans_temporarily) —
     * which would dim the icon even when the short-click default is "Use"
     * (OPHELDT_START). Client-TS never sets selectedArea for Use. Drop those
     * click intents here; the machine runs the default row itself, and
     * OPHELD1-5 / INV_BUTTON / IF_BUTTON re-fire on_op from inv_action with the
     * op the action actually chose.
     *
     * `pressed_filled_obj` is the same statement for a press and its release in
     * ONE frame -- a finger's tap. The machine has not ticked yet when this
     * runs (app_inv_drag_tick is later in this function), so its own field is
     * still -1 and the intent survived. It then ran the cell's on_op with op
     * index 1, which on an inventory slot is the SHIFT-CLICK handler, and that
     * drops: a tap on a rune platebody sent the wear, was refused by the
     * server, and dropped the platebody in the same tick. */
    if( app->inv_drag.component_id >= 0 || pressed_filled_obj )
    {
        int kept = 0;
        for( int i = 0; i < out.intent_count; i++ )
        {
            struct UIIntent const* intent = &out.intents[i];
            if( intent->is_click )
                continue;
            out.intents[kept++] = out.intents[i];
        }
        out.intent_count = kept;
    }

    /* InteractFrame collected these against one tree snapshot, but menu/default
     * actions above may already have run CS2 and hidden or deleted a later
     * target.  Compact before touching any retained hook pointer. */
    {
        int kept = 0;
        for( int i = 0; i < out.intent_count; i++ )
        {
            if( !app_intent_targets_live(app, &out.intents[i]) )
                continue;
            out.intents[kept++] = out.intents[i];
        }
        out.intent_count = kept;
    }

    /* Snapshot hooks by value before dispatching anything: intent->hook points
     * into tree->components[], and an earlier intent's script can CC_CREATE
     * (realloc) or CC_DELETEALL (reclaim/reuse the slot), dangling the pointer. */
    {
        struct UITreeRuntimeScriptHook hook_copies[UI_INTENT_MAX];
        /* Drag hooks must not sit behind a yielding head on the main FIFO —
         * drag_visual updates the thumb middle every frame while on_drag (caps +
         * if_setscrollpos) would starve. Side-queue + drain matches the
         * reference's synchronous ScriptEvent invoke during drag. */
        struct ToriRS_TaskQueue* drag_queue = NULL;
        struct TaskRunner drag_runner;

        for( int i = 0; i < out.intent_count; i++ )
            if( out.intents[i].hook )
                hook_copies[i] = *out.intents[i].hook;

        for( int i = 0; i < out.intent_count; i++ )
        {
            struct UIIntent const* intent = &out.intents[i];
            struct TaskRunner* dest = &app->runner;
            /* An earlier intent in this same batch can hide/delete this one.
             * Re-resolve at the last possible point before dispatch. */
            if( !app_intent_targets_live(app, intent) )
                continue;
            /* Set the op index explicitly per intent rather than relying on the
             * host default, so one intent's op cannot leak into the next.
             * Unset (0) means the primary left-click op, which is what every
             * mouse-driven dispatch reports; op-key matches carry their own. */
            RS_CS2_SetEventOp(&app->host, intent->op_index > 0 ? intent->op_index : 1, 0);
            if( intent->has_event_mouse )
                RS_CS2_SetEventMouse(&app->host, intent->event_mouse_x, intent->event_mouse_y);
            if( intent->has_drag_target )
            {
                RS_CS2_SetEventDragTarget(&app->host, app->tree, intent->drag_target_id);
                if( !drag_queue )
                {
                    drag_queue = ToriRS_TaskQueue_New();
                    drag_runner.queue = drag_queue;
                    drag_runner.io = app->runner.io;
                    drag_runner.px = app->runner.px;
                }
                dest = &drag_runner;
            }
            RS_CS2_DispatchHook(
                &app->host, dest, intent->component_id, intent->hook ? &hook_copies[i] : NULL);
            ran_cs2 = 1;
        }

        if( drag_queue )
        {
            TaskRunner_Drain(&drag_runner);
            ToriRS_TaskQueue_Free(drag_queue);
        }
    }

    /* Chat input focus, before the keys are handed out: which onKey hooks may
     * see this frame's keys is a focus question, and the frame's own clicks and
     * Enter are part of the answer. */
    int chat_submit_pending = 0;
    int const chat_keys_suppressed =
        app_chat_focus_tick(app, input, plugin_pointer_consumed, &chat_submit_pending);

    /*
     * Plugins see the keyboard before the interface scripts do.
     *
     * Reported as transitions of the key_down/key_up arrays rather than from
     * the key-event stream, because those arrays are indexed by
     * LibToriRS_KeyCode -- the same space api->key_held answers in. The event
     * stream carries OSRS key codes instead, and handing a plugin two
     * different numbering schemes for "which key" is how a handler ends up
     * gating on the wrong one.
     *
     * CONSUME suppresses this frame's onKey broadcast, which is the whole of
     * what a key means to the interface layer. It deliberately does not reach
     * the chat focus decision above: that has already run, and a plugin
     * silently eating a keystroke the chat box was waiting for is a worse
     * failure than not being able to intercept it.
     */
    /*
     * A chrome field under the caret takes the keyboard from the game.
     *
     * The plugin window's text fields are the MODEL's -- the host routes keys
     * into them (app_chrome_route_keys) long before this point -- and nothing
     * downstream knew it. So typing a colour into a plugin's field also ran
     * every armed onKey script and typed the same characters into the chat
     * line, and an Enter meant to commit the field sent whatever was in the
     * chat box. Folded in beside the plugin-consume flag because it means the
     * same thing to everything below: these keys are already spoken for.
     */
    int const chrome_ate_keys = app_chrome_holds_keyboard(app);

    int plugin_ate_keys = 0;
    if( app->plugins )
    {
        for( int k = 0; k < TORIRSK_COUNT; k++ )
        {
            if( LibToriRS_Input_IsKeyDown(input, (enum LibToriRS_KeyCode)k) )
                plugin_ate_keys |= PluginHost_Key(app->plugins, k, 0, true);
            if( LibToriRS_Input_IsKeyUp(input, (enum LibToriRS_KeyCode)k) )
                plugin_ate_keys |= PluginHost_Key(app->plugins, k, 0, false);
        }
    }

    /*
     * A focused IF3 text-entry field eats the keyboard.
     *
     * Before the broadcast and instead of it, which is the point: the cache's
     * onKey handlers are the chatbox's typed line and the gameframe's F-key tab
     * switches, and a panel search box that let them run would type a player's
     * name into the chat box and change sidebar tabs on every `f`. The
     * cache-authored search boxes take the keyboard by calling
     * `~chatdefault_stopinput`; a type-12 field has no such call because in the
     * reference the caret itself is the claim. This is that claim.
     *
     * Every key is consumed while a focused field is available, including ones the field
     * does nothing with -- see RS_CS2_InputKey.
     */
    int input_ate_keys = 0;
    if( !plugin_ate_keys && !chrome_ate_keys && RS_CS2_InputFocusId(&app->host) >= 0 )
    {
        for( int e = 0; e < out.key_event_count; e++ )
        {
            input_ate_keys |= RS_CS2_InputKey(
                &app->host,
                &app->runner,
                &app->ui_host,
                out.key_events[e].key_typed,
                out.key_events[e].key_pressed);
        }
        if( input_ate_keys )
        {
            RS_CS2_PumpTransmits(&app->host, &app->runner);
            ran_cs2 = 1;
            app->need_redraw = 1;
        }
    }

    /* Keyboard broadcast: every event this frame times every visible onKey
     * handler, with nothing filtered out on the way -- which is the reference
     * client's dispatch exactly. Every registered hook gets every key and each
     * script decides for itself: the chatbox's takes the printable ones into
     * the typed line, the gameframe's takes the F-keys and switches tabs, and
     * a panel with a search box takes the keyboard by disarming the chatbox's
     * hook rather than by anything the client does. There is no client-side
     * chat focus at a cache revision to route them by. Unlike the intent loop above
     * this re-resolves each component id immediately before dispatching it
     * rather than snapshotting hooks up front -- a broadcast runs many scripts
     * in one frame, and an earlier one can CC_CREATE (realloc components[]) or
     * CC_DELETEALL (reclaim the slot), so a target collected during the scan may
     * be gone by its turn. Same reasoning as the on_timer loop. */
    for( int e = 0;
         e < out.key_event_count && !plugin_ate_keys && !chrome_ate_keys && !input_ate_keys;
         e++ )
    {
        for( int t = 0; t < out.key_target_count; t++ )
        {
            struct UIKeyTarget const* target = &out.key_targets[t];
            int32_t idx;
            if( !(target->hooks & UI_KEY_HOOK_TYPED) )
                continue;
            idx = target->node_index;
            if( idx < 0 || (uint32_t)idx >= app->tree->component_count ||
                app->tree->components[idx].freed ||
                app->tree->components[idx].incarnation != target->node_incarnation ||
                app->tree->components[idx].component_id != target->component_id ||
                UITree_NodeOrAncestorDisplayHidden(app->tree, idx) )
                continue;
            /* Re-check the hook too: the id may have been reclaimed and handed
             * to a different node since collection. */
            if( UITree_Hooks(&app->tree->components[idx])->on_key.script_id <= 0 )
                continue;
            RS_CS2_SetEventMouse(
                &app->host, out.key_mouse_x - target->abs_x, out.key_mouse_y - target->abs_y);
            RS_CS2_SetEventKey(
                &app->host, out.key_events[e].key_typed, out.key_events[e].key_pressed);
            if( torirs_env_key_debug() )
                TORIRS_LOG(
                    "key_dispatch: com=0x%08x script=%d typed=%d pressed=%d\n",
                    target->component_id,
                    UITree_Hooks(&app->tree->components[idx])->on_key.script_id,
                    out.key_events[e].key_typed,
                    out.key_events[e].key_pressed);
            RS_CS2_DispatchHook(
                &app->host,
                &app->runner,
                target->component_id,
                &UITree_Hooks(&app->tree->components[idx])->on_key);
            ran_cs2 = 1;
        }
    }

    /* Key-down and key-up broadcasts, the same shape as the onKey loop above
     * (re-resolve the id, re-check the hook) over the frame's pressed and
     * released key codes. These are what the inventory registers to rebuild
     * itself while shift is held: script6007 arms both, script6008 filters to
     * key 81 (shift) and re-runs the slot builder with shift down or up, and
     * script6012 then promotes the shift-click op to op 1. Only the code is
     * reported — the reference passes no character here, so `event_key` is set
     * and the char left at 0. */
    for( int pass = 0; pass < 2; pass++ )
    {
        int const down = pass == 0;
        int const code_count = down ? out.key_down_count : out.key_up_count;
        int const* codes = down ? out.key_down_codes : out.key_up_codes;
        int const want = down ? UI_KEY_HOOK_DOWN : UI_KEY_HOOK_UP;

        for( int e = 0; e < code_count; e++ )
        {
            for( int t = 0; t < out.key_target_count; t++ )
            {
                struct UIKeyTarget const* target = &out.key_targets[t];
                struct UITreeRuntimeScriptHook const* hook;
                int32_t idx;
                if( !(target->hooks & want) )
                    continue;
                idx = target->node_index;
                if( idx < 0 || (uint32_t)idx >= app->tree->component_count ||
                    app->tree->components[idx].freed ||
                    app->tree->components[idx].incarnation != target->node_incarnation ||
                    app->tree->components[idx].component_id != target->component_id ||
                    UITree_NodeOrAncestorDisplayHidden(app->tree, idx) )
                    continue;
                hook = down ? &UITree_Hooks(&app->tree->components[idx])->on_key_down
                            : &UITree_Hooks(&app->tree->components[idx])->on_key_up;
                if( hook->script_id <= 0 )
                    continue;
                RS_CS2_SetEventMouse(
                    &app->host, out.key_mouse_x - target->abs_x, out.key_mouse_y - target->abs_y);
                RS_CS2_SetEventKey(&app->host, codes[e], 0);
                if( torirs_env_key_debug() )
                    TORIRS_LOG(
                        "key_%s_dispatch: com=0x%08x script=%d key=%d\n",
                        down ? "down" : "up",
                        target->component_id,
                        hook->script_id,
                        codes[e]);
                RS_CS2_DispatchHook(&app->host, &app->runner, target->component_id, hook);
                ran_cs2 = 1;
            }
        }
    }

    /* Chat input: typed characters/backspace/return feed whichever chat
     * input line is open (reference handleInputKey — typing goes to the chat
     * line even while op-key bindings also fire). Only when a chat region
     * exists (dat1 gameframe), and not while the loc editor is open -- it
     * already forced every chat-focus flag off this frame, and W/A/S/D/R/
     * Space/Backspace are its keys while it's up, not chat's.
     *
     * Focus itself is not decided here: app_chat_focus_tick above owns it for
     * every revision, because a cache chatbox has a focus state too and only
     * its *typing* is a clientscript's. What is left below is the typing. */
    /*
     * The title screen takes every key, ahead of all the in-game routing.
     *
     * Ahead of it AND to the exclusion of it: the game's key passes are not
     * merely irrelevant on a login form, they are wrong. The chat branch below
     * would decline on its own (a title tree has no chat node), but the camera
     * keys and the hotkey passes further down would happily act on a keystroke
     * meant for a password.
     */
    title_captures_keys = app->screen == APP_SCREEN_TITLE || app->screen == APP_SCREEN_CONNECTING;
    if( title_captures_keys )
    {
        for( int e = 0; e < input->key_event_count; e++ )
        {
            if( RS_Title_HandleKey(
                    &app->title, input->key_events[e].key_typed, input->key_events[e].key_pressed) )
                app_title_state_changed(app);
        }
    }

    if( !title_captures_keys && app_chat_node_index(app) >= 0 && !app->locedit.visible )
    {
        int chat_captures =
            app->chat_input_active || app->chat.social_input_open || app->chat.dialog_input_open;

        /* Straight from the input queue: out.key_events only fills when some
         * component carries an onKey hook, but chat typing must work without
         * any (the dat1 packs have none). A public-chat message submitted this
         * frame (input line empties, a new PUBLIC message from us appears at
         * the front) is forwarded to the server. */
        for( int e = 0; e < input->key_event_count; e++ )
        {
            /* Escape asks the server to close whatever modal is up — the same
             * CLOSE_MODAL the gameframe X's clientscript (29, if_close)
             * raises, so what actually closes stays the server's decision and
             * an idle Escape is a no-op there. Releasing the chat focus was
             * app_chat_focus_tick's, before the keys were routed. */
            if( input->key_events[e].key_typed == TORIRS_OSRSKEY_ESCAPE )
            {
                app->host.close_modal_requested = true;
                app->need_redraw = 1;
                continue;
            }
            /* A suppressed frame is one whose keys were focus commands (the
             * Enter that took focus, the Escape that dropped it); the line must
             * not type them as well. Nor may it type what a chrome field is
             * already taking. */
            if( chat_keys_suppressed || chrome_ate_keys || !chat_captures )
                continue;

            int had_input = app->chat.input[0] != '\0';
            /* Snapshot the input state a Return might submit, since HandleKey
             * clears it: a public line, a "::" cheat, a social prompt, or the
             * count/amount dialog. */
            char input_copy[sizeof(app->chat.input)];
            char social_copy[sizeof(app->chat.social_input)];
            char dialog_copy[sizeof(app->chat.dialog_input)];
            int was_social = app->chat.social_input_open;
            int social_type = app->chat.social_input_type;
            int was_dialog = app->chat.dialog_input_open;
            snprintf(input_copy, sizeof(input_copy), "%s", app->chat.input);
            snprintf(social_copy, sizeof(social_copy), "%s", app->chat.social_input);
            snprintf(dialog_copy, sizeof(dialog_copy), "%s", app->chat.dialog_input);

            if( RS_Chat_HandleKey(
                    &app->chat,
                    &app->social,
                    input->key_events[e].key_typed,
                    input->key_events[e].key_pressed) )
            {
                app->need_redraw = 1;

                /* Count/amount dialog submitted (was open, now closed). */
                if( was_dialog && !app->chat.dialog_input_open && dialog_copy[0] )
                    APP_NET_SEND(
                        app,
                        net_out_resume_countdialog(
                            app->net->rev,
                            app->net->random_out,
                            _nsbuf,
                            sizeof(_nsbuf),
                            (int)atol(dialog_copy)));
                /* Social prompt submitted: the local store op already ran in
                 * HandleKey; also notify the friend server. */
                else if( was_social && !app->chat.social_input_open && social_copy[0] )
                {
                    int64_t name37 = (int64_t)strtobase37(social_copy);
                    switch( social_type )
                    {
                    case RS_CHAT_SOCIAL_ADD_FRIEND:
                        APP_NET_SEND(
                            app,
                            net_out_friendlist_add(
                                app->net->rev,
                                app->net->random_out,
                                _nsbuf,
                                sizeof(_nsbuf),
                                name37));
                        break;
                    case RS_CHAT_SOCIAL_DEL_FRIEND:
                        APP_NET_SEND(
                            app,
                            net_out_friendlist_del(
                                app->net->rev,
                                app->net->random_out,
                                _nsbuf,
                                sizeof(_nsbuf),
                                name37));
                        break;
                    case RS_CHAT_SOCIAL_ADD_IGNORE:
                        APP_NET_SEND(
                            app,
                            net_out_ignorelist_add(
                                app->net->rev,
                                app->net->random_out,
                                _nsbuf,
                                sizeof(_nsbuf),
                                name37));
                        break;
                    case RS_CHAT_SOCIAL_DEL_IGNORE:
                        APP_NET_SEND(
                            app,
                            net_out_ignorelist_del(
                                app->net->rev,
                                app->net->random_out,
                                _nsbuf,
                                sizeof(_nsbuf),
                                name37));
                        break;
                    default:
                        break;
                    }
                }
                /* Public chat line submitted (had text, now cleared). "::" is
                 * a client-cheat command (reference), not a public message. */
                else if( had_input && app->chat.input[0] == '\0' && input_copy[0] )
                {
                    /*
                     * The reference keeps one cheat for itself: `::clientdrop`
                     * severs the connection locally so the lost-connection
                     * path can be exercised on demand (Client-TS
                     * Client.ts:3312). Without it the only way to reach that
                     * code is to genuinely lose a socket.
                     */
                    if( strcmp(input_copy, "::clientdrop") == 0 )
                        app_net_lost(app, "::clientdrop");
                    else if( input_copy[0] == ':' && input_copy[1] == ':' )
                        APP_NET_SEND(
                            app,
                            net_out_client_cheat(
                                app->net->rev,
                                app->net->random_out,
                                _nsbuf,
                                sizeof(_nsbuf),
                                input_copy + 2));
                    else if(
                        app->chat.message_count > 0 &&
                        app->chat.messages[0].type == RS_CHAT_TYPE_PUBLIC )
                    {
                        APP_NET_SEND(
                            app,
                            net_out_message_public(
                                app->net->rev,
                                app->net->random_out,
                                _nsbuf,
                                sizeof(_nsbuf),
                                app->chat.messages[0].text,
                                0));

                        /* Reference sets localPlayer.chatMessage on submit
                         * (Client.ts:3405) so our own overhead line shows
                         * immediately, before the server echoes it back through
                         * PLAYER_INFO. colour/effect default to 0/0 (no chat
                         * style selector ported yet). */
                        {
                            int local_idx = -1;
                            if( app->world &&
                                RS_EntitySync_FindPlayer(
                                    &app->esync,
                                    app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
                                    &local_idx,
                                    NULL) )
                                World_PlayerSetChat(
                                    app->world, local_idx, app->chat.messages[0].text, 0, 0);
                        }
                    }
                }
            }
        }

        /* Chat scrollbar: held left button over the scrollbar column drives the
         * arrows / grip (reference doScrollbar, gated on no chat dialog open —
         * the dialog pack replaces the message column + scrollbar). */
        {
            int rx = 0;
            int ry = 0;
            int rh = UI_CHATVIEW_NATIVE_HEIGHT;
            int left_held = LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);
            if( left_held && !out.minimenu_consumed_pointer && app->slots.chat_com_id == -1 &&
                app_chat_region(app, &rx, &ry, &rh, NULL) )
            {
                struct RS_ChatFilters filters = app_chat_filters(app);
                app->chat_scroll_cycle++;
                if( RS_Chat_ScrollbarInput(
                        &app->chat,
                        &filters,
                        rh,
                        input->curr.mouse_x - rx,
                        input->curr.mouse_y - ry,
                        app->chat_scroll_cycle) )
                    app->need_redraw = 1;
            }
            else
            {
                app->chat_scroll_cycle = 0;
                app->chat.scroll_grabbed = 0;
            }
        }

        /* Wheel over the chat region scrolls the message history. */
        if( input->curr.mouse_wheel_y != 0 )
        {
            int rx = 0;
            int ry = 0;
            int rh = UI_CHATVIEW_NATIVE_HEIGHT;
            if( app_chat_region(app, &rx, &ry, &rh, NULL) )
            {
                int32_t const chat_idx = app_chat_node_index(app);
                int bx = 0, by = 0, bw = 0, bh = 0;
                if( chat_idx >= 0 )
                {
                    struct UITreeComponent const* node = &app->tree->components[chat_idx];
                    UITree_LayoutGetBounds(&node->position, &bx, &by, &bw, &bh);
                    if( input->curr.mouse_x >= bx && input->curr.mouse_x < bx + bw &&
                        input->curr.mouse_y >= by && input->curr.mouse_y < by + bh )
                    {
                        struct RS_ChatFilters filters = app_chat_filters(app);
                        RS_Chat_Scroll(&app->chat, &filters, rh, input->curr.mouse_wheel_y);
                        app->need_redraw = 1;
                    }
                }
            }
        }
    }

    /* Click handlers can unhide tabs; pump immediately so the freshly visible
     * widgets populate this frame instead of one tick later. Early-outs when
     * nothing was unhidden. */
    if( out.intent_count > 0 || out.key_target_count > 0 )
        RS_CS2_PumpTransmits(&app->host, &app->runner);

    /* The line was sent this frame, so the focus goes with it: the next key
     * belongs to the hotkeys again until Enter asks for the line back. Applied
     * here rather than in the focus tick because the dat1 RS_Chat_HandleKey
     * loop had to have the frame's Enter first. Dead at a cache revision,
     * where there is no client-owned focus and `chat_submit_pending` is never
     * raised -- the chatbox's own onKey script owns submitting there. */
    if( chat_submit_pending && app->chat_input_active )
    {
        app->chat_input_active = 0;
        app->need_redraw = 1;
    }

    /* Every one of these reads the same key queue the login form just drained;
     * none of them has anything to act on before a world exists. */
    if( !title_captures_keys )
    {
        app_world_camera_keys(app, input, &out);
        app_world_camera_mouse(app, input, &out);
        /* Before the debug world hotkeys: a configured binding claims its key
         * so the same press cannot also spawn something. */
        app_ui_hotkeys(app, input);
        app_world_hotkeys(app, input, &out);
        app_inv_drag_tick(app, input, plugin_pointer_consumed);
        app_worldmap_drag_tick(app, input, plugin_pointer_consumed);
    }

    /* Idle timer (reference IDLE_TIMER after ~90s of no input). */
    if( input->key_event_count > 0 || input->curr.mouse_button_down[TORIRSM_LEFT] ||
        input->curr.mouse_button_down[TORIRSM_RIGHT] )
    {
        app->idle_frames = 0;
        app->idle_timer_sent = 0;
    }
    else if( ++app->idle_frames > 4500 && !app->idle_timer_sent )
    {
        app->idle_timer_sent = 1;
        APP_NET_SEND(
            app, net_out_idle_timer(app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf)));
    }

    if( ran_cs2 )
    {
        /* DispatchHook only enqueues. Settle so UI scripts (esp. on_drag →
         * scrollbar_vertical_drag's if_setscrollpos / cap cc_setposition) apply
         * before this frame's layout+emit. Reference runs ScriptEvents
         * synchronously while dragging; without this the middle thumb moves via
         * drag_visual while caps and the scroll layer stay a frame (or forever
         * under a busy queue) behind. */
        {
            enum TaskRunnerStat stat;

            TORIRS_PERF_SCOPE_CS2()
            {
                stat = app_settle_cs2_frame(app);
            }
            if( stat != TASK_RUNNER_IDLE )
            {
                app->runner_had_work = 1;
                return 0;
            }
            /* Press-time track onclick → cc_dragpickup stages pending during
             * the drain above. Consume it in the same frame so the thumb jumps
             * under the cursor now and keeps following it while held. */
            if( app->tree && app->tree->pending_drag_pickup )
            {
                int32_t node = UITree_ResolveRef(app->tree, app->tree->pending_drag_pickup_ref);
                if( node < 0 || app_displayable_component_node(
                                    app, app->tree->components[node].component_id) < 0 )
                    app->tree->pending_drag_pickup = 0;
            }
            if( app->tree && app->tree->pending_drag_pickup )
            {
                struct UIInteractOut pickup_out;
                int n = UITree_InteractConsumePendingDragPickup(
                    &app->interact, app->tree, &app->ui_host, input, &pickup_out);
                if( n > 0 )
                {
                    struct UITreeRuntimeScriptHook hook_copies[UI_INTENT_MAX];
                    struct ToriRS_TaskQueue* drag_queue = ToriRS_TaskQueue_New();
                    struct TaskRunner drag_runner = {
                        .queue = drag_queue,
                        .io = app->runner.io,
                        .px = app->runner.px,
                    };
                    for( int i = 0; i < pickup_out.intent_count; i++ )
                        if( pickup_out.intents[i].hook &&
                            app_intent_targets_live(app, &pickup_out.intents[i]) )
                            hook_copies[i] = *pickup_out.intents[i].hook;
                    for( int i = 0; i < pickup_out.intent_count; i++ )
                    {
                        struct UIIntent const* intent = &pickup_out.intents[i];
                        if( !app_intent_targets_live(app, intent) )
                            continue;
                        RS_CS2_SetEventOp(
                            &app->host, intent->op_index > 0 ? intent->op_index : 1, 0);
                        if( intent->has_event_mouse )
                            RS_CS2_SetEventMouse(
                                &app->host, intent->event_mouse_x, intent->event_mouse_y);
                        if( intent->has_drag_target )
                            RS_CS2_SetEventDragTarget(
                                &app->host, app->tree, intent->drag_target_id);
                        /* Same side-queue as the main intent loop — must not
                         * wait on a yielding main-queue head. */
                        RS_CS2_DispatchHook(
                            &app->host,
                            &drag_runner,
                            intent->component_id,
                            intent->hook ? &hook_copies[i] : NULL);
                    }
                    TaskRunner_Drain(&drag_runner);
                    ToriRS_TaskQueue_Free(drag_queue);
                    if( pickup_out.need_redraw )
                        app->need_redraw = 1;
                }
            }
            /* cc_dragpickup's hook can itself raise resize/trigger/transmit
             * work.  It belongs to the same click transaction. */
            stat = app_settle_cs2_frame(app);
            if( stat != TASK_RUNNER_IDLE )
            {
                app->runner_had_work = 1;
                return 0;
            }
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_LAYOUT)
            {
                UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            }
        }
    }

    /* Never publish a tree while a fenced server clientscript has not run.
     * This second gate covers a script request raised during interaction. */
    if( !App_FrameSettled(app) )
        return 0;

    if( app->need_redraw )
    {
        /* Mounts/bakes bump tree->generation; server texts that landed while
         * the target interface was unmounted re-apply onto the fresh nodes
         * (reference: IF_SETTEXT persists on IfType.list). */
        if( app->if_texts.count > 0 &&
            app->tree->generation != app->if_texts.applied_generation )
        {
            app->if_texts.applied_generation = app->tree->generation;
            for( int i = 0; i < app->if_texts.count; i++ )
            {
                bool ok = UITree_ApplyText(
                    app->tree, app->if_texts.entries[i].com_id, app->if_texts.entries[i].text);
                if( !ok && torirs_env_net_debug() )
                    TORIRS_LOG(
                        "if_settext: reapply com=%d gen=%u missed\n",
                        app->if_texts.entries[i].com_id,
                        app->tree->generation);
            }
        }
        if( app->if_hides.count > 0 &&
            app->tree->generation != app->if_hides.applied_generation )
        {
            app->if_hides.applied_generation = app->tree->generation;
            for( int i = 0; i < app->if_hides.count; i++ )
            {
                bool ok = UITree_ApplyHide(
                    app->tree, app->if_hides.entries[i].com_id, app->if_hides.entries[i].value);
                if( !ok && torirs_env_net_debug() )
                    TORIRS_LOG(
                        "if_sethide: reapply com=%d gen=%u missed\n",
                        app->if_hides.entries[i].com_id,
                        app->tree->generation);
            }
        }
        if( app->if_colours.count > 0 &&
            app->tree->generation != app->if_colours.applied_generation )
        {
            app->if_colours.applied_generation = app->tree->generation;
            for( int i = 0; i < app->if_colours.count; i++ )
            {
                bool ok = UITree_ApplyColour(
                    app->tree, app->if_colours.entries[i].com_id, app->if_colours.entries[i].value);
                if( !ok && torirs_env_net_debug() )
                    TORIRS_LOG(
                        "if_setcolour: reapply com=%d gen=%u missed\n",
                        app->if_colours.entries[i].com_id,
                        app->tree->generation);
            }
        }
        /* Rebind persistent models onto their (possibly newly mounted) nodes. */
        app_if_head_poll(app);
        app_if_player_model_poll(app);
        app_chat_build_view(app);
        /* TORIRS_FORCE_SHOW_SLOT=<component_id> (debug): clear the hide flag on one
         * mounted node each frame so a panel the gameframe keeps hidden can still be
         * rendered for inspection — the sidebar tab-reveal CS2 is still a follow-on,
         * so e.g. the magic tab (161|82 = 0x00a10052) is otherwise never visible.
         * Only this node's own flag is forced; the subtree's own hide state stands. */
        {
            char const* force_show = torirs_env_force_show_slot();
            if( force_show )
            {
                int want = (int)strtol(force_show, NULL, 0);
                int32_t idx = UITree_FindByComponentId(app->tree, want);
                if( idx >= 0 )
                    (void)UITree_SetHideAt(app->tree, idx, 0);
            }
        }
        /* Interaction hooks and the persistent-state rebinds above can run a
         * second CS2/topology transaction. This is the publication fence: the
         * standing semantic declaration must name the exact incarnations the
         * emit walk is about to commit, never the tree from frame start. */
        { uint64_t const pa_w0 = PerfAudit_Now(); App_PluginLayoutTick(app);
      PA_ADD(layout_tick_ns, PerfAudit_Now() - pa_w0); }
        app_minimenu_close_if_stale(app);
        /* Publication invariant: an emit list is a frame commit, not a view of
         * whatever intermediate state the cooperative schedulers reached. */
        assert(App_FrameSettled(app));
#if defined(TORIRS_UI_EMIT_PMU)
        ui_emit_pmu_begin();
        int overlay_motion_reused = 0;
#endif
        struct UITreeEmitRetainGate overlay_motion_gate;
        int overlay_motion_ready = 0;
        if( !app->plugins && !UIInk_IsActive(&app->ink) )
            (void)UITree_EmitOverlayMotionBegin(
                app->tree, &app->ui_host, &app->emit, app->hover_com_id, &app->emit_gate);
        app_entity_overlay_layout(app);
        overlay_motion_ready =
            UITree_EmitOverlayMotionEnd(app->tree, &app->emit_gate, &overlay_motion_gate);
        /* Resolve before evaluating retention. Frame reconciliation and the
         * projected entity-overlay pass can both invalidate boxes without
         * changing an already-pruned node's filtered dirty mark; resolving here
         * advances layout_resolve_seq and makes the gate observe that change. */
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_LAYOUT)
        {
            UITree_EnsureLayout(app->tree);
        }
        app_ui_host_publish_inputs(app);
        /* Opt 11 retention, SHADOW MODE: work out whether this walk could have
         * been skipped, but always run it anyway, and check the verdict against
         * the byte compare below. `emit_gen_unsound` counts frames the gate
         * called quiet whose list nevertheless changed — i.e. skips that would
         * have served a stale list. It is the safety proof for turning the skip
         * on, and it must be 0 over a full replay.
         *
         * The tree half is `dirty_gen`; the host half is the dependency stamp
         * captured by the previous full walk and published immediately above. */
        int gate_quiet = 0;

        /* Per-frame, and cleared here rather than after the render: everything
         * downstream reads it as "THIS frame retained", and a flag that only
         * ever latches true would make every later frame claim a retention it
         * did not get. */
        app->ui_retained_frame = 0;
        {
            /* Three terms, not two. `dirty_gen` covers writes that claim to
             * change what a node draws; it does NOT cover layout re-resolving,
             * which moves resolved boxes (and therefore emitted clips) off
             * `layout_stale` / `layout_force_full` / a changed root box without
             * raising anyone's `is_dirty`. That is the one hole the unsound
             * counter found: emit #5 of a 2,000-frame run, clip.w 765 -> 807 on
             * the group-548 root, gate quiet, list changed. */
            /* A pending invalidation precedes the resolver sequence bump.  It
             * is therefore part of the identity itself: comparing only the
             * last completed sequence can otherwise retain stale geometry in
             * the frame between UITree_LayoutInvalidate and EmitWalk's
             * UITree_EnsureLayout. */
            gate_quiet = UITree_EmitRetainGateQuiet(
                app->tree, &app->ui_host, &app->emit, app->hover_com_id, &app->emit_gate);
            /*
             * A running touch marker is never a quiet frame.
             *
             * The gate's two terms are the TREE's dirty generation and the
             * HOST's published dependency stamp, and the marker is in neither:
             * its whole state (position, colour, which of the eight frames) is
             * app-side, reached through UITREE_HOST_GET_INKWELL at walk time,
             * and it advances every frame without any node claiming to be
             * dirty. So the gate calls the frame quiet, the retained list --
             * the one already on screen -- is reused, and the marker is frozen
             * on whichever frame the last full walk happened to build.
             *
             * Measured on the phone: a tap produced two full walks, both
             * emitting frame 0, then eighteen more animation ticks with no walk
             * at all. On screen that is a blob that appears, does not animate,
             * and stays until something unrelated forces a rebuild -- which is
             * why it reads as "doesn't animate, often doesn't render".
             *
             * Stated here rather than by dirtying a node because the marker
             * does not belong to a node: it is host state that a component
             * renders, and the honest way to say that is that the frame is not
             * quiet while it is running. It costs a full walk only for the
             * ~400 ms a marker is alive, and only on a lane that declares one.
             */
            if( UIInk_IsActive(&app->ink) )
                gate_quiet = 0;

#if defined(TORIRS_UI_RETAIN_TRACE)
            /* Correctness/dependency census only, never a timing run. Count
             * failed retention inputs after startup; no per-node timer. */
            {
                static unsigned frames, quiet, dirty, layout, topology, hover, host, unrefreshable,
                    ink, domains[UITREE_HOST_INPUT_DOMAIN_COUNT];
                extern unsigned g_ui_retain_trace_mutations;
                if( frames == 600u )
                    g_ui_retain_trace_mutations = 48u;
                if( ++frames > 600u && frames <= 1800u )
                {
                    quiet += gate_quiet != 0;
                    dirty += app->tree->dirty_gen != app->emit_gate.dirty_gen;
                    layout += app->tree->layout_stale || app->tree->layout_force_full ||
                              app->tree->layout_resolve_seq != app->emit_gate.layout_resolve_seq;
                    topology += app->tree->generation != app->emit_gate.tree_generation;
                    hover += app->hover_com_id != app->emit_gate.hovered_component_id;
                    host += !UITree_EmitBufferHostInputsCurrent(&app->emit, &app->ui_host);
                    unrefreshable += app->emit.volatile_unrefreshable != 0;
                    ink += UIInk_IsActive(&app->ink) != 0;
                    for( int d = 0; d < UITREE_HOST_INPUT_DOMAIN_COUNT; d++ )
                        domains[d] +=
                            (app->emit.host_input_stamp.dependencies & UITREE_HOST_INPUT_BIT(d)) &&
                            app->emit.host_input_stamp.epoch[d] != app->ui_host.input_epoch[d];
                    if( frames == 1800u )
                    {
                        TORIRS_REPORT(
                            "ui-retain-trace: frames=1200 quiet=%u dirty=%u layout=%u topology=%u "
                            "hover=%u host=%u unrefreshable=%u ink=%u\n",
                            quiet,
                            dirty,
                            layout,
                            topology,
                            hover,
                            host,
                            unrefreshable,
                            ink);
                        for( int d = 0; d < UITREE_HOST_INPUT_DOMAIN_COUNT; d++ )
                            TORIRS_REPORT(
                                "ui-retain-trace: host_domain=%d changed=%u\n", d, domains[d]);
                    }
                }
            }
#endif

            /* Every dirty_gen source was measured bursty (creates, hide flips,
             * child link/unlink — all interface-open work), so on a steady-state
             * frame the tree term should hold and the gate should fire. It fires
             * twice in 2,000 frames. These two counters say which term is
             * actually failing instead of assuming it is the tree one. */
            if( app->emit_gate.primed && app->tree->dirty_gen == app->emit_gate.dirty_gen )
                TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GATE_TREE_QUIET, 1);
            if( app->emit_gate.primed && app->hover_com_id == app->emit_gate.hovered_component_id )
                TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GATE_HOVER_QUIET, 1);
            if( app->emit_gate.primed )
                TORIRS_PERF_COUNT(
                    TORIRS_PERF_CTR_EMIT_DIRTY_BUMPS,
                    (int)(app->tree->dirty_gen - app->emit_gate.dirty_gen));
        }
        /* Opt 11/12/14, emit half: nothing the walk reads has moved, so the list
         * it would produce is the one already in the buffer. Reuse it in place —
         * no copy, no compare, the buffer simply is not touched.
         *
         * Two guards, both necessary:
         *
         *  - Descs holding a host-owned pointer with same-frame lifetime are
         *    byte-identical frame to frame while the buffer behind them changes
         *    completely, so a plain skip would freeze a live minimap or health
         *    bar. Every quiet frame in this scene has at least one (the entity
         *    overlay), so a whole-list skip is worth exactly zero — measured, not
         *    assumed. `UITree_EmitRefreshVolatile` re-issues just those host
         *    requests in place, which is a few calls against a whole-tree DFS.
         *    Where a volatile desc cannot be re-issued from itself, the buffer
         *    says so and the full walk runs.
         *  - TORIRS_EMIT_VERIFY=1 forces the walk to run anyway, which keeps the
         *    `[emit-unsound]` detector below live over a full replay. That
         *    detector is the soundness proof for this skip; leaving it with no
         *    way to run once the skip exists would retire the proof along with
         *    the problem it proved.
         */
        {
            static int verify = -1;
            int retain;

            if( verify < 0 )
                verify = getenv("TORIRS_EMIT_VERIFY") ? 1 : 0;

            retain =
                gate_quiet && !verify && app->emit.count > 0 && !app->emit.volatile_unrefreshable;

            if( gate_quiet && !verify && app->emit.count > 0 && app->emit.volatile_unrefreshable )
                TORIRS_PERF_COUNT(TORIRS_PERF_CTR_EMIT_RETAIN_BLOCKED, 1);

            if( overlay_motion_ready && !verify &&
                UITree_EmitOverlayMotionRefresh(
                    app->tree,
                    &app->ui_host,
                    &app->emit,
                    &app->hover_com_id,
                    &overlay_motion_gate) )
            {
#if defined(TORIRS_UI_EMIT_PMU)
                overlay_motion_reused = 1;
#endif
                /* Moving overlays repaint; keep ui_retained_frame false so
                 * damage presentation never treats their pixels as stable. */
#if defined(TORIRS_OVERLAY_RETAIN_VERIFY)
                struct UITreeEmitBuffer reference;
                UITree_EmitBufferInit(&reference);
                UITree_EmitWalk(app->tree, &app->ui_host, &reference, app->hover_com_id);
                if( reference.count != app->emit.count ||
                    memcmp(
                        reference.cmds,
                        app->emit.cmds,
                        (size_t)reference.count * sizeof(*reference.cmds)) )
                {
                    TORIRS_REPORT(
                        "overlay-retain verification FAILED: reference=%d partial=%d\n",
                        reference.count,
                        app->emit.count);
                    abort();
                }
                UITree_EmitBufferFree(&reference);
                static unsigned matched;
                if( (++matched % 100u) == 0u )
                    TORIRS_REPORT(
                        "overlay-retain verification: %u full command lists matched\n", matched);
#endif
            }
            else if( retain )
            {
                int reusable = UITree_EmitRetainGateRefreshVolatile(
                    app->tree, &app->ui_host, &app->emit, &app->hover_com_id, &app->emit_gate);
                if( reusable )
                {
                    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_EMIT_RETAINED, 1);
                    /* The command list is the one already on screen, so the
                     * chrome pixels are unchanged and only the live regions
                     * need presenting. See App_PresentDamage. */
                    app->ui_retained_frame = 1;
                }
                else
                {
                    /* Defensive fallback for a volatile source that could not
                     * be refreshed safely. Rebuild once and retain normally
                     * again next frame. */
                    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_EMIT_RETAIN_BLOCKED, 1);
                    app->emit.count = 0;
                    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_EMIT)
                    {
                        UITree_EmitWalk(app->tree, &app->ui_host, &app->emit, app->hover_com_id);
                    }
                }
            }
            else
            {
                app->emit.count = 0;
                TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_EMIT)
                {
                    UITree_EmitWalk(app->tree, &app->ui_host, &app->emit, app->hover_com_id);
                }
            }
        }
        /* Publish the identity after either the retained volatile refresh or
         * the full walk. A host callback or EnsureLayout inside those paths may
         * advance an input/tree epoch; capturing before publication would make
         * the next frame conservatively rebuild despite a settled result. */
        UITree_EmitRetainGateCapture(app->tree, &app->emit, app->hover_com_id, &app->emit_gate);
#if defined(TORIRS_UI_EMIT_PMU)
        ui_emit_pmu_end(overlay_motion_reused, (unsigned)app->emit.count);
#endif
        /* DIAGNOSTIC (Opt 11 scoping, temporary): retaining the emit list only
         * pays if the list repeats, so measure the repeat rate before building
         * anything that could retain it. Every desc is memset before fill, so
         * the padding is deterministic and a byte compare is well defined.
         * The compare + copy is ~450 KB of traffic per frame — it inflates the
         * `emit` stage in any build carrying it. Read the counters, not the
         * clock. */
        if( g_torirs_perf_enabled )
        {
            static struct UITreeEmitDesc* prev = NULL;
            static int prev_count = -1;
            static int prev_cap = 0;
            static int emit_seq = 0;
            int n = app->emit.count;

            emit_seq++;

            if( prev_count == n &&
                (n == 0 || memcmp(prev, app->emit.cmds, (size_t)n * sizeof(*prev)) == 0) )
            {
                TORIRS_PERF_COUNT(TORIRS_PERF_CTR_EMIT_LIST_SAME, 1);
                if( gate_quiet )
                    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_EMIT_GEN_QUIET, 1);
            }
            else
            {
                /* The gate called this frame quiet and the list moved anyway:
                 * skipping here would have shown a stale panel. */
                if( gate_quiet )
                {
                    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_EMIT_GEN_UNSOUND, 1);
                    /* DIAGNOSTIC (temporary): an unsound frame is rare enough
                     * that naming the byte that moved is cheaper than reasoning
                     * about which of ~60 desc fields could have changed without
                     * touching dirty_gen or hover. The offset resolves to a
                     * field with pahole / offsetof. */
                    if( prev_count != n )
                    {
                        TORIRS_LOG("[emit-unsound] count %d -> %d\n", prev_count, n);
                    }
                    else
                    {
                        for( int u = 0; u < n; u++ )
                        {
                            unsigned char const* a = (unsigned char const*)&prev[u];
                            unsigned char const* b = (unsigned char const*)&app->emit.cmds[u];
                            if( memcmp(a, b, sizeof(*prev)) == 0 )
                                continue;
                            for( size_t k = 0; k < sizeof(*prev); k++ )
                            {
                                if( a[k] == b[k] )
                                    continue;
                                TORIRS_LOG(
                                    "[emit-unsound] emit#%d desc %d/%d kind %d "
                                    "com %d node %d first diff at byte %zu  "
                                    "clip.w %d -> %d\n",
                                    emit_seq,
                                    u,
                                    n,
                                    (int)app->emit.cmds[u].kind,
                                    app->emit.cmds[u].component_id,
                                    (int)app->emit.cmds[u].node_index,
                                    k,
                                    prev[u].clip.w,
                                    app->emit.cmds[u].clip.w);
                                break;
                            }
                            break;
                        }
                    }
                }

                int i;
                int diff = 0;

                for( i = 0; i < n; i++ )
                {
                    if( i >= prev_count ||
                        memcmp(&prev[i], &app->emit.cmds[i], sizeof(*prev)) != 0 )
                        diff++;
                }
                TORIRS_PERF_COUNT(TORIRS_PERF_CTR_EMIT_LIST_DIFF, 1);
                TORIRS_PERF_COUNT(TORIRS_PERF_CTR_EMIT_DESC_DIFF, diff);

                if( prev_cap < n )
                {
                    free(prev);
                    prev = malloc((size_t)n * sizeof(*prev));
                    assert(prev);
                    prev_cap = n;
                }
                if( n > 0 )
                    memcpy(prev, app->emit.cmds, (size_t)n * sizeof(*prev));
                prev_count = n;
            }
        }
        /* Keep painting while a server-driven rebuild is in flight so the
         * loading overlay refreshes (and picks up p12 once FontLoad lands). */
        if( app->world_load_server_driven && app->world_load_inflight )
            app->need_redraw = 1;
        else
            app->need_redraw = 0;
        return 1;
    }
    return 0;
}

