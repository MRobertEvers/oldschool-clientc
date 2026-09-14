#include "app.h"

#include "ui/uitree_canvas_measure.h"
#if defined(TORIRS_UI_EMIT_PMU)
#include "../tools/perf/ui_emit_pmu.u.h"
#endif
#include "bmp.h"
#include "game/rs_minimap_state.h"
#include "log/torirs_log.h"
#include "torirs_env.h"
#include "torirs_env_values.h"
/* Screenshot encoding. Already linked for the cache codecs; the PNG writer
 * rides along, so a plugin capture costs no new dependency. */
#include "bootmanifest/bootmanifest.h"
#include "engine/boot_bar.h"
#include "miniz.h"
#include "revconfig/revconfig_load.h"
#if !defined(TORIRS_PLATFORM_WEB)
/* The dat1 cache source that is a LostCity server rather than a directory.
 * Native only: a browser build has no host cache to replace, and its reads
 * already leave the process (platform_x_io_web.c). */
#include "platform/platform_x_io_ondemand.h"
#endif
#include "cmd/cmdbus.h"
#include "cs2vm2/cs2vm2.h"
#include "editor/editor.h"
#include "graphics/convex_hull.h"
#include "torirsmaped/torirs_maped.h"

/* Highlight colours. The model silhouette is the brighter of the two because it
 * is the "this is the thing" mark; the ground footprint under it is supporting
 * information and reads as such. */
#define APP_OUTLINE_COLOR_HOVER 0xFFFFFF00u
#define APP_OUTLINE_COLOR_FOOTPRINT 0xFFFF0000u
/* The map editor's SELECT latch -- green, so it never reads as the yellow
 * hover or the red footprint mark it can be drawn alongside. */
#define APP_OUTLINE_COLOR_EDITOR_SELECT 0xFF00FF00u
/* 0 opaque .. 255 invisible. High enough that the model reads through it. */
#define APP_OUTLINE_FILL_TRANS 205
#if defined(TORIRS_PLATFORM_ANDROID)
/* For the boot refusal below: on a phone there is no terminal to print to and
 * no shell to have typed the command, so a refusal has to reach the screen. */
#include "platform/platform_android.h"
#endif

#if defined(TORIRS_PLATFORM_WEB)
#include "ui/torirs_chrome_exec_web.h"

#include <emscripten.h>

/*
 * Ask the page to open the command-panel tab (`[editor:boot] panel=tab`).
 *
 * EM_JS rather than EM_ASM, matching main.c: EM_ASM is rejected in `-std=c*`
 * modes and this file is C11. The page owns the channel -- this only asks, and
 * a page that defines no hook (or a browser that blocks the popup) leaves the
 * editor running without its chrome rather than failing the boot.
 */
EM_JS(
    void,
    web_editor_open_panel_tab,
    (void),
    {
        if( typeof window.torirsOpenPanelTab == = 'function' )
            window.torirsOpenPanelTab();
        else
            console.warn('[torirs] panel=tab, but the page defines no torirsOpenPanelTab()');
    });
#endif

#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat1/dat1_tasks.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/entity_model_build.h"
#include "engine/player_appearance.h"
#include "engine/png_decode.h"
#include "engine/task_obj_model_load.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/async_pending.h"
#include "engine/toridraw_element_anim.h"
#include "engine/world_seq_source_toridraw.h"
#include "engine/torirs_chrome_skin_baked.h"
#include "engine/torirs_model_from_rscache.h"
#include "engine/torirs_model_inst_cache.h"
#include "engine/torirs_worldmap_from_rscache.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "engine/uitree_cmd_render.h"
#include "engine/uitree_role_load.h"
#include "engine/world_builder/task_world_load.h"
#include "engine/world_builder/world_builder.h"
#include "game/preview_state.h"
#include "game/rs_attack_option.h"
#include "game/rs_client_trigger.h"
#include "game/rs_clientcode.h"
#include "game/rs_cs2_dispatch.h"
#include "game/rs_ground_items_dirty.h"
#include "game/rs_game_events.h"
#include "game/rs_gameproto_exec.h"
#include "game/rs_minimenu_build.h"
#include "game/rs_minimenu_cross.h"
#include "game/rs_worldmap.h"
#include "game/rs_worldmap_drag.h"
#include "game/rs_worldmap_render.h"
#include "game/sailing_navigation.h"
#include "game/task_cs1_run.h"
#include "game/task_cs2_run.h"
#include "game/task_exec_entity_info.h"
#include "game/task_gameproto_exec.h"
#include "game/varc_ids.h"
#include "input/torirs_input_cmd.h"
#include "input/torirs_keymap.h"
#include "net/jbase37.h"
#include "net/net.h"
#include "net/net_out.h"
#include "net/rev/gameproto_parse.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "painters/painters.h"
#include "painters/painters_cull_project.h"
#include "painters/scene_occluders.h"
#include "perf/torirs_perf.h"
#include "platform/platform_memory.h"
#include "platform/platform_sdl2_renderer_soft3d.h"
#include "plugin/task_plugin_io.h"
#include "plugin/torirs_plugin_lua.h"
#include "plugin/torirs_plugin_mesh.h"
#include "plugin/torirs_plugin_registry.h"
#include "render/torirs_frame.h"
#include "render/torirs_pick.h"
#include "render/torirs_wedge_camera_path.h"
#include "render/torirs_viewport_projection.h"
#include "render/torirs_world_projection.h"
#include "toridraw.h"
#include "toridraw_model_transform.h"
#include "ui/torirs_chrome_panel_draw.h"
#include "ui/uitree_build.h"
#include "ui/uitree_frame.h"
#include "ui/uitree_input_signature.h"
#include "ui/uitree_if_events.h"
#include "ui/uitree_if_store.h"
#include "ui/uitree_keyboard_owner.h"
#include "ui/uitree_popup_place.h"
#include "ui/uitree_iface_stats.h"
#include "ui/uitree_layout.h"
#include "ui/uitree_obj_cell.h"
#include "world/world.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Developer hotkeys hold CTRL.
 *
 * Because the game's own keys are not the client's to intercept. Every key
 * event goes to every registered onKey hook and the cache's scripts decide
 * what to do with it -- that is the reference client's dispatch, and it means
 * a bare `p` is a letter somebody is typing into the chat line, not a request
 * to open the debug overlay. The client used to know better by tracking chat
 * focus itself, which took naming the chatbox's interface id.
 *
 * The modifier applies in every era rather than only where a cache chatbox
 * exists: one rule the muscle memory can hold, and a dat1 boot has a chat line
 * to type into as well.
 */
static int
app_debug_modifier_held(struct LibToriRS_Input* input)
{
    assert(input);
    return LibToriRS_Input_IsKeyHeld(input, TORIRSK_CTRL);
}

static struct AppDebugHotkeyBinding const*
app_debug_binding_down(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target)
{
    if( !app_debug_modifier_held(input) )
        return NULL;
    for( int i = 0; i < app->cfg.debug_hotkey_count; i++ )
        if( app->cfg.debug_hotkeys[i].target == target &&
            LibToriRS_Input_IsKeyDown(input, app->cfg.debug_hotkeys[i].key) )
            return &app->cfg.debug_hotkeys[i];
    return NULL;
}

static int
app_debug_key_down(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target)
{
    return app_debug_binding_down(app, input, target) != NULL;
}

static int
app_debug_key_held(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target)
{
    if( !app_debug_modifier_held(input) )
        return 0;
    for( int i = 0; i < app->cfg.debug_hotkey_count; i++ )
        if( app->cfg.debug_hotkeys[i].target == target &&
            LibToriRS_Input_IsKeyHeld(input, app->cfg.debug_hotkeys[i].key) )
            return 1;
    return 0;
}
#include <time.h>

/* A/B probe for the world painter: 0 = default (bucket, or world3d under
 * TORIRS_PAINTER_W3D=1), 1 = force world3d, 2 = force bucket. Set by the
 * TORIRS_PAINTER_ALT same-frame BMP pair in main.c. */
int g_torirs_painter_force = 0;
/* Ordinal of the frame being drawn, bumped once per main-loop iteration in
 * main.c. Anything whose phase must be a function of the frame rather than
 * of the clock reads it -- TORIRS_WEDGE_CAM_PATH does. Distinct from main's
 * frame_count, which only advances when TORIRS_MAX_FRAMES bounded the run;
 * a camera path has to keep moving in an unbounded session too. */
long g_torirs_frame_no = 0;
/* TORIRS_MAX_FRAMES, mirrored here from main.c so the logic pacer can see it.
 * Zero in an ordinary session. See the pacer for what it changes. */
long g_torirs_max_frames = 0;

enum
{
    APP_LOGIC_TICK_MS = 20,
    APP_MAX_CATCHUP_TICKS = 5,
    /*
     * The async pipeline's runaway tripwire. NOT a frame budget.
     *
     * The bound that used to live at the call site (512 booting, 32 otherwise,
     * from 8f3028ede under the note "a small budget keeps frame pacing")
     * throttled the pipeline to budget-times-framerate -- 1600 steps a second
     * once past boot -- which made the frame cap decide how fast the world
     * could load. This client streams its whole world through that pipeline.
     *
     * Set far above any frame that is making progress, so reaching it means a
     * task never returns IDLE. That aborts, because the alternative is a
     * client that looks merely slow for a reason nothing reports.
     */
    APP_ASYNC_STEP_LIMIT = 5000,
    /*
     * Connection-loss thresholds. See the `net_lost` block in app.h.
     *
     * APP_NET_TIMEOUT_MS is the reference's own: Client-TS gives up 15s after
     * the last packet (Client.ts:2443), and the server sends often enough that
     * a healthy link never comes close.
     *
     * APP_NET_STALL_MS answers a different question — not "has the server gone
     * quiet" but "was this client running". A frame gap that large means the
     * process was not scheduled (a hidden or frozen browser tab, a suspended
     * machine), so whatever is queued behind the socket is a backlog to
     * abandon, not a stream to replay.
     *
     * The gap it tests is now_ms - last_frame_ms, and last_frame_ms is stamped
     * at the TOP of App_RunOnce — so the quantity is the previous frame's whole
     * wall duration, work included, not the time the process spent descheduled.
     * That conflation is harmless where a frame is always short. It is not
     * harmless on the Windows XP lane: the earlier value, 4000, assumed "a slow
     * map load is hundreds of milliseconds, not seconds", and on that hardware
     * it is seconds. Measured worst legitimate frame, hydrating the sparse
     * cache over JS5 while rebuilding the world:
     *
     *   docs/winxp_profiles/baseline-winxp-soft3d-torirs-perf.csv   6.73s
     *   docs/winxp_profiles/new-run1.csv                           11.04s
     *
     * A client working flat out therefore concluded twelve times in one
     * thousand frames that it had not been running, and dropped a healthy
     * session — each drop costing a reconnect and a fresh login, which is what
     * made the profile unusable. 30000 clears the worst measured frame by
     * ~2.7x while staying far below any real suspend, which is minutes.
     */
    APP_NET_TIMEOUT_MS = 15000,
    APP_NET_STALL_MS = 30000,
    /* Outbound silence that has to pass before the NO_TIMEOUT keepalive goes
     * out. The reference's own figure (Client.ts:2181), and far below the
     * server's idle cutoff, so one late tick cannot cost the session. */
    APP_NET_KEEPALIVE_MS = 1000,
    /* Wait between re-establish attempts, and how many to make before giving
     * up and saying so. The reference retries once and falls back to the login
     * screen; a browser client that a phone backgrounded deserves more than
     * one try, but not an unbounded loop against a server that is gone. */
    APP_NET_RECONNECT_DELAY_MS = 2000,
    APP_NET_RECONNECT_MAX_ATTEMPTS = 5,
    /* Mouseover text origin inside the viewport. The reference container puts
     * its text child at (0,0); the classic client drew the same line at
     * (4, 15) — one padded cell in, with the baseline a line down. Ours is a
     * text box, so the baseline offset comes from the font ascent. */
    APP_HOVERTEXT_INSET_X = 4,
    APP_HOVERTEXT_INSET_Y = 2,
    /* Middle-button rotate. Yaw is 2048 units per turn, so 4 units per pixel
     * puts a full turn at 512 px of travel — roughly the viewport's width.
     * The orbit pitch band is only 255 units wide, so it moves at half that. */
    APP_WORLD_MMB_YAW_PER_PX = 4,
    APP_WORLD_MMB_PITCH_PER_PX = 2,
    /* Free camera (offline / scripted): no orbit distance to scale, so a notch
     * dollies along the view axis instead. The follow camera's notch is not
     * here — it is `[camera] wheel_step=` in the revconfig, beside the
     * `zoom_closest=`..`zoom_furthest=` band it moves in. */
    APP_WORLD_ZOOM_FREECAM_STEP = 140,
};

static struct RS_ChatFilters
app_chat_filters(struct App const* app)
{
    struct RS_ChatFilters filters = {
        .public_mode = app->slots.chat_filter_mode[RS_UI_CHAT_FILTER_PUBLIC],
        .private_mode = app->slots.chat_filter_mode[RS_UI_CHAT_FILTER_PRIVATE],
        .trade_mode = app->slots.chat_filter_mode[RS_UI_CHAT_FILTER_TRADE],
        .social = &app->social,
    };
    return filters;
}

/*
 * The follow camera's pitch, held inside the range the profile states.
 *
 * One place, because this used to be four: the boot value, the middle-button
 * drag, TORIRS_ORBIT_CAM and the arrow-key ease each spelled `128` and `383`
 * themselves, and the terrain clamp spelled them a fifth time in 256ths. A
 * profile that moved the range would have moved one of the five.
 * @see RevConfigCameraItem::pitch_flattest.
 */
static int
app_world_clamp_pitch(
    struct App const* app,
    int pitch)
{
    assert(app);
    if( pitch < app->revconfig_profile.camera.pitch_flattest )
        return app->revconfig_profile.camera.pitch_flattest;
    if( pitch > app->revconfig_profile.camera.pitch_steepest )
        return app->revconfig_profile.camera.pitch_steepest;
    return pitch;
}

/* Resolve a component id at the point an app-owned action is about to use it.
 * Interaction and minimenu models deliberately retain ids across frames, while
 * CC_DELETEALL may reclaim their old node and plugin layouts may suppress an
 * ancestor after the model was built.  A missing id is not visible -- the
 * public ComponentOrAncestorDisplayHidden helper cannot make that distinction
 * because it starts from an already-resolved node. */
static int32_t
app_displayable_component_node(
    struct App const* app,
    int component_id)
{
    int32_t idx;

    if( !app || !app->tree || component_id < 0 )
        return -1;
    idx = UITree_FindByComponentId(app->tree, component_id);
    if( idx < 0 || (uint32_t)idx >= app->tree->component_count ||
        app->tree->components[idx].freed || UITree_NodeOrAncestorDisplayHidden(app->tree, idx) )
        return -1;
    return idx;
}

static int
app_intent_targets_live(
    struct App const* app,
    struct UIIntent const* intent)
{
    if( intent->has_node_identity )
    {
        struct UITreeComponent const* node;
        if( !app || !app->tree || intent->node_index < 0 ||
            (uint32_t)intent->node_index >= app->tree->component_count )
            return 0;
        node = &app->tree->components[intent->node_index];
        if( node->freed || intent->node_incarnation == 0 ||
            node->incarnation != intent->node_incarnation ||
            (intent->component_id >= 0 && node->component_id != intent->component_id) ||
            UITree_NodeOrAncestorDisplayHidden(app->tree, intent->node_index) )
            return 0;
    }
    else if( app_displayable_component_node(app, intent->component_id) < 0 )
        return 0;
    /* -1 is the ordinary "no drop target" carried by onDrag. */
    if( intent->has_drag_target && intent->drag_target_id >= 0 && intent->has_drag_target_identity )
    {
        struct UITreeComponent const* target;
        if( !app || !app->tree || intent->drag_target_node_index < 0 ||
            (uint32_t)intent->drag_target_node_index >= app->tree->component_count )
            return 0;
        target = &app->tree->components[intent->drag_target_node_index];
        if( target->freed || intent->drag_target_node_incarnation == 0 ||
            target->incarnation != intent->drag_target_node_incarnation ||
            target->component_id != intent->drag_target_id ||
            UITree_NodeOrAncestorDisplayHidden(app->tree, intent->drag_target_node_index) )
            return 0;
    }
    else if(
        intent->has_drag_target && intent->drag_target_id >= 0 &&
        app_displayable_component_node(app, intent->drag_target_id) < 0 )
        return 0;
    return 1;
}

/* chat_index is a long-lived slot-table index rather than an identity.  Check
 * that it still names the authored chat slot before using it, and apply the
 * same effective visibility predicate as paint/hit traversal. */
static int32_t
app_chat_node_index(struct App const* app)
{
    struct UITreeComponent const* node;
    int32_t idx;

    if( !app || !app->tree )
        return -1;
    idx = app->slots.chat_index;
    if( idx < 0 || (uint32_t)idx >= app->tree->component_count )
        return -1;
    node = &app->tree->components[idx];
    if( node->freed || node->slot_tag != UITREE_SLOT_CHAT ||
        UITree_NodeOrAncestorDisplayHidden(app->tree, idx) )
        return -1;
    return idx;
}

/* Chat node geometry + font, resolved through the slot index so nothing here
 * names coordinates or interface ids. Returns 0 when no chat region exists. */
static int
app_chat_region(
    struct App const* app,
    int* out_x,
    int* out_y,
    int* out_font_id)
{
    struct UITreeComponent const* node;
    int32_t idx;
    int x = 0, y = 0, w = 0, h = 0;

    idx = app_chat_node_index(app);
    if( idx < 0 )
        return 0;
    node = &app->tree->components[idx];
    UITree_LayoutGetBounds(&node->position, &x, &y, &w, &h);
    if( out_x )
        *out_x = x;
    if( out_y )
        *out_y = y;
    if( out_font_id )
        *out_font_id = UITree_Chat(node)->font_id > 0 ? UITree_Chat(node)->font_id : 1;
    return 1;
}

/* The unfocused input line's wording, from the chat component's `prompt=` key
 * (a `@mobile` override is how a touch lane says "Tap here to chat...").
 * NULL when no chat region exists or the profile stated nothing, which
 * RS_Chat_BuildView reads as "use the reference wording". */
static char const*
app_chat_prompt(struct App const* app)
{
    int32_t idx;

    idx = app_chat_node_index(app);
    if( idx < 0 )
        return NULL;
    return UITree_Chat(&app->tree->components[idx])->prompt;
}

/* True when a canvas-space point lands inside the chat region's bounds. Used to
 * decide chat input focus on a left click.
 *
 * A revconfig (dat1) idea only: that tree tags its chat region with a slot, and
 * paints the messages into it itself. A cache tree has no chat *region* at all
 * -- its chatbox is an interface whose own onKey hook is the typed line, and
 * which the client does not route keys for. */
static int
app_point_in_chat(
    struct App const* app,
    int x,
    int y)
{
    struct UITreeComponent const* node;
    int32_t idx;
    int bx = 0, by = 0, bw = 0, bh = 0;

    idx = app_chat_node_index(app);
    if( idx < 0 )
        return 0;
    node = &app->tree->components[idx];
    UITree_LayoutGetBounds(&node->position, &bx, &by, &bw, &bh);
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

/*
 * An interface's own text box has the keyboard.
 *
 * A panel with a search field (settings 134, collection log 621, league tasks)
 * takes it by calling `~chatdefault_stopinput`, which sets this varc and
 * disarms the chatbox's onKey; `~chatdefault_restoreinput` puts both back. It
 * is the cache's own focus flag, and reading it is how the client learns that
 * something it does not own is being typed into. Unset reads -1, which is not
 * "a box has it" — hence `== 1`. */
static int
app_iface_text_input_focused(struct App const* app)
{
    struct VarCIds const* ids =
        varc_ids_for_revision(app->net && app->net->rev ? (int)app->net->rev->revision : 0);

    assert(app);
    if( ids->interface_input_active < 0 )
        return 0;
    return VarCManager_GetInt(&app->varcs, ids->interface_input_active) == 1;
}

/*
 * Is the keyboard spoken for by something the player is typing into?
 *
 * The one question every hotkey has to ask, and the reason it is a function
 * rather than the three-flag conjunction it used to be spelled as at four call
 * sites: each of those spellings was a list of the text inputs that existed
 * when it was written, so a text input added later silently kept the hotkeys
 * live underneath it. `f` switching a tab behind the caret, a spawn digit
 * spawning while typing a bank amount, W flying the camera through a search
 * term — all one bug, four times.
 */
static int
app_modelview_focused(struct App const* app);

static int
app_chrome_holds_keyboard(struct App const* app);

static int
app_text_input_focused(struct App const* app)
{
    struct UIKeyboardOwners owners;
    struct VarCIds const* ids;

    assert(app);
    ids = varc_ids_for_revision(app->net && app->net->rev ? (int)app->net->rev->revision : 0);

    owners.chat_line_present = app_chat_node_index(app) >= 0;
    owners.chat_input_active = app->chat_input_active != 0;
    owners.chat_social_input_open = app->chat.social_input_open != 0;
    owners.chat_dialog_input_open = app->chat.dialog_input_open != 0;
    owners.interface_input_active_varc =
        ids->interface_input_active >= 0
            ? VarCManager_GetInt((struct VarCManager*)&app->varcs, ids->interface_input_active)
            : -1;
    owners.tree_input_focus_id = app->tree ? UITree_InputFocusId(app->tree) : -1;
    owners.chrome_field_focused = app_chrome_holds_keyboard(app) != 0;

    return UIKeyboard_TextInputFocused(&owners) ? 1 : 0;
}

/*
 * Is a ToriRSChrome field being typed into?
 *
 * BOTH instances, which is the whole point of it being a function. dbg_ui is
 * the developer chrome (the map editor's Height field and its neighbours);
 * plugin_ui is the plugin window, and it was missing -- so a keystroke aimed
 * at a plugin's colour or note field also fired whatever debug hotkey shares
 * that letter, and was typed into the chat line underneath. Both are the same
 * bug app_text_input_focused exists to kill, and naming only one instance is
 * how it came back.
 */
static int
app_chrome_holds_keyboard(struct App const* app)
{
    assert(app);
    return app->dbg_ui.focus >= 0 || app->plugin_ui.focus >= 0;
}

/*
 * Chat input focus, for the frame. A dat1/revconfig idea only.
 *
 * Runs ahead of the keyboard broadcast so the frame's keys are routed by a
 * focus state this frame's clicks and Enter have already been folded into,
 * rather than by last frame's.
 *
 * Focus is taken by clicking the chat region or by pressing Enter, and dropped
 * by clicking anywhere else, by Escape, and by sending the line. Which is the
 * whole point of the state: while it is off every key belongs to the hotkeys,
 * and while it is on none of them do.
 *
 * A CACHE revision has none of this, and that is the reference client's own
 * answer rather than a gap: it has no press-enter-to-chat, no focused/unfocused
 * chat line and no client-side routing of keys to the chatbox. Every key goes
 * to every registered onKey hook and the cache's scripts decide -- script 73
 * takes the typed line, script 905 takes the F-key tab switches, and a panel
 * search box takes the keyboard by disarming the chatbox's hook. The gate is
 * `slots.chat_index`, the revconfig-declared chat region, because a client that
 * paints its own chatbox is exactly the client that has to own focus for it.
 *
 * @param pointer_consumed an open minimenu already claimed this frame's press.
 * @param out_submit set when Enter arrived with the line focused — the line
 *        sends this frame, and the focus goes with it once the sending script
 *        has run (which is after this returns).
 * @return nonzero when the frame's keys must not reach the chat line at all:
 *         the Escape that dropped focus and the Enter that took it are focus
 *         commands, not text. It suppresses anything else typed in the same
 *         20ms frame, which is not a rate a player types at.
 */
static int
app_chat_focus_tick(
    struct App* app,
    struct LibToriRS_Input* input,
    int pointer_consumed,
    int* out_submit)
{
    int suppress = 0;
    int const was_focused = app->chat_input_active;

    assert(app);
    assert(input);
    assert(out_submit);
    *out_submit = 0;

    if( app_chat_node_index(app) < 0 )
    {
        if( app->chat_input_active )
        {
            app->chat_input_active = 0;
            app->need_redraw = 1;
        }
        return 0;
    }
    /* The loc editor took W/A/S/D/R/Space/Backspace for the frame and has
     * already forced the focus flags off; do not hand them back under it. */
    if( app->locedit_visible )
        return 0;
    /* An IF3 text-entry field has the caret. Enter belongs to that field (it
     * submits), and without this the chat line would claim the same press and
     * open itself behind the box being typed into. @see UITree_InputFocusId. */
    if( app->tree && UITree_InputFocusId(app->tree) >= 0 )
    {
        app->chat_input_active = 0;
        return 0;
    }
    /* A panel's own search box has the keyboard (the cache disarmed the
     * chatbox's onKey to give it to them), so the chat line cannot also have
     * it. Dropping focus here rather than merely ignoring it keeps
     * app_text_input_focused's two halves from both claiming to be the focused
     * one. */
    if( app_iface_text_input_focused(app) )
    {
        app->chat_input_active = 0;
        return 0;
    }

    if( !pointer_consumed && LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) )
        app->chat_input_active = app_point_in_chat(app, input->curr.mouse_x, input->curr.mouse_y);

    for( int e = 0; e < input->key_event_count; e++ )
    {
        int const typed = input->key_events[e].key_typed;

        if( typed == TORIRS_OSRSKEY_ESCAPE )
        {
            app->chat_input_active = 0;
            suppress = 1;
            continue;
        }
        if( typed != TORIRS_OSRSKEY_ENTER )
            continue;
        /* Enter with the line unfocused takes focus instead of submitting, so
         * the keyboard alone can start a message; with it focused it submits,
         * and the focus is released with the message the way the reference's
         * press-enter-to-chat does. */
        if( app->chat_input_active || app->chat.social_input_open || app->chat.dialog_input_open )
            *out_submit = 1;
        else
        {
            app->chat_input_active = 1;
            suppress = 1;
        }
    }

    if( app->chat_input_active != was_focused )
        app->need_redraw = 1;
    return suppress;
}

/*
 * Rebuild the chat presentation (called before every emit).
 *
 * One shape, and it belongs to the eras whose chatbox is a *surface* the client
 * paints: a revconfig tree reserves a region, tags it with a slot, and this
 * flattens the message list into it for `emit_chat`.
 *
 * A cache revision has no chat region and nothing here to do. Its chatbox is
 * interface 162 -- 500 text components inside a scrolling layer -- and the
 * cache's own `[proc,rebuildchatbox]` writes every one of them, off the
 * chat-transmit hook, reading the same message store through the
 * CHAT_GETHISTORY* opcodes. The client used to write those components itself
 * from here, which meant it had to be told which components they were.
 */
static void
app_chat_build_view(struct App* app)
{
    struct RS_ChatFilters filters = app_chat_filters(app);
    int font_id = 1;

    if( !app_chat_region(app, NULL, NULL, &font_id) )
    {
        memset(&app->chat_view, 0, sizeof(app->chat_view));
        return;
    }
    RS_Chat_BuildView(
        &app->chat,
        &filters,
        &app->ui_host,
        font_id,
        /* Not chat_com_id: the tutorial-progress component shares this region
         * and suppresses the log the same way a dialogue does. */
        RS_UISlots_ChatRegionIface(&app->slots) != -1,
        app->chat_input_active || app->chat.social_input_open || app->chat.dialog_input_open,
        app_chat_prompt(app),
        &app->chat_view);
}

/* RS_MinimenuChatSource seam: sender under a canvas-space click. */
static int
app_chat_line_at(
    void* user,
    int x,
    int y,
    char* out_sender,
    int sender_cap,
    int* out_chat_type)
{
    struct App* app = (struct App*)user;
    struct RS_ChatFilters filters = app_chat_filters(app);
    int rx = 0;
    int ry = 0;

    if( !app_chat_region(app, &rx, &ry, NULL) )
        return 0;
    return RS_Chat_LineAt(
        &app->chat, &filters, x - rx, y - ry, out_sender, sender_cap, out_chat_type);
}

/* Adapter so rs_minimenu_build can ask about server-declared events without
 * knowing what an App is. Uses the node-aware lookup so a dynamic child
 * inherits its parent's IF_SETEVENTS range (popout:buttons, bank items, …). */
static int
app_minimenu_events_for_component(
    void* user,
    int com_id,
    int sub_id)
{
    if( sub_id >= 0 )
        return App_IfEventsGetAt((struct App const*)user, com_id, sub_id);
    return (int)App_IfEventsGetEffective((struct App const*)user, com_id);
}

/* The CS2 host's twin of the above, for IF/CC_GETTARGETMASK. It reports
 * *presence* rather than an effective value because that is what the reference
 * split needs (deob method12093): where the server declared nothing the widget's
 * own decoded target mask answers, and that one is already normalised per cache
 * generation on the node — shifting a dat1 mask like a dat2 events word would
 * turn a real answer into noise. */
static int
app_cs2_events_override_for_component(
    void* user,
    int com_id,
    int* out_events)
{
    unsigned events = 0;
    if( !UIIfEventTable_Lookup(&((struct App const*)user)->if_events, com_id, -1, &events) )
        return 0;
    if( out_events )
        *out_events = (int)events;
    return 1;
}

void
App_IfEventsSet(
    struct App* app,
    int com_id,
    int from,
    int to,
    int events)
{
    assert(app);
    UIIfEventTable_Set(&app->if_events, com_id, from, to, events);

    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "if_setevents: com=%d (%d:%d) slots=%d..%d events=0x%x\n",
            com_id,
            (com_id >> 16) & 0xffff,
            com_id & 0xffff,
            from,
            to,
            events);
    app->need_redraw = 1;
}

void
App_IfEventsClear(struct App* app)
{
    assert(app);
    UIIfEventTable_Clear(&app->if_events);
}

int
App_IfEventsGet(
    struct App const* app,
    int com_id)
{
    return App_IfEventsGetAt(app, com_id, -1);
}

int
App_IfEventsGetAt(
    struct App const* app,
    int com_id,
    int sub_id)
{
    assert(app);
    return UIIfEventTable_At(&app->if_events, com_id, sub_id);
}

unsigned
App_IfEventsGetEffective(
    struct App const* app,
    int com_id)
{
    assert(app);
    return UIIfEventTable_Effective(&app->if_events, app->tree, com_id);
}

/*
 * Which target kinds a component actually offers — deob `method12079`'s
 * `method7577(method12093(this, widget))`, the same pair `rs_cs2_target_mask`
 * keeps for IF/CC_GETTARGETMASK and `component_effective_target_mask`
 * (rs_minimenu_build.c) uses to decide whether to BUILD the target row.
 *
 * The arm has to agree with the builder or the row and the arm disagree about
 * what the click meant: reading only the widget's decoded `target_mask` armed
 * a script-built button with mask 0, and `add_world_select_row` then refused
 * every world row while target mode was active — a menu with the verb on it
 * and nothing the verb could be used on. A `cc_create`d child is memset to
 * zero and no `cc_` opcode can write a target mask, so its whole declaration
 * is the server's `if_setevents` (sailing's crew panel: `^if_event_op_all +
 * 16384`, bit 14 -> mask 0x8 PLAYER).
 *
 * The shift is IF3-only, exactly as in the two functions above: a dat1
 * component stores `targetMask` unshifted in `click_mask`, so shifting an IF1
 * events word by 11 would turn a real answer into noise.
 */
static int
app_component_target_mask(
    struct App const* app,
    int com_id)
{
    int32_t idx;
    struct UITreeComponent const* node;
    int mask;

    assert(app);
    if( !app->tree )
        return 0;
    idx = UITree_FindByComponentId(app->tree, com_id);
    if( idx < 0 )
        return 0;
    node = &app->tree->components[idx];
    mask = (int)node->behavior.target_mask;
    if( node->if3 )
        mask |= (int)((App_IfEventsGetEffective(app, com_id) >> TORIRS_TARGET_MASK_IF3_SHIFT) &
                      TORIRS_TARGET_MASK_IF3_BITS);
    return mask;
}

/*
 * The identity the SERVER knows the armed component by — the one that goes on
 * the wire in OPPLAYERT/OPNPCT/OPLOCT/OPOBJT/OPHELDT.
 *
 * `targetsel.component_id` is the TREE id, because that is what the local
 * target-enter/leave hooks are dispatched against. For a dynamic child that id
 * is a runtime allocation the server has never heard of, so putting it on the
 * wire meant `[opplayert,sailing_sidepanel:crew_content_clicklayer]` could
 * never match and the armed click did nothing at all. `app_if_button_target`
 * is the same (container, index-within-it) resolution IF_BUTTON already does,
 * and it is a no-op for a static spellbook button.
 *
 * Resolved here rather than stored at arm time because the armed component's
 * parent is what the packet needs and `targetsel` has no wire-id field; the
 * tree cannot have moved the child to a different container between the arm
 * and the click without destroying it, and a destroyed child resolves to
 * itself, which is what an unarmed cast already sends.
 */
static int
app_targetsel_wire_component(struct App const* app)
{
    int com;
    int sub;

    assert(app);
    UIIfEventTable_ButtonTarget(app->tree, app->targetsel.component_id, &com, &sub);
    return com;
}

static void
app_send_if_button(
    void* user,
    int com_id);
static void
app_send_resume_pausebutton(
    void* user,
    int com_id);
static void
app_send_close_modal(void* user);
static void
app_world_bind_pending_seqs(struct App* app);
static void
app_world_sync_entity_animations(struct App* app);
static void
app_world_anim_frame_sound(
    void* userdata,
    int seq_id,
    int frame,
    int world_x,
    int world_z);
static void
app_world_sync_entity_spotanims(struct App* app);
static void
app_entity_spotanim_drop(
    struct App* app,
    int body_element_id);
static struct AppEntitySpotanim*
app_entity_spotanim_find(
    struct App* app,
    int body_element_id,
    int owner_entity_id);
static void
app_entity_spotanim_detach(
    struct App* app,
    struct AppEntitySpotanim* entry,
    bool restore);

/*
 * Send an outbound packet built by a net_out_* builder, gated on networking.
 * The builder writes into a scratch buffer using the game out-cipher; the
 * bytes then queue to the socket via the subsystem's SEND_DATA ring.
 *
 * Building and queueing remain one operation so the outbound ISAAC stream
 * cannot advance without the corresponding packet being sent.
 */
#define APP_NET_SEND(app, builder_call)                                                            \
    do                                                                                             \
    {                                                                                              \
        if( (app)->net && (app)->net->state == TORIRS_NET_GAME )                                   \
        {                                                                                          \
            uint8_t _nsbuf[512];                                                                   \
            int _nslen = builder_call;                                                             \
            if( _nslen > 0 )                                                                       \
            {                                                                                      \
                ToriRS_Network_SendRaw((app)->net, _nsbuf, _nslen);                                \
                (app)->net_last_send_ms = (app)->last_frame_ms;                                    \
            }                                                                                      \
        }                                                                                          \
    } while( 0 )

/* Server-synced local player entity (esync pid), or NULL (offline / not yet
 * spawned). Shared by camera follow, minimap centering, and roof check. */
static struct WorldEntity_Player*
app_local_player(struct App* app)
{
    int world_idx;
    if( !app->world )
        return NULL;
    if( !RS_EntitySync_FindPlayer(
            &app->esync,
            app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
            &world_idx,
            NULL) )
        return NULL;
    return World_EntityPoolGet(&app->world->entities.player, world_idx);
}

struct WevDeckBox;
static void
app_wev_deck_box(
    struct App* app,
    struct Wev const* wev,
    struct World const* parent_world,
    struct WevDeckBox* out_box);
/**
 * An aboard actor's position pushed out through its hull into ROOT scene-local
 * fine units — the transform the deob applies before anything main-world reads
 * an aboard actor's position (camera focus, minimap centre, minimap dots:
 * Statics.method8690). Returns 0 (out untouched) when the actor is not in a
 * live, root-parented view — the caller keeps its root-space position.
 */
static int
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

static int
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

static void
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

static int
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

/* World_LocalPlaneFn: the reference's minusedlevel — the plane of the MAP the
 * client holds, which aboard is the hull's parent level, not the rider's deck
 * plane. One authority: app_cinema_level (declared with the overlay helpers
 * further down). */
static int
app_cinema_level(struct App* app);
static int
app_world_local_plane(void* userdata)
{
    struct App* app = (struct App*)userdata;

    assert(app);
    return app_cinema_level(app);
}

/*
 * World_ActorRootFrameFn: the facing math's cross-frame answer — an aboard
 * actor's position pushed out through the hull (app_wev_actor_root_fine) plus
 * the frame's yaw offset, the hull's live angle: a homed actor's element yaw
 * is deck-frame and the descent adds the hull's yaw at draw, so a direction
 * computed in the root must have it taken back out.
 */
static int
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
    int dx = rel_fx / 32;
    int dy = rel_fz / 32;
    int yaw, x, y;
    int w = 4, h = 4;
    struct UITreeMinimapDot* dot;

    if( app->minimap_dot_count >= (int)(sizeof(app->minimap_dots) / sizeof(app->minimap_dots[0])) )
        return;
    if( dx * dx + dy * dy > 6400 )
        return;
    yaw = ToriDraw_NormalizeAngle(app->world_camera.yaw);
    {
        int sin = ToriDraw_Sin(yaw);
        int cos = ToriDraw_Cos(yaw);
        x = (dy * sin + dx * cos) >> 16;
        y = (dy * cos - dx * sin) >> 16;
    }
    {
        int count = 0;
        struct ToriDraw_Sprite** frames = ToriDraw_SceneSpriteGet(app->scene, scene_id, &count);
        if( frames && atlas_index >= 0 && atlas_index < count && frames[atlas_index] )
        {
            w = frames[atlas_index]->width;
            h = frames[atlas_index]->height;
        }
    }
    dot = &app->minimap_dots[app->minimap_dot_count++];
    dot->dx = x - w / 2;
    dot->dy = -y - h / 2;
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

/*
 * World map surface: which baked regions cover the widget this frame, and where
 * each one lands on screen.
 *
 * Ported from xrsps-typescript widgets-gl.ts (contentType 1400). The view is a
 * centre point in map-surface tiles (display_x/display_y) at a fixed number of
 * pixels per tile, so a region's top-left corner projects to
 * centre + (region_tile - display) * scale, with y flipped because map tiles
 * count north-up. Regions are baked at exactly that scale, so every blit is 1:1.
 */
static void
app_worldmap_build_icons(
    struct App* app,
    struct ToriRS_WorldMapArea const* area,
    int centre_x,
    int centre_y,
    int display_x,
    int display_y,
    int region_px);

/*
 * One map element icon at a screen position, loading its config and sprite on
 * demand. Both loads are lazy, so an icon appears a frame or two after the
 * region under it — the same order the reference fills a cold cache in.
 *
 * Returns false when it could not be drawn (yet), which the callers ignore: the
 * next frame asks again.
 */
/*
 * The flash marker drawn behind a flashing icon: a translucent yellow disc with
 * an opaque white core, 30x30, built once and parked at a reserved scene id.
 *
 * It is synthesised rather than resolved by name because the cache has no flash
 * marker to resolve. The `worldmap_marker_0..8` / `worldmap_marker_mini_0..2`
 * packs are the *player-placed* map markers (marker_0 measures 37x37 and is the
 * yellow X), not this. Nothing else in the sprite index names a flash asset, so
 * there is no id to look up — and inventing one would be worse than drawing the
 * shape. This mirrors the reference client wrapper, which composites the same
 * disc itself for the same reason (widgets-gl.ts getWorldMapFlashTexture).
 */
static int
app_worldmap_flash_marker_scene(struct App* app)
{
    enum
    {
        MARKER_SIZE = 30
    };
    uint32_t* argb;
    struct ToriDraw_Sprite* sprite;
    struct ToriDraw_Sprite** sprites;
    int const radius = MARKER_SIZE / 2;
    int const core = 7;

    if( app->worldmap_flash_scene_id != 0 )
        return app->worldmap_flash_scene_id;

    app->worldmap_flash_scene_id = -1;
    argb = calloc((size_t)MARKER_SIZE * MARKER_SIZE, sizeof(*argb));
    assert(argb);

    for( int y = 0; y < MARKER_SIZE; y++ )
    {
        int dy = y - radius;
        for( int x = 0; x < MARKER_SIZE; x++ )
        {
            int dx = x - radius;
            int d2 = dx * dx + dy * dy;
            if( d2 <= core * core )
                argb[y * MARKER_SIZE + x] = 0xFFFFFFFFu; /* opaque white core */
            else if( d2 <= radius * radius )
                argb[y * MARKER_SIZE + x] = 0x80FFFF00u; /* half-alpha yellow halo */
        }
    }

    sprite = ToriDraw_SpriteNewFromArgbOwned(argb, MARKER_SIZE, MARKER_SIZE);
    if( !sprite )
    {
        free(argb);
        return -1;
    }
    sprites = malloc(sizeof(*sprites));
    assert(sprites);
    sprites[0] = sprite;
    ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_WORLD_MAP_FLASH_SPRITE_ID, sprites, 1);
    app->worldmap_flash_scene_id = UITREE_SCENE_WORLD_MAP_FLASH_SPRITE_ID;
    return app->worldmap_flash_scene_id;
}

/* Loc mapfunction / worldmap icon: mapelement id → sprite scene id (dat2).
 * Queues MapElementLoad / SpriteLoad when cold; returns <= 0 until ready.
 * Label-only elements (sprite_id < 0) return 0. If out_element is non-NULL and
 * the config is loaded, it is filled (even when the sprite is not ready). */
static int
app_mapfunction_scene_id(
    struct App* app,
    int element_id,
    struct ToriRS_MapElement** out_element)
{
    struct ToriRS_MapElement* element;
    struct ToriRS_Sprite* sprite;
    int scene_id;

    assert(app);
    assert(app->provider);
    if( out_element )
        *out_element = NULL;
    if( element_id < 0 )
        return 0;

    element = CacheProvider_MapElementGet(app->provider, element_id);
    if( !element )
    {
        struct ToriRS_Task* task = CreateTask_MapElementLoad(app->provider, element_id);
        if( task )
            ToriRS_TaskQueue_Add(app->runner.queue, task);
        return 0;
    }
    if( out_element )
        *out_element = element;
    if( element->sprite_id < 0 )
        return 0;

    sprite = CacheProvider_SpriteGet(app->provider, element->sprite_id);
    if( !sprite || sprite->frame_count <= 0 )
    {
        struct ToriRS_Task* task = CreateTask_SpriteLoad(app->provider, element->sprite_id);
        if( task )
            ToriRS_TaskQueue_Add(app->runner.queue, task);
        return 0;
    }

    scene_id = UITreeSceneBridge_EnsureSprite(&app->bridge, element->sprite_id);
    return scene_id > 0 ? scene_id : 0;
}

static bool
app_worldmap_push_icon(
    struct App* app,
    int element_id,
    int screen_x,
    int screen_y)
{
    struct ToriRS_MapElement* element = NULL;
    struct ToriRS_Sprite* sprite;
    struct UITreeWorldMapTile* tile;
    int scene_id;
    int capacity = (int)(sizeof(app->worldmap_tiles) / sizeof(app->worldmap_tiles[0]));

    if( app->worldmap_tile_count >= capacity || element_id < 0 )
        return false;

    /* Off-surface icons are not worth a config load. */
    if( screen_x < app->worldmap_drag.box_x - 32 ||
        screen_x > app->worldmap_drag.box_x + app->worldmap_drag.box_w + 32 ||
        screen_y < app->worldmap_drag.box_y - 32 ||
        screen_y > app->worldmap_drag.box_y + app->worldmap_drag.box_h + 32 )
        return false;

    /* Warm the mapelement first so category visibility can gate the sprite
     * load — same order as before the shared helper. */
    element = CacheProvider_MapElementGet(app->provider, element_id);
    if( !element )
    {
        struct ToriRS_Task* task = CreateTask_MapElementLoad(app->provider, element_id);
        if( task )
            ToriRS_TaskQueue_Add(app->runner.queue, task);
        return false;
    }
    /* The one seam both icon sources funnel through, so it is where the map's
     * element-enable state gets its only consumer: WORLDMAP_DISABLEELEMENT(S)
     * / _ELEMENTCATEGORY are write-only until something declines to draw. The
     * key panel's five display toggles are exactly these calls. */
    if( !RS_WorldMap_IconVisible(app->host.worldmap, element_id, element->category) )
        return false;

    scene_id = app_mapfunction_scene_id(app, element_id, &element);
    if( scene_id <= 0 )
        return false; /* cold sprite, or label-only (sprite_id < 0) */

    assert(element);
    sprite = CacheProvider_SpriteGet(app->provider, element->sprite_id);
    assert(sprite && sprite->frame_count > 0);

    /* Flash marker first, so it lands *behind* the icon (tiles draw in push
     * order). Reserve room for both, or the marker would be the last blit that
     * fits and the icon would drop out. */
    if( RS_WorldMap_ShouldFlashIcon(app->host.worldmap, element_id, element->category) &&
        app->worldmap_tile_count + 1 < capacity )
    {
        int flash_scene = app_worldmap_flash_marker_scene(app);
        if( flash_scene > 0 )
        {
            tile = &app->worldmap_tiles[app->worldmap_tile_count++];
            tile->scene_id = flash_scene;
            tile->atlas_index = 0;
            tile->w = 30;
            tile->h = 30;
            tile->scaled = 0;
            tile->x = screen_x - tile->w / 2;
            tile->y = screen_y - tile->h / 2;
        }
    }

    tile = &app->worldmap_tiles[app->worldmap_tile_count++];
    tile->scene_id = scene_id;
    tile->atlas_index = 0;
    tile->w =
        sprite->frames[0].crop_width > 0 ? sprite->frames[0].crop_width : sprite->frames[0].width;
    tile->h = sprite->frames[0].crop_height > 0 ? sprite->frames[0].crop_height
                                                : sprite->frames[0].height;
    tile->scaled = 0;
    /* Centred on its tile, like every map icon in the reference. */
    tile->x = screen_x - tile->w / 2;
    tile->y = screen_y - tile->h / 2;
    return true;
}

/** Nearest first; ties broken by region so the order is stable frame to frame. */
static int
app_worldmap_visit_cmp(
    void const* lhs,
    void const* rhs)
{
    struct App_WorldMapVisit const* a = (struct App_WorldMapVisit const*)lhs;
    struct App_WorldMapVisit const* b = (struct App_WorldMapVisit const*)rhs;
    if( a->distance != b->distance )
        return a->distance < b->distance ? -1 : 1;
    if( a->region_y != b->region_y )
        return a->region_y - b->region_y;
    return a->region_x - b->region_x;
}

static int
app_worldmap_build_tiles(
    struct App* app,
    struct UITreeHostRequest* req)
{
    struct RS_WorldMapState* map = app->host.worldmap;
    struct ToriRS_WorldMapArea const* area;
    int box_x = req->u.get_worldmap_tiles.box_x;
    int box_y = req->u.get_worldmap_tiles.box_y;
    int box_w = req->u.get_worldmap_tiles.box_w;
    int box_h = req->u.get_worldmap_tiles.box_h;
    /* Two scales, deliberately: `bake_scale` is the whole-pixel size regions
     * are rendered at (bakes are keyed by it, so it must not follow the zoom
     * animation or every intermediate value would rebake the whole view), and
     * `scale_fp` is where the zoom actually is this frame. Everything measured
     * on screen uses the second; only RegionSprite takes the first. */
    int bake_scale;
    int scale_fp;
    /* Pixel width of a whole region at the live zoom, and the unit every
     * position below is derived from — reference method5686 computes exactly
     * this (`(int)(zoom * 64)`) and lays regions out in multiples of it, so
     * neighbours stay flush instead of drifting apart by a rounding error. */
    int region_px;
    int display_x;
    int display_y;
    int centre_x;
    int centre_y;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int min_region_x;
    int max_region_x;
    int min_region_y;
    int max_region_y;

    app->worldmap_tile_count = 0;
    /* Emit-time record of where the tiles were placed this redraw. Click
     * coordinate math reads it; whether the map is open at all is answered by
     * app_worldmap_surface_live, not by this box — emit only runs on redraw
     * frames, and once the interface is hidden it stops writing, so the last
     * rectangle would otherwise outlive the open map. */
    app->worldmap_drag.box_x = box_x;
    app->worldmap_drag.box_y = box_y;
    app->worldmap_drag.box_w = box_w;
    app->worldmap_drag.box_h = box_h;
    *req->u.get_worldmap_tiles.out_items = app->worldmap_tiles;
    if( req->u.get_worldmap_tiles.out_background_rgb )
        *req->u.get_worldmap_tiles.out_background_rgb = 0;

    if( !map || box_w <= 0 || box_h <= 0 )
        return 0;
    /* Adopts the areas once the load task has published them. */
    if( !RS_WorldMap_Sync(map) )
        return 0;
    area = RS_WorldMap_CurrentArea(map);
    if( !area )
        return 0;

    /* TORIRS_WORLDMAP_FORCE_MAP=<id>: one-shot area switch for measuring why
     * non-Gielinor surfaces go black. Logs display vs region bounds and leaves
     * the forced area selected for the rest of the run. */
    {
        static int force_done;
        char const* force = getenv("TORIRS_WORLDMAP_FORCE_MAP");
        if( force && !force_done )
        {
            int map_id = (int)strtol(force, NULL, 0);
            force_done = 1;
            RS_WorldMap_SetCurrentMapId(map, map_id);
            area = RS_WorldMap_CurrentArea(map);
            if( area )
            {
                int dx, dy;
                RS_WorldMap_DisplayPosition(map, &dx, &dy);
                TORIRS_ERR(
                    "worldmap FORCE_MAP id=%d name=%s display=%d,%d "
                    "regions x=%d..%d y=%d..%d sources=%d sections=%d zoom=%d\n",
                    area->id,
                    area->internal_name ? area->internal_name : "?",
                    dx,
                    dy,
                    area->region_low_x,
                    area->region_high_x,
                    area->region_low_y,
                    area->region_high_y,
                    area->region_source_count,
                    area->section_count,
                    RS_WorldMap_Zoom(map));
            }
            else
                TORIRS_ERR("worldmap FORCE_MAP id=%d: Area() returned NULL\n", map_id);
        }
    }

    /* Drop resident Gielinor (or previous-area) bakes on area change so the new
     * area does not churn the LRU on its first frames. */
    {
        static int last_area_id = -1;
        int area_id = area->id;
        if( last_area_id >= 0 && last_area_id != area_id )
            RS_WorldMapRender_Clear(app->worldmap_render, app->scene);
        last_area_id = area_id;
    }

    if( req->u.get_worldmap_tiles.out_background_rgb )
        *req->u.get_worldmap_tiles.out_background_rgb = area->background_colour & 0xFFFFFF;

    /* The widget owns the surface size; the scripts read it back through
     * WORLDMAP_GETSIZE, so it has to be told what it actually got. */
    RS_WorldMap_SetDisplayPixelSize(map, box_w, box_h);

    /* TORIRS_WORLDMAP_ZOOM="z[,z2[,at_frame]]": force the zoom, optionally
     * switching to z2 after at_frame frames (default 300). The zoom buttons are
     * CS2 ops on the surface chrome, so a headless run cannot press them, and
     * the transition between two zooms is the thing worth capturing. */
    {
        char const* forced = getenv("TORIRS_WORLDMAP_ZOOM");
        if( forced )
        {
            char* end = NULL;
            long first = strtol(forced, &end, 0);
            long second = first;
            long at_frame = 300;
            if( end && *end == ',' )
            {
                second = strtol(end + 1, &end, 0);
                if( end && *end == ',' )
                    at_frame = strtol(end + 1, NULL, 0);
            }
            RS_WorldMap_SetZoom(map, (int)(app->worldmap_debug_frame < at_frame ? first : second));
        }
    }

    bake_scale = RS_WorldMap_ZoomScale(map);
    scale_fp = RS_WorldMap_ZoomScaleFp(map);
    if( bake_scale <= 0 )
        bake_scale = 1;
    if( scale_fp <= 0 )
        scale_fp = RS_WORLDMAP_ZOOM_SCALE_ONE;
    region_px = WORLD_MAP_TERRAIN_X * scale_fp / RS_WORLDMAP_ZOOM_SCALE_ONE;
    if( region_px <= 0 )
        region_px = 1;
    RS_WorldMap_DisplayPosition(map, &display_x, &display_y);
    if( display_x < 0 || display_y < 0 )
        return 0;

    centre_x = box_x + box_w / 2;
    centre_y = box_y + box_h / 2;
    RS_WorldMapRender_BeginFrame(app->worldmap_render);
    /* The mapscene pack lives in the bridge's static-sprite registry, which the
     * renderer cannot reach; hand it over before any bake. */
    RS_WorldMapRender_SetMapScenes(
        app->worldmap_render,
        UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPSCENE));

    /* One region of slack each way so a half-visible region at the edge is
     * still drawn (reference uses the same +/-64 tiles). */
    min_x = display_x - box_w * WORLD_MAP_TERRAIN_X / (2 * region_px) - WORLD_MAP_TERRAIN_X;
    max_x = display_x + box_w * WORLD_MAP_TERRAIN_X / (2 * region_px) + WORLD_MAP_TERRAIN_X;
    min_y = display_y - box_h * WORLD_MAP_TERRAIN_Z / (2 * region_px) - WORLD_MAP_TERRAIN_Z;
    max_y = display_y + box_h * WORLD_MAP_TERRAIN_Z / (2 * region_px) + WORLD_MAP_TERRAIN_Z;

    min_region_x = min_x / WORLD_MAP_TERRAIN_X;
    max_region_x = max_x / WORLD_MAP_TERRAIN_X;
    min_region_y = min_y / WORLD_MAP_TERRAIN_Z;
    max_region_y = max_y / WORLD_MAP_TERRAIN_Z;
    if( min_region_x < area->region_low_x )
        min_region_x = area->region_low_x;
    if( max_region_x > area->region_high_x )
        max_region_x = area->region_high_x;
    if( min_region_y < area->region_low_y )
        min_region_y = area->region_low_y;
    if( max_region_y > area->region_high_y )
        max_region_y = area->region_high_y;

    /*
     * Visit order is nearest-the-centre first, as the reference sorts its
     * visible tiles. It decides who gets the frame's bake and asset-load
     * allowance, and scan order (top-left onwards) spends it on whatever
     * happens to be scanned first — so a region the view is centred on could
     * wait behind a whole screenful of edge regions, which is how a pan leaves
     * tiles unloaded until it has moved past them.
     */
    app->worldmap_visit_count = 0;
    for( int region_y = min_region_y; region_y <= max_region_y; region_y++ )
    {
        for( int region_x = min_region_x; region_x <= max_region_x; region_x++ )
        {
            struct App_WorldMapVisit* visit;
            int centre_tile_x = region_x * WORLD_MAP_TERRAIN_X + WORLD_MAP_TERRAIN_X / 2;
            int centre_tile_y = region_y * WORLD_MAP_TERRAIN_Z + WORLD_MAP_TERRAIN_Z / 2;
            int dx = centre_tile_x - display_x;
            int dy = centre_tile_y - display_y;

            if( app->worldmap_visit_count >=
                (int)(sizeof(app->worldmap_visits) / sizeof(app->worldmap_visits[0])) )
                break;
            visit = &app->worldmap_visits[app->worldmap_visit_count++];
            visit->region_x = region_x;
            visit->region_y = region_y;
            visit->distance = dx * dx + dy * dy;
        }
    }
    qsort(
        app->worldmap_visits,
        (size_t)app->worldmap_visit_count,
        sizeof(app->worldmap_visits[0]),
        app_worldmap_visit_cmp);

    for( int i = 0; i < app->worldmap_visit_count; i++ )
    {
        {
            int region_x = app->worldmap_visits[i].region_x;
            int region_y = app->worldmap_visits[i].region_y;
            struct UITreeWorldMapTile* tile;
            int size = 0;
            int fallback_scene_id = -1;
            int scene_id;

            if( app->worldmap_tile_count >=
                (int)(sizeof(app->worldmap_tiles) / sizeof(app->worldmap_tiles[0])) )
                break;

            scene_id = RS_WorldMapRender_RegionSprite(
                app->worldmap_render,
                app->provider,
                app->scene,
                app->runner.queue,
                area,
                region_x,
                region_y,
                bake_scale,
                &size,
                &fallback_scene_id);
            /* Mid-zoom the right bake may not exist yet; a bake of the same
             * region at the previous zoom stands in, stretched, so the view
             * scales continuously instead of blinking through the background. */
            if( scene_id < 0 )
                scene_id = fallback_scene_id;
            if( scene_id < 0 )
                continue;

            tile = &app->worldmap_tiles[app->worldmap_tile_count++];
            tile->scene_id = scene_id;
            tile->atlas_index = 0;
            tile->x = centre_x + (region_x * WORLD_MAP_TERRAIN_X - display_x) * region_px /
                                     WORLD_MAP_TERRAIN_X;
            tile->y =
                centre_y - ((region_y * WORLD_MAP_TERRAIN_Z + WORLD_MAP_TERRAIN_Z) - display_y) *
                               region_px / WORLD_MAP_TERRAIN_Z;
            /* The box is a region at the *live* zoom either way — that is what
             * makes both the stand-in bake and a bake at another zoom step line
             * up with their neighbours while the transition runs. */
            tile->w = region_px;
            tile->h = region_px;
            tile->scaled = 1;
            (void)size;
        }
    }

    /* Icons in a second pass, so no later region paints over an earlier
     * region's icons: everything in this list draws in order. */
    for( int i = 0; i < app->worldmap_visit_count; i++ )
    {
        {
            int region_x = app->worldmap_visits[i].region_x;
            int region_y = app->worldmap_visits[i].region_y;
            struct RS_WorldMapRegionIcon const* icons = NULL;
            int scene_id = RS_WorldMapRender_RegionSprite(
                app->worldmap_render,
                app->provider,
                app->scene,
                app->runner.queue,
                area,
                region_x,
                region_y,
                bake_scale,
                NULL,
                NULL);
            int icon_count =
                scene_id < 0
                    ? 0
                    : RS_WorldMapRender_RegionIcons(app->worldmap_render, scene_id, &icons);

            for( int i = 0; i < icon_count; i++ )
                app_worldmap_push_icon(
                    app,
                    icons[i].element_id,
                    centre_x + (region_x * WORLD_MAP_TERRAIN_X + icons[i].tile_x - display_x) *
                                   region_px / WORLD_MAP_TERRAIN_X,
                    centre_y - (region_y * WORLD_MAP_TERRAIN_Z + icons[i].tile_y - display_y) *
                                   region_px / WORLD_MAP_TERRAIN_Z);
        }
    }

    app_worldmap_build_icons(app, area, centre_x, centre_y, display_x, display_y, region_px);

    /*
     * The HINT ARROW's target, marked on the map -- All Settings row 272,
     * "Clue scroll helper - Worldmap marker".
     *
     * Same payload as row 273's in-world arrow and deliberately so: neither row
     * has a reader in the cache or in the NXT engine, and the only marker family
     * the reference carries is the hint arrow's own three sprites
     * (`GetSpriteHintMapMarkersID` / `...HintMapEdgeID` / `...HintHeadIconsID`).
     * So one server-sent coord is what both rows are about, and the server's
     * choice of whether to send it is the setting; this is the map half of
     * drawing it.
     *
     * Only the COORD form. An npc or player subject moves, and the world map is
     * a static surface the player pans by hand -- a marker that chased an npc
     * across it would be redrawing a map the player is reading. The reference
     * marks a coord for the same reason.
     *
     * Last, so it lands over every icon: a marker underneath a mapfunction is a
     * marker nobody sees, and the icons are exactly what a clue step points at.
     */
    if( app->hint_arrow.type == APP_HINT_ARROW_COORD )
    {
        int map_x, map_y;
        int const capacity = (int)(sizeof(app->worldmap_tiles) / sizeof(app->worldmap_tiles[0]));

        /* Plane 0: the hint packet carries no plane, and the world map surface
         * is composited from one anyway. */
        if( app->worldmap_tile_count < capacity &&
            ToriRS_WorldMapArea_Position(
                area, 0, app->hint_arrow.target, app->hint_arrow.tile_z, &map_x, &map_y) )
        {
            int const flash_scene = app_worldmap_flash_marker_scene(app);
            int const x = centre_x + (map_x - display_x) * region_px / WORLD_MAP_TERRAIN_X;
            int const y = centre_y - (map_y - display_y) * region_px / WORLD_MAP_TERRAIN_Z;

            if( flash_scene > 0 && x > app->worldmap_drag.box_x - 32 &&
                x < app->worldmap_drag.box_x + app->worldmap_drag.box_w + 32 &&
                y > app->worldmap_drag.box_y - 32 && y < app->worldmap_drag.box_y + app->worldmap_drag.box_h + 32 )
            {
                /*
                 * The synthesised flash disc, not one of `worldmap_marker_0..8`.
                 * Those are the PLAYER-PLACED markers -- `marker_0` is the yellow
                 * X somebody dropped by hand -- and reusing one would make a
                 * server hint indistinguishable from the player's own note to
                 * themselves. The disc is already what this client draws to say
                 * "look here" (see `app_worldmap_flash_marker_scene`), and the
                 * cache names no hint-marker asset to prefer over it.
                 */
                struct UITreeWorldMapTile* tile = &app->worldmap_tiles[app->worldmap_tile_count++];

                tile->scene_id = flash_scene;
                tile->atlas_index = 0;
                tile->w = 30;
                tile->h = 30;
                tile->scaled = 0;
                tile->x = x - tile->w / 2;
                tile->y = y - tile->h / 2;
            }
        }
    }

    app->worldmap_debug_frame++;
    if( getenv("TORIRS_WORLDMAP_DEBUG") && app->worldmap_debug_frame % 300 == 0 )
    {
        /* Queue depth is the tell for the surface freezing: the runner is
         * serial (one task per IO round trip), so a backlog that climbs every
         * frame means loads are being queued faster than they can retire, and
         * anything newly in view waits behind all of it. */
        int queued = 0;
        for( struct ToriRS_Task* task = app->runner.queue ? app->runner.queue->head : NULL;
             task && queued < 100000;
             task = task->next )
            queued++;
        TORIRS_ERR(
            "worldmap frame: display=%d,%d zoom=%d bake_scale=%d region_px=%d "
            "regions x=%d..%d y=%d..%d blits=%d queued_tasks=%d\n",
            display_x,
            display_y,
            RS_WorldMap_Zoom(map),
            bake_scale,
            region_px,
            min_region_x,
            max_region_x,
            min_region_y,
            max_region_y,
            app->worldmap_tile_count,
            queued);
    }

    return app->worldmap_tile_count;
}

/*
 * Overview pane (clientCode 1401): scale-blit the current area's compositetexture
 * into the widget box. Red viewport rects are CS2 on overview_overlay — not here.
 * SpriteNewFromArgbOwned takes the pixel buffer, so each upload copies from the
 * area-owned decode; SceneSpriteAdd frees the previous overview sprite.
 */
static int
app_worldmap_ensure_overview_scene(
    struct App* app,
    struct ToriRS_WorldMapArea const* area)
{
    uint32_t* copy;
    struct ToriDraw_Sprite* sprite;
    struct ToriDraw_Sprite** sprites;
    size_t nbytes;

    assert(app);
    assert(area);
    assert(area->overview_pixels);
    assert(area->overview_width > 0);
    assert(area->overview_height > 0);

    if( app->worldmap_overview_area_id == area->id &&
        app->worldmap_overview_scene_id == UITREE_SCENE_WORLD_MAP_OVERVIEW_SPRITE_ID )
        return app->worldmap_overview_scene_id;

    nbytes = (size_t)area->overview_width * (size_t)area->overview_height * sizeof(*copy);
    copy = malloc(nbytes);
    assert(copy);
    memcpy(copy, area->overview_pixels, nbytes);

    sprite = ToriDraw_SpriteNewFromArgbOwned(copy, area->overview_width, area->overview_height);
    if( !sprite )
    {
        free(copy);
        return -1;
    }
    sprites = malloc(sizeof(*sprites));
    assert(sprites);
    sprites[0] = sprite;
    ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_WORLD_MAP_OVERVIEW_SPRITE_ID, sprites, 1);
    app->worldmap_overview_scene_id = UITREE_SCENE_WORLD_MAP_OVERVIEW_SPRITE_ID;
    app->worldmap_overview_area_id = area->id;
    return app->worldmap_overview_scene_id;
}

static int
app_worldmap_build_overview(
    struct App* app,
    struct UITreeHostRequest* req)
{
    struct RS_WorldMapState* map;
    struct ToriRS_WorldMapArea const* area;
    int box_x;
    int box_y;
    int box_w;
    int box_h;
    int scene_id;

    assert(app);
    assert(req);
    assert(req->u.get_worldmap_overview.out_items);

    box_x = req->u.get_worldmap_overview.box_x;
    box_y = req->u.get_worldmap_overview.box_y;
    box_w = req->u.get_worldmap_overview.box_w;
    box_h = req->u.get_worldmap_overview.box_h;

    memset(&app->worldmap_overview_tile, 0, sizeof(app->worldmap_overview_tile));
    *req->u.get_worldmap_overview.out_items = &app->worldmap_overview_tile;
    if( req->u.get_worldmap_overview.out_background_rgb )
        *req->u.get_worldmap_overview.out_background_rgb = 0;

    map = app->host.worldmap;
    if( !map || !RS_WorldMap_IsLoaded(map) )
        return 0;
    area = RS_WorldMap_CurrentArea(map);
    if( !area )
        return 0;

    if( req->u.get_worldmap_overview.out_background_rgb )
        *req->u.get_worldmap_overview.out_background_rgb = area->background_colour & 0xFFFFFF;

    if( !area->overview_pixels || area->overview_width <= 0 || area->overview_height <= 0 )
        return 0;

    scene_id = app_worldmap_ensure_overview_scene(app, area);
    if( scene_id <= 0 )
        return 0;

    app->worldmap_overview_tile.scene_id = scene_id;
    app->worldmap_overview_tile.atlas_index = 0;
    app->worldmap_overview_tile.x = box_x;
    app->worldmap_overview_tile.y = box_y;
    app->worldmap_overview_tile.w = box_w;
    app->worldmap_overview_tile.h = box_h;
    app->worldmap_overview_tile.scaled = 1;
    return 1;
}

/*
 * Map element icons over the surface (banks, altars, shops, ...).
 *
 * The compositemap gives each icon a *source* world coord and a map element id;
 * the area converts the coord to a map surface position, and the element config
 * (MEC, config group 35) gives the sprite. Both the config and the sprite load
 * on demand, so an icon appears a frame or two after the region under it —
 * exactly how the reference behaves on a cold cache.
 */
static void
app_worldmap_build_icons(
    struct App* app,
    struct ToriRS_WorldMapArea const* area,
    int centre_x,
    int centre_y,
    int display_x,
    int display_y,
    int region_px)
{
    for( int i = 0; i < area->icon_count; i++ )
    {
        struct ToriRS_WorldMapIcon const* icon = &area->icons[i];
        int plane;
        int world_x;
        int world_y;
        int map_x;
        int map_y;

        if( icon->hidden )
            continue;

        /* The compositemap stores a *source* world coord; the area's sections
         * say where that lands on the map surface. */
        ToriRS_WorldMapUnpackCoord(icon->coord, &plane, &world_x, &world_y);
        if( !ToriRS_WorldMapArea_Position(area, plane, world_x, world_y, &map_x, &map_y) )
            continue;

        app_worldmap_push_icon(
            app,
            icon->element,
            centre_x + (map_x - display_x) * region_px / WORLD_MAP_TERRAIN_X,
            centre_y - (map_y - display_y) * region_px / WORLD_MAP_TERRAIN_Z);
    }
}

/*
 * A click on the open world map, reported to the server as the absolute tile it
 * landed on (reference ClickWorldMap).
 *
 * The screen -> tile conversion is the inverse of the icon placement above: the
 * box centre shows the view's display position, and each map tile is
 * `zoom scale` pixels wide, with screen y growing opposite map y. That gives a
 * *display* coord (a position on the flattened map surface); the area's
 * sections turn it back into the world coord the surface was baked from, which
 * is what the packet carries.
 */
static void
app_worldmap_click(
    struct App* app,
    int mouse_x,
    int mouse_y)
{
    int display_x = 0;
    int display_y = 0;
    int scale_fp;
    int map_x;
    int map_y;
    int source;
    int plane;
    int abs_x;
    int abs_z;

    assert(app);
    if( !app->host.worldmap || !app->net )
        return;
    RS_WorldMap_DisplayPosition(app->host.worldmap, &display_x, &display_y);
    if( display_x < 0 || display_y < 0 )
        return;
    /* The live scale, not the target: this inverts what was drawn, and mid
     * zoom-transition those differ. */
    scale_fp = RS_WorldMap_ZoomScaleFp(app->host.worldmap);
    if( scale_fp <= 0 )
        return;

    map_x = display_x + (mouse_x - (app->worldmap_drag.box_x + app->worldmap_drag.box_w / 2)) *
                            RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp;
    map_y = display_y - (mouse_y - (app->worldmap_drag.box_y + app->worldmap_drag.box_h / 2)) *
                            RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp;

    source =
        RS_WorldMap_DisplayToSource(app->host.worldmap, ToriRS_WorldMapPackCoord(0, map_x, map_y));
    if( source < 0 )
        return;
    ToriRS_WorldMapUnpackCoord(source, &plane, &abs_x, &abs_z);
    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "worldmap_click: screen=%d,%d display=%d,%d -> %d,%d,%d\n",
            mouse_x,
            mouse_y,
            map_x,
            map_y,
            plane,
            abs_x,
            abs_z);
    APP_NET_SEND(
        app,
        net_out_click_world_map(
            app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), plane, abs_x, abs_z));
}

/*
 * Is the map surface actually on screen? The emit-time box cannot answer this:
 * emit only runs on redraw frames, and once the interface is hidden it stops
 * running at all, so the last box it recorded outlives the open map. Close
 * sets hide only on the group roots (not on the builtin surface node itself),
 * so the ancestor walk and RootIsDisplayable are both required.
 */
static int
app_worldmap_surface_live(struct App* app)
{
    struct UITree* tree;
    int32_t idx;

    assert(app);
    tree = app->tree;
    if( !tree )
        return 0;
    idx = tree->worldmap_index;
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return 0;
    if( tree->components[idx].freed || tree->components[idx].type != UIELEM_BUILTIN_WORLDMAP )
        return 0;
    /* Includes cache/script hide, plugin-frame suppression, mount-container
     * ancestry and root displayability.  The old local walk tested only
     * behavior.hide, so a frame-hidden world map still owned drag/click. */
    return !UITree_NodeOrAncestorDisplayHidden(tree, idx);
}

/*
 * Drag to pan the world map, as the App sees it.
 *
 * The pan itself is UIWorldMapDrag (game/rs_worldmap_drag.h). What is here is
 * the half that needs an App: gathering this frame's pointer facts, and
 * turning the result back into a click or a redraw.
 */
static void
app_worldmap_drag_tick(
    struct App* app,
    struct LibToriRS_Input* input,
    int pointer_consumed)
{
    struct UIWorldMapDragInput drag_input;
    enum UIWorldMapDragResult result;

    assert(app);
    assert(input);

    drag_input.mouse_x = input->curr.mouse_x;
    drag_input.mouse_y = input->curr.mouse_y;
    drag_input.left_down = LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT);
    drag_input.left_held = LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);
    drag_input.left_up = input->curr.mouse_button_up[TORIRSM_LEFT];
    drag_input.pointer_consumed = pointer_consumed;
    drag_input.minimenu_visible = app->interact.minimenu.visible;
    drag_input.hover_component_id = app->hover_com_id;
    drag_input.surface_live = app_worldmap_surface_live(app);

    result = UIWorldMapDrag_Tick(&app->worldmap_drag, &drag_input, app->host.worldmap);
    if( result == UI_WORLDMAP_DRAG_CLICKED )
        app_worldmap_click(app, drag_input.mouse_x, drag_input.mouse_y);
    else if( result == UI_WORLDMAP_DRAG_PANNED )
        app->need_redraw = 1;
}

/* Defined with the world-map bake further down. */
static int
app_minimap_level(
    struct App* app,
    struct WorldEntity_Player const* local);

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

    app->minimap_dot_count = 0;
    *out_dots = app->minimap_dots;
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
                int before = app->minimap_dot_count;

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
                if( app->minimap_dot_count > before )
                    app->minimap_dots[app->minimap_dot_count - 1].rotate =
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

    if( marker_scene > 0 && app->minimap_flag_x >= 0 )
        app_minimap_push_dot(
            app,
            app->minimap_flag_x * 128 + 64 - px,
            app->minimap_flag_z * 128 + 64 - pz,
            marker_scene,
            0);

    /* Local player: white 3x3 square at the widget center (fillRect 97,78). */
    if( app->minimap_dot_count < (int)(sizeof(app->minimap_dots) / sizeof(app->minimap_dots[0])) )
    {
        struct UITreeMinimapDot* dot = &app->minimap_dots[app->minimap_dot_count++];
        dot->dx = -1;
        dot->dy = -1;
        dot->w = 3;
        dot->h = 3;
        dot->scene_id = 0;
        dot->atlas_index = 0;
        dot->color = 0xFFFFFFFFu;
        dot->rotate = 0; /* persistent array — see app_minimap_push_dot */
    }
    return app->minimap_dot_count;
}

/* ---------------------------------------------------------------------- */
/* Entity overlays: health bars + hitsplats (reference drawEntities)       */
/* ---------------------------------------------------------------------- */

/* Defined with the other cache/scene helpers further down. */
static int
app_world_height(
    void* userdata,
    int world_x,
    int world_z,
    int level);
/* Defined with the world-entity helpers further down (SAILING_PLAN C3). */
static void
app_wev_register_pseudo_locs(
    void* userdata,
    struct World* world);
static int
app_wev_claim_deck_actors(
    void* userdata,
    struct World* world,
    int* out_element_ids,
    int max);
static int
app_cinema_level(struct App* app);
static int
app_hitsplat_font_scene_id(struct App* app);
static int
app_minimenu_font_scene_id(struct App* app);

/*
 * Reference getOverlayPos (Client.ts:5253): rotate the entity's
 * camera-relative fine offset by yaw then pitch and divide by depth, from the
 * viewport centre. Returns 0 when the point is behind the near plane
 * (reference sets projectX = -1) or off the map.
 *
 * The linear scale is the camera's own, NOT the reference's `<< 9`:
 * Client-TS could shift by UNIT_SCALE_SHIFT because its world scale was the
 * constant 512, and ours stopped being one in §15. A hardcoded 512 here
 * re-creates the §1 wedge for every overlay — outlines, health bars,
 * hitsplats and overhead chat all landing 512/scale times too far from the
 * viewport centre.
 */
/* Project a world point at an ABSOLUTE height. The height-above-ground
 * spelling below samples terrain per point, which is right for entities but
 * wrong for anything that must stay coplanar — a footprint outline on sloped
 * ground warps if each corner samples its own column. */
static int
app_world_project_at(
    struct App* app,
    int fine_x,
    int fine_z,
    int world_y,
    int* out_x,
    int* out_y)
{
    if( !app->world || !app->world_view_valid )
        return 0;
    if( fine_x < 128 || fine_z < 128 )
        return 0;
    /* Keep the reference's overlay near plane at 50. The renderer camera may
     * be lowered experimentally, but accepting a point closer than the overlay
     * contract did before this extraction would be an appearance change. */
    return ToriRS_WorldProjectPoint(
        &app->world_camera,
        &app->world_camera_pos,
        app->world_emit_desc.x,
        app->world_emit_desc.y,
        app->world_emit_desc.w,
        app->world_emit_desc.h,
        50,
        fine_x,
        world_y,
        fine_z,
        out_x,
        out_y);
}

static int
app_world_project(
    struct App* app,
    int fine_x,
    int fine_z,
    int height_above_ground,
    int* out_x,
    int* out_y)
{
    int ground_y;
    int level = 0;

    if( !app->world || !app->world_view_valid )
        return 0;
    if( fine_x < 128 || fine_z < 128 )
        return 0;
    /* The effective ROOT plane, not the local player's own: aboard, the
     * player's level is a DECK plane (the planking is authored at plane 1),
     * and sampling root terrain there put every shore npc's health bar and
     * hitsplat at the level-1 heightmap's idea of the ground. Ashore the two
     * are the same number. Same authority as the movers and the minimap. */
    level = app_cinema_level(app);
    ground_y = app_world_height(app, fine_x, fine_z, level);
    return app_world_project_at(app, fine_x, fine_z, ground_y - height_above_ground, out_x, out_y);
}

/* Defined with the world-entity helpers further down. */
static int
app_wev_deck_level(
    struct App* app,
    int view_id);

/*
 * Project an ACTOR's overlay anchor. A root actor is app_world_project
 * verbatim. A HOMED rider's draw position is DECK-LOCAL fine units, so the
 * root spelling composes the way the emit path does: the point out through
 * the hull (Wev_ParentFromDeck) at the height the descent draws them — the
 * deck world's heightmap at the actor's own plane, plus the hull's y and
 * bob. The deob never converts at all: each world's actors project through
 * that world's own composed transform (its 2D overlays anchor on the screen
 * coordinates that projection produced), which is what makes a splat above a
 * rider ride the hull. Projected (footprint-routed) actors keep root draw
 * positions and take the root arm.
 */
static int
app_world_project_actor(
    struct App* app,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int fine_x,
    int fine_z,
    int height_above_ground,
    int* out_x,
    int* out_y)
{
    struct Wev* wev;
    struct Worldview* view;
    struct WevDeckBox box;
    int root_fx;
    int root_fz;
    int ground_y;
    int deck_level;

    if( !placement || placement->view_id == WORLDVIEW_ROOT ||
        !Wevs_IsLive(&app->wevs, placement->view_id) ||
        !WorldviewRegistry_IsLive(&app->worldviews, placement->view_id) )
        return app_world_project(app, fine_x, fine_z, height_above_ground, out_x, out_y);
    wev = Wevs_Get(&app->wevs, placement->view_id);
    /* Model population and its overlay have the same visibility contract:
     * flattened/skipped passengers must not leave floating names or bars. */
    if( wev->flattened || !wev->render_visible )
        return 0;
    if( wev->parent_view_id != WORLDVIEW_ROOT )
        return app_world_project(app, fine_x, fine_z, height_above_ground, out_x, out_y);
    view = WorldviewRegistry_Get(&app->worldviews, placement->view_id);
    if( placement->home_view == 0 )
    {
        /* Projected crew retain root-wire draw positions, but their borrowed
         * render placement already contains the correct deck-local point. */
        fine_x = placement->x;
        fine_z = placement->z;
    }
    app_wev_deck_box(app, wev, app->world, &box);
    Wev_ParentFromDeck(&box, fine_x, fine_z, &root_fx, &root_fz);
    deck_level = actor_level;
    if( deck_level < 0 )
        deck_level = app_wev_deck_level(app, placement->view_id);
    if( deck_level >= COLLISION_LEVELS )
        deck_level = COLLISION_LEVELS - 1;
    ground_y = wev->y + wev->bob_y + World_HeightAt(view->world, fine_x, fine_z, deck_level);
    return app_world_project_at(
        app, root_fx, root_fz, ground_y - height_above_ground, out_x, out_y);
}

/* Reference ClientEntity.height = model.minY, which Client-TS accumulates as
 * `max(-vertexY)` — a POSITIVE magnitude measuring up from the model origin.
 * ToriDraw's bounds cylinder stores the true minimum instead (negative, since
 * up is -y), so it has to be negated here. Getting this wrong collapses the
 * health bar onto the entity's feet.
 *
 * ClientNpc/ClientPlayer.getTempModel() sets `this.height = model.minY` from
 * the entity's OWN model, then — only after that assignment — combines in the
 * attached graphic for rendering (ClientNpc.ts:34 runs before the spotanim
 * branch below it). `height` never sees the combined mesh.
 *
 * A live attached graphic (`app_entity_spotanim_find` non-NULL) means this
 * element's current model is that combined mesh: `app_world_sync_one_entity_
 * spotanim` merges the spot graphic's posed geometry into it every frame the
 * spot animation advances and calls `ToriDraw_ModelMerge`, which recomputes
 * the bounds cylinder over every vertex in the merge. Reading that live bounds
 * here pulled the spot graphic's own (frequently rescaled, always moving)
 * geometry into the entity's reported height, so the health bar / hitsplat /
 * chat / headicon position — everything anchored on this — tracked the spot
 * animation's pose instead of standing still on the entity. `entry->body` is
 * the pristine pre-combine snapshot (`ToriDraw_ModelCopy` sets its own bounds
 * cylinder), the port's equivalent of the reference's separate `height` field. */
static int
app_entity_model_height(
    struct App* app,
    int element_id)
{
    struct ToriDraw_SceneElement* el;
    struct ToriDraw_BoundsCylinder* bounds;
    struct AppEntitySpotanim* spot_entry = app_entity_spotanim_find(app, element_id, 0);

    if( spot_entry && spot_entry->body )
    {
        struct ToriDraw_ModelHandle body_hnd = { .kind = TORIDRAWMK_MODEL };
        body_hnd.u.model.model = spot_entry->body;
        bounds = ToriDraw_ModelGetBoundsCylinder(body_hnd);
        return bounds ? -bounds->min_y : 0;
    }

    if( element_id < 0 || !app->scene || !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return 0;
    el = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !el || !ToriDraw_ModelKindIsFull(el->model.kind) )
        return 0;
    bounds = ToriDraw_ModelGetBoundsCylinder(el->model);
    return bounds ? -bounds->min_y : 0;
}

/* The height OVERHEADS hang off, which is not always the model's height.
 *
 * Reference `Actor.getLogicalHeight` and the NPC override of it: an npc whose
 * type states `height` (opcode 124) anchors its bar and splats at that instead
 * of at the model, and the model is unaffected either way. Otherwise the anchor
 * is `logicalHeight`, which the reference refreshes from each model it builds
 * and initialises to 200 -- so an actor that never builds a model keeps 200
 * rather than collapsing to the floor. That default is the whole reason a
 * model-less marker npc reads as floating slightly above its tile there, and
 * `height` is how a record moves it deliberately. */
#define APP_OVERLAY_DEFAULT_LOGICAL_HEIGHT 200

static int
app_entity_overlay_height(
    struct App* app,
    int element_id,
    int type_height)
{
    int height;

    if( type_height >= 0 )
        return type_height;
    height = app_entity_model_height(app, element_id);
    return height > 0 ? height : APP_OVERLAY_DEFAULT_LOGICAL_HEIGHT;
}

static void
app_overlay_push(
    struct App* app,
    struct UITreeEntityOverlay const* item)
{
    if( app->plugin_draw_canvas == APP_PLUGIN_SURFACE_PANEL )
    {
        struct UITreeEntityOverlay* out;

        /* PANEL is a staged, panel-local target. If the application did not
         * prepare an exact custom well, drawing is dropped rather than falling
         * through into the world list. That is the isolation boundary. */
        if( !app->panel_overlay_stage_active )
            return;
        if( app->panel_overlay_stage_count >= APP_PLUGIN_PANEL_OVERLAYS_MAX )
        {
            app->panel_overlay_stage_overflow = 1;
            return;
        }

        out = &app->panel_overlay_stage[app->panel_overlay_stage_count];
        if( !ToriRSChromePanelDraw_Transform(
                item,
                app->panel_overlay_origin_x,
                app->panel_overlay_origin_y,
                app->panel_overlay_scale,
                app->panel_overlay_clip,
                out) )
            return;
        app->panel_overlay_stage_count++;
        return;
    }

    /*
     * Which list depends on the draw window that is open, and nothing above
     * this has to know which one that is.
     *
     * Every built-in overlay -- health bars, hitsplats, overhead chat, the
     * editor marks -- is built with no window open at all, so `canvas` is 0
     * for all of them and they land in the world list exactly as before. Only
     * an explicit plugin draw event flips it; PANEL returned above and can
     * never enter one of these game lists.
     */
    if( app->plugin_draw_canvas == APP_PLUGIN_SURFACE_CANVAS )
    {
        int cap = (int)(sizeof(app->canvas_overlays) / sizeof(app->canvas_overlays[0]));
        if( app->canvas_overlay_count >= cap )
            return;
        app->canvas_overlays[app->canvas_overlay_count++] = *item;
        return;
    }

    int cap = (int)(sizeof(app->entity_overlays) / sizeof(app->entity_overlays[0]));
    if( app->entity_overlay_count >= cap )
        return;
    app->entity_overlays[app->entity_overlay_count++] = *item;
}

/** How many items the open draw window has pushed, so a draw verb can report
 *  its own cost without knowing which list it landed in. */
static int
app_overlay_count(struct App const* app)
{
    assert(app);
    switch( app->plugin_draw_canvas )
    {
    case APP_PLUGIN_SURFACE_CANVAS:
        return app->canvas_overlay_count;
    case APP_PLUGIN_SURFACE_PANEL:
        return app->panel_overlay_stage_active ? app->panel_overlay_stage_count : 0;
    default:
        return app->entity_overlay_count;
    }
}

/* One entity's overlay set. combat/damage state lives on the shared facet, so
 * players and NPCs go through the same body (reference drawEntities treats
 * them identically). */
/* Resolve a reference chatColour/chatTimer pair to an ARGB colour. Static
 * palette entries (< 6) map straight through; flashing/rainbow effects (6-11)
 * animate off the scene cycle / remaining timer (reference Client.ts:4962). */
static uint32_t
app_overlay_chat_colour(
    struct App* app,
    int chat_colour,
    int timer)
{
    static const int CHAT_COLOURS[6] = {
        0xffff00, /* YELLOW */
        0xff0000, /* RED */
        0x00ff00, /* GREEN */
        0x00ffff, /* CYAN */
        0xff00ff, /* MAGENTA */
        0xffffff, /* WHITE */
    };
    int cyc = app->world ? app->world->cycle : 0;
    int rgb = 0xffff00;
    int delta = 150 - timer;

    if( chat_colour >= 0 && chat_colour < 6 )
        rgb = CHAT_COLOURS[chat_colour];
    else if( chat_colour == 6 )
        rgb = (cyc % 20 < 10) ? 0xff0000 : 0xffff00;
    else if( chat_colour == 7 )
        rgb = (cyc % 20 < 10) ? 0x0000ff : 0x00ffff;
    else if( chat_colour == 8 )
        rgb = (cyc % 20 < 10) ? 0x00b000 : 0x80ff80;
    else if( chat_colour == 9 )
    {
        if( delta < 50 )
            rgb = delta * 1280 + 0xff0000;
        else if( delta < 100 )
            rgb = 0xffff00 - (delta - 50) * 327680;
        else if( delta < 150 )
            rgb = (delta - 100) * 5 + 0x00ff00;
    }
    else if( chat_colour == 10 )
    {
        if( delta < 50 )
            rgb = delta * 5 + 0xff0000;
        else if( delta < 100 )
            rgb = 0xff00ff - (delta - 50) * 327680;
        else if( delta < 150 )
            rgb = (delta - 100) * 327680 + 0x0000ff - (delta - 100) * 5;
    }
    else if( chat_colour == 11 )
    {
        if( delta < 50 )
            rgb = 0xffffff - delta * 327685;
        else if( delta < 100 )
            rgb = (delta - 50) * 327685 + 0x00ff00;
        else if( delta < 150 )
            rgb = 0xffffff - (delta - 100) * 327680;
    }
    return 0xff000000u | (uint32_t)(rgb & 0xffffff);
}

/* Overhead chat: a black shadow then the (colour-resolved) message, centred
 * above the model top (reference drawEntities, Client.ts:4871/4958). Effects
 * (wave/scroll) fall back to plain centred text — the styled variants need
 * per-glyph font passes the overlay descs don't carry yet. */
static void
app_overlay_build_chat(
    struct App* app,
    int element_id,
    struct WorldEntityFacet_Chat const* chat,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int font_id)
{
    int height = app_entity_model_height(app, element_id);
    int screen_x, screen_y;

    assert(chat);
    if( chat->timer <= 0 || chat->message[0] == '\0' || font_id < 0 )
        return;
    if( !app_world_project_actor(
            app,
            placement,
            actor_level,
            (int)draw_position->x,
            (int)draw_position->z,
            height,
            &screen_x,
            &screen_y) )
        return;

    struct UITreeEntityOverlay shadow = {
        .kind = UITREE_ENTITY_OVERLAY_TEXT,
        .x = screen_x,
        .y = screen_y + 1,
        .font_id = font_id,
        .color = 0xff000000u,
    };
    snprintf(shadow.text, sizeof(shadow.text), "%s", chat->message);
    app_overlay_push(app, &shadow);

    struct UITreeEntityOverlay body = shadow;
    body.y = screen_y;
    body.color = app_overlay_chat_colour(app, chat->colour, chat->timer);
    app_overlay_push(app, &body);
}

/* Overhead prayer/skull headicons (reference drawEntities, Client.ts:4849).
 * `headicons` is a bitmask; each set bit plots sprite[icon] from the headicons
 * pack stacked upward above the model top (start 30px up, 25px per icon).
 * Projection is at `entity.height + 15`, same as the health bar.
 *
 * The mask is walked to 31, not to 8. Eight was the width of the classic wire
 * field, but the pack it indexes is 24 frames deep at rev 239 and 30 with the
 * Ancient Curses lane's six overheads appended (Deflect ×4, Wrath, Soul Split
 * at 24..29). A loop that stops at 8 does not draw a smaller icon for those —
 * it draws nothing, and the curse reads as having no overhead at all. */
static void
app_overlay_build_player_headicons(
    struct App* app,
    int element_id,
    int headicons,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int headicons_scene)
{
    int height = app_entity_model_height(app, element_id);
    int screen_x, screen_y;
    int y_off = 30;

    if( headicons == 0 || headicons_scene <= 0 )
        return;
    if( !app_world_project_actor(
            app,
            placement,
            actor_level,
            (int)draw_position->x,
            (int)draw_position->z,
            height + 15,
            &screen_x,
            &screen_y) )
        return;

    for( int icon = 0; icon < 31; icon++ )
    {
        if( (headicons & (0x1 << icon)) == 0 )
            continue;
        struct UITreeEntityOverlay spr = {
            .kind = UITREE_ENTITY_OVERLAY_SPRITE,
            .x = screen_x - 12,
            .y = screen_y - y_off,
            .w = 0,
            .h = 0,
            .scene_id = headicons_scene,
            .atlas_index = icon,
        };
        app_overlay_push(app, &spr);
        y_off -= 25;
    }
}

/*
 * The HINT ARROW -- the server pointing at something.
 *
 * `HINT_ARROW` (server prot 50) has been parsed into `app->hint_arrow` for as
 * long as the packet existed and drawn nowhere, which `app.h` recorded as
 * "drawing is a flagged follow-on". This is that follow-on.
 *
 * It is also the only mechanism this revision has for two All Settings rows:
 *
 *   272  Clue scroll helper - Worldmap marker
 *   273  Clue scroll helper - World arrows
 *
 * Neither has a reader in the cache and neither has one in the NXT engine --
 * the only marker family the reference carries is
 * `GraphicsDefaults::GetSpriteHintMapMarkersID` / `...HintHeadIconsID` /
 * `...HintMapEdgeID`, which is this. So the payload is one coord the server
 * sends, and the two rows are the server's choice of whether to send it; the
 * client's job is to draw the arrow it is given.
 *
 * ## The three subject kinds
 *
 * The wire's `type` byte selects what `id`/`z` mean, and the reference's own
 * values are 1 = a COORD (id is x, z is z, and the height byte is how far above
 * the tile the arrow floats), 2 = an NPC by slot, 10 = a PLAYER by pid. 255
 * clears, which `rs_gameproto_exec.c` already normalises to 0.
 *
 * A subject that is out of view is not an error and not a clear: an npc can
 * walk behind the camera and come back. It simply does not project this frame,
 * which is the same distinction the scripted-overlay reaper had to learn.
 */
static void
app_overlay_build_hint_arrow(struct App* app)
{
    int const type = app->hint_arrow.type;
    int hint_scene;
    int screen_x, screen_y;
    int world_x, world_z, height;
    /* A homed rider's draw position is deck-local; the arrow anchors through
     * the same actor projection as their health bar. NULL = a root point. */
    struct WorldEntityFacet_ViewPlacement const* placement = NULL;
    int actor_level = -1;

    if( type <= 0 || !app->world )
        return;

    hint_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_HEADICONS_HINT);
    if( hint_scene <= 0 )
        return;

    switch( type )
    {
    case APP_HINT_ARROW_COORD:
    {
        /*
         * A tile, in ABSOLUTE world coordinates, converted to the scene's own
         * frame through the same origin `SET_MAP_FLAG`'s absolute form uses.
         *
         * Absolute rather than scene-local because the arrow's whole purpose is
         * to point somewhere the player is not, and a scene-local coord cannot
         * name a tile outside the loaded window. `ToriRSServer_SendHintArrowCoord`
         * sends it that way; a third-party server that sends scene-local coords
         * would put the arrow near the map corner, which is the symptom to look
         * for.
         *
         * The packet's `height` is in the projector's units above the tile, not
         * a pixel offset -- the reference floats a coord arrow clear of the
         * ground so it stays readable over scenery.
         */
        int const base_x = (app->rebuild_zone_x - 6) * 8;
        int const base_z = (app->rebuild_zone_z - 6) * 8;

        world_x = ((app->hint_arrow.target - base_x) << 7) + 64;
        world_z = ((app->hint_arrow.tile_z - base_z) << 7) + 64;
        height = app->hint_arrow.height * 2;
        break;
    }
    case APP_HINT_ARROW_NPC:
    {
        struct WorldEntity_NPC* npc = World_NpcGetByServerSlot(app->world, app->hint_arrow.target);

        if( !npc || npc->element_id < 0 )
            return;
        world_x = (int)npc->draw_position.x;
        world_z = (int)npc->draw_position.z;
        placement = &npc->view_placement;
        height = app_entity_model_height(app, npc->element_id) + 15;
        break;
    }
    case APP_HINT_ARROW_PLAYER:
    {
        struct WorldEntity_Player* player =
            World_PlayerGetByServerPid(app->world, app->hint_arrow.target);

        if( !player || player->element_id < 0 )
            return;
        world_x = (int)player->draw_position.x;
        world_z = (int)player->draw_position.z;
        placement = &player->view_placement;
        actor_level = player->grid_position.level;
        height = app_entity_model_height(app, player->element_id) + 15;
        break;
    }
    default:
        /* An unknown subject kind, which is a server sending something this
         * revision does not define. Silent: it is not this frame's business to
         * decide, and a log line per frame would be the whole log. */
        return;
    }

    if( !app_world_project_actor(
            app, placement, actor_level, world_x, world_z, height, &screen_x, &screen_y) )
        return;

    {
        /*
         * Frame 0: the solid downward arrow that sits over the subject.
         *
         * There is no screen-EDGE form to draw, and that is a fact about this
         * cache rather than a gap here. `headicons_hint` (sprite archive 441)
         * declares six 25x25 frames and **only two have any pixels**: frame 0 is
         * the solid arrow and frame 1 is the same arrow in outline. Frames 2..5
         * are entirely transparent.
         *
         * The reference's edge form comes from a different pack --
         * `GraphicsDefaults::GetSpriteHintMapEdgeID`, beside
         * `...HintMapMarkersID` -- and this cache's sprite gameval table names
         * no such group: `headicons_hint` is its only hint asset. So an
         * off-screen arrow here would need artwork invented for it.
         *
         * Re-derive with:
         *   3rd/rscache/tools/spritebake/spritebake --rev osrs239 cache.osrs239 \
         *       --list --probe headicons_hint
         */
        struct UITreeEntityOverlay spr = {
            .kind = UITREE_ENTITY_OVERLAY_SPRITE,
            .x = screen_x - 12,
            .y = screen_y - 48,
            .w = 0,
            .h = 0,
            .scene_id = hint_scene,
            .atlas_index = 0,
        };
        app_overlay_push(app, &spr);
    }
}

/*
 * The sprite-group id of `headicons_prayer`.
 *
 * An npc's opcode-102 icon names its group as a NUMBER, and the client
 * resolves that pack by NAME (static_sprites.c, STATIC_SPRITE_HEADICONS_PRAYER)
 * — the provider offers no synchronous name -> group-id lookup to close the
 * gap with, only an async load task. So the number is stated here, from
 * `OSRS-Content/osrs239-content/pack/8_sprites.pack` line 441, where it is the
 * only group any of this cache's 77 headicon-bearing npc records names.
 *
 * Failure mode if a future cache renumbers it: npcs stop drawing overheads.
 * That is the deliberate direction — a record naming an unrecognised group is
 * skipped rather than drawn out of the prayer pack, because an icon that says
 * "Protect from Magic" when the record meant something else is worse than no
 * icon at all.
 */
#define APP_HEADICONS_PRAYER_GROUP 440

/*
 * Overhead prayer icon for an NPC (reference drawEntities, NpcType.headicon).
 *
 * Where a player carries an eight-bit MASK and stacks every set bit, an npc
 * carries ONE frame from one sprite group and plots it in the first slot. That
 * asymmetry is the reference's, not a simplification: the player's icons are a
 * live prayer set, the npc's is a property of which record it currently is.
 * Which is exactly how a prayer-switching npc works — `npc_changetype` between
 * records that differ only in this field is what makes the overhead change.
 *
 * The group is a sprite-archive id. 440 (`headicons_prayer`) is the only one
 * cache.osrs239 uses on an npc, and the client already resolves that pack for
 * the player pass, so it is passed in rather than looked up again here; a
 * record naming any other group draws nothing rather than drawing the wrong
 * pack's frame.
 */
static void
app_overlay_build_npc_headicon(
    struct App* app,
    int element_id,
    struct ToriRS_Npctype const* npctype,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int prayer_scene,
    int prayer_group)
{
    int height;
    int screen_x, screen_y;

    if( !npctype || npctype->head_icon_index < 0 || prayer_scene <= 0 )
        return;
    if( npctype->head_icon_group >= 0 && npctype->head_icon_group != prayer_group )
        return;
    height = app_entity_model_height(app, element_id);
    if( !app_world_project_actor(
            app,
            placement,
            -1,
            (int)draw_position->x,
            (int)draw_position->z,
            height + 15,
            &screen_x,
            &screen_y) )
        return;

    {
        struct UITreeEntityOverlay spr = {
            .kind = UITREE_ENTITY_OVERLAY_SPRITE,
            .x = screen_x - 12,
            .y = screen_y - 30,
            .w = 0,
            .h = 0,
            .scene_id = prayer_scene,
            .atlas_index = npctype->head_icon_index,
        };
        app_overlay_push(app, &spr);
    }
}

/* Push one projected world segment as a LINE overlay (box + diagonal). */
static void
app_overlay_push_segment(
    struct App* app,
    int screen_x0,
    int screen_y0,
    int screen_x1,
    int screen_y1,
    uint32_t color)
{
    struct UITreeEntityOverlay seg = {
        .kind = UITREE_ENTITY_OVERLAY_LINE,
        .x = screen_x0 < screen_x1 ? screen_x0 : screen_x1,
        .y = screen_y0 < screen_y1 ? screen_y0 : screen_y1,
        .w = screen_x0 < screen_x1 ? screen_x1 - screen_x0 : screen_x0 - screen_x1,
        .h = screen_y0 < screen_y1 ? screen_y1 - screen_y0 : screen_y0 - screen_y1,
        .color = color,
        .line_width = 2,
        /* Direction 0 = TL->BR. The segment runs that diagonal when x and y
         * grow together; otherwise it is the other one. */
        .line_direction = ((screen_x0 < screen_x1) != (screen_y0 < screen_y1)) ? 1 : 0,
    };
    app_overlay_push(app, &seg);
}

/*
 * TORIRS_HOVER_FOOTPRINT=1: outline the hovered loc's footprint tiles in red.
 *
 * The painter orders scenery by its FOOTPRINT (size_x x size_z from the loc
 * config, orientation-swapped), while the model draws wherever its vertices
 * land — and nothing on screen says which tiles the painter believed the loc
 * covered. When a model overhangs its footprint, terrain on the overhung
 * tiles legitimately draws later and paints over it, which reads as "the
 * painter is broken" while every ordering rule is being honoured. This makes
 * the footprint visible so model-vs-footprint mismatches are a hover, not an
 * afternoon (the multiloc trap of loc-placement-debug fame).
 *
 * Each footprint tile is outlined at terrain height through the same
 * projector the health bars use, so the outline hugs the contour.
 */
/**
 * Emit a convex polygon as a closed outline.
 *
 * The overlay's own primitives are boxes and box-diagonals, so a polygon is
 * expanded here into one LINE per edge rather than reaching the draw layer as a
 * single command. That keeps the emit walk's one-item-one-command stepping
 * intact — a real multi-segment render command would need a sub-step counter
 * threaded through every backend, which is worth doing only when a highlight
 * needs to be FILLED rather than outlined.
 *
 * Degenerate hulls are drawn as what they are: two points are a single
 * segment (a footprint seen edge-on), and one point draws nothing rather than a
 * zero-length line the rasteriser would have to special-case.
 */
/**
 * Emit a convex polygon as a FILL: a begin / point... / end run.
 *
 * The run is three kinds of overlay item rather than one item holding an array
 * so that each still maps to exactly one render command — the emit walk is one
 * command per step, and bracketing is what lets a variable-length primitive
 * through it without a sub-step counter in the walk and in all four backends.
 *
 * @param trans 0 opaque .. 255 invisible. A highlight is a wash over the model
 *        it marks, so an opaque fill would hide the thing being highlighted.
 */
static void
app_overlay_push_polygon_filled(
    struct App* app,
    const int* points_x,
    const int* points_y,
    int point_count,
    uint32_t color,
    int trans)
{
    struct UITreeEntityOverlay item;

    assert(app);
    assert(points_x);
    assert(points_y);

    /* Under three points there is no area to fill. The caller still draws the
     * outline, so a hull seen edge-on degrades to a line rather than vanishing. */
    if( point_count < 3 )
        return;

    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_POLY_BEGIN;
    item.color = color;
    item.trans = trans;
    app_overlay_push(app, &item);

    for( int i = 0; i < point_count; i++ )
    {
        memset(&item, 0, sizeof(item));
        item.kind = UITREE_ENTITY_OVERLAY_POLY_POINT;
        item.x = points_x[i];
        item.y = points_y[i];
        app_overlay_push(app, &item);
    }

    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_POLY_END;
    app_overlay_push(app, &item);
}

static void
app_overlay_push_polygon(
    struct App* app,
    const int* points_x,
    const int* points_y,
    int point_count,
    uint32_t color)
{
    assert(app);
    assert(points_x);
    assert(points_y);
    assert(point_count >= 0);

    if( point_count < 2 )
        return;

    if( point_count == 2 )
    {
        app_overlay_push_segment(app, points_x[0], points_y[0], points_x[1], points_y[1], color);
        return;
    }

    for( int i = 0; i < point_count; i++ )
    {
        int const next = (i + 1) % point_count;
        app_overlay_push_segment(
            app, points_x[i], points_y[i], points_x[next], points_y[next], color);
    }
}

/**
 * Outline the MODEL of a scene element: a silhouette that wraps the thing in
 * three dimensions, not a quad on the ground under it.
 *
 * Renderer-independent by construction, which is the constraint that shapes it.
 * The projection is the app's own integer camera transform and the output is
 * the LINE primitives the overlay pass already carries, so soft3d, gl3 and
 * gl3zb all draw this without knowing it exists. Anything that reached into a
 * renderer — a stencil pass, an edge filter over the depth buffer, a shader —
 * would have to be written three times and would not exist at all in the
 * software rasteriser.
 *
 * The shape projected is the model's bounds CYLINDER as an eight-corner box:
 * `radius` either way in x and z, `min_y`..`max_y` vertically. The cylinder is
 * what the renderer itself culls and sorts against, so an outline drawn from it
 * agrees with what is on screen; and because a cylinder has no orientation in
 * xz, this needs no yaw and is correct for a loc at any angle and for an npc
 * mid-turn.
 *
 * Hulling eight corners rather than the mesh's vertices is a deliberate stop:
 * it is one outline that always wraps the model, at fixed cost per entity per
 * frame. Hugging the mesh exactly means projecting every vertex, which is the
 * same code with a bigger input — see ToriDraw_ConvexHullScratch, which exists
 * for that and has no point cap.
 *
 * @param fill_trans 0 opaque .. 255 invisible, or -1 for no fill at all. The
 *        hover and editor marks pass APP_OUTLINE_FILL_TRANS; a plugin picks
 *        its own, because a highlight over a crowd of npcs wants to be lighter
 *        than one over a single latched selection -- or absent entirely.
 * @return 1 when an outline was emitted.
 */
static int
app_overlay_outline_element_model_trans(
    struct App* app,
    int element_id,
    uint32_t color,
    int fill_trans)
{
    struct ToriDraw_SceneElement* element;
    struct ToriDraw_BoundsCylinder* bounds;
    int px[8];
    int py[8];
    int hull_x[8];
    int hull_y[8];
    int count = 0;
    int hull_size;
    int ox;
    int oy;
    int oz;
    int radius;

    assert(app);

    if( !app->scene || element_id < 0 )
        return 0;
    if( !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return 0;

    element = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !element )
        return 0;

    bounds = ToriDraw_ModelGetBoundsCylinder(element->model);
    /* No bounds is not a failure: a handle that is not a full model (a sprite
     * billboard, an empty slot) has none, and there is nothing to outline. */
    if( !bounds )
        return 0;

    ox = element->world_position.x;
    oy = element->world_position.y;
    oz = element->world_position.z;
    radius = bounds->radius;
    if( radius <= 0 )
        return 0;

    for( int corner = 0; corner < 8; corner++ )
    {
        /* Bit 0 = east, bit 1 = south, bit 2 = the model's top edge. `min_y` is
         * the TOP in scene space, where y grows downward. */
        int const wx = ox + ((corner & 1) ? radius : -radius);
        int const wz = oz + ((corner & 2) ? radius : -radius);
        int const wy = oy + ((corner & 4) ? bounds->min_y : bounds->max_y);
        int screen_x;
        int screen_y;

        if( !app_world_project_at(app, wx, wz, wy, &screen_x, &screen_y) )
            continue;
        px[count] = screen_x;
        py[count] = screen_y;
        count++;
    }

    if( count < 2 )
        return 0;

    hull_size = ToriDraw_ConvexHull(px, py, count, hull_x, hull_y);
    /* Fill first, outline over it: the wash says "this one" at a glance and the
     * outline gives it a definite edge, which a translucent fill alone does not
     * have against busy ground. */
    if( fill_trans >= 0 )
        app_overlay_push_polygon_filled(app, hull_x, hull_y, hull_size, color, fill_trans);
    app_overlay_push_polygon(app, hull_x, hull_y, hull_size, color);
    return 1;
}

/**
 * Directions sampled around a projected mesh when reducing it to a hull.
 *
 * The reduction is what makes a mesh outline affordable. The exact hull of a
 * few thousand screen points costs an angular sort over all of them; the
 * extreme point along a FIXED direction is one multiply-add and one compare
 * per vertex. Every such extreme is a vertex of the true hull, so the polygon
 * built from them is inscribed in it — tighter than the real silhouette by at
 * most the sagitta of a 360/(2*N) degree arc, never looser — and it is capped
 * at 2*N points, which is what keeps a highlight's cost to the overlay budget
 * bounded no matter how detailed the model is.
 *
 * 16 directions is an 11.25 degree gap between samples: under half a percent
 * of the silhouette's radius, which is sub-pixel on anything short of a boss
 * filling the viewport.
 */
#define APP_OUTLINE_HULL_MESH_DIRECTIONS 16

/**
 * Outline the MESH of a scene element: the model's own posed vertices, rather
 * than the box that contains them.
 *
 * The bounds outline above is the cylinder — `radius` in every horizontal
 * direction — so an npc is wrapped at the radius of whatever sticks out
 * furthest: a halberd, a cape, a wing. That reads on screen as a square around
 * every npc regardless of its shape, which is exactly what this is for. Here
 * the geometry that is actually drawn is what gets hulled, so a thin thing
 * outlines thin and a turning thing narrows as it turns.
 *
 * The vertices read are the LIVE ones (`vertices_*`, never
 * `original_vertices_*`): the animation frame, the post-transform placement
 * and any merged spot graphic are already applied to them, which is what keeps
 * the outline on the pose being rendered instead of the bind pose.
 *
 * Placement is re-derived here the way the projector derives it — roll, then
 * pitch, then yaw about the model's own origin, then the element's world
 * position — because a model's vertices are stored in its own frame and only
 * the projector has ever combined them with the element's angles.
 *
 * Cost is one projection per vertex per frame against the bounds outline's
 * eight, which is why the shape is the caller's choice and not the only mode.
 *
 * @param fill_trans 0 opaque .. 255 invisible, or -1 for no fill at all.
 * @return 1 when an outline was emitted.
 */
static int
app_overlay_outline_element_mesh_trans(
    struct App* app,
    int element_id,
    uint32_t color,
    int fill_trans)
{
    enum
    {
        DIRECTIONS = APP_OUTLINE_HULL_MESH_DIRECTIONS,
        CANDIDATES = DIRECTIONS * 2
    };
    struct ToriDraw_SceneElement* element;
    vertexint_t const* vertices_x;
    vertexint_t const* vertices_y;
    vertexint_t const* vertices_z;
    int vertex_count;
    int sin_dir[DIRECTIONS];
    int cos_dir[DIRECTIONS];
    long long extreme[CANDIDATES];
    int extreme_x[CANDIDATES];
    int extreme_y[CANDIDATES];
    int extreme_seen[CANDIDATES];
    int px[CANDIDATES];
    int py[CANDIDATES];
    int hull_x[CANDIDATES];
    int hull_y[CANDIDATES];
    int count = 0;
    int hull_size;
    int ox;
    int oy;
    int oz;
    int yaw;
    int pitch;
    int roll;
    int sin_yaw;
    int cos_yaw;
    int sin_pitch;
    int cos_pitch;
    int sin_roll;
    int cos_roll;

    assert(app);

    if( !app->scene || element_id < 0 )
        return 0;
    if( !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return 0;

    element = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !element )
        return 0;

    vertex_count = ToriDraw_ModelGetVertexCount(element->model);
    vertices_x = ToriDraw_ModelGetVerticesX(element->model);
    vertices_y = ToriDraw_ModelGetVerticesY(element->model);
    vertices_z = ToriDraw_ModelGetVerticesZ(element->model);
    /* No mesh is not a failure, for the same reason no bounds cylinder is not:
     * a handle that is not a full model (a sprite billboard, an empty slot)
     * has no vertices and there is nothing to outline. */
    if( vertex_count <= 0 || !vertices_x || !vertices_y || !vertices_z )
        return 0;

    for( int d = 0; d < DIRECTIONS; d++ )
    {
        /* Half a turn of directions, not a whole one: the minimum along a
         * direction IS the maximum along its opposite, so the other half would
         * ask every vertex the same question a second time. */
        int const angle = d * (2048 / (DIRECTIONS * 2));
        sin_dir[d] = ToriDraw_Sin(angle);
        cos_dir[d] = ToriDraw_Cos(angle);
        extreme_seen[d * 2] = 0;
        extreme_seen[d * 2 + 1] = 0;
    }

    ox = element->world_position.x;
    oy = element->world_position.y;
    oz = element->world_position.z;
    yaw = element->world_position.yaw;
    pitch = element->world_position.pitch;
    roll = element->world_position.roll;
    sin_yaw = ToriDraw_Sin(yaw);
    cos_yaw = ToriDraw_Cos(yaw);
    sin_pitch = ToriDraw_Sin(pitch);
    cos_pitch = ToriDraw_Cos(pitch);
    sin_roll = ToriDraw_Sin(roll);
    cos_roll = ToriDraw_Cos(roll);

    for( int v = 0; v < vertex_count; v++ )
    {
        /* 64-bit intermediates. A vertex coordinate is a signed 16-bit
         * quantity and the trig tables are 16.16, so one product alone reaches
         * 2^31 and the sum of two passes it -- the same shape the projection
         * kernels carry, but they are fed a model that has already been culled
         * against the scene's capacity while this runs on whatever the
         * element holds. The >>16 result is identical wherever int would not
         * have overflowed. */
        long long vx = vertices_x[v];
        long long vy = vertices_y[v];
        long long vz = vertices_z[v];
        int screen_x;
        int screen_y;
        long long tmp;

        /* graphics/projection.u.c project_orthographic order: roll (Z), pitch
         * (X), yaw (Y). Any other order puts the outline somewhere the model
         * is not the moment two of the three are non-zero. */
        if( roll != 0 )
        {
            tmp = (vy * sin_roll + vx * cos_roll) >> 16;
            vy = (vy * cos_roll - vx * sin_roll) >> 16;
            vx = tmp;
        }
        if( pitch != 0 )
        {
            tmp = (vy * cos_pitch - vz * sin_pitch) >> 16;
            vz = (vy * sin_pitch + vz * cos_pitch) >> 16;
            vy = tmp;
        }
        if( yaw != 0 )
        {
            tmp = (vz * sin_yaw + vx * cos_yaw) >> 16;
            vz = (vz * cos_yaw - vx * sin_yaw) >> 16;
            vx = tmp;
        }

        /* A vertex behind the near plane is dropped rather than clamped: the
         * hull of what IS on screen is a smaller mark, while a clamped one is
         * a wrong mark. */
        if( !app_world_project_at(
                app, ox + (int)vx, oz + (int)vz, oy + (int)vy, &screen_x, &screen_y) )
            continue;

        for( int d = 0; d < DIRECTIONS; d++ )
        {
            /* 64-bit: screen coordinates run to six figures once a model is
             * close to the camera, and a 16.16 direction multiplies that past
             * 2^32. A wrapped dot product picks the wrong vertex and the
             * outline folds through itself. */
            long long const dot =
                (long long)screen_x * cos_dir[d] + (long long)screen_y * sin_dir[d];
            int const hi = d * 2;
            int const lo = d * 2 + 1;

            if( !extreme_seen[hi] || dot > extreme[hi] )
            {
                extreme_seen[hi] = 1;
                extreme[hi] = dot;
                extreme_x[hi] = screen_x;
                extreme_y[hi] = screen_y;
            }
            if( !extreme_seen[lo] || dot < extreme[lo] )
            {
                extreme_seen[lo] = 1;
                extreme[lo] = dot;
                extreme_x[lo] = screen_x;
                extreme_y[lo] = screen_y;
            }
        }
    }

    /* Distinct points only. One vertex is the extreme in many directions at
     * once — on a small model, in nearly all of them — and repeated points
     * make the scan's collinear tie-break decide a turn between two copies of
     * the same coordinate. */
    for( int i = 0; i < CANDIDATES; i++ )
    {
        int duplicate = 0;

        if( !extreme_seen[i] )
            continue;
        for( int j = 0; j < count; j++ )
        {
            if( px[j] == extreme_x[i] && py[j] == extreme_y[i] )
            {
                duplicate = 1;
                break;
            }
        }
        if( duplicate )
            continue;
        px[count] = extreme_x[i];
        py[count] = extreme_y[i];
        count++;
    }

    if( count < 2 )
        return 0;

    hull_size = ToriDraw_ConvexHull(px, py, count, hull_x, hull_y);
    if( fill_trans >= 0 )
        app_overlay_push_polygon_filled(app, hull_x, hull_y, hull_size, color, fill_trans);
    app_overlay_push_polygon(app, hull_x, hull_y, hull_size, color);
    return 1;
}

/* The mark the hover footprint and the editor selection both draw: the
 * silhouette with the standard wash under it. */
static int
app_overlay_outline_element_model(
    struct App* app,
    int element_id,
    uint32_t color)
{
    return app_overlay_outline_element_model_trans(app, element_id, color, APP_OUTLINE_FILL_TRANS);
}

static void
app_overlay_outline_scenery(
    struct App* app,
    struct WorldEntity_Scenery const* scenery)
{
    int base_x = scenery->grid_position.x;
    int base_z = scenery->grid_position.z;
    int size_x = scenery->debug.draw_size_x > 0 ? scenery->debug.draw_size_x : 1;
    int size_z = scenery->debug.draw_size_z > 0 ? scenery->debug.draw_size_z : 1;
    int plane_y;

    /* One flat plane at the SW corner's ground height, the height the loc was
     * placed against — not per-corner terrain samples. Sampling each corner's
     * own column bends the outline over every slope and, on the raised ground
     * an overhung footprint reaches into, floats it clear of the loc it is
     * meant to describe. */
    plane_y = app_world_height(app, base_x * 128, base_z * 128, scenery->grid_position.level);

    /*
     * The SILHOUETTE of the footprint, not a box per tile.
     *
     * Outlining each tile separately draws every internal edge — a 3x3 loc came
     * out as nine overlapping quads, which reads as a grid laid over the loc
     * rather than as the loc being highlighted. Hulling the projected corners
     * collapses that to the one closed outline the eye is looking for, and it
     * costs less to draw: four segments instead of thirty-six.
     *
     * The corners are projected first and hulled in SCREEN space, not hulled on
     * the ground and then projected. A footprint is convex on the ground, but
     * "convex after projection" is what makes the outline enclose the pixels,
     * and the two only agree for an axis-aligned camera.
     */
    {
        /* Corner order SW, SE, NE, NW; fine coords are tile * 128. */
        static const int corner[4][2] = {
            { 0, 0 },
            { 1, 0 },
            { 1, 1 },
            { 0, 1 }
        };
        int px[TORIDRAW_CONVEX_HULL_MAX_POINTS];
        int py[TORIDRAW_CONVEX_HULL_MAX_POINTS];
        int hull_x[TORIDRAW_CONVEX_HULL_MAX_POINTS];
        int hull_y[TORIDRAW_CONVEX_HULL_MAX_POINTS];
        int count = 0;
        int hull_size;

        for( int tz = base_z; tz < base_z + size_z; tz++ )
        {
            for( int tx = base_x; tx < base_x + size_x; tx++ )
            {
                for( int c = 0; c < 4; c++ )
                {
                    int screen_x;
                    int screen_y;

                    if( count >= TORIDRAW_CONVEX_HULL_MAX_POINTS )
                        break;
                    /* A corner behind the camera projects to nothing usable, so
                     * it is dropped rather than clamped: the hull of what IS in
                     * front is still the right outline for the visible part,
                     * where a clamped point would drag an edge across the
                     * screen. */
                    if( !app_world_project_at(
                            app,
                            (tx + corner[c][0]) * 128,
                            (tz + corner[c][1]) * 128,
                            plane_y,
                            &screen_x,
                            &screen_y) )
                        continue;
                    px[count] = screen_x;
                    py[count] = screen_y;
                    count++;
                }
            }
        }

        if( count == 0 )
            return;

        hull_size = ToriDraw_ConvexHull(px, py, count, hull_x, hull_y);
        /* Why an outline looks wrong, in one line: too few corners means the
         * projection dropped some (behind the camera), and hull < corners is
         * the interior points being discarded, which is the point. */
        if( getenv("TORIRS_HULL_DEBUG") )
            TORIRS_LOG(
                "hull: loc %d footprint %dx%d corners=%d hull=%d\n",
                scenery->loc_id,
                size_x,
                size_z,
                count,
                hull_size);
        app_overlay_push_polygon(app, hull_x, hull_y, hull_size, APP_OUTLINE_COLOR_FOOTPRINT);
    }
}

static void
app_overlay_build_hover_footprint(struct App* app)
{
    /* 0 = off; 1 = the hovered loc; >1 = every instance of that LOC ID.
     * The id form exists for headless runs: TORIRS_SIM_HOVER parks the mouse
     * before the frame loop, so an exit screenshot has no hover to read.
     *
     * The live mode is App state rather than a static resolved once, because
     * the hover_footprint hotkey turns it on and off during a session. The env
     * var still chooses WHICH mode, and app_hover_footprint_toggle restores it
     * — see App::hover_footprint_mode. */
    int mode = app->hover_footprint;

    if( !mode || !app->world )
        return;

    if( mode == 1 )
    {
        /*
         * The pickset is this frame's under-mouse set, nearest hits first (the
         * same order the minimenu consumes), so the first loc or npc in it is
         * the one the cursor is actually on.
         *
         * Npcs are outlined as well as locs because an editor is placing both
         * against each other, and "what am I about to act on" is the same
         * question for either. The model outline is the same call for both —
         * they are both scene elements — which is why this does not need to
         * know what kind of entity it found beyond where to read the id.
         */
        struct World_Picked const* hit = NULL;
        for( int i = 0; i < app->world_pickset.count && !hit; i++ )
        {
            enum World_PickType const type = app->world_pickset.items[i].type;
            if( type == WORLD_PICK_SCENERY || type == WORLD_PICK_NPC )
                hit = &app->world_pickset.items[i];
        }
        if( !hit )
            return;

        /* Model silhouette first. It is the outline that reads as "this thing
         * is selected"; the ground footprint below says which TILES it owns,
         * which is what an editor needs when placing something beside it. */
        app_overlay_outline_element_model(app, hit->element_id, APP_OUTLINE_COLOR_HOVER);

        if( hit->type == WORLD_PICK_SCENERY )
        {
            struct WorldEntity_Scenery* scenery =
                World_SceneryGetByElementId(app->world, hit->element_id);
            if( scenery )
                app_overlay_outline_scenery(app, scenery);
        }
        return;
    }

    struct World_EntityPool* pool = &app->world->entities.scenery;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery* scenery = World_EntityPoolGet(pool, i);
        if( scenery && scenery->loc_id == mode )
        {
            /* Both marks, the same pair the hover path draws. The two modes
             * showing different things would make the by-id form useless for
             * checking the hover form — which is what it is for, since a
             * headless run has no cursor to hover with. */
            app_overlay_outline_element_model(app, scenery->element_id, APP_OUTLINE_COLOR_HOVER);
            app_overlay_outline_scenery(app, scenery);
        }
    }
}

/**
 * The map editor SELECT tool's latch (editor_panel.sel_kind) -- distinct from
 * the hover footprint above: hover follows the mouse every frame, this stays
 * on what was latched even after the cursor moves off it, matching what
 * panel_refresh (editor_panel.c) is reading for the readout at the same time.
 */
static void
app_overlay_build_editor_selection(struct App* app)
{
    struct Editor_Panel const* panel = &app->editor_panel;

    /* Whatever tool is active: the selection is tool-independent (pick with
     * Select, then rotate/move/reshape with the others), so its highlight
     * must not vanish the moment the tool that will act on it is chosen --
     * the Move tool with an invisible subject is aiming blind. The old
     * SELECT-only gate predates select-then-operate. */
    if( !panel->visible )
        return;

    /* The Place-loc ghost's FOOTPRINT: the translucent model says what it
     * looks like, this says which tiles it will own -- the question that
     * decides whether it fits beside the wall. Same silhouette the hover
     * footprint draws, fed by the ghost's own scenery entity, so a 3x2 loc
     * shows 3x2 here without anything re-deriving sizes. Before the
     * selection early-out: a ghost exists with or without a selection. */
    if( app->ghost_active && app->world )
    {
        int const idx = World_SceneryFindAt(
            app->world, app->ghost_x, app->ghost_z, app->ghost_level, app->ghost_shape);
        if( idx >= 0 )
        {
            struct WorldEntity_Scenery* ghost =
                World_EntityPoolGet(&app->world->entities.scenery, idx);
            if( ghost )
                app_overlay_outline_scenery(app, ghost);
        }
    }

    if( panel->sel_kind == EDITOR_SELECTION_NONE || !app->world )
        return;

    if( panel->sel_kind == EDITOR_SELECTION_LOC )
    {
        struct WorldEntity_Scenery* scenery =
            World_SceneryGetByElementId(app->world, panel->sel_element_id);

        /* A reshape/swap deleted the element this selection pointed at (a loc
         * change is delete + add, and the add is async). Re-find the NEW
         * element by tile and shape, and heal the selection -- transiently
         * absent while the add is still in flight, which draws no highlight
         * for a frame or two rather than the wrong one forever. */
        if( !scenery )
        {
            int const idx = World_SceneryFindAt(
                app->world,
                panel->sel_scene_x,
                panel->sel_scene_z,
                panel->sel_level,
                panel->sel_shape);
            if( idx >= 0 )
                scenery = World_EntityPoolGet(&app->world->entities.scenery, idx);
            if( scenery )
                app->editor_panel.sel_element_id = scenery->element_id;
        }
        if( !scenery )
            return;
        app_overlay_outline_element_model(
            app, panel->sel_element_id, APP_OUTLINE_COLOR_EDITOR_SELECT);
        app_overlay_outline_scenery(app, scenery);
        return;
    }

    /* Terrain: the same single-plane, hulled-corners outline
     * app_overlay_outline_scenery draws for a loc's footprint, for the one
     * latched tile -- there is no WorldEntity_Scenery here to read a size
     * from, so the four corners are built directly instead of looped per
     * tile. */
    {
        int const base_x = panel->sel_scene_x;
        int const base_z = panel->sel_scene_z;
        static int const corner[4][2] = {
            { 0, 0 },
            { 1, 0 },
            { 1, 1 },
            { 0, 1 }
        };
        int px[4];
        int py[4];
        int hull_x[4];
        int hull_y[4];
        int count = 0;
        int hull_size;
        int const plane_y = app_world_height(app, base_x * 128, base_z * 128, panel->sel_level);

        for( int c = 0; c < 4; c++ )
        {
            int screen_x;
            int screen_y;

            if( !app_world_project_at(
                    app,
                    (base_x + corner[c][0]) * 128,
                    (base_z + corner[c][1]) * 128,
                    plane_y,
                    &screen_x,
                    &screen_y) )
                continue;
            px[count] = screen_x;
            py[count] = screen_y;
            count++;
        }
        if( count == 0 )
            return;

        hull_size = ToriDraw_ConvexHull(px, py, count, hull_x, hull_y);
        app_overlay_push_polygon_filled(
            app,
            hull_x,
            hull_y,
            hull_size,
            APP_OUTLINE_COLOR_EDITOR_SELECT,
            APP_OUTLINE_FILL_TRANS);
        app_overlay_push_polygon(app, hull_x, hull_y, hull_size, APP_OUTLINE_COLOR_EDITOR_SELECT);
    }
}

/* The plugin host's view of the engine: snapshots, projection, drawing and
 * menu rows, all written against the static helpers above. Included here
 * rather than compiled separately so those helpers stay static -- the same
 * arrangement world_builder.c uses for world_terrain.u.c. */
/* Defined far below, beside the other chrome plumbing; the plugin window's
 * tick calls it for its own chrome instance and is included here. */
static void
app_chrome_route_input(
    struct App* app,
    struct ToriRSChrome* ui,
    struct LibToriRS_Input* input);

/* Its keyboard half alone -- what a WEB/BROWSER DOM editor needs. */
static void
app_chrome_route_keys(
    struct App* app,
    struct ToriRSChrome* ui,
    struct LibToriRS_Input* input);

/* Scene font id for the minimenu (reference uses bold-12; dat2 fonts-table
 * archive 496 in this cache era, e.g. bank title font). Dat1 has no fonts
 * table: its fonts live in the title jagfile and are pinned at cache-font
 * slots 0-3 by RevConfig, where b12 is slot 2. Falls back to any text node's
 * already-resolved scene font when b12 cannot load. Declared above the plugin
 * includes because the plugin window's CS2 presentation sets its rows in the
 * same p12 the interfaces use. */
/*
 * The three fonts the client draws with directly, named the way the RevConfig
 * profile names them. `app_font_cache_id` turns one of these into the number
 * this cache uses — a dat1 scene slot or a dat2 fonts-table archive id, which
 * are different numbers for the same font.
 *
 * Minimenu and hovertext use bold-12; hitsplat numbers use p11 (reference
 * `this.p11.centreString`); the rebuild overlay uses p12 (Client-TS
 * `p12.centreString`).
 */
#define APP_FONT_B12 "b12"
#define APP_FONT_P11 "p11"
#define APP_FONT_P12 "p12"

/* Defined below; declared here because the plugin panel included beneath this
 * point sets its rows in the game's own p12 and mounts into a cache interface
 * the profile names. */
static int
app_font_cache_id(
    struct App const* app,
    char const* font_name);

static int
app_iface_com(
    struct App const* app,
    char const* iface_name,
    int child);

/*
 * Forward-declared for the bridge below, which is included here and needs the
 * dispatcher that lives 18,000 lines further down: api->if_click presses a
 * button by running the row a real click would have built, and the row is run
 * by exactly one function in this file.
 */
static int
app_minimenu_run_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y);

static void
app_minimenu_stamp_node_identities(
    struct App const* app,
    struct UIMinimenu* menu);

#include "plugin/torirs_plugin_bridge.u.c"
#include "plugin/torirs_plugin_panel.u.c"

/**
 * Pixel size of a sprite already resident in the scene.
 *
 * The scene is the only place a decoded sprite's dimensions exist -- the
 * healthbar config names an id, not a size -- and this runs inside the
 * per-frame overlay build, where there is nowhere to yield to a load. The boot
 * preload in task_dat2_healthbar_load.c is what makes the answer available;
 * false here means it is not, and the caller falls back to the declared width.
 */
static bool
app_scene_sprite_size(
    struct App* app,
    int scene_id,
    int* out_w,
    int* out_h)
{
    struct ToriDraw_Sprite** sprites;
    int count = 0;

    assert(app);
    assert(out_w);
    assert(out_h);
    if( scene_id <= 0 )
        return false;
    sprites = ToriDraw_SceneSpriteGet(app->scene, scene_id, &count);
    if( !sprites || count <= 0 || !sprites[0] )
        return false;
    *out_w = sprites[0]->width;
    *out_h = sprites[0]->height;
    return true;
}

/*
 * The overhead health bar, as the rev-239 client draws it.
 *
 * Three things the old two-rectangle version got wrong, all of them the same
 * mistake -- treating the server's fill byte as a pixel count:
 *
 *   - The bar's span is the FRONT SPRITE's width, minus the type's padding at
 *     both ends. It runs 30..160 across cache.osrs239's 85 records. Only a
 *     type naming no sprites falls back to `width` pixels.
 *   - `width` (opcode 14) is the denominator the fill arrives as a fraction of,
 *     which is a different number from the span for `healthbar_8` and equal to
 *     it for the other 84.
 *   - A block carries a start fill, an end fill and a duration; the bar
 *     travels between them and then fades, rather than snapping to one value.
 *
 * Reference: the health-bar block of drawEntities, class381 and class66.
 */
static void
app_overlay_build_healthbar(
    struct App* app,
    struct WorldEntityFacet_Combat const* combat,
    int screen_x,
    int screen_y)
{
    struct RS_HealthbarType const* type =
        RS_Healthbars_TypeFor(&app->healthbars, combat->healthbar_type);
    int cycle = app->world->cycle;
    /* -1 is the common "this type has no sprites" state, and EnsureSprite
     * answers -1 for it as well -- so the pair is resolved unconditionally and
     * only the resulting scene ids are tested. */
    int front_scene = UITreeSceneBridge_EnsureSprite(&app->bridge, type->front_sprite);
    int back_scene = UITreeSceneBridge_EnsureSprite(&app->bridge, type->back_sprite);
    int front_w = 0;
    int front_h = 0;
    int back_w = 0;
    int back_h = 0;
    bool sprites = app_scene_sprite_size(app, front_scene, &front_w, &front_h) &&
                   app_scene_sprite_size(app, back_scene, &back_w, &back_h);
    int padding = 0;
    int span;
    int elapsed = cycle - combat->healthbar_start_cycle;
    int end_span;
    int drawn;
    int alpha = 255;
    int bar_x;

    /* Denominator, so a type that somehow declares 0 would divide by zero. The
     * reference has no such guard because its constructor cannot produce one;
     * ours reads a cache, so it can. */
    assert(type->width > 0);

    if( sprites )
    {
        /* The reference only accepts the padding when it fits inside the
         * sprite -- an oversized one would invert the span. */
        if( type->padding < front_w )
            padding = type->padding;
        span = front_w - padding * 2;
    }
    else
    {
        span = type->width;
    }

    end_span = combat->healthbar_end_fill * span / type->width;
    if( combat->healthbar_duration > elapsed )
    {
        int start_span = combat->healthbar_start_fill * span / type->width;
        drawn = (end_span - start_span) * elapsed / combat->healthbar_duration + start_span;
    }
    else
    {
        drawn = end_span;
        /* Past the travel, the bar fades over the tail of its persist window.
         * -1 (the constructor default, and what most records keep) never
         * fades, which is why this is not a plain subtraction. */
        if( type->fade_threshold >= 0 && type->persist_cycles > type->fade_threshold )
        {
            int remaining = combat->healthbar_duration + type->persist_cycles - elapsed;
            alpha = (remaining << 8) / (type->persist_cycles - type->fade_threshold);
        }
    }
    /* A living entity never renders as empty: any non-zero fill keeps a pixel. */
    if( combat->healthbar_end_fill > 0 && drawn < 1 )
        drawn = 1;
    if( drawn > span )
        drawn = span;
    if( drawn < 0 )
        drawn = 0;

    bar_x = screen_x - (span >> 1);

    if( !sprites )
    {
        struct UITreeEntityOverlay bar = {
            .kind = UITREE_ENTITY_OVERLAY_RECT,
            .x = bar_x,
            .y = screen_y - 3,
            .w = drawn,
            .h = RS_HEALTHBAR_FALLBACK_HEIGHT,
            .color = 0xFF00FF00u, /* Colour.GREEN */
        };
        app_overlay_push(app, &bar);
        bar.x = bar_x + drawn;
        bar.w = span - drawn;
        bar.color = 0xFFFF0000u; /* Colour.RED */
        app_overlay_push(app, &bar);
        return;
    }

    {
        /* Both halves are blitted at the same origin; the filled one is cut
         * off at the current fill. The full bar gets both paddings back
         * because its right edge is the sprite's own, not a cut. */
        int clip_w = (drawn == span) ? padding * 2 + drawn : padding + drawn;
        int x = bar_x - padding;
        /* Centred on the same row the rectangle path uses, so switching
         * between the two does not move the bar. */
        int y = screen_y - back_h / 2;
        int trans = (alpha >= 0 && alpha < 255) ? 255 - alpha : 0;
        struct UITreeEntityOverlay back = {
            .kind = UITREE_ENTITY_OVERLAY_SPRITE,
            .x = x,
            .y = y,
            .scene_id = back_scene,
            .trans = trans,
        };
        struct UITreeEntityOverlay front = {
            .kind = UITREE_ENTITY_OVERLAY_SPRITE,
            .x = x,
            .y = y,
            .scene_id = front_scene,
            .trans = trans,
            .clip_x = x,
            .clip_y = y,
            .clip_w = clip_w,
            .clip_h = front_h,
        };

        app_overlay_push(app, &back);
        if( clip_w > 0 )
            app_overlay_push(app, &front);
    }
}

static void
app_overlay_build_entity(
    struct App* app,
    int element_id,
    struct WorldEntityFacet_Combat const* combat,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int font_id,
    int hitmarks_scene,
    int type_height)
{
    int cycle = app->world->cycle;
    int height = app_entity_overlay_height(app, element_id, type_height);
    int screen_x, screen_y;

    /*
     * Health bar, 15px above the model top. Two sources, and they are not
     * alternatives so much as two eras:
     *
     *   - A HEADBAR block (dat2/OldSchool) names a healthbar type and carries
     *     fills relative to it. Everything about how it draws is the type's.
     *   - A legacy dat1 hitsplat block carries raw hitpoints and no type at
     *     all, so it keeps the client's own 30-wide rectangle and the
     *     `combatCycle > loopCycle + 100` window (combat_cycle is set to
     *     loopCycle + 400 on every hit -- the same 300 cycles the standard
     *     healthbar type spells as its persist window).
     */
    if( combat->healthbar_type >= 0 && combat->healthbar_end_cycle > cycle &&
        app_world_project_actor(
            app,
            placement,
            actor_level,
            (int)draw_position->x,
            (int)draw_position->z,
            height + 15,
            &screen_x,
            &screen_y) )
    {
        app_overlay_build_healthbar(app, combat, screen_x, screen_y);
    }
    else if(
        combat->healthbar_type < 0 && combat->combat_cycle > cycle + 100 &&
        combat->total_health > 0 &&
        app_world_project_actor(
            app,
            placement,
            actor_level,
            (int)draw_position->x,
            (int)draw_position->z,
            height + 15,
            &screen_x,
            &screen_y) )
    {
        int bar_width = RS_HEALTHBAR_DEFAULT_WIDTH;
        int filled = (combat->health * bar_width) / combat->total_health;
        if( filled > bar_width )
            filled = bar_width;
        if( filled < 0 )
            filled = 0;
        struct UITreeEntityOverlay bar = {
            .kind = UITREE_ENTITY_OVERLAY_RECT,
            .x = screen_x - (bar_width >> 1),
            .y = screen_y - 3,
            .w = filled,
            .h = RS_HEALTHBAR_FALLBACK_HEIGHT,
            .color = 0xFF00FF00u, /* Colour.GREEN */
        };
        app_overlay_push(app, &bar);
        bar.x = screen_x - (bar_width >> 1) + filled;
        bar.w = bar_width - filled;
        bar.color = 0xFFFF0000u; /* Colour.RED */
        app_overlay_push(app, &bar);
    }

    /* Hitsplats: up to 4 concurrent, each alive for 70 cycles, positioned by
     * slot (reference nudges slots 1-3 off the centre). */
    for( int i = 0; i < WORLD_ENTITY_DAMAGE_SLOTS; i++ )
    {
        char text[UITREE_ENTITY_OVERLAY_TEXT_LEN];

        if( combat->damage_start_cycles[i] > cycle || combat->damage_cycles[i] <= cycle )
            continue;

        /*
         * The wire named a type; the CACHE decides which one is drawn.
         *
         * 34 of this cache's hitsplat records are opcode 17/18 selectors keyed
         * on the player's own settings — 5 "Hitsplat tinting" and 279 "Max hit
         * hitsplats" — so the id that arrived is a question and this is where it
         * is answered. See `game/rs_hitsplat.h`.
         *
         * Resolved HERE, per frame, rather than once when the hit landed, which
         * is where the reference resolves it too: a splat already on screen
         * re-skins the instant the setting is toggled, and resolving on receipt
         * would leave the ones in flight wearing the old answer.
         *
         * `duration` and `slot_policy` are deliberately NOT resolved — those are
         * read off the type the wire named, at the moment it arrived
         * (`task_exec_entity_info.c`), exactly as the reference reads them from
         * the unresolved type before it swaps.
         */
        int const splat_type =
            RS_Hitsplats_ResolveType(&app->hitsplats, &app->varps, combat->damage_types[i]);

        /* A resolved -1 is the cache saying "draw nothing for this hit". No
         * record in cache.osrs239 says it; it is honoured anyway, because the
         * alternative is drawing a splat the cache asked to hide. */
        if( splat_type < 0 )
            continue;

        if( !app_world_project_actor(
                app,
                placement,
                actor_level,
                (int)draw_position->x,
                (int)draw_position->z,
                height / 2,
                &screen_x,
                &screen_y) )
            continue;

        if( i == 1 )
            screen_y -= 20;
        else if( i == 2 )
        {
            screen_x -= 15;
            screen_y -= 10;
        }
        else if( i == 3 )
        {
            screen_x += 15;
            screen_y -= 10;
        }

        /*
         * The splat behind the number.
         *
         * Two eras, two sources, and the type index means a different thing in
         * each. dat1 packs every splat into one "hitmarks" sprite archive and
         * the damage type is the frame within it. OldSchool gives each type its
         * own *config record* naming an ordinary sprite id (group 32 — rev 239
         * damage is type 28 / sprite 1359 and block is type 26 / sprite 1358),
         * so the type is a table lookup and the resulting sprite has one frame.
         *
         * Preferring the config table means the OldSchool path works; falling
         * back to the archive means the dat1 path is untouched. Neither
         * available draws the number alone, which is what this used to do
         * always.
         */
        {
            int splat_sprite = RS_Hitsplats_SpriteFor(&app->hitsplats, splat_type);
            int splat_scene = -1;
            int splat_frame = 0;

            if( splat_sprite >= 0 )
                splat_scene = UITreeSceneBridge_EnsureSprite(&app->bridge, splat_sprite);
            if( splat_scene < 0 && hitmarks_scene > 0 )
            {
                splat_scene = hitmarks_scene;
                /* The WIRE type, not the resolved one: on the dat1 path this is
                 * a frame index into the hitmarks archive, which is a different
                 * id space from the config table. The two agree today only
                 * because a dat1 cache has no selectors to resolve. */
                splat_frame = combat->damage_types[i];
            }
            if( splat_scene >= 0 )
            {
                struct UITreeEntityOverlay spr = {
                    .kind = UITREE_ENTITY_OVERLAY_SPRITE,
                    .x = screen_x - 12,
                    .y = screen_y - 12,
                    .w = 0,
                    .h = 0,
                    .scene_id = splat_scene,
                    .atlas_index = splat_frame,
                };
                app_overlay_push(app, &spr);
            }
        }
        snprintf(text, sizeof(text), "%d", combat->damage_values[i]);
        if( font_id >= 0 )
        {
            /* Black shadow then white, offset by one px — reference draws the
             * number twice (Client.ts:4931-4932). */
            struct UITreeEntityOverlay num = {
                .kind = UITREE_ENTITY_OVERLAY_TEXT,
                .x = screen_x,
                .y = screen_y + 4,
                .font_id = font_id,
                .color = 0xFF000000u,
            };
            snprintf(num.text, sizeof(num.text), "%s", text);
            app_overlay_push(app, &num);
            num.x = screen_x - 1;
            num.y = screen_y + 3;
            num.color = 0xFFFFFFFFu;
            app_overlay_push(app, &num);
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * Client triggers (game/rs_client_trigger.h).
 * ---------------------------------------------------------------------------
 *
 * The scripts the cache expects the CLIENT to find and run: one per npc type
 * (or category) when it walks on screen, one per loc type when the scene
 * builder places it. Nothing calls them; they are addressed by the hash of a
 * group name, and until now this client had no way to reach a single one.
 */

/*
 * The subject, queued.
 *
 * RS_CS2_RunScript does not run a script -- it queues one, because a script
 * may have to be read off disk first. So writing the active-subject register
 * beside the queue call is writing it for whichever npc happens to be last:
 * one region load queues twenty-six copies of the global npc-add script, they
 * all run during the same settle, and every one of them sees npc twenty-six.
 * Measured before this existed -- all twenty-six npcs shared one overlay pair,
 * indices 0 and 1.
 *
 * The fix is to make the write part of the queue rather than of the caller: a
 * one-shot task that carries its own snapshot, queued immediately in front of
 * the script. The queue is a strict serial FIFO, so "immediately in front"
 * survives every IO yield the script itself takes.
 */
struct Task_ClientTriggerSubject
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_CS2Host* host;
    int kind;
    struct RS_ClientOpContext ctx;
};

static int
Task_ClientTriggerSubject_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_ClientTriggerSubject* task = (struct Task_ClientTriggerSubject*)task_base;

    (void)io;
    PT_BEGIN(&task->pt);
    RS_ClientOpActiveSet(&task->host->clientop, (enum RS_ClientOpKind)task->kind, &task->ctx);
    PT_END(&task->pt);
}

static void
Task_ClientTriggerSubject_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_ClientTriggerSubject_VTable = {
    .run = Task_ClientTriggerSubject_Run,
    .free = Task_ClientTriggerSubject_Free,
};

static void
app_client_trigger_queue(
    struct App* app,
    struct RS_ClientOpContext const* ctx,
    int script_id)
{
    struct Task_ClientTriggerSubject* task;

    assert(app);
    assert(ctx);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ClientTriggerSubject_VTable;
    strcpy(task->task.name, "ClientTriggerSubject");
    task->host = &app->host;
    task->kind = ctx->kind;
    task->ctx = *ctx;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->runner.queue, &task->task);

    RS_CS2_RunScript(&app->host, &app->runner, script_id, NULL, 0, 0, NULL, 0);
}

/* RS_ClientTriggerScriptLookupFn over the cache provider. */
static int
app_client_trigger_lookup_script(
    void* user,
    int name_hash)
{
    return CacheProvider_ClientScriptIdByNameHash((struct CacheProvider*)user, name_hash);
}

/** The clientscript bound to `trigger` for this subject, or -1. Narrowest form
 *  first, exactly as `ClientScript::Get` walks them. */
static int
app_client_trigger_script(
    struct App* app,
    int trigger,
    int subject,
    int category)
{
    assert(app);

    /* "Is there a cache yet" is this caller's question. */
    if( !app->provider )
        return -1;
    return RS_ClientTriggerScriptFor(
        trigger, subject, category, app_client_trigger_lookup_script, app->provider);
}

static void
app_client_trigger_debug(
    char const* what,
    int trigger,
    int subject,
    int category,
    int script_id)
{
    if( !getenv("TORIRS_TRIGGER_DEBUG") )
        return;
    TORIRS_LOG(
        "trigger: %s %d (subject=%d category=%d) -> script %d\n",
        what,
        trigger,
        subject,
        category,
        script_id);
}

/** The same line with the subject's coord, for the ops that compare against a
 *  server-published one. Split out because most triggers have no coord to
 *  print and the extra field would be -1 noise on every npc. */
static void
app_client_trigger_debug_coord(
    char const* what,
    int trigger,
    int subject,
    int coord)
{
    if( !getenv("TORIRS_TRIGGER_DEBUG") )
        return;
    TORIRS_LOG("trigger: %s %d subject=%d coord=%d\n", what, trigger, subject, coord);
}

/** Fire an npc trigger with the npc as the active subject. */
static void
app_client_trigger_npc(
    struct App* app,
    struct WorldEntity_NPC* npc,
    int trigger)
{
    struct ToriRS_Npctype* type;
    struct RS_ClientOpContext ctx;
    int script_id;

    assert(app);
    assert(npc);

    if( !app->world )
        return;
    type = CacheProvider_NpctypeGet(app->provider, npc->npc_id);
    script_id = app_client_trigger_script(app, trigger, npc->npc_id, type ? type->category : 0);
    app_client_trigger_debug("npc", trigger, npc->npc_id, type ? type->category : 0, script_id);
    if( script_id < 0 )
        return;

    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = RS_CLIENTOP_NPC;
    ctx.layer = -1;
    ctx.uid = npc->server_slot;
    ctx.type = npc->npc_id;
    ctx.coord = RS_CLIENTOP_COORD(
        npc->grid_position.level,
        app->world->_base_tile_x + npc->grid_position.x,
        app->world->_base_tile_z + npc->grid_position.z);
    snprintf(ctx.name, sizeof(ctx.name), "%s", npc->name);
    /*
     * The ACTIVE register, not a client-op dispatch context.
     *
     * A dispatch is gated on the script id that was named for it, which is
     * right for a right-click row and wrong here: a trigger script calls procs
     * and installs hooks that read the subject back later, and the reference
     * models exactly that with a register that simply stands until something
     * else writes it. See rs_clientop.h.
     */
    app_client_trigger_queue(app, &ctx, script_id);
}

/** Fire a loc trigger with the loc as the active subject. */
static void
app_client_trigger_loc(
    struct App* app,
    struct WorldEntity_Scenery* loc,
    int trigger)
{
    struct ToriRS_Location* type;
    struct RS_ClientOpContext ctx;
    int script_id;

    assert(app);
    assert(loc);

    if( !app->world )
        return;
    type = CacheProvider_LocationGet(app->provider, loc->loc_id);
    script_id = app_client_trigger_script(app, trigger, loc->loc_id, type ? type->category : 0);
    app_client_trigger_debug("loc", trigger, loc->loc_id, type ? type->category : 0, script_id);
    if( script_id < 0 )
        return;

    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = RS_CLIENTOP_LOC;
    ctx.uid = -1;
    ctx.type = loc->loc_id;
    ctx.coord = RS_CLIENTOP_COORD(
        loc->grid_position.level,
        app->world->_base_tile_x + loc->grid_position.x,
        app->world->_base_tile_z + loc->grid_position.z);
    ctx.layer = World_LocShapeToLayer(loc->shape);
    snprintf(ctx.name, sizeof(ctx.name), "%s", loc->info->name);
    app_client_trigger_debug_coord("loc", trigger, loc->loc_id, ctx.coord);
    app_client_trigger_queue(app, &ctx, script_id);
}

/**
 * The npc's NPC_ADD trigger, from the entity-sync path.
 *
 * Not from the spawn helper, which is where it started: `server_slot` is
 * written by the caller AFTER the helper returns, so the trigger read -1 for
 * every npc, every overlay keyed on the same absent subject, and the per-frame
 * anchor pass reaped them all a frame later. From here the npc is finished.
 */
void
App_ClientTriggerNpcAdd(
    struct App* app,
    int npc_pool_index)
{
    struct WorldEntity_NPC* npc;

    assert(app);

    if( !app->world )
        return;
    npc = World_EntityPoolGet(&app->world->entities.npc, npc_pool_index);
    if( npc )
        app_client_trigger_npc(app, npc, RS_TRIGGER_NPC_ADD);
}

/**
 * Fire LOC_ADD for every loc in the freshly built scene.
 *
 * Once per world build rather than per frame: scenery only changes when the
 * scene is rebuilt or a zone packet mutates one loc, so a per-frame
 * reconciliation would walk tens of thousands of entries to find nothing.
 *
 * The reference fires this from `Client::OnLoadLocation`, one loc at a time as
 * the builder places it. This client's builder does not have a seam there, and
 * the observable difference is only WHEN inside one build the script runs --
 * every loc in the scene still gets exactly one.
 */
static void
app_client_triggers_world_loaded(struct App* app)
{
    struct World_EntityPool* pool;

    assert(app);

    if( !app->world || !app->provider )
        return;
    pool = &app->world->entities.scenery;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery* loc = World_EntityPoolGet(pool, i);
        if( loc )
            app_client_trigger_loc(app, loc, RS_TRIGGER_LOC_ADD);
    }
}

/**
 * Re-fire every ADD trigger for everything already in the world.
 *
 * For a tree rebuild, which is the one event that destroys a scripted overlay
 * without destroying its subject. See App::client_trigger_overlay_com.
 *
 * The overlay records go first: their `component_id`s name nodes that no
 * longer exist, and a GET op answering "you already have one" would stop the
 * script rebuilding it.
 */
static void
app_client_triggers_refire(struct App* app)
{
    struct World_EntityPool* pool;

    assert(app);

    if( !app->world || !app->provider )
        return;

    RS_OverlayReset(&app->host.overlay);
    /* The pile subjects survive a UI remount just like NPCs and scenery.
     * Rebuild their native CS2 labels at the ordinary ground-items tick. */
    RS_GroundItemsDirty_MarkAll(&app->ground_items_dirty);
    app_client_triggers_world_loaded(app);

    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        /* An npc with no server slot has no uid for an overlay to key on --
         * offline debug spawns, and npcs mid-sync. The next NPC_INFO gives it
         * one and App_ClientTriggerNpcAdd fires there. */
        if( npc && npc->server_slot >= 0 )
            app_client_trigger_npc(app, npc, RS_TRIGGER_NPC_ADD);
    }
}

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
static int
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
static int
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
static void
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

static void
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
static int
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
static int
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
static int
app_cs2_objs_on_coord(
    void* user,
    int coord,
    int index,
    struct RS_CS2GroundObj* out)
{
    struct App* app = (struct App*)user;
    struct World* world;
    struct World_EntityPool* pool;
    int tile_x;
    int tile_z;
    int level;
    int count = 0;

    assert(app);
    assert(out);

    if( !app->world || !World_CoordToSceneTile(app->world, coord, &tile_x, &tile_z, &level) )
        return 0;
    world = app->world;
    pool = &world->entities.obj_stack;
    for( int oi = World_EntityPoolHead(pool); oi != WORLD_ENTITY_NIL;
         oi = World_EntityPoolNext(pool, oi) )
    {
        struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, oi);
        if( !stack || stack->element_id < 0 )
            continue;
        if( stack->grid_position.x != tile_x || stack->grid_position.z != tile_z ||
            stack->grid_position.level != level )
            continue;
        if( count == index )
        {
            out->obj_id = stack->obj_id;
            out->count = stack->count;
            out->public_clock = stack->public_clock;
            out->despawn_clock = stack->despawn_clock;
            out->owner = stack->owner;
            out->never_becomes_public = stack->never_becomes_public;
        }
        count++;
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
static void
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
static void
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
            fprintf(
                stderr,
                "ground_items: tile %d,%d level %d -> %d obj(s), script %d\n",
                (coord >> 14) & 0x3fff,
                coord & 0x3fff,
                (coord >> 28) & 3,
                app_cs2_objs_on_coord(app, coord, -1, &entry),
                app->host.script_ground_items_overlay);
        }
        app_cs2_set_active_tile(app, coord);
        RS_CS2_RunScript(
            &app->host, &app->runner, app->host.script_ground_items_overlay, NULL, 0, 0, NULL, 0);
    }
    RS_GroundItemsDirty_Clear(&app->ground_items_dirty);
}

/**
 * The three anchor points an overlay's band chooses between
 * (`Client::GetAllOverlayPositions`): the top of the subject, its middle, and
 * its feet. Screen pixels.
 *
 * `subject_live` and `ok` are deliberately two answers, not one. An npc that
 * is merely behind the camera does not project, and treating that as "the
 * subject has gone" reaped every overlay the moment its npc left the view --
 * which, with the global npc-add trigger giving every npc a name plate, meant
 * the whole table churned back to index 0 every frame.
 */
struct AppOverlayPos
{
    bool subject_live;
    bool ok;
    int top_x;
    int top_y;
    int mid_x;
    int mid_y;
    int foot_x;
    int foot_y;
};

/** Where one overlay's subject is this frame, or `ok = false` when the subject
 *  has gone -- which is the signal to reap the overlay, not to hide it. */
static struct AppOverlayPos
app_overlay_anchor(
    struct App* app,
    struct RS_Overlay const* item)
{
    struct AppOverlayPos out;
    int fine_x = 0;
    int fine_z = 0;
    int height = 0;

    assert(app);
    assert(item);

    memset(&out, 0, sizeof(out));
    if( !app->world )
        return out;

    if( item->anchor == RS_OVERLAY_ANCHOR_NPC )
    {
        struct WorldEntity_NPC* npc = World_NpcGetByServerSlot(app->world, item->uid);
        if( !npc )
            return out;
        fine_x = (int)npc->draw_position.x;
        fine_z = (int)npc->draw_position.z;
        height = app_entity_overlay_height(app, npc->element_id, -1);
        out.subject_live = true;
    }
    else if( item->anchor == RS_OVERLAY_ANCHOR_PLAYER )
    {
        struct WorldEntity_Player* pl = World_PlayerGetByServerPid(app->world, item->uid);
        if( !pl )
            return out;
        fine_x = (int)pl->draw_position.x;
        fine_z = (int)pl->draw_position.z;
        height = app_entity_overlay_height(app, pl->element_id, -1);
        out.subject_live = true;
    }
    else
    {
        int x;
        int z;
        int level;
        if( !app->world || !World_CoordToSceneTile(app->world, item->coord, &x, &z, &level) )
            return out;
        /*
         * A tile has no model, so all three anchors are the tile centre at
         * ground height: an "above" overlay stacks up from the floor and a
         * "below" one stacks down from it. The reference measures the loc's own
         * model here; a loc whose overlay wants to clear it says so with its
         * band and its height, which is what every static overlay in this cache
         * does (60x60 above, at the tile).
         */
        fine_x = x * 128 + 64;
        fine_z = z * 128 + 64;
        height = 0;
        out.subject_live = true;
    }

    int32_t overlay_node = app->tree ? UITree_FindByComponentId(app->tree, item->component_id) : -1;
    int lift = UITree_WidgetProjectionHeight(app->tree, overlay_node);
    if( !app_world_project(app, fine_x, fine_z, height + lift, &out.top_x, &out.top_y) )
        return out;
    if( !app_world_project(app, fine_x, fine_z, height / 2 + lift, &out.mid_x, &out.mid_y) )
        return out;
    if( !app_world_project(app, fine_x, fine_z, -15 + lift, &out.foot_x, &out.foot_y) )
        return out;
    out.ok = true;
    return out;
}

/**
 * Move every scripted overlay's layer to where its subject is, and reap the
 * ones whose subject has gone.
 *
 * Runs immediately before the emit walk, off the PREVIOUS frame's world
 * viewport -- the same rect `app_world_project` reads, so an overlay and the
 * health bar over the same npc cannot disagree by a frame.
 */
static void
app_entity_overlay_layout(struct App* app)
{
    assert(app);

    if( !app->tree )
        return;

    int32_t const parent = app->tree->entity_overlay_index;
    if( parent < 0 )
        return;

    /* A rebuilt tree took every overlay layer with it (see
     * App::client_trigger_refire_pending). The sweep below sets the flag when
     * it finds an overlay whose layer is gone; it is acted on here, before the
     * sweep, so the refire happens outside the walk it would invalidate. */
    if( app->client_trigger_refire_pending )
    {
        app->client_trigger_refire_pending = 0;
        app_client_triggers_refire(app);
    }

    /* The parent IS the world rect: it is what clips the overlays (see
     * UITree_ComponentClipsChildren), and the App is the only thing that knows
     * the rect. A tree whose world has not been emitted yet has no rect and so
     * no overlays -- which is right, because there is nothing to anchor to. */
    {
        struct UITreeElemPosition const* pos = &app->tree->components[parent].position;
        int const w = app->world_view_valid ? app->world_emit_desc.w : 0;
        int const h = app->world_view_valid ? app->world_emit_desc.h : 0;
        int const x = app->world_view_valid ? app->world_emit_desc.x : 0;
        int const y = app->world_view_valid ? app->world_emit_desc.y : 0;
        if( pos->kind != UIPOS_XY || pos->x != x || pos->y != y || pos->width != w ||
            pos->height != h )
            (void)UITree_SetXYBoxAt(app->tree, parent, x, y, w, h);
    }

    /* Band 1 stacks upward and band 2 downward, per subject -- two overlays on
     * one npc must not overprint. The cursors are keyed by the subject the
     * overlay names, so a second pass over the same npc continues the stack. */
    for( int i = 0; i < RS_OVERLAY_MAX; i++ )
    {
        struct RS_Overlay const* item = RS_OverlayGet(&app->host.overlay, i);
        if( !item )
            continue;

        struct AppOverlayPos anchor = app_overlay_anchor(app, item);
        if( !anchor.subject_live )
        {
            /*
             * The subject is GONE -- the npc despawned, or the tile fell out of
             * the rebuilt scene.
             *
             * Reaped rather than hidden, because nothing else will: an npc that
             * walked out of the scene is never coming back under the same uid,
             * and the script that made the overlay gets no event to tell it so.
             */
            if( torirs_env_overlay_script_debug() )
                TORIRS_LOG(
                    "overlay: reap #%d anchor=%d uid=%d coord=%d slot=%d\n",
                    i,
                    item->anchor,
                    item->uid,
                    item->coord,
                    item->slot);
            RS_CS2Host_OverlayReap(&app->host, i);
            continue;
        }

        int32_t const node = UITree_FindByComponentId(app->tree, item->component_id);
        if( node < 0 )
        {
            /* The layer is gone but the record is not: a tree rebuild. Every
             * overlay is in the same state, so this is raised once and acted
             * on at the top of the next pass. */
            app->client_trigger_refire_pending = 1;
            continue;
        }
        /* Projection failure is camera-owned visibility, not script-owned
         * `hide`. Leaving the old layer visible here freezes an overlay at its
         * last valid coordinates when its subject crosses the near plane. */
        if( !anchor.ok )
        {
            (void)UITree_SetProjectionHiddenAt(app->tree, node, 1);
            continue;
        }
        (void)UITree_SetProjectionHiddenAt(app->tree, node, 0);

        struct UITreeComponent* c = &app->tree->components[node];
        struct UITreeElemPosition allocation = c->position;
        UITree_WidgetPositionOverride(app->tree, node, &allocation);
        int const w = allocation.width;
        int const h = allocation.height;
        int x = anchor.mid_x - w / 2;
        int y = anchor.mid_y - h / 2;

        if( item->band == RS_OVERLAY_BAND_ABOVE )
        {
            x = anchor.top_x - w / 2;
            y = anchor.top_y - h;
        }
        else if( item->band == RS_OVERLAY_BAND_BELOW )
        {
            x = anchor.foot_x - w / 2;
            y = anchor.foot_y;
        }

        /* The box is the parent's, so subtract the world rect the parent sits
         * at -- the projection is in screen pixels and the layout is not. */
        x -= app->tree->components[parent].position.x;
        y -= app->tree->components[parent].position.y;

        if( c->position.x != x || c->position.y != y )
        {
            /* This is a retained-tree mutation, not just a repaint request.
             * The setter clears the cached absolute box and bumps dirty_gen,
             * so the emit-retention gate cannot reuse the sprite command from
             * the previous camera angle. */
            (void)UITree_EntityOverlaySetLayerPosition(app->tree, node, x, y);
        }

        if( torirs_env_overlay_script_debug() )
        {
            int kids = 0;
            for( int32_t k = c->first_child; k >= 0; k = app->tree->components[k].next_sibling )
                kids++;
            TORIRS_LOG(
                "overlay: #%d anchor=%d slot=%d band=%d com=0x%08x box=%d,%d %dx%d kids=%d "
                "hide=%d\n",
                i,
                item->anchor,
                item->slot,
                item->band,
                (unsigned)item->component_id,
                x,
                y,
                w,
                h,
                kids,
                (int)c->behavior.hide);
        }
    }
}

/*
 * The plugin canvas overlay, built on demand.
 *
 * A whole function for four lines because the shape has to match the world
 * list's exactly: empty it, let the pushers fill it, hand the array over. What is
 * NOT here is any of the client's own drawing -- nothing but a plugin ever
 * writes to this list, which is why the pass that asks for it can be
 * unconditional and still cost nothing on a client with no plugins.
 */
static int
app_build_canvas_overlays(
    struct App* app,
    struct UITreeEntityOverlay const** out_items)
{
    assert(app);
    assert(out_items);

    if( app->plugin_canvas_overlay_prepared )
    {
        *out_items = app->canvas_overlays;
        return app->canvas_overlay_count;
    }

    app->plugin_canvas_overlay_prepared = 1;
    app->canvas_overlay_count = 0;
    *out_items = app->canvas_overlays;
    if( !app->plugins )
        return 0;

    PluginHost_DrawCanvas(app->plugins, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    return app->canvas_overlay_count;
}

static int
app_build_entity_overlays(
    struct App* app,
    struct UITreeEntityOverlay const** out_items)
{
    struct World* world = app->world;
    struct World_EntityPool* pool;
    int font_id;
    int hitmarks_scene;

    app->entity_overlay_count = 0;
    *out_items = app->entity_overlays;
    if( !world || !world->load_complete || !app->world_view_valid )
        return 0;

    font_id = app_hitsplat_font_scene_id(app);
    hitmarks_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_HITMARKS);
    /*
     * Older caches ship one `headicons` pack holding prayer icons and the PK
     * skull together; OldSchool split it, and rev 230 has no `headicons`
     * archive at all — only `headicons_prayer`, `headicons_pk` and
     * `headicons_hint`. The prayer icons keep their indices across the split
     * (0 melee, 1 missiles, 2 magic, 3 retribution, 4 smite, 5 redemption), so
     * the split pack is a drop-in for the overhead pass. Without this the whole
     * feature is silently dead on a modern cache: the mask arrives, the slot is
     * -1, and nothing draws.
     */
    int headicons_scene =
        UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_HEADICONS);
    if( headicons_scene <= 0 )
        headicons_scene =
            UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_HEADICONS_PRAYER);
    pool = &world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        struct ToriRS_Npctype* npctype;
        if( !npc || npc->multinpc_hidden || npc->element_id < 0 )
            continue;
        /* Only the npc branch can carry an overhead-height override; the
         * reference reads it off the NpcComposition, which players have no
         * equivalent of (Actor.getLogicalHeight is unconditional there). */
        npctype = CacheProvider_NpctypeGet(app->provider, npc->npc_id);
        app_overlay_build_entity(
            app,
            npc->element_id,
            &npc->combat,
            &npc->draw_position,
            &npc->view_placement,
            -1,
            font_id,
            hitmarks_scene,
            npctype ? npctype->height : -1);
        app_overlay_build_npc_headicon(
            app,
            npc->element_id,
            npctype,
            &npc->draw_position,
            &npc->view_placement,
            headicons_scene,
            APP_HEADICONS_PRAYER_GROUP);
    }

    pool = &world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
        if( !player || player->element_id < 0 )
            continue;
        app_overlay_build_entity(
            app,
            player->element_id,
            &player->combat,
            &player->draw_position,
            &player->view_placement,
            player->grid_position.level,
            font_id,
            hitmarks_scene,
            -1);
        app_overlay_build_player_headicons(
            app,
            player->element_id,
            player->headicon,
            &player->draw_position,
            &player->view_placement,
            player->grid_position.level,
            headicons_scene);
    }

    /* One arrow, after every entity, so it layers over the health bars and
     * splats of whatever it is pointing at. */
    app_overlay_build_hint_arrow(app);

    /* Overhead chat is a second pass so it layers above every entity's health
     * bar and hitsplats (reference draws chatX/chatY after the entity loop).
     * It uses b12, the bold chat font, not the p11 hitsplat font. */
    {
        int chat_font = app_minimenu_font_scene_id(app);

        pool = &world->entities.npc;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
            if( !npc || npc->multinpc_hidden || npc->element_id < 0 )
                continue;
            app_overlay_build_chat(
                app,
                npc->element_id,
                &npc->chat,
                &npc->draw_position,
                &npc->view_placement,
                -1,
                chat_font);
        }

        pool = &world->entities.player;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
            if( !player || player->element_id < 0 )
                continue;
            app_overlay_build_chat(
                app,
                player->element_id,
                &player->chat,
                &player->draw_position,
                &player->view_placement,
                player->grid_position.level,
                chat_font);
        }
    }

    /* Debug: hovered loc's painter footprint, in red (see the builder). Last
     * so the outline layers above bars/splats/chat. */
    app_overlay_build_hover_footprint(app);
    /* The map editor's own latch, in green -- separate from the hover mark
     * above so a select-tool session and TORIRS_HOVER_FOOTPRINT can be on at
     * once without one drawing over the other's meaning. */
    app_overlay_build_editor_selection(app);

    /*
     * Plugins last, so their marks layer above every built-in in this pass.
     *
     * This whole layer is hoisted to just above the 3D world by
     * emit_hoist_entity_overlays, which puts plugin drawing exactly where a
     * RuneLite scene overlay sits: over the world, under the interfaces, the
     * cross, the hover line and the minimenu. It costs nothing when no plugin
     * subscribed, and the items land in the pool the built-ins have already
     * taken what they need from -- so a crowded scene clips the plugin, never
     * a health bar.
     */
    PluginHost_DrawWorld(app->plugins);

    /* TORIRS_OVERLAY_DEBUG=1: the primitives this frame, plus the two assets
     * they need — a missing p11 (font -1) or hitmarks pack is the usual
     * reason a hit lands but nothing is drawn. */
    if( torirs_env_overlay_debug() && app->entity_overlay_count > 0 )
    {
        TORIRS_LOG(
            "overlay: %d items font=%d hitmarks=%d\n",
            app->entity_overlay_count,
            font_id,
            hitmarks_scene);
        for( int i = 0; i < app->entity_overlay_count; i++ )
        {
            struct UITreeEntityOverlay const* item = &app->entity_overlays[i];
            /* scene/clip/trans are printed because a SPRITE primitive carries
             * no w/h -- it blits at the sprite's own size -- so without them a
             * health bar's line says nothing about how wide it came out. */
            TORIRS_LOG(
                "  overlay[%d] kind=%d at %d,%d %dx%d scene=%d clip=%d,%d %dx%d "
                "trans=%d \"%s\"\n",
                i,
                item->kind,
                item->x,
                item->y,
                item->w,
                item->h,
                item->scene_id,
                item->clip_x,
                item->clip_y,
                item->clip_w,
                item->clip_h,
                item->trans,
                item->text);
        }
    }
    return app->entity_overlay_count;
}

/* Forward decls: UITREE_HOST_GET_INV_DRAG asks whether the armed press is
 * ghosting; the definitions live beside app_inv_drag_tick. */
static int
app_inv_drag_promoted(struct App const* app);
static int
app_inv_drag_ghosting(struct App const* app);

/** Convert one retained panel primitive for the in-canvas fallback. */
static int
app_panel_overlay_to_chrome(
    struct App const* app,
    int index,
    struct ToriRSChromePrim* out)
{
    struct UITreeEntityOverlay visible;
    struct UITreeEntityOverlay const* item = &visible;

    assert(app);
    if( !app_plugin_panel_overlay_visible(app, index, &visible) )
        return 0;
    assert(item);
    assert(out);
    memset(out, 0, sizeof(*out));
    out->x = item->x;
    out->y = item->y;
    out->w = item->w;
    out->h = item->h;
    out->color = item->color & 0x00FFFFFFu;
    out->trans = item->trans;
    out->clip.x = item->clip_x;
    out->clip.y = item->clip_y;
    out->clip.w = item->clip_w;
    out->clip.h = item->clip_h;

    switch( item->kind )
    {
    case UITREE_ENTITY_OVERLAY_RECT:
        out->kind = TORIRS_CHROME_PRIM_RECT;
        out->filled = 1;
        return 1;
    case UITREE_ENTITY_OVERLAY_TEXT:
        out->kind = TORIRS_CHROME_PRIM_TEXT;
        out->font_slot = TORIRS_CHROME_FONT_BODY;
        out->baseline = 1;
        out->text = item->text;
        return item->text[0] != '\0';
    case UITREE_ENTITY_OVERLAY_SPRITE:
        out->kind = TORIRS_CHROME_PRIM_SPRITE;
        out->sprite_scene_id = item->scene_id;
        return item->scene_id > 0;
    case UITREE_ENTITY_OVERLAY_LINE:
        out->kind = TORIRS_CHROME_PRIM_LINE;
        out->line_direction = item->line_direction;
        out->line_width = item->line_width;
        return 1;
    default:
        /* Panel APIs expose rect/line/text/image, not world polygons. */
        return 0;
    }
}

/**
 * Both chrome instances' display lists, then panel-local custom primitives.
 *
 * Rebuilt only when the pair actually differs from what was merged last: the
 * prim arrays are handed downstream by pointer and a steady frame must stay a
 * pointer copy, which is the property the whole retained design is for. The
 * cheap comparison is the two counts plus the two damage states, and Build
 * having already decided nothing changed is what makes both stable.
 *
 * The plugin window goes SECOND, so it draws over the developer readout: it is
 * the one a player opened, and a frame-time counter on top of it would be a
 * developer tool covering a user's window.
 */
static struct ToriRSChromePrim const*
app_chrome_merged_prims(
    struct App* app,
    int* out_count)
{
    int dbg_count = 0;
    int win_count = 0;
    struct ToriRSChromePrim const* dbg = ToriRSChrome_Prims(&app->dbg_ui, &dbg_count);
    struct ToriRSChromePrim const* win = ToriRSChrome_Prims(&app->plugin_ui, &win_count);

    assert(out_count);

    /*
     * Nothing to merge: hand the developer chrome's own array straight out, so
     * the common case -- no plugin window open -- costs exactly what it did
     * before this existed.
     *
     * WEB/BROWSER rebuild the window as DOM controls, so putting its prims in
     * the canvas as well would draw it twice. BUFFER is the only internal
     * presentation that consumes the display list here.
     */
    if( win_count == 0 || app->plugin_exec_kind != TORIRS_CHROME_EXEC_BUFFER )
    {
        *out_count = dbg_count;
        return dbg;
    }

    /* Exact, not a heuristic: the serials move on every rebuild, including one
     * that changed a string without changing the prim count. */
    if( app->chrome_merged_dbg != app->dbg_ui.build_serial ||
        app->chrome_merged_win != app->plugin_ui.build_serial ||
        app->chrome_merged_panel != app->panel_overlay_revision )
    {
        int n = dbg_count < APP_CHROME_PRIMS_MAX ? dbg_count : APP_CHROME_PRIMS_MAX;
        memcpy(app->chrome_merged, dbg, (size_t)n * sizeof(*dbg));
        if( n < APP_CHROME_PRIMS_MAX )
        {
            int const take =
                win_count < APP_CHROME_PRIMS_MAX - n ? win_count : APP_CHROME_PRIMS_MAX - n;
            memcpy(&app->chrome_merged[n], win, (size_t)take * sizeof(*win));
            n += take;
        }
        for( int i = 0; i < app->panel_overlay_count && n < APP_CHROME_PRIMS_MAX; i++ )
            if( app_panel_overlay_to_chrome(app, i, &app->chrome_merged[n]) )
                n++;
        app->chrome_merged_dbg = app->dbg_ui.build_serial;
        app->chrome_merged_win = app->plugin_ui.build_serial;
        app->chrome_merged_panel = app->panel_overlay_revision;
        app->chrome_merged_count = n;
    }
    *out_count = app->chrome_merged_count;
    return app->chrome_merged;
}

/*
 * The system-update line, or NULL when no update is pending.
 *
 * Reference drawScene: seconds = rebootTimer / 50 (fifty 20ms cycles to the
 * second), then minutes:seconds zero-padded. The 50 is spelled here as the
 * cycles-per-second it is, so the one place the client turns its own clock
 * into a wall-clock reading says which clock it means.
 *
 * The returned pointer is App-owned and lives until the next call, which is
 * the same frame lifetime the hovertext model has.
 */
static char const*
app_reboot_timer_text(struct App* app)
{
    int seconds;
    int minutes;

    assert(app);
    if( app->reboot_timer == 0 )
        return NULL;

    seconds = app->reboot_timer / APP_LOGIC_CYCLES_PER_SECOND;
    minutes = seconds / 60;
    seconds %= 60;
    snprintf(
        app->reboot_timer_text,
        sizeof(app->reboot_timer_text),
        "System update in: %d:%02d",
        minutes,
        seconds);
    return app->reboot_timer_text;
}

/*
 * The blink period the title tree's focused input asks for, or 0 when nothing
 * on screen blinks.
 *
 * Read off the tree rather than kept on App because it is the widget's
 * property: two revisions may spell the caret differently and time it
 * differently, and both say so in their own INI.
 */
static int
app_title_caret_blink(struct App const* app)
{
    assert(app);
    if( !app->tree )
        return 0;
    for( uint32_t i = 0; i < app->tree->component_count; i++ )
    {
        struct UITreeComponent const* comp = &app->tree->components[i];
        if( comp->freed || comp->type != UIELEM_BUILTIN_LOGIN_INPUT )
            continue;
        if( UITree_LoginInput(comp)->field != app->title.focus )
            continue;
        return UITree_LoginInput(comp)->caret_blink;
    }
    return 0;
}

/*
 * Light the braziers, once the title tree's art is resident.
 *
 * The fire burns in front of two 128-wide columns of the backdrop, so it needs
 * the composited panel before it can start -- which is also why this is not
 * done at bake time: the sprite arrives through the same async asset pass
 * everything else does.
 *
 * A profile with no backdrop gets no fire rather than a fire over black. That
 * is the undeclared-means-absent contract again, and it is the honest answer:
 * the flames are a lighting effect on a picture, and without the picture they
 * are just two glowing rectangles.
 */
static void
app_title_flames_start(struct App* app)
{
    struct ToriRS_Sprite* runes;
    struct ToriDraw_Sprite** panel_frames;
    struct ToriDraw_Sprite const* panel;
    uint32_t* column[TORIRS_FLAME_SIDES] = { NULL, NULL };
    uint32_t const* pair[TORIRS_FLAME_SIDES];
    int sprite_id;
    int scene_id;
    int frame_count = 0;
    int col_h;

    assert(app);
    if( app->flames || !app->provider || !app->scene )
        return;

    sprite_id = CacheProvider_SpriteIdByName(app->provider, "title_background");
    if( sprite_id < 0 )
        return;

    /*
     * Read the backdrop out of the SCENE, not the provider.
     *
     * Uploading a sprite hands its pixels to the scene and leaves the
     * provider's copy empty -- the client deliberately does not keep two of
     * every image. The scene is therefore where the picture actually is by the
     * time anything wants to look at it.
     */
    scene_id = UITreeSceneBridge_EnsureSprite(&app->bridge, sprite_id);
    if( scene_id < 0 )
        return;
    panel_frames = ToriDraw_SceneSpriteGet(app->scene, scene_id, &frame_count);
    if( !panel_frames || frame_count < 1 || !panel_frames[0] )
        return;
    panel = panel_frames[0];
    if( !panel->pixels_argb || panel->width < TORIRS_FLAME_W * 2 )
        return;

    /*
     * The column is taller than the heat field, and deliberately.
     *
     * The reference's surface is 128x265 while the fire it holds is
     * 128x256: the fire is drawn nine rows down, so its base lands in the
     * brazier bowl rather than at the bottom edge of the strip. Cutting the
     * column to the fire's own height instead leaves the flame standing on
     * the surface's edge, with a hard seam where the copied wall stops.
     */
    col_h = panel->height < TORIRS_FLAME_COLUMN_H ? panel->height : TORIRS_FLAME_COLUMN_H;

    /* The two strips the reference burns in: hard against each edge of the
     * panel, which is where the braziers are painted. */
    for( int side = 0; side < TORIRS_FLAME_SIDES; side++ )
    {
        int src_x = side == TORIRS_FLAME_LEFT ? 0 : panel->width - TORIRS_FLAME_W;
        column[side] = malloc((size_t)TORIRS_FLAME_W * col_h * sizeof(*column[side]));
        assert(column[side]);
        for( int y = 0; y < col_h; y++ )
            memcpy(
                &column[side][(size_t)y * TORIRS_FLAME_W],
                &panel->pixels_argb[(size_t)y * panel->width + src_x],
                (size_t)TORIRS_FLAME_W * sizeof(*column[side]));
        pair[side] = column[side];
    }

    /* Runes are optional: without them the cooling map carries no glyphs and
     * the fire is a plain one, which is what a revision with no rune pack
     * honestly has. */
    sprite_id = CacheProvider_SpriteIdByName(app->provider, "runes");
    runes = sprite_id >= 0 ? CacheProvider_SpriteGet(app->provider, sprite_id) : NULL;

    app->flames = calloc(1, sizeof(*app->flames));
    assert(app->flames);
    TitleFlames_Init(app->flames, pair, TORIRS_FLAME_W, col_h, runes);
    app->flames_last_ms = 0;

    for( int side = 0; side < TORIRS_FLAME_SIDES; side++ )
        free(column[side]);
}

static void
app_title_flames_stop(struct App* app)
{
    assert(app);
    if( !app->flames )
        return;
    TitleFlames_Free(app->flames);
    free(app->flames);
    app->flames = NULL;
}

/* One frame of fire, uploaded into the two reserved scene slots. */
static void
app_title_flames_tick(
    struct App* app,
    uint64_t now_ms)
{
    static int const k_slot[TORIRS_FLAME_SIDES] = {
        UITREE_SCENE_TITLE_FLAME_LEFT_ID,
        UITREE_SCENE_TITLE_FLAME_RIGHT_ID,
    };
    int elapsed;

    assert(app);
    if( !app->flames || !app->scene )
        return;

    elapsed = app->flames_last_ms == 0 ? 0 : (int)(now_ms - app->flames_last_ms);
    app->flames_last_ms = now_ms;
    if( !TitleFlames_Advance(app->flames, elapsed) )
        return;

    for( int side = 0; side < TORIRS_FLAME_SIDES; side++ )
    {
        size_t bytes = (size_t)app->flames->width * app->flames->height * sizeof(uint32_t);
        uint32_t* copy = malloc(bytes);
        struct ToriDraw_Sprite* sprite;
        struct ToriDraw_Sprite** sprites;

        assert(copy);
        memcpy(copy, TitleFlames_Pixels(app->flames, (enum TitleFlameSide)side), bytes);
        sprite = ToriDraw_SpriteNewFromArgbOwned(copy, app->flames->width, app->flames->height);
        if( !sprite )
        {
            free(copy);
            continue;
        }
        sprites = malloc(sizeof(*sprites));
        assert(sprites);
        sprites[0] = sprite;
        /* Adding over a live id frees what was there and re-emits the load, so
         * the GPU lanes pick the new pixels up; the soft lane reads the scene
         * directly. */
        if( ToriDraw_SceneSpriteHas(app->scene, k_slot[side]) )
            ToriDraw_SceneSpriteRemove(app->scene, k_slot[side]);
        ToriDraw_SceneSpriteAdd(app->scene, k_slot[side], sprites, 1);
    }

    app->need_redraw = 1;
}

/*
 * Show the group belonging to the current title screen, hide the rest.
 *
 * The title tree carries every screen at once -- menu, form, info, loading --
 * because they share a panel and rebuilding the tree per screen would flash
 * the whole backdrop. Which one is visible is the only thing that changes.
 *
 * Found by role, never by index: the tree is rebuilt whenever the window
 * changes shape, and a remembered index would then point at whatever landed in
 * that slot.
 */
static void
app_title_sync_groups(struct App* app)
{
    static char const* const k_groups[] = {
        "title_menu_group",
        "title_form_group",
        "title_info_group",
        "title_progress_group",
    };
    /* Parallel to k_groups: which RS_TitleScreen each belongs to, and -1 for
     * the loading bar, which answers to the boot progress instead. */
    static int const k_screen[] = {
        RS_TITLE_MAIN_MENU,
        RS_TITLE_LOGIN_FORM,
        RS_TITLE_INFO,
        -1,
    };
    int showing_progress;

    assert(app);
    if( !app->tree )
        return;

    /* The bar owns the panel while there is one: the reference draws the
     * loading screen INSTEAD of the login box, in the same 360x200 space. */
    showing_progress = app->title.progress_percent >= 0;

    for( size_t i = 0; i < sizeof(k_groups) / sizeof(k_groups[0]); i++ )
    {
        int32_t idx = UITree_RoleNodeByName(app->tree, &app->ui_roles, k_groups[i]);
        int visible;

        if( idx < 0 )
            continue;
        visible = k_screen[i] < 0 ? showing_progress
                                  : (!showing_progress && k_screen[i] == (int)app->title.screen);
        UITree_SetScreenHiddenAt(app->tree, idx, !visible);
    }

    /* While a submitted login is dialling, the form stays up -- its message
     * lines are where "Connecting to server..." appears -- but the Login and
     * Cancel buttons are withdrawn: mid-handshake there is nothing either
     * could meaningfully do. The profile declares the role around exactly
     * the widgets it wants withdrawn; one that declares no such layer keeps
     * its buttons and loses nothing else. */
    {
        int32_t idx = UITree_RoleNodeByName(app->tree, &app->ui_roles, "title_form_buttons");
        if( idx >= 0 )
            UITree_SetScreenHiddenAt(app->tree, idx, app->screen == APP_SCREEN_CONNECTING);
    }
}

/*
 * The title screen's state changed: redraw, and tell the retention gate.
 *
 * Both halves matter. Without the epoch bump the emit walk reuses last frame's
 * command buffer and the typed character never appears; without need_redraw
 * the frame loop may not present at all.
 */
static void
app_title_state_changed(struct App* app)
{
    assert(app);
    app_title_sync_groups(app);
    UITree_HostInputsChanged(&app->ui_host, UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_CLIENT_STATE));
    app->need_redraw = 1;
}

/*
 * Publish a boot step to the title screen's loading bar.
 *
 * The percentage is the client's; the words are the profile's, looked up by
 * name. A revision that declares no such string gets the bar with no caption
 * rather than an English sentence it never chose.
 */
/*
 * Announce one step of the profile's preload list.
 *
 * The percentage and the words are both the step's, so a revision that
 * counts its boot differently -- and the two here do; one steps through
 * positions while the other sums weights -- says so in its profile rather
 * than in this function. A step the profile does not declare announces
 * nothing and the bar stays where it was, which is the same
 * undeclared-means-absent rule the rest of revconfig runs on.
 *
 * Returns the step so the caller can see whether it asked to be rendered.
 */
static struct RS_PreloadStep const*
app_preload_announce(
    struct App* app,
    char const* step_name)
{
    struct RS_PreloadStep const* step = NULL;

    assert(app);
    assert(step_name);
    for( int i = 0; i < app->preload.count; i++ )
    {
        if( strcmp(app->preload.steps[i].name, step_name) == 0 )
        {
            step = &app->preload.steps[i];
            break;
        }
    }
    if( !step )
        return NULL;

    if( step->percent >= 0 )
        app->boot_progress = step->percent;
    RS_Title_SetProgress(
        &app->title,
        step->percent,
        step->say[0] ? RS_LoginReplies_String(&app->login_replies, step->say) : NULL);
    if( app->screen == APP_SCREEN_TITLE || app->screen == APP_SCREEN_CONNECTING )
        app_title_state_changed(app);
    return step;
}

static void
app_title_progress(
    struct App* app,
    int percent,
    char const* string_key)
{
    assert(app);
    assert(string_key);
    RS_Title_SetProgress(
        &app->title, percent, RS_LoginReplies_String(&app->login_replies, string_key));
    if( app->screen == APP_SCREEN_TITLE || app->screen == APP_SCREEN_CONNECTING )
        app_title_state_changed(app);
}

/*
 * Compose one credential line: prefix, the value (masked if the widget asked),
 * and the caret when this field has focus and the blink is showing.
 *
 * The host composes it rather than the widget because the pieces belong to
 * different owners -- the value and the focus are the model's, the blink is the
 * client's clock, and the spelling of the prefix, the mask and the caret are
 * the revision's. The reference draws exactly this, as one string, because
 * centring or measuring the label separately from the value would not
 * reproduce it (Client-TS titleScreenDraw, deob method8166).
 *
 * The returned pointer is App-owned and lives until the next call: the same
 * frame lifetime the hovertext and reboot-timer strings have.
 */
static int
app_title_field_line(
    struct App* app,
    struct UITreeHostRequest* req)
{
    struct UITreeLoginInputConfig const* cfg = req->u.get_title_field.config;
    char const* value;
    char masked[RS_TITLE_FIELD_LEN];
    int focused;
    int caret_showing = 0;

    assert(app);
    assert(cfg);

    if( app->screen != APP_SCREEN_TITLE && app->screen != APP_SCREEN_CONNECTING )
        return 0;
    if( cfg->field < 0 || cfg->field >= RS_TITLE_FIELD_COUNT )
        return 0;

    value = RS_Title_FieldText(&app->title, (enum RS_TitleField)cfg->field);
    if( cfg->mask[0] != '\0' )
    {
        size_t len = strlen(value);
        if( len >= sizeof(masked) )
            len = sizeof(masked) - 1;
        memset(masked, cfg->mask[0], len);
        masked[len] = '\0';
        value = masked;
    }

    focused = app->title.focus == cfg->field;
    /* caret_blink=0 is a SOLID caret, not an absent one -- the header's
     * "default 0 = never blink" (revconfig.h) means always shown. A profile
     * that wants no caret at all declares caret= empty instead. */
    if( focused )
        caret_showing =
            cfg->caret_blink > 0
                ? (int)(app->logic_cycle % (uint64_t)cfg->caret_blink) < cfg->caret_blink / 2
                : 1;

    snprintf(
        app->title_field_line[cfg->field],
        sizeof(app->title_field_line[cfg->field]),
        "%s%s%s",
        cfg->prefix,
        value,
        caret_showing ? cfg->caret : "");

    if( req->u.get_title_field.out_focused )
        *req->u.get_title_field.out_focused = focused;
    *req->u.get_title_field.out_text = app->title_field_line[cfg->field];
    return 1;
}

static int
app_host_request(
    void* user,
    struct UITreeHostRequest* req)
{
    struct App* app = (struct App*)user;
    struct InvSlot slot;

    assert(req);
    assert(app);

    switch( req->kind )
    {
    case UITREE_HOST_BEGIN_OVERLAYS:
        /* A retained refresh which discovers its first semantic anchor is
         * abandoned in favour of a full walk. Both passes issue BEGIN in the
         * same App_RunOnce; preserve the already-built Canvas list across the
         * fallback so plugin callbacks and their per-frame draw budget run
         * exactly once. App_RunOnce clears this latch for the next frame. */
        if( !app->plugin_overlay_batch_started )
        {
            app->plugin_overlay_batch_started = 1;
            app->plugin_canvas_overlay_prepared = 0;
        }
        return 0;
    case UITREE_HOST_GET_SCROLLBAR_SCENE:
        return UITreeSceneBridge_ScrollbarSceneId(&app->bridge);
    case UITREE_HOST_GET_INKWELL:
    {
        /*
         * The component supplies the artwork it was configured with and the
         * app supplies the marker's live state; neither knows the other's
         * half. -1 from the profile means "unstated", and the defaults here
         * are the reference client's convention: yellow walks, red interacts.
         */
        int const style =
            req->u.get_inkwell.style >= 0 ? req->u.get_inkwell.style : TORIRS_INKWELL_SPLASH;
        int const walk = req->u.get_inkwell.walk_color >= 0 ? req->u.get_inkwell.walk_color
                                                            : TORIRS_INKWELL_YELLOW;
        int const interact = req->u.get_inkwell.interact_color >= 0
                                 ? req->u.get_inkwell.interact_color
                                 : TORIRS_INKWELL_RED;
        int colour;

        if( !UIInk_IsActive(&app->ink) )
            return 0;
        colour = app->ink.colour == TORIRS_INKWELL_RED ? interact : walk;
        if( req->u.get_inkwell.out_x )
            *req->u.get_inkwell.out_x = app->ink.x;
        if( req->u.get_inkwell.out_y )
            *req->u.get_inkwell.out_y = app->ink.y;
        if( req->u.get_inkwell.out_atlas_index )
            *req->u.get_inkwell.out_atlas_index =
                ToriRSInkwell_AtlasIndex(style, colour, UIInk_Frame(&app->ink));
        return 1;
    }
    case UITREE_HOST_GET_INKWELL_SCENE:
        return UITreeSceneBridge_EnsureInkwell(&app->bridge);
    case UITREE_HOST_GET_STATIC_SPRITE_SCENE:
        return UITreeSceneBridge_StaticSpriteSceneId(
            &app->bridge, (enum StaticSpriteSlot)req->u.static_sprite.slot);
    case UITREE_HOST_GET_ENTITY_OVERLAYS:
        *req->u.get_entity_overlays.out_clip_x = app->world_emit_desc.x;
        *req->u.get_entity_overlays.out_clip_y = app->world_emit_desc.y;
        *req->u.get_entity_overlays.out_clip_w = app->world_emit_desc.w;
        *req->u.get_entity_overlays.out_clip_h = app->world_emit_desc.h;
        return app_build_entity_overlays(app, req->u.get_entity_overlays.out_items);
    case UITREE_HOST_GET_CANVAS_OVERLAYS:
        /* The canvas, not the world viewport -- that difference IS this
         * surface. Built here rather than beside the world list because the
         * two are asked for at different points of the emit walk, and the
         * plugin drawing into either has to see the same frame's state. */
        *req->u.get_entity_overlays.out_clip_x = 0;
        *req->u.get_entity_overlays.out_clip_y = 0;
        *req->u.get_entity_overlays.out_clip_w = UITREE_LAYOUT_ROOT_W;
        *req->u.get_entity_overlays.out_clip_h = UITREE_LAYOUT_ROOT_H;
        return app_build_canvas_overlays(app, req->u.get_entity_overlays.out_items);
    case UITREE_HOST_GET_CROSS_ACTIVE:
        return UICross_IsActive(&app->cross) ? 1 : 0;
    case UITREE_HOST_GET_CROSS_ATLAS_FRAME:
        return UICross_AtlasFrame(&app->cross);
    case UITREE_HOST_GET_CROSS_POSITION:
        if( req->u.get_cross_position.out_x )
            *req->u.get_cross_position.out_x = app->cross.x;
        if( req->u.get_cross_position.out_y )
            *req->u.get_cross_position.out_y = app->cross.y;
        return 1;
    case UITREE_HOST_GET_MINIMENU_VISIBLE:
        return app->interact.minimenu.visible ? 1 : 0;
    case UITREE_HOST_GET_MINIMENU_STATE:
        assert(req->u.get_minimenu_state.out);
        *req->u.get_minimenu_state.out = &app->interact.minimenu;
        return 1;
    case UITREE_HOST_GET_HOVERTEXT_STATE:
        assert(req->u.get_hovertext_state.out);
        *req->u.get_hovertext_state.out = &app->hover_text;
        return 1;
    case UITREE_HOST_MEASURE_TEXT:
    {
        struct ToriDraw_Font* font = ToriDraw_SceneFontGet(app->scene, req->u.measure_text.font_id);
        if( !font || !req->u.measure_text.text )
            return 0;
        return ToriDraw2D_MeasureString(font, req->u.measure_text.text);
    }
    /* Compass/minimap rotation, in the 0..2047 units the rotated sprite blit
     * takes. Normalized because ToriDraw_Sin/Cos assert that range. */
    case UITREE_HOST_GET_CAMERA_YAW:
        return ToriDraw_NormalizeAngle(app->world_camera.yaw);
    /* Minimap: the baked world map plus the camera's pivot inside it. The
     * widget box is fixed, so the map scrolls by moving this source anchor. */
    case UITREE_HOST_GET_MINIMAP_STATE:
    {
        /* Reference centers the minimap on the local player (minimapDraw
         * anchors at player.x/32), not the orbit eye; free-cam (offline)
         * keeps the eye anchor. Aboard, the player's own coordinates are
         * deck-local — pan by their position pushed out through the hull,
         * the same frame the dots and their centre use. */
        struct WorldEntity_Player* local_player = app_local_player(app);
        int anchor_x = local_player ? (int)local_player->draw_position.x : app->world_camera_pos.x;
        int anchor_z = local_player ? (int)local_player->draw_position.z : app->world_camera_pos.z;
        if( local_player )
            app_wev_actor_root_fine(app, &local_player->view_placement, &anchor_x, &anchor_z);
        if( app->world_map_scene_id <= 0 || !app->world || !app->world->minimap )
            return -1;
        minimap_compute_camera_src_anchor(
            anchor_x,
            anchor_z,
            app->world_map_w,
            app->world_map_h,
            app->world->minimap->width,
            app->world->minimap->height,
            req->u.get_minimap_state.out_src_anchor_x,
            req->u.get_minimap_state.out_src_anchor_y);
        return app->world_map_scene_id;
    }
    case UITREE_HOST_GET_MINIMAP_HIDDEN:
        return !(RS_MinimapPermissions(app->minimap_state) & RS_MINIMAP_DRAW_MAP);
    case UITREE_HOST_GET_COMPASS_HIDDEN:
        return !(RS_MinimapPermissions(app->minimap_state) & RS_MINIMAP_DRAW_COMPASS);
    case UITREE_HOST_GET_MULTIWAY:
        return app->multiway == 1;
    case UITREE_HOST_GET_REBOOT_TIMER:
        assert(req->u.get_reboot_timer.out_text);
        *req->u.get_reboot_timer.out_text = app_reboot_timer_text(app);
        return *req->u.get_reboot_timer.out_text != NULL;
    case UITREE_HOST_GET_TITLE_SCREEN:
        /* -1 rather than 0: 0 is a real screen (the front menu), so "not on
         * the title screen at all" needs a value of its own. */
        if( app->screen != APP_SCREEN_TITLE && app->screen != APP_SCREEN_CONNECTING )
            return -1;
        return (int)app->title.screen;
    case UITREE_HOST_GET_TITLE_FIELD:
        assert(req->u.get_title_field.config);
        assert(req->u.get_title_field.out_text);
        return app_title_field_line(app, req);
    case UITREE_HOST_GET_TITLE_MESSAGE:
    {
        int index = req->u.get_title_message.index;
        assert(req->u.get_title_message.out_text);
        if( index < 0 || index >= RS_TITLE_MESSAGE_LINES )
            return 0;
        *req->u.get_title_message.out_text = app->title.messages[index];
        return app->title.messages[index][0] != '\0';
    }
    case UITREE_HOST_GET_TITLE_TOGGLE:
    {
        int toggle = req->u.get_title_toggle.toggle;
        if( toggle < 0 || toggle >= RS_TITLE_TOGGLE_COUNT )
            return 0;
        return app->title.toggles[toggle];
    }
    case UITREE_HOST_GET_TITLE_PROGRESS:
        assert(req->u.get_title_progress.out_percent);
        assert(req->u.get_title_progress.out_text);
        *req->u.get_title_progress.out_percent = app->title.progress_percent;
        *req->u.get_title_progress.out_text = app->title.progress_text;
        return app->title.progress_percent >= 0;
    case UITREE_HOST_GET_TITLE_FLAMES:
    {
        int side = req->u.get_title_flames.side;
        struct TitleFlameGeometry geometry;
        assert(req->u.get_title_flames.out_scene_id);
        if( !app->flames || side < 0 || side >= TORIRS_FLAME_SIDES )
            return 0;

        /*
         * Where this era leans its fire, restated every frame because the
         * node is the only thing that knows and the simulation is shared.
         * Cheap -- four ints -- and it keeps the numbers in the profile
         * where the two revisions disagree about them.
         */
        geometry.bias = req->u.get_title_flames.bias;
        geometry.sway = req->u.get_title_flames.sway;
        geometry.run = req->u.get_title_flames.run;
        geometry.row = req->u.get_title_flames.row;
        TitleFlames_SetGeometry(app->flames, (enum TitleFlameSide)side, &geometry);
        /* Shared by both braziers -- the simulation is one field -- so the
         * two nodes state the same value and either may set it. */
        TitleFlames_SetBlur(app->flames, req->u.get_title_flames.blur);
        *req->u.get_title_flames.out_scene_id = side == TORIRS_FLAME_LEFT
                                                    ? UITREE_SCENE_TITLE_FLAME_LEFT_ID
                                                    : UITREE_SCENE_TITLE_FLAME_RIGHT_ID;
        return ToriDraw_SceneSpriteHas(app->scene, *req->u.get_title_flames.out_scene_id);
    }
    case UITREE_HOST_TITLE_ACTION:
    {
        enum RS_TitleAction action = (enum RS_TitleAction)req->u.title_action.action;
        /*
         * A tap on a field re-asks for the soft keyboard even when the focus
         * did not move -- and it usually has not, because the form always has
         * a focused field. The keyboard request is edge-triggered
         * (App_TakeTextInputChange pushes only changes), so after the player
         * hides the keyboard, "wanted" never changes and no tap could bring
         * it back. Forgetting what was last pushed makes the next take push
         * the current answer again; on a desktop that re-push is a no-op.
         */
        if( action == RS_TITLE_ACTION_FOCUS_USERNAME || action == RS_TITLE_ACTION_FOCUS_PASSWORD )
            app->text_input_effective = -1;
        if( RS_Title_HandleAction(&app->title, action) )
        {
            /* The form is greeted with a prompt rather than an empty box, and
             * the words are the profile's -- this era invites an email as
             * well as a display name, the 2004 one does not. A login reply
             * replaces it afterwards, which is the reference's behaviour and
             * why this is a message line rather than a static label. */
            if( action == RS_TITLE_ACTION_EXISTING_USER )
                RS_Title_SetMessages(
                    &app->title,
                    RS_LoginReplies_String(&app->login_replies, "enter_credentials"),
                    NULL,
                    NULL);
            app_title_state_changed(app);
        }
        return 0;
    }
    case UITREE_HOST_GET_MINIMAP_DOTS:
        return App_MinimapBuildDots(app, req->u.get_minimap_dots.out_dots);
    case UITREE_HOST_GET_WORLDMAP_TILES:
        return app_worldmap_build_tiles(app, req);
    case UITREE_HOST_GET_WORLDMAP_OVERVIEW:
        return app_worldmap_build_overview(app, req);
    case UITREE_HOST_GET_INV_SOURCE_SLOT:
        assert(req->u.get_inv_source_slot.out);
        if( !InvManager_GetSlot(
                &app->invs,
                req->u.get_inv_source_slot.source_id,
                req->u.get_inv_source_slot.slot,
                &slot) )
            return 0;
        req->u.get_inv_source_slot.out->obj_id = slot.obj_id;
        req->u.get_inv_source_slot.out->obj_count = slot.obj_count;
        req->u.get_inv_source_slot.out->scene_id = slot.scene_id;
        req->u.get_inv_source_slot.out->atlas_index = slot.atlas_index;
        return 1;
    /* CS1 answers come from the per-tick evaluation cached on each node, so
     * drawing never runs the VM and never has to handle a mid-frame yield. */
    case UITREE_HOST_IS_ACTIVE:
        if( !req->u.is_active.component )
            return 0;
        return req->u.is_active.component->cs1_active ? 1 : 0;
    case UITREE_HOST_EVAL_TEXT_PLACEHOLDER:
        if( !req->u.eval_text_placeholder.component ||
            req->u.eval_text_placeholder.script_idx < 0 ||
            req->u.eval_text_placeholder.script_idx >= UITREE_CS1_VALUE_MAX )
            return 0;
        return req->u.eval_text_placeholder.component
            ->cs1_values[req->u.eval_text_placeholder.script_idx];
    /* Tab + privacy-bar state lives on RS_UISlots (reference sideTab /
     * sideOverlayId / chat*Mode). A side modal suppresses the tab subtree by
     * answering -1, which no sidebar tabno matches. */
    case UITREE_HOST_GET_SELECTED_TAB:
        if( app->slots.side_modal_id != -1 )
            return -1;
        return app->slots.side_tab;
    case UITREE_HOST_SET_SELECTED_TAB:
        if( app->slots.side_tab != req->u.set_selected_tab.tabno )
        {
            app->slots.side_tab = req->u.set_selected_tab.tabno;
            app->need_redraw = 1;
        }
        /* Opening the tab the tutorial was pointing at is the instruction being
         * followed, so the blink stops. The reference notices this while
         * drawing the sidebar; noticing it at the selection itself is the same
         * rule asked once instead of every frame. */
        if( app->slots.flash_tab == req->u.set_selected_tab.tabno )
            app->slots.flash_tab = -1;
        return 1;
    case UITREE_HOST_GET_TAB_ENABLED:
        return RS_UISlots_TabEnabled(&app->slots, req->u.tab_enabled.tabno);
    case UITREE_HOST_GET_TAB_FLASH_HIDDEN:
        /* logic_cycle is the reference loopCycle, and the 20/10 split is its
         * own: visible for ten client ticks, hidden for ten. */
        return RS_UISlots_TabFlashHidden(&app->slots, req->u.tab_enabled.tabno, app->logic_cycle);
    case UITREE_HOST_GET_CHAT_FILTER_MODE:
        if( req->u.chat_filter.filter < 0 || req->u.chat_filter.filter >= RS_UI_CHAT_FILTER_COUNT )
            return 0;
        return app->slots.chat_filter_mode[req->u.chat_filter.filter];
    case UITREE_HOST_CYCLE_CHAT_FILTER_MODE:
        app->need_redraw = 1;
        return RS_UISlots_CycleChatFilter(&app->slots, req->u.chat_filter.filter);
    case UITREE_HOST_APPLY_BUTTON_CLICK:
        if( !req->u.apply_button_click.component )
            return 0;
        return RS_IF1_ApplyButtonClick(
            app,
            req->u.apply_button_click.component->component_id,
            RS_Minimenu_IfButtonActionForType(
                req->u.apply_button_click.component->behavior.button_type));
    case UITREE_HOST_GET_CHAT_STATE:
        assert(req->u.get_chat_state.out);
        *req->u.get_chat_state.out = &app->chat_view;
        return 1;
    case UITREE_HOST_GET_OBJ_NAME:
    {
        struct ToriRS_Objtype const* obj =
            CacheProvider_ObjtypeGet(app->provider, req->u.get_obj_name.obj_id);
        if( !obj || !req->u.get_obj_name.out || req->u.get_obj_name.cap <= 0 )
            return 0;
        strncpy(req->u.get_obj_name.out, obj->name, (size_t)req->u.get_obj_name.cap - 1);
        req->u.get_obj_name.out[req->u.get_obj_name.cap - 1] = '\0';
        if( req->u.get_obj_name.out_stackable )
            *req->u.get_obj_name.out_stackable = obj->stackable ? 1 : 0;
        /* Same two fields `oc_placeholder` reads (rs_cs2_host.c): a bank
         * placeholder is the record that carries a template. */
        if( req->u.get_obj_name.out_placeholder )
            *req->u.get_obj_name.out_placeholder = obj->placeholder_template >= 0 ? 1 : 0;
        return 1;
    }
    case UITREE_HOST_GET_INV_DRAG:
        /* Reports the slot only while it should ghost (trans 128), which is
         * from the press that armed it (reference Client.ts:8589 / :10207). */
        if( !app_inv_drag_ghosting(app) )
            return 0;
        if( req->u.get_inv_drag.out_source_id )
            *req->u.get_inv_drag.out_source_id = app->inv_drag_source_id;
        if( req->u.get_inv_drag.out_slot )
            *req->u.get_inv_drag.out_slot = app->inv_drag_from_slot;
        if( req->u.get_inv_drag.out_dx )
            *req->u.get_inv_drag.out_dx = app->inv_drag_dx;
        if( req->u.get_inv_drag.out_dy )
            *req->u.get_inv_drag.out_dy = app->inv_drag_dy;
        if( req->u.get_inv_drag.out_component_id )
            *req->u.get_inv_drag.out_component_id = app->inv_drag_com_id;
        return 1;
    case UITREE_HOST_GET_INV_COUNT_FONT:
        /* Reference draws stack counts with the client's p11 — same font (and
         * same load-on-miss self-heal) as the hitsplat numbers. */
        return app_hitsplat_font_scene_id(app);
    case UITREE_HOST_GET_INV_SELECTION:
        /* The armed (component, slot), for a cell that cannot name its own
         * addressing — a CS2 `cc_create`d item child, whose protocol identity
         * is its static parent's uid plus its index. Same shape as
         * GET_INV_DRAG: report the identity, let emit match the node. */
        if( !app->objsel.active )
            return 0;
        if( req->u.get_inv_selection.out_component_id )
            *req->u.get_inv_selection.out_component_id = app->objsel.component_id;
        if( req->u.get_inv_selection.out_slot )
            *req->u.get_inv_selection.out_slot = app->objsel.slot;
        return 1;
    case UITREE_HOST_GET_INV_SELECT_ICON:
        /* Reference TYPE_INV draw: only the slot armed for "Use" (useMode==1,
         * matching objSelectedSlot + objSelectedComId) gets the white outline.
         * The model is already resident (its plain icon is on screen), so the
         * white variant bakes on first request and is cached thereafter. */
        if( !app->objsel.active )
            return 0;
        if( app->objsel.component_id != req->u.get_inv_select_icon.com_id )
            return 0;
        if( app->objsel.slot != req->u.get_inv_select_icon.slot )
            return 0;
        return UITreeSceneBridge_EnsureObjIconSelected(
            &app->bridge,
            req->u.get_inv_select_icon.obj_id,
            req->u.get_inv_select_icon.count > 0 ? req->u.get_inv_select_icon.count : 1);
    case UITREE_HOST_GET_OBJ_ICON_PLAIN:
        return UITreeSceneBridge_EnsureObjIconPlain(
            &app->bridge,
            req->u.get_obj_icon_plain.obj_id,
            req->u.get_obj_icon_plain.count > 0 ? req->u.get_obj_icon_plain.count : 1);
    case UITREE_HOST_GET_OBJ_ICON_BORDERED:
        return UITreeSceneBridge_EnsureObjIconBordered(
            &app->bridge,
            req->u.get_obj_icon_bordered.obj_id,
            req->u.get_obj_icon_bordered.count > 0 ? req->u.get_obj_icon_bordered.count : 1);
    case UITREE_HOST_GET_IF_EVENTS:
        return (int)App_IfEventsGetEffective(app, req->u.get_if_events.com_id);
    /* The developer overlay's display list, handed over by pointer — the array
     * is owned by app->dbg_ui and outlives the frame. With the panel hidden the
     * list is empty and this returns 0, which is the whole cost of a declared
     * but switched-off overlay. */
    case UITREE_HOST_GET_DEBUG_OVERLAY:
    {
        int count = 0;
        if( !req->u.get_debug_overlay.out_prims )
            return 0;
        /*
         * Two chrome instances, one display list.
         *
         * The emit layer takes a single pointer-and-count for the whole
         * overlay, so the alternative to concatenating here would be a second
         * overlay NODE -- a second entry in the tree, a second pass, a second
         * z-order question to answer. Concatenation answers it instead: the
         * plugin window's prims go last and therefore draw on top, which is
         * what a window a player opened should do over a developer readout.
         *
         * The merge only runs when one of the two actually rebuilt; a steady
         * frame is still the one pointer copy the retained design promises.
         */
        *req->u.get_debug_overlay.out_prims = app_chrome_merged_prims(app, &count);
        return count;
    }
    default:
        return 0;
    }
}

/* ---- Retained UITree host-input publication --------------------------- *
 *
 * Host requests copy ambient App state into otherwise-retainable descriptors.
 * The emit walk records which coarse domains it actually read; this publication
 * fence gives those domains semantic versions without making each state writer
 * know which nodes (or even which open interface) consumed it.
 *
 * These hashes are not render caches. They are compact compare-before-bump
 * snapshots of the values host requests can expose. Event-driven sources such
 * as InvManager also bump their domain directly, while the snapshot closes
 * over local selection state which is not owned by that manager. */
static void
app_ui_host_publish_inputs(struct App* app)
{
    uint64_t signature[UITREE_HOST_INPUT_DOMAIN_COUNT];
    struct WorldEntity_Player const* local;
    struct UIMinimenu const* menu;
    int menu_count;
    int ghosting;

    assert(app);
    for( int domain = 0; domain < UITREE_HOST_INPUT_DOMAIN_COUNT; domain++ )
        signature[domain] = UITree_InputSignatureInt(UITREE_INPUT_SIGNATURE_OFFSET, domain + 1);

    /* CAMERA: everything used by yaw-based chrome and world projection. The
     * local player is the minimap anchor when present; free camera position is
     * the fallback. */
    signature[UITREE_HOST_INPUT_CAMERA] = UITree_InputSignatureInt(
        signature[UITREE_HOST_INPUT_CAMERA], ToriDraw_NormalizeAngle(app->world_camera.yaw));
    signature[UITREE_HOST_INPUT_CAMERA] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], app->world_camera.pitch);
    signature[UITREE_HOST_INPUT_CAMERA] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], app->world_camera_pos.x);
    signature[UITREE_HOST_INPUT_CAMERA] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], app->world_camera_pos.y);
    signature[UITREE_HOST_INPUT_CAMERA] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], app->world_camera_pos.z);
    local = app_local_player(app);
    signature[UITREE_HOST_INPUT_CAMERA] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], local != NULL);
    if( local )
    {
        signature[UITREE_HOST_INPUT_CAMERA] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], (int)local->draw_position.x);
        signature[UITREE_HOST_INPUT_CAMERA] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_CAMERA], (int)local->draw_position.z);
    }

    /* POINTER: hash only visible/observable phases. Inactive cross/menu/hover
     * scratch may move without changing a descriptor and should not defeat a
     * quiet retained frame. */
    signature[UITREE_HOST_INPUT_POINTER] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], UICross_IsActive(&app->cross));
    if( UICross_IsActive(&app->cross) )
    {
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->cross.x);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->cross.y);
        signature[UITREE_HOST_INPUT_POINTER] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_POINTER], UICross_AtlasFrame(&app->cross));
    }
    menu = &app->interact.minimenu;
    signature[UITREE_HOST_INPUT_POINTER] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->visible);
    if( menu->visible )
    {
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->x);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->y);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->width);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->height);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->hovered_option);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu->font_id);
        signature[UITREE_HOST_INPUT_POINTER] = UITree_InputSignatureBytes(
            signature[UITREE_HOST_INPUT_POINTER], &menu->layout, sizeof(menu->layout));
        menu_count = menu->option_count;
        if( menu_count < 0 )
            menu_count = 0;
        if( menu_count > UITREE_MINIMENU_MAX_OPTIONS )
            menu_count = UITREE_MINIMENU_MAX_OPTIONS;
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], menu_count);
        for( int i = 0; i < menu_count; i++ )
        {
            struct UIMinimenuOption const* option = &menu->options[i];
            signature[UITREE_HOST_INPUT_POINTER] =
                UITree_InputSignatureString(signature[UITREE_HOST_INPUT_POINTER], option->text);
            signature[UITREE_HOST_INPUT_POINTER] = UITree_InputSignatureBytes(
                signature[UITREE_HOST_INPUT_POINTER], &option->action, sizeof(option->action));
            signature[UITREE_HOST_INPUT_POINTER] = UITree_InputSignatureBytes(
                signature[UITREE_HOST_INPUT_POINTER],
                &option->action_index,
                sizeof(option->action_index));
            signature[UITREE_HOST_INPUT_POINTER] = UITree_InputSignatureBytes(
                signature[UITREE_HOST_INPUT_POINTER], &option->pick, sizeof(option->pick));
        }
    }
    signature[UITREE_HOST_INPUT_POINTER] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->hover_text.visible);
    if( app->hover_text.visible )
    {
        signature[UITREE_HOST_INPUT_POINTER] = UITree_InputSignatureBytes(
            signature[UITREE_HOST_INPUT_POINTER],
            &app->hover_text.x,
            sizeof(app->hover_text.x) * 5);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureString(signature[UITREE_HOST_INPUT_POINTER], app->hover_text.text);
    }
    ghosting = app_inv_drag_ghosting(app);
    signature[UITREE_HOST_INPUT_POINTER] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], ghosting);
    if( ghosting )
    {
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->inv_drag_com_id);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->inv_drag_source_id);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->inv_drag_from_slot);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->inv_drag_dx);
        signature[UITREE_HOST_INPUT_POINTER] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_POINTER], app->inv_drag_dy);
    }

    /* CLIENT_STATE: selected/available tabs, chat presentation and the server
     * IF_SETEVENTS overrides copied into host-produced menu descriptors. */
    signature[UITREE_HOST_INPUT_CLIENT_STATE] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_CLIENT_STATE], &app->slots, sizeof(app->slots));
    signature[UITREE_HOST_INPUT_CLIENT_STATE] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_CLIENT_STATE], &app->chat_view, sizeof(app->chat_view));
    signature[UITREE_HOST_INPUT_CLIENT_STATE] = UITree_InputSignatureInt(
        signature[UITREE_HOST_INPUT_CLIENT_STATE], app->if_events.count);
    if( app->if_events.count > 0 )
        signature[UITREE_HOST_INPUT_CLIENT_STATE] = UITree_InputSignatureBytes(
            signature[UITREE_HOST_INPUT_CLIENT_STATE],
            app->if_events.ranges,
            (size_t)app->if_events.count * sizeof(*app->if_events.ranges));

    /* INVENTORY: container contents publish through the InvManager callback;
     * selection and drag addressing live on App and need this small snapshot. */
    signature[UITREE_HOST_INPUT_INVENTORY] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_INVENTORY], &app->invs.selection, sizeof(app->invs.selection));
    signature[UITREE_HOST_INPUT_INVENTORY] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_INVENTORY], &app->objsel, sizeof(app->objsel));
    signature[UITREE_HOST_INPUT_INVENTORY] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_INVENTORY],
        &app->inv_drag_com_id,
        sizeof(app->inv_drag_com_id) * 4);

    /* ASSETS: owner-side mutation revisions catch arrivals before a skipped
     * host request gets another chance to publish them, plus same-id registry
     * replacements which map cardinality cannot see. Provider model/sprite
     * streaming may conservatively cause an extra full UI walk; three scalar
     * reads are still cheaper and more reliable than scanning the registries. */
    signature[UITREE_HOST_INPUT_ASSETS] = UITree_InputSignatureU64(
        signature[UITREE_HOST_INPUT_ASSETS],
        app->provider ? CacheProvider_UIAssetRevision(app->provider) : 0);
    signature[UITREE_HOST_INPUT_ASSETS] = UITree_InputSignatureU64(
        signature[UITREE_HOST_INPUT_ASSETS], UITreeSceneBridge_AssetRevision(&app->bridge));
    signature[UITREE_HOST_INPUT_ASSETS] = UITree_InputSignatureU64(
        signature[UITREE_HOST_INPUT_ASSETS],
        app->scene ? ToriDraw_SceneUIAssetRevision(app->scene) : 0);

    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->minimap_state);
    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->multiway);
    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->world_map_scene_id);
    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->world_map_w);
    signature[UITREE_HOST_INPUT_WORLD] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->world_map_h);
    signature[UITREE_HOST_INPUT_WORLD] = UITree_InputSignatureInt(
        signature[UITREE_HOST_INPUT_WORLD], app->world && app->world->load_complete);
    if( app->world && app->world->minimap )
    {
        signature[UITREE_HOST_INPUT_WORLD] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->world->minimap->width);
        signature[UITREE_HOST_INPUT_WORLD] =
            UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_WORLD], app->world->minimap->height);
    }

    /* Hash visible animation phases, not raw clocks: an inactive cross and a
     * non-flashing tab do not change their descriptors as cycles advance. */
    signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
        signature[UITREE_HOST_INPUT_ANIMATION], UICross_IsActive(&app->cross));
    if( UICross_IsActive(&app->cross) )
        signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_ANIMATION], UICross_AtlasFrame(&app->cross));
    signature[UITREE_HOST_INPUT_ANIMATION] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_ANIMATION], app->reboot_timer != 0);
    if( app->reboot_timer != 0 )
        signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_ANIMATION],
            app->reboot_timer / APP_LOGIC_CYCLES_PER_SECOND);
    /*
     * The login caret's blink phase, and only the phase.
     *
     * Hashing logic_cycle itself would bump the animation epoch every frame
     * and the title screen would never retain anything; hashing the half of
     * the period the caret is in flips the epoch exactly twice per blink,
     * which is how often the screen actually changes. The period is the
     * widget's (revconfig caret_blink=), so the client asks the tree for it.
     */
    if( app->screen == APP_SCREEN_TITLE || app->screen == APP_SCREEN_CONNECTING )
    {
        int blink = app_title_caret_blink(app);
        signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_ANIMATION],
            blink > 0 ? (int)(app->logic_cycle % (uint64_t)blink) < blink / 2 : 0);
    }
    /*
     * The fire's step counter, while there is a fire.
     *
     * always_dirty on the node is only half of the contract and does not
     * work alone: it keeps the node's own descriptor out of the retained
     * list, but nothing rebuilds the FRAME unless an input epoch moves, so
     * a title screen whose only animation is the braziers retains the very
     * first frame forever. That is exactly what shipped -- the fire ran, the
     * sprite was re-uploaded every step, and the screen kept showing the
     * seed row from step one, a bright line at the bowl rim and nothing
     * above it.
     *
     * The counter and not the clock: it moves once per fixed 35 ms step, so
     * the epoch flips exactly when the pixels do rather than every frame.
     */
    if( app->flames )
        signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_ANIMATION], app->flames->update_index);
    signature[UITREE_HOST_INPUT_ANIMATION] =
        UITree_InputSignatureInt(signature[UITREE_HOST_INPUT_ANIMATION], app->slots.flash_tab);
    if( app->slots.flash_tab >= 0 )
        signature[UITREE_HOST_INPUT_ANIMATION] = UITree_InputSignatureInt(
            signature[UITREE_HOST_INPUT_ANIMATION],
            RS_UISlots_TabFlashHidden(&app->slots, app->slots.flash_tab, app->logic_cycle));

    /* Same-frame world/plugin overlay arrays are refreshed by source-tagged
     * standing records, including sources currently returning zero items.
     * Chrome is retained data, so its exact build serials participate here. */
    signature[UITREE_HOST_INPUT_OVERLAYS] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_OVERLAYS],
        &app->dbg_ui.build_serial,
        sizeof(app->dbg_ui.build_serial));
    signature[UITREE_HOST_INPUT_OVERLAYS] = UITree_InputSignatureBytes(
        signature[UITREE_HOST_INPUT_OVERLAYS],
        &app->plugin_ui.build_serial,
        sizeof(app->plugin_ui.build_serial));
    for( int domain = 0; domain < UITREE_HOST_INPUT_DOMAIN_COUNT; domain++ )
        (void)UITree_HostPublishInputSignature(
            &app->ui_host, (enum UITreeHostInputDomain)domain, signature[domain]);
}

static void
app_inv_ui_host_change(
    void* userdata,
    int container_id)
{
    struct App* app = (struct App*)userdata;
    (void)container_id;

    UITree_HostInputsChanged(&app->ui_host, UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_INVENTORY));
    app->need_redraw = 1;
}

/* ---- Inventory obj-icon reconcile ------------------------------------- *
 *
 * Server UPDATE_INV_FULL/PARTIAL (rs_gameproto_exec.c) write item ids into the
 * inv containers but leave scene_id = INV_MANAGER_NO_SCENE_ID, because the
 * inventory model may not be resident and rasterizing needs it loaded. The
 * emit path (emit_rs_inv_slots) only draws a slot when scene_id >= 0, so those
 * items never appear. This mirrors task_interface_open's seed-time icon step
 * (load the models, then UITreeSceneBridge_EnsureObjIcon and stamp the scene id
 * back) but is driven per tick off the live containers, so items that arrive
 * after the interface is open still get icons — the missing lazy path the
 * exec handlers' comment promised.
 *
 * A slot whose model can never be built is stamped with a distinct sentinel so
 * the per-tick scan stops re-enqueueing it; the server replacing the item
 * resets scene_id to NO_SCENE_ID and re-arms the reconcile. */
#define APP_INV_ICON_BATCH_MAX 64
#define APP_INV_ICON_SCENE_FAILED (-2)
/* Reconcile passes a slot may spend waiting for its objtype, model and
 * textures before the icon is given up on. One pass per tick, so this is a
 * two-second ceiling on a cache that answers over the network. */
#define APP_INV_ICON_ATTEMPT_MAX 100

struct Task_InvIconReconcile
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int obj_ids[APP_INV_ICON_BATCH_MAX];
    int counts[APP_INV_ICON_BATCH_MAX];
    int n;
    int published_change;
};

static int
Task_InvIconReconcile_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_InvIconReconcile* self = (struct Task_InvIconReconcile*)base;
    struct App* app = self->app;
    (void)io;

    PT_BEGIN(&self->pt);

    self->published_change = 0;
    /* Collect the batch that still needs a model load (bounded; leftovers are
     * caught by the next tick's scan once this pass stamps its slots). */
    self->n = 0;
    for( int ci = 0; ci < app->invs.container_count && self->n < APP_INV_ICON_BATCH_MAX; ci++ )
    {
        struct InvContainer const* c = &app->invs.containers[ci];
        if( !c->slots )
            continue;
        for( int s = 0; s < c->slot_count && self->n < APP_INV_ICON_BATCH_MAX; s++ )
        {
            struct InvSlot const* slot = &c->slots[s];
            if( slot->obj_id > 0 && slot->scene_id == INV_MANAGER_NO_SCENE_ID )
            {
                self->obj_ids[self->n] = slot->obj_id;
                self->counts[self->n] = slot->obj_count > 0 ? slot->obj_count : 1;
                self->n++;
            }
        }
    }
    if( self->n > 0 )
        PT_TASK_AWAITSELF_IF(
            CreateTask_ObjModelLoad(app->provider, self->obj_ids, self->counts, self->n));

    /*
     * Rasterize the pending slots whose parts have actually landed, and leave
     * the rest pending for a later pass.
     *
     * Asking `ObjModelLoad_NeedsWork` first is the whole fix for icons that go
     * missing at random. The batch above is capped, so on a bank (1410 slots)
     * or a busy boot most pending slots never had their models requested; an
     * UPDATE_INV that lands while this task awaits adds slots it never asked
     * for either; and against an on-demand cache the answer arrives over the
     * network whenever it arrives. In all three cases the raster below used to
     * find nothing resident, stamp APP_INV_ICON_SCENE_FAILED, and the per-tick
     * scan — which only looks at INV_MANAGER_NO_SCENE_ID — would never come
     * back. The slot stayed blank until the server replaced the item.
     *
     * Baking too early is the same bug wearing the other face: an icon rastered
     * before its model's textures arrive is cached blank against that
     * (obj, count) key for the rest of the session, which draws as a stack
     * count with no item under it.
     *
     * The attempt counter bounds the wait. An obj whose model genuinely never
     * resolves keeps NeedsWork true forever, and without a bound it would
     * re-arm this reconcile every tick for the rest of the session.
     */
    for( int ci = 0; ci < app->invs.container_count; ci++ )
    {
        struct InvContainer* c = &app->invs.containers[ci];
        if( !c->slots )
            continue;
        for( int s = 0; s < c->slot_count; s++ )
        {
            struct InvSlot* slot = &c->slots[s];
            int count;
            int scene_id;
            if( slot->obj_id <= 0 || slot->scene_id != INV_MANAGER_NO_SCENE_ID )
                continue;
            count = slot->obj_count > 0 ? slot->obj_count : 1;
            scene_id = UITreeSceneBridge_EnsureObjIcon(&app->bridge, slot->obj_id, count);
            if( scene_id >= 0 )
            {
                slot->scene_id = scene_id;
                slot->atlas_index = 0;
                self->published_change = 1;
                continue;
            }
            /* Could not build it *yet*: leave the slot pending so the next
             * pass batches it, until the attempts run out. */
            if( ObjModelLoad_NeedsWork(app->provider, slot->obj_id, count) &&
                ++slot->icon_attempts < APP_INV_ICON_ATTEMPT_MAX )
                continue;
            slot->scene_id = APP_INV_ICON_SCENE_FAILED;
            slot->atlas_index = 0;
            self->published_change = 1;
        }
    }

    if( self->published_change )
        UITree_HostInputsChanged(
            &app->ui_host,
            UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_INVENTORY) |
                UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_ASSETS));
    app->inv_icon_reconcile_inflight = 0;
    app->need_redraw = 1;
    PT_END(&self->pt);
}

static void
Task_InvIconReconcile_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_InvIconReconcile_VTable = {
    .run = Task_InvIconReconcile_Run,
    .free = Task_InvIconReconcile_Free,
};

/* Per-tick hook: enqueue one reconcile if any item icon is still unresolved and
 * none is already running. Serial on the exec pipeline so it applies after the
 * inventory packets that dirtied the slots. */
static void
app_inv_icon_reconcile_tick(struct App* app)
{
    struct Task_InvIconReconcile* task;

    if( app->inv_icon_reconcile_inflight || !InvManager_HasUnbakedIcon(&app->invs) )
        return;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_InvIconReconcile_VTable;
    strncpy(task->task.name, "InvIconReconcile", sizeof(task->task.name) - 1);
    task->app = app;
    PT_INIT(&task->pt);
    app->inv_icon_reconcile_inflight = 1;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Wrapper protothread that owns one CS1 evaluation pass: awaits the eval
 * task (which may itself yield for pack loads), then clears the in-flight
 * gate and requests a redraw when a cached result changed. */
struct Task_AppCS1Eval
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
};

static int
Task_AppCS1Eval_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppCS1Eval* self = (struct Task_AppCS1Eval*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);
    app->cs1_host.eval_dirty = false;
    PT_TASK_AWAITSELF_IF(CreateTask_CS1Eval(&app->cs1_host));
    app->cs1_eval_inflight = 0;
    if( app->cs1_host.eval_dirty )
        app->need_redraw = 1;
    PT_END(&self->pt);
}

static void
Task_AppCS1Eval_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_AppCS1Eval_VTable = {
    .run = Task_AppCS1Eval_Run,
    .free = Task_AppCS1Eval_Free,
};

/* Request a CS1 evaluation pass; at most one is ever in flight (the tick
 * re-requests every 20ms anyway, so a busy pass simply coalesces). Never
 * blocks — the frame pump drives it. */
static void
app_request_cs1_eval(struct App* app)
{
    struct Task_AppCS1Eval* task;

    if( app->cs1_eval_inflight )
        return;
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppCS1Eval_VTable;
    strncpy(task->task.name, "AppCS1Eval", sizeof(task->task.name) - 1);
    task->app = app;
    PT_INIT(&task->pt);
    app->cs1_eval_inflight = 1;
    ToriRS_TaskQueue_Add(app->runner.queue, &task->task);
}

/* Scene models reference textures by face id, but the ToriDraw texture map
 * starts empty (reference: textures load on demand and faces skip-render
 * until they land). The ids come from model construction itself
 * (ToriDraw_ModelTextureWantsTake) — whatever built a model reported the
 * textures it needs — so this costs nothing per tick when no geometry was
 * built. Queue the loads and remember the ids; app_sync_textures_poll
 * publishes them into the scene as the loads land. Ids that fail stay marked
 * in the bridge and are never re-requested. */
/* #region agent log — TORIRS_TEX_TRACE=1 narrates the whole want -> request ->
 * provider -> publish handoff, one line per id per decision. The gap between a
 * texture the loader created and a texture the raster can see has no other
 * observer: every stage on the way silently `continue`s. */
int
app_tex_trace_enabled(void)
{
    static int enabled = -1;
    if( enabled < 0 )
        enabled = getenv("TORIRS_TEX_TRACE") ? 1 : 0;
    return enabled;
}

static int g_tex_trace_frame = 0;

int
app_tex_trace_frame(void)
{
    return g_tex_trace_frame;
}
/* #endregion */

static void
app_sync_textures(struct App* app)
{
    int ids[256];
    int ready[256];
    int ready_count = 0;
    int id_count;

    id_count = ToriDraw_ModelTextureWantsTake(ids, 256);
    if( id_count == 0 )
        return;
    if( getenv("TORIRS_TEX_DEBUG") )
    {
        TORIRS_LOG("tex_wants drained %d:", id_count);
        for( int i = 0; i < id_count; i++ )
            TORIRS_LOG(" %d", ids[i]);
        TORIRS_LOG("\n");
    }

    for( int i = 0; i < id_count; i++ )
    {
        int const id = ids[i];
        int already_pending = 0;

        if( id >= 0 && id < 2048 && app->bridge.texture_failed[id] )
        {
            if( app_tex_trace_enabled() )
                TORIRS_ERR("tex_trace: want id=%d -> skip (already failed)\n", id);
            continue;
        }
        if( UITreeSceneBridge_TextureResident(&app->bridge, id) )
        {
            if( app_tex_trace_enabled() )
                TORIRS_LOG("tex_trace: want id=%d -> skip (already resident)\n", id);
            continue;
        }

        /* A model may be rebuilt while its first texture request is still in
         * flight. Do the pending-set test before creating the task; the old
         * order queued another decoder for every rebuild and only deduplicated
         * the publish list afterwards. */
        already_pending = AsyncPendingTextures_Has(&app->tex_pending, id);
        if( already_pending )
        {
            if( app_tex_trace_enabled() )
                TORIRS_LOG("tex_trace: want id=%d -> skip (already pending)\n", id);
            continue;
        }

        /* Already decoded — publish it now, in the same tick the geometry that
         * wants it was built. Deferring to app_sync_textures_poll costs a frame,
         * and the frame it costs is the one that first draws the new models: the
         * raster skips every textured face whose texture is not in the scene map
         * yet. The QBD arena load spent that frame skipping ~1000 faces with both
         * of its textures sitting decoded in the provider. Loads that really are
         * in flight still go through the pending list below. */
        if( CacheProvider_TextureHas(app->provider, id) && app->bridge.scene )
        {
            if( app_tex_trace_enabled() )
                TORIRS_LOG("tex_trace: want id=%d -> already in provider\n", id);
            ready[ready_count++] = id;
            continue;
        }

        {
            struct ToriRS_Task* task = CreateTask_TextureLoad(app->provider, id);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
            if( app_tex_trace_enabled() )
                TORIRS_ERR(
                    "tex_trace: want id=%d -> load task %s\n",
                    id,
                    task ? "queued" : "REFUSED (provider returned no task)");
        }
        if( !AsyncPendingTextures_Add(&app->tex_pending, id) && app_tex_trace_enabled() )
            TORIRS_LOG("tex_trace: want id=%d -> DROPPED (pending list full)\n", id);
    }

    if( ready_count > 0 )
    {
        int published = UITreeSceneBridge_PublishTextures(&app->bridge, ready, ready_count);
        if( app_tex_trace_enabled() )
            TORIRS_LOG(
                "tex_trace: immediate publish %d ready -> %d published\n", ready_count, published);
        if( published )
            app->need_redraw = 1;
    }
}

/* Per-frame: publish any pending textures that finished loading; keep only
 * the ones still in flight (present in neither the provider nor the bridge's
 * failed set). */
static void
app_sync_textures_poll(struct App* app)
{
    int ready[512];
    int ready_count = 0;
    int kept = 0;
    int const queue_idle = !app->runner.queue || !app->runner.queue->head;

    if( app->tex_pending.count == 0 )
        return;

    for( int i = 0; i < app->tex_pending.count; i++ )
    {
        int id = app->tex_pending.ids[i];

        if( id < 0 || id >= 2048 || app->bridge.texture_failed[id] )
        {
            if( app_tex_trace_enabled() )
                TORIRS_ERR("tex_trace: poll id=%d -> dropped (failed/out of range)\n", id);
            continue;
        }
        if( UITreeSceneBridge_TextureResident(&app->bridge, id) )
        {
            if( app_tex_trace_enabled() )
                TORIRS_LOG("tex_trace: poll id=%d -> dropped (resident)\n", id);
            continue;
        }
        if( CacheProvider_TextureHas(app->provider, id) )
        {
            ready[ready_count++] = id;
            continue;
        }

        /* A missing provider entry does not mean a failed texture while its
         * async load is still queued. Publishing it here used to mark it
         * failed on the very next frame, before a busy task runner reached the
         * request; every affected model face was then skipped forever. Once
         * the queue drains, absence is a real terminal load failure. */
        if( queue_idle )
        {
            app->bridge.texture_failed[id] = 1;
            if( app_tex_trace_enabled() )
                TORIRS_ERR(
                    "tex_trace: poll id=%d -> MARKED FAILED (queue idle, not in provider)\n", id);
        }
        else
        {
            app->tex_pending.ids[kept++] = id;
        }
    }
    AsyncPendingTextures_Keep(&app->tex_pending, kept);

    if( ready_count > 0 )
    {
        int published = UITreeSceneBridge_PublishTextures(&app->bridge, ready, ready_count);
        if( app_tex_trace_enabled() )
            TORIRS_LOG(
                "tex_trace: publish %d ready -> %d published (%d still pending)\n",
                ready_count,
                published,
                app->tex_pending.count);
        if( published )
            app->need_redraw = 1;
    }
}

/* Load and resolve only the config chain. Body and interface-head consumers
 * deliberately share this step, then await their own distinct model sets. */
struct Task_NpcMultiResolve
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int base_npc_id;
    int* out_npc_id;
    int current_npc_id;
    int depth;
};

static int
Task_NpcMultiResolve_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_NpcMultiResolve* self = (struct Task_NpcMultiResolve*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    self->current_npc_id = self->base_npc_id;
    *self->out_npc_id = self->base_npc_id;

    for( self->depth = 0; self->depth <= TORIRS_NPC_MULTI_MAX_DEPTH && self->current_npc_id >= 0;
         self->depth++ )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_NpcLoad(app->provider, self->current_npc_id));
        {
            struct ToriRS_Npctype* npctype =
                CacheProvider_NpctypeGet(app->provider, self->current_npc_id);
            int next;

            if( !npctype || npctype->transform_count <= 0 || !npctype->transforms )
                break;
            next = VarPManager_ResolveTransform(
                &app->varps,
                npctype->transforms,
                npctype->transform_count,
                npctype->transform_varbit,
                npctype->transform_varp);
            if( next < 0 )
            {
                self->current_npc_id = -1;
                break;
            }
            if( next == self->current_npc_id || self->depth == TORIRS_NPC_MULTI_MAX_DEPTH )
                break;
            self->current_npc_id = next;
        }
    }

    *self->out_npc_id = self->current_npc_id;
    PT_END(&self->pt);
}

static void
Task_NpcMultiResolve_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_NpcMultiResolve_VTable = {
    .run = Task_NpcMultiResolve_Run,
    .free = Task_NpcMultiResolve_Free,
};

static struct ToriRS_Task*
CreateTask_NpcMultiResolve(
    struct App* app,
    int base_npc_id,
    int* out_npc_id)
{
    struct Task_NpcMultiResolve* task;

    assert(app && out_npc_id);
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_NpcMultiResolve_VTable;
    strncpy(task->task.name, "NpcMultiResolve", sizeof(task->task.name) - 1);
    task->app = app;
    task->base_npc_id = base_npc_id;
    task->out_npc_id = out_npc_id;
    PT_INIT(&task->pt);
    return &task->task;
}

/*
 * A multiNpc cannot be resolved before its wrapper config is resident. The
 * packet path used to try exactly that, get a cache miss, and permanently
 * spawn the model-less wrapper. Keep the config walk and its asset waits in a
 * reusable task so initial adds, server retypes and local-var remorphs all obey
 * the same cold-cache-safe rule.
 */
struct Task_NpcMultiLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int base_npc_id;
    int* out_npc_id;
    int resolved_npc_id;
    int model_i;
    int seq_i;
    /** Body parts and movement sequences queued and not yet ended. */
    int pending;
};

static int
Task_NpcMultiLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_NpcMultiLoad* self = (struct Task_NpcMultiLoad*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    PT_TASK_AWAITSELF_IF(
        CreateTask_NpcMultiResolve(app, self->base_npc_id, &self->resolved_npc_id));

    if( self->resolved_npc_id >= 0 )
    {
        /*
         * The terminal config was loaded by the walk above. Load its complete
         * body and movement set before the caller mounts/replaces the model.
         *
         * The body parts AND the five movement sequences go out TOGETHER.
         * Awaiting them one at a time is one network round trip per part on a
         * cache being streamed, and an npc body is commonly four models and
         * five sequences each several reads deep -- for a roster of a thousand
         * that is the difference between a world that populates and one that
         * trickles. They are independent reads with a common consumer, which
         * is exactly the shape the runner can overlap: queued as siblings,
         * each gets its own IO slot and they are all on the wire at once.
         *
         * Then joined, not waited on for residency: a part the cache cannot
         * serve never becomes resident, and the residency wait this replaced
         * spent its whole budget on one -- while a loader that fails still
         * ENDS, which is what the join counts. The npc then spawns missing the
         * part, exactly as the old per-part await did.
         */
        {
            struct ToriRS_Npctype* npctype =
                CacheProvider_NpctypeGet(app->provider, self->resolved_npc_id);
            for( self->model_i = 0; npctype && self->model_i < npctype->models_count;
                 self->model_i++ )
            {
                if( npctype->models[self->model_i] >= 0 )
                    ToriRS_TaskQueue_AddJoined(
                        app->runner.queue,
                        CreateTask_ModelLoad(app->provider, npctype->models[self->model_i]),
                        &self->pending);
            }
            if( npctype )
            {
                int const seqs[5] = {
                    npctype->readyanim,  npctype->walkanim,   npctype->walkanim_b,
                    npctype->walkanim_r, npctype->walkanim_l,
                };
                for( self->seq_i = 0; self->seq_i < 5; self->seq_i++ )
                {
                    if( seqs[self->seq_i] >= 0 )
                        ToriRS_TaskQueue_AddJoined(
                            app->runner.queue,
                            CreateTask_SequenceLoad(app->provider, app->scene, seqs[self->seq_i]),
                            &self->pending);
                }
            }
        }
        PT_TASK_JOIN(pending);
    }

    *self->out_npc_id = self->resolved_npc_id;
    PT_END(&self->pt);
}

static void
Task_NpcMultiLoad_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_NpcMultiLoad_VTable = {
    .run = Task_NpcMultiLoad_Run,
    .free = Task_NpcMultiLoad_Free,
};

struct ToriRS_Task*
CreateTask_NpcMultiLoad(
    struct App* app,
    int base_npc_id,
    int* out_npc_id)
{
    struct Task_NpcMultiLoad* task;

    assert(app && out_npc_id);
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_NpcMultiLoad_VTable;
    strncpy(task->task.name, "NpcMultiLoad", sizeof(task->task.name) - 1);
    task->app = app;
    task->base_npc_id = base_npc_id;
    task->out_npc_id = out_npc_id;
    PT_INIT(&task->pt);
    return &task->task;
}

static int
app_npc_transform_depends_on_varp(
    struct App* app,
    int base_npc_id,
    int varp_id)
{
    int npc_id = base_npc_id;

    for( int depth = 0; depth <= TORIRS_NPC_MULTI_MAX_DEPTH && npc_id >= 0; depth++ )
    {
        struct ToriRS_Npctype* npc = CacheProvider_NpctypeGet(app->provider, npc_id);
        int next;

        if( !npc || npc->transform_count <= 0 || !npc->transforms )
            return 0;
        if( varp_id < 0 || npc->transform_varp == varp_id )
            return 1;
        if( npc->transform_varbit >= 0 && npc->transform_varbit < app->varps.varbit_count )
        {
            struct VarBitType const* vb = &app->varps.varbit_types[npc->transform_varbit];
            if( vb->basevar == varp_id )
                return 1;
        }
        next = VarPManager_ResolveTransform(
            &app->varps,
            npc->transforms,
            npc->transform_count,
            npc->transform_varbit,
            npc->transform_varp);
        if( next < 0 || next == npc_id )
            return 0;
        npc_id = next;
    }
    return 0;
}

struct Task_AppNpcTransform
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int world_idx;
    int element_id;
    int server_slot;
    int base_npc_id;
    int resolved_npc_id;
};

static int
Task_AppNpcTransform_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppNpcTransform* self = (struct Task_AppNpcTransform*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);
    PT_TASK_AWAITSELF_IF(CreateTask_NpcMultiLoad(app, self->base_npc_id, &self->resolved_npc_id));
    {
        int world_idx = self->world_idx;
        int element_id = self->element_id;
        struct WorldEntity_NPC* npc;

        if( self->server_slot >= 0 &&
            !RS_EntitySync_FindNpc(&app->esync, self->server_slot, &world_idx, &element_id) )
            world_idx = -1;
        npc = world_idx >= 0 ? World_EntityPoolGet(&app->world->entities.npc, world_idx) : NULL;
        /* Asset IO can yield for several frames. Revalidate the exact entity
         * and wrapper so a despawn/slot reuse or server CHANGE_TYPE cannot be
         * overwritten by this older local-var refresh. */
        if( npc && npc->element_id == self->element_id && npc->base_npc_id == self->base_npc_id )
        {
            int hidden = self->resolved_npc_id < 0;
            int effective = hidden ? self->base_npc_id : self->resolved_npc_id;
            if( npc->npc_id != effective )
                App_WorldApplyNpcType(
                    app, world_idx, npc->element_id, effective, self->base_npc_id);
            npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
            if( npc )
                npc->multinpc_hidden = hidden != 0;
        }
    }
    PT_END(&self->pt);
}

static void
Task_AppNpcTransform_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_AppNpcTransform_VTable = {
    .run = Task_AppNpcTransform_Run,
    .free = Task_AppNpcTransform_Free,
};

static void
app_varp_refresh_npc_transforms(
    struct App* app,
    int varp_id)
{
    struct World_EntityPool* pool;

    if( !app || !app->world || !app->world->load_complete || !app->provider )
        return;
    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        struct Task_AppNpcTransform* task;

        if( !npc || !app_npc_transform_depends_on_varp(app, npc->base_npc_id, varp_id) )
            continue;
        task = calloc(1, sizeof(*task));
        assert(task);
        task->task.vtable = &Task_AppNpcTransform_VTable;
        strncpy(task->task.name, "NpcTransform", sizeof(task->task.name) - 1);
        task->app = app;
        task->world_idx = i;
        task->element_id = npc->element_id;
        task->server_slot = npc->server_slot;
        task->base_npc_id = npc->base_npc_id;
        task->resolved_npc_id = npc->npc_id;
        PT_INIT(&task->pt);
        ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
    }
}

/*
 * Live multiloc remorph (Java ClientLocAnim / OpenRS2 Loc.getMultiLoc): when a
 * varp that drives a LocType transform table changes, re-apply each matching
 * scenery instance so the model/name/ops track the new child without a zone
 * LOC packet or a full chunk rebuild. Queues App_WorldLocChange (async model
 * wait) with the same BASE loc_id the map placed.
 */
static int
app_loc_transform_depends_on_varp(
    struct App* app,
    struct ToriRS_Location const* loc,
    int varp_id)
{
    assert(loc);
    if( loc->transform_count <= 0 || !loc->transforms )
        return 0;
    if( loc->transform_varp == varp_id )
        return 1;
    if( loc->transform_varbit >= 0 && loc->transform_varbit < app->varps.varbit_count )
    {
        struct VarBitType const* vb = &app->varps.varbit_types[loc->transform_varbit];
        if( vb->basevar == varp_id )
            return 1;
    }
    return 0;
}

static void
app_varp_refresh_loc_transforms(
    struct App* app,
    int varp_id)
{
    assert(app);
    if( !app->provider || varp_id < 0 )
        return;
    int previous_view = app->active_world;
    /* Each view has independent scene-local tile keys. In particular, (3,2)
     * on a raft must never retype (3,2) in the root or another boat. */
    for( int view = 0; view < WORLDVIEW_MAX; ++view )
    {
        if( !WorldviewRegistry_IsLive(&app->worldviews, view) )
            continue;
        struct World* world = WorldviewRegistry_Get(&app->worldviews, view)->world;
        if( !world || !world->load_complete )
            continue;
        enum
        {
            MAX_REFRESH = 256
        };
        struct
        {
            int x, z, level, loc_id, shape, angle, op_flags;
            char ops[5][32];
        } pending[MAX_REFRESH];
        int n = 0;
        struct World_EntityPool* pool = &world->entities.scenery;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Scenery* sc = World_EntityPoolGet(pool, i);
            assert(sc);
            struct ToriRS_Location* loc = CacheProvider_LocationGet(app->provider, sc->loc_id);
            int depends = 0;
            /* A varp may drive a descendant of the placed wrapper. Every
             * config along a previously materialised chain is resident. */
            for( int depth = 0; loc && depth < 16; ++depth )
            {
                if( app_loc_transform_depends_on_varp(app, loc, varp_id) )
                {
                    depends = 1;
                    break;
                }
                if( loc->transform_count <= 0 || !loc->transforms )
                    break;
                int next = VarPManager_ResolveTransform(
                    &app->varps,
                    loc->transforms,
                    loc->transform_count,
                    loc->transform_varbit,
                    loc->transform_varp);
                if( next < 0 || next == loc->id )
                    break;
                loc = CacheProvider_LocationGet(app->provider, next);
            }
            if( !depends )
                continue;
            int duplicate = 0;
            for( int j = 0; j < n; ++j )
                if( pending[j].x == sc->grid_position.x && pending[j].z == sc->grid_position.z &&
                    pending[j].level == sc->grid_position.level && pending[j].shape == sc->shape )
                {
                    duplicate = 1;
                    break;
                }
            if( duplicate )
                continue;
            if( n == MAX_REFRESH )
                break;
            pending[n].x = sc->grid_position.x;
            pending[n].z = sc->grid_position.z;
            pending[n].level = sc->grid_position.level;
            pending[n].loc_id = sc->loc_id;
            pending[n].shape = sc->shape;
            pending[n].angle = sc->angle;
            pending[n].op_flags = 0x1f;
            memset(pending[n].ops, 0, sizeof(pending[n].ops));
            for( int op = 0; op < 5; ++op )
                if( sc->placement_op_overrides & (1 << op) )
                {
                    if( !(sc->placement_op_mask & (1 << op)) )
                        pending[n].op_flags &= ~(1 << op);
                    else
                        snprintf(
                            pending[n].ops[op],
                            sizeof(pending[n].ops[op]),
                            "%s",
                            sc->info->actions[op].name);
                }
            ++n;
        }
        app->active_world = view;
        for( int i = 0; i < n; ++i )
            App_WorldLocChangeOps(
                app,
                pending[i].x,
                pending[i].z,
                pending[i].level,
                pending[i].loc_id,
                pending[i].shape,
                pending[i].angle,
                pending[i].op_flags,
                pending[i].ops);
    }
    app->active_world = previous_view;
}

/*
 * Plain value-change callback: loc transforms + anything else that must react
 * to optimistic CS2/IF1 writes as well as server VARP packets. Must NOT feed
 * the CS2 var-transmit ring (that is app_varp_server_update only).
 */
static void
app_varp_change(
    void* userdata,
    int varp_id)
{
    struct App* app = (struct App*)userdata;

    UITree_HostInputsChanged(&app->ui_host, UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_CLIENT_STATE));
    app->need_redraw = 1;
    app_varp_refresh_loc_transforms(app, varp_id);
    app_varp_refresh_npc_transforms(app, varp_id);
    /* Modern audio slider clicks call GAMEOPTION/DEVICEOPTION directly, while
     * the four mute icons only write their backing varps. Both paths must
     * reach the same host snapshot; this is the reference's client-side varp
     * side effect and deliberately does not feed the var-transmit ring. */
    RS_CS2Host_SyncAudioVarp(&app->host, varp_id);
}

/*
 * Server varp update -> CS2 host, so the tick's var-transmit pump re-dispatches
 * the hooks that list this varp as a trigger. Userdata is the app, not the host,
 * because the same callback routes client-code varps (the sound volume setting)
 * to their subsystems.
 *
 * Deliberately NOT wired to the plain value-change callback, and not wired to
 * varcs at all. The reference feeds its changed-varp ring only from the
 * VARP_SMALL / VARP_LARGE / VARP_RESET packet handlers: a script-side write
 * (CS2 SETVARP, IF1 button, varbit set) updates the varp and notifies the
 * server but never enters the ring, and `Varcs` writes touch nothing beyond
 * their own map. Wiring either of those in makes the dispatch self-feeding —
 * a hook whose script writes a var bumps the change serial, which re-triggers
 * that same hook next tick, forever. That is what had rev230's gameframe
 * rebuilding the popout strip, the world-hop list (601 dynamic children) and
 * the 161|36 listener from scratch every ~8 frames, and it is why the hovered
 * component id climbed without end: every rebuild hands the same three popout
 * icons brand-new dynamic uids.
 *
 * Loc remorph runs from ChangeFn (also fired by ApplySmall/Large when the value
 * actually changes), so this path only adds CS2 transmit + clientcode audio.
 */
static void
app_varp_server_update(
    void* userdata,
    int varp_id)
{
    struct App* app = (struct App*)userdata;

    RS_CS2Host_NotifyVarChanged(&app->host, varp_id);

    /* Client-code varps are settings the client acts on rather than displays.
     * Code 4 is the sound-effect volume slider (reference Client.updateVarp:
     * 0..3 pick 128/96/64/32, 4 mutes) — the only one audio cares about, and the
     * only reason the player's volume choice reaches the platform at all.
     *
     * Codes 18 and 22 are the Controls panel's two Attack-options dropdowns.
     * They are read here rather than by the minimenu builder because the
     * reference stores the DERIVED enum, not the varp: the builder must not see
     * the zero a never-transmitted varp holds (that reads as "Depends on combat
     * levels" while the reference is still suppressing every Attack row). */
    switch( VarPManager_GetClientcode(&app->varps, varp_id) )
    {
    case 4:
        RS_Audio_SetVolumeLevel(
            &app->audio, VarPManager_GetVarp(&app->varps, varp_id), &app->audio_out);
        break;
    case RS_ATTACK_OPTION_CLIENTCODE_PLAYER:
        if( app->features->attack_option_model == TORIRS_ATTACK_OPTION_MODEL_SETTINGS )
            app->player_attack_option =
                RS_AttackOption_FromVarp(VarPManager_GetVarp(&app->varps, varp_id));
        break;
    case RS_ATTACK_OPTION_CLIENTCODE_NPC:
        if( app->features->attack_option_model == TORIRS_ATTACK_OPTION_MODEL_SETTINGS )
            app->npc_attack_option =
                RS_AttackOption_FromVarp(VarPManager_GetVarp(&app->varps, varp_id));
        break;
    default:
        break;
    }
}

/**
 * Tell the provider which cache it is reading.
 *
 * The profile is what rscache's decoders consult instead of a bare revision number.
 * Resolving it here, once, is the point: era information used to reach decoders as
 * whichever JS5 archive counter the record happened to come from — a per-archive value
 * whose units differ between eras — or as a flag constant spelled out at the call site.
 *
 * The manifest states all four identity fields (game, epoch, revision, quirks).
 * RSCache_ProfileForIdentity returns them verbatim and borrows codec pins from the
 * revision registry on an exact match. There is no nearest-lower fallback and no
 * guessing from the container alone.
 */
static void
app_provider_set_cache_profile(
    struct App* app,
    struct AppConfig const* cfg)
{
    assert(app);
    assert(app->provider);
    assert(cfg->cache_identity_set && "manifest must state [cache:boot] identity");

    struct RSCache profile = RSCache_ProfileForIdentity(
        cfg->cache_game, cfg->cache_epoch, cfg->cache_revision, cfg->cache_quirks);

    char quirks_buf[32];
    RSCache_QuirksName(profile.quirks, quirks_buf, (int)sizeof(quirks_buf));
    TORIRS_LOG(
        "app: cache profile epoch=%s game=%s revision=%d quirks=%s\n",
        RSCache_EpochName(profile.epoch),
        RSCache_GameName(profile.game),
        profile.revision,
        quirks_buf);
    if( getenv("TORIRS_TRACE_NATIVE_UI") )
        TORIRS_REPORT(
            "NATIVE_REVISION epoch=%s game=%s revision=%d\n",
            RSCache_EpochName(profile.epoch),
            RSCache_GameName(profile.game),
            profile.revision);

    /* The disk resolves logical table names to ids and decides map XTEA, so it
     * needs the same identity the decoders got. Without this it answers as
     * unset, which on a 643 cache means every logical table is ABSENT. */
    if( app->dat2_disk )
        RSCache_Dat2DiskSetProfile(app->dat2_disk, &profile);

    CacheProvider_SetProfile(app->provider, &profile);
}

/* ---- Developer overlay ------------------------------------------------- *
 *
 * One minimenu-styled ToriRSChrome panel (src/ui/README_DEBUG_OVERLAY.md) holding
 * the frame time, averaged over the last APP_DEBUG_FRAME_SAMPLES frames. The
 * App feeds the model; the node that draws it is declared by the manifest
 * (`type=debug_overlay`, docs/debug_overlay.md §2) and answered through
 * UITREE_HOST_GET_DEBUG_OVERLAY.
 *
 * A hidden panel builds no primitives, so the overlay costs one host call and
 * nothing else until the toggle key turns it on. The samples keep accumulating
 * either way — the average is a property of the client, not of whether anyone
 * is looking at it, and a readout that starts at "--" for ten frames after
 * every toggle would be useless for exactly the stutter it is there to catch.
 */

/* Sizes the panel. The title is the widest string the panel will ever hold, so
 * content sizing (fixed_w 0) settles on one width and the panel never resizes
 * as the digits change under it. */
static char const k_app_debug_overlay_title[] = "Frame time (10-frame avg)";

/*
 * Point the tree's overlay components at the faces baked for the current
 * chrome scale.
 *
 * Split out because it runs twice: once when the scale is set, and again after
 * a tree rebuild, which resolves the ids itself at bake time but from whatever
 * scale the bridge is holding. Both paths end at the same three ids.
 */
static void
app_chrome_fonts_resolve(struct App* app)
{
    int small;
    int menu;
    int body;

    assert(app);
    /* Before the tree exists there is nothing to point at, and the bake will
     * resolve these itself from the scale the bridge is now holding. Not a
     * contract violation: App_SetChromeScale is legitimately called at boot,
     * ahead of the first build. */
    if( !app->tree )
        return;
    small = UITreeSceneBridge_EnsureDebugFont(&app->bridge, TORIRS_CHROME_FONT_SMALL);
    menu = UITreeSceneBridge_EnsureDebugFont(&app->bridge, TORIRS_CHROME_FONT_MENU);
    body = UITreeSceneBridge_EnsureDebugFont(&app->bridge, TORIRS_CHROME_FONT_BODY);
    UITree_DebugOverlaySetFontIds(app->tree, small, menu, body);
}

int
App_SetChromeScale(
    struct App* app,
    int scale)
{
    assert(app);
    if( scale < TORIRS_CHROME_SCALE_MIN )
        scale = TORIRS_CHROME_SCALE_MIN;
    /* Clamped, not asserted: this number comes from the DISPLAY, and a 4x
     * monitor is a fact about the world rather than a caller's bug. Chrome one
     * baked size below the display's density is a little small; an assert here
     * would be a crash on a machine nobody tested on. */
    if( scale > TORIRS_CHROME_SCALE_MAX )
        scale = TORIRS_CHROME_SCALE_MAX;
    if( ToriRSChrome_Scale(&app->dbg_ui) == scale )
        return 0;

    ToriRSChrome_SetScale(&app->dbg_ui, scale);
    /* Both instances, because there is one scale: the font ids resolved below
     * are shared, so a plugin window left at 1x would lay its rows out for a
     * face the renderer draws at 2x -- text overflowing boxes sized for a
     * smaller font, which is the exact failure SetScale exists to prevent. */
    ToriRSChrome_SetScale(&app->plugin_ui, scale);
    UITreeSceneBridge_SetChromeScale(&app->bridge, scale);
    app_chrome_fonts_resolve(app);
    return 1;
}

int
App_ChromeScale(struct App const* app)
{
    assert(app);
    return ToriRSChrome_Scale(&app->dbg_ui);
}

int
App_SetChromeCheckStyle(
    struct App* app,
    int style)
{
    assert(app);
    if( ToriRSChrome_CheckStyle(&app->dbg_ui) == style )
        return 0;
    /* Both instances, because there is one answer to "what does a checkbox
     * look like here" -- the same rule App_SetChromeScale keeps, and for a
     * sharper reason: the two panels are commonly on screen together. */
    ToriRSChrome_SetCheckStyle(&app->dbg_ui, style);
    ToriRSChrome_SetCheckStyle(&app->plugin_ui, style);
    return 1;
}

int
App_ChromeCheckStyle(struct App const* app)
{
    assert(app);
    return ToriRSChrome_CheckStyle(&app->dbg_ui);
}

static void
app_debug_overlay_init(struct App* app)
{
    assert(app);

    ToriRSChrome_Init(&app->dbg_ui);

    /*
     * Tell the chrome which baked skin images this build actually carries.
     *
     * The chrome cannot ask: it reaches nothing outside the C library, which is
     * the property that lets it draw on a cache that failed to open. So the
     * host, which does link the baked module, reports what it has -- and a
     * build with the skin stubbed out reports nothing and gets the flat look,
     * with no code path here that has to know about that case.
     *
     * TORIRS_CHROME_THEME=flat forces the flat developer palette, for reading a
     * dense readout without the parchment behind it.
     */
    {
        char const* theme = getenv("TORIRS_CHROME_THEME");
        if( theme && strcmp(theme, "flat") == 0 )
            app->dbg_ui.theme = torirs_chrome_theme_default;

        /*
         * Which boolean art the checkboxes wear: the manifest's answer, and
         * TORIRS_CHROME_CHECKBOX over the top of it -- the same order every
         * other chrome option here is resolved in.
         *
         * Set on dbg_ui alone because plugin_ui is COPIED from it a few lines
         * below; going through App_SetChromeCheckStyle would be a call on an
         * instance that does not exist yet.
         */
        {
            char const* pick = getenv("TORIRS_CHROME_CHECKBOX");
            int style = app->cfg.chrome_checkbox;
            if( pick && strcmp(pick, "box") == 0 )
                style = TORIRS_CHROME_CHECK_STYLE_BOX;
            else if( pick && strcmp(pick, "tick") == 0 )
                style = TORIRS_CHROME_CHECK_STYLE_TICK;
            else if( pick )
                TORIRS_LOG("chrome: TORIRS_CHROME_CHECKBOX must be tick|box, got '%s'\n", pick);
            ToriRSChrome_SetCheckStyle(&app->dbg_ui, style);
        }

        for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT && i < ToriRSChromeSkin_Count(); i++ )
            app->dbg_ui.skin_avail |= 1u << i;
        if( app->dbg_ui.skin_avail & (1u << TORIRS_CHROME_SKIN_PANEL_BODY) )
        {
            struct ToriRSChromeSkin_Sprite const* body =
                ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY);
            app->dbg_ui.skin_tile_w = body->w;
            app->dbg_ui.skin_tile_h = body->h;
        }
    }

    /*
     * The plugin window's own instance.
     *
     * Copied wholesale from the developer one rather than initialised
     * separately, so the two cannot drift on theme, scale or which skin slots
     * the build carries -- three things a second Init would have to repeat and
     * a fourth panel would eventually be found not to have.
     */
    app->plugin_ui = app->dbg_ui;
    ToriRSChromeShell_Init(&app->plugin_shell, 320);
    /* File-static view state survives Android recreating main() in one
     * process; the App does not, so a new App must not inherit a selected page
     * whose model belonged to the previous run. */
    g_plugin_page = -1;
    g_plugin_page_built = -1;
    g_plugin_fullscreen = 0;
    g_plugin_fullscreen_built = -1;
    app->plugin_panel = -1;
    app->plugin_button_node = -1;
    app->plugin_button_disabled = 0;
    app->plugin_panel_built_for = -1;
    app->plugin_panel_built_rev = -1;
    app->plugin_panel_built_model_rev = 0;
    app->plugin_panel_built_generation = 0;
    app->plugin_panel_built_registry_rev = 0;
    app->plugin_panel_intent_sequence = 0;
    ToriRSChromeRailSync_Init(&app->plugin_rail);
    app->plugin_rail_has_layout = 0;
    /* No executor has been reported yet, and BUFFER is a real answer. */
    app->plugin_exec_logged_kind = -1;

    app->dbg_visible = 0;
    app->dbg_frame_head = 0;
    app->dbg_frame_count = 0;
    app->dbg_panel = ToriRSChrome_PanelAdd(
        &app->dbg_ui, TORIRS_CHROME_PANEL_MENU, 8, 8, 0, k_app_debug_overlay_title);
    app->dbg_frame_row = ToriRSChrome_MenuItem(&app->dbg_ui, app->dbg_panel, "--");
    ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->dbg_panel, 0);

    app->locedit_panel =
        ToriRSChrome_PanelAdd(&app->dbg_ui, TORIRS_CHROME_PANEL_MENU, 8, 40, 0, "Loc Editor");
    app->locedit_row_target =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit_panel, "nothing selected");
    app->locedit_row_pos = ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit_panel, "");
    app->locedit_row_size = ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit_panel, "");
    app->locedit_row_extra = ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit_panel, "");
    ToriRSChrome_Separator(&app->dbg_ui, app->locedit_panel);
    /* Rows double as the key reference: chat input is forced off while this
     * panel is open (below), so these letters are always free to use without
     * a message box eating them. Still clickable too -- the key is the fast
     * path, the click is the discoverable one. */
    app->locedit_item_xplus =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit_panel, "Move X+1  [D]");
    app->locedit_item_xminus =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit_panel, "Move X-1  [A]");
    app->locedit_item_zplus =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit_panel, "Move Z+1  [W]");
    app->locedit_item_zminus =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit_panel, "Move Z-1  [S]");
    app->locedit_item_rotate =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit_panel, "Rotate  [R]");
    app->locedit_item_reselect =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit_panel, "Reselect (under cursor)  [Space]");
    app->locedit_item_deselect =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit_panel, "Deselect  [Backspace]");
    app->locedit_item_close =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit_panel, "Close  [9 / Esc]");
    ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->locedit_panel, 0);
    app->locedit_visible = 0;

    /* The All Settings colour picker. Built empty and hidden: its rows are the
     * ROW's -- title, default swatch -- and are only known once a swatch has
     * been clicked, so every open clears and rebuilds them. Declared here all
     * the same, so the handle is valid from the first frame and no path has to
     * test for a panel that does not exist yet. */
    app->settings_colour_panel =
        ToriRSChrome_PanelAdd(&app->dbg_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 40, 0, "Colour");
    ToriRSChrome_PanelSetFramed(&app->dbg_ui, app->settings_colour_panel, 1);
    ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->settings_colour_panel, 0);
    app->settings_colour_visible = 0;
    app->settings_colour_pick = -1;
    app->settings_colour_default_btn = -1;
    app->settings_colour_close_btn = -1;
    app->settings_number_panel =
        ToriRSChrome_PanelAdd(&app->dbg_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 40, 0, "Value");
    ToriRSChrome_PanelSetFramed(&app->dbg_ui, app->settings_number_panel, 1);
    ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->settings_number_panel, 0);
    app->settings_number_visible = 0;
    app->settings_number_input = -1;
    app->settings_number_close_btn = -1;
    app->locedit_loc_id = -1;
    app->locedit_shape = -1;
    app->locedit_angle = 0;
    app->locedit_size_x = 0;
    app->locedit_size_z = 0;
    app->locedit_interactive = 0;
    app->locedit_name[0] = '\0';
    app->locedit_scene_x = -1;
    app->locedit_scene_z = -1;
    app->locedit_level = 0;
    app->locedit_terrain = 0;
    app->locedit_terrain_level = 0;
    app->locedit_hover_x = -1;
    app->locedit_hover_z = -1;

    /* Footprint outline: the env var picks the mode AND the starting state, so
     * an existing `TORIRS_HOVER_FOOTPRINT=1` run is unchanged and the hotkey
     * merely gains the ability to turn it off. Unset means off but armed at
     * mode 1, which is what the hotkey turns on. A negative or unparsable value
     * is off with nothing to restore. */
    {
        char const* env = getenv("TORIRS_HOVER_FOOTPRINT");
        int mode = (env && env[0]) ? (int)strtol(env, NULL, 0) : 0;
        if( mode < 0 )
            mode = 0;
        app->hover_footprint = mode;
        app->hover_footprint_mode = mode > 0 ? mode : 1;
    }
}

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

    app->dbg_frame_us[app->dbg_frame_head] =
        frame_us > UINT32_MAX ? UINT32_MAX : (uint32_t)frame_us;
    app->dbg_frame_head = (app->dbg_frame_head + 1) % APP_DEBUG_FRAME_SAMPLES;
    if( app->dbg_frame_count < APP_DEBUG_FRAME_SAMPLES )
        app->dbg_frame_count++;
}

uint64_t
App_LastFrameUs(struct App const* app)
{
    assert(app);

    if( app->dbg_frame_count <= 0 )
        return 0;
    /* dbg_frame_head is where the NEXT sample lands, so the newest is the slot
     * before it, wrapping. */
    return app->dbg_frame_us
        [(app->dbg_frame_head + APP_DEBUG_FRAME_SAMPLES - 1) % APP_DEBUG_FRAME_SAMPLES];
}

/*
 * The client-side cheats, in one place.
 *
 * `lootkill <source> <obj> [qty]` seeds the loot store as a kill would; it is
 * answered here because no server knows the client's loot store. Every path a
 * cheat can arrive by -- typed into the chat line, the login cheat list, or
 * the headless TORIRS_SIM_CMD sender -- asks this first, so a headless run and
 * a typed command reach the same code. True when the text was consumed.
 */
static bool
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

/** Mean of the samples held so far, in microseconds. 0 when there are none. */
static uint32_t
app_debug_frame_mean_us(struct App const* app)
{
    uint64_t total = 0;

    if( app->dbg_frame_count <= 0 )
        return 0;
    for( int i = 0; i < app->dbg_frame_count; i++ )
        total += app->dbg_frame_us[i];
    return (uint32_t)(total / (uint64_t)app->dbg_frame_count);
}

/*
 * Toggle the overlay, refresh its readout, rebuild its display list.
 *
 * Runs before the BOOTING early-out in App_RunOnce so the key still latches
 * during a boot, and before the emit rebuild so a changed readout reaches the
 * same frame's display list rather than the next one's.
 */
static void
app_debug_overlay_tick(
    struct App* app,
    struct LibToriRS_Input* input)
{
    assert(app);
    assert(input);

    /* Suppressed while a text line has focus, like the camera keys: typing a
     * message must not flip debug chrome. */
    if( !app_text_input_focused(app) &&
        app_debug_key_down(app, input, APP_DEBUG_HOTKEY_DEBUG_OVERLAY) )
    {
        app->dbg_visible = !app->dbg_visible;
        ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->dbg_panel, app->dbg_visible);
    }

    if( app->dbg_visible )
    {
        uint32_t const mean_us = app_debug_frame_mean_us(app);
        char text[TORIRS_CHROME_INPUT_MAX];

        /* Two decimals: the samples are microseconds, and rounding a 3.4 ms
         * frame to "3 ms" throws away the part that moves. */
        if( app->dbg_frame_count > 0 )
            snprintf(
                text,
                sizeof(text),
                "%u.%02u ms",
                (unsigned)(mean_us / 1000u),
                (unsigned)((mean_us % 1000u) / 10u));
        else
            snprintf(text, sizeof(text), "--");
        /* Compare-then-set: an unchanged readout dirties nothing, so a steady
         * client rebuilds no display list and requests no redraw. */
        ToriRSChrome_SetText(&app->dbg_ui, app->dbg_frame_row, text);
    }

    /* Build returns 0 on a frame where nothing moved. When it did rebuild the
     * canvas is stale — including the frame the panel was hidden on, whose
     * vacated pixels are still on screen until something repaints them. */
    if( ToriRSChrome_Build(&app->dbg_ui) )
    {
        app->need_redraw = 1;
        ToriRSChrome_DamageClear(&app->dbg_ui);
    }
}

/* ---- Loc editor --------------------------------------------------------- *
 *
 * A second panel in the same dbg_ui instance (see the developer overlay
 * above): move and rotate whatever loc sits under the cursor, live, without a
 * server round trip. `App_WorldLocChange` already exists as a client-only
 * "swap the loc at this tile" primitive (it drives zone LOC_ADD_CHANGE/DEL
 * packets too), so a move is del-at-old-tile + change-at-new-tile and a
 * rotate is a same-tile change with a new angle. Nothing here touches the
 * server or persists past a world reload — the point is to read the exact
 * scene x/z/angle off the panel once it looks right and hand-copy those
 * numbers into the actual placement script.
 */

/* Refreshes the panel's readout rows from current selection state. Called
 * after every selection change, deselect, move, and rotate. */
static void
app_loc_editor_refresh_labels(struct App* app)
{
    char text[TORIRS_CHROME_INPUT_MAX];

    /*
     * A selected TILE, which answers a different set of questions than a loc.
     *
     * The three levels are all different on exactly the columns where ground
     * misbehaves, so all three are shown rather than one "level":
     *   cache  — the plane the map authored this floor on (the mesh level).
     *   draw   — the plane it is culled and picked against; VIS_BELOW makes
     *            that 0, and a LinkBelow column's upper planes one lower.
     *   paint  — where the build's push-down parked the tile in the painter.
     * A flat column reads the same number three times; a bridge deck reads
     * 1/0/0, and that spread is the readout's whole reason to exist.
     */
    if( app->locedit_terrain )
    {
        char settings[4 * 6 + 1];
        char meshes[WORLD_MAP_TERRAIN_LEVELS + 1];
        int const cache_level = app->locedit_terrain_level;

        World_TileSettingsText(
            app->world,
            app->locedit_scene_x,
            app->locedit_scene_z,
            settings,
            (int)sizeof(settings));
        World_TerrainMeshLevelsText(
            app->world, app->locedit_scene_x, app->locedit_scene_z, meshes, (int)sizeof(meshes));

        snprintf(text, sizeof(text), "terrain tile, mesh on level %d", cache_level);
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit_row_target, text);
        snprintf(
            text,
            sizeof(text),
            "x=%d z=%d abs(%d,%d)",
            app->locedit_scene_x,
            app->locedit_scene_z,
            app->world ? app->world->_base_tile_x + app->locedit_scene_x : -1,
            app->world ? app->world->_base_tile_z + app->locedit_scene_z : -1);
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit_row_pos, text);
        snprintf(
            text,
            sizeof(text),
            "cache=%d draw=%d paint=%d",
            cache_level,
            World_TerrainDrawLevel(
                app->world, app->locedit_scene_x, app->locedit_scene_z, cache_level),
            World_LocPaintLevel(
                app->world, app->locedit_scene_x, app->locedit_scene_z, cache_level));
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit_row_size, text);
        snprintf(text, sizeof(text), "s[%s] mesh[%s]", settings, meshes);
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit_row_extra, text);
        return;
    }

    if( app->locedit_loc_id < 0 )
    {
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit_row_target, "nothing selected");
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit_row_pos, "");
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit_row_size, "");
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit_row_extra, "");
        return;
    }
    snprintf(text, sizeof(text), "loc %d shape %d", app->locedit_loc_id, app->locedit_shape);
    ToriRSChrome_SetText(&app->dbg_ui, app->locedit_row_target, text);
    snprintf(
        text,
        sizeof(text),
        "x=%d z=%d level=%d",
        app->locedit_scene_x,
        app->locedit_scene_z,
        app->locedit_level);
    ToriRSChrome_SetText(&app->dbg_ui, app->locedit_row_pos, text);
    snprintf(
        text,
        sizeof(text),
        "size %dx%d angle=%d",
        app->locedit_size_x,
        app->locedit_size_z,
        app->locedit_angle);
    ToriRSChrome_SetText(&app->dbg_ui, app->locedit_row_size, text);
    /* A baked-map loc usually has no LocType.name resolved client-side (that
     * lives in the config, not the placed entity), so an empty name is the
     * common case -- fall back to whether it can be clicked at all rather
     * than print a blank row. */
    if( app->locedit_name[0] )
        snprintf(
            text,
            sizeof(text),
            "\"%s\" interactive=%d",
            app->locedit_name,
            app->locedit_interactive);
    else
        snprintf(text, sizeof(text), "interactive=%d", app->locedit_interactive);
    ToriRSChrome_SetText(&app->dbg_ui, app->locedit_row_extra, text);
}

/* Targets whatever loc sits at locedit_hover_x/z -- the last tile the cursor
 * hovered while NOT over the panel -- on the local player's current level (a
 * loc editor has no reason to reach across planes). Clears the selection
 * (loc_id -1) when there is no loc there or nothing was ever hovered. Only
 * ever called from an explicit Reselect click -- opening the panel does NOT
 * call this, so a selection stays active across a close/reopen.
 *
 * Deliberately reads locedit_hover_x/z, not the live world_hover_tile_x/z:
 * clicking "Reselect" necessarily moves the cursor onto the panel first, and
 * by the time the click lands, the live hover reflects the panel, not
 * whatever loc the player was actually pointing at. */
static void
app_loc_editor_reselect(struct App* app)
{
    struct WorldEntity_Player* player;
    struct WorldEntity_Scenery* scenery;
    int idx;

    app->locedit_loc_id = -1;
    app->locedit_terrain = 0;
    if( !app->world || app->locedit_hover_x < 0 || app->locedit_hover_z < 0 )
    {
        app_loc_editor_refresh_labels(app);
        return;
    }
    player = app_local_player(app);
    app->locedit_level = player ? player->grid_position.level : 0;
    /* loc_shape < 0: match the first loc on the tile regardless of layer --
     * a decoration like a bridge is exactly as findable as a wall this way. */
    idx = World_SceneryFindAt(
        app->world, app->locedit_hover_x, app->locedit_hover_z, app->locedit_level, -1);
    if( idx < 0 )
    {
        app_loc_editor_refresh_labels(app);
        return;
    }
    scenery = World_EntityPoolGet(&app->world->entities.scenery, idx);
    if( !scenery )
    {
        app_loc_editor_refresh_labels(app);
        return;
    }
    app->locedit_loc_id = scenery->loc_id;
    app->locedit_shape = scenery->shape;
    app->locedit_angle = scenery->angle;
    app->locedit_size_x = scenery->size_x;
    app->locedit_size_z = scenery->size_z;
    app->locedit_interactive = scenery->interactive;
    snprintf(app->locedit_name, sizeof(app->locedit_name), "%s", scenery->info->name);
    app->locedit_scene_x = scenery->grid_position.x;
    app->locedit_scene_z = scenery->grid_position.z;
    app->locedit_level = scenery->grid_position.level;
    app_loc_editor_refresh_labels(app);
}

/* Explicit Deselect: clears the target without touching the world. Clears the
 * tile selection too — one row, both subjects, or Deselect would appear to do
 * nothing while a tile was up. */
static void
app_loc_editor_deselect(struct App* app)
{
    app->locedit_loc_id = -1;
    app->locedit_terrain = 0;
    app_loc_editor_refresh_labels(app);
}

/* Targets an exact scene element -- the "Select" minimenu row's handler.
 * Unlike app_loc_editor_reselect (a tile-only guess, first-loc-regardless-of-
 * layer), this comes from the real pick/classify/dedup pipeline the minimenu
 * itself uses, via the row's UIMinimenuPick.id, so it disambiguates a tile
 * with a wall AND a wall-decor AND a ground loc on it exactly the way a
 * player reading the right-click menu would. */
static void
app_loc_editor_select_element(
    struct App* app,
    int element_id)
{
    struct WorldEntity_Scenery* scenery;

    if( !app->world )
        return;
    scenery = World_SceneryGetByElementId(app->world, element_id);
    if( !scenery )
        return;
    /* The two selections are exclusive: one panel, one subject. */
    app->locedit_terrain = 0;
    app->locedit_loc_id = scenery->loc_id;
    app->locedit_shape = scenery->shape;
    app->locedit_angle = scenery->angle;
    app->locedit_size_x = scenery->size_x;
    app->locedit_size_z = scenery->size_z;
    app->locedit_interactive = scenery->interactive;
    snprintf(app->locedit_name, sizeof(app->locedit_name), "%s", scenery->info->name);
    app->locedit_scene_x = scenery->grid_position.x;
    app->locedit_scene_z = scenery->grid_position.z;
    app->locedit_level = scenery->grid_position.level;
    app_loc_editor_refresh_labels(app);
}

/* Select the GROUND at a scene tile. `cache_level` is the picked mesh level —
 * the plane the map authored that floor on — which the panel then reads the
 * draw and paint levels off, since those are derived and not stored. */
static void
app_loc_editor_select_terrain(
    struct App* app,
    int scene_x,
    int scene_z,
    int cache_level)
{
    if( !app->world )
        return;
    app->locedit_terrain = 1;
    app->locedit_loc_id = -1;
    app->locedit_scene_x = scene_x;
    app->locedit_scene_z = scene_z;
    app->locedit_terrain_level = cache_level;
    app->locedit_level = cache_level;
    app_loc_editor_refresh_labels(app);
}

/* Client-only reposition: clear the old tile, place at the new one. Both legs
 * go through App_WorldLocChange so this is exactly what a zone LOC_DEL +
 * LOC_ADD_CHANGE pair would produce, just without a server round trip. */
static void
app_loc_editor_nudge(
    struct App* app,
    int dx,
    int dz)
{
    if( app->locedit_loc_id < 0 )
        return;
    App_WorldLocChange(
        app,
        app->locedit_scene_x,
        app->locedit_scene_z,
        app->locedit_level,
        -1,
        app->locedit_shape,
        app->locedit_angle);
    /* The scene edit above is client-side only; this records the same move
     * against the authored loc list so it survives a reload and can be saved.
     * Recorded BEFORE the coordinates advance, since the command needs both
     * ends of the move. */
    Editor_PanelRecordLocEdit(
        &app->editor_panel,
        app,
        app->locedit_scene_x,
        app->locedit_scene_z,
        app->locedit_level,
        app->locedit_loc_id,
        app->locedit_shape,
        app->locedit_angle,
        app->locedit_scene_x + dx,
        app->locedit_scene_z + dz,
        app->locedit_angle);
    app->locedit_scene_x += dx;
    app->locedit_scene_z += dz;
    App_WorldLocChange(
        app,
        app->locedit_scene_x,
        app->locedit_scene_z,
        app->locedit_level,
        app->locedit_loc_id,
        app->locedit_shape,
        app->locedit_angle);
    app_loc_editor_refresh_labels(app);
}

/* Same-tile change with the next of the 4 config angles (0..3 = W/N/E/S,
 * entity_scenery.h) -- no del needed, App_WorldLocChange already replaces
 * whatever is at scene_x/z. */
static void
app_loc_editor_rotate(struct App* app)
{
    if( app->locedit_loc_id < 0 )
        return;
    /* Same pair as a nudge: the authored record first, then the scene. */
    Editor_PanelRecordLocEdit(
        &app->editor_panel,
        app,
        app->locedit_scene_x,
        app->locedit_scene_z,
        app->locedit_level,
        app->locedit_loc_id,
        app->locedit_shape,
        app->locedit_angle,
        app->locedit_scene_x,
        app->locedit_scene_z,
        (app->locedit_angle + 1) % 4);
    app->locedit_angle = (app->locedit_angle + 1) % 4;
    App_WorldLocChange(
        app,
        app->locedit_scene_x,
        app->locedit_scene_z,
        app->locedit_level,
        app->locedit_loc_id,
        app->locedit_shape,
        app->locedit_angle);
    app_loc_editor_refresh_labels(app);
}

/**
 * OSRS key code -> the overlay's editing key, or TORIRS_CHROME_KEY_NONE.
 *
 * The overlay deliberately owns no keymap (see uitree_debug_overlay.h), so the
 * translation lives here, where the client's own key codes already are.
 * Printable characters do not come through this at all — they arrive as
 * `key_pressed` and go straight to ToriRSChrome_KeyChar.
 */

static int
app_dbgui_key_edit_from_osrs(int osrs_key)
{
    switch( osrs_key )
    {
    case TORIRS_OSRSKEY_BACKSPACE:
        return TORIRS_CHROME_KEY_BACKSPACE;
    case TORIRS_OSRSKEY_DELETE:
        return TORIRS_CHROME_KEY_DELETE;
    case TORIRS_OSRSKEY_ENTER:
        return TORIRS_CHROME_KEY_ENTER;
    case TORIRS_OSRSKEY_ESCAPE:
        return TORIRS_CHROME_KEY_ESCAPE;
    /* Arrows and home/end have no named constants; the keymap table spells
     * them (src/input/torirs_keymap.c: 96 left, 97 right, 98 up, 99 down,
     * 102 home, 103 end). */
    case 96:
        return TORIRS_CHROME_KEY_LEFT;
    case 97:
        return TORIRS_CHROME_KEY_RIGHT;
    /* Up and down a LINE, for a multiline field. The model ignores them on
     * every other kind, so routing them costs nothing where there is none. */
    case 98:
        return TORIRS_CHROME_KEY_UP;
    case 99:
        return TORIRS_CHROME_KEY_DOWN;
    case 102:
        return TORIRS_CHROME_KEY_HOME;
    case 103:
        return TORIRS_CHROME_KEY_END;
    default:
        return TORIRS_CHROME_KEY_NONE;
    }
}

/**
 * Does chrome DRAWN IN THIS CANVAS own the pointer at (x, y)?
 *
 * The game's interaction pass, the world hittest and the camera all run long
 * after the chrome handled this frame's input, and by then they cannot read
 * "the chrome took it" off `input_frame_consumed` -- the shell sets that on
 * every frame as its own replay fence. They ask this instead, and a press, a
 * wheel or a right click over a panel stops there.
 *
 * The plugin window counts only under the BUFFER executor. Every other
 * presentation draws it somewhere else -- its own SDL window, a DOM, the
 * interface tree -- while the in-canvas model keeps the geometry it was laid
 * out with. That geometry is a ghost: hit-testable at a floating position
 * nothing draws, so honouring it would punch an invisible hole in the game.
 */
static int
app_chrome_wants_pointer(
    struct App const* app,
    int x,
    int y)
{
    assert(app);
    if( ToriRSChrome_WantsPointer(&app->dbg_ui, x, y) )
        return 1;
    if( app->plugin_panel_visible && app->plugin_exec_kind == TORIRS_CHROME_EXEC_BUFFER &&
        ToriRSChrome_WantsPointer(&app->plugin_ui, x, y) )
        return 1;
    return 0;
}

/**
 * Feed one frame's pointer and keyboard to one chrome instance.
 *
 * Instance-taking rather than reaching for app->dbg_ui, because the plugin
 * window is a chrome of its own: routing input is the half of "a second chrome
 * is a second of all of this" that genuinely would have been duplicated, and
 * this is the one copy both instances share.
 *
 * Chrome first, then the game (README_DEBUG_OVERLAY.md §6): a click or drag
 * that lands on a panel must not also reach the world's click-to-walk
 * underneath it, which is what `input_frame_consumed` says.
 */
static void
app_chrome_route_input(
    struct App* app,
    struct ToriRSChrome* ui,
    struct LibToriRS_Input* input)
{
    assert(app);
    assert(ui);
    assert(input);

    if( ToriRSChrome_MouseMove(ui, input->curr.mouse_x, input->curr.mouse_y) )
        app->input_frame_consumed = 1;
    /*
     * The press lands where the button actually went DOWN, not where the
     * pointer finished the frame.
     *
     * curr.mouse_x is the last position any event in this frame's batch
     * carried, and a press is one event in that batch. A finger crossing the
     * drag slop pushes move(landed), down(landed), move(now) in a single batch
     * (input/torirs_touch.c), so reading curr here grabbed a scrollbar a slop's
     * width from where the finger was put -- which pages the list instead of
     * taking the grip when the grip's edge is inside that gap. press_origin is
     * the position the down carried, and for a mouse it is the same number.
     */
    if( input->curr.mouse_button_down[TORIRSM_LEFT] &&
        ToriRSChrome_MouseDown(
            ui, input->press_origin_x[TORIRSM_LEFT], input->press_origin_y[TORIRSM_LEFT]) )
        app->input_frame_consumed = 1;
    if( input->curr.mouse_button_up[TORIRSM_LEFT] &&
        ToriRSChrome_MouseUp(ui, input->curr.mouse_x, input->curr.mouse_y) )
        app->input_frame_consumed = 1;

    /* The wheel, so an open dropdown or a scrolling panel moves. Consumed when
     * the chrome takes it, or the camera would zoom behind it at the same time. */
    if( input->curr.mouse_wheel_y != 0 &&
        ToriRSChrome_MouseWheel(
            ui, input->curr.mouse_x, input->curr.mouse_y, input->curr.mouse_wheel_y) )
        app->input_frame_consumed = 1;

    app_chrome_route_keys(app, ui, input);
}

/*
 * The keyboard half of the routing above, on its own because a browser-backed
 * presentation needs exactly this half: pointer intents arrive through the
 * browser bridge, but its text fields are the model's, so typing still has
 * to reach the model. Routing the MOUSE too would hand clicks to the in-canvas
 * window's ghost -- laid out and hit-testable at its floating position even
 * though nothing draws it there.
 *
 * `key_typed` carries the OSRS key code and `key_pressed` the typed character
 * (see torirs_input.h), so editing keys and printable bytes come off different
 * fields of the same event.
 *
 * Safe to run for any visible panel: with nothing focused the chrome consumes
 * neither, so a panel that is merely on screen -- the developer readout, say
 * -- never swallows a keystroke meant for the game.
 */
static void
app_chrome_route_keys(
    struct App* app,
    struct ToriRSChrome* ui,
    struct LibToriRS_Input* input)
{
    assert(app);
    assert(ui);
    assert(input);

    for( int i = 0; i < input->key_event_count; i++ )
    {
        struct LibToriRS_KeyEvent const* ev = &input->key_events[i];
        int consumed = 0;

        if( ev->key_pressed >= 32 && ev->key_pressed < 127 )
            consumed = ToriRSChrome_KeyChar(ui, ev->key_pressed);
        else
        {
            int const edit = app_dbgui_key_edit_from_osrs(ev->key_typed);
            if( edit != TORIRS_CHROME_KEY_NONE )
                consumed = ToriRSChrome_KeyEdit(ui, edit);
        }
        if( consumed )
            app->input_frame_consumed = 1;
    }
}

static void
app_loc_editor_tick(
    struct App* app,
    struct LibToriRS_Input* input)
{
    int activated;

    assert(app);
    assert(input);

    /* Same suppression as the developer overlay toggle: a chat line has focus
     * must not also flip debug chrome. */
    if( !app_text_input_focused(app) &&
        app_debug_key_down(app, input, APP_DEBUG_HOTKEY_LOC_EDITOR) )
    {
        /* Toggling visibility only, never the selection -- a target picked
         * with Reselect stays active across a close/reopen. */
        app->locedit_visible = !app->locedit_visible;
        ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->locedit_panel, app->locedit_visible);
    }

    /* Footprint outline, toggled here rather than in its own tick because it
     * is the same loc-inspection tool and wants the same chat suppression.
     * Restores the configured mode instead of a literal 1, so a run started
     * with TORIRS_HOVER_FOOTPRINT=<loc id> keeps outlining that id after an
     * off/on rather than silently downgrading to "the hovered loc". */
    if( !app_text_input_focused(app) &&
        app_debug_key_down(app, input, APP_DEBUG_HOTKEY_HOVER_FOOTPRINT) )
    {
        app->hover_footprint = app->hover_footprint ? 0 : app->hover_footprint_mode;
        app->need_redraw = 1;
        TORIRS_LOG("hover_footprint: %d\n", app->hover_footprint);
    }

    /* Map editor panel. Gated on the session existing, so binding this key in a
     * manifest with no [editor:boot] is inert rather than a panel with nothing
     * behind it. */
    if( app->editor && !app_text_input_focused(app) &&
        app_debug_key_down(app, input, APP_DEBUG_HOTKEY_MAP_EDITOR) )
    {
        Editor_PanelSetVisible(&app->editor_panel, &app->dbg_ui, !app->editor_panel.visible);
        app->need_redraw = 1;
    }

    /* Both tools inspect locs the pick classifier drops by default — walls,
     * fences, gravel, ground decor: everything with no ops on it, which is
     * most of what a placement or footprint question is actually about. Told
     * here, once, from the two toggles that own the state, so neither tool has
     * to reach into the pick path itself. */
    /* The MAP editor makes every loc pickable too: its Select/Delete minimenu
     * rows are built per PICKED element, so a wall or roof with no ops of its
     * own -- invisible to the pick without this flag -- could never grow a
     * "Select Wall" row however the menu was gated. This one line is the
     * difference between "the menu ignores half the tile" and not. */
    WorldEntity_SceneryDebugSetTools(
        app->locedit_visible || app->hover_footprint != 0 ||
        (app->editor && app->editor_panel.visible));

    /* Remember the world tile under the cursor whenever the cursor is NOT
     * over the panel itself. Runs every frame, panel open or not, so the
     * moment Reselect is clicked there is already a last-known-good world
     * hover to read -- the live world_hover_tile_x/z cannot be used at click
     * time because reaching the menu item necessarily moved the cursor onto
     * the panel first. */
    if( ToriRSChrome_HitTest(&app->dbg_ui, input->curr.mouse_x, input->curr.mouse_y) < 0 &&
        app->world_hover_tile_x >= 0 && app->world_hover_tile_z >= 0 )
    {
        app->locedit_hover_x = app->world_hover_tile_x;
        app->locedit_hover_z = app->world_hover_tile_z;
    }

    /*
     * Overlay input, for ANY visible chrome panel.
     *
     * Asked of the chrome rather than listed here, because the list was the
     * bug: this was gated on the loc editor, then on the loc editor OR the map
     * editor, and every panel added after that -- the plugin settings panel
     * among them -- silently got no clicks. A panel whose checkboxes cannot be
     * ticked reads as broken, not as unrouted, so the gate is now "is anything
     * of this instance on screen" and a new panel needs no gate edit at all.
     */
    if( ToriRSChrome_HasVisiblePanel(&app->dbg_ui) )
        app_chrome_route_input(app, &app->dbg_ui, input);

    if( app->locedit_visible || app->editor_panel.visible )
    {
        /* A chat line stealing W/A/S/D/R/Space/Backspace would make the panel
         * unusable, so force it (and the modal chat variants) closed for as
         * long as this panel is open rather than merely suppressing the
         * toggle key like the developer overlay does. The later chat-focus
         * code (Enter / click-in-chat-region) is itself gated on
         * locedit_visible so it cannot steal focus back mid-session.
         *
         * Deliberately NOT part of the generic routing above: this is an
         * editor's claim on the whole keyboard, and a panel a player may leave
         * open beside the game -- the plugin window -- must not disable chat
         * for as long as it is up. */
        app->chat_input_active = 0;
        app->chat.social_input_open = 0;
        app->chat.dialog_input_open = 0;

        /*
         * A focused model view owns the movement keys: WASD orbits, E/F zooms,
         * arrows orbit too. Every accepted key re-renders with the camera
         * held, consumes the frame, and never reaches the world camera --
         * which also checks this focus itself, for the keys that arrive on
         * frames this block does not see.
         *
         * HELD, not the down edge. The world camera flies for as long as W is
         * down and the preview has to answer the same gesture the same way: on
         * the edge alone, holding a key nudged the model once and then sat
         * there, which is indistinguishable from a control that does not work.
         * The steps are per FRAME because of it -- a fifth of the old edge
         * step, so a press-and-release is still a small turn and a hold is a
         * smooth orbit rather than a spin.
         */
        if( app_modelview_focused(app) )
        {
            int took = 0;
            int const yaw_step = 12;
            int const pitch_step = 6;

            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_A) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_LEFT) )
            {
                app->preview_yan = (app->preview_yan + 2048 - yaw_step) & 2047;
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_D) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT) )
            {
                app->preview_yan = (app->preview_yan + yaw_step) & 2047;
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_W) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP) )
            {
                app->preview_xan = (app->preview_xan + 2048 - pitch_step) & 2047;
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_S) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN) )
            {
                app->preview_xan = (app->preview_xan + pitch_step) & 2047;
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_E) )
            {
                app->preview_zoom = app->preview_zoom * 49 / 50;
                if( app->preview_zoom < 300 )
                    app->preview_zoom = 300;
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_F) )
            {
                app->preview_zoom = app->preview_zoom * 51 / 50;
                if( app->preview_zoom > 16000 )
                    app->preview_zoom = 16000;
                took = 1;
            }
            if( took )
            {
                app->preview_dirty = 1;
                app->preview_keep_camera = 1;
                app->input_frame_consumed = 1;
                app->need_redraw = 1;
            }
        }

        /* The map editor's own key: apply the current tool to the SELECTION,
         * so a subject picked once can be operated on without going back to
         * the world with the cursor. `E` because the camera's up/down moved to
         * R/F, leaving it free next to WASD. */
        if( app->editor && app->editor_panel.visible && !app_text_input_focused(app) &&
            !app_modelview_focused(app) && LibToriRS_Input_IsKeyDown(input, TORIRSK_E) )
        {
            Editor_PanelApplyToSelection(&app->editor_panel, app);
            app->input_frame_consumed = 1;
        }

        /*
         * Keyboard control, the fast path the menu rows advertise. IsKeyDown
         * (edge, not held) so one press moves one tile rather than a nudge
         * repeating every frame a key is held down. Space reselects at the
         * live world_hover_tile_x/z directly -- pressing a key, unlike
         * clicking a menu row, never moves the cursor off the world first, so
         * the live hover is already correct and the remembered
         * locedit_hover_x/z (which Reselect itself reads) is equally valid
         * here since this same tick already refreshed it above.
         *
         * Gated on the LOC editor specifically, not on "any panel is open".
         * The enclosing block widened to route input for the map editor too,
         * and these came along with it -- so with only the map editor open,
         * W/A/S/D nudged whatever loc the loc editor had latched *while also*
         * flying the camera, since a nudge does not consume the frame. Same
         * reasoning as the activation latch below.
         */
        if( app->locedit_visible && LibToriRS_Input_IsKeyDown(input, TORIRSK_D) )
            app_loc_editor_nudge(app, 1, 0);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_A) )
            app_loc_editor_nudge(app, -1, 0);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_W) )
            app_loc_editor_nudge(app, 0, 1);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_S) )
            app_loc_editor_nudge(app, 0, -1);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_R) )
            app_loc_editor_rotate(app);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_SPACE) )
            app_loc_editor_reselect(app);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_BACKSPACE) )
            app_loc_editor_deselect(app);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_ESCAPE) )
        {
            app->locedit_visible = 0;
            ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->locedit_panel, 0);
        }

        /* Only when the LOC editor is open. The block above widened to route
         * input for any visible panel, but this half is loc-editor rows, and
         * draining the shared activation latch here swallowed the map editor's
         * clicks -- its dropdown showed the new value while its tool never
         * changed, because the activation was taken before its tick ran. */
        activated = app->locedit_visible ? ToriRSChrome_TakeActivated(&app->dbg_ui) : -1;
        if( activated >= 0 )
        {
            if( activated == app->locedit_item_xplus )
                app_loc_editor_nudge(app, 1, 0);
            else if( activated == app->locedit_item_xminus )
                app_loc_editor_nudge(app, -1, 0);
            else if( activated == app->locedit_item_zplus )
                app_loc_editor_nudge(app, 0, 1);
            else if( activated == app->locedit_item_zminus )
                app_loc_editor_nudge(app, 0, -1);
            else if( activated == app->locedit_item_rotate )
                app_loc_editor_rotate(app);
            else if( activated == app->locedit_item_reselect )
                app_loc_editor_reselect(app);
            else if( activated == app->locedit_item_deselect )
                app_loc_editor_deselect(app);
            else if( activated == app->locedit_item_close )
            {
                app->locedit_visible = 0;
                ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->locedit_panel, 0);
            }
        }
    }

    if( ToriRSChrome_Build(&app->dbg_ui) )
    {
        app->need_redraw = 1;
        ToriRSChrome_DamageClear(&app->dbg_ui);
    }
}

/* =========================================================================
 * All Settings: the colour rows
 *
 * A colour row in the All Settings panel (interface 134) is a title, a
 * description and a swatch with a "Select" op on it, and that op's script --
 * `settings_colour_input_click`, cache script 4183 -- is two lines long:
 *
 *     [clientscript,settings_colour_input_click](int $int0, int $int1)
 *     if (~settings_op_checker($int0, $int1) = 0) {
 *         return;
 *     }
 *
 * That is the whole body. `~settings_op_checker` plays the panel's click sound
 * and, for a row the player is not allowed to change, prints the row's own
 * refusal message. Nothing writes a colour, because in the reference the
 * picker is the ENGINE's: it opens its own, and it writes the row's varp
 * itself. Read one way that makes every colour row in the panel inert here --
 * "Tile highlight colour" showed the default green swatch, said what it was
 * for, and did nothing at all when clicked. Read the other way it is the same
 * arrangement as the two Activities buttons and the client layout dropdown:
 * the cache has stated everything except the part only a client can do.
 *
 * So this is that part. RS_CS2Host_ScriptStarted catches the click with its
 * arguments intact and resolves the row -- setting id, title, default swatch,
 * and the varp the read hub was seen reading for it. Here that becomes a
 * picker on the HSL16 axes the renderer actually draws in, and its value goes
 * back into the varp the way the row stores it: `colour + 1`, so that zero
 * keeps meaning "never chosen".
 *
 * Committing to the varp is the whole apply. The cache does the rest of the
 * work it always did -- writing the varp fires the var-transmit hooks the row
 * itself installed, so `settings_colour_input_update` re-fills the swatch and,
 * for the tile markers, clientscript 4763 re-runs HIGHLIGHT_TILE_SETUP on
 * group 6 in the new colour. Nothing here knows what a tile marker is.
 * ========================================================================= */

static void
app_settings_colour_close(struct App* app)
{
    assert(app);
    app->settings_colour_visible = 0;
    if( app->settings_colour_panel >= 0 )
        ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->settings_colour_panel, 0);
}

/**
 * Write `rgb` to the open row's varp, in the row's own encoding.
 *
 * `colour + 1`, which is what `settings_get_colour` reads back with
 * `calc(%var<n> - 1)` and what makes a varp of 0 mean "never chosen" rather
 * than "black".
 */
static void
app_settings_colour_commit(
    struct App* app,
    uint32_t rgb)
{
    assert(app);
    /* A picker is only ever opened for a row whose varp is known, so this is a
     * contract and not a state to tolerate: a commit with nowhere to go would
     * be a picker the user is dragging that changes nothing. */
    assert(app->settings_colour_req.varp_id >= 0);
    RS_CS2Host_ScriptWriteVarp(
        &app->host, app->settings_colour_req.varp_id, (int)(rgb & 0xFFFFFFu) + 1);
    app->need_redraw = 1;
}

/** Put the picker beside the swatch that opened it, clamped onto the canvas. */
static void
app_settings_colour_place(
    struct App* app,
    int component_id)
{
    int scale;
    int width;
    int32_t idx;
    struct UITreeComponent const* anchor = NULL;
    struct UIPopupPlacement placement;

    assert(app);
    scale = ToriRSChrome_Scale(&app->dbg_ui);
    width = 230 * scale;
    idx = app->tree && component_id >= 0 ? UITree_FindByComponentId(app->tree, component_id) : -1;
    if( idx >= 0 )
        anchor = &app->tree->components[idx];

    /* Two thirds: the colour picker's axis popup drops BELOW the panel, so it
     * needs room under itself as well as for itself. */
    placement = UITree_PlacePopupBesideAnchor(
        UITREE_LAYOUT_ROOT_W,
        UITREE_LAYOUT_ROOT_H,
        width,
        anchor != NULL,
        anchor ? anchor->position.abs_x : 0,
        anchor ? anchor->position.abs_y : 0,
        8 * scale,
        2,
        3);

    ToriRSChrome_PanelSetFixedWidth(&app->dbg_ui, app->settings_colour_panel, width);
    ToriRSChrome_PanelMove(&app->dbg_ui, app->settings_colour_panel, placement.x, placement.y);
}

static void
app_settings_colour_open(
    struct App* app,
    struct RS_CS2SettingsColourRequest const* req)
{
    assert(app);
    assert(req);

    if( app->settings_colour_panel < 0 )
        return;
    if( req->varp_id < 0 )
    {
        /* The read hub never named a varp for this row, so there is nowhere to
         * put an answer. Said out loud rather than opening a picker whose
         * every move would be discarded. */
        TORIRS_LOG(
            "settings: colour row %d (%s) has no varp; not opening a picker\n",
            req->setting_id,
            req->label[0] ? req->label : "unnamed");
        return;
    }

    app->settings_colour_req = *req;
    ToriRSChrome_PanelClearWidgets(&app->dbg_ui, app->settings_colour_panel);
    ToriRSChrome_PanelSetTitle(
        &app->dbg_ui, app->settings_colour_panel, req->label[0] ? req->label : "Colour");
    /* Seeded through NearestRgb, not the reference quantiser: this value is
     * read back and re-shown every time the row is opened, and the reference
     * round trip moves nearly every entry by a shade each pass. */
    app->settings_colour_pick = ToriRSChrome_ColorPick(
        &app->dbg_ui,
        app->settings_colour_panel,
        "Colour",
        ToriRSChrome_Hsl16NearestRgb((uint32_t)req->colour & 0xFFFFFFu));
    app->settings_colour_default_btn =
        ToriRSChrome_Button(&app->dbg_ui, app->settings_colour_panel, "Default");
    app->settings_colour_close_btn =
        ToriRSChrome_Button(&app->dbg_ui, app->settings_colour_panel, "Done");
    ToriRSChrome_PanelSetClosable(&app->dbg_ui, app->settings_colour_panel, 1);

    app_settings_colour_place(app, req->component_id);
    ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->settings_colour_panel, 1);
    app->settings_colour_visible = 1;
    app->need_redraw = 1;
}

/*
 * Open, drive and commit the picker. Called once a frame, after the developer
 * overlay's tick has already routed this frame's input into dbg_ui.
 *
 * The activation is PEEKED and only taken when it belongs to this panel.
 * dbg_ui's activation latch is shared with the loc editor and the map editor,
 * and draining it unconditionally is how the loc editor once swallowed the map
 * editor's clicks -- a dropdown that showed the new value while nothing
 * changed. Peeking costs nothing and cannot do that to anyone.
 */
static void
app_settings_colour_tick(struct App* app)
{
    struct RS_CS2SettingsColourRequest req;
    int activated;

    assert(app);

    if( RS_CS2Host_TakeSettingsColourRequest(&app->host, &req) )
        app_settings_colour_open(app, &req);

    if( !app->settings_colour_visible )
        return;

    /*
     * The panel's own Close button hid it; the flag above is this side's idea
     * of whether the picker is up, and left unreconciled the next click on the
     * same swatch would "reopen" something that is already open.
     */
    if( app->settings_colour_panel >= 0 && !app->dbg_ui.panels[app->settings_colour_panel].visible )
    {
        app->settings_colour_visible = 0;
        return;
    }

    /*
     * Follow the panel out of existence.
     *
     * All Settings is opened and closed by the interface stack, and a picker
     * still floating over the game after the panel it belongs to is gone has
     * nothing to point at.
     *
     * Asked of the GROUP -- the interface the swatch's component id names in
     * its high half -- and not of the component. A colour row's swatch is a
     * DYNAMIC child, created by `cc_create` on a container with a component id
     * of its own, so looking that id up finds the CONTAINER: it is present for
     * as long as any of the interface is, which made the test true forever and
     * left the picker over the world after the panel had gone. The group is
     * the question actually being asked -- is the panel still open.
     */
    if( app->settings_colour_req.component_id >= 0 && app->tree &&
        !UITree_GroupPresent(app->tree, app->settings_colour_req.component_id >> 16) )
    {
        app_settings_colour_close(app);
        return;
    }

    activated = app->dbg_ui.activated;
    if( activated < 0 )
        return;
    if( activated == app->settings_colour_pick )
    {
        (void)ToriRSChrome_TakeActivated(&app->dbg_ui);
        app_settings_colour_commit(
            app,
            ToriRSChrome_Hsl16ToRgb(
                ToriRSChrome_ColorPickValue(&app->dbg_ui, app->settings_colour_pick)));
    }
    else if( activated == app->settings_colour_default_btn )
    {
        /* The DEFAULT is committed verbatim, not as the palette entry nearest
         * to it. `param_1230` is a colour the cache authored and the row draws
         * its swatch in before anyone picks; restoring it as an approximation
         * would mean "Default" never quite got back to where the row started.
         * The picker still shows the nearest entry, because that is the only
         * thing its axes can hold -- and what it shows is honestly what the
         * next pick would produce. */
        uint32_t const rgb = (uint32_t)app->settings_colour_req.default_colour & 0xFFFFFFu;
        (void)ToriRSChrome_TakeActivated(&app->dbg_ui);
        ToriRSChrome_ColorPickSet(
            &app->dbg_ui, app->settings_colour_pick, ToriRSChrome_Hsl16NearestRgb(rgb));
        app_settings_colour_commit(app, rgb);
    }
    else if( activated == app->settings_colour_close_btn )
    {
        (void)ToriRSChrome_TakeActivated(&app->dbg_ui);
        app_settings_colour_close(app);
    }

    if( ToriRSChrome_Build(&app->dbg_ui) )
    {
        app->need_redraw = 1;
        ToriRSChrome_DamageClear(&app->dbg_ui);
    }
}

/* =========================================================================
 * All Settings: the number-input rows
 *
 * The numeric twin of the colour block above, and it exists for the identical
 * reason. A number row -- built by `settings_create_input_setting`, cache
 * script 3856 -- draws its value in a boxed field with a "Select" op, and that
 * op's script is
 *
 *     [clientscript,settings_input_op](int $int0, int $int1)
 *     if (~settings_op_checker($int0, $int1) = 0) {
 *         return;
 *     }
 *     %varbit16074 = 0;
 *
 * -- a click sound, the row's refusal message when it is blocked, and a flag
 * that closes the panel's search box. Nothing types anything, because in the
 * reference the entry is the ENGINE's: `%varbit16075` ("a settings row is
 * being edited") is read by three clientscripts and written by none, and
 * `settings_input_timer` blinks a caret in a field the cache never fills.
 *
 * Untyped, those rows are not merely inert, they are the feature: the five
 * ground-items price tiers decide which of the six colours a pile's name is
 * drawn in, and `ground_items_max_lines` decides how many names appear at all.
 * At their zero default every pile draws in the tier-5 colour and, with the
 * line limit at zero, nothing draws.
 *
 * Committing to the varp is the whole apply, exactly as it is for a colour:
 * the row installed its own var-transmit hook (`settings_input_setting_
 * transmit`), so the field repaints itself and the overlay's own hooks pick
 * the new thresholds up.
 * ========================================================================= */

static void
app_settings_number_close(struct App* app)
{
    assert(app);
    app->settings_number_visible = 0;
    if( app->settings_number_panel >= 0 )
        ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->settings_number_panel, 0);
}

/*
 * Commit what is typed in the box.
 *
 * Stored PLAIN, unlike a colour row's `value + 1`: zero is a real answer for
 * every one of these rows (a threshold of 0 colours everything at that tier,
 * a line limit of 0 hides the overlay), so there is no never-chosen sentinel
 * to make room for -- which is also why `settings_get_number_input` reads them
 * back with a bare `%var<n>` and not a `calc(%var<n> - 1)`.
 *
 * A field emptied and confirmed commits zero rather than being ignored: "off"
 * is a thing these rows can be set to, and the row's own `param_1113` is the
 * word it draws for it.
 */
static void
app_settings_number_commit(struct App* app)
{
    char const* text;
    long value;

    assert(app);
    /* A box is only ever opened for a row whose varp is known. */
    assert(app->settings_number_req.varp_id >= 0);
    text = ToriRSChrome_Text(&app->dbg_ui, app->settings_number_input);
    value = strtol(text, NULL, 10);
    /* Clamped rather than asserted: this is a number a person typed, and
     * "2000000000000" is a typo and not a caller's bug. The floor is zero
     * because none of these rows means anything negative -- a threshold below
     * nothing would colour every pile at that tier for ever. */
    if( value < 0 )
        value = 0;
    if( value > INT_MAX )
        value = INT_MAX;
    RS_CS2Host_ScriptWriteVarp(&app->host, app->settings_number_req.varp_id, (int)value);
    app->need_redraw = 1;
}

/** Put the box beside the field that opened it, clamped onto the canvas. */
static void
app_settings_number_place(
    struct App* app,
    int component_id)
{
    int scale;
    int width;
    int32_t idx;
    struct UITreeComponent const* anchor = NULL;
    struct UIPopupPlacement placement;

    assert(app);
    scale = ToriRSChrome_Scale(&app->dbg_ui);
    width = 200 * scale;
    idx = app->tree && component_id >= 0 ? UITree_FindByComponentId(app->tree, component_id) : -1;
    if( idx >= 0 )
        anchor = &app->tree->components[idx];

    /* Three quarters, not two thirds: a number entry has no axis popup under
     * it, so it may sit lower than the colour picker. */
    placement = UITree_PlacePopupBesideAnchor(
        UITREE_LAYOUT_ROOT_W,
        UITREE_LAYOUT_ROOT_H,
        width,
        anchor != NULL,
        anchor ? anchor->position.abs_x : 0,
        anchor ? anchor->position.abs_y : 0,
        8 * scale,
        3,
        4);

    ToriRSChrome_PanelSetFixedWidth(&app->dbg_ui, app->settings_number_panel, width);
    ToriRSChrome_PanelMove(&app->dbg_ui, app->settings_number_panel, placement.x, placement.y);
}

static void
app_settings_number_open(
    struct App* app,
    struct RS_CS2SettingsNumberRequest const* req)
{
    char value[32];
    char label[128];

    assert(app);
    assert(req);

    if( app->settings_number_panel < 0 )
        return;
    if( req->varp_id < 0 )
    {
        /* The read hub never named a varp for this row, so there is nowhere to
         * put an answer. Said out loud rather than opening a box whose every
         * keystroke would be discarded. */
        TORIRS_LOG(
            "settings: number row %d (%s) has no varp; not opening an entry\n",
            req->setting_id,
            req->label[0] ? req->label : "unnamed");
        return;
    }

    app->settings_number_req = *req;
    snprintf(value, sizeof(value), "%d", req->value);
    /* The row's own suffix on the box's label, so the two agree about what is
     * being typed -- "Value (gp)" over a field the panel prints as "20,000 gp".
     * Its zero-word goes in the same place, because it is the other half of
     * what this number means to the row. */
    if( req->suffix[0] && req->zero_label[0] )
        snprintf(label, sizeof(label), "Value (%s, 0 = %s)", req->suffix, req->zero_label);
    else if( req->suffix[0] )
        snprintf(label, sizeof(label), "Value (%s)", req->suffix);
    else if( req->zero_label[0] )
        snprintf(label, sizeof(label), "Value (0 = %s)", req->zero_label);
    else
        snprintf(label, sizeof(label), "Value");

    ToriRSChrome_PanelClearWidgets(&app->dbg_ui, app->settings_number_panel);
    ToriRSChrome_PanelSetTitle(
        &app->dbg_ui, app->settings_number_panel, req->label[0] ? req->label : "Value");
    app->settings_number_input =
        ToriRSChrome_TextInput(&app->dbg_ui, app->settings_number_panel, label, value);
    app->settings_number_close_btn =
        ToriRSChrome_Button(&app->dbg_ui, app->settings_number_panel, "Done");
    ToriRSChrome_PanelSetClosable(&app->dbg_ui, app->settings_number_panel, 1);

    app_settings_number_place(app, req->component_id);
    ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->settings_number_panel, 1);
    app->settings_number_visible = 1;
    app->need_redraw = 1;

    /* fprintf, and gated on its own name: a dbg_ui panel reaches the platform
     * renderer as ToriRSChrome_Prims and is invisible to every BMP path this
     * client has, so a screenshot cannot answer "did the box open" -- and
     * TORIRS_LOG is stripped from the optimized build, which is the only build
     * worth taking a screenshot of. Same choice as TORIRS_GROUND_ITEMS_DEBUG. */
    if( getenv("TORIRS_SETTINGS_DEBUG") )
        fprintf(
            stderr,
            "settings: number entry open, setting=%d varp=%d value=%d \"%s\"\n",
            req->setting_id,
            req->varp_id,
            req->value,
            req->label);
}

/* Open, drive and commit the entry. The activation is PEEKED and only taken
 * when it belongs to this panel, for the same reason the colour tick peeks. */
static void
app_settings_number_tick(struct App* app)
{
    struct RS_CS2SettingsNumberRequest req;
    int activated;

    assert(app);

    if( RS_CS2Host_TakeSettingsNumberRequest(&app->host, &req) )
        app_settings_number_open(app, &req);

    if( !app->settings_number_visible )
        return;

    /* The panel's own Close button hid it. */
    if( app->settings_number_panel >= 0 && !app->dbg_ui.panels[app->settings_number_panel].visible )
    {
        app->settings_number_visible = 0;
        return;
    }

    /* Follow All Settings out. Asked of the GROUP, not the component: the
     * field is a dynamic child whose component id names its container, which
     * outlives the row. Same trap as the colour picker's. */
    if( app->settings_number_req.component_id >= 0 && app->tree &&
        !UITree_GroupPresent(app->tree, app->settings_number_req.component_id >> 16) )
    {
        app_settings_number_close(app);
        return;
    }

    activated = app->dbg_ui.activated;
    if( activated < 0 )
        return;
    if( activated == app->settings_number_input )
    {
        /* Enter, which is when a chrome text input activates. The box stays up
         * so a mistyped threshold can be corrected without clicking the row
         * again. */
        (void)ToriRSChrome_TakeActivated(&app->dbg_ui);
        app_settings_number_commit(app);
    }
    else if( activated == app->settings_number_close_btn )
    {
        (void)ToriRSChrome_TakeActivated(&app->dbg_ui);
        app_settings_number_commit(app);
        app_settings_number_close(app);
    }

    if( ToriRSChrome_Build(&app->dbg_ui) )
    {
        app->need_redraw = 1;
        ToriRSChrome_DamageClear(&app->dbg_ui);
    }
}

void
App_SetWorldRenderMode(
    struct App* app,
    enum ToriRS_WorldRenderMode mode)
{
    if( app )
        app->world_render_mode = mode;
}

void
App_SetRendererAnimatesTextures(
    struct App* app,
    bool animates)
{
    assert(app);
    app->renderer_animates_textures = animates;
}

/* Defined with the other map-editor helpers below; App_Init registers it as
 * the editor session's shared-state sink. */
static void
app_editor_on_state(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count);

/*
 * The boot cannot proceed, for a reason the person running this can fix.
 *
 * A missing cache and a cache server that is not up are DEPLOYMENT states, not
 * contract violations: an assert would name the wrong culprit, and carrying on
 * is worse than either -- that is what this client used to do, limping past a
 * failed on-demand enable with no cache provider at all and taking SIGSEGV in
 * the first buildcache lookup, a mile from the cause.
 *
 * So it refuses, loudly, in one sentence addressed to whoever has to act on it.
 * WHERE that sentence has to land is the platform's business and not this
 * function's: a desktop run prints it to the terminal the command was typed in
 * and exits, and on Android there is no such terminal -- exit() there kills the
 * process, the activity vanishes to the launcher, and the diagnosis sits in
 * logcat where nobody holding a phone will read it. Which is to say it reads
 * exactly like a crash. @see PlatformAndroid_BootFailed.
 */
static void
app_boot_refuse(char const* message)
{
    assert(message);
    TORIRS_ERR("app: %s\n", message);
#if defined(TORIRS_PLATFORM_ANDROID)
    /* Hands the message to the boot menu and ends the frame thread; the process
     * survives, so the gear can fix the profile and the next run is a new run
     * rather than a new launch. Does not return. */
    PlatformAndroid_BootFailed(message);
#endif
    exit(1);
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
static void
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

#include "app_construct.u.c"

/*
 * Cache font id for a RevConfig `[font:<name>]` section on this cache.
 *
 * -1 when the profile does not declare it, which every caller already handles
 * the same way it handles a font that has not finished loading: draw nothing,
 * or fall back to whatever font a text node already resolved.
 */
static int
app_font_cache_id(
    struct App const* app,
    char const* font_name)
{
    assert(app);
    assert(font_name);
    return RevConfigRefs_FontCacheId(
        &app->revconfig_refs, font_name, app->cfg.cache_kind == APP_CACHE_DAT1);
}

static int
app_font_b12_cache_id(struct App const* app)
{
    return app_font_cache_id(app, APP_FONT_B12);
}

/*
 * Packed component uid — `(iface << 16) | child` — for a RevConfig
 * `[iface:<name>]` section, or -1 when this profile declares no such interface.
 *
 * The CHILD number stays in C: which component of the XP panel holds the stat
 * listener is a fact about that interface's own layout, and it travels with the
 * interface. Which id the interface HAS does not, so that half is the
 * profile's.
 */
static int
app_iface_com(
    struct App const* app,
    char const* iface_name,
    int child)
{
    int iface;
    assert(app);
    assert(iface_name);
    assert(child >= 0);
    iface = RevConfigRefs_Get(&app->revconfig_refs, "iface", iface_name);
    if( iface < 0 )
        return -1;
    return (iface << 16) | child;
}

/** Id of a `[setting:<name>]` row, or -1 when this profile has no such row. */
static int
app_setting_id(
    struct App const* app,
    char const* setting_name)
{
    assert(app);
    assert(setting_name);
    return RevConfigRefs_Get(&app->revconfig_refs, "setting", setting_name);
}

/* Scene font for hitsplat numbers; queues the load on a miss the same way
 * app_minimenu_font_scene_id does, and returns -1 until it lands.
 *
 * -1, not 0: scene font ids ARE cache font ids, and dat1 p11 is cache id 0 —
 * the same trap that once left every p11 label invisible. */
static int
app_hitsplat_font_scene_id(struct App* app)
{
    int font_cache_id = app_font_cache_id(app, APP_FONT_P11);
    int scene_id;
    if( font_cache_id < 0 )
        return -1;
    scene_id = UITreeSceneBridge_EnsureFont(&app->bridge, font_cache_id);
    if( scene_id < 0 )
    {
        struct ToriRS_Task* task = CreateTask_FontLoad(app->provider, font_cache_id);
        if( task )
            ToriRS_TaskQueue_Add(app->runner.queue, task);
    }
    return scene_id;
}

static int
app_minimenu_font_scene_id(struct App* app)
{
    int font_cache_id = app_font_b12_cache_id(app);
    int scene_id =
        font_cache_id >= 0 ? UITreeSceneBridge_EnsureFont(&app->bridge, font_cache_id) : -1;
    if( scene_id <= 0 && font_cache_id >= 0 )
    {
        /* Queue the load (no blocking drain — the boot task awaits this font
         * before binding the configured overlay models, so at runtime a miss
         * just falls through to the text-node scan below until it lands). */
        struct ToriRS_Task* task = CreateTask_FontLoad(app->provider, font_cache_id);
        if( task )
            ToriRS_TaskQueue_Add(app->runner.queue, task);
    }
    if( scene_id <= 0 )
    {
        for( uint32_t i = 0; i < app->tree->component_count; i++ )
        {
            struct UITreeComponent const* node = &app->tree->components[i];
            if( !node->freed && node->type == UIELEM_RS_TEXT && node->u.rs_text.font_id > 0 )
            {
                scene_id = node->u.rs_text.font_id;
                break;
            }
        }
    }
    return scene_id;
}

/* Bind app-owned overlay models to their revision-configured nodes. No node
 * means that overlay does not exist for the revision; there is intentionally
 * no C fallback that changes the shape of the UITree. */
static void
app_bind_configured_overlays(struct App* app)
{
    app->interact.minimenu.font_id = -1;
    app->hover_text.font_id = -1;
    for( uint32_t i = 0; i < app->tree->component_count; i++ )
    {
        struct UITreeComponent const* node = &app->tree->components[i];
        if( node->freed )
            continue;
        if( node->type == UIELEM_BUILTIN_MINIMENU )
            app->interact.minimenu.font_id = node->u.minimenu.font_id;
        else if( node->type == UIELEM_BUILTIN_HOVERTEXT )
            app->hover_text.font_id = node->u.hovertext.font_id;
    }
}

/* World_HeightFn: projectiles/movers track terrain height (world units). */
static int
app_world_height(
    void* userdata,
    int world_x,
    int world_z,
    int level)
{
    struct App* app = (struct App*)userdata;

    assert(app);
    return World_HeightAt(app->world, world_x, world_z, level);
}

/* WevHeightFn: terrain under a hull, for the world-entity interpolator.
 *
 * Two things separate this from app_world_height. The sample belongs to the
 * view the boat floats IN (the root for a boat, a carrier's deck later), not
 * to app->world by assumption. And a Wev transform is absolute root-world fine
 * units off the wire, while every World samples its heightmap in scene-local
 * units — feeding the absolute value straight in puts every real boat outside
 * [0,scene_size) and the out-of-scene guard flattens it to y 0. */
static int
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

/** The registry slot whose World is `world`, or -1. Views are 16 and the
 * lookup runs once per painter pass, so a scan beats a back-pointer. */
static int
app_worldview_id_of(
    struct App* app,
    const struct World* world)
{
    assert(app);
    assert(world);
    for( int i = 0; i < WORLDVIEW_MAX; i++ )
    {
        if( WorldviewRegistry_IsLive(&app->worldviews, i) &&
            WorldviewRegistry_Get(&app->worldviews, i)->world == world )
            return i;
    }
    return -1;
}

struct WevDeckBox;
static void
app_wev_deck_box(
    struct App* app,
    struct Wev const* wev,
    struct World const* parent_world,
    struct WevDeckBox* out_box);
static void
app_wev_decide_flatten(struct App* app);

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
            int boat_x;
            int boat_z;
            int dx;
            int dy;
            int dz;
            int inv_angle;
            int cs;
            int sn;
            int deck_x;
            int deck_z;
            int recenter_x;
            int recenter_z;
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

            boat_x = wev->x - (parent_world->_base_tile_x << 7);
            boat_z = wev->z - (parent_world->_base_tile_z << 7);
            dx = cam_x - boat_x;
            dy = cam_y - wev->y;
            dz = cam_z - boat_z;

            inv_angle = (2048 - (wev->angle & 0x7ff)) & 0x7ff;
            cs = ToriDraw_Cos(inv_angle);
            sn = ToriDraw_Sin(inv_angle);
            deck_x = (dx * cs + dz * sn) >> 16;
            deck_z = (dz * cs - dx * sn) >> 16;

            recenter_x = -(view->size_x_tiles * 64) - wev->config->pivot_x;
            recenter_z = -(view->size_z_tiles * 64) - wev->config->pivot_z;
            deck_x -= recenter_x;
            deck_z -= recenter_z;

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

            assert(wev);
            assert(wev->config);
            if( !WorldviewRegistry_IsLive(&app->worldviews, wev->id) )
                continue;
            view = WorldviewRegistry_Get(&app->worldviews, wev->id);
            assert(view->world);

            ToriRS_FrameSetViewXform(
                frame,
                wev->id,
                view->world,
                -(view->size_x_tiles * 64) - wev->config->pivot_x,
                -(view->size_z_tiles * 64) - wev->config->pivot_z,
                wev->x - (parent_world->_base_tile_x << 7),
                /* + the bob: the animaya root-bone Y multiplied into the
                 * whole sub-scene (app_wev_advance_bobs), the deob's
                 * class112.method4034 matrix chain reduced to its
                 * translation term. */
                wev->y + wev->bob_y,
                wev->z - (parent_world->_base_tile_z << 7),
                wev->angle);
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
 * fine units. Mirrors app_wev_bind_view_cameras' arithmetic exactly — same
 * recenter, same base-tile subtraction — because a disagreement between the
 * two is an actor drawn somewhere the camera is not looking.
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
    out_box->pos_x = wev->x - (parent_world->_base_tile_x << 7);
    out_box->pos_z = wev->z - (parent_world->_base_tile_z << 7);
    out_box->angle = wev->angle;
    out_box->recenter_x = -(view->size_x_tiles * 64) - wev->config->pivot_x;
    out_box->recenter_z = -(view->size_z_tiles * 64) - wev->config->pivot_z;
    out_box->size_x_tiles = view->size_x_tiles;
    out_box->size_z_tiles = view->size_z_tiles;
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
    assert(out_local_x);
    assert(out_local_z);

    for( int id = 1; id < WORLDVIEW_MAX; id++ )
    {
        struct Worldview const* view;

        if( !WorldviewRegistry_IsLive(&app->worldviews, id) )
            continue;
        view = WorldviewRegistry_Get(&app->worldviews, id);
        /* base 0,0 is the registration default until REBUILD_WORLDENTITY
         * names the staging square; a real deck base is never the map
         * origin. Reservations do not overlap, so first match is only
         * match. */
        if( view->base_x == 0 && view->base_z == 0 )
            continue;
        if( view->size_x_tiles <= 0 || view->size_z_tiles <= 0 )
            continue;
        if( abs_tile_x < view->base_x || abs_tile_x >= view->base_x + view->size_x_tiles )
            continue;
        if( abs_tile_z < view->base_z || abs_tile_z >= view->base_z + view->size_z_tiles )
            continue;
        *out_local_x = abs_tile_x - view->base_x;
        *out_local_z = abs_tile_z - view->base_z;
        return id;
    }
    *out_local_x = abs_tile_x;
    *out_local_z = abs_tile_z;
    return 0;
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

/* Reference drawDetail's mapscene pass: after the tile/wall bake, plot each loc
 * mapscene sprite gathered at scene build (world->mapscenes) for the level being
 * baked. The mapscene atlas lives in the scene, so this runs in app.c rather than
 * the leaf minimap layer. Level selection matches the tile bake's VisBelow
 * composition (minimap_bake_argb): an icon on the baked level draws unless its
 * tile is a hole onto the level below, and an icon one level up draws where that
 * tile is VisBelow (balcony/overhang showing the floor beneath). */
static void
app_bake_mapscenes(
    struct App* app,
    uint32_t* argb,
    int pw,
    int ph,
    int level)
{
    struct World* world = app->world;
    int mapscene_scene;
    int count = 0;
    struct ToriDraw_Sprite** frames;
    int scene_size, plane;
    uint8_t const* flags;

    if( !world || world->mapscene_count <= 0 || !world->minimap )
        return;
    mapscene_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPSCENE);
    if( mapscene_scene <= 0 )
        return;
    frames = ToriDraw_SceneSpriteGet(app->scene, mapscene_scene, &count);
    if( !frames || count <= 0 )
        return;

    scene_size = world->_scene_size;
    plane = scene_size * scene_size;
    flags = world->tile_flags;

    for( int i = 0; i < world->mapscene_count; i++ )
    {
        struct World_MapSceneIcon const* icon = &world->mapscenes[i];
        struct ToriDraw_Sprite* spr;
        int idx, draw = 0;

        if( icon->mapscene < 0 || icon->mapscene >= count )
            continue;
        if( icon->x < 0 || icon->x >= scene_size || icon->z < 0 || icon->z >= scene_size )
            continue;
        spr = frames[icon->mapscene];
        if( !spr || !spr->pixels_argb || spr->width <= 0 || spr->height <= 0 )
            continue;

        idx = icon->x + icon->z * scene_size;
        if( icon->level == level &&
            (!flags || (flags[idx + level * plane] &
                        (MINIMAP_FLAG_VIS_BELOW | MINIMAP_FLAG_FORCE_HIGH_DETAIL)) == 0) )
            draw = 1;
        else if(
            flags && icon->level == level + 1 && level + 1 < world->minimap->levels &&
            (flags[idx + (level + 1) * plane] & MINIMAP_FLAG_VIS_BELOW) != 0 )
            draw = 1;
        if( !draw )
            continue;

        minimap_plot_mapscene(
            argb,
            pw,
            ph,
            spr->pixels_argb,
            spr->width,
            spr->height,
            spr->crop_x,
            spr->crop_y,
            icon->x,
            icon->z,
            world->minimap->height,
            icon->width,
            icon->length);
    }
}

/* Bake the loaded world's minimap tiles into a single scene sprite the minimap
 * widget blits from (v1 GameRunescape_RebuildWorldMap). SceneSpriteAdd frees any
 * previous entry, so the reload hotkey just overwrites in place.
 *
 * The bake is per level (reference minimapBuildBuffer(minusedlevel)), so it has
 * to be redone whenever the local player changes floor — app_world_map_poll. */
static void
app_rebuild_world_map(
    struct App* app,
    int level)
{
    int pixel_w = 0;
    int pixel_h = 0;
    uint32_t* argb;
    struct ToriDraw_Sprite* sprite;
    struct ToriDraw_Sprite** sprites;

    assert(app);
    assert(app->world);

    if( !app->world->minimap )
        return;

    argb =
        minimap_bake_argb(app->world->minimap, level, app->world->tile_flags, &pixel_w, &pixel_h);
    if( !argb )
        return;

    /* Reference drawDetail plots loc mapscene sprites (trees, rocks, altars, …)
     * into the same minimap image as the tiles/walls. */
    app_bake_mapscenes(app, argb, pixel_w, pixel_h, level);

    /* TORIRS_MINIMAP_BMP=path: the baked map straight to disk. The on-screen
     * minimap is a rotated, camera-anchored crop of this and needs a local
     * player to center on, so offline runs can only inspect the bake here. */
    if( getenv("TORIRS_MINIMAP_BMP") )
    {
        bmp_write_file(getenv("TORIRS_MINIMAP_BMP"), (int*)argb, pixel_w, pixel_h);
        TORIRS_LOG(
            "minimap: wrote %s (%dx%d level=%d)\n",
            getenv("TORIRS_MINIMAP_BMP"),
            pixel_w,
            pixel_h,
            level);
    }

    sprite = ToriDraw_SpriteNewFromArgbOwned(argb, pixel_w, pixel_h);
    if( !sprite )
    {
        free(argb);
        return;
    }

    sprites = malloc(sizeof(*sprites));
    assert(sprites);
    sprites[0] = sprite;

    ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_WORLD_MAP_SPRITE_ID, sprites, 1);
    app->world_map_scene_id = UITREE_SCENE_WORLD_MAP_SPRITE_ID;
    app->world_map_w = pixel_w;
    app->world_map_h = pixel_h;
    app->world_map_level = level;
#if defined(TORIRS_HAVE_GLES2)
    {
        /* The GLES2 renderer keeps a GPU copy of the pixels behind the
         * rotated-masked minimap and has no event that says they changed --
         * this is the one place they do. Declared here rather than through
         * its header because this is the one call site in this file. */
        void ToriRS_GLES2_RotmaskSourceChanged(void);
        ToriRS_GLES2_RotmaskSourceChanged();
    }
#endif
}

/* The level the minimap lives at: aboard, the rider's own level is a DECK
 * plane (the planking is authored at plane 1) while the minimap is the ROOT
 * world's — the deob renders it from the main world around the projected
 * position. Bake and cull with the hull's root level, or a plane-1 rider
 * gets the (empty) level-1 bake: a black map with floating icons. */
static int
app_minimap_level(
    struct App* app,
    struct WorldEntity_Player const* local)
{
    (void)local;
    /* One authority for the effective root plane — see app_cinema_level. */
    return app_cinema_level(app);
}

/* Reference checkMinimap/minimapBuildBuffer trigger (Client.ts:5331): rebake
 * whenever the level the map was baked for stops matching the player's, or a
 * runtime loc change edited the wall/door bits (world->minimap_seq — an opened
 * door's red line has to move on the baked sprite). */
static void
app_world_map_poll(struct App* app)
{
    static unsigned baked_minimap_seq = 0;
    struct WorldEntity_Player* local;

    if( !app->world || !app->world->load_complete || app->world_map_scene_id <= 0 )
        return;
    local = app_local_player(app);
    if( !local )
        return;
    if( app_minimap_level(app, local) == app->world_map_level &&
        baked_minimap_seq == app->world->minimap_seq )
        return;
    baked_minimap_seq = app->world->minimap_seq;
    app_rebuild_world_map(app, app_minimap_level(app, local));
    app->need_redraw = 1;
}

/* The most squares one offline TORIRS_WORLD_MAP load may name. 16 is a 4x4
 * block, 256x256 tiles -- well past the 104x104 the live client keeps
 * resident, so the cap bounds the array without capping any scene worth
 * meshing. */
#define APP_WORLD_MAP_SQUARE_MAX 16

/* Task_WorldLoad on_done trampoline: adapts the void* hook to App_WorldLoadFinish. */
static void
app_world_load_finish_cb(void* userdata)
{
    App_WorldLoadFinish((struct App*)userdata);
}

/* Queue Task_WorldLoad for a chunk list; never blocks. App_WorldLoadFinish runs
 * as the task's on_done the moment the load lands (no polling). Reused by the
 * reload hotkey and the first-load trigger; assets already cached make a reload
 * near-instant. chunks == NULL -> the configured/default map. The REBUILD_NORMAL
 * packet task queues its own load (it awaits it) rather than calling here. */
static void
app_world_load_begin(
    struct App* app,
    int const* chunks_xz,
    int chunk_pair_count)
{
    int chunks[APP_WORLD_MAP_SQUARE_MAX * 2] = { 50, 50 };
    struct ToriRS_Task* task;

    /* Same seam as CacheProvider_TrimDerivedCaches inside Task_WorldLoad:
     * previous scene's instance bases are no longer live. */
    TorirsModelInstCache_Clear(&app->model_inst_cache);

    if( !chunks_xz )
    {
        char const* env;
        int pair_count = 1;

        /*
         * Spawn square precedence: TORIRS_WORLD_MAP, then the manifest's `[cache:boot] spawn`,
         * then the client default of 50,50.
         *
         * The manifest layer matters because 50,50 is not universally loadable. A cache carries
         * XTEA keys only for the squares it was dumped with, and cache.643 has no key for
         * 50,50 (nor 49,49 / 50,49 / 51,49 / 51,50 — a hole right over Lumbridge). Terrain is
         * unencrypted, so an unkeyed square still renders ground and then **zero locs**, which
         * looks like a broken renderer rather than absent data.
         */
        if( app->cfg.spawn_x >= 0 && app->cfg.spawn_z >= 0 )
        {
            chunks[0] = app->cfg.spawn_x;
            chunks[1] = app->cfg.spawn_z;
        }
        env = getenv("TORIRS_WORLD_MAP");
        if( env )
        {
            /* Into scratch, not `chunks`: a list that turns out to be malformed
             * halfway through must not have already overwritten the manifest
             * square the message below is about to name as the fallback. */
            int parsed_chunks[APP_WORLD_MAP_SQUARE_MAX * 2];
            int parsed = ToriRS_EnvChunkList(env, parsed_chunks, APP_WORLD_MAP_SQUARE_MAX);
            if( parsed > 0 )
            {
                memcpy(chunks, parsed_chunks, sizeof(int) * 2 * (size_t)parsed);
                pair_count = parsed;
            }
            else
            {
                TORIRS_LOG(
                    "TORIRS_WORLD_MAP must be \"x,z\", or up to %d such squares "
                    "separated by ';', got '%s' - using %d,%d\n",
                    APP_WORLD_MAP_SQUARE_MAX,
                    env,
                    chunks[0],
                    chunks[1]);
            }
        }
        chunks_xz = chunks;
        chunk_pair_count = pair_count;
    }

    /*
     * Hold the camera across the reload.
     *
     * Every editor edit lands here through app_map_editor_drain's chunklist
     * rebuild, and without this each paint click snapped the eye back to the
     * scene centre -- the finish path places the camera for a FIRST look at a
     * scene, and a rebuild is not a first look. Absolute coordinates, so the
     * restore survives the scene window moving; see cam_keep_valid in app.h.
     */
    if( app->world && app->world_active )
    {
        app->cam_keep_valid = 1;
        app->cam_keep_abs_x = app->world->_base_tile_x * 128 + app->world_camera_pos.x;
        app->cam_keep_abs_z = app->world->_base_tile_z * 128 + app->world_camera_pos.z;
        app->cam_keep_y = app->world_camera_pos.y;
        app->cam_keep_pitch = app->world_camera.pitch;
        app->cam_keep_yaw = app->world_camera.yaw;
    }

    app->world_load_attempted = 1;
    app->world_load_inflight = 1;
    App_WorldDrainEntityRemoved(app);

    /*
     * Editor boots seed the provider from the content tree before the load
     * runs, so what gets meshed is the `.jm2`/`.jl2` text being edited rather
     * than the last bake. Task_WorldLoad skips a square the provider already
     * holds, so the text wins simply by being there first — the editor never
     * has to invalidate or race the cache path, and an unsaved edit is visible
     * without a bake.
     *
     * A square the content tree does not carry is left alone and loads from the
     * cache as usual, which is what lets an editor session sit at the edge of
     * authored content and still see the world around it.
     */
    if( app->editor )
    {
        for( int i = 0; i < chunk_pair_count; i++ )
            Editor_LoadSquare(app->editor, app->provider, chunks_xz[i * 2], chunks_xz[i * 2 + 1]);
    }

    task = CreateTask_WorldLoad(
        app->provider,
        app->world_builder,
        app->runner.queue,
        chunks_xz,
        chunk_pair_count,
        -1,
        -1,
        104,
        NULL,
        app_world_load_finish_cb,
        app);
    ToriRS_TaskQueue_Add(app->runner.queue, task);
    app->need_redraw = 1;
}

/**
 * Whether the map editor's SELECT tool is the thing the minimenu should be
 * offering "Select wall/object/decor/terrain" rows for -- panel closed or a
 * paint tool active both mean no such row belongs on the menu, same as
 * `app->locedit_visible` gates the loc editor's own Select row.
 */
static bool
app_mapedit_select_active(struct App const* app)
{
    assert(app);
    return app->editor_panel.visible && app->editor_panel.tool == EDITOR_TOOL_SELECT;
}

static void
app_map_editor_ghost_forget(struct App* app);

/** Keyboard belongs to the catalog's model view? (Focused via a click; the
 *  chrome holds the focus, the app routes the keys.) */
static int
app_modelview_focused(struct App const* app)
{
    int const f = app->dbg_ui.focus;
    return f >= 0 && f < app->dbg_ui.widget_count &&
           app->dbg_ui.widgets[f].kind == TORIRS_CHROME_W_MODELVIEW;
}

/**
 * A click in the world applies the current tool, as one undoable edit.
 *
 * Gated on `input_frame_consumed` so a click that landed on the panel does not
 * also paint the tile behind it -- the overlay sets that flag when it takes a
 * press, and this runs after it for exactly that reason.
 *
 * ALSO gated on the minimenu owning this gesture, which is TWO conditions and
 * not one. `input_frame_consumed` covers neither: this runs early in the frame
 * (before UITree_InteractFrame), so nothing has classified the click yet.
 *
 *   - `minimenu.visible` -- a menu is on screen, so the world is not taking
 *     clicks at all.
 *   - `interact.swallow_left_click` -- the menu already consumed the PRESS
 *     edge of this click and this is the matching RELEASE.
 *
 * The second is the one that actually bites, and checking only the first is
 * why "Select Object" still latched terrain after it was supposedly fixed:
 * the minimenu selects on mousedown and hides itself immediately, so by the
 * time the mouse-up arrives -- the edge THIS function triggers on, a frame
 * later -- `minimenu.visible` is already 0 and the gate opens. The latch is
 * still set at that instant because interact_frame retires it further down
 * the same frame, after this ran. Reading it here is not a race: this runs
 * before the retire by construction, which is the same ordering that made
 * the bug.
 *
 * The tile is the one under the cursor THIS frame. The pickset and hover are
 * refreshed by the render pass, so they are at most one frame stale, which at
 * mouse speed is the tile the user is looking at.
 */
static void
app_map_editor_world_click(
    struct App* app,
    struct LibToriRS_Input* input)
{
    assert(app);
    assert(input);

    if( !app->editor || !app->editor_panel.visible )
        return;
    if( app->interact.minimenu.visible || app->interact.swallow_left_click )
    {
        if( getenv("TORIRS_EDIT_DEBUG") && input->curr.mouse_button_up[TORIRSM_LEFT] )
            TORIRS_LOG(
                "edit: click belongs to the minimenu (visible=%d swallow=%d)\n",
                app->interact.minimenu.visible,
                app->interact.swallow_left_click);
        return;
    }
    if( app->input_frame_consumed )
    {
        if( getenv("TORIRS_EDIT_DEBUG") && input->curr.mouse_button_up[TORIRSM_LEFT] )
            TORIRS_LOG("edit: click swallowed (input_frame_consumed)\n");
        return;
    }
    if( !input->curr.mouse_button_up[TORIRSM_LEFT] )
        return;
    if( getenv("TORIRS_EDIT_DEBUG") )
        TORIRS_LOG(
            "edit: click tool=%d consumed=%d hover=%d,%d\n",
            (int)app->editor_panel.tool,
            app->input_frame_consumed,
            app->world_hover_tile_x,
            app->world_hover_tile_z);
    if( app->world_hover_tile_x < 0 )
        return;

    /*
     * Modifier accelerators: hold a key to act on a layer without changing the
     * tool dropdown first.
     *
     *   L  place the catalog's picked loc      (the tool's Place loc)
     *   K  delete the loc under the cursor     (the tool's Delete loc)
     *   C  clear every loc on the tile
     *
     * Deliberately resolving to the SAME functions the tool rows call rather
     * than to a parallel path, so the readout, the undo step and the document
     * write are identical however the edit was asked for. Gated on
     * app_text_input_focused via the caller's chain -- without that, `L` typed
     * into the catalog's search box would place a loc.
     */
    if( !app_text_input_focused(app) )
    {
        int const level = Editor_PanelEditLevel(&app->editor_panel, app);
        int const hx = app->world_hover_tile_x;
        int const hz = app->world_hover_tile_z;

        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_L) )
        {
            Editor_PanelPlaceLocAt(&app->editor_panel, app, hx, hz, level);
            app->need_redraw = 1;
            return;
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_K) )
        {
            Editor_PanelDeleteLocAt(&app->editor_panel, app, hx, hz, level);
            app->need_redraw = 1;
            return;
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_C) )
        {
            Editor_PanelClearLocsAt(&app->editor_panel, app, hx, hz, level);
            app->need_redraw = 1;
            return;
        }
    }

    /* SELECT latches the plain-click default: the hovered TILE, unambiguous
     * even where a wall, a wall-decor and a ground loc share it. Picking one
     * of those exactly is what the minimenu's "Select wall/object/decor" rows
     * are for (app_minimenu_run_option) -- this is the one-click fallback for
     * "just the ground". */
    if( app->editor_panel.tool == EDITOR_TOOL_SELECT )
    {
        Editor_PanelSelectTerrain(
            &app->editor_panel,
            app,
            app->world_hover_tile_x,
            app->world_hover_tile_z,
            app->world_hover_tile_level);
        app->need_redraw = 1;
        return;
    }

    /* One click is one undo step. A drag would open the stroke on press and
     * close it on release; this is the single-click case, which is a stroke of
     * one and needs no bracketing.
     *
     * The level the PANEL says to edit, not the one the pick happened to
     * return: a pinned plane is the whole point of the Level row, and reading
     * the hover here would silently ignore it. */
    Editor_PanelApplyToolAt(
        &app->editor_panel,
        app,
        app->world_hover_tile_x,
        app->world_hover_tile_z,
        Editor_PanelEditLevel(&app->editor_panel, app));

    /* A Place or Move click landed on the ghost's tile: the real add just
     * replaced the ghost's element, so the ghost must be FORGOTTEN, not
     * removed -- removing now would delete the loc that was just placed. */
    if( app->editor_panel.tool == EDITOR_TOOL_LOC_PLACE ||
        app->editor_panel.tool == EDITOR_TOOL_LOC_MOVE )
        app_map_editor_ghost_forget(app);

    /*
     * The subject follows the work -- BY KIND.
     *
     * A tile tool's click latches the tile it painted, so the readout
     * describes what just happened and Apply repeats there. The LOC tools do
     * NOT latch terrain: their subject is a loc, and stamping a terrain latch
     * after every place/move wiped the loc selection the user was working
     * with -- Place selects what it placed (inside PlaceLocAt), Move keeps
     * the selection riding the loc, and Delete's handler latches the vacated
     * tile itself. Switching tools never touches the selection at all.
     */
    app->need_redraw = 1;
}

/**
 * Push the frame's edits back into the provider and rebuild what they changed.
 *
 * Once per frame, not once per edit: a brush drag produces a command per tile,
 * and remeshing a square for each of them would spend the frame rebuilding
 * terrain nobody has seen yet. Draining here coalesces them, so a drag costs
 * one rebuild per square per frame however fast the mouse moves.
 */
/** Forget the ghost WITHOUT removing it from the scene -- for the commit
 *  click, whose real placement just replaced the ghost's element on the same
 *  tile and layer. Removing would delete the loc that was just placed. */
static void
app_map_editor_ghost_forget(struct App* app)
{
    assert(app);
    app->ghost_active = 0;
    app->ghost_alpha_done = 0;
    /* A commit chose to overwrite the displaced occupant; forgetting it too
     * is what makes that choice stick instead of resurrecting the old loc
     * over the one just placed. */
    app->ghost_displaced_valid = 0;
}

/** Remove the ghost from the scene, put back whatever it displaced, forget. */
static void
app_map_editor_ghost_remove(struct App* app)
{
    assert(app);
    if( !app->ghost_active )
        return;
    App_WorldLocChange(
        app, app->ghost_x, app->ghost_z, app->ghost_level, -1, app->ghost_shape, app->ghost_angle);
    /* The slot the ghost sat in belonged to someone: restore them, or the
     * hover reads as a deletion. Scene-only, like the ghost itself -- the
     * document never knew about either. */
    if( app->ghost_displaced_valid )
        App_WorldLocChange(
            app,
            app->ghost_x,
            app->ghost_z,
            app->ghost_level,
            app->ghost_displaced_loc_id,
            app->ghost_displaced_shape,
            app->ghost_displaced_angle);
    app_map_editor_ghost_forget(app);
    app->need_redraw = 1;
}

/**
 * Keep the Place-loc hover ghost current. Once per frame, with the other
 * editor drains.
 */
static void
app_map_editor_ghost_update(struct App* app)
{
    int want;
    int id = -1;
    int shape = 0;
    int angle = 0;
    int level;

    assert(app);

    if( !app->editor )
        return;

    /* Two tools ghost: Place previews the CATALOG pick, Move previews the
     * SELECTED loc at the tile it would land on. Move skips the selection's
     * own tile -- ghosting a loc onto itself replaces it with its own
     * translucent double, which reads as flicker, not preview. */
    {
        struct Editor_Panel const* panel = &app->editor_panel;
        int const hover_ok = app->world_hover_tile_x >= 0 && !app->interact.minimenu.visible &&
                             !app->input_frame_consumed;

        want = 0;
        if( panel->visible && hover_ok && panel->tool == EDITOR_TOOL_LOC_PLACE &&
            panel->cat_picked_id >= 0 && panel->cat_kind == CACHEPROVIDER_CATALOG_LOC )
        {
            want = 1;
            id = panel->cat_picked_id;
            Editor_PanelGhostSpec(&app->editor_panel, app, &shape, &angle);
            level = Editor_PanelEditLevel(panel, app);
        }
        else if(
            panel->visible && hover_ok && panel->tool == EDITOR_TOOL_LOC_MOVE &&
            panel->sel_kind == EDITOR_SELECTION_LOC &&
            !(app->world_hover_tile_x == panel->sel_scene_x &&
              app->world_hover_tile_z == panel->sel_scene_z) )
        {
            want = 1;
            id = panel->sel_loc_id;
            shape = panel->sel_shape;
            angle = panel->sel_angle;
            /* A move keeps its plane; the Level row is for edits, not this. */
            level = panel->sel_level;
        }
        else
            level = Editor_PanelEditLevel(panel, app);
    }

    /* The ghost follows the hover; any change of tile, loc or pose is a
     * remove + add. Same tile and spec: nothing to do but the alpha pass. */
    if( app->ghost_active &&
        (!want || app->ghost_x != app->world_hover_tile_x ||
         app->ghost_z != app->world_hover_tile_z || app->ghost_level != level ||
         app->ghost_loc_id != id || app->ghost_shape != shape || app->ghost_angle != angle) )
        app_map_editor_ghost_remove(app);

    if( want && !app->ghost_active )
    {
        /* Whoever holds this tile's slot in the ghost's layer is about to be
         * replaced by the add below; remember them for the restore. Read
         * BEFORE the add is queued -- the capture must see the pre-ghost
         * scene. */
        app->ghost_displaced_valid = 0;
        if( app->world )
        {
            int const occ = World_SceneryFindAt(
                app->world, app->world_hover_tile_x, app->world_hover_tile_z, level, shape);
            if( occ >= 0 )
            {
                struct WorldEntity_Scenery const* occupant =
                    World_EntityPoolGet(&app->world->entities.scenery, occ);
                if( occupant )
                {
                    app->ghost_displaced_valid = 1;
                    app->ghost_displaced_loc_id = occupant->loc_id;
                    app->ghost_displaced_shape = occupant->shape;
                    app->ghost_displaced_angle = occupant->angle;
                }
            }
        }

        App_WorldLocChange(
            app, app->world_hover_tile_x, app->world_hover_tile_z, level, id, shape, angle);
        app->ghost_active = 1;
        app->ghost_x = app->world_hover_tile_x;
        app->ghost_z = app->world_hover_tile_z;
        app->ghost_level = level;
        app->ghost_loc_id = id;
        app->ghost_shape = shape;
        app->ghost_angle = angle;
        app->ghost_alpha_done = 0;
        app->need_redraw = 1;
    }

    /* Translucency, once the async add has produced an element. The fade is
     * written onto the ELEMENT's own model rather than the loc's, so only the
     * placement under the cursor goes translucent. */
    if( app->ghost_active && !app->ghost_alpha_done && app->world && app->scene )
    {
        int const idx = World_SceneryFindAt(
            app->world, app->ghost_x, app->ghost_z, app->ghost_level, app->ghost_shape);
        if( idx >= 0 )
        {
            struct WorldEntity_Scenery* scenery =
                World_EntityPoolGet(&app->world->entities.scenery, idx);
            /* ForWrite, not Get: placements of one loc share a single model
             * (a ToriDraw_SharedModel), and fading it in place would
             * ghost every other one of the same fence on screen. It also
             * answers the tagged-union question -- only a full model carries
             * faces to fade, and a sprite billboard comes back NULL. */
            struct ToriDraw_Model* model =
                scenery ? ToriDraw_SceneElementModelForWrite(app->scene, scenery->element_id)
                        : NULL;

            if( model && model->face_count > 0 )
            {
                /* RS face alpha: 0 opaque, higher more transparent. */
                if( !model->face_alphas )
                {
                    model->face_alphas = malloc((size_t)model->face_count);
                    assert(model->face_alphas);
                }
                memset(model->face_alphas, 150, (size_t)model->face_count);
                app->ghost_alpha_done = 1;
                app->need_redraw = 1;
            }
        }
    }
}

/**
 * Render the catalog's picked entry into its model-view well.
 *
 * Objs ride the inventory-icon pipeline unchanged. Locs have no equivalent --
 * loc models are composed per shape by the world builder, privately -- so this
 * picks the models for the DEFAULT shape (the catalog previews "what is this",
 * not a placement) and rasterises the first through the same
 * ModelFromToriRS -> light -> raster route the icons take. Models not resident
 * yet are queued and retried: the updater latches its key only once a render
 * lands, so a miss this frame is a retry next frame, not a permanent blank.
 */
/**
 * Raster a preview model with the preview camera, fitting the zoom on demand.
 *
 * The fit reads the model's bounds cylinder and scales the raster distance so
 * the larger dimension fills most of the well -- a candle and a castle gate
 * both arrive framed, instead of one vanishing and the other cropping to a
 * wall of pixels. The constant is calibrated against the obj-icon pipeline
 * (zoom 2000 frames a typical item in ~30px) and clamped so a degenerate
 * bounds cannot zoom to infinity.
 */
static struct ToriDraw_Sprite*
app_preview_raster(
    struct App* app,
    struct ToriDraw_ModelHandle hnd)
{
    if( app->preview_fit_pending )
    {
        struct ToriDraw_BoundsCylinder* bounds = ToriDraw_ModelGetBoundsCylinder(hnd);
        int size = 128;
        if( bounds )
        {
            int const height = bounds->max_y - bounds->min_y;
            size = 2 * bounds->radius > height ? 2 * bounds->radius : height;
        }
        app->preview_zoom = (size * 9) / 2;
        if( app->preview_zoom < 500 )
            app->preview_zoom = 500;
        if( app->preview_zoom > 12000 )
            app->preview_zoom = 12000;
        app->preview_fit_pending = 0;
    }
    return ToriDraw_SpriteNewFromModelRaster(
        app->scene, hnd, app->preview_zoom, app->preview_xan, app->preview_yan, 120, 96, false);
}

static void
app_map_editor_preview_update(struct App* app)
{
    static int last_kind = -1;
    static int last_id = -1;
    struct Editor_Panel* panel = &app->editor_panel;
    struct ToriDraw_Sprite* sprite = NULL;
    /* Not the pick: while a multiloc VARIANT row is chosen this is that
     * rung's loc, so the well shows the variant the catalog is reading out. */
    int preview_id;

    assert(app);

    if( !app->editor || !panel->visible || panel->cat_view < 0 )
        return;
    preview_id = Editor_PanelCatalogPreviewId(panel);
    if( preview_id < 0 )
    {
        ToriRSChrome_ModelViewSet(&app->dbg_ui, panel->cat_view, 0);
        last_kind = -1;
        last_id = -1;
        return;
    }
    if( app->preview_dirty )
    {
        /* A key moved the camera: re-render the same pick. */
        app->preview_dirty = 0;
        last_kind = -1;
        last_id = -1;
    }
    if( panel->cat_kind == last_kind && preview_id == last_id )
        return;
    if( panel->cat_kind != last_kind || preview_id != last_id )
    {
        /* A NEW pick gets the default framing; a camera nudge does not. */
        if( !app->preview_keep_camera )
        {
            app->preview_xan = 160;
            app->preview_yan = 300;
            app->preview_fit_pending = 1;
        }
        app->preview_keep_camera = 0;
    }

    if( panel->cat_kind == CACHEPROVIDER_CATALOG_OBJ )
    {
        /*
         * The obj's own model, rastered with the preview camera -- NOT the
         * inventory icon.
         *
         * The icon was the obvious thing to reach for (it is already cached,
         * one call) and it is the one thing in this well that cannot be
         * turned: an icon is baked at the objtype's authored xan2d/yan2d/zoom2d
         * and handed back from an id-keyed cache, so every camera key was a
         * no-op for the whole obj kind while loc and npc orbited fine. Building
         * the model here costs a raster per nudge and answers the keys.
         *
         * The resize/recolour order is ObjModelLoader's (resize first, 128 ==
         * 1.0), so the preview is the item the game builds.
         */
        struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, preview_id);
        struct ToriDraw_Model* model;
        struct ToriDraw_ModelHandle hnd;

        if( !obj || (obj->inventory_model_id > 0 &&
                     !CacheProvider_ModelHas(app->provider, obj->inventory_model_id)) )
        {
            /* Objtype or model still to come. Task_ObjModelLoad fetches the
             * objtype, its count variant, the inventory model and that model's
             * textures together, so ask it once rather than per piece. */
            if( ObjModelLoad_NeedsWork(app->provider, preview_id, 1) )
            {
                int const ids[1] = { preview_id };
                int const counts[1] = { 1 };
                struct ToriRS_Task* task = CreateTask_ObjModelLoad(app->provider, ids, counts, 1);
                if( task )
                    ToriRS_TaskQueue_Add(app->runner.queue, task);
            }
            return; /* retry next frame once it lands */
        }
        if( obj->inventory_model_id <= 0 )
        {
            /* A real answer, not a pending one: a bank note or placeholder
             * has no model of its own. Cached, so this does not re-ask. */
            ToriRSChrome_ModelViewSet(&app->dbg_ui, panel->cat_view, 0);
            last_kind = panel->cat_kind;
            last_id = preview_id;
            return;
        }

        {
            struct ToriRS_Model* rs_model =
                CacheProvider_ModelGet(app->provider, obj->inventory_model_id);

            assert(rs_model);
            model = ToriDraw_ModelFromToriRS(rs_model);
            assert(model);
            if( obj->resize_x != 128 || obj->resize_y != 128 || obj->resize_z != 128 )
                ToriDraw_ModelScale(model, obj->resize_x, obj->resize_z, obj->resize_y);
            for( int i = 0; i < obj->recolor_count; i++ )
                ToriDraw_ModelRecolor(model, obj->recolors_from[i], obj->recolors_to[i]);
            ToriDraw_ModelSetBoundsCylinder(model);
            ToriDraw_ModelDropNonSdTextures(app->provider, model);
            memset(&hnd, 0, sizeof(hnd));
            hnd.kind = TORIDRAWMK_MODEL;
            hnd.u.model.model = model;
            ToriDraw_LightModelScene(hnd, obj->contrast, obj->ambient);
            sprite = app_preview_raster(app, hnd);
            ToriDraw_ModelFree(model);
        }

        if( sprite )
        {
            struct ToriDraw_Sprite** sprites = malloc(sizeof(*sprites));
            assert(sprites);
            sprites[0] = sprite;
            if( ToriDraw_SceneSpriteHas(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID) )
                ToriDraw_SceneSpriteRemove(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID);
            ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID, sprites, 1);
            ToriRSChrome_ModelViewSet(
                &app->dbg_ui, panel->cat_view, UITREE_SCENE_EDITOR_PREVIEW_ID);
            last_kind = panel->cat_kind;
            last_id = preview_id;
        }
        return;
    }

    if( panel->cat_kind == CACHEPROVIDER_CATALOG_LOC )
    {
        struct ToriRS_Location* cfg = CacheProvider_LocationGet(app->provider, preview_id);
        int model_id = -1;

        if( !cfg )
            return;
        /* The default-shape model set: no shapes array means one set; with
         * one, prefer the centrepiece (10) row, else the first row. */
        if( cfg->shapes_and_model_count > 0 && cfg->models && cfg->lengths )
        {
            int row = 0;
            if( cfg->shapes )
                for( int i = 0; i < cfg->shapes_and_model_count; i++ )
                    if( cfg->shapes[i] == RSCACHE_LOC_SHAPE_SCENERY )
                    {
                        row = i;
                        break;
                    }
            if( cfg->lengths[row] > 0 )
                model_id = cfg->models[row][0];
        }
        if( model_id <= 0 )
        {
            ToriRSChrome_ModelViewSet(&app->dbg_ui, panel->cat_view, 0);
            last_kind = panel->cat_kind;
            last_id = preview_id;
            return;
        }

        if( !CacheProvider_ModelHas(app->provider, model_id) )
        {
            struct ToriRS_Task* task = CreateTask_ModelLoad(app->provider, model_id);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
            return; /* retry next frame once it lands */
        }

        {
            struct ToriRS_Model* rs_model = CacheProvider_ModelGet(app->provider, model_id);
            struct ToriDraw_Model* model;
            struct ToriDraw_ModelHandle hnd;

            assert(rs_model);
            model = ToriDraw_ModelFromToriRS(rs_model);
            assert(model);
            for( int i = 0; i < cfg->recolor_count; i++ )
                ToriDraw_ModelRecolor(model, cfg->recolors_from[i], cfg->recolors_to[i]);
            ToriDraw_ModelSetBoundsCylinder(model);
            ToriDraw_ModelDropNonSdTextures(app->provider, model);
            memset(&hnd, 0, sizeof(hnd));
            hnd.kind = TORIDRAWMK_MODEL;
            hnd.u.model.model = model;
            ToriDraw_LightModelScene(hnd, cfg->contrast, cfg->ambient);
            sprite = app_preview_raster(app, hnd);
            ToriDraw_ModelFree(model);
        }

        if( sprite )
        {
            struct ToriDraw_Sprite** sprites = malloc(sizeof(*sprites));
            assert(sprites);
            sprites[0] = sprite;
            /* One well, re-rendered per pick: the old sprite goes with the
             * old registration. The scene owns what it holds. */
            if( ToriDraw_SceneSpriteHas(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID) )
                ToriDraw_SceneSpriteRemove(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID);
            ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID, sprites, 1);
            ToriRSChrome_ModelViewSet(
                &app->dbg_ui, panel->cat_view, UITREE_SCENE_EDITOR_PREVIEW_ID);
            last_kind = panel->cat_kind;
            last_id = preview_id;
        }
        return;
    }

    if( panel->cat_kind == CACHEPROVIDER_CATALOG_NPC )
    {
        struct ToriRS_Npctype* npc = CacheProvider_NpctypeGet(app->provider, preview_id);
        int missing = 0;

        if( !npc || npc->models_count <= 0 )
        {
            ToriRSChrome_ModelViewSet(&app->dbg_ui, panel->cat_view, 0);
            last_kind = panel->cat_kind;
            last_id = preview_id;
            return;
        }
        for( int i = 0; i < npc->models_count; i++ )
            if( npc->models[i] > 0 && !CacheProvider_ModelHas(app->provider, npc->models[i]) )
            {
                struct ToriRS_Task* task = CreateTask_ModelLoad(app->provider, npc->models[i]);
                if( task )
                    ToriRS_TaskQueue_Add(app->runner.queue, task);
                missing = 1;
            }
        if( missing )
            return; /* retry once the parts land */

        {
            /* An npc body is its PARTS MERGED -- QBD is two models, and a
             * first-part-only render shows a torso and reads as corruption.
             * Merge exactly as the entity path does, then raster the merge. */
            struct ToriDraw_Model* parts[16];
            struct ToriDraw_Model* merged = NULL;
            struct ToriDraw_ModelHandle hnd;
            int part_count = 0;

            for( int i = 0; i < npc->models_count && part_count < 16; i++ )
            {
                struct ToriRS_Model* rs =
                    npc->models[i] > 0 ? CacheProvider_ModelGet(app->provider, npc->models[i])
                                       : NULL;
                if( !rs )
                    continue;
                parts[part_count] = ToriDraw_ModelFromToriRS(rs);
                assert(parts[part_count]);
                part_count++;
            }
            if( part_count == 0 )
            {
                ToriRSChrome_ModelViewSet(&app->dbg_ui, panel->cat_view, 0);
                last_kind = panel->cat_kind;
                last_id = preview_id;
                return;
            }
            merged = part_count == 1 ? parts[0] : ToriDraw_ModelNewMerge(parts, part_count);
            assert(merged);
            for( int i = 0; i < npc->recolor_count; i++ )
                ToriDraw_ModelRecolor(merged, npc->recolors_from[i], npc->recolors_to[i]);
            ToriDraw_ModelSetBoundsCylinder(merged);
            ToriDraw_ModelDropNonSdTextures(app->provider, merged);
            memset(&hnd, 0, sizeof(hnd));
            hnd.kind = TORIDRAWMK_MODEL;
            hnd.u.model.model = merged;
            ToriDraw_LightModelScene(hnd, npc->contrast, npc->ambient);
            sprite = app_preview_raster(app, hnd);
            if( part_count > 1 )
                for( int i = 0; i < part_count; i++ )
                    ToriDraw_ModelFree(parts[i]);
            ToriDraw_ModelFree(merged);
        }

        if( sprite )
        {
            struct ToriDraw_Sprite** sprites = malloc(sizeof(*sprites));
            assert(sprites);
            sprites[0] = sprite;
            if( ToriDraw_SceneSpriteHas(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID) )
                ToriDraw_SceneSpriteRemove(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID);
            ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID, sprites, 1);
            ToriRSChrome_ModelViewSet(
                &app->dbg_ui, panel->cat_view, UITREE_SCENE_EDITOR_PREVIEW_ID);
            last_kind = panel->cat_kind;
            last_id = preview_id;
        }
        return;
    }

    ToriRSChrome_ModelViewSet(&app->dbg_ui, panel->cat_view, 0);
    last_kind = panel->cat_kind;
    last_id = preview_id;
}

static void
app_world_spawn_npc(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args);
static void
app_world_spawn_obj(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args);

void
App_EditorPlaceSpawn(
    struct App* app,
    int is_obj,
    int id,
    int scene_x,
    int scene_z,
    int level)
{
    char args[32];

    assert(app);

    snprintf(args, sizeof(args), "id=%d", id);
    if( is_obj )
        app_world_spawn_obj(app, scene_x, scene_z, level, args);
    else
        app_world_spawn_npc(app, scene_x, scene_z, level, args);
}

/** Start a load the square browser asked for. Runs on the frame boundary with
 *  the other editor drains, so a panel click never loads a world mid-tick. */
static void
app_map_editor_open_pending_square(struct App* app)
{
    int chunks[2];

    assert(app);

    if( !app->editor || !app->editor_panel.sq_open_pending )
        return;
    app->editor_panel.sq_open_pending = 0;
    if( app->world_load_inflight )
        return;

    chunks[0] = app->editor_panel.sq_open_x;
    chunks[1] = app->editor_panel.sq_open_z;
    TORIRS_LOG("editor: opening m%d_%d\n", chunks[0], chunks[1]);
    app_world_load_begin(app, chunks, 1);
}

/** Shared-state facts from this connection's Client land on the panel — the
 *  receiving half of the selection relay. Registered at editor construction;
 *  fires from Editor_PumpFacts inside the per-frame drain below, and for the
 *  common single-connection boot that includes this panel's own echoes,
 *  which apply idempotently. */
static void
app_editor_on_state(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct App* app = user_data;

    assert(app);
    Editor_PanelApplySharedState(&app->editor_panel, app, key, values, count);
}

static void
app_map_editor_drain(struct App* app)
{
    int squares[EDITOR_REBUILD_QUEUE_MAX * 2];
    int count;

    assert(app);

    if( !app->editor || app->editor->rebuild_count == 0 )
        return;
    if( app->world_load_inflight )
        return; /* A load is already rewriting the scene; let it land first. */

    count = Editor_DrainRebuilds(app->editor, app->provider, squares, EDITOR_REBUILD_QUEUE_MAX);
    if( count <= 0 )
        return;

    /* The chunklist rebuild path -- the same one an offline world load uses,
     * given only the squares whose meshes the edit invalidated. */
    app->world_load_attempted = 0;
    app_world_load_begin(app, squares, count);
    app->need_redraw = 1;
}

/* Post-load wiring, split from the old synchronous app_world_load: height
 * fn, texture requests, minimap bake, and the server ack when the load was
 * REBUILD_NORMAL-driven. Camera placement is only for offline/hotkey loads —
 * a server-driven rebuild shifts the existing camera (deob field3239 -= dx<<7)
 * instead of resetting to scene-centre top-down. */
void
App_WorldLoadFinish(struct App* app)
{
    app->world_load_inflight = 0;

    if( app->world->load_complete )
    {
        int server_driven = app->world_load_server_driven;

        app->world_active = 1;
        /* The absolute tile origin just moved, which is the one thing a plugin
         * holding saved tiles has to hear about: every scene-local number it
         * might have cached is renumbered by a rebuild. Raised after
         * world_active so a handler that queries the world finds it live. */
        PluginHost_WorldLoaded(app->plugins, app->world->_base_tile_x, app->world->_base_tile_z);
        /* AFTER the event, so an object a handler placed in response to the
         * rebuild is materialised by its own set_position rather than being
         * swept up by a pass that already ran. */
        app_plugin_objects_rebuild(app);
        /* Every loc in the new scene gets its LOC_ADD trigger. After the
         * plugin seam because a trigger script can create overlays, and the
         * object rebuild sweeps anything placed before it. */
        app_client_triggers_world_loaded(app);
        World_SetHeightFn(app->world, app_world_height, app);
        {
            struct World_SeqSource seq_source;
            /* Bound here rather than once at boot: the scene and the provider
             * are what the source reads, and this is the point at which the
             * world being handed the source has them. */
            WorldSeqSourceToriDraw_Bind(&app->seq_source, app->scene, app->provider);
            WorldSeqSourceToriDraw_Fill(&app->seq_source, &seq_source);
            World_SetSeqSource(app->world, &seq_source);
        }
        {
            struct World_AnimSoundSink anim_sound_sink = {
                .userdata = app,
                .frame = app_world_anim_frame_sound,
            };
            World_SetAnimSoundSink(app->world, &anim_sound_sink);
        }
        if( !server_driven )
        {
            int restored = 0;

            /* A held camera wins over the first-look placement, IF the new
             * scene contains it. Outside the scene (the square browser opened
             * somewhere distant) the hold is meaningless and the first-look
             * centre below is correct. */
            if( app->cam_keep_valid )
            {
                int const sx = app->cam_keep_abs_x - app->world->_base_tile_x * 128;
                int const sz = app->cam_keep_abs_z - app->world->_base_tile_z * 128;
                int const max = app->world->_scene_size * 128;

                app->cam_keep_valid = 0;
                if( sx >= 0 && sx < max && sz >= 0 && sz < max )
                {
                    app->world_camera_pos.x = sx;
                    app->world_camera_pos.z = sz;
                    app->world_camera_pos.y = app->cam_keep_y;
                    app->world_camera.pitch = app->cam_keep_pitch;
                    app->world_camera.yaw = app->cam_keep_yaw;
                    restored = 1;
                }
            }

            if( !restored )
            {
                /* Offline/hotkey load: place the camera at scene centre. */
                app->world_camera_pos.x = app->world->_scene_size / 2 * 128 + 64;
                app->world_camera_pos.z = app->world->_scene_size / 2 * 128 + 64;
                app->world_camera_pos.y = -2000;
                app->world_camera.pitch = 450;
                app->world_camera.yaw = 0;
            }
            {
                char const* cam = getenv("TORIRS_WORLD_CAM");
                int cx, cy, cz, cpitch, cyaw;
                if( cam && sscanf(cam, "%d,%d,%d,%d,%d", &cx, &cy, &cz, &cpitch, &cyaw) == 5 )
                {
                    app->world_camera_pos.x = cx;
                    app->world_camera_pos.y = cy;
                    app->world_camera_pos.z = cz;
                    app->world_camera.pitch = cpitch;
                    app->world_camera.yaw = cyaw;
                }
            }
        }
        /* World scenery models reference textures; the bridge scan walks the
         * scene elements the rebuild just created. */
        app_sync_textures(app);
        /* Decouple the BFS window from the resident scene size: rsmod floods a
         * fixed 128x128 box around the mover. LostCity leaves this at 0
         * (whole map). */
        if( app->features )
        {
            for( int i = 0; i < COLLISION_LEVELS; i++ )
            {
                if( app->world->collision_maps[i] )
                    collision_map_set_route_window(
                        app->world->collision_maps[i], app->features->route_window_tiles);
            }
        }
        {
            struct WorldEntity_Player* local = app_local_player(app);
            app_rebuild_world_map(app, app_minimap_level(app, local));
        }

        if( server_driven )
        {
            app->world_load_server_driven = 0;
            App_SendMapBuildComplete(app);
        }
    }
    else
    {
        app->world_load_server_driven = 0;
        TORIRS_LOG("app: world load incomplete\n");
    }
    app->need_redraw = 1;
}

/* Cache the WORLD node's emit desc: the mouse gate rect and the viewport the
 * frame emitter draws with (pick/render parity comes from sharing it). Also the
 * "is a world on screen this frame" flag every world subsystem gates on. Uses
 * the previous frame's emit buffer — the world box only changes on relayout. */
/* TORIRS_POS_DEBUG=1: the local player's authoritative tile in every frame of
 * reference at once — scene tile, absolute world tile (compare against the
 * server's ::getcoord), fine draw position, and the camera the painter used.
 * The one-liner for "is the player where the server thinks it is". */
static void
app_debug_log_position(struct App* app)
{
    struct WorldEntity_Player* local;

    if( !torirs_env_pos_debug() || !app->world )
        return;
    local = app_local_player(app);
    if( !local )
        return;
    fprintf(
        stderr,
        "pos: scene=%d,%d route0=%d,%d abs=%d,%d level=%d draw=%u,%u y=%d flags=%02x/%02x "
        "cam=%d,%d,%d yaw=%d pitch=%d\n",
        local->grid_position.x,
        local->grid_position.z,
        local->pathing.route_x[0],
        local->pathing.route_z[0],
        app->world->_base_tile_x + local->pathing.route_x[0],
        app->world->_base_tile_z + local->pathing.route_z[0],
        local->grid_position.level,
        local->draw_position.x,
        local->draw_position.z,
        /* Ground y under the player + the land settings of its column at the
         * player's level / level 1 — the bridge bump (LinkBelow 0x02 at
         * level 1) is only visible here. */
        app_world_height(
            app,
            (int)local->draw_position.x,
            (int)local->draw_position.z,
            local->grid_position.level),
        (unsigned)World_TileFlagGet(
            app->world, local->grid_position.x, local->grid_position.z, local->grid_position.level),
        (unsigned)World_TileFlagGet(app->world, local->grid_position.x, local->grid_position.z, 1),
        app->world_camera_pos.x,
        app->world_camera_pos.y,
        app->world_camera_pos.z,
        app->world_camera.yaw,
        app->world_camera.pitch);
}

/* TORIRS_HPROF=x0,x1,z0,z1: ground height across a rectangle of scene tiles,
 * once per load.
 *
 * Flat terrain and correctly-varying terrain are indistinguishable from a
 * screenshot, and "the heights must be wrong" is the first thing anyone reaches
 * for when scenery and ground intersect oddly. This settles it in one run: the
 * Inferno arena reads a single height across 41x31 tiles and is *supposed* to —
 * its depth is scenery, not relief — while Lumbridge over the same span ramps
 * -464 to -240. Without the second half of that comparison the first half reads
 * as a bug. */
static void
app_debug_height_profile(struct App* app)
{
    static unsigned logged = (unsigned)-1;
    const char* env = torirs_env_hprof();
    int x0, x1, z0, z1;
    if( !env || !app->world || !app->world->load_complete )
        return;
    if( app->world->load_seq == logged )
        return;
    logged = app->world->load_seq;
    int lvl = 0;
    if( sscanf(env, "%d,%d,%d,%d,%d", &x0, &x1, &z0, &z1, &lvl) < 4 )
        return;
    for( int z = z1; z >= z0; z-- )
    {
        TORIRS_LOG("hprof L%d z=%3d:", lvl, z);
        for( int x = x0; x <= x1; x++ )
            TORIRS_LOG(" %5d", app_world_height(app, x * 128 + 64, z * 128 + 64, lvl));
        TORIRS_LOG("\n");
    }
}

/* TORIRS_TFLAGS=x0,x1,z0,z1: per-tile terrain settings at every cache level,
 * once per load. BLOCK 0x1, LINK_BELOW 0x2, REMOVE_ROOF 0x4, VIS_BELOW 0x8,
 * FORCE_HIGH_DETAIL 0x10. VIS_BELOW is the one that drags a tile from an upper
 * level down onto level 0's draw pass. */
static void
app_debug_tile_flags(struct App* app)
{
    static unsigned logged = (unsigned)-1;
    const char* env = torirs_env_tflags();
    int x0, x1, z0, z1;
    if( !env || !app->world || !app->world->load_complete )
        return;
    if( app->world->load_seq == logged )
        return;
    logged = app->world->load_seq;
    if( sscanf(env, "%d,%d,%d,%d", &x0, &x1, &z0, &z1) != 4 )
        return;
    for( int lv = 0; lv < WORLD_MAP_TERRAIN_LEVELS; lv++ )
    {
        int n_vis = 0, n_link = 0;
        for( int z = z0; z <= z1; z++ )
            for( int x = x0; x <= x1; x++ )
            {
                unsigned f = (unsigned)World_TileFlagGet(app->world, x, z, lv);
                if( f & RSCACHE_FLOFLAG_VIS_BELOW )
                {
                    n_vis++;
                    if( n_vis <= 400 )
                        TORIRS_LOG(
                            "tflags L%d tile=%d,%d VIS_BELOW (0x%02x) terrain_element=%d\n",
                            lv,
                            x,
                            z,
                            f,
                            World_TerrainElementAt(app->world, x, z, lv));
                }
                if( f & RSCACHE_FLOFLAG_LINK_BELOW )
                    n_link++;
            }
        TORIRS_LOG("tflags L%d: vis_below=%d link_below=%d\n", lv, n_vis, n_link);
    }
}

/* TORIRS_TPROJ=x0,x1,z0,z1: project each tile centre to screen, once per load.
 * Turns "which tile is that artifact" from a guess into a lookup. */
static void
app_debug_tile_project(struct App* app)
{
    static int ticks = 0;
    const char* env = torirs_env_tproj();
    int x0, x1, z0, z1;
    if( !env || !app->world || !app->world->load_complete || !app->world_view_valid )
        return;
    /* After the camera has settled, not on the load callback: the projection
     * reads app->world_camera, which the load has not written yet. */
    if( ++ticks != 120 )
        return;
    if( sscanf(env, "%d,%d,%d,%d", &x0, &x1, &z0, &z1) != 4 )
        return;
    for( int z = z0; z <= z1; z++ )
        for( int x = x0; x <= x1; x++ )
        {
            int sx = 0, sy = 0;
            if( app_world_project(app, x * 128 + 64, z * 128 + 64, 0, &sx, &sy) )
                TORIRS_LOG("tproj tile=%d,%d screen=%d,%d\n", x, z, sx, sy);
        }
}

/* TORIRS_BRIDGE_DEBUG=1: list every LinkBelow column in the loaded scene with
 * the level-0/level-1 ground heights the getAvH bump chooses between. The
 * "am I standing on the deck or under it" one-liner. Prints once per load. */
static void
app_debug_log_bridges(struct App* app)
{
    static unsigned logged_seq = 0;
    int count = 0;

    if( !torirs_env_bridge_debug() || !app->world || !app->world->load_complete )
        return;
    if( app->world->load_seq == logged_seq )
        return;
    logged_seq = app->world->load_seq;

    for( int x = 0; x < app->world->_scene_size; x++ )
    {
        for( int z = 0; z < app->world->_scene_size; z++ )
        {
            if( (World_TileFlagGet(app->world, x, z, 1) & RSCACHE_FLOFLAG_LINK_BELOW) == 0 )
                continue;
            count++;
            if( count > 40 )
                continue;
            TORIRS_LOG(
                "bridge: scene=%d,%d abs=%d,%d y0=%d y1=%d\n",
                x,
                z,
                app->world->_base_tile_x + x,
                app->world->_base_tile_z + z,
                heightmap_get_interpolated(app->world->heightmap, x * 128 + 64, z * 128 + 64, 0),
                heightmap_get_interpolated(app->world->heightmap, x * 128 + 64, z * 128 + 64, 1));
        }
    }
    TORIRS_LOG("bridge: %d link-below columns in scene\n", count);
}

/*
 * World projection scale — recomputed per layout, the reference behaviour
 * (docs/ORANGE_WEDGE.md §4/§11/§12, promoted to the default per §11.7).
 *
 * The reference client recomputes the world projection scale from the world
 * viewport HEIGHT on every layout (class159.method5357:
 * scale = viewportHeight * zoom / 334, zoom interpolated between the two
 * VIEWPORT_SETFOV endpoints over height-334 in [0,100]). Leaving it at the
 * compile-time projection_scale = 512 is what drew the Inferno 2.68x magnified —
 * at a 503-high world viewport the reference lands on 191/192.
 *
 * ON by default. TORIRS_WEDGE_SCALE values:
 *   (unset) | 1 | auto        recompute per class159.method5357 (default)
 *   0 | off                   legacy constant scale 512, for A/B comparison
 *   <n>                       force the linear scale to n (n >= 8), for bisection
 *   TORIRS_WEDGE_ZOOM=<n>,<f>   override the decoded SETFOV endpoints (auto mode)
 *   TORIRS_WEDGE_FOV_DEBUG=1    log the SETFOV decode and the resulting scale
 *
 * The scale reaches the kernels exactly, through ToriDraw_Camera.projection_scale.
 *
 * TORIRS_WORLD_FOV=<n> drives the camera's OTHER knob instead: it switches the
 * world camera to TORIDRAW_PROJECTION_MODE_FOV and sets fov_rpi2048 to n (units of
 * 2*pi/2048, 512 = the default). Both spellings are configurable; the angle
 * cannot express most integer scales, so it is the wrong tool for matching a
 * reference projection and the right one for a free camera. It wins over
 * TORIRS_WEDGE_SCALE when both are set, since it is the more explicit request.
 */
static int
app_world_fov_override(void)
{
    static int cached = -2;
    if( cached == -2 )
        cached = ToriRS_EnvFovOverride(
            getenv("TORIRS_WORLD_FOV"),
            TORIDRAW_PROJECTION_FOV_MIN,
            TORIDRAW_PROJECTION_FOV_MAX);
    return cached;
}

/* 0 = auto (recompute — the default), -1 = forced off (legacy constant 512),
 * >0 = forced linear scale. */
static int
app_wedge_scale_mode(void)
{
    static int cached = -2;
    if( cached == -2 )
        cached = ToriRS_EnvScaleMode(getenv("TORIRS_WEDGE_SCALE"));
    return cached;
}

static void
app_apply_wedge_scale(struct App* app)
{
    int near_zoom = app->host.viewport_zoom_near;
    int far_zoom = app->host.viewport_zoom_far;
    struct ToriRS_ViewportProjection projection;

    /* TORIRS_WEDGE_ZOOM=<near>,<far> overrides the cache's SETFOV endpoints,
     * which is how the band itself gets bisected. */
    {
        char const* zoom_spec = torirs_env_wedge_zoom();
        int spec_near;
        int spec_far;

        if( zoom_spec && sscanf(zoom_spec, "%d,%d", &spec_near, &spec_far) == 2 )
        {
            near_zoom = spec_near;
            far_zoom = spec_far;
        }
    }

    projection = ToriRS_ProjectionForViewport(
        app_wedge_scale_mode(),
        app_world_fov_override(),
        app->revconfig_profile.camera.viewport_zoom,
        app->world_view_valid,
        app->world_emit_desc.h,
        near_zoom,
        far_zoom);

    switch( projection.action )
    {
    case TORIRS_VIEWPORT_PROJECTION_KEEP:
        return;

    case TORIRS_VIEWPORT_PROJECTION_FOV:
        app->world_camera.projection_mode = TORIDRAW_PROJECTION_MODE_FOV;
        app->world_camera.fov_rpi2048 = projection.fov_rpi2048;
        if( torirs_env_wedge_fov_debug() )
        {
            static int logged = 0;
            if( !logged )
            {
                logged = 1;
                TORIRS_LOG(
                    "wedge: fov mode fov_rpi2048=%d -> realised scale %d\n",
                    projection.fov_rpi2048,
                    toridraw_projection_scale_from_cot16(
                        toridraw_projection_cot16_from_fov(projection.fov_rpi2048)));
            }
        }
        return;

    case TORIRS_VIEWPORT_PROJECTION_SCALE:
        /* Exact: the kernels multiply by projection_scale directly. */
        app->world_camera.projection_mode = TORIDRAW_PROJECTION_MODE_SCALE;
        app->world_camera.projection_scale = projection.scale;
        if( torirs_env_wedge_fov_debug() )
        {
            static int last = -1;
            if( projection.scale != last )
            {
                last = projection.scale;
                TORIRS_ERR(
                    "wedge: vp_h=%d zoom(near=%d far=%d)=%d -> scale=%d realised=%d\n",
                    app->world_emit_desc.h,
                    projection.near_zoom,
                    projection.far_zoom,
                    projection.zoom,
                    projection.scale,
                    toridraw_projection_scale_from_cot16(toridraw_projection_cot16(
                        app->world_camera.projection_mode,
                        app->world_camera.projection_scale,
                        app->world_camera.fov_rpi2048)));
            }
        }
        return;
    }
}

static void
app_update_world_viewport(struct App* app)
{
    app_debug_log_position(app);
    app_debug_log_bridges(app);
    app_debug_height_profile(app);
    app_debug_tile_project(app);
    app_debug_tile_flags(app);
    app->world_view_valid = 0;
    app->minimap_view_valid = 0;
    if( torirs_env_world_view_debug() )
    {
        int kinds[24] = { 0 };
        for( int i = 0; i < app->emit.count; i++ )
            if( app->emit.cmds[i].kind >= 0 && app->emit.cmds[i].kind < 24 )
                kinds[app->emit.cmds[i].kind]++;
        TORIRS_LOG(
            "worldview: node_index=%d emit_count=%d kinds:",
            App_WorldNodeIndex(app),
            app->emit.count);
        for( int k = 0; k < 24; k++ )
            if( kinds[k] )
                TORIRS_LOG(" %d:%d", k, kinds[k]);
        int wx = 0, wy = 0, ww = 0, wh = 0, widx = -1;
        for( int i = 0; i < app->emit.count; i++ )
            if( app->emit.cmds[i].kind == UITREE_EMIT_WORLD )
            {
                wx = app->emit.cmds[i].x;
                wy = app->emit.cmds[i].y;
                ww = app->emit.cmds[i].w;
                wh = app->emit.cmds[i].h;
                widx = i;
            }
        TORIRS_LOG(" WORLDRECT=%d,%d %dx%d idx=%d\n", wx, wy, ww, wh, widx);
        /* Anything drawn after the world that covers a meaningful slice of it. */
        for( int i = widx + 1; i < app->emit.count; i++ )
        {
            struct UITreeEmitDesc* d = &app->emit.cmds[i];
            int ox = d->x > wx ? d->x : wx, oy = d->y > wy ? d->y : wy;
            int ex = (d->x + d->w) < (wx + ww) ? (d->x + d->w) : (wx + ww);
            int ey = (d->y + d->h) < (wy + wh) ? (d->y + d->h) : (wy + wh);
            int area = (ex - ox) > 0 && (ey - oy) > 0 ? (ex - ox) * (ey - oy) : 0;
            if( area > (ww * wh) / 20 )
                TORIRS_LOG(
                    "  occluder idx=%d kind=%d comp=%d node=%d rect=%d,%d %dx%d trans=%d "
                    "overlap=%d%%\n",
                    i,
                    d->kind,
                    d->component_id,
                    d->node_index,
                    d->x,
                    d->y,
                    d->w,
                    d->h,
                    d->trans,
                    area * 100 / (ww * wh));
        }
    }
    for( int i = 0; i < app->emit.count; i++ )
    {
        if( app->emit.cmds[i].kind == UITREE_EMIT_WORLD )
        {
            app->world_emit_desc = app->emit.cmds[i];
            app->world_view_valid = 1;
        }
        else if( app->emit.cmds[i].kind == UITREE_EMIT_MINIMAP )
        {
            app->minimap_emit_desc = app->emit.cmds[i];
            app->minimap_view_valid = 1;
        }
    }
    /* Recomputes the projection scale from the world viewport height, the
     * reference behaviour (TORIRS_WEDGE_SCALE=off reverts to constant 512). */
    app_apply_wedge_scale(app);
}

int32_t
App_WorldNodeIndex(struct App const* app)
{
    assert(app);
    assert(app->tree);
    return app->tree->world_index;
}

/* A bake that must not replay the loading screen's staged captions or walk
 * the bar back: everything after the session's first. On a profile with a
 * title screen the loading ran once, at boot (App_BootGameframeThenTitle,
 * screen still APP_SCREEN_BOOT); the title bake that follows it, the
 * post-login gameframe rebake and any in-game display-mode remount all cross
 * caches that bake warmed, and replaying 10..100 with captions over them
 * would put a second loading sequence after the first -- or worse, after the
 * login screen. A quiet bake leaves the bar where the loud one parked it
 * (100) and App_Render's caption fallback says "loading" / "entering world".
 * The lanes with no title screen or no net keep announcing: their startup
 * bake is the only loading they have. */
static int
app_boot_bake_is_quiet(struct App const* app)
{
    assert(app);
    return app->net_enabled && App_HasTitleScreen(app) && app->screen != APP_SCREEN_BOOT;
}

int
App_BootTextOnly(struct App const* app)
{
    assert(app);
    /* GAME only: the startup title bake is also quiet, but it belongs to the
     * boot's loading screen and holds the bar at 100 instead -- text-only is
     * the POST-LOGIN picture. */
    return app->app_state == APP_STATE_BOOTING && app->screen == APP_SCREEN_GAME &&
           app_boot_bake_is_quiet(app);
}

#include "app_boot.u.c"

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
    count = app->pending_clientscript_count;
    if( count <= 0 )
        return;
    /* Cleared before dispatching: a script that pushes another (CC_TRIGGEROP's
     * queue drain reaches this path) must append to an empty list rather than
     * be run twice by the loop it is inside. */
    app->pending_clientscript_count = 0;
    for( int i = 0; i < count; i++ )
        app_dispatch_clientscript(app, &app->pending_clientscripts[i]);
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
     * `App_FlushPendingClientScripts` for where they go. */
    if( app->pending_clientscript_count < APP_PENDING_CLIENTSCRIPT_MAX )
    {
        if( !app->pending_clientscript_count )
            app->pending_clientscript_cycle = app->logic_cycle;
        app->pending_clientscripts[app->pending_clientscript_count++] = *request;
    }
    else
        app_dispatch_clientscript(app, request);
}

void
App_LootNotifyKill(
    struct App* app,
    char const* source_name,
    int obj_id,
    int qty)
{
    assert(app);
    assert(source_name);

    /*
     * Script 7166 only mounts a source into the Drops-mode info slots when
     * _7604(name) != 0. That opcode is the per-source kill count; seed one
     * kill via a fresh event_id. Dat2 objtypes default cost to 1; when the
     * type is not yet resident (common right after login for a lootkill
     * cheat) treat value the same way and queue the load so later OC_* ops
     * see the real record.
     */
    int cost = 1;
    struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
    if( obj )
    {
        cost = obj->cost;
    }
    else if( app->provider )
    {
        struct ToriRS_Task* load = CreateTask_ObjLoad(app->provider, obj_id);
        if( load )
            ToriRS_TaskQueue_Add(app->runner.queue, load);
    }

    int event_id = app->loot.next_event_id++;
    LootStore_AddKillLoot(&app->loot, source_name, obj_id, qty, cost, event_id);

    /*
     * Clientscript 7159: (int killDelta, int qty, string sourceName).
     * The engine fills the store FIRST, then pushes 7159 so CS2 can read it
     * back. 7159 → 7162 adds killDelta onto the source's scroll height
     * ("Name x N"); pass 1 per kill, never the obj id.
     *
     * Argument layout: intv[0] = killDelta, intv[1] = qty; str_mask bit 2
     * marks argument 2 as a string; str_args[0] = sourceName (compacted).
     */
    {
        int intv[3] = { 1, qty, 0 };
        char const* str_args[1] = { source_name };
        uint64_t str_mask = 1u << 2;

        RS_CS2_RunScript(&app->host, &app->runner, 7159, intv, 3, str_mask, str_args, 1);
    }
}

void
App_SimulateLocOp(
    struct App* app,
    int op_num,
    int abs_x,
    int abs_z,
    int loc_id)
{
    assert(app);
    APP_NET_SEND(
        app,
        net_out_oploc(
            app->net->rev,
            app->net->random_out,
            _nsbuf,
            sizeof(_nsbuf),
            op_num,
            abs_x,
            abs_z,
            loc_id));
}

int
App_SimulateNpcOp(
    struct App* app,
    int op_num,
    int npc_id)
{
    assert(app);
    if( !app->world )
        return -1;
    /*
     * Addressed by npc TYPE rather than by server slot, which is the only id a
     * test can state up front: slots are handed out by the server as npcs enter
     * the build area and are not stable between runs. The first live entity of
     * that type wins, the same one a click would land on when there is only one
     * — a familiar, a spawned quest actor.
     */
    struct World_EntityPool* pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);

        if( !npc || npc->npc_id != npc_id || npc->server_slot < 0 )
            continue;
        APP_NET_SEND(
            app,
            net_out_opnpc(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                op_num,
                npc->server_slot));
        return npc->server_slot;
    }
    return -1;
}

int
App_NpcScreenPosition(
    struct App* app,
    int npc_id,
    int* out_x,
    int* out_y,
    int* out_type)
{
    /* Inside the world viewport with this much to spare: a body projected on
     * the viewport's edge is half under the frame, and the frame takes the
     * click. */
    enum
    {
        MARGIN = 12
    };
    struct UITreeEmitDesc const* viewport;

    assert(app);
    assert(out_x);
    assert(out_y);
    assert(out_type);
    if( !app->world || !app->world_view_valid )
        return -1;
    viewport = &app->world_emit_desc;
    struct World_EntityPool* pool = &app->world->entities.npc;
    /* Of the candidates on screen, the one nearest the viewport's centre: a
     * body at the corner is mostly clipped, and a hull drawn round it later
     * is a few pixels that prove nothing. */
    int best_slot = -1;
    long best_distance = 0;
    int const centre_x = viewport->x + viewport->w / 2;
    int const centre_y = viewport->y + viewport->h / 2;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        int x;
        int y;
        long distance;

        /* npc_id < 0 takes any npc that is on screen. */
        if( !npc || npc->server_slot < 0 || (npc_id >= 0 && npc->npc_id != npc_id) )
            continue;
        /* Mid-body rather than the feet: the feet of an npc standing behind a
         * table project onto the table, and a click there is the table's. */
        if( !app_world_project_actor(
                app,
                &npc->view_placement,
                npc->grid_position.level,
                (int)npc->draw_position.x,
                (int)npc->draw_position.z,
                60,
                &x,
                &y) )
            continue;
        if( x < viewport->x + MARGIN || x >= viewport->x + viewport->w - MARGIN ||
            y < viewport->y + MARGIN || y >= viewport->y + viewport->h - MARGIN )
            continue;
        distance = (long)(x - centre_x) * (x - centre_x) + (long)(y - centre_y) * (y - centre_y);
        if( best_slot >= 0 && distance >= best_distance )
            continue;
        best_slot = npc->server_slot;
        best_distance = distance;
        *out_x = x;
        *out_y = y;
        *out_type = npc->npc_id;
    }
    return best_slot;
}

bool
App_LocalPlayerTiles(
    struct App* app,
    int* true_x,
    int* true_z,
    int* level,
    int* dest_x,
    int* dest_z,
    int* flag_x,
    int* flag_z,
    int* draw_x,
    int* draw_z)
{
    struct WorldEntity_Player* player;
    int base_x;
    int base_z;

    assert(app);
    assert(true_x);
    assert(true_z);
    assert(level);
    assert(dest_x);
    assert(dest_z);
    assert(flag_x);
    assert(flag_z);
    assert(draw_x);
    assert(draw_z);
    player = app_local_player(app);
    if( !player || !app->world )
        return false;
    base_x = app->world->_base_tile_x;
    base_z = app->world->_base_tile_z;
    /* The interpolated model position in fine units (128 per tile), scene
     * relative: where the figure is DRAWN, against the whole tile above. */
    *draw_x = (int)player->draw_position.x;
    *draw_z = (int)player->draw_position.z;
    /* route[0] is the server's whole tile; the draw position slides between
     * tiles every frame. Same reading as the plugin bridge's player snapshot. */
    if( player->pathing.route_length > 0 )
    {
        *true_x = base_x + player->pathing.route_x[0];
        *true_z = base_z + player->pathing.route_z[0];
    }
    else
    {
        *true_x = base_x + player->grid_position.x;
        *true_z = base_z + player->grid_position.z;
    }
    *level = player->grid_position.level;
    if( app->minimap_flag_x >= 0 )
    {
        *flag_x = base_x + app->minimap_flag_x;
        *flag_z = base_z + app->minimap_flag_z;
        *dest_x = *flag_x;
        *dest_z = *flag_z;
    }
    else
    {
        *flag_x = -1;
        *flag_z = -1;
        *dest_x = *true_x;
        *dest_z = *true_z;
    }
    return true;
}

void
App_TraceWorldEntities(struct App* app)
{
    int base_x;
    int base_z;

    assert(app);
    if( !app->world )
        return;
    base_x = app->world->_base_tile_x;
    base_z = app->world->_base_tile_z;
    {
        struct World_EntityPool* pool = &app->world->entities.npc;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_NPC const* npc = World_EntityPoolGet(pool, i);
            if( !npc || npc->server_slot < 0 )
                continue;
            TORIRS_REPORT(
                "NATIVE_NPC slot=%d type=%d tile=%d,%d,%d\n",
                npc->server_slot,
                npc->npc_id,
                base_x + npc->grid_position.x,
                base_z + npc->grid_position.z,
                npc->grid_position.level);
        }
    }
    {
        struct World_EntityPool* pool = &app->world->entities.obj_stack;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_ObjStack const* stack = World_EntityPoolGet(pool, i);
            if( !stack )
                continue;
            TORIRS_REPORT(
                "NATIVE_GROUND_STACK tile=%d,%d,%d id=%d count=%d name=%s\n",
                base_x + stack->grid_position.x,
                base_z + stack->grid_position.z,
                stack->grid_position.level,
                stack->obj_id,
                stack->count,
                stack->name);
        }
    }
}

void
App_PluginObjectCounts(
    struct App* app,
    int* in_use,
    int* active,
    int* built)
{
    assert(app);
    assert(in_use);
    assert(active);
    assert(built);
    *in_use = 0;
    *active = 0;
    *built = 0;
    for( int i = 0; i < APP_PLUGIN_OBJECTS_MAX; i++ )
    {
        struct AppPluginObject const* object = &app->plugin_objects[i];

        if( !object->in_use )
            continue;
        (*in_use)++;
        if( object->active )
            (*active)++;
        if( object->element_id >= 0 )
            (*built)++;
    }
}

bool
App_MinimenuRowCenter(
    struct App* app,
    char const* prefix,
    int* out_x,
    int* out_y,
    char* out_text,
    size_t out_text_capacity)
{
    struct UIMinimenu const* menu;
    size_t prefix_len;

    assert(app);
    assert(prefix);
    assert(out_x);
    assert(out_y);
    menu = &app->interact.minimenu;
    if( !menu->visible )
        return false;
    prefix_len = strlen(prefix);
    for( int i = 0; i < menu->option_count; i++ )
    {
        if( strncmp(menu->options[i].text, prefix, prefix_len) != 0 )
            continue;
        *out_x = menu->x + menu->width / 2;
        *out_y = UIMinimenu_OptionY(menu, i);
        if( out_text && out_text_capacity > 0 )
            snprintf(out_text, out_text_capacity, "%s", menu->options[i].text);
        return true;
    }
    return false;
}

/* Shared per-frame completion polls for async work (world load, textures,
 * deferred seq binds, tree refresh). Not run while BOOTING. */
static void
app_async_polls(struct App* app)
{
    /* World-load completion is no longer polled here: Task_WorldLoad runs
     * App_WorldLoadFinish itself at its synchronous tail (via on_done, or the
     * REBUILD_NORMAL path's inline call after it awaits the load). */
    if( app_tex_trace_enabled() )
        TORIRS_LOG("tex_trace: --- frame %d ---\n", ++g_tex_trace_frame);
    app_world_map_poll(app);
    app_sync_textures_poll(app);
    app_world_bind_pending_seqs(app);

    if( app->pending_tree_refresh )
    {
        app->pending_tree_refresh = 0;
        UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        app_request_cs1_eval(app);
        app->need_redraw = 1;
    }
}

void
App_BootWait(struct App* app)
{
    /* Headless harnesses/tests only: step both pipelines until the boot task
     * AND every load it queued (world, anims, textures) settle. IO still runs
     * exclusively inside the platform pump; the interactive loop never calls
     * this — it renders the loading state instead. */
    long guard = 1000000;

    assert(app);
    while( guard-- > 0 )
    {
        enum TaskRunnerStat main_stat;
        enum TaskRunnerStat exec_stat;
        app->boot_steps += 2;
        main_stat = TaskRunner_Step(&app->runner);
        exec_stat = TaskRunner_Step(&app->exec_runner);
        app_title_swap_if_pending(app);
        if( app->app_state == APP_STATE_READY )
            app_async_polls(app);
        if( main_stat == TASK_RUNNER_IDLE && exec_stat == TASK_RUNNER_IDLE &&
            app->app_state == APP_STATE_READY && !app->pending_tree_refresh &&
            !app->world_load_inflight )
            break;
    }
    if( guard <= 0 )
        TORIRS_LOG("app: boot wait exceeded step guard\n");
}

/* Defined below with the other interface-model binders; driven from the tick
 * so the figure's oscillation keeps running even on a frame nothing else
 * dirtied — the same reason RS_ClientCode_Tick drives the design preview's. */
static void
app_player_model_poll(struct App* app);

/* Input-driven host effects are drained at the frame's CS2 fixed point.
 * They do not consume simulation time: a close or amount response must not
 * remain queued until a later boat command when the test clock is paused. */
/* Settings writes settle with the click, including while simulation is paused. */
static int
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

static int
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
     * Parked by the host and turned into a session teardown here, the same
     * split if_close above takes: the CS2 host knows nothing about the socket.
     * Handed to the tick's own drain rather than performed here so it lands
     * behind whatever this tick has already queued. @see App_Logout.
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
static int
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
static int
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
static enum TaskRunnerStat
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
    app->need_redraw = 1;
}

void
App_Logout(struct App* app)
{
    assert(app);

    app->logout_requested = 0;
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
    app->net_last_recv_ms = 0;
    app->net_first_recv_ms = 0;
    app->net_lost = 0;
    app->net_reconnect_attempts = 0;
    app->net_reconnect_failed = 0;

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
     * player asked to leave. @see App::autologin_done. */
    app->autologin_done = 1;
    app->title_connect_pending = 0;
    TORIRS_LOG("logout: session ended; back to the title screen\n");
    App_OpenTitleScreen(app);
}

/*
 * Declare the session dead and start trying to get it back.
 *
 * Idempotent: every detector below can fire in the same frame as another (a
 * stalled tab both misses packets and reports a huge frame gap), and the
 * first one to arrive owns the transition.
 */
static void
app_net_lost(
    struct App* app,
    char const* why)
{
    if( !app->net || app->net_lost )
        return;

    TORIRS_LOG("net: connection lost (%s) — attempting to reestablish\n", why);
    app->net_lost = 1;
    app->net_reconnect_attempts = 0;
    app->net_reconnect_failed = 0;
    /* Immediately: the first attempt is the one most likely to work, and the
     * delay below exists to space out *retries*. */
    app->net_reconnect_at_ms = 0;
    app->net_force_rebuild = 1;
    /* Pushes NET_OUT_DISCONNECT, so the peer sees the FIN before the
     * re-established session asks for the character back. */
    ToriRS_Network_Logout(app->net);
    App_NetSessionReset(app);
}

/*
 * Watch a live session, and drive the re-establishment of a dead one.
 *
 * Called once per App_RunOnce with the wall clock, ahead of the logic ticks:
 * a frame that decides the backlog is stale must not first spend five ticks
 * draining it.
 */
static void
app_net_link_watch(
    struct App* app,
    uint64_t now_ms)
{
    uint64_t gap;

    if( !app->net || !app->net_enabled )
        return;

    gap = app->last_frame_ms && now_ms > app->last_frame_ms ? now_ms - app->last_frame_ms : 0;

    if( !app->net_lost )
    {
        /*
         * TORIRS_NET_DROP_MS=<ms>: sever the connection this long after the
         * first packet. The headless equivalent of the reference's
         * `::clientdrop` — a harness has no chat box to type into, and the
         * whole point of this path is that it is otherwise reached only by
         * genuinely losing a socket.
         */
        {
            static long drop_ms = -2;
            if( drop_ms == -2 )
            {
                char const* env = getenv("TORIRS_NET_DROP_MS");
                drop_ms = env && *env ? strtol(env, NULL, 0) : -1;
            }
            if( drop_ms > 0 && app->net_last_recv_ms &&
                now_ms - app->net_first_recv_ms >= (uint64_t)drop_ms )
            {
                drop_ms = -1; /* once */
                app_net_lost(app, "TORIRS_NET_DROP_MS");
                return;
            }
        }
        /*
         * 1. This process stopped running.
         *
         * The browser stops calling a hidden tab's animation frame, so the
         * client stops draining a socket the server keeps writing to. Coming
         * back and replaying that backlog is what made a returning tab spend
         * seconds fast-forwarding with input ignored — and, before the
         * transports learned back-pressure, silently truncated the stream.
         * Neither is worth having: the session is stale, so drop it.
         *
         * Gated on having heard from the server at least once: a boot frame
         * can legitimately run long (a cold cache, a browser IO round trip),
         * and there is no session to lose yet.
         */
        if( app->net_last_recv_ms && gap >= (uint64_t)APP_NET_STALL_MS )
        {
            app_net_lost(app, "client was not running");
            return;
        }
        /*
         * 2. The server stopped speaking. The reference's own 15s bound; only
         * armed once in the game world, since the login handshake legitimately
         * sits quiet while a proof-of-work is solved.
         */
        if( app->net->state == TORIRS_NET_GAME && app->net_last_recv_ms &&
            now_ms - app->net_last_recv_ms >= (uint64_t)APP_NET_TIMEOUT_MS )
        {
            app_net_lost(app, "no packets for 15s");
            return;
        }
        /*
         * 3. The transport says the socket is gone. Only meaningful once the
         * session was up: before that, DISCONNECTED is just the initial state.
         */
        if( app->net_last_recv_ms &&
            (app->net->conn_status == TORIRS_NET_STATUS_DISCONNECTED ||
             app->net->conn_status == TORIRS_NET_STATUS_FAILED) &&
            app->net->state == TORIRS_NET_DISCONNECTED )
        {
            app_net_lost(app, "socket closed");
        }
        return;
    }

    /* Re-established: the handshake reached the game stream again. */
    if( app->net->state == TORIRS_NET_GAME )
    {
        TORIRS_LOG(
            "net: session re-established after %d attempt(s)\n", app->net_reconnect_attempts);
        app->net_lost = 0;
        app->net_reconnect_attempts = 0;
        app->net_last_recv_ms = now_ms;
        app->need_redraw = 1;
        return;
    }

    /* An attempt is still in flight while the login machine runs; only a
     * machine that fell back to DISCONNECTED has failed. */
    if( app->net->state == TORIRS_NET_LOGIN )
        return;
    if( app->net_reconnect_failed )
        return;
    if( now_ms < app->net_reconnect_at_ms )
        return;
    if( app->net_reconnect_attempts >= APP_NET_RECONNECT_MAX_ATTEMPTS )
    {
        TORIRS_LOG("net: giving up after %d reconnect attempts\n", app->net_reconnect_attempts);
        app->net_reconnect_failed = 1;
        app->need_redraw = 1;
        return;
    }

    app->net_reconnect_attempts++;
    app->net_reconnect_at_ms = now_ms + APP_NET_RECONNECT_DELAY_MS;
    TORIRS_LOG("net: reconnect attempt %d\n", app->net_reconnect_attempts);
    if( !ToriRS_Network_Reconnect(app->net) )
    {
        app->net_reconnect_failed = 1;
        app->need_redraw = 1;
    }
}

/*
 * XP-drop panel probe (TORIRS_XPDROP_DEBUG=1).
 *
 * The panel is entirely client-driven and its failure modes all present the
 * same way — no drops — so the probe prints the three states that separate them
 * and nothing else. The interface is the profile's `[iface:xpdrop]`; the child
 * numbers are that interface's own layout (child 2 statlistener holds the
 * stat-transmit hook, 17 drops_container, 18..24 the seven rows) and travel
 * with it, stable across rev 230 and 239.
 */
#define APP_XPDROP_COM(child) app_iface_com(app, "xpdrop", (child))
#define APP_XPDROP_ROW_COUNT 7

static int
app_xpdrop_debug(void)
{
    static int on = -1;
    if( on < 0 )
        on = getenv("TORIRS_XPDROP_DEBUG") ? 1 : 0;
    return on;
}

static void
app_xpdrop_debug_tick(struct App* app)
{
    static char last[768];
    static int last_print_cycle = -100000;
    char line[768];
    int n = 0;
    int32_t listener_idx = UITree_FindByComponentId(app->tree, APP_XPDROP_COM(2));
    struct RS_CS2StatTransmitHook const* hook = NULL;

    if( listener_idx < 0 )
        return; /* panel not mounted — nothing to say */

    for( int i = 0; i < app->host.stat_transmit_hook_count; i++ )
    {
        if( app->host.stat_transmit_hooks[i].component_id == APP_XPDROP_COM(2) )
        {
            hook = &app->host.stat_transmit_hooks[i];
            break;
        }
    }

    /* 122:0 (universe) carries the auto-hide timer script998 armed by script997.
     * Its last argument is the clientclock deadline at which it hides the whole
     * panel; once past it, script998 is supposed to disarm itself. */
    {
        int32_t const uni = UITree_FindByComponentId(app->tree, APP_XPDROP_COM(0));
        struct UITreeRuntimeScriptHook const* t =
            uni >= 0 ? &UITree_Hooks(&app->tree->components[uni])->on_timer : NULL;
        n += snprintf(
            line + n,
            sizeof(line) - (size_t)n,
            "uni{timer=%d deadline=%d} ",
            t ? t->script_id : -1,
            (t && t->argc > 0) ? UITree_HookArg(t, t->argc - 1) : -1);
    }

    n += snprintf(
        line + n,
        sizeof(line) - (size_t)n,
        "vc70=%d vc71=%d vc76=%d serial=%u hook{%s seen=%u args=%d hid=%d} "
        "c17hid=%d timers=%d",
        VarCManager_GetInt(&app->varcs, 70),
        VarCManager_GetInt(&app->varcs, 71),
        VarCManager_GetInt(&app->varcs, 76),
        app->host.stat_change_serial,
        hook ? "armed" : "MISSING",
        hook ? hook->last_seen_serial : 0u,
        hook ? hook->int_arg_count : -1,
        UITree_ComponentOrAncestorHidden(app->tree, APP_XPDROP_COM(2)) ? 1 : 0,
        UITree_ComponentOrAncestorHidden(app->tree, APP_XPDROP_COM(17)) ? 1 : 0,
        app->tree->timer_hooks.count);

    for( int r = 0; r < APP_XPDROP_ROW_COUNT; r++ )
    {
        int const com = APP_XPDROP_COM(18 + r);
        int32_t const idx = UITree_FindByComponentId(app->tree, com);
        int kids = 0;
        int timer = 0;
        int hid = 1;
        if( idx >= 0 )
        {
            for( int32_t c = app->tree->components[idx].first_child; c >= 0;
                 c = app->tree->components[c].next_sibling )
                kids++;
            timer = UITree_Hooks(&app->tree->components[idx])->on_timer.script_id;
            hid = UITree_ComponentOrAncestorHidden(app->tree, com) ? 1 : 0;
        }
        n += snprintf(
            line + n,
            sizeof(line) - (size_t)n,
            " r%d=%s/%d/%d/%d",
            r,
            idx < 0 ? "gone" : (hid ? "hid" : "vis"),
            kids,
            timer,
            idx >= 0 ? app->tree->components[idx].behavior.hide : -1);
        if( n >= (int)sizeof(line) )
            break;
    }

    /* Only on change, plus a heartbeat, so a session can be left running. The
     * clock is printed but deliberately not part of the compared state — it
     * changes every tick and would make every line "new". */
    if( strcmp(line, last) == 0 && (int)app->logic_cycle - last_print_cycle < 1500 )
        return;
    snprintf(last, sizeof(last), "%s", line);
    last_print_cycle = (int)app->logic_cycle;
    TORIRS_LOG("xpdrop: t=%d clock=%d %s\n", (int)app->logic_cycle, app->host.client_clock, line);
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
static int
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
                    "io_pending=%d cycle=%d\n",
                    (int)assets,
                    app->runner.progressed,
                    n,
                    Platform_IO_Pending(app->runner.px, app->runner.io),
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
                TORIRS_LOG(
                    "frame_latch: exec parked stat=%d head=%s blocked=%d io_pending=%d cycle=%d\n",
                    (int)stat,
                    head ? head->name : "(none)",
                    head ? head->blocked : -1,
                    Platform_IO_Pending(app->exec_runner.px, app->exec_runner.io),
                    (int)app->logic_cycle);
            }
            app->exec_runner_had_work = 1;
            break;
        }
        app->exec_runner_had_work = 0;

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
            app->net_last_recv_ms = app->last_frame_ms;
            if( !app->net_first_recv_ms )
                app->net_first_recv_ms = app->last_frame_ms;

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
            ToriRS_TaskQueue_Add(app->exec_runner.queue, CreateTask_GameProtoExec(app, &packet));
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
    if( drained && app->pending_clientscript_count &&
        (!app->server_tick_fence_seen ||
         app->logic_cycle - app->pending_clientscript_cycle >= APP_CLIENTSCRIPT_FENCE_MAX_CYCLES) )
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

/*
 * Submit whatever is in the login form.
 *
 * The one path a clicked Login, an Enter on the password and an autologin all
 * take, which is what makes the scripted lanes exercise the login screen
 * instead of going around it.
 */
/*
 * Restate the login checksums from the cache server, when that is where they
 * came from at boot.
 *
 * Before EVERY dial, because the init-time read alone left a trap: the server
 * repacks whenever its content changes, and a client that had been sitting at
 * the title since before the repack sent boot-time sums with each attempt --
 * a reply=6 ("client out of date") loop that no retry could leave, on
 * exactly the machine that keeps a client open across server iterations.
 * One HTTP GET per Login click; a failed read keeps the table it has.
 */
/* The login block carries the same identity the cache scripts are told
 * (CS2VM2_SetClientIdentity, resolved in App_Init): a mobile client says so
 * on the wire, and the server opens the mobile gameframe for it. */
static void
app_login_set_client_identity(struct App* app)
{
    assert(app);
    assert(app->net);
    app->net->client_type = CS2VM2_ClientType();
    app->net->platform_type = CS2VM2_OnMobile() ? 2 : 0;
}

static void
app_login_refresh_jag_checksums(struct App* app)
{
#if !defined(TORIRS_PLATFORM_WEB)
    int32_t crc[9];

    assert(app);
    if( !app->jag_crc_from_ondemand || !app->net || !app->net->rev )
        return;
    if( PlatformXIO_Dat1OnDemandJagChecksumsRefresh(app->runner.px, crc) == 0 )
        GameProtoRev_SetJagChecksums(app->net->rev, crc);
#else
    (void)app;
#endif
}

static void
app_title_submit(struct App* app)
{
    char const* user;

    assert(app);
    app->title.submit_requested = 0;

    if( !app->net_enabled || !app->net )
        return;

    /* The password is not read here: the connect happens a tick later and
     * takes both straight off the form -- see App::title_connect_pending. */
    user = RS_Title_FieldText(&app->title, RS_TITLE_FIELD_USERNAME);
    /* Nothing to send. Left on the form rather than dialled with an empty
     * name, which every server answers with a rejection the player then has
     * to read as if it meant something. */
    if( user[0] == '\0' )
        return;

    /* The client knows WHEN to say this; the revision says what. */
    RS_Title_SetMessages(
        &app->title, NULL, RS_LoginReplies_String(&app->login_replies, "connecting"), NULL);
    app->screen = APP_SCREEN_CONNECTING;
    app_title_state_changed(app);
    /*
     * And stop here, one frame short of dialling.
     *
     * Everything above is a change to what is ON SCREEN -- the message line,
     * and the Login/Cancel buttons that app_title_sync_groups withdraws for
     * APP_SCREEN_CONNECTING -- and none of it has been drawn yet.
     * app_title_state_changed has asked for the frame; connecting here would
     * spend the rest of this tick, and every tick after it, on a handshake and
     * then on the whole gameframe's assets, with the pre-click picture still on
     * the screen. The player clicks Login and watches nothing happen.
     *
     * The tick ends instead, the frame goes out, and app_title_tick dials on
     * the next one -- see App::title_connect_pending.
     */
    app->title_connect_pending = 1;
}

/*
 * The title screen's own tick: autologin, submit, and the login result.
 *
 * Runs while the session is on the title screen or connecting through it. The
 * frame loop keeps drawing throughout, which is where "Connecting to
 * server..." appears -- the reference does the same, and it is the reason the
 * connect had to come out of App_Init.
 */
static int
app_title_tick(struct App* app)
{
    int redraw = 0;

    assert(app);

    /*
     * A networked profile that declares no title screen still has to log in.
     *
     * Any manifest whose revconfig predates [layout:title] is in that
     * position: it boots straight to the gameframe, so there is no screen to
     * show progress on -- but the credentials still have to reach the server,
     * which is what App_Init used to do before the connect moved to submit.
     */
    if( app->net_enabled && app->net && !app->autologin_done && app->screen == APP_SCREEN_GAME &&
        app->autologin_user[0] )
    {
        app->autologin_done = 1;
        app_login_refresh_jag_checksums(app);
        app_login_set_client_identity(app);
        ToriRS_Network_ConnectLogin(
            app->net, app->connect_target, app->autologin_user, app->autologin_pass);
        return 0;
    }

    if( app->screen != APP_SCREEN_TITLE && app->screen != APP_SCREEN_CONNECTING )
        return 0;
    /* Nothing to submit into until the title tree is up. */
    if( app->app_state != APP_STATE_READY )
        return 0;

    /*
     * Credentials from the command line or the manifest: prefill and submit
     * once. Once, because a rejected login must land back on the form rather
     * than dial again forever.
     */
    if( !app->autologin_done && app->autologin_user[0] && app->screen == APP_SCREEN_TITLE )
    {
        app->autologin_done = 1;
        RS_Title_SetFieldText(&app->title, RS_TITLE_FIELD_USERNAME, app->autologin_user);
        RS_Title_SetFieldText(&app->title, RS_TITLE_FIELD_PASSWORD, app->autologin_pass);
        RS_Title_SetScreen(&app->title, RS_TITLE_LOGIN_FORM);
        app->title.submit_requested = 1;
        redraw = 1;
    }

    if( app->title.submit_requested )
    {
        app_title_submit(app);
        redraw = 1;
    }
    /*
     * The connect the previous tick deferred, now that its frame has been
     * drawn. Read out of the form rather than carried across, because the form
     * IS the state -- nothing can have edited it in between, and a copy would
     * be a second place for the credentials to live.
     */
    else if( app->title_connect_pending )
    {
        app->title_connect_pending = 0;
        app_login_refresh_jag_checksums(app);
        app_login_set_client_identity(app);
        ToriRS_Network_ConnectLogin(
            app->net,
            app->connect_target,
            RS_Title_FieldText(&app->title, RS_TITLE_FIELD_USERNAME),
            RS_Title_FieldText(&app->title, RS_TITLE_FIELD_PASSWORD));
        redraw = 1;
    }

    /* The handshake finished: the server's IF_OPENTOP roots the gameframe,
     * exactly as a networked boot used to do straight out of App_Init. */
    if( app->screen == APP_SCREEN_CONNECTING && app->net && app->net->state == TORIRS_NET_GAME )
    {
        App_OpenRootInterface(app, -1);
        return 1;
    }

    /*
     * The handshake failed. Back to the form, with what the server said.
     *
     * The reply code is the only thing separating "wrong password" from "this
     * world is full" from "you have only just left another world", and the
     * player is owed the difference. The words are the profile's; if it
     * declares none, the code goes to the log and the screen says nothing
     * rather than inventing a sentence.
     *
     * Not while the dial is still queued. app_title_submit puts the screen on
     * APP_SCREEN_CONNECTING one tick BEFORE it connects (App::title_connect_pending),
     * and TORIRS_NET_DISCONNECTED is also the state a network that has never
     * been dialled sits in -- so without this the submit tick reads its own
     * not-yet-started handshake as a failure, and the player is shown
     * [login_reply:default] ("Unexpected server response") on the way to a
     * login that then succeeds.
     */
    if( app->screen == APP_SCREEN_CONNECTING && !app->title_connect_pending && app->net &&
        app->net->state == TORIRS_NET_DISCONNECTED )
    {
        struct RS_LoginReply const* reply =
            RS_LoginReplies_Get(&app->login_replies, app->net->login_reply);

        app->screen = APP_SCREEN_TITLE;
        if( reply )
        {
            RS_Title_SetMessages(&app->title, reply->line[0], reply->line[1], reply->line[2]);
            if( reply->screen >= 0 )
                RS_Title_SetScreen(&app->title, (enum RS_TitleScreen)reply->screen);
        }
        else
        {
            TORIRS_ERR(
                "login: rejected with reply=%d and the profile declares no text for it\n",
                app->net->login_reply);
            RS_Title_SetMessages(&app->title, NULL, NULL, NULL);
        }
        app_title_state_changed(app);
        return 1;
    }

    return redraw;
}

#include "app_tick.u.c"

/**
 * Position an actor that is standing in a non-root view (SAILING_PLAN C5.2).
 * Returns 1 when it handled the element, 0 when the actor is ashore and the
 * caller's ordinary root-space path applies.
 *
 * The element carries DECK-LOCAL coordinates and the actor's deck-local yaw;
 * C3's descent transform is what puts it back in root space at emit time,
 * composing the hull's yaw onto the element's. Writing root coordinates here
 * instead would rotate the actor around the boat twice. Height comes from the
 * DECK's heightmap, at the deck's own plane (@see app_wev_deck_level), so an
 * actor stands on the planking rather than on the sea floor beneath it — or,
 * as level 0 gave, inside the hull under the planking.
 */
static int
app_world_sync_placement(
    struct App* app,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int element_id,
    int yaw,
    int actor_level)
{
    struct Worldview* view;
    struct Wev const* wev;
    int deck_yaw;
    int deck_y;

    assert(app);
    assert(placement);
    assert(element_id >= 0);

    if( placement->view_id == WORLDVIEW_ROOT )
        return 0;
    /* The view can die between the routing pass and this one only through
     * App_WevDespawn, which evicts first — so a stale id here would be a bug,
     * not a race. Guarded rather than asserted all the same: the alternative
     * is aborting the client over one frame of one actor. */
    if( !WorldviewRegistry_IsLive(&app->worldviews, placement->view_id) ||
        !Wevs_IsLive(&app->wevs, placement->view_id) )
        return 0;
    view = WorldviewRegistry_Get(&app->worldviews, placement->view_id);
    assert(view->world);
    wev = Wevs_Get(&app->wevs, placement->view_id);
    assert(wev);

    /* The element's yaw is composed with the hull's on the way out, so what
     * goes on the element is the actor's heading RELATIVE to the deck. A
     * HOMED actor's whole frame is already the deck's — their movement, and
     * so the yaw the mover derives from it, happens in view-local space — so
     * their yaw goes on unrotated; only a projected (footprint-routed) actor
     * carries a root-frame heading that needs the hull's yaw taken out. */
    /* The legacy NPC bridge projects positions but still sends each crew
     * member's native deck-facing direction (NPC_INFO face_dir). */
    deck_yaw =
        actor_level < 0 || (placement->home_view == placement->view_id && placement->home_view != 0)
            ? yaw & 0x7ff
            : (yaw - wev->angle) & 0x7ff;
    /* The deck plane: a PLAYER stands at their own wire plane inside the
     * boat's world — the deob copies the PLAYER_INFO coordinate's plane
     * verbatim (class60.field552) and samples the sub-view's heights there;
     * the config's plane opcode is only ever a menu/click plane selector.
     * A caller passing -1 (npcs) gets the config plane, matching the deob's
     * npc rule (class86 never overrides getPlane, so an npc reads the
     * view's plane). Boarding at level 0 therefore KEEPS you at level 0. */
    {
        int deck_level = actor_level;

        if( deck_level < 0 )
            deck_level = app_wev_deck_level(app, placement->view_id);
        if( deck_level >= COLLISION_LEVELS )
            deck_level = COLLISION_LEVELS - 1;
        deck_y = World_HeightAt(view->world, placement->x, placement->z, deck_level);
    }
    ToriDraw_SceneElementSetPosition(
        app->scene, element_id, placement->x, deck_y, placement->z, deck_yaw);
    return 1;
}

/* Movers (players/npcs) and in-flight projectiles push their sim positions
 * into the scene elements the frame emitter draws (v1 synced projectiles;
 * movers were spawn-time only there because nothing pathed them). */
static void
app_world_sync_positions(struct App* app)
{
    struct World* world = app->world;
    struct World_EntityPool* pool;
    /* Probed once: the two prints below run for every player and every npc in
     * the scene, every frame. */
    static int npcpos_debug = -1;
    if( npcpos_debug < 0 )
        npcpos_debug = getenv("TORIRS_NPCPOS_DEBUG") != NULL;
    /* Reference getAvH(minusedlevel, …) — all movers sit on the local plane. */
    int local_level = app_cinema_level(app);

    pool = &world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        if( !player || player->element_id < 0 )
            continue;
        /* Rebuild-parked movers sit outside the scene until the server's
         * next info packet removes them; the heightmap has no data there. */
        if( player->grid_position.x < 0 || player->grid_position.z < 0 ||
            player->grid_position.x >= world->_scene_size ||
            player->grid_position.z >= world->_scene_size )
            continue;
        int wx = (int)player->draw_position.x;
        int wz = (int)player->draw_position.z;
        int wy;
        if( app_world_sync_placement(
                app,
                &player->view_placement,
                player->element_id,
                player->orientation.yaw,
                player->grid_position.level) )
            continue;
        wy = app_world_height(app, wx, wz, local_level);
        if( npcpos_debug )
            TORIRS_LOG(
                "plrpos: tile=%d,%d lvl=%d(local %d) w=%d,%d y=%d\n",
                player->grid_position.x,
                player->grid_position.z,
                player->grid_position.level,
                local_level,
                wx,
                wz,
                wy);
        ToriDraw_SceneElementSetPosition(
            app->scene, player->element_id, wx, wy, wz, player->orientation.yaw);
    }

    pool = &world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        if( !npc || npc->element_id < 0 )
            continue;
        if( npc->grid_position.x < 0 || npc->grid_position.z < 0 ||
            npc->grid_position.x >= world->_scene_size ||
            npc->grid_position.z >= world->_scene_size )
            continue;
        int wx = (int)npc->draw_position.x;
        int wz = (int)npc->draw_position.z;
        int wy;
        /* -1: npcs take the deck's config plane, the deob's rule (class86
         * does not override getPlane, so an npc reads its view's plane). */
        if( app_world_sync_placement(
                app, &npc->view_placement, npc->element_id, npc->orientation.yaw, -1) )
            continue;
        wy = app_world_height(app, wx, wz, local_level);
        if( npcpos_debug )
            TORIRS_LOG(
                "npcpos: id=%d tile=%d,%d lvl=%d(local %d) w=%d,%d y=%d size=%d "
                "h0=%d h1=%d h2=%d lb=%d\n",
                npc->npc_id,
                npc->grid_position.x,
                npc->grid_position.z,
                npc->grid_position.level,
                local_level,
                wx,
                wz,
                wy,
                npc->size,
                heightmap_get_interpolated(app->world->heightmap, wx, wz, 0),
                heightmap_get_interpolated(app->world->heightmap, wx, wz, 1),
                heightmap_get_interpolated(app->world->heightmap, wx, wz, 2),
                (World_TileFlagGet(app->world, wx >> 7, wz >> 7, 1) & RSCACHE_FLOFLAG_LINK_BELOW) !=
                    0);
        ToriDraw_SceneElementSetPosition(
            app->scene, npc->element_id, wx, wy, wz, npc->orientation.yaw);
    }

    pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Projectile* proj = World_EntityPoolGet(pool, i);
        if( !proj || proj->element_id < 0 || !proj->launched )
            continue;
        ToriDraw_SceneElementSetPositionPitchYaw(
            app->scene,
            proj->element_id,
            (int)proj->x,
            (int)proj->y,
            (int)proj->z,
            proj->orientation.pitch,
            proj->orientation.yaw);
    }
}

/*
 * Sequence-frame-sound picker RNG.
 *
 * The reference uses `Math.random()`. A fixed-seed LCG is used instead so a
 * headless run picks the same alternatives every time -- the screenshot and BMP
 * harnesses compare frames, and a genuinely random audio path would still be a
 * genuinely random *counter* in the audio ledger `TORIRS_AUDIO_DEBUG` prints.
 * The sequence is long enough that no frame's alternatives correlate.
 */
static uint32_t g_frame_sound_rng = 0x9e3779b9u;

static uint32_t
app_next_random(struct App* app)
{
    (void)app;
    g_frame_sound_rng = g_frame_sound_rng * 1664525u + 1013904223u;
    return g_frame_sound_rng >> 16;
}

/*
 * Play a frame sound for a sequence animation when its frame advances.
 *
 * `world_x`/`world_z` are the element's position in world units, so the sound is
 * attenuated and panned from where the thing making it is standing -- a smithing
 * hammer three squares away should not be as loud as one under the camera. Pass
 * -1 for a sound with no place in the scene.
 *
 * A frame may declare **several alternative** sounds and exactly one of them
 * plays, chosen in proportion to the entries' weights (rev226+; 67 osrs239
 * frames carry up to six). The map is sorted by frame index with repeats, so the
 * alternatives for a frame are a contiguous run -- the binary search finds *a*
 * member of that run and the run has to be widened from there. Stopping at the
 * first hit, which is what this used to do, made the choice a function of where
 * the search happened to land.
 */
static void
app_play_frame_sounds(
    struct App* app,
    const struct ToriDraw_Animation* anim,
    int current_frame,
    int world_x,
    int world_z)
{
    int left = 0;
    int right;
    int hit = -1;
    int first;
    int last;
    int total_weight = 0;
    int roll;
    int chosen;
    struct ToriDraw_AnimFrameSound const* sound;

    assert(anim);
    if( anim->frame_sounds.count <= 0 )
        return;
    assert(app);

    right = anim->frame_sounds.count - 1;
    while( left <= right )
    {
        int mid = (left + right) / 2;
        int frame_idx = anim->frame_sounds.frame_indices[mid];
        if( frame_idx == current_frame )
        {
            hit = mid;
            break;
        }
        if( frame_idx < current_frame )
            left = mid + 1;
        else
            right = mid - 1;
    }
    if( hit < 0 )
        return;

    first = hit;
    while( first > 0 && anim->frame_sounds.frame_indices[first - 1] == current_frame )
        first--;
    last = hit;
    while( last + 1 < anim->frame_sounds.count &&
           anim->frame_sounds.frame_indices[last + 1] == current_frame )
        last++;

    chosen = first;
    if( last > first )
    {
        /*
         * Weights are relative, and an entry that declares none (-1, the pre-226
         * shape) counts as one -- otherwise a single unweighted alternative in a
         * weighted run could never be picked.
         */
        for( int i = first; i <= last; i++ )
            total_weight +=
                anim->frame_sounds.sounds[i].weight > 0 ? anim->frame_sounds.sounds[i].weight : 1;
        roll = (int)(app_next_random(app) % (uint32_t)(total_weight > 0 ? total_weight : 1));
        for( int i = first; i <= last; i++ )
        {
            int w =
                anim->frame_sounds.sounds[i].weight > 0 ? anim->frame_sounds.sounds[i].weight : 1;
            if( roll < w )
            {
                chosen = i;
                break;
            }
            roll -= w;
        }
    }

    sound = &anim->frame_sounds.sounds[chosen];
    if( sound->id < 0 )
        return;
    if( world_x >= 0 )
        RS_Audio_SynthAt(
            &app->audio, sound->id, sound->loops, 0, world_x >> 7, world_z >> 7, sound->radius, 0);
    else
        RS_Audio_Synth(&app->audio, sound->id, sound->loops, 0);
}

/*
 * World_AnimSoundSink: one entity animation frame, as the world steps onto it.
 *
 * The world knows nothing about the animation registry, so resolving the seq is
 * this side's job -- and a seq whose async load has not landed yet is an
 * ordinary runtime state, not a caller bug: the entity holds frame 0 of it and
 * simply makes no sound until it arrives.
 */
static void
app_world_anim_frame_sound(
    void* userdata,
    int seq_id,
    int frame,
    int world_x,
    int world_z)
{
    struct App* app = userdata;
    struct ToriDraw_Animation* anim;

    assert(app);
    anim = ToriDraw_SceneAnimationGet(app->scene, seq_id);
    if( !anim )
        return;
    app_play_frame_sounds(app, anim, frame, world_x, world_z);
}

/* One client tick of scene-element animation frames. UITreeAnim only advances
 * UI model widgets; world scene elements (scenery + entities) advance here
 * (v1 GameRunescape_TickAnimations, both classic and skeletal branches). */
static void
app_world_tick_animations(struct App* app)
{
    /* Only elements with a seq bound, rather than every slot in a pool that is
     * overwhelmingly static scenery. The list is a hint (it can hold ids that
     * have since died or lost their seq), so the per-element checks below still
     * stand — they are just no longer paid once per slot per cycle. */
    int anim_count = 0;
    int const* anim_ids = ToriDraw_SceneAnimatedElements(app->scene, &anim_count);
    TORIRS_PERF_COUNT_SET(
        TORIRS_PERF_CTR_SCENE_ELEMENTS,
        app->scene ? ToriDraw_SceneElementSlotCount(app->scene) : 0);
    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_SCENE_ANIM_LIST, anim_count);
    if( app->provider )
    {
        TORIRS_PERF_COUNT_SET(
            TORIRS_PERF_CTR_CACHE_MODEL_SIZE,
            app->provider->model_cache ? (int64_t)app->provider->model_cache->size : 0);
        TORIRS_PERF_COUNT_SET(
            TORIRS_PERF_CTR_CACHE_SPRITE_SIZE,
            app->provider->sprite_cache ? (int64_t)app->provider->sprite_cache->size : 0);
    }
    for( int k = 0; k < anim_count; k++ )
    {
        int element_id = anim_ids[k];
        struct ToriDraw_SceneElement* element;

        if( !ToriDraw_SceneElementIsLive(app->scene, element_id) )
            continue;
        element = ToriDraw_SceneElementGet(app->scene, element_id);
        if( !element || element->anim_seq_id == -1 )
            continue;
        /* Entity elements: the world sim steps their frames (delay/loop/
         * priority semantics) — the naive modulo tick must not touch them. */
        if( element->anim_external )
            continue;

        if( element->is_skeletal )
        {
            const struct ToriDraw_SkeletalAnim* skeletal = element->skeletal_animation;
            const struct ToriDraw_Animation* anim = element->animation;
            int play_frames;
            if( !skeletal || skeletal->frame_count <= 0 )
                continue;
            play_frames = element->skeletal_play_frames;
            if( play_frames <= 0 || play_frames > skeletal->frame_count )
                play_frames = skeletal->frame_count;
            element->anim_cycle++;
            if( element->anim_cycle >= 1 )
            {
                /* Location DynamicObjects do not modulo-wrap: frameStep
                 * decides whether the final pose is retained or the sequence
                 * is discarded. Keep the skeletal playback span as the
                 * authoritative bound when the config limits it. */
                if( anim && anim->frame_count == play_frames )
                {
                    if( !ToriDraw_AnimationAdvanceObjectFrame(anim, &element->anim_frame) )
                        ToriDraw_SceneElementSetAnimation(app->scene, element_id, NULL, true);
                }
                else
                    element->anim_frame = (element->anim_frame + 1) % play_frames;
                element->anim_cycle = 0;
            }
        }
        else
        {
            const struct ToriDraw_Animation* anim = element->animation;
            if( !anim || anim->frame_count <= 0 || !anim->frames )
                continue;
            {
                int old_frame = element->anim_frame;

                if( element->anim_loop )
                    ToriDraw_AnimationAdvanceLoopCycles(
                        anim, &element->anim_frame, &element->anim_cycle, 1);
                else if( !ToriDraw_AnimationAdvanceObjectCycles(
                             anim, &element->anim_frame, &element->anim_cycle, 1) )
                    ToriDraw_SceneElementSetAnimation(app->scene, element_id, NULL, true);
                /* Play any frame sounds for the new frame. A finished
                 * DynamicObject has no sequence, so it cannot emit another. */
                if( element->anim_seq_id != -1 && element->anim_frame != old_frame )
                    app_play_frame_sounds(
                        app,
                        anim,
                        element->anim_frame,
                        element->world_position.x,
                        element->world_position.z);
            }
        }
    }
}

enum
{
    APP_CAMERA_MOVEMENT_SPEED = 70, /* v1 RUNESCAPE_CAMERA_MOVEMENT_SPEED */
    APP_CAMERA_ROTATION_SPEED = 10,
};

static int
app_world_drawable(struct App* app)
{
    /* world_view_valid == a WORLD desc survived the last emit walk, so a hidden
     * or absent viewport component costs nothing: no paint, no 3D, no pick.
     * During a server-driven rebuild (deob gameState 25 / Client-TS sceneState
     * 1) suppress the world so mid-load frames are not a frozen wrong scene —
     * App_Render draws the "Loading - please wait." overlay instead. */
    return app->world_view_valid && app->world && app->world->load_complete &&
           app->world->painter && app->painter_buffer &&
           !(app->world_load_server_driven && app->world_load_inflight);
}

/* deob method5761 / Client-TS REBUILD_NORMAL: black fill of the game area plus
 * centred "Loading - please wait." while maps rebuild. */
static void
app_draw_viewport_message(
    struct App* app,
    int* pixels,
    int width,
    int height,
    char const* line1,
    char const* line2_nullable,
    int fill_black)
{
    struct ToriDraw_Font* font;
    struct ToriDraw_ViewPort vp;
    int font_cache_id;
    int scene_id;
    int vx, vy, vw, vh;
    int cx, cy;

    assert(app);
    assert(pixels);
    if( width <= 0 || height <= 0 || !line1 )
        return;

    if( app->world_view_valid )
    {
        vx = app->world_emit_desc.x;
        vy = app->world_emit_desc.y;
        vw = app->world_emit_desc.w;
        vh = app->world_emit_desc.h;
    }
    else
    {
        vx = 0;
        vy = 0;
        vw = width;
        vh = height;
    }
    if( vw <= 0 || vh <= 0 )
        return;

    /*
     * Whether the world underneath survives is the caller's call, and the two
     * callers differ. A scene rebuild blacks it out because the scene it was
     * drawn from is gone (deob method5761). A lost connection does not: the
     * reference paints its two lines straight onto the retained viewport, and
     * the last frame the session produced is exactly what a player wants to
     * still be looking at while it comes back.
     */
    if( fill_black )
    {
        for( int y = vy; y < vy + vh; y++ )
        {
            if( y < 0 || y >= height )
                continue;
            for( int x = vx; x < vx + vw; x++ )
            {
                if( x < 0 || x >= width )
                    continue;
                pixels[y * width + x] = 0x000000;
            }
        }
    }

    font_cache_id = app_font_cache_id(app, APP_FONT_P12);
    scene_id = font_cache_id >= 0 ? UITreeSceneBridge_EnsureFont(&app->bridge, font_cache_id) : -1;
    if( scene_id < 0 )
    {
        if( font_cache_id >= 0 )
        {
            struct ToriRS_Task* task = CreateTask_FontLoad(app->provider, font_cache_id);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
        }
        scene_id = app_minimenu_font_scene_id(app);
    }
    if( scene_id < 0 )
        return;
    font = ToriDraw_SceneFontGet(app->scene, scene_id);
    if( !font )
        return;

    vp.width = width;
    vp.height = height;
    vp.stride = width;
    vp.x_center = width / 2;
    vp.y_center = height / 2;
    vp.clip_left = vx < 0 ? 0 : vx;
    vp.clip_top = vy < 0 ? 0 : vy;
    vp.clip_right = (vx + vw > width) ? width : (vx + vw);
    vp.clip_bottom = (vy + vh > height) ? height : (vy + vh);

    cx = vx + vw / 2;
    cy = vy + vh / 2;
    /* Two lines straddle the centre by the reference's own 15px step
     * (143/158 against a 503-tall viewport); one line sits on it. */
    if( line2_nullable )
        cy -= 8;
    /* Shadow then white — matches deob black+white centreString pair. */
    (void)ToriDraw2D_DrawString(font, &vp, cx + 1, cy + 1, line1, 0x000000, true, false, pixels);
    (void)ToriDraw2D_DrawString(font, &vp, cx, cy, line1, 0xffffff, true, false, pixels);
    if( line2_nullable )
    {
        (void)ToriDraw2D_DrawString(
            font, &vp, cx + 1, cy + 16, line2_nullable, 0x000000, true, false, pixels);
        (void)ToriDraw2D_DrawString(
            font, &vp, cx, cy + 15, line2_nullable, 0xffffff, true, false, pixels);
    }
}

/*
 * The face the boot bar's caption is drawn with, on every lane.
 *
 * ONE face for the whole boot, and a baked one: ToriRSChromeFont_Menu is cache
 * archive 496 (b12) baked into .rdata (engine/torirs_debug_font_baked.h),
 * needing no cache and no IO.
 *
 * The alternative -- resolve the profile's p12 when the cache has handed it
 * over and fall back to the baked face until then -- is what this used to do,
 * and it is wrong twice. It changes face mid-boot, so the same bar draws its
 * sentences in two or three different hands as the archives land; and it makes
 * the caption depend on cache state that the GPU lanes, which draw the bar
 * before any frame is built, have no path to wait for.
 *
 * The reference has neither problem because it has one face for the whole
 * screen and always has it: an AWT `java.awt.Font("Helvetica", BOLD, 13)`
 * (deob class510; Client-TS GameShell.messageBox draws `bold 13px helvetica`).
 * A software rasteriser has no system face, so a baked one is the same answer
 * to the same question -- and the BOLD one, for the same reason.
 *
 * At 1x, because this lands in canvas pixels on every lane: the boot bar is
 * placed in canvas coordinates, and a chrome-scaled face would paint
 * double-size text into them.
 *
 * The Ensure is what puts the font IN the scene -- ToriDraw_SceneFontAdd emits
 * TORIDRAW_EVENT_FONT_LOAD -- and the GPU backends resolve a font id by
 * looking it up there (d3d9_ui_ensure_font, gl3_ensure_font_slot). A lane that
 * never called this would find no font under the id and draw nothing, which is
 * exactly what the D3D9 boot screen did while the caption lived in App_Render.
 */
static int
app_boot_bar_font_scene_id(struct App* app)
{
    assert(app);
    return UITreeSceneBridge_EnsureDebugFont1x(&app->bridge, TORIRS_CHROME_FONT_MENU);
}

char const*
App_BootBarCaption(
    struct App* app,
    int* out_font_scene_id)
{
    char const* caption = NULL;
    int font_scene_id;

    assert(app);
    assert(out_font_scene_id);

    /*
     * What the boot task asked for, when it asked for anything; otherwise the
     * profile's own word for the phase.
     *
     * The render step does not decide what a load looks like: the task that
     * knows which stage it is at says so, and this obeys. A task which never
     * asks gets the standing sentence rather than silence.
     */
    if( app->runner.render.intent == TORIRS_RENDER_BOOT_BAR && app->runner.render.caption &&
        app->runner.render.caption[0] )
        caption = app->runner.render.caption;
    else
        caption = RS_LoginReplies_String(
            &app->login_replies, app->screen == APP_SCREEN_GAME ? "entering_world" : "loading");
    if( !caption || !caption[0] )
        return NULL;

    font_scene_id = app_boot_bar_font_scene_id(app);
    if( font_scene_id < 0 )
        return NULL;
    *out_font_scene_id = font_scene_id;
    return caption;
}

/*
 * The bar's caption, centred on `center_x` with its baseline at `baseline_y`.
 *
 * The software lane's half of App_BootBarCaption: the GPU lanes draw the same
 * two facts (`text`, `font_scene_id`) through their own text paths.
 */
static void
app_boot_bar_caption(
    struct App* app,
    int* pixels,
    int width,
    int height,
    int center_x,
    int baseline_y,
    char const* text,
    int font_scene_id)
{
    struct ToriDraw_Font* font;
    struct ToriDraw_ViewPort vp;

    assert(app);
    assert(pixels);
    assert(text);
    assert(font_scene_id >= 0);

    font = ToriDraw_SceneFontGet(app->scene, font_scene_id);
    if( !font )
        return;

    vp.width = width;
    vp.height = height;
    vp.stride = width;
    vp.x_center = width / 2;
    vp.y_center = height / 2;
    vp.clip_left = 0;
    vp.clip_top = 0;
    vp.clip_right = width;
    vp.clip_bottom = height;

    (void)ToriDraw2D_DrawString(
        font, &vp, center_x, baseline_y, text, 0xFFFFFF, true, false, pixels);
}

/* deob method5761 / Client-TS REBUILD_NORMAL: while the scene rebuilds, the
 * game area shows "Loading - please wait." instead of the world. */
static void
app_draw_rebuild_loading_overlay(
    struct App* app,
    int* pixels,
    int width,
    int height)
{
    app_draw_viewport_message(
        app, pixels, width, height, "Loading - please wait.", NULL, /* fill_black */ 1);
}

/*
 * The reference's lost-connection notice (Client-TS `lostCon`, Client.ts:2739;
 * deob gameState 40, client.java:8542), over the retained viewport.
 *
 * Two lines, because they answer two different questions: what happened, and
 * whether the player has to do anything about it. Once the attempts are spent
 * the second line stops promising a reconnect that is no longer coming.
 */
static void
app_draw_connection_lost_overlay(
    struct App* app,
    int* pixels,
    int width,
    int height)
{
    app_draw_viewport_message(
        app,
        pixels,
        width,
        height,
        "Connection lost",
        app->net_reconnect_failed ? "Unable to reestablish - please reload"
                                  : "Please wait - attempting to reestablish",
        /* fill_black */ 0);
}

/* Reference roofCheck (Client.ts 4713): walk the camera->player tile line;
 * if any stepped tile (endpoints included) carries the remove-roof land flag
 * (0x4), cut drawing down to the player's level, else draw all 4. Only armed
 * at low pitch (< 310) — the high look-down orbit clears roofs anyway.
 * Scripted cams use the simpler roofCheck2 height test. */
static int
app_world_roof_check(struct App* app)
{
    struct WorldEntity_Player* player = app_local_player(app);
    struct World* world = app->world;
    int top = 3;
    int level;

    if( !player || !world || !world->tile_flags )
        return 3;
    level = player->grid_position.level;

    /* Aboard, the player's coordinates are deck-local — the raster walk below
     * would march toward a tile near the scene corner and read roof flags
     * that are not over anyone. The deob flips to sub-view roof rules while
     * aboard; a hull on open water has no roofs to remove, so show all. */
    if( app->aboard_view != WORLDVIEW_ROOT )
        return 3;

    /*
     * "Hide roofs" — game option 1, and the first thing both of the reference's
     * roof checks test (`if (!prefs.isHidingRoofs())` guards the whole selective
     * walk in each; when it is set they return the player's level outright).
     * The Display panel's toggle and the reference's ::toggleroof cheat write
     * this same setting, whose two messages say what the two states are:
     * "Roofs are now all hidden" against "Roofs will only be removed
     * selectively".
     */
    if( RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_GAME, RS_CS2_GAMEOPTION_HIDE_ROOFS) )
        return level;

    if( app->cam_script.scripted )
    {
        int cam_tx = app->world_camera_pos.x >> 7;
        int cam_tz = app->world_camera_pos.z >> 7;
        int ground_y =
            app_world_height(app, app->world_camera_pos.x, app->world_camera_pos.z, level);
        if( ground_y - app->world_camera_pos.y >= 800 ||
            (World_TileFlagGet(world, cam_tx, cam_tz, level) & 0x4) == 0 )
            return 3;
        return level;
    }

    if( app->world_camera.pitch < 310 )
    {
        int cam_tx = app->world_camera_pos.x >> 7;
        int cam_tz = app->world_camera_pos.z >> 7;
        int ply_tx = (int)player->draw_position.x >> 7;
        int ply_tz = (int)player->draw_position.z >> 7;
        int delta_x = ply_tx > cam_tx ? ply_tx - cam_tx : cam_tx - ply_tx;
        int delta_z = ply_tz > cam_tz ? ply_tz - cam_tz : cam_tz - ply_tz;

        if( World_TileFlagGet(world, cam_tx, cam_tz, level) & 0x4 )
            top = level;

        if( delta_x > delta_z )
        {
            int delta = delta_x ? (delta_z * 65536) / delta_x : 0;
            int accumulator = 32768;
            while( cam_tx != ply_tx )
            {
                cam_tx += cam_tx < ply_tx ? 1 : -1;
                if( World_TileFlagGet(world, cam_tx, cam_tz, level) & 0x4 )
                    top = level;
                accumulator += delta;
                if( accumulator >= 65536 )
                {
                    accumulator -= 65536;
                    if( cam_tz != ply_tz )
                        cam_tz += cam_tz < ply_tz ? 1 : -1;
                    if( World_TileFlagGet(world, cam_tx, cam_tz, level) & 0x4 )
                        top = level;
                }
            }
        }
        else if( delta_z > 0 )
        {
            int delta = (delta_x * 65536) / delta_z;
            int accumulator = 32768;
            while( cam_tz != ply_tz )
            {
                cam_tz += cam_tz < ply_tz ? 1 : -1;
                if( World_TileFlagGet(world, cam_tx, cam_tz, level) & 0x4 )
                    top = level;
                accumulator += delta;
                if( accumulator >= 65536 )
                {
                    accumulator -= 65536;
                    if( cam_tx != ply_tx )
                        cam_tx += cam_tx < ply_tx ? 1 : -1;
                    if( World_TileFlagGet(world, cam_tx, cam_tz, level) & 0x4 )
                        top = level;
                }
            }
        }
    }

    if( World_TileFlagGet(
            world, (int)player->draw_position.x >> 7, (int)player->draw_position.z >> 7, level) &
        0x4 )
        top = level;
    return top;
}

/* Update painter frustum cull for the current camera. Default path builds a
 * per-frame analytic span from live eye height (zoom-aware). TORIRS_PAINTER_CULL=baked
 * restores the old CPU-baked table. TORIRS_PAINTER_NOCULL=1 disables both. */
static void
app_update_painter_cull(
    struct App* app,
    int cam_sx,
    int cam_sz)
{
    struct World* world;
    struct Painter* painter;
    char const* nocull;
    char const* cull_mode;
    int follow_cam;
    int vw;
    int vh;
    int near_z;
    int far_z;
    int radius;
    int center_sx;
    int center_sz;
    int eye_height;
    int level;
    int anchor_x;
    int anchor_z;
    struct PaintersCullSpan span;
    struct PaintersCullSpanParams params;

    assert(app);
    world = app->world;
    if( !world || !world->painter )
        return;
    painter = world->painter;
    {
        char const* dd = torirs_env_draw_distance();
        int v = ToriRS_Features_PainterDrawDistance(app->features);
        int from_env;
        if( dd && dd[0] != '\0' && sscanf(dd, "%d", &from_env) == 1 )
            v = from_env;
        painter_set_draw_distance(painter, v);
    }
    radius = painter_get_draw_distance(painter);

    nocull = torirs_env_painter_nocull();
    if( nocull && nocull[0] != '\0' && nocull[0] != '0' )
    {
        painter_set_cullspan(painter, NULL);
        if( !world->cullmap || !world->cullmap->all_visible )
        {
            if( world->cullmap )
                painters_cullmap_free(world->cullmap);
            world->cullmap = painters_cullmap_new_nocull();
            painter_set_cullmap(painter, world->cullmap);
        }
        painter_set_draw_center(painter, -1, -1);
        return;
    }

    if( !app->world_view_valid )
        return;
    vw = app->world_emit_desc.w;
    vh = app->world_emit_desc.h;
    if( vw < 1 || vh < 1 )
        return;

    /* The painter's draw box is centred on the EYE tile — the official does
     * this unconditionally (class112.method4111 derives the window from
     * field1755/field1765, the camera tile). Centring on the orbit anchor
     * instead put nine extra z rows behind the camera in the box and dropped
     * nine near ones, which is §9.7(b)'s share of the Inferno wedge
     * (docs/ORANGE_WEDGE.md, promoted per §11.7).
     * TORIRS_WEDGE_DRAWCENTER=orbit restores the old behaviour for A/B. */
    follow_cam = 0;
    {
        char const* dc = torirs_env_wedge_drawcenter();
        if( dc && strcmp(dc, "orbit") == 0 )
            follow_cam = app->net && !app->cam_script.scripted;
    }
    if( follow_cam )
    {
        /* (int) then >>7, as the reference does: field2354 is `(int) field917`
         * and the camera tile is that mirror shifted (client.java:9373). */
        center_sx = (int)app->orbit_x >> 7;
        center_sz = (int)app->orbit_z >> 7;
        anchor_x = (int)app->orbit_x;
        anchor_z = (int)app->orbit_z;
        painter_set_draw_center(painter, center_sx, center_sz);
    }
    else
    {
        center_sx = cam_sx;
        center_sz = cam_sz;
        anchor_x = app->world_camera_pos.x;
        anchor_z = app->world_camera_pos.z;
        painter_set_draw_center(painter, -1, -1);
    }

    level = 0;
    {
        struct WorldEntity_Player* player = World_PlayerGetByServerPid(world, world->local_pid);
        if( player )
            level = player->grid_position.level;
    }
    eye_height = app_world_height(app, anchor_x, anchor_z, level) - app->world_camera_pos.y;

    near_z = app->world_camera.near_plane_z;
    if( near_z < 1 )
        near_z = 50;
    /* Far clip = drawDistance * 210 (deob class243.method4457). Covers the
     * radius box diagonal (radius * 128 * sqrt(2)) at both 25 and 90. */
    far_z = radius * OCCLUDER_FAR_CLIP_PER_TILE;

    cull_mode = torirs_env_painter_cull();
    if( cull_mode && strcmp(cull_mode, "baked") == 0 )
    {
        struct PaintersCullMap* cm = NULL;
        int slice_n;
        struct timespec t0;
        struct timespec t1;
        uint64_t bake_ns;

        painter_set_cullspan(painter, NULL);

        /* Debounce viewport resize: the CPU bake is multi-hundred-ms. */
        if( app->painter_cullmap_bake_w > 0 && app->painter_cullmap_bake_h > 0 )
        {
            int dw = vw - app->painter_cullmap_bake_w;
            int dh = vh - app->painter_cullmap_bake_h;
            if( dw < 0 )
                dw = -dw;
            if( dh < 0 )
                dh = -dh;
            if( dw < 8 && dh < 8 )
                return;
        }

        {
            struct ToriDrawTrigTables tables = {
                .sin = ToriDraw_GetSinTable(),
                .cos = ToriDraw_GetCosTable(),
                .tan = ToriDraw_GetTanTable(),
            };
            struct ToriDrawTrigFns trig;
            ToriDraw_TrigFnsFromTables(&trig, &tables);

            clock_gettime(CLOCK_MONOTONIC, &t0);
            cm = painters_cullmap_build_toridraw(
                radius,
                near_z,
                vw,
                vh,
                toridraw_projection_cot16(
                    app->world_camera.projection_mode,
                    app->world_camera.projection_scale,
                    app->world_camera.fov_rpi2048),
                &trig);
            clock_gettime(CLOCK_MONOTONIC, &t1);
        }
        if( !cm )
            return;

        bake_ns =
            (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull + (uint64_t)(t1.tv_nsec - t0.tv_nsec);

        slice_n = painters_cullmap_slice_visible_count(
            cm, app->world_camera.pitch, app->world_camera.yaw);
        if( slice_n <= 0 )
        {
            TORIRS_LOG(
                "painter_cullmap: bake empty for pitch=%d yaw=%d near=%d %dx%d "
                "(%.2f ms) — keeping nocull\n",
                app->world_camera.pitch,
                app->world_camera.yaw,
                near_z,
                vw,
                vh,
                (double)bake_ns / 1.0e6);
            painters_cullmap_free(cm);
            if( !world->cullmap || !world->cullmap->all_visible )
            {
                if( world->cullmap )
                    painters_cullmap_free(world->cullmap);
                world->cullmap = painters_cullmap_new_nocull();
                painter_set_cullmap(painter, world->cullmap);
            }
            app->painter_cullmap_bake_w = vw;
            app->painter_cullmap_bake_h = vh;
            return;
        }

        TORIRS_LOG(
            "painter_cullmap: baked radius=%d near=%d %dx%d slice_vis=%d in %.2f ms\n",
            radius,
            near_z,
            vw,
            vh,
            slice_n,
            (double)bake_ns / 1.0e6);

        if( world->cullmap )
            painters_cullmap_free(world->cullmap);
        world->cullmap = cm;
        painter_set_cullmap(painter, world->cullmap);
        app->painter_cullmap_bake_w = vw;
        app->painter_cullmap_bake_h = vh;
        return;
    }

    /* Default: analytic per-frame span. Keep a nocull cullmap installed so any
     * leftover bit-test path is a no-op. */
    if( !world->cullmap || !world->cullmap->all_visible )
    {
        if( world->cullmap )
            painters_cullmap_free(world->cullmap);
        world->cullmap = painters_cullmap_new_nocull();
        painter_set_cullmap(painter, world->cullmap);
    }

    params.pitch = app->world_camera.pitch;
    params.yaw = app->world_camera.yaw & 0x7ff;
    params.eye_height = eye_height;
    params.y_lo = PCULL_FRUSTUM_Y_START;
    params.y_hi = PCULL_FRUSTUM_Y_END;
    params.near_clip = near_z;
    params.far_clip = far_z;
    params.screen_width = vw;
    params.screen_height = vh;
    /* Same values the frame will be drawn with; the cull frustum must not
     * assume a different projection scale than the rasterizer uses. */
    params.projection_mode = app->world_camera.projection_mode;
    params.projection_scale = app->world_camera.projection_scale;
    params.fov_rpi2048 = app->world_camera.fov_rpi2048;
    /* Eye-relative row range covering the draw box around the orbit centre. */
    params.dz_min = (center_sz - radius) - cam_sz - 2;
    params.dz_max = (center_sz + radius) - cam_sz + 2;
    if( params.dz_min < -PAINTERS_CULLSPAN_MAX_DZ )
        params.dz_min = -PAINTERS_CULLSPAN_MAX_DZ;
    if( params.dz_max > PAINTERS_CULLSPAN_MAX_DZ )
        params.dz_max = PAINTERS_CULLSPAN_MAX_DZ;

    painters_cullspan_build(&span, &params);
    painter_set_cullspan(painter, &span);
    (void)center_sx;
}

static void
app_world_paint(struct App* app)
{
    /* TORIRS_WEDGE_CAM=x,y,z,pitch,yaw — pin the eye so a draw-order capture can
     * be taken at the same camera as the instrumented official client. Pitch/yaw
     * are the C client's 2048-per-turn units (the official's 16384-per-turn value
     * divided by 8). Off unless the env var is set; when set it is re-applied every
     * frame *before* anything reads the camera, so the painter, the occluders and
     * the frame the renderer draws all agree. Ordering telemetry is worthless if
     * the two clients look from different places (the C settled eye is two tiles
     * farther in z than the official's, and the bucket traversal is centred on the
     * camera tile). */
    {
        static int resolved = 0;
        static int have = 0;
        static int have_path = 0;
        static int px, py, pz, ppitch, pyaw;
        static struct ToriRS_WedgeCameraPath path;
        if( !resolved )
        {
            char const* wc = getenv("TORIRS_WEDGE_CAM");
            char const* wp = getenv("TORIRS_WEDGE_CAM_PATH");
            resolved = 1;
            /* The path wins where a scene sets both: TORIRS_WEDGE_CAM is then
             * the still the route was authored from, and the route is the
             * thing being measured. */
            if( wp && wp[0] )
            {
                if( ToriRS_WedgeCameraPathParse(wp, &path) )
                    have_path = 1;
                else
                    /* Once, at resolve time, never per frame. Loud because the
                     * alternative is a mistyped route silently measuring a
                     * still camera and reporting a number that looks fine. */
                    TORIRS_ERR(
                        "TORIRS_WEDGE_CAM_PATH: cannot parse, camera not "
                        "moving: %s\n",
                        wp);
            }
            if( wc && sscanf(wc, "%d,%d,%d,%d,%d", &px, &py, &pz, &ppitch, &pyaw) == 5 )
                have = 1;
        }
        if( have_path )
        {
            struct ToriRS_WedgeCameraKey key;
            ToriRS_WedgeCameraPathEval(&path, g_torirs_frame_no, &key);
            app->world_camera_pos.x = key.x;
            app->world_camera_pos.y = key.y;
            app->world_camera_pos.z = key.z;
            app->world_camera.pitch = key.pitch;
            app->world_camera.yaw = key.yaw;
        }
        else if( have )
        {
            app->world_camera_pos.x = px;
            app->world_camera_pos.y = py;
            app->world_camera_pos.z = pz;
            app->world_camera.pitch = ppitch;
            app->world_camera.yaw = pyaw;
        }
    }

    /* >>7, not /128: the orbit eye can sit at negative coords past the scene
     * edge, and truncation toward zero would mis-seed the bucket flood-fill
     * origin by a tile. Clamp into the scene — the bucket's distance metric
     * and adjacency tests assume an in-bounds origin. */
    int cam_sx = app->world_camera_pos.x >> 7;
    int cam_sz = app->world_camera_pos.z >> 7;
    int cam_slevel = 0; /* painter_paint_bucket ignores it (iterates levels) */
    if( app->world )
    {
        int max_tile = app->world->_scene_size - 1;
        if( cam_sx < 0 )
            cam_sx = 0;
        if( cam_sx > max_tile )
            cam_sx = max_tile;
        if( cam_sz < 0 )
            cam_sz = 0;
        if( cam_sz > max_tile )
            cam_sz = max_tile;
    }
    /* The viewport component owns the level mask (RevConfig `levels=`); older
     * nodes leave it 0, which would draw nothing — treat that as all levels. */
    uint8_t level_mask = app->world_emit_desc.world_level_mask;
    if( cam_slevel < 0 )
        cam_slevel = 0;
    if( cam_slevel > 3 )
        cam_slevel = 3;
    if( !level_mask )
        level_mask = 0xF;
    /* Roof hiding: the per-frame camera->player roofCheck caps the top drawn
     * level; config (RevConfig levels=) can still restrict further. */
    level_mask &= (uint8_t)((1u << (app_world_roof_check(app) + 1)) - 1);
    /* The map editor's Vis row REPLACES the mask rather than narrowing it: the
     * point of the row is to see a plane the ordinary rules would hide, and an
     * AND could only ever take levels away. 0 is "all levels", the default,
     * and leaves everything above untouched. */
    uint8_t editor_vis_mask = 0;
    if( app->editor )
    {
        editor_vis_mask = Editor_PanelVisLevelMask(&app->editor_panel);
        if( editor_vis_mask )
            level_mask = editor_vis_mask;
    }
    /* CAM_SHAKE jitter (reference Client-TS 4448): each axis is a sine plus a
     * random spread, and all five compound. It displaces the camera for this
     * frame's draw only — the base position is restored below, or the y axis
     * would ratchet the eye away a little more every frame. */
    int shake_x = app->world_camera_pos.x;
    int shake_y = app->world_camera_pos.y;
    int shake_z = app->world_camera_pos.z;
    int shake_pitch = app->world_camera.pitch;
    int shake_yaw = app->world_camera.yaw;
    for( int axis = 0; axis < 5; axis++ )
    {
        int spread, jitter;
        if( !app->cam_script.shake[axis] )
            continue;
        spread = app->cam_script.shake_jitter[axis];
        jitter = (int)((double)rand() / ((double)RAND_MAX + 1.0) * (spread * 2 + 1)) - spread;
        jitter += (int)(sin((double)app->cam_script.shake_cycle[axis] *
                            ((double)app->cam_script.shake_speed[axis] / 100.0)) *
                        app->cam_script.shake_amplitude[axis]);
        switch( axis )
        {
        case 0:
            app->world_camera_pos.x += jitter;
            cam_sx = app->world_camera_pos.x >> 7;
            break;
        case 1:
            app->world_camera_pos.y += jitter;
            break;
        case 2:
            app->world_camera_pos.z += jitter;
            cam_sz = app->world_camera_pos.z >> 7;
            break;
        case 3:
            app->world_camera.yaw = (app->world_camera.yaw + jitter) & 0x7ff;
            break;
        case 4:
            app->world_camera.pitch = app_world_clamp_pitch(app, app->world_camera.pitch + jitter);
            break;
        default:
            break;
        }
        app->cam_script.shake_cycle[axis]++;
    }
    if( app->world )
    {
        int max_tile = app->world->_scene_size - 1;
        if( cam_sx < 0 )
            cam_sx = 0;
        if( cam_sx > max_tile )
            cam_sx = max_tile;
        if( cam_sz < 0 )
            cam_sz = 0;
        if( cam_sz > max_tile )
            cam_sz = max_tile;
    }
    painter_set_camera_angles(app->world->painter, app->world_camera.pitch, app->world_camera.yaw);
    painter_set_level_mask(app->world->painter, level_mask);

    /* World entities (SAILING_PLAN C3): the eye is only final here — WEDGE_CAM
     * has been applied and the shake added, and both are undone right after the
     * paint — so this is the one place a boat-space camera can be derived.
     * Once per boat per frame, not once per descent. */
    app_wev_bind_view_cameras(
        app,
        app->world_camera.pitch,
        app->world_camera.yaw,
        app->world_camera_pos.x,
        app->world_camera_pos.y,
        app->world_camera_pos.z);

    app_update_painter_cull(app, cam_sx, cam_sz);

    /* Planar occluders: project shadows for this eye. TORIRS_OCCLUDERS=0
     * disables (mirrors TORIRS_PAINTER_NOCULL=1). Default is on. */
    {
        struct SceneOccluders* occ = painter_get_occluders(app->world->painter);
        const char* env_occ = torirs_env_occluders();
        int occ_off = env_occ && env_occ[0] == '0' && env_occ[1] == '\0';
        if( occ && !occ_off )
        {
            int top_level = app_world_roof_check(app);
            /* The occluder set is bucketed by the top level being drawn --
             * bucket N holds every surface spanning levels 0..N. The Vis row
             * replaced the painter's level mask above, so the bucket has to
             * follow it: left at the roof-check level, the floors and roofs of
             * the storeys the row just hid keep casting their shadows and cull
             * the plane the user asked to look at. Highest set bit of the mask
             * is the top level it draws (solo included -- bucket N is the only
             * one whose surfaces can be in front of level N's geometry). */
            if( editor_vis_mask )
            {
                top_level = 0;
                while( (editor_vis_mask >> (top_level + 1)) != 0 )
                    top_level++;
            }
            /* Eye for depth/spread stays raw (deob cameraX/Y/Z); only the
             * camera tile used by the footprint gate is scene-clamped inside
             * select_for_camera so it lines up with the painter's cam_sx/sz. */
            scene_occluders_select_for_camera(
                occ,
                app->world_camera_pos.x,
                app->world_camera_pos.y,
                app->world_camera_pos.z,
                top_level,
                painter_get_draw_distance(app->world->painter),
                painter_get_cullspan(app->world->painter),
                NULL,
                app->world_camera.pitch,
                app->world_camera.yaw);
            if( torirs_env_occluders_debug() )
            {
                static int s_logged;
                if( !s_logged )
                {
                    int n_wall = 0;
                    int n_floor = 0;
                    int i;
                    s_logged = 1;
                    for( i = 0; i < occ->level_occluder_count[top_level]; i++ )
                    {
                        uint8_t p = occ->level_occluders[top_level][i].plane;
                        if( p == OCCLUDER_PLANE_CONSTANT_Y )
                            n_floor++;
                        else
                            n_wall++;
                    }
                    TORIRS_LOG(
                        "occluders: top=%d built_wall=%d built_floor=%d active=%d "
                        "eye=(%d,%d,%d) cam_tile=(%d,%d)\n",
                        top_level,
                        n_wall,
                        n_floor,
                        occ->active_count,
                        app->world_camera_pos.x,
                        app->world_camera_pos.y,
                        app->world_camera_pos.z,
                        occ->camera_sx,
                        occ->camera_sz);
                }
            }
        }
        else if( occ && occ_off )
        {
            occ->active_count = 0;
        }
    }

    /* Draw-order telemetry (TORIRS_WEDGELOG): hand the painter the eye and world
     * viewport it is about to paint with, for the log header. No-op otherwise. */
    PAINTER_DBG_WEDGE_SET_EYE(
        app->world_camera_pos.x,
        app->world_camera_pos.y,
        app->world_camera_pos.z,
        app->world_view_valid ? app->world_emit_desc.w : 0,
        app->world_view_valid ? app->world_emit_desc.h : 0);

    if( app->world_render_mode == TORIRS_WORLD_DEPTH )
        painter_collect_visible_depth(
            app->world->painter, app->painter_buffer, cam_sx, cam_sz, cam_slevel);
    /* TORIRS_PAINTER_W3D=1 runs the reference cascade (painter_paint_world3d)
     * in the live client instead of the distance-bucket drain. A draw-order
     * bug is either in the traversal or in the geometry it orders, and this is
     * what separates the two: same scene, same frame, the other painter. Pair
     * it with TORIRS_PIXOWNER to name what changed hands, or
     * TORIRS_PAINTER_ALT=1 + TORIRS_BMP_SERIES for a same-frame image pair. */
    else if(
        g_torirs_painter_force == 1 || (g_torirs_painter_force == 0 && torirs_env_painter_w3d()) )
        painter_paint_world3d(app->world->painter, app->painter_buffer, cam_sx, cam_sz, cam_slevel);
    else
        painter_paint_bucket(app->world->painter, app->painter_buffer, cam_sx, cam_sz, cam_slevel);

    if( app->world_render_mode != TORIRS_WORLD_DEPTH )
        app_wev_order_parent_ground(app);

    app->world_camera_pos.x = shake_x;
    app->world_camera_pos.y = shake_y;
    app->world_camera_pos.z = shake_z;
    app->world_camera.pitch = shake_pitch;
    app->world_camera.yaw = shake_yaw;

    /* TORIRS_PAINT_DEBUG: what the painter actually emitted this frame, by kind.
     * Scene elements existing is not the same as being painted — the bucket
     * flood-fill, the level mask and the cull map each drop work silently. */
    if( torirs_env_paint_debug() )
    {
        int by_kind[16] = { 0 };
        for( int i = 0; i < app->painter_buffer->command_count; i++ )
            by_kind[app->painter_buffer->commands[i]._bf_kind & 0xF]++;
        TORIRS_LOG(
            "paint: cam=%d,%d campos=(%d,%d,%d) pitch=%d yaw=%d level_mask=0x%x roof=%d "
            "commands=%d kinds:",
            cam_sx,
            cam_sz,
            (int)app->world_camera_pos.x,
            (int)app->world_camera_pos.y,
            (int)app->world_camera_pos.z,
            app->world_camera.pitch,
            app->world_camera.yaw,
            level_mask,
            app_world_roof_check(app),
            app->painter_buffer->command_count);
        for( int k = 0; k < 16; k++ )
            if( by_kind[k] )
                TORIRS_LOG(" %d:%d", k, by_kind[k]);
        TORIRS_LOG("\n");
    }

    /* TORIRS_TILETABLE=x0,x1,z0,z1: one row per (tile, cache level) joining
     * everything that decides when a ground mesh reaches the screen —
     *
     *   flags     the raw floor settings byte (VIS_BELOW 0x08 is the one that
     *             moves a mesh onto another level's pass)
     *   elem      the terrain element id, or -1 when the tile has no geometry
     *             at all; a -1 row can carry any flag and still draw nothing
     *   set       PaintersTile::terrain_levels, the meshes this level emits
     *   order     the command-buffer index the mesh landed at, or "-" when it
     *             was never emitted
     *
     * The point is the join: flags alone say what *should* happen, the emit
     * order alone says what did, and only the two side by side say whether a
     * tile that looks wrong on screen is mis-flagged, mis-ordered, or simply
     * has no mesh to move. Prints once, after the paint that produced it. */
    {
        static int done = 0;
        static int paints = 0;
        const char* env = torirs_env_tiletable();
        /* Wait for the paint the caller means. The table is only interesting
         * after a teleport into an instance, and printing on the first paint
         * silently describes wherever the player logged in — the same trap that
         * made TORIRS_HPROF report Lumbridge's relief for the arena. */
        const char* at = torirs_env_tiletable_at();
        int want_paint = at ? atoi(at) : 600;
        int x0, x1, z0, z1;
        paints++;
        if( env && !done && paints >= want_paint &&
            sscanf(env, "%d,%d,%d,%d", &x0, &x1, &z0, &z1) == 4 )
        {
            done = 1;
            TORIRS_LOG("tile     lvl flags elem  set order\n");
            for( int z = z0; z <= z1; z++ )
                for( int x = x0; x <= x1; x++ )
                    for( int lv = 0; lv < WORLD_MAP_TERRAIN_LEVELS; lv++ )
                    {
                        int order = -1;
                        /* Count, do not stop at the first: "the mesh is drawn
                         * once, from the level that owns it" is only provable
                         * by showing there is no second emission. A search that
                         * breaks on the first hit reports a double-draw and a
                         * single draw identically. */
                        int order_count = 0;
                        for( int i = 0; i < app->painter_buffer->command_count; i++ )
                        {
                            struct PaintersElementCommand* c = &app->painter_buffer->commands[i];
                            if( c->_bf_kind != PNTR_CMD_TERRAIN )
                                continue;
                            if( (int)c->_terrain._bf_terrain_x != x ||
                                (int)c->_terrain._bf_terrain_z != z ||
                                (int)c->_terrain._bf_terrain_y != lv )
                                continue;
                            if( order < 0 )
                                order = i;
                            order_count++;
                        }
                        TORIRS_LOG(
                            "%3d,%-3d  L%d  0x%02x %5d 0x%x ",
                            x,
                            z,
                            lv,
                            (unsigned)World_TileFlagGet(app->world, x, z, lv),
                            World_TerrainElementAt(app->world, x, z, lv),
                            painter_tile_get_terrain_levels(app->world->painter, x, z, lv));
                        if( order < 0 )
                            TORIRS_LOG("-\n");
                        else if( order_count > 1 )
                            TORIRS_LOG("%d  DRAWN %dx\n", order, order_count);
                        else
                            TORIRS_LOG("%d\n", order);
                    }
            /* The other half of the comparison. A terrain order is only
             * meaningful against the scenery it is supposed to be behind, and
             * both have to be read off the SAME buffer — the render-command
             * sequence TORIRS_DRAW_ORDER prints is a filtered renumbering, so
             * the two cannot be lined up across tools. */
            TORIRS_LOG("locs overlapping the rect (same numbering)\n");
            for( int i = 0; i < app->painter_buffer->command_count; i++ )
            {
                struct PaintersElementCommand* c = &app->painter_buffer->commands[i];
                struct WorldEntity_Scenery* sc;
                if( c->_bf_kind != PNTR_CMD_ELEMENT )
                    continue;
                sc = World_SceneryGetByElementId(app->world, painter_command_element_id(c));
                if( !sc )
                    continue;
                if( sc->grid_position.x < x0 - 8 || sc->grid_position.x > x1 + 8 ||
                    sc->grid_position.z < z0 - 8 || sc->grid_position.z > z1 + 8 )
                    continue;
                TORIRS_LOG(
                    "  order %5d loc=%-6d slot=%d,%d L%d size=%dx%d\n",
                    i,
                    sc->loc_id,
                    sc->grid_position.x,
                    sc->grid_position.z,
                    sc->grid_position.level,
                    sc->debug.draw_size_x,
                    sc->debug.draw_size_z);
            }
        }
    }
}

/* The world rectangle is retained from the last emit, but visibility is live.
 * A layout may suppress the viewport before the next emit refreshes that
 * rectangle, and a component-array index may have been reclaimed meanwhile.
 * Check both the tree's current world identity and effective display state
 * before an app-owned mouse gesture uses the retained box. */
static int
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
static int
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
    if( app->worldmap_drag.box_w > 0 && app->worldmap_drag.box_h > 0 && mouse_x >= app->worldmap_drag.box_x &&
        mouse_x < app->worldmap_drag.box_x + app->worldmap_drag.box_w && mouse_y >= app->worldmap_drag.box_y &&
        mouse_y < app->worldmap_drag.box_y + app->worldmap_drag.box_h && app_worldmap_surface_live(app) )
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
    if( app->tree && UITree_PointBlocksWorld(app->tree, &app->ui_host, mouse_x, mouse_y) )
        return 0;
    /* Clickable UI wins over the world; pass-through layers with hover scripts
     * do not. */
    if( app->tree && UITree_HitTestInteractive(app->tree, &app->ui_host, mouse_x, mouse_y) >= 0 )
        return 0;
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
static int
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
static int
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
            app->minimap_flag_x = dst_x;
            app->minimap_flag_z = dst_z;
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
        app->minimap_flag_x = route_x[0];
        app->minimap_flag_z = route_z[0];
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

    app->minimap_flag_x = route_x[0];
    app->minimap_flag_z = route_z[0];
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
static int
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
static int
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
static int
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
static void
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

/* Minimap click-to-walk (reference minimapLoop, Client.ts 2990-3032): map the
 * click through the same rotation the blit drew with into a player-relative
 * tile, then MOVE_MINIMAPCLICK with the 14-byte anticheat trailer. The sin/cos
 * tables are 16.16, so >>11 leaves fine units directly (32 fine units per
 * minimap pixel at 4 px/tile). Returns 1 when the click was consumed. */
static int
app_minimap_click(
    struct App* app,
    int mouse_x,
    int mouse_y,
    int ctrl_held)
{
    struct UITreeEmitDesc const* desc = &app->minimap_emit_desc;
    struct WorldEntity_Player* player;
    int center_x, center_y, yaw, rel_x, rel_y;
    int tile_x, tile_z;

    if( !app->minimap_view_valid || !app->world || !app->world->load_complete )
        return 0;
    /* Native permission, independent of whether the map is currently painted. */
    if( !(RS_MinimapPermissions(app->minimap_state) & RS_MINIMAP_WALK) )
        return 0;
    if( mouse_x < desc->x || mouse_x >= desc->x + desc->w || mouse_y < desc->y ||
        mouse_y >= desc->y + desc->h )
        return 0;
    player = app_local_player(app);
    if( !player )
        return 0;

    center_x = mouse_x - (desc->x + desc->w / 2);
    center_y = mouse_y - (desc->y + desc->h / 2);
    yaw = desc->rotation_r2pi2048 & 0x7ff;
    {
        int sin = ToriDraw_Sin(yaw);
        int cos = ToriDraw_Cos(yaw);
        rel_x = (center_y * sin + center_x * cos) >> 11;
        rel_y = (center_y * cos - center_x * sin) >> 11;
    }
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

/* Classify the raw hits the render pass collected into the app pickset +
 * hover tile. Runs after ToriRS_Soft3D_RenderFrame when the pick was armed. */
static void
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

static void
app_camera_move_forward(
    struct App* app,
    int amount)
{
    int direction_x = ToriDraw_Sin(app->world_camera.yaw);
    int direction_z = ToriDraw_Cos(app->world_camera.yaw);
    app->world_camera_pos.x -= (direction_x * amount) >> 16;
    app->world_camera_pos.z += (direction_z * amount) >> 16;
}

static void
app_camera_move_left(
    struct App* app,
    int amount)
{
    int direction_x = ToriDraw_Cos(app->world_camera.yaw);
    int direction_z = ToriDraw_Sin(app->world_camera.yaw);
    app->world_camera_pos.x += (direction_x * amount) >> 16;
    app->world_camera_pos.z += (direction_z * amount) >> 16;
}

/* Optional developer camera keys. All bindings come from `[debug:hotkeys]` and
 * are off when omitted. Arrow yaw/pitch remains ordinary game camera input. */
static void
app_world_camera_keys(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out)
{
    const int move = APP_CAMERA_MOVEMENT_SPEED;
    const int rotate = APP_CAMERA_ROTATION_SPEED;

    /* TORIRS_KEY_DEBUG: why a world/debug key did nothing. Every gate below
     * silently swallows the whole key set, and "the hotkey is broken" and
     * "the hotkey never ran" look identical from outside. Keyed off the raw
     * key state rather than key_event_count — that counter only fills when a
     * component carries an onKey hook, so it is 0 for exactly the debug keys
     * this is meant to explain. */
    if( torirs_env_key_debug() &&
        (app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_TOGGLE) ||
         app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_MORE) ||
         app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_LESS) ||
         app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_MORE_100) ||
         app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_LESS_100)) )
        TORIRS_LOG(
            "camera_keys: paint-cap key seen; world_active=%d view_valid=%d "
            "chat=%d/%d/%d iface_input=%d\n",
            app->world_active,
            app->world_view_valid,
            app->chat_input_active,
            app->chat.social_input_open,
            app->chat.dialog_input_open,
            app_iface_text_input_focused(app));

    /* No key_target gating: the reference broadcasts every key to onKey
     * scripts AND moves the camera in the same frame; there is no focused
     * text-input concept to defer to yet. The viewport still has to be on
     * screen — with no world drawn these keys belong to the interface. */
    if( !app->world_active || !app_world_viewport_component_live(app) )
    {
        /* Do not leave held arrow ownership feeding the follow-camera tick
         * after its viewport became display:none. */
        app->cam_key_left = 0;
        app->cam_key_right = 0;
        app->cam_key_up = 0;
        app->cam_key_down = 0;
        return;
    }
    /* Suppressed while any text input has focus (the chat line, a modal
     * prompt, a panel's search box), so typing never flies the camera -- and
     * while the catalog's model view holds focus, whose WASD/EF orbit the
     * preview, not the world. */
    if( app_text_input_focused(app) || app_modelview_focused(app) )
        return;
    (void)out;

    if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_FORWARD) )
        app_camera_move_forward(app, move);
    if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_BACK) )
        app_camera_move_forward(app, -move);
    if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_LEFT) )
        app_camera_move_left(app, -move);
    if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_RIGHT) )
        app_camera_move_left(app, move);
    if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_UP) )
        app->world_camera_pos.y -= move;
    if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_DOWN) )
        app->world_camera_pos.y += move;
    /* Arrows drive the orbit camera (reference keyHeld[1..4]); the follow
     * step consumes these next frame. When the follow cam is off (offline or
     * scripted) fall back to the free-cam direct rotate. */
    if( app->revconfig_profile.camera.controls & REVCONFIG_CAMERA_CONTROL_ARROW_KEYS )
    {
        app->cam_key_left = LibToriRS_Input_IsKeyHeld(input, TORIRSK_LEFT);
        app->cam_key_right = LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT);
        app->cam_key_up = LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP);
        app->cam_key_down = LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN);
    }
    else
    {
        /* Cleared, not merely left alone: the follow step reads these every
         * cycle and a latch held from before the profile said no would keep
         * accelerating the yaw for as long as the key stayed down. */
        app->cam_key_left = 0;
        app->cam_key_right = 0;
        app->cam_key_up = 0;
        app->cam_key_down = 0;
    }
    if( app->cam_script.scripted || !app->net || app->camera_unlocked )
    {
        if( app->cam_key_left )
            app->world_camera.yaw = ToriDraw_AddAngle(app->world_camera.yaw, rotate);
        if( app->cam_key_right )
            app->world_camera.yaw = ToriDraw_AddAngle(app->world_camera.yaw, -rotate);
        if( app->cam_key_up )
            app->world_camera.pitch = ToriDraw_AddAngle(app->world_camera.pitch, rotate);
        if( app->cam_key_down )
            app->world_camera.pitch = ToriDraw_AddAngle(app->world_camera.pitch, -rotate);
    }

    /* Unlock / relock the camera. Unlocked, the follow update stands down
     * (app_world_camera_follow) and the configured movement bindings fly the
     * eye while arrows rotate it — the debug flight that only worked offline,
     * available online.
     * Relocking snaps back through the follow's own teleport path. */
    if( app_debug_key_down(app, input, APP_DEBUG_HOTKEY_CAMERA_UNLOCK) )
    {
        app->camera_unlocked = !app->camera_unlocked;
        TORIRS_LOG("camera: %s\n", app->camera_unlocked ? "UNLOCKED" : "locked");
        app->need_redraw = 1;
    }

    /* Reload the world through the task system (assets cached -> fast;
     * rebuild clears world scene elements incl. spawned entities). */
    if( app_debug_key_down(app, input, APP_DEBUG_HOTKEY_WORLD_RELOAD) &&
        App_WorldNodeIndex(app) >= 0 )
        app_world_load_begin(app, NULL, 0);

    /* Painter-command stepping, the v0 client's debug (docs/ORANGE_WEDGE.md):
     * I toggles the cap (unlimited <-> 0), J/K step it +-1, L/, +-100.  The
     * raster then draws exactly the first N painter commands, which is how a
     * draw-order artefact is walked to the command that paints it.
     *
     * The steppers repeat while HELD, like the W/A/S/D camera keys and unlike
     * the one-shot debug keys above: a scene is ~1700 commands, so finding the
     * one that paints a pixel by tapping J is not a thing anyone will do. Only
     * the toggle is edge-triggered — held, it would flip every frame. */
    {
        int limit = ToriRS_Frame_PaintLimitGet();
        int next = limit;
        int stepping = 0;
        int toggled = 0;
        /* One log line per gesture, not per frame: while a stepper is held the
         * value changes every frame, and a hundred lines a second buries the
         * number you are trying to read. Printed when the keys settle.
         * Seeded with the starting cap so an untouched client says nothing. */
        static int logged = INT_MIN;
        if( logged == INT_MIN )
            logged = limit;

        if( app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_TOGGLE) )
        {
            next = limit < 0 ? 0 : -1;
            toggled = 1;
        }
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_PAINT_MORE) )
        {
            next = (next < 0 ? 0 : next) + 1;
            stepping = 1;
        }
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_PAINT_LESS) )
        {
            next = (next < 0 ? 0 : next) - 1;
            stepping = 1;
        }
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_PAINT_MORE_100) )
        {
            next = (next < 0 ? 0 : next) + 100;
            stepping = 1;
        }
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_PAINT_LESS_100) )
        {
            next = (next < 0 ? 0 : next) - 100;
            stepping = 1;
        }

        if( next != limit )
        {
            if( next < -1 )
                next = -1;
            ToriRS_Frame_PaintLimitSet(next);
            app->need_redraw = 1;
        }
        if( (toggled || !stepping) && ToriRS_Frame_PaintLimitGet() != logged )
        {
            logged = ToriRS_Frame_PaintLimitGet();
            TORIRS_LOG("paintlimit: %d\n", logged);
        }
    }
}

/*
 * Configured hotkeys: revconfig binds a key to one chrome node + effect, and
 * the effects themselves are hard-coded here (enum UITreeHotkeyEffect).
 *
 * Dispatch lives in the app rather than in uitree_interact because the
 * suppression rule does: a key that reaches a hotkey must not also be a
 * character being typed, and only the app knows which of the chat, social, and
 * dialog input lines has focus. Keys are read off osrs_key_pressed — the same
 * edge array CS2's KEYPRESSED reads — which, unlike enum LibToriRS_KeyCode,
 * covers the F-keys.
 *
 * Returns nothing, but marks each key it acted on in app->hotkey_consumed so a
 * bound key does not also fire a debug world hotkey on the same press.
 */
static void
app_ui_hotkeys(
    struct App* app,
    struct LibToriRS_Input* input)
{
    memset(app->hotkey_consumed, 0, sizeof(app->hotkey_consumed));

    /* Escape, ahead of every gate below: it must fire with the chat line
     * focused (the line is focused by default) and with no revconfig bindings
     * loaded (cache chrome has none). Releases chat focus and asks the server
     * to close whatever modal is up — the same CLOSE_MODAL the gameframe X's
     * clientscript (29, if_close) raises, so what closes stays the server's
     * decision and an idle Escape is a no-op there. Read off osrs_key_pressed
     * rather than the key_events queue so CS2 KEYPRESSED-style injection (and
     * TORIRS_SIM_HOTKEY) reach it too; a real press feeds both, and the two
     * writes to the same request flag collapse into one packet. */
    if( input->osrs_key_pressed[TORIRS_OSRSKEY_ESCAPE] )
    {
        app->chat_input_active = 0;
        app->host.close_modal_requested = true;
        app->need_redraw = 1;
    }

    if( !app->tree || app->tree->hotkey_count <= 0 )
        return;
    /* Typing wins over every binding — otherwise "f" in a chat line, or a digit
     * in a bank amount, silently switches tabs behind the caret. */
    if( app_text_input_focused(app) )
        return;
    /* An open right-click menu owns the pointer; let it own the keyboard too,
     * matching how interact_minimenu swallows everything until it closes. */
    if( app->interact.minimenu.visible )
        return;

    for( int i = 0; i < app->tree->hotkey_count; i++ )
    {
        struct UITreeHotkey const* binding = &app->tree->hotkeys[i];
        struct UITreeComponent const* node;

        if( binding->osrs_key < 0 || binding->osrs_key >= TORIRS_OSRSKEY_COUNT )
            continue;
        if( !input->osrs_key_pressed[binding->osrs_key] )
            continue;
        if( binding->node_index < 0 || (uint32_t)binding->node_index >= app->tree->component_count )
            continue;

        node = &app->tree->components[binding->node_index];
        if( node->freed || UITree_NodeOrAncestorDisplayHidden(app->tree, binding->node_index) )
            continue;

        switch( binding->effect )
        {
        case UITREE_HOTKEY_EFFECT_SELECT_TAB:
        {
            /* Same path a click on the tab takes (interact_click's chrome
             * gestures), enabled-gate included: a tab with no interface
             * mounted is not selectable by key any more than by mouse. */
            int tabno = -1;
            if( node->type == UIELEM_BUILTIN_TAB_ICONS )
                tabno = node->u.tab_icon.tabno;
            else if( node->type == UIELEM_BUILTIN_REDSTONE_TAB )
                tabno = node->u.redstone_tab.tabno;
            else if( node->type == UIELEM_BUILTIN_SIDEBAR )
                tabno = node->u.sidebar.tabno;
            if( tabno < 0 )
                break;
            {
                struct UITreeHostRequest enabled_req = {
                    .kind = UITREE_HOST_GET_TAB_ENABLED,
                    .u.tab_enabled.tabno = tabno,
                };
                if( !UITree_Host(&app->ui_host, &enabled_req) )
                    break;
            }
            {
                struct UITreeHostRequest set_req = {
                    .kind = UITREE_HOST_SET_SELECTED_TAB,
                    .u.set_selected_tab.tabno = tabno,
                };
                UITree_Host(&app->ui_host, &set_req);
            }
            app->hotkey_consumed[binding->osrs_key] = 1;
            app->need_redraw = 1;
            if( getenv("TORIRS_HOTKEY_DEBUG") )
                TORIRS_LOG(
                    "hotkey: osrs_key=%d node=%d effect=select_tab tab=%d\n",
                    binding->osrs_key,
                    binding->node_index,
                    tabno);
            break;
        }
        default:
            break;
        }
    }
}

/*
 * Does the follow camera zoom right now? Both halves have to say yes.
 *
 * `wheel` is the SWITCH -- the settings page's "Zoom" row. Pinned, the eye
 * stays at `[camera] rest=` with nothing the player does moving it, which is
 * the 2004 camera. `zoom_closest < zoom_furthest` is the ROOM: a profile may
 * state both ends the same and leave the wheel nowhere to go even when the
 * switch is on.
 *
 * Reading only the band was what made the "Zoom" row a no-op on the two
 * behaviours it appeared to name -- it moved no wheel and only changed the
 * projection, which is the one thing it should never have touched.
 */
static int
app_world_camera_zooms(struct App const* app)
{
    assert(app);
    if( app->revconfig_profile.camera.wheel != REVCONFIG_CAMERA_WHEEL_LIVE )
        return 0;
    return app->revconfig_profile.camera.zoom_closest < app->revconfig_profile.camera.zoom_furthest;
}

/* TORIRS_CAM_DEBUG=1: one line whenever a mouse gesture moves the camera.
 * Prints whichever camera is live, since the follow cam and the free cam keep
 * their angles in different fields. */
static void
app_debug_log_camera(
    struct App* app,
    char const* what,
    int follow_cam)
{
    if( !getenv("TORIRS_CAM_DEBUG") )
        return;
    TORIRS_LOG(
        "cam_%s: %s yaw=%d pitch=%d height=%d eye=%d,%d,%d\n",
        what,
        follow_cam ? "orbit" : "free",
        follow_cam ? app->orbit_yaw : app->world_camera.yaw,
        follow_cam ? app->orbit_pitch : app->world_camera.pitch,
        app->world_cam_zoom,
        app->world_camera_pos.x,
        app->world_camera_pos.y,
        app->world_camera_pos.z);
}

/* Middle-button rotate and wheel zoom over the world viewport. Both gestures
 * are properties of the REVISION (revconfig `[camera] controls=` and the
 * band), not of the viewport widget: a camera that cannot zoom cannot zoom over any
 * viewport. The emit desc is still what app_world_mouse_gate reads to decide
 * the pointer is on the scene rather than on the interface.
 *
 * Neither gesture exists in the reference client, so there is nothing to match.
 * The rotate uses the drag-the-camera convention every OSRS client with a
 * middle-button camera uses: the view turns the way the pointer moves, so the
 * scene slides the OPPOSITE way. In projection terms (app_world_project) screen
 * x grows with camera yaw and the scene rises as pitch grows, hence yaw -= dx
 * and pitch += dy. Both cameras get the identical screen-space rule; matching
 * each one's arrow keys instead is not an option, since the free cam's arrows
 * and the orbit cam's turn the camera in opposite directions. */
static void
app_world_camera_mouse(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out)
{
    int mouse_x = input->curr.mouse_x;
    int mouse_y = input->curr.mouse_y;
    int follow_cam;

    if( !app->world_active || !app_world_viewport_component_live(app) )
    {
        app->cam_mmb_active = 0;
        return;
    }
    /* The same split app_world_camera_keys makes: online and out of a cutscene
     * the orbit follow cam owns the angles, otherwise the free camera does. */
    follow_cam = app->net && !app->cam_script.scripted;

    /* `controls=` is the revision's answer for a MOUSE; app->touch_camera is
     * the platform's answer for a FINGER, and the two are different questions.
     * @see App.touch_camera. */
    if( (app->revconfig_profile.camera.controls & REVCONFIG_CAMERA_CONTROL_MMB) ||
        app->touch_camera )
    {
        /* Only the press has to land on the scene; once latched the drag keeps
         * the pointer until release, so sweeping over the sidebar mid-rotate
         * does not stall the camera. */
        if( !app->cam_mmb_active && input->curr.mouse_button_down[TORIRSM_MIDDLE] &&
            !app->interact.minimenu.visible && app_world_mouse_gate(app, mouse_x, mouse_y) )
        {
            app->cam_mmb_active = 1;
            app->cam_mmb_x = mouse_x;
            app->cam_mmb_y = mouse_y;
        }

        if( app->cam_mmb_active )
        {
            int dx = mouse_x - app->cam_mmb_x;
            int dy = mouse_y - app->cam_mmb_y;

            app->cam_mmb_x = mouse_x;
            app->cam_mmb_y = mouse_y;

            if( dx != 0 || dy != 0 )
            {
                if( follow_cam )
                {
                    /* The key path eases through a velocity; a drag is already
                     * a position delta, so it writes the angle and zeroes the
                     * velocity rather than fighting the decay next frame. */
                    app->orbit_yaw = (app->orbit_yaw - dx * APP_WORLD_MMB_YAW_PER_PX) & 0x7ff;
                    app->orbit_pitch = app_world_clamp_pitch(
                        app, app->orbit_pitch + dy * APP_WORLD_MMB_PITCH_PER_PX);
                    app->orbit_yaw_vel = 0;
                    app->orbit_pitch_vel = 0;
                }
                else
                {
                    app->world_camera.yaw =
                        ToriDraw_AddAngle(app->world_camera.yaw, -dx * APP_WORLD_MMB_YAW_PER_PX);
                    app->world_camera.pitch =
                        ToriDraw_AddAngle(app->world_camera.pitch, dy * APP_WORLD_MMB_PITCH_PER_PX);
                }
                app_debug_log_camera(app, "rotate", follow_cam);
                app->need_redraw = 1;
            }
        }

        if( !LibToriRS_Input_IsMouseHeld(input, TORIRSM_MIDDLE) ||
            input->curr.mouse_button_up[TORIRSM_MIDDLE] )
            app->cam_mmb_active = 0;
    }
    else
        app->cam_mmb_active = 0;

    /* Wheel up (positive) zooms in. Gated on the pointer being over the scene
     * and on no widget having already taken this notch, so a wheel over a
     * scroll pane drawn across the viewport still belongs to that pane —
     * app_world_mouse_gate alone only rejects *interactive* nodes, and an IF1
     * scroll layer is pass-through. */
    if( input->curr.mouse_wheel_y != 0 && !out->wheel_consumed && !app->interact.minimenu.visible &&
        /* The chrome's claim is checked HERE, not inferred from consumed
         * flags: this runs long after the overlay handled input, when
         * input_frame_consumed is 1 on every frame. A wheel over a panel or
         * an open dropdown belongs to the chrome even when the chrome had
         * nothing to do with it -- consumed into nothing beats zooming the
         * world behind a panel. */
        !app_chrome_wants_pointer(app, mouse_x, mouse_y) &&
        app_world_mouse_gate(app, mouse_x, mouse_y) )
    {
        /* Only the FOLLOW camera is the revision's. The free camera is this
         * client's own debug flight -- the one U unlocks and W/A/S/D drives --
         * so a revision that does not zoom does not take its dolly away, or an
         * offline rev-254 boot (which has no follow camera at all) would lose
         * the only way it has of moving in or out. */
        if( !follow_cam )
        {
            app_camera_move_forward(app, input->curr.mouse_wheel_y * APP_WORLD_ZOOM_FREECAM_STEP);
        }
        else if( app_world_camera_zooms(app) )
        {
            app->world_cam_zoom = RevConfigProfile_CameraClampZoom(
                &app->revconfig_profile,
                app->world_cam_zoom -
                    input->curr.mouse_wheel_y * app->revconfig_profile.camera.wheel_step);
        }
        else
            return;
        app_debug_log_camera(app, "zoom", follow_cam);
        app->need_redraw = 1;
    }
}

/* Classic human animation set (players; INTERFACE_PLAYER_IDLE_SEQ parity). */
enum
{
    APP_PLAYER_SEQ_READY = 808,
    APP_PLAYER_SEQ_WALK = 819,
    APP_PLAYER_SEQ_WALK_B = 820,
    APP_PLAYER_SEQ_WALK_L = 821,
    APP_PLAYER_SEQ_WALK_R = 822,
    APP_PLAYER_SEQ_TURN = 823,
    APP_PLAYER_SEQ_RUN = 824,
};

/* Wrap a freshly built (owned) model in a new dynamic scene element. The
 * element owns the model from here (SceneElementRemove frees it), which is
 * why spawns copy registry models instead of sharing handles.
 *
 * The pool is the ROOT view's dynamic half, which is right for as long as
 * every entity lives in app->world. When entities start being owned by a boat
 * view they must be allocated in THAT view's dynamic pool
 * (TORIDRAW_SCENE_POOL_DYNAMIC_VIEW / WorldBuilder.dynamic_pool) instead: the
 * reconcile pass sweeps a view's own pool against its own entity list, so a
 * deck entity holding a root-pool element would be swept by the mainland's
 * next rebuild, which does not know its owner. */
static int
app_world_scene_element_create(
    struct App* app,
    enum ToriDraw_ElementKind kind,
    struct ToriDraw_Model* model,
    int world_x,
    int world_y,
    int world_z)
{
    struct ToriDraw_ModelHandle hnd;
    /* Tagged here, where the caller still knows what it is making. Every
     * reader downstream would otherwise have to ask the world, and the
     * pick classifier asked by trying all four pools in turn. */
    int element_id = ElementId_Raw(ElementId_Make(
        kind, ToriDraw_SceneElementAddPool(app->scene, TORIDRAW_SCENE_POOL_DYNAMIC)));

    if( element_id < 0 )
    {
        ToriDraw_ModelFree(model);
        return -1;
    }
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;
    ToriDraw_SceneElementSetModel(app->scene, element_id, hnd);
    ToriDraw_SceneElementSetPosition(app->scene, element_id, world_x, world_y, world_z, 0);
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
        {
            el->dynamic = true;
            /*
             * Entities pick per-face, like locs do.
             *
             * The reference sets Model.useAABBMouseCheck on exactly these
             * (ObjType.getWorldModel:359, ClientPlayer:321/395, NpcType:227),
             * and this followed it — but a screen-space box around a large
             * model is enormously bigger than the model. TzKal-Zuk is the case
             * that made it untenable: his box swallows most of the arena, so
             * clicking the floor near him hits him instead. The box is also
             * built over *every* projected vertex, including geometry that is
             * never drawn, which inflates it further.
             *
             * ToriDraw_ProjectedModelMouseHitTest still uses the AABB as its
             * cheap reject before walking faces, so this costs a triangle scan
             * only on models the cursor is actually over.
             */
            el->pick_aabb = false;
        }
    }
    return element_id;
}

/* Advance a newly-bound packet animation over the client cycles its async load
 * consumed. The reference constructs a DynamicObject at LOC_ANIM receipt, so
 * loading is synchronous from its clock's point of view; beginning at frame 0
 * when our task finishes makes two sequences from one enclosed zone update
 * start at different times.
 *
 * Use the same counters as app_world_tick_animations rather than converting a
 * cycle count to a frame by division: frame lengths vary, and frameStep=1 holds
 * these Inferno locs on their terminal frame. DynamicObject caps catch-up at
 * 100 cycles for a looping sequence, which also bounds this loop after a stall. */
static void
app_world_catch_up_object_seq(
    struct App* app,
    int element_id,
    struct ToriDraw_Animation* anim,
    int elapsed_cycles)
{
    struct ToriDraw_SceneElement* element;

    if( elapsed_cycles <= 0 )
        return;
    assert(anim);
    element = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !element )
        return;
    /* A sequence that cannot terminate needs the same 100-cycle bound a looping
     * DynamicObject gets, or a long load stall is paid back one cycle at a time
     * here. anim_loop never terminates by construction. */
    if( (anim->frame_step > 0 || element->anim_loop) && elapsed_cycles > 100 )
        elapsed_cycles = 100;

    for( int cycle = 0; cycle < elapsed_cycles && element->anim_seq_id != -1; cycle++ )
    {
        if( element->is_skeletal )
        {
            int play_frames = element->skeletal_play_frames;
            if( play_frames <= 0 )
                play_frames = anim->frame_count;
            element->anim_cycle++;
            if( element->anim_cycle >= 1 )
            {
                if( anim->frame_count == play_frames )
                {
                    if( !ToriDraw_AnimationAdvanceObjectFrame(anim, &element->anim_frame) )
                        ToriDraw_SceneElementSetAnimation(app->scene, element_id, NULL, true);
                }
                else
                    element->anim_frame = (element->anim_frame + 1) % play_frames;
                element->anim_cycle = 0;
            }
        }
        else if( anim->frames && anim->frame_count > 0 )
        {
            if( element->anim_loop )
                ToriDraw_AnimationAdvanceLoopCycles(
                    anim, &element->anim_frame, &element->anim_cycle, 1);
            else if( !ToriDraw_AnimationAdvanceObjectCycles(
                         anim, &element->anim_frame, &element->anim_cycle, 1) )
                ToriDraw_SceneElementSetAnimation(app->scene, element_id, NULL, true);
        }
    }
}

/* Try binding a loaded scene animation onto an element. Returns 1 when bound
 * OR permanently unbindable (failed/empty sentinel), 0 while still loading. */
static int
app_world_try_bind_seq(
    struct App* app,
    int element_id,
    int seq_id,
    int start_cycle)
{
    struct ToriDraw_Animation* anim;

    if( !ToriDraw_SceneAnimationHas(app->scene, seq_id) )
        return 0;
    /* Bind the resolved animation onto the element — the tick loop and
     * frame emitter read element->animation, which SetAnimationSeq alone
     * leaves NULL. Skip the empty sentinel (failed seqs). */
    anim = ToriDraw_SceneAnimationGet(app->scene, seq_id);
    if( ToriDraw_ElementAnimPlayable(anim) )
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        ToriDraw_SceneElementSetAnimationSeq(app->scene, element_id, seq_id);
        ToriDraw_SceneElementSetAnimation(app->scene, element_id, anim, true);
        if( el )
            ToriDraw_ElementSetAnim(el, anim);
        if( app->world )
            app_world_catch_up_object_seq(app, element_id, anim, app->world->cycle - start_cycle);
        if( getenv("TORIRS_ANIM_DEBUG") )
        {
            /* The rig, not just the binding. A seq binds to any element, but
             * types 0-3 need `vertex_bones` and type 5 needs `face_bones`; a
             * model carrying neither discards every op and stands perfectly
             * still while its neighbours animate. That failure is invisible
             * here without these three numbers — it looks exactly like a
             * sequence that was never sent. */
            struct ToriDraw_Model const* m =
                (el && ToriDraw_ModelKindIsFull(el->model.kind)) ? el->model.u.model.model : NULL;
            TORIRS_LOG(
                "seq_bind: element=%d seq=%d frames=%d skeletal=%d start=%d now=%d "
                "frame=%d cycle=%d kind=%d vbones=%d fbones=%d falpha=%d\n",
                element_id,
                seq_id,
                anim->frame_count,
                anim->skeletal ? 1 : 0,
                start_cycle,
                app->world ? app->world->cycle : start_cycle,
                el ? el->anim_frame : -1,
                el ? el->anim_cycle : -1,
                el ? (int)el->model.kind : -1,
                m && m->vertex_bones ? m->vertex_bones->bones_count : -1,
                m && m->face_bones ? m->face_bones->bones_count : -1,
                m && m->face_alphas ? 1 : 0);
        }
    }
    else if( getenv("TORIRS_ANIM_DEBUG") )
        TORIRS_LOG(
            "seq_bind: element=%d seq=%d UNBINDABLE (anim=%p frames=%d)\n",
            element_id,
            seq_id,
            (void*)anim,
            anim ? anim->frame_count : -1);
    return 1;
}

/* Queue a sequence load (no-op when cached) and attach it to the element —
 * immediately when already resident, else via the per-frame bind poll. */
static void
app_world_apply_seq(
    struct App* app,
    int element_id,
    int seq_id)
{
    struct ToriRS_Task* task;
    int const start_cycle = app->world ? app->world->cycle : 0;

    if( seq_id < 0 )
        return;
    task = CreateTask_SequenceLoad(app->provider, app->scene, seq_id);
    if( task )
        ToriRS_TaskQueue_Add(app->runner.queue, task);

    if( app_world_try_bind_seq(app, element_id, seq_id, start_cycle) )
        return;
    (void)AsyncPendingSeqBinds_Add(&app->seq_bind_pending, element_id, seq_id, start_cycle);
}

/*
 * Forget deferred binds for an element that is going away.
 *
 * Necessary because the poll below can only ask whether the element is LIVE,
 * and scene element ids are recycled: an entry left behind by a despawned
 * entity is indistinguishable from a valid one once the id is handed out
 * again, and its sequence then binds onto whoever inherited it -- the wrong
 * creature suddenly playing somebody else's animation. Dropping at the moment
 * of death removes the ambiguity instead of trying to detect it later. Same
 * class of bug as AppEntitySpotanim::owner_entity_id; see that note.
 */
static void
app_seq_bind_pending_drop(
    struct App* app,
    int element_id)
{
    AsyncPendingSeqBinds_DropElement(&app->seq_bind_pending, element_id);
}

/* Per-frame: bind deferred element/sequence pairs whose loads landed. */
static void
app_world_bind_pending_seqs(struct App* app)
{
    int kept = 0;
    for( int i = 0; i < app->seq_bind_pending.count; i++ )
    {
        struct AsyncPendingSeqBind* pend = &app->seq_bind_pending.items[i];
        if( !ToriDraw_SceneElementIsLive(app->scene, pend->element_id) )
            continue; /* element despawned while loading */
        if( app_world_try_bind_seq(app, pend->element_id, pend->seq_id, pend->start_cycle) )
        {
            app->need_redraw = 1;
            continue;
        }
        app->seq_bind_pending.items[kept++] = *pend;
    }
    AsyncPendingSeqBinds_Keep(&app->seq_bind_pending, kept);
}

/* Config-driven color/texture swaps for a built model (npc/loc style). NULL
 * where the caller has none. */
struct AppModelRecolorSpec
{
    const int* recolors_from;
    const int* recolors_to;
    int recolor_count;
    const int* retextures_from;
    const int* retextures_to;
    int retexture_count;
};

/* Convert + merge + recolor + scale + light one drawable model from cache model ids.
 * SYNCHRONOUS: the models must already be resident (callers await
 * CreateTask_ModelLoad first — the spawn tasks do). Returns an owned model
 * or NULL when any part is missing.
 *
 * scale_xz/scale_y are 128 == 1.0 (pass 128,128 for none). Applied before the
 * rest-pose capture, because animation frames reset vertices from the capture —
 * a scale applied after it would vanish on the first animated tick. The
 * reference re-scales the animated copy every frame instead (NpcModelLoader);
 * scaling the base is equivalent for rotation frames and off by the scale
 * factor only on a frame's translate deltas, which nothing visible exercises.
 *
 * light_actor selects the actor regime (players/NPCs/spotanims/projectiles) vs
 * the scene regime (ground objs). light_ambient/contrast are signed config
 * offsets added onto the regime base (0,0 for Client-TS NPC bodies). */
enum
{
    APP_LIGHT_SCENE = 0,
    APP_LIGHT_ACTOR = 1,
};

/**
 * Which npcs were imported from a z-buffered client.
 *
 * What that then MEANS for the render is
 * app_model_apply_import_render_flags' answer, not this one's: the face
 * priorities always go, and the depth-tested kernels are separately switchable
 * (see app_model_zbuffer_kernels_enabled). This function only identifies the
 * models; it does not decide how they are resolved.
 *
 * The content says so, per npc, with the `zbuffer_model` param -- see
 * OSRS-Content/.../minigame_rs2012_qbd/configs/rs2012_qbd.param. The client
 * reads it off the npc type it already has (ToriRS_Npctype::zbuffer_model), so
 * nothing here has to know which npcs those are: adding one is a content edit
 * and a repack, not a client change.
 *
 * TORIRS_ZBUFFER_NPCS overrides the content entirely -- a comma list of npc ids
 * to treat as imported instead, or the empty string for nobody. That is the A/B
 * knob: it takes the decision away from the config without editing it.
 *
 * Per npc rather than globally because that is the unit of the question. The
 * goblin standing next to the dragon was authored for a painter's sort and
 * paying a depth test for it buys nothing.
 *
 * A model carrying TORIDRAW_MODEL_FLAG_ZBUFFER also has its face priorities
 * dropped by the sort -- see the flag's own comment. The two cannot both decide
 * a pixel, and a priority would win.
 */
static bool
app_npc_wants_zbuffer(
    int npc_id,
    struct ToriRS_Npctype const* npctype)
{
    char const* list = getenv("TORIRS_ZBUFFER_NPCS");
    if( !list )
        return npctype && npctype->zbuffer_model != 0;
    return ToriRS_EnvIdListHas(list, npc_id);
}

/**
 * Are the depth-tested kernels switched on for the models that ask for them?
 *
 * On by default: an imported model's parts genuinely interpenetrate and the
 * depth test is the only thing that resolves them correctly.
 *
 * TORIRS_MODEL_ZBUFFER=0 leaves those models the OTHER half of the opt-in --
 * their face priorities still dropped (TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY),
 * which is right for them either way, but resolved by the painter's sort. That
 * is the A/B knob for the reported QBD symptom, where faces on the Queen and
 * the other rs2012 npcs blink out at some camera angles: a per-pixel reject
 * looks nothing like a bad sort, so splitting the two halves says which one is
 * doing it without editing content or reverting code.
 */
static bool
app_model_zbuffer_kernels_enabled(void)
{
    char const* off = getenv("TORIRS_MODEL_ZBUFFER");
    return !(off && *off && *off == '0');
}

/**
 * Stamp the render policy for a model imported from a z-buffered client.
 *
 * `imported` is the content's `zbuffer_model` answer (app_npc_wants_zbuffer).
 * Written both ways because the model may be a cache copy of one that was
 * stamped under a different npc id.
 */
static void
app_model_apply_import_render_flags(
    struct ToriDraw_Model* model,
    bool imported)
{
    uint8_t const both =
        (uint8_t)(TORIDRAW_MODEL_FLAG_ZBUFFER | TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY);
    assert(model);
    model->flags &= (uint8_t)~both;
    if( !imported )
        return;
    model->flags |= TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY;
    if( app_model_zbuffer_kernels_enabled() )
        model->flags |= TORIDRAW_MODEL_FLAG_ZBUFFER;
}

static struct ToriDraw_Model*
app_world_build_model(
    struct App* app,
    const int* model_ids,
    int count,
    const struct AppModelRecolorSpec* recolors,
    int scale_xz,
    int scale_y,
    int light_actor,
    int light_contrast,
    int light_ambient)
{
    struct ToriDraw_Model* parts[16];
    struct ToriDraw_Model* model = NULL;
    int part_count = 0;

    for( int i = 0; i < count && part_count < 16; i++ )
    {
        struct ToriRS_Model* rs = CacheProvider_ModelGet(app->provider, model_ids[i]);
        struct ToriDraw_Model* part = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
        if( part )
            parts[part_count++] = part;
    }
    if( part_count == 0 )
        return NULL;

    if( part_count > 1 )
    {
        model = ToriDraw_ModelMerge(parts, part_count);
        for( int i = 0; i < part_count; i++ )
            ToriDraw_ModelFree(parts[i]);
    }
    else
        model = parts[0];
    if( !model )
        return NULL;

    /* Recolor before lighting: lighting bakes face colors into per-vertex
     * shaded colors, so a swap afterwards would be a no-op (same order as
     * scenery apply_transforms and the obj icon path in the scene bridge). */
    if( recolors )
    {
        for( int i = 0; i < recolors->recolor_count; i++ )
            ToriDraw_ModelRecolor(model, recolors->recolors_from[i], recolors->recolors_to[i]);
        for( int i = 0; i < recolors->retexture_count; i++ )
            ToriDraw_ModelRetexture(
                model, recolors->retextures_from[i], recolors->retextures_to[i]);
        /* Swapped-in ids are new to the loader's registry — see
         * ToriDraw_ModelNoteTextureWants. */
        if( recolors->retexture_count > 0 )
            ToriDraw_ModelNoteTextureWants(model);
    }

    /*
     * Recorded, not applied: the reference resizes the model AFTER animating it
     * (NpcType.getModel), and the animation is applied to the bind pose this
     * function is about to capture. Applying it here instead put every
     * keyframe's translations and ORIGIN pivots -- authored at full size --
     * against a shrunken model, which is what threw Xarpus (resizeh/resizev 64)
     * into the air above his arena. ToriDraw_ModelApplyPostTransforms below puts
     * this instance into render scale for the un-animated case; every pose
     * re-applies it.
     *
     * Lighting therefore also runs at the authored size, which is where the
     * reference lights its cached base too.
     */
    ToriDraw_ModelSetPostResize(model, scale_xz, scale_xz, scale_y);

    /* HD-only textures off before lighting — ModelData.light()'s isSd gate.
     * Without this, every face whose material is HD-only keeps a texture id,
     * lighting stores 0–127 lightness (not HSL16), and the software raster
     * skips the face when the texture map has no entry. Steel titan (30469)
     * is entirely HD-textured (materials 238/288/241, all valid=0). */
    ToriDraw_ModelDropNonSdTextures(app->provider, model);
    ToriDraw_ModelNoteTextureWants(model);

    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        if( light_actor )
            ToriDraw_LightModelActor(hnd, light_contrast, light_ambient);
        else
            ToriDraw_LightModelScene(hnd, light_contrast, light_ambient);
    }
    /* Capture first: the bind pose is the authored-size model. */
    ToriDraw_ModelCaptureOriginalVertices(model);
    ToriDraw_ModelApplyPostTransforms(model);
    ToriDraw_ModelSetBoundsCylinder(model);
    return model;
}

/*
 * The lit/transformed base for an npc type, cached like the spotanim path
 * (Client-TS NpcType model cache, 30 entries).
 *
 * Every input app_world_build_model consumes here -- part models, recolours,
 * retextures, the two scales, ambient/contrast -- is read off the npctype, and
 * app->npc_light_uses_type_ambient_contrast is resolved once during feature
 * setup and never rewritten, so the npc id alone identifies the result. Without
 * this an npc walking into view paid a full merge + recolour + scale +
 * DropNonSdTextures + per-vertex light + bounds + vertex capture EVERY time,
 * even though the source models were already resident: one server tick's worth
 * of arrivals was ~13ms of a 20ms frame.
 *
 * Returns an owned mutable instance -- the scene animates it in place, so the
 * cache keeps its own copy and never hands out the base. Render flags are
 * deliberately left off the cached base; the callers set TORIDRAW_MODEL_FLAG_
 * ZBUFFER per npc id after this returns.
 */
static struct ToriDraw_Model*
app_world_build_npc_model(
    struct App* app,
    int npc_id,
    struct ToriRS_Npctype* npctype)
{
    struct ToriDraw_Model* model;

    assert(app && npctype);

    model = TorirsModelInstCache_CopyGet(
        &app->model_inst_cache, TORIRS_MODEL_INST_NPC, (int64_t)npc_id);
    if( model )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_NPC_MODEL_CACHE_HIT, 1);
        return model;
    }
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_NPC_MODEL_CACHE_MISS, 1);

    {
        struct AppModelRecolorSpec recolors = {
            .recolors_from = npctype->recolors_from,
            .recolors_to = npctype->recolors_to,
            .recolor_count = npctype->recolor_count,
            .retextures_from = npctype->retextures_from,
            .retextures_to = npctype->retextures_to,
            .retexture_count = npctype->retexture_count,
        };
        model = app_world_build_model(
            app,
            npctype->models,
            npctype->models_count,
            &recolors,
            npctype->width_scale,
            npctype->height_scale,
            APP_LIGHT_ACTOR,
            app->npc_light_uses_type_ambient_contrast ? npctype->contrast : 0,
            app->npc_light_uses_type_ambient_contrast ? npctype->ambient : 0);
    }
    if( !model )
        return NULL;

    {
        struct ToriDraw_Model* base_copy = ToriDraw_ModelCopy(model);
        if( base_copy )
            TorirsModelInstCache_Put(
                &app->model_inst_cache, TORIRS_MODEL_INST_NPC, (int64_t)npc_id, base_copy);
    }
    return model;
}

/* Build the drawable model for a spotanim (reference SpotType.getTempModel2 +
 * MapSpotAnim.getTempModel static transforms): a single model, recoloured/
 * retextured, resized, angle-rotated and lit with the config ambient/contrast.
 * The seq animation itself is bound onto the element and stepped per-tick by
 * app_world_tick_animations (the projectile path), so only the static
 * transforms are baked here. SYNCHRONOUS — the model must already be resident.
 * Returns an owned model or NULL. */
static struct ToriDraw_Model*
app_world_build_spotanim_model(
    struct App* app,
    const struct ToriRS_Spotanimtype* spot)
{
    struct ToriDraw_Model* cached;
    struct ToriDraw_Model* model;
    int retextured = 0;
    int64_t key;

    assert(spot);
    /* Key by spotanim id — transforms are baked into the cached base, matching
     * Client-TS SpotType.modelCache keyed on spot id. */
    key = (int64_t)spot->id;
    cached = TorirsModelInstCache_CopyGet(&app->model_inst_cache, TORIRS_MODEL_INST_SPOT, key);
    if( cached )
        return cached;

    {
        struct ToriRS_Model* rs = CacheProvider_ModelGet(app->provider, spot->model);
        model = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
    }
    if( !model )
        return NULL;

    /* Recolour: reference guards the whole 6-pair loop on recol_s[0] != 0. */
    if( spot->recol_s[0] != 0 )
    {
        for( int i = 0; i < 6; i++ )
            ToriDraw_ModelRecolor(model, spot->recol_s[i], spot->recol_d[i]);
    }
    /* Retexture (dat2 only; dat1 leaves these zero). */
    for( int i = 0; i < 6; i++ )
    {
        if( spot->retex_s[i] != 0 )
        {
            ToriDraw_ModelRetexture(model, spot->retex_s[i], spot->retex_d[i]);
            retextured = 1;
        }
    }
    if( retextured )
        ToriDraw_ModelNoteTextureWants(model);

    /* Recorded rather than applied, for the reason app_world_build_model gives:
     * MapSpotAnim.getModel animates the copy and only then resizes it. */
    ToriDraw_ModelSetPostResize(model, spot->resizeh, spot->resizeh, spot->resizev);

    if( spot->angle != 0 )
        ToriDraw_ModelOrient(model, spot->angle / 90);

    ToriDraw_ModelDropNonSdTextures(app->provider, model);
    ToriDraw_ModelNoteTextureWants(model);

    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        ToriDraw_LightModelActor(hnd, spot->contrast, spot->ambient);
    }
    ToriDraw_ModelCaptureOriginalVertices(model);
    ToriDraw_ModelApplyPostTransforms(model);
    ToriDraw_ModelSetBoundsCylinder(model);

    /* Cache the lit base; return a copy so the scene owns a mutable instance. */
    {
        struct ToriDraw_Model* base_copy = ToriDraw_ModelCopy(model);
        if( base_copy )
            TorirsModelInstCache_Put(
                &app->model_inst_cache, TORIRS_MODEL_INST_SPOT, key, base_copy);
    }
    return model;
}

/* Hotkey 9 body: default player model on the hovered tile. SYNCHRONOUS —
 * the appearance kit + ready seq must be resident (Task_AppSpawn awaits).
 * Returns the world player-pool index, or -1. */
static int
app_world_spawn_player_now(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    int scene_model_id;
    struct ToriDraw_ModelHandle reg;
    struct ToriDraw_Model* copy;
    int world_x = tile_x * 128 + 64;
    int world_z = tile_z * 128 + 64;
    int world_y;
    int element_id;

    int idx;

    scene_model_id = UITreeSceneBridge_EnsurePlayerModel(&app->bridge);
    if( scene_model_id <= 0 )
    {
        TORIRS_LOG("spawn_player: player model unavailable\n");
        return -1;
    }
    reg = ToriDraw_SceneModelGet(app->scene, scene_model_id);
    if( !ToriDraw_ModelKindIsFull(reg.kind) || !reg.u.model.model )
        return -1;
    copy = ToriDraw_ModelCopy(reg.u.model.model);
    if( !copy )
        return -1;
    ToriDraw_ModelSetBoundsCylinder(copy);
    ToriDraw_ModelCaptureOriginalVertices(copy);

    world_y = app_world_height(app, world_x, world_z, level);
    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_PLAYER, copy, world_x, world_y, world_z);
    if( element_id < 0 )
        return -1;
    app_world_apply_seq(app, element_id, APP_PLAYER_SEQ_READY);
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
            el->anim_external = true;
        ToriDraw_SceneAnimListInvalidate(app->scene);
    }

    {
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = APP_PLAYER_SEQ_READY,
            .walkanim = APP_PLAYER_SEQ_WALK,
            .turnanim = APP_PLAYER_SEQ_TURN,
            .runanim = APP_PLAYER_SEQ_RUN,
            .walkanim_b = APP_PLAYER_SEQ_WALK_B,
            .walkanim_r = APP_PLAYER_SEQ_WALK_R,
            .walkanim_l = APP_PLAYER_SEQ_WALK_L,
        };
        idx = World_PlayerSpawn(app->world, element_id, level, tile_x, tile_z, idle);
    }
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(&app->world->entities.player, idx);
        if( player )
            player->server_pid = -1;
    }
    TORIRS_LOG("spawn_player: element=%d tile=%d,%d level=%d\n", element_id, tile_x, tile_z, level);
    app_sync_textures(app);
    app->need_redraw = 1;
    return idx;
}

/* Has this npc id already been reported as unavailable?  16384 bits is 2KB and
 * covers the whole npc id space of every revision here (osrs239 tops out near
 * 13000); an id past the end warns every time rather than being dropped. */
enum
{
    APP_NPC_WARN_BITS = 16384
};

static int
app_warn_once_npc(int npc_id)
{
    static uint32_t seen[APP_NPC_WARN_BITS / 32];

    if( npc_id < 0 || npc_id >= APP_NPC_WARN_BITS )
        return 0;
    if( seen[npc_id / 32] & (1u << (npc_id % 32)) )
        return 1;
    seen[npc_id / 32] |= (1u << (npc_id % 32));
    return 0;
}

/*
 * The entity's own facts, for a multinpc, are the RUNG's where it states them
 * and the SHELL's where it does not.
 *
 * A multinpc is one shell record plus a rung per state, and this client
 * resolves the rung BEFORE it spawns anything (Task_AppSpawn,
 * Task_ExecNpcInfo) -- so without this the whole entity was built out of the
 * rung, defaults and all. Rungs are not authored to stand alone: they are
 * deltas. `verzik_initial_base` states a name, a model, a chathead, two ops and
 * nothing else, while its shell `verzik_initial` carries `size=5` and
 * `readyanim=verzik_phase1_idle`. Read straight off the rung, Verzik was size
 * 1 -- which puts her draw origin at `tile * 128 + 64` instead of
 * `tile * 128 + 5 * 64`, two tiles south-west of her own dais -- and readyanim
 * -1, so no sequence ever bound to the element and she sat in the model's bind
 * pose for the whole of phase one. One record's absent fields, two bugs that
 * looked unrelated.
 *
 * Across this cache the gap is 102 rungs silently size 1 under a shell that
 * states a size, and 526 silently animation-less under a shell that states a
 * readyanim. NOTHING here changes a rung that states the field itself: the 49
 * records whose readyanim genuinely disagrees with their shell keep the rung's,
 * and no rung anywhere disagrees about size.
 *
 * That last point is a deliberate deviation, flagged rather than hidden. The
 * reference reads these off ONE record -- `npc.type`, the id the WIRE sent,
 * i.e. the shell -- at both the NPC add and the CHANGETYPE mask (Client.ts
 * 8344-8353, 8455-8464), and `transform()` is called later and only where the
 * MODEL, the name and the ops are chosen. By that rule the shell would win
 * outright and those 49 rungs' readyanims would be dead data. There is no
 * revision-239 client in this tree to confirm it against, and taking the shell
 * outright would silently restyle the 674 rungs whose shell states nothing, so
 * this fills gaps and never overrides. Revisit with a rev-239 deob in hand.
 *
 * `turn_speed` is deliberately absent: its default is 32 rather than a
 * sentinel, so "absent" and "authored 32" are the same value and gap-filling
 * cannot be expressed for it.
 */
struct AppNpcEntityFacts
{
    int size;
    int readyanim;
    int walkanim;
    int walkanim_b;
    int walkanim_l;
    int walkanim_r;
    int turnanim;
    int runanim;
    int runanim_b;
    int runanim_l;
    int runanim_r;
};

static void
app_npc_entity_facts(
    struct App* app,
    int base_npc_id,
    struct ToriRS_Npctype const* drawn,
    struct AppNpcEntityFacts* out)
{
    struct ToriRS_Npctype* shell;

    assert(app);
    assert(drawn);
    assert(out);

    out->size = drawn->size > 0 ? drawn->size : 1;
    out->readyanim = drawn->readyanim;
    out->walkanim = drawn->walkanim;
    out->walkanim_b = drawn->walkanim_b;
    out->walkanim_l = drawn->walkanim_l;
    out->walkanim_r = drawn->walkanim_r;
    out->turnanim = drawn->turnanim_l;
    out->runanim = drawn->runanim;
    out->runanim_b = drawn->runanim_b;
    out->runanim_l = drawn->runanim_l;
    out->runanim_r = drawn->runanim_r;

    if( base_npc_id < 0 )
        return;
    shell = CacheProvider_NpctypeGet(app->provider, base_npc_id);
    if( !shell || shell == drawn )
        return;

    if( out->size <= 1 && shell->size > 1 )
        out->size = shell->size;
    if( out->readyanim < 0 )
        out->readyanim = shell->readyanim;
    if( out->walkanim < 0 )
        out->walkanim = shell->walkanim;
    if( out->walkanim_b < 0 )
        out->walkanim_b = shell->walkanim_b;
    if( out->walkanim_l < 0 )
        out->walkanim_l = shell->walkanim_l;
    if( out->turnanim < 0 )
        out->turnanim = shell->turnanim_l;
    if( out->runanim < 0 )
        out->runanim = shell->runanim;
    if( out->runanim_b < 0 )
        out->runanim_b = shell->runanim_b;
    if( out->runanim_l < 0 )
        out->runanim_l = shell->runanim_l;
    if( out->runanim_r < 0 )
        out->runanim_r = shell->runanim_r;
    if( out->walkanim_r < 0 )
        out->walkanim_r = shell->walkanim_r;
}

/* Hotkey 8 body: npc on the hovered tile. SYNCHRONOUS — the npc config and
 * its models must be resident (Task_AppSpawn awaits them first).
 *
 * `npc_id` is the type whose MODEL is drawn (a multinpc rung, once resolved);
 * `base_npc_id` is the id the wire sent, and is what `app_npc_entity_facts`
 * fills the entity's own facts from. Pass -1 when there is no separate
 * shell.
 *
 * Returns the world npc-pool index, or -1. */
static int
app_world_spawn_npc_now(
    struct App* app,
    int npc_id,
    int base_npc_id,
    int tile_x,
    int tile_z,
    int level)
{
    struct AppNpcEntityFacts facts;
    struct ToriRS_Npctype* npctype;
    struct ToriDraw_Model* model;
    int size;
    int world_x, world_z, world_y;
    int element_id;
    int idx;
    /* TORIRS_SPAWN_BREAKDOWN=<us>: split one npc spawn when it exceeds <us>.
     * TORIRS_SPAWN_LOG=1: restore the old per-spawn narration line. */
    static int bd_us = -1;
    static int spawn_log = -1;
    uint64_t bd_t0, bd_t;
    uint64_t bd_model = 0, bd_elem = 0, bd_world = 0, bd_seq = 0, bd_tex = 0, bd_log = 0;
    extern uint64_t PlatformWindow_TicksUs(void);

    if( bd_us < 0 )
    {
        char const* v = getenv("TORIRS_SPAWN_BREAKDOWN");
        bd_us = (v && v[0]) ? atoi(v) : 0;
        spawn_log = getenv("TORIRS_SPAWN_LOG") ? 1 : 0;
    }
    bd_t0 = bd_us ? PlatformWindow_TicksUs() : 0;

    npctype = CacheProvider_NpctypeGet(app->provider, npc_id);
    if( !npctype )
    {
        /* Once per id. The server re-sends the same missing npc every time the
         * player walks back into its zone, and on Windows an unbuffered stderr
         * write costs milliseconds -- see the spawn narration below. Which ids
         * are unavailable is the whole diagnostic; the repeat count is not. */
        if( !app_warn_once_npc(npc_id) )
            TORIRS_LOG("spawn_npc: npc %d unavailable\n", npc_id);
        return -1;
    }

    bd_t = bd_us ? PlatformWindow_TicksUs() : 0;
    if( npctype->models_count <= 0 )
    {
        /*
         * A model-less npc is legal content, not a broken record.
         *
         * The reference client builds the entity straight off the wire and only
         * resolves a model at draw time, where a null model skips the body and
         * leaves the entity otherwise intact (rev239 deob: the npc add path
         * constructs and registers unconditionally; Renderable.draw returns
         * early on a null model). OldSchool ships such npcs deliberately --
         * `invisible_npc_softblocking`, `hw22_trick_ghost_invis` -- as pure
         * server-side markers that still carry hitsplats, overhead text and
         * collision.
         *
         * Rejecting them here dropped the entity entirely: no element, so no
         * entry in the npc pool, so every later NPC_INFO mask for that slot
         * resolved to -1 and was discarded. An empty model keeps the entity in
         * the world and draws nothing; its zeroed bounds cylinder gives
         * height 0, which anchors overlays at the marker's own tile.
         */
        model = ToriDraw_ModelNew(0, 0, 0);
        if( model )
            ToriDraw_ModelSetBoundsCylinder(model);
    }
    else
    {
        model = app_world_build_npc_model(app, npc_id, npctype);
    }
    if( bd_us )
        bd_model = PlatformWindow_TicksUs() - bd_t;
    if( !model )
    {
        /* Same rationale as the models_count<=0 branch above: a missing
         * model must not drop the entity itself, or every later NPC_INFO
         * mask for this slot resolves to -1 and is silently discarded for
         * the rest of the session, with no retry (npc_add only fires once
         * per spawn). This path is reached on a transient/real model-load
         * failure -- large multi-part npcs like QBD are the most exposed --
         * so register an empty placeholder and keep going; that degrades to
         * an invisible npc instead of erasing it outright. */
        TORIRS_ERR("spawn_npc: npc %d models failed to load\n", npc_id);
        model = ToriDraw_ModelNew(0, 0, 0);
        if( model )
            ToriDraw_ModelSetBoundsCylinder(model);
        if( !model )
            return -1;
    }
    /* Set explicitly both ways. Models arrive from ToriDraw_ModelFromToriRS with
     * no render flags, so the opt-in is this line and nothing else; clearing is
     * still written out because this model may be a cache copy of one that was
     * opted in earlier under a different npc id. */
    app_model_apply_import_render_flags(model, app_npc_wants_zbuffer(npc_id, npctype));

    app_npc_entity_facts(app, base_npc_id, npctype, &facts);
    size = facts.size;
    world_x = tile_x * 128 + size * 64;
    world_z = tile_z * 128 + size * 64;
    world_y = app_world_height(app, world_x, world_z, level);
    bd_t = bd_us ? PlatformWindow_TicksUs() : 0;
    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_NPC, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return -1;
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
            el->anim_external = true;
        ToriDraw_SceneAnimListInvalidate(app->scene);
    }
    if( bd_us )
        bd_elem = PlatformWindow_TicksUs() - bd_t;

    bd_t = bd_us ? PlatformWindow_TicksUs() : 0;
    {
        /* Config movement anims (dat1 has no turn/run for npcs; the reference
         * walkanim_l/r swap applies here at spawn like at CHANGE_TYPE). */
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = facts.readyanim,
            .walkanim = facts.walkanim,
            /* Opcodes 15/114 rather than the -1 pair that used to sit here.
             * `World_EntityFace` takes turnanim over walkanim and
             * `World_UpdateMoverMovementAndAnimation` takes runanim over
             * walkanim at speed; both were already written and neither had
             * anything to read. The run set gets the same left/right swap the
             * walk set does -- it is the same reference line (Client.ts
             * 8460-8462), which swaps the pair for every movement set. */
            .turnanim = facts.turnanim,
            .runanim = facts.runanim,
            .walkanim_b = facts.walkanim_b,
            .walkanim_r = facts.walkanim_l,
            .walkanim_l = facts.walkanim_r,
            .idle_anim_restart = npctype->idle_anim_restart ? 1 : 0,
        };
        idx = World_NpcSpawn(app->world, element_id, npc_id, level, tile_x, tile_z, size, idle);
    }
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, idx);
        if( npc )
            npc->server_slot = -1;
    }
    if( bd_us )
        bd_world = PlatformWindow_TicksUs() - bd_t;

    bd_t = bd_us ? PlatformWindow_TicksUs() : 0;
    app_world_apply_seq(app, element_id, facts.readyanim);
    if( bd_us )
        bd_seq = PlatformWindow_TicksUs() - bd_t;
    /* Spawn does not carry menu data; the minimenu rows read it off the
     * entity, so copy name/actions/level from the config here. */
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, idx);
        if( npc )
        {
            npc->combat_level = npctype->combat_level;
            npc->alwaysontop = npctype->alwaysontop;
            npc->minimap_visible = npctype->minimap_visible;
            npc->interactable = npctype->interactable;
            npc->facing.turn_speed = npctype->turn_speed;
            snprintf(npc->name, sizeof(npc->name), "%s", npctype->name);
            for( int i = 0; i < 5; i++ )
                snprintf(
                    npc->actions[i].name, sizeof(npc->actions[i].name), "%s", npctype->actions[i]);
        }
    }
    /* Was unconditional. stderr is unbuffered, so this is a synchronous write
     * per npc arrival on the packet-apply path -- and npcs arrive in bursts of
     * 20+ when the player crosses into a populated zone. Gated behind its own
     * switch so the cost stays measurable (spawn_bd's `log` column). */
    bd_t = bd_us ? PlatformWindow_TicksUs() : 0;
    if( spawn_log )
        TORIRS_LOG(
            "spawn_npc: npc=%d element=%d tile=%d,%d level=%d size=%d recolors=%d "
            "retextures=%d\n",
            npc_id,
            element_id,
            tile_x,
            tile_z,
            level,
            size,
            npctype->recolor_count,
            npctype->retexture_count);
    if( bd_us )
        bd_log = PlatformWindow_TicksUs() - bd_t;

    bd_t = bd_us ? PlatformWindow_TicksUs() : 0;
    app_sync_textures(app);
    if( bd_us )
    {
        uint64_t total;

        bd_tex = PlatformWindow_TicksUs() - bd_t;
        total = PlatformWindow_TicksUs() - bd_t0;
        if( total >= (uint64_t)bd_us )
            TORIRS_LOG(
                "spawn_bd: npc=%d total %llu model %llu elem %llu world %llu seq %llu "
                "log %llu tex %llu (us)\n",
                npc_id,
                (unsigned long long)total,
                (unsigned long long)bd_model,
                (unsigned long long)bd_elem,
                (unsigned long long)bd_world,
                (unsigned long long)bd_seq,
                (unsigned long long)bd_log,
                (unsigned long long)bd_tex);
    }
    app->need_redraw = 1;
    /* After the entity is in the pool and carries its name and facts, so a
     * handler's snapshot is the finished npc rather than a half-built one. */
    if( app->plugins && idx != WORLD_ENTITY_NIL )
    {
        struct WorldEntity_NPC* spawned = World_EntityPoolGet(&app->world->entities.npc, idx);
        if( spawned )
        {
            struct ToriRS_NpcSnapshot snap;
            app_plugin_fill_npc(app, spawned, &snap);
            PluginHost_NpcSpawn(app->plugins, &snap);
        }
    }
    return idx;
}

/* Hotkey 0 fire body: launch source -> destination. SYNCHRONOUS — the
 * projectile model must be resident (Task_AppSpawn awaits it). Arc math
 * lives in World_ProjectileSetTarget/Move (TS reference parity). `target` is
 * the wire target-entity id when a synced NPC sits on the destination tile,
 * WORLD_PROJECTILE_TARGET_NONE for a plain tile shot. */
static void
app_world_spawn_projectile_now(
    struct App* app,
    struct World* world,
    int model_id,
    int seq_id,
    int src_tile_x,
    int src_tile_z,
    int src_level,
    int tile_x,
    int tile_z,
    int target)
{
    int model_ids[1];
    struct ToriDraw_Model* model;
    int src_x, src_z, dst_x, dst_z, src_y;
    int range, t2;
    int element_id;

    assert(app);
    assert(world);
    model_ids[0] = model_id;
    model = app_world_build_model(app, model_ids, 1, NULL, 128, 128, APP_LIGHT_ACTOR, 0, 0);
    if( !model )
    {
        TORIRS_ERR("spawn_projectile: model %d failed to load\n", model_id);
        return;
    }

    src_x = src_tile_x * 128 + 64;
    src_z = src_tile_z * 128 + 64;
    dst_x = tile_x * 128 + 64;
    dst_z = tile_z * 128 + 64;
    /* World y is negative-up: start slightly above the source ground. */
    src_y = app_world_height(app, src_x, src_z, src_level) - 160;

    range = abs(tile_x - src_tile_x);
    if( abs(tile_z - src_tile_z) > range )
        range = abs(tile_z - src_tile_z);
    t2 = 60 + range * 5; /* ticks: base flight + per-tile stretch */

    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_PROJECTILE, model, src_x, src_y, src_z);
    if( element_id < 0 )
        return;

    World_ProjectileSpawn(
        world,
        element_id,
        src_level,
        src_x,
        src_z,
        dst_x,
        dst_z,
        src_y,
        144, /* end height above target ground (36 * 4) */
        0,
        t2,
        15, /* launch slope (1/2048 circle units) */
        64,
        target);
    /* Bind the spotanim's sequence so the projectile model animates in flight
     * (v1 Task_*ProjectileAdd loads the seq, then ElementSetSequenceId). The
     * element is left non-external, so app_world_tick_animations advances the
     * frame each tick — matching ClientProj.move's plain frame loop, which is
     * why the element is marked anim_loop. */
    ToriDraw_SceneElementSetAnimLoop(app->scene, element_id, true);
    app_world_apply_seq(app, element_id, seq_id);
    TORIRS_LOG(
        "spawn_projectile: element=%d %d,%d -> %d,%d t2=%d target=%d\n",
        element_id,
        src_tile_x,
        src_tile_z,
        tile_x,
        tile_z,
        t2,
        target);
    app_sync_textures(app);
    app->need_redraw = 1;
}

/*
 * The ground under a ROOT-frame point, composed through a hull when one is
 * there: a point over a live boat's deck answers the DECK's heightmap plus
 * the hull's y and bob — the same composition the emit path and the overlay
 * anchors use — and root terrain otherwise. A projectile fired from (or at)
 * the deck must measure its height from the planking, not from the grass or
 * water the hull happens to be sitting on: the deob never faces the question
 * because its projectile lives in the boat's own world, where "ground" IS
 * the deck; this is that answer for the root-world approximation.
 */
static int
app_world_ground_composed(
    struct App* app,
    int fine_x,
    int fine_z,
    int level)
{
    for( int view_id = 1; view_id < WORLDVIEW_MAX; view_id++ )
    {
        struct Wev* wev;
        struct WevDeckBox box;
        int deck_x;
        int deck_z;
        int deck_level;
        struct Worldview* view;

        if( !Wevs_IsLive(&app->wevs, view_id) ||
            !WorldviewRegistry_IsLive(&app->worldviews, view_id) )
            continue;
        wev = Wevs_Get(&app->wevs, view_id);
        if( wev->parent_view_id != WORLDVIEW_ROOT )
            continue;
        /* Membership is the HULL's footprint — the gunwale rule. The deck box
         * is the whole zone reservation and the templates carry walkable
         * staging ground around the parked hull, so a shore tile beside the
         * boat is inside the box but not on the planking; composing its
         * ground from the deck template would bend a shot aimed past the
         * rail. The footprint is the painter's own answer to "which root
         * tiles does this hull cover", so the two cannot disagree; a config
         * with no stated extent (the wire-sized Zenith) makes the footprint
         * the whole box, which is the right fallback too. */
        if( wev->config )
        {
            int min_tile_x;
            int min_tile_z;
            int size_x;
            int size_z;
            int abs_tile_x = (fine_x >> 7) + app->world->_base_tile_x;
            int abs_tile_z = (fine_z >> 7) + app->world->_base_tile_z;

            Wev_FootprintTiles(wev, 0, &min_tile_x, &min_tile_z, &size_x, &size_z);
            if( abs_tile_x < min_tile_x || abs_tile_x >= min_tile_x + size_x ||
                abs_tile_z < min_tile_z || abs_tile_z >= min_tile_z + size_z )
                continue;
        }
        app_wev_deck_box(app, wev, app->world, &box);
        Wev_DeckFromParent(&box, fine_x, fine_z, &deck_x, &deck_z);
        if( !Wev_DeckContainsDeckPoint(&box, deck_x, deck_z) )
            continue;
        view = WorldviewRegistry_Get(&app->worldviews, view_id);
        deck_level = app_wev_deck_level(app, view_id);
        if( deck_level >= COLLISION_LEVELS )
            deck_level = COLLISION_LEVELS - 1;
        return wev->y + wev->bob_y + World_HeightAt(view->world, deck_x, deck_z, deck_level);
    }
    return app_world_height(app, fine_x, fine_z, level);
}

/* Server-driven projectile (reference ClientProj / MAP_PROJANIM). Builds the
 * transformed spotanim model (recolour/resize/angle/lighting), spawns the world
 * projectile with the wire trajectory params, and binds the spotanim seq so the
 * model animates in flight. SYNCHRONOUS — the spotanimtype, its model and its
 * seq must already be resident (Task_AppSpawn awaits them). src_height/dst_height
 * are raw wire bytes (×4, matching Client.ts h1/h2). */
static void
app_world_spawn_projectile_spot_now(
    struct App* app,
    struct World* world,
    int spotanim_id,
    int src_tile_x,
    int src_tile_z,
    int src_level,
    int dst_tile_x,
    int dst_tile_z,
    int dst_level,
    int src_height,
    int dst_height,
    int start_delay,
    int end_delay,
    int peak,
    int arc,
    int target)
{
    struct ToriRS_Spotanimtype* spot;
    struct ToriDraw_Model* model;
    int src_x, src_z, dst_x, dst_z, src_y;
    int element_id;

    assert(app);
    assert(world);
    spot = CacheProvider_SpotanimtypeGet(app->provider, spotanim_id);
    if( !spot )
    {
        TORIRS_LOG("spawn_projectile_spot: spotanim %d not resident\n", spotanim_id);
        return;
    }

    model = app_world_build_spotanim_model(app, spot);
    if( !model )
    {
        TORIRS_ERR(
            "spawn_projectile_spot: spotanim %d model %d failed\n", spotanim_id, spot->model);
        return;
    }

    src_x = src_tile_x * 128 + 64;
    src_z = src_tile_z * 128 + 64;
    dst_x = dst_tile_x * 128 + 64;
    dst_z = dst_tile_z * 128 + 64;
    /* World y is negative-up: reference y = getAvH(src) - h1, h1 = src_height*4.
     * The SOURCE ground composes through a hull when the shooter stands on
     * one — a bow fired from the deck leaves at deck + chest, not at the
     * terrain the hull is parked over (a beached hull's planking can sit well
     * below the bank beside it). The deob never faces this: its projectile
     * lives in the boat's world, where ground IS the deck. The DESTINATION
     * deliberately does NOT compose: the landing samples this world's own
     * ground exactly as the deob's `getAvH(dst, proj.plane) - h2` does, and
     * World re-aims a tracked target with that same rule every cycle — a
     * composed one-shot correction here went stale the moment the target
     * moved and bent the arc whenever the hull's footprint brushed the
     * target's tile. */
    src_y = app_world_ground_composed(app, src_x, src_z, src_level) - src_height * 4;

    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_PROJECTILE, model, src_x, src_y, src_z);
    if( element_id < 0 )
        return;

    /* peak -> angle, arc -> startpos, end_height = dst_height*4 (World computes
     * dst y as height_fn(dst) - end_height, matching getAvH(dst) - h2). `target`
     * goes through in its wire encoding so World re-aims the arc at that
     * entity's live position every cycle (reference addProjectiles). */
    World_ProjectileSpawn(
        world,
        element_id,
        dst_level,
        src_x,
        src_z,
        dst_x,
        dst_z,
        src_y,
        dst_height * 4,
        start_delay,
        end_delay,
        peak,
        arc,
        target);
    /* Reference ClientProj.move wraps animFrame to 0 at the end of the frame
     * list and never drops the sequence. Flight time routinely outlasts the
     * sequence — a 12-cycle spotanim seq against a 30+ cycle flight is normal —
     * so without this the model spends most of the flight back in its un-posed
     * bind pose. Spotanim models hide geometry by scaling it to zero in every
     * frame, so what that actually looks like is extra parts of the model
     * appearing mid-air partway to the target. */
    ToriDraw_SceneElementSetAnimLoop(app->scene, element_id, true);
    app_world_apply_seq(app, element_id, spot->seq);

    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "spawn_projectile_spot: element=%d spotanim=%d model=%d seq=%d "
            "%d,%d -> %d,%d lvl=%d/%d t1=%d t2=%d target=%d src_y=%d ground=%d "
            "dst_ground=%d h1=%d h2=%d\n",
            element_id,
            spotanim_id,
            spot->model,
            spot->seq,
            src_tile_x,
            src_tile_z,
            dst_tile_x,
            dst_tile_z,
            src_level,
            dst_level,
            start_delay,
            end_delay,
            target,
            src_y,
            app_world_ground_composed(app, src_x, src_z, src_level),
            app_world_height(app, dst_x, dst_z, dst_level),
            src_height * 4,
            dst_height * 4);
    app_sync_textures(app);
    app->need_redraw = 1;
}

/* Free-standing spotanim (reference MapSpotAnim / MAP_ANIM). SYNCHRONOUS — the
 * spotanimtype, its model and its seq must already be resident (Task_AppSpawn
 * awaits them). Builds the transformed model, spawns the world entity with a
 * single-shot lifetime equal to one seq loop, and binds the seq so the element
 * animates per-tick. */
static void
app_world_spawn_spotanim_now(
    struct App* app,
    struct World* world,
    int spotanim_id,
    int tile_x,
    int tile_z,
    int level,
    int height,
    int delay)
{
    struct ToriRS_Spotanimtype* spot;
    struct ToriDraw_Model* model;
    int world_x, world_z, world_y;
    int element_id;
    int lifetime;

    assert(app);
    assert(world);
    spot = CacheProvider_SpotanimtypeGet(app->provider, spotanim_id);
    if( !spot )
    {
        TORIRS_LOG("spawn_spotanim: spotanim %d not resident\n", spotanim_id);
        return;
    }

    model = app_world_build_spotanim_model(app, spot);
    if( !model )
    {
        TORIRS_ERR("spawn_spotanim: spotanim %d model %d failed\n", spotanim_id, spot->model);
        return;
    }

    world_x = tile_x * 128 + 64;
    world_z = tile_z * 128 + 64;
    /* Reference y = getAvH(x,z) - height; world y is negative-up so subtracting
     * raises the effect above the ground by `height`. */
    world_y = app_world_height(app, world_x, world_z, level) - height;

    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_SPOTANIM, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return;

    lifetime = WorldSeqSourceToriDraw_TotalDuration(&app->seq_source, spot->seq);

    World_SpotanimSpawn(world, element_id, level, world_x, world_z, world_y, 0, delay, lifetime);
    app_world_apply_seq(app, element_id, spot->seq);
    /* A delayed spotanim is invisible until World flips it active, so its
     * sequence must not run in the meantime. Park it as anim_external — the
     * flag the naive per-element tick uses to mean "someone else owns this
     * element's frames" — and let WorldEventKind_SpotanimStarted hand it back
     * on the cycle it first draws. Without this the whole delay is spent
     * animating out of sight: `spotanim_map`'s delay is a projectile's flight
     * time, which is routinely longer than the sequence, so the splash
     * surfaced already finished and sat on a cleared final frame. Elements
     * with no delay are left alone and start immediately, as before. */
    if( delay > 0 )
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
        {
            el->anim_external = true;
            /* anim_list membership is filtered on anim_external, so the cached
             * list is now stale (toridraw_scene.h: the caller mutating this
             * flag directly owns the invalidation). */
            ToriDraw_SceneAnimListInvalidate(app->scene);
        }
    }

    TORIRS_LOG(
        "spawn_spotanim: id=%d element=%d tile=%d,%d level=%d model=%d seq=%d "
        "life=%d delay=%d\n",
        spotanim_id,
        element_id,
        tile_x,
        tile_z,
        level,
        spot->model,
        spot->seq,
        lifetime,
        delay);
    app_sync_textures(app);
    app->need_redraw = 1;
}

/* ------------------------------------------------ plugin-authored meshes */

/*
 * Geometry a plugin built itself, and the model made from it.
 *
 * The reason this exists at all is that a cache id is not portable. A plugin
 * that stands a beam of light over a drop by naming model 43330 is naming a
 * number that means that beam in one revision, means something else in
 * another, and is absent from most -- so the plugin works on the cache it was
 * written against and silently draws a crate, or nothing, on the rest. An
 * authored mesh has no such dependency: it is the same triangles wherever it
 * is drawn.
 *
 * Storage is the App's rather than the host's because only this side can turn
 * a mesh into something drawable, and the arrays grow on append because the
 * api ceilings are a limit and not a size.
 */

static struct AppPluginAssetModel*
app_plugin_asset_model_at(
    struct App* app,
    int handle)
{
    assert(app);
    if( handle < 0 || handle >= TORIRS_PLUGIN_MODELS_MAX )
        return NULL;
    if( !app->plugin_asset_models[handle].in_use )
        return NULL;
    return &app->plugin_asset_models[handle];
}

static struct ToriRS_PluginMesh*
app_plugin_mesh_at(
    struct App* app,
    int handle)
{
    assert(app);
    if( handle < 0 || handle >= APP_PLUGIN_MESHES_MAX )
        return NULL;
    if( !app->plugin_meshes[handle].in_use )
        return NULL;
    return &app->plugin_meshes[handle];
}

/* ----------------------------------------------- plugin-owned world objects */

/*
 * A plugin's model, standing in the scene.
 *
 * Three things have to agree for one of these to draw, and they arrive at
 * different times: the plugin's intent (a model id, a tile, a colour), the
 * cache assets that intent names, and a scene element to hang them on. The
 * record in app->plugin_objects is the intent; everything below is the
 * machinery that keeps the other two chasing it.
 *
 * The rule that makes it tractable: intent is never applied in place. A change
 * to anything the MODEL is built from tears the element down and rebuilds it;
 * a change to where it stands or whether it is drawn is applied to the live
 * entity. Deciding which is which is `built_*` versus what the plugin now
 * says, which is also what stops a plugin that re-states the same intent every
 * frame from rebuilding a model every frame.
 */

static struct AppPluginObject*
app_plugin_object_at(
    struct App* app,
    int handle)
{
    assert(app);
    if( handle < 0 || handle >= APP_PLUGIN_OBJECTS_MAX )
        return NULL;
    if( !app->plugin_objects[handle].in_use )
        return NULL;
    return &app->plugin_objects[handle];
}

/* A cheap identity for the recolour list, so "did the colours change?" does not
 * mean comparing two arrays on every call. Order-sensitive on purpose: recolour
 * is applied in order and two lists that differ only in order can genuinely
 * produce different models. */
static int
app_plugin_object_recolor_stamp(struct AppPluginObject const* obj)
{
    int stamp = 17;
    assert(obj);
    for( int i = 0; i < obj->recolor_count; i++ )
        stamp = stamp * 31 + (obj->recolor_from[i] * 65599 + obj->recolor_to[i]);
    return stamp * 31 + obj->recolor_count;
}

/* The revision of the geometry this object stands on, or 0 when it stands on
 * none -- a cache-sourced object, or one whose mesh was destroyed or whose
 * shipped model never decoded. Compared against built_geometry_revision, so
 * geometry that is re-authored, or that arrives late off the IO queue,
 * rebuilds the objects made from it, and geometry re-stated unchanged rebuilds
 * nothing. */
static int
app_plugin_object_geometry_revision(
    struct App* app,
    struct AppPluginObject const* obj)
{
    assert(app);
    assert(obj);

    if( obj->source == TORIRS_HOST_MODEL_MESH )
    {
        struct ToriRS_PluginMesh const* mesh = app_plugin_mesh_at(app, obj->model_id);
        return mesh ? mesh->revision : 0;
    }
    if( obj->source == TORIRS_HOST_MODEL_ASSET )
    {
        struct AppPluginAssetModel const* shipped = app_plugin_asset_model_at(app, obj->model_id);
        return shipped ? shipped->revision : 0;
    }
    return 0;
}

/* The model ids this object's source needs resident before it can be built:
 * a CACHE object names its model directly, a SPOTANIM object names it through
 * the spotanimtype. Returns -1 when it is not knowable yet. */
static int
app_plugin_object_model_id(
    struct App* app,
    struct AppPluginObject const* obj)
{
    assert(app);
    assert(obj);

    if( obj->source == TORIRS_HOST_MODEL_SPOTANIM )
    {
        struct ToriRS_Spotanimtype* spot =
            obj->model_id >= 0 ? CacheProvider_SpotanimtypeGet(app->provider, obj->model_id) : NULL;
        return spot ? spot->model : -1;
    }
    /* A MESH object's model_id is a mesh handle and an ASSET object's is a
     * model-load handle -- neither is a cache id, and neither has anything for
     * the spawn task to make resident. That is the whole point of both: the
     * geometry travels with the plugin. */
    if( obj->source == TORIRS_HOST_MODEL_MESH || obj->source == TORIRS_HOST_MODEL_ASSET )
        return -1;
    return obj->model_id;
}

/* The sequence the object should play: the plugin's if it named one, else the
 * spotanimtype's own. -1 = no animation. */
static int
app_plugin_object_seq_id(
    struct App* app,
    struct AppPluginObject const* obj)
{
    assert(app);
    assert(obj);

    /* An AUTHORED mesh carries no rig, and a cache sequence is a table of
     * transforms addressed by transform group -- there is nothing in a mesh
     * for one to drive. Binding one is a plugin bug rather than a shape this
     * can express, so it stops here instead of animating nothing.
     *
     * A SHIPPED model is not covered: a model file can carry bones, and a
     * plugin that ships one alongside a cache revision it knows may legitimately
     * name that revision's sequence. Whether the two agree is its business. */
    assert(!(obj->source == TORIRS_HOST_MODEL_MESH && obj->seq_id >= 0));

    if( obj->seq_id >= 0 )
        return obj->seq_id;
    if( obj->source == TORIRS_HOST_MODEL_SPOTANIM )
    {
        struct ToriRS_Spotanimtype* spot =
            obj->model_id >= 0 ? CacheProvider_SpotanimtypeGet(app->provider, obj->model_id) : NULL;
        return spot ? spot->seq : -1;
    }
    return -1;
}

/*
 * Build the drawable model. SYNCHRONOUS -- everything it reads must already be
 * resident (the spawn task awaits it). Returns an owned model or NULL.
 *
 * Not routed through app_world_build_spotanim_model even for a SPOTANIM
 * object, and not through the instance cache either, for one reason: the
 * plugin's recolours have to be applied BEFORE lighting. Lighting bakes the
 * face colours into the per-vertex a/b/c triples the rasteriser reads, and a
 * recolour after that point rewrites a table nothing looks at again -- the
 * model comes out exactly the colour it started. That is a silent failure, so
 * this path is written out rather than layered on one that cannot express it.
 */
static struct ToriDraw_Model*
app_plugin_object_build_model(
    struct App* app,
    struct AppPluginObject const* obj)
{
    struct ToriRS_Spotanimtype const* spot = NULL;
    struct ToriDraw_Model* model;
    int retextured = 0;

    assert(app);
    assert(obj);

    if( obj->source == TORIRS_HOST_MODEL_ASSET )
    {
        /* Not resident yet is the ordinary state for the first frame or two:
         * the file crosses the IO queue like every other asset, and the settle
         * builds the object when it lands. */
        struct AppPluginAssetModel const* shipped = app_plugin_asset_model_at(app, obj->model_id);
        if( !shipped || !shipped->model )
            return NULL;
        model = ToriDraw_ModelFromToriRS(shipped->model);
        if( !model )
            return NULL;
    }
    else if( obj->source == TORIRS_HOST_MODEL_MESH )
    {
        struct ToriRS_PluginMesh const* mesh = app_plugin_mesh_at(app, obj->model_id);
        /* An empty mesh is not a bug: a plugin that has taken a handle and not
         * yet authored into it is mid-build, and the object has nothing to
         * draw until it has. A handle that names no mesh at all is the same
         * answer from the object's side -- it was destroyed under it. */
        if( !mesh || mesh->face_count <= 0 )
            return NULL;
        model = ToriRS_PluginMeshBuildModel(mesh);
    }
    else
    {
        int const model_id = app_plugin_object_model_id(app, obj);

        if( obj->source == TORIRS_HOST_MODEL_SPOTANIM )
            spot = obj->model_id >= 0 ? CacheProvider_SpotanimtypeGet(app->provider, obj->model_id)
                                      : NULL;
        if( model_id < 0 )
            return NULL;

        {
            struct ToriRS_Model* rs = CacheProvider_ModelGet(app->provider, model_id);
            model = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
        }
        if( !model )
            return NULL;

        /* The spotanimtype's own recolours first, so the plugin's pairs are
         * stated against the colours it can actually see on the finished
         * graphic. */
        if( spot )
        {
            if( spot->recol_s[0] != 0 )
            {
                for( int i = 0; i < 6; i++ )
                    ToriDraw_ModelRecolor(model, spot->recol_s[i], spot->recol_d[i]);
            }
            for( int i = 0; i < 6; i++ )
            {
                if( spot->retex_s[i] != 0 )
                {
                    ToriDraw_ModelRetexture(model, spot->retex_s[i], spot->retex_d[i]);
                    retextured = 1;
                }
            }
        }
    }

    for( int i = 0; i < obj->recolor_count; i++ )
        ToriDraw_ModelRecolor(model, obj->recolor_from[i], obj->recolor_to[i]);

    if( retextured )
        ToriDraw_ModelNoteTextureWants(model);

    /* Recorded, not applied: the resize belongs after every animation frame,
     * for the reason app_world_build_spotanim_model gives. */
    if( spot )
    {
        ToriDraw_ModelSetPostResize(model, spot->resizeh, spot->resizeh, spot->resizev);
        if( spot->angle != 0 )
            ToriDraw_ModelOrient(model, spot->angle / 90);
    }

    ToriDraw_ModelDropNonSdTextures(app->provider, model);
    ToriDraw_ModelNoteTextureWants(model);

    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        /* The plugin's offsets ON TOP of the type's, so a SPOTANIM object with
         * no light of its own looks exactly like the server-drawn graphic. */
        ToriDraw_LightModelActor(
            hnd,
            (spot ? spot->contrast : 0) + obj->contrast,
            (spot ? spot->ambient : 0) + obj->ambient);
    }
    ToriDraw_ModelCaptureOriginalVertices(model);
    ToriDraw_ModelApplyPostTransforms(model);
    ToriDraw_ModelSetBoundsCylinder(model);
    return model;
}

/* Drop the live element and world entity, leaving the intent alone. The
 * EntityRemoved event frees the scene element on the next drain, which is the
 * same path every other despawn takes. */
static void
app_plugin_object_teardown(
    struct App* app,
    struct AppPluginObject* obj)
{
    assert(app);
    assert(obj);

    if( obj->world_index >= 0 && app->world )
        World_PluginObjectDespawn(app->world, obj->world_index);
    obj->world_index = -1;
    obj->element_id = -1;
    obj->built_source = -1;
    obj->built_model_id = -1;
    obj->built_recolor_stamp = 0;
    obj->built_geometry_revision = 0;
}

/* Scene-local placement for an object's absolute tile, or 0 when it is off the
 * current scene. */
static int
app_plugin_object_scene_pos(
    struct App* app,
    struct AppPluginObject const* obj,
    int* out_world_x,
    int* out_world_z,
    int* out_world_y)
{
    int scene_x;
    int scene_z;

    assert(app);
    assert(obj);
    assert(out_world_x);
    assert(out_world_z);
    assert(out_world_y);

    if( !app->world )
        return 0;
    scene_x = obj->tile_x - app->world->_base_tile_x;
    scene_z = obj->tile_z - app->world->_base_tile_z;
    if( scene_x < 0 || scene_z < 0 || scene_x >= app->world->_scene_size ||
        scene_z >= app->world->_scene_size )
        return 0;

    *out_world_x = scene_x * 128 + 64;
    *out_world_z = scene_z * 128 + 64;
    /* World y is negative-up, so subtracting `height` raises the model off the
     * ground -- the same arithmetic a map spotanim's height uses. */
    *out_world_y = app_world_height(app, *out_world_x, *out_world_z, obj->level) - obj->height;
    return 1;
}

/* Build the element and hand it to World. SYNCHRONOUS -- the model and seq
 * must be resident. */
static void
app_plugin_object_materialize_now(
    struct App* app,
    int handle)
{
    struct AppPluginObject* obj = app_plugin_object_at(app, handle);
    struct ToriDraw_Model* model;
    int world_x;
    int world_z;
    int world_y;
    int element_id;
    int seq_id;

    assert(app);
    if( !obj )
        return; /* destroyed while its load was in flight */
    obj->load_pending = 0;
    if( obj->element_id >= 0 )
        return; /* a second task raced ahead of this one */
    if( !app_plugin_object_scene_pos(app, obj, &world_x, &world_z, &world_y) )
        return;

    model = app_plugin_object_build_model(app, obj);
    if( !model )
        return;

    element_id = app_world_scene_element_create(
        app, TORIDRAW_ELEMENT_KIND_NONE, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return;
    /* The element carries the yaw, not the entity: the painter is handed an
     * element id and reads the orientation off it, so an object whose yaw
     * lived only on the WorldEntity stood in its bind orientation forever --
     * a documented parameter that turned nothing. */
    ToriDraw_SceneElementSetPosition(app->scene, element_id, world_x, world_y, world_z, obj->yaw);

    obj->element_id = element_id;
    obj->built_source = obj->source;
    obj->built_model_id = obj->model_id;
    obj->built_recolor_stamp = app_plugin_object_recolor_stamp(obj);
    obj->built_geometry_revision = app_plugin_object_geometry_revision(app, obj);
    obj->world_index = World_PluginObjectSpawn(
        app->world,
        element_id,
        obj->level,
        world_x,
        world_z,
        world_y,
        obj->yaw,
        /*size_x=*/1,
        /*size_z=*/1);
    World_PluginObjectSetActive(app->world, obj->world_index, obj->active != 0);

    seq_id = app_plugin_object_seq_id(app, obj);
    if( seq_id >= 0 )
    {
        /* Before the bind, not after: the flag is read by the per-element tick
         * from the first cycle the sequence advances, and a beam that plays
         * once and freezes on its terminal frame is the whole difference
         * between an idle loop and a one-shot graphic. */
        ToriDraw_SceneElementSetAnimLoop(app->scene, element_id, obj->loop != 0);
        app_world_apply_seq(app, element_id, seq_id);
    }

    app_sync_textures(app);
    app->need_redraw = 1;
}

/* Async spawn driver for the three debug hotkeys: awaits the cache loads the
 * synchronous spawn bodies assume, then runs them. Enqueued on the serial
 * exec pipeline so spawns interleave cleanly with packet exec + mounts. */
/* Defined with the other world-entity appliers, far below; the LOC_ANIM task
 * body needs it here. */
static void
app_world_scenery_anim_apply(
    struct App* app,
    struct World* world,
    int scene_x,
    int scene_z,
    int level,
    int loc_shape,
    int seq_id);

enum AppSpawnKind
{
    APP_SPAWN_PLAYER = 0,
    APP_SPAWN_NPC,
    APP_SPAWN_PROJECTILE,
    APP_SPAWN_PROJECTILE_SPOT,
    APP_SPAWN_OBJ,
    APP_SPAWN_SPOTANIM,
    APP_SPAWN_ENTITY_SPOTANIM,
    APP_SPAWN_LOC_CHANGE,
    APP_SPAWN_LOC_ANIM,
    APP_SPAWN_PLUGIN_OBJECT,
};

struct Task_AppSpawn
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    /* The worldview cursor AS OF ENQUEUE (app->active_world). Spawn tasks run
     * at the END of the exec FIFO, behind later packets — including the
     * SERVER_TICK_END that resets the live cursor — so the apply cannot read
     * app->active_world when it finally runs; it must carry its own copy. The
     * view can legitimately die while the task is parked (a boat despawns
     * mid-flight), so applies guard on WorldviewRegistry_IsLive rather than
     * asserting. */
    int view;
    enum AppSpawnKind kind;
    int tile_x;
    int tile_z;
    int level;
    int npc_id;
    int obj_id;
    int model_id;
    int seq_id;
    int src_tile_x;
    int src_tile_z;
    int src_level;
    int model_i;
    int spotanim_id;
    int spotanim_height;
    int spotanim_delay;
    /* APP_SPAWN_ENTITY_SPOTANIM: the body scene element of the player/npc the
     * attached graphic belongs to (stable, scene-unique key to re-find the live
     * entity when the async load lands). */
    int entity_element_id;
    /* APP_SPAWN_LOC_CHANGE: the placement's own right-click menu, carried
     * across the async model wait because the scenery entity it lands on does
     * not exist until then. `loc_op_flags` is the 5-bit shown mask and
     * `loc_ops[i]` the replacement label for slot i ("" = keep the loctype's).
     * See App_WorldLocChangeOps. */
    int loc_op_flags;
    char loc_ops[5][32];
    /* MAP_PROJANIM (spotanim-based projectile) trajectory params. Source and
     * destination tiles reuse src_tile_x/z and tile_x/z; src_level and level
     * carry the source and destination levels. */
    int proj_src_height;
    int proj_dst_height;
    int proj_start_delay;
    int proj_end_delay;
    int proj_peak;
    int proj_arc;
    int proj_target;
    /* APP_SPAWN_LOC_CHANGE (zone LOC_ADD_CHANGE / LOC_DEL): the replacement loc
     * (-1 = pure delete), its map shape/angle, and the nested model-list cursor
     * (loc models are [shape_entry][model]; both indices must survive awaits). */
    int loc_id;
    int loc_shape;
    int loc_angle;
    int loc_model_j;
    int loc_resolved_id;
    int loc_resolve_depth;
    int loc_base_seq;
    /** APP_SPAWN_LOC_CHANGE: the loc's models and sequence, fanned out and not yet ended. */
    int pending;
    /* APP_SPAWN_PLUGIN_OBJECT: the plugin object handle whose assets this task
     * is waiting on. Not a pointer -- the record can be destroyed while the
     * task is parked, and the handle re-resolves to NULL rather than to freed
     * memory. */
    int plugin_object;
};

/*
 * Apply a LOC_ADD_CHANGE_V2 placement menu onto the scenery entity the change
 * just spawned.
 *
 * Order is the reference's (deob class108), and each step matters:
 *
 *   1. a slot the mask clears is GONE, whatever either side calls it. The
 *      reference `continue`s before it has read a label at all, so a swung
 *      door does not keep offering "Open" beside its "Close".
 *   2. a replacement label wins over the loctype's, and wins on a slot the
 *      loctype left EMPTY too — which is the whole mechanism: it is how a
 *      single cache record grows an option it never declared.
 *
 * `code` is left alone. It is the op slot the click reports, and the override
 * renames a row rather than moving it.
 */
static void
app_loc_change_apply_ops(
    struct App* app,
    struct World* world,
    const struct Task_AppSpawn* self)
{
    int idx;
    struct WorldEntity_Scenery* sc;

    assert(app);
    (void)app; /* asserted only; unused under NDEBUG */
    assert(world);
    assert(self);
    if( self->loc_id < 0 )
        return; /* a pure delete has no placement to describe */
    idx = World_SceneryFindAt(world, self->tile_x, self->tile_z, self->level, self->loc_shape);
    if( idx < 0 )
        return; /* the spawn was refused (unknown loc, off-scene) — nothing to dress */
    sc = World_EntityPoolGet(&world->entities.scenery, idx);
    if( !sc )
        return;

    /* The label block is shared, so the override is not written in place: the
     * edited copy is interned and the placement repointed. Every slot is
     * rewritten from zero rather than truncated with a NUL, because entries are
     * compared byte for byte and a shortened name that left its old tail behind
     * would intern as a second, identical-looking label. */
    {
        struct WorldEntity_SceneryInfo probe = *sc->info;
        int has_action = 0;

        sc->placement_op_mask = (uint8_t)self->loc_op_flags;
        sc->placement_op_overrides = 0;
        for( int i = 0; i < 5; i++ )
        {
            if( !(self->loc_op_flags & (1 << i)) || self->loc_ops[i][0] )
                sc->placement_op_overrides |= (uint8_t)(1 << i);
            char const* label = NULL;

            if( (self->loc_op_flags & (1 << i)) == 0 )
                label = "";
            else if( self->loc_ops[i][0] != '\0' )
                label = self->loc_ops[i];
            if( label )
            {
                memset(probe.actions[i].name, 0, sizeof(probe.actions[i].name));
                snprintf(probe.actions[i].name, sizeof(probe.actions[i].name), "%s", label);
            }
            if( probe.actions[i].name[0] != '\0' )
                has_action = 1;
        }
        sc->info = World_SceneryInfoIntern(world, &probe);
        /* A placement the server gave a MENU is clickable by definition —
         * that is what LOC_ADD_CHANGE_V2's op strings exist for. The
         * loctype's own `active` default can say no (the sailing masts ship
         * with no name and no cache ops), and the pick's interactive gate
         * would then refuse a loc whose whole point is its one op. */
        if( has_action )
            sc->interactive = 1;
    }
}

static int
Task_AppSpawn_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppSpawn* self = (struct Task_AppSpawn*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    if( self->kind == APP_SPAWN_PLAYER )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider));
        PT_TASK_AWAITSELF_IF(
            CreateTask_SequenceLoad(app->provider, app->scene, APP_PLAYER_SEQ_READY));
        app_world_spawn_player_now(app, self->tile_x, self->tile_z, self->level);
    }
    else if( self->kind == APP_SPAWN_NPC )
    {
        /* npc_id is the requested/wrapper id; model_id temporarily carries
         * this player's selected child so developer/content spawns follow the
         * same multiNpc path as NPC_INFO. */
        PT_TASK_AWAITSELF_IF(CreateTask_NpcMultiLoad(app, self->npc_id, &self->model_id));
        {
            int effective = self->model_id >= 0 ? self->model_id : self->npc_id;
            int idx = app_world_spawn_npc_now(
                app, effective, self->npc_id, self->tile_x, self->tile_z, self->level);
            struct WorldEntity_NPC* npc =
                idx >= 0 ? World_EntityPoolGet(&app->world->entities.npc, idx) : NULL;
            if( npc )
            {
                npc->base_npc_id = self->npc_id;
                npc->multinpc_hidden = self->model_id < 0;
            }
        }
    }
    else if( self->kind == APP_SPAWN_OBJ )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(app->provider, self->obj_id));
        {
            struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, self->obj_id);
            if( obj && obj->inventory_model_id > 0 )
                self->model_id = obj->inventory_model_id;
        }
        if( self->model_id > 0 )
        {
            PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
            /* The applicator resolves the cursor itself, so re-arm it with the
             * view captured at enqueue for the duration of the call. The view
             * can have died while the task was parked (a despawned boat takes
             * its late drops with it) — that is the guard, not a contract. */
            if( WorldviewRegistry_IsLive(&app->worldviews, self->view) )
            {
                int prev_view = app->active_world;
                app->active_world = self->view;
                App_WorldObjStackAdd(app, self->tile_x, self->tile_z, self->level, self->obj_id, 1);
                app->active_world = prev_view;
            }
        }
        else
            TORIRS_LOG("spawn_obj: obj %d has no inventory model\n", self->obj_id);
    }
    else if( self->kind == APP_SPAWN_SPOTANIM )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_SpotanimLoad(app->provider, self->spotanim_id));
        {
            struct ToriRS_Spotanimtype* spot =
                CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
            self->model_id = (spot && spot->model > 0) ? spot->model : -1;
        }
        if( self->model_id > 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
        {
            struct ToriRS_Spotanimtype* spot =
                CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
            self->seq_id = spot ? spot->seq : -1;
        }
        if( self->seq_id >= 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id));
        /* Guarded, not asserted: the captured view can die while the task is
         * parked, and a late effect on a despawned boat is simply dropped. */
        if( WorldviewRegistry_IsLive(&app->worldviews, self->view) )
            app_world_spawn_spotanim_now(
                app,
                WorldviewRegistry_Get(&app->worldviews, self->view)->world,
                self->spotanim_id,
                self->tile_x,
                self->tile_z,
                self->level,
                self->spotanim_height,
                self->spotanim_delay);
    }
    else if( self->kind == APP_SPAWN_ENTITY_SPOTANIM )
    {
        /* Attached graphic (SPOTANIM mask): load the same asset chain as the
         * free-standing spotanim. No completion callback — once resident,
         * app_world_sync_entity_spotanims combines synchronously next frame. */
        PT_TASK_AWAITSELF_IF(CreateTask_SpotanimLoad(app->provider, self->spotanim_id));
        {
            struct ToriRS_Spotanimtype* spot =
                CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
            self->model_id = (spot && spot->model > 0) ? spot->model : -1;
        }
        if( self->model_id > 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
        {
            struct ToriRS_Spotanimtype* spot =
                CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
            self->seq_id = spot ? spot->seq : -1;
        }
        if( self->seq_id >= 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id));
        app->need_redraw = 1;
    }
    else if( self->kind == APP_SPAWN_PROJECTILE_SPOT )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_SpotanimLoad(app->provider, self->spotanim_id));
        {
            struct ToriRS_Spotanimtype* spot =
                CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
            self->model_id = (spot && spot->model > 0) ? spot->model : -1;
        }
        if( self->model_id > 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
        {
            struct ToriRS_Spotanimtype* spot =
                CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
            self->seq_id = spot ? spot->seq : -1;
        }
        if( self->seq_id >= 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id));
        /* Guarded, not asserted: see the spotanim branch. */
        if( WorldviewRegistry_IsLive(&app->worldviews, self->view) )
            app_world_spawn_projectile_spot_now(
                app,
                WorldviewRegistry_Get(&app->worldviews, self->view)->world,
                self->spotanim_id,
                self->src_tile_x,
                self->src_tile_z,
                self->src_level,
                self->tile_x,
                self->tile_z,
                self->level,
                self->proj_src_height,
                self->proj_dst_height,
                self->proj_start_delay,
                self->proj_end_delay,
                self->proj_peak,
                self->proj_arc,
                self->proj_target);
    }
    else if( self->kind == APP_SPAWN_PLUGIN_OBJECT )
    {
        /*
         * Same asset chain as a spotanim, resolved through whichever source
         * the object named. Every step re-reads the record through its handle
         * rather than caching a pointer across the awaits: a plugin may have
         * destroyed the object, or restated its model, while this was parked.
         */
        {
            struct AppPluginObject* obj = app_plugin_object_at(app, self->plugin_object);
            self->spotanim_id =
                (obj && obj->source == TORIRS_HOST_MODEL_SPOTANIM) ? obj->model_id : -1;
        }
        if( self->spotanim_id >= 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_SpotanimLoad(app->provider, self->spotanim_id));
        {
            struct AppPluginObject* obj = app_plugin_object_at(app, self->plugin_object);
            self->model_id = obj ? app_plugin_object_model_id(app, obj) : -1;
            self->seq_id = obj ? app_plugin_object_seq_id(app, obj) : -1;
        }
        if( self->model_id >= 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
        if( self->seq_id >= 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id));
        app_plugin_object_materialize_now(app, self->plugin_object);
    }
    else if( self->kind == APP_SPAWN_LOC_ANIM )
    {
        /* Nothing to await: the point of the task is its PLACE IN THE FIFO,
         * behind any LOC_ADD_CHANGE for the same tile in the same packet. See
         * App_WorldSceneryAnim. */
        if( WorldviewRegistry_IsLive(&app->worldviews, self->view) )
            app_world_scenery_anim_apply(
                app,
                WorldviewRegistry_Get(&app->worldviews, self->view)->world,
                self->tile_x,
                self->tile_z,
                self->level,
                self->loc_shape,
                self->seq_id);
    }
    else if( self->kind == APP_SPAWN_LOC_CHANGE )
    {
        /* Reference locChangeDoQueue (Client.ts:7701): a zone loc change only
         * applies once changeLocAvailable — the loc config and every model it
         * references are resident (an open-door variant is usually absent from
         * the static map build's preload). LOC_DEL (loc_id < 0) has nothing to
         * load but still runs through this task so same-tile changes apply in
         * packet order on the serial exec FIFO. */
        if( self->loc_id >= 0 )
        {
            self->loc_resolved_id = self->loc_id;
            self->loc_base_seq = -1;
            for( self->loc_resolve_depth = 0;
                 self->loc_resolved_id >= 0 && self->loc_resolve_depth < 16;
                 ++self->loc_resolve_depth )
            {
                PT_TASK_AWAITSELF_IF(CreateTask_LocLoad(app->provider, self->loc_resolved_id));
                struct ToriRS_Location* cfg =
                    CacheProvider_LocationGet(app->provider, self->loc_resolved_id);
                if( !cfg )
                {
                    self->loc_resolved_id = -1;
                    break;
                }
                if( self->loc_resolve_depth == 0 )
                    self->loc_base_seq = cfg->seq_id;
                if( cfg->transform_count <= 0 || !cfg->transforms )
                    break;
                int next = VarPManager_ResolveTransform(
                    &app->varps,
                    cfg->transforms,
                    cfg->transform_count,
                    cfg->transform_varbit,
                    cfg->transform_varp);
                if( next == self->loc_resolved_id )
                    break;
                self->loc_resolved_id = next;
            }
            /*
             * Every model the resolved child names, and its sequence, TOGETHER: they
             * are independent reads with one consumer (the placement below),
             * and awaiting them one after another was a round trip each on a
             * streamed cache -- an open-door variant is commonly absent from
             * the map build's preload, so a door was that chain every time.
             * Queued as siblings on the asset queue and joined.
             */
            {
                struct ToriRS_Location* cfg =
                    self->loc_resolved_id >= 0
                        ? CacheProvider_LocationGet(app->provider, self->loc_resolved_id)
                        : NULL;
                int entries = 0;
                if( cfg && cfg->models && cfg->lengths )
                    entries = cfg->shapes ? cfg->shapes_and_model_count : 1;
                for( int i = 0; i < entries; i++ )
                    for( int j = 0; j < cfg->lengths[i]; j++ )
                        if( cfg->models[i][j] >= 0 )
                            ToriRS_TaskQueue_AddJoined(
                                app->runner.queue,
                                CreateTask_ModelLoad(app->provider, cfg->models[i][j]),
                                &self->pending);
                self->seq_id = cfg && cfg->seq_id >= 0 ? cfg->seq_id : self->loc_base_seq;
                if( self->seq_id >= 0 )
                    ToriRS_TaskQueue_AddJoined(
                        app->runner.queue,
                        CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id),
                        &self->pending);
            }
            PT_TASK_JOIN(pending);
        }
        /* Resolve the view captured at enqueue — the change queued behind the
         * asset waits, so the live cursor moved on long ago. IsLive is a
         * guard, not an assert: the view can legitimately die mid-flight. */
        struct Worldview* wv = WorldviewRegistry_IsLive(&app->worldviews, self->view)
                                   ? WorldviewRegistry_Get(&app->worldviews, self->view)
                                   : NULL;
        struct World* world = wv ? wv->world : NULL;
        if( wv && wv->builder && world && world->load_complete )
        {
            int old_type = -1;
            int old_angle = 0;
            int old_shape = -1;
            int old_idx = World_SceneryFindAt(
                world, self->tile_x, self->tile_z, self->level, self->loc_shape);
            if( old_idx >= 0 )
            {
                struct WorldEntity_Scenery* old =
                    World_EntityPoolGet(&world->entities.scenery, old_idx);
                if( old )
                {
                    old_type = old->loc_id;
                    old_angle = old->angle;
                    old_shape = old->shape;
                }
            }
            World_LocChangePush(
                world,
                self->level,
                World_LocShapeToLayer(self->loc_shape),
                self->tile_x,
                self->tile_z,
                old_type,
                old_angle,
                old_shape,
                self->loc_id,
                self->loc_angle,
                self->loc_shape,
                world->cycle,
                -1);
            WorldBuilder_ApplyLocChange(
                wv->builder,
                self->tile_x,
                self->tile_z,
                self->level,
                self->loc_id,
                self->loc_shape,
                self->loc_angle);
            /*
             * The placement's own menu, over the loctype's.
             *
             * After the spawn and not before it: the scenery entity is created
             * by ApplyLocChange with the loctype's actions copied in, so this
             * is the only point where both the entity and the override exist.
             * The reference does the same thing in the same order — its scene
             * loc carries the mask and the labels, and the menu builder reads
             * the loctype first and lets the placement win (deob class108).
             */
            app_loc_change_apply_ops(app, world, self);
            /*
             * The cache's own "a loc was placed" script, for a loc that arrived
             * AFTER the scene was built.
             *
             * The world-loaded sweep covers the map's own locs; this covers a
             * zone packet's, and the difference is not academic -- a Dwarf
             * multicannon is placed by the server four ticks after you click,
             * and clientscript 6672 is bound to `dwarf_multicannon1` by exactly
             * this trigger. Without it the cannon hud (All Settings row 247)
             * can never appear, because the cannon is never a loc the sweep
             * saw. See game/rs_client_trigger.h.
             *
             * After the ops, for the same reason they are applied after the
             * spawn: this is the first point at which the entity exists and is
             * finished, and the script reads its type and its coord.
             */
            if( self->loc_id >= 0 )
            {
                int const placed_idx = World_SceneryFindAt(
                    world, self->tile_x, self->tile_z, self->level, self->loc_shape);
                struct WorldEntity_Scenery* placed =
                    placed_idx >= 0 ? World_EntityPoolGet(&world->entities.scenery, placed_idx)
                                    : NULL;
                if( placed )
                    app_client_trigger_loc(app, placed, RS_TRIGGER_LOC_ADD);
            }
            app_sync_textures(app);
            app->need_redraw = 1;
        }
    }
    else
    {
        PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
        if( WorldviewRegistry_IsLive(&app->worldviews, self->view) )
            app_world_spawn_projectile_now(
                app,
                WorldviewRegistry_Get(&app->worldviews, self->view)->world,
                self->model_id,
                self->seq_id,
                self->src_tile_x,
                self->src_tile_z,
                self->src_level,
                self->tile_x,
                self->tile_z,
                self->proj_target);
    }

    PT_END(&self->pt);
}

static void
Task_AppSpawn_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_AppSpawn_VTable = {
    .run = Task_AppSpawn_Run,
    .free = Task_AppSpawn_Free,
};

/* Interface chathead: load the npctype/appearance + head models, composite the
 * head into the scene (UITreeSceneBridge_Ensure*Head), and bind it onto the
 * MODEL widget the dialogue set (reference IfType.getModel type 2/3, resolved
 * lazily — here via the async provider). */
enum AppIfHeadKind
{
    APP_IFHEAD_NPC = 0,
    APP_IFHEAD_PLAYER,
    /* IF_SETOBJECT (reference IfType model1Type 4): the obj's lit inventory
     * model bound to a MODEL widget — e.g. the combat-tab weapon. npc_id
     * carries the obj id; zoom carries the wire zoom. */
    APP_IFHEAD_OBJ,
    /* IF_SETMODEL (reference IfType model1Type 1): npc_id carries the raw
     * cache model id. */
    APP_IFHEAD_MODEL,
};

#define APP_IFHEAD_MAX_HEADS 24

struct Task_AppIfHead
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    enum AppIfHeadKind kind;
    int component_id;
    int npc_id;
    int resolved_npc_id;
    int model_i;
    int slot_i;
    int head_ids[APP_IFHEAD_MAX_HEADS]; /* player: idk + worn-obj head model ids to load */
    int head_count;
};

static int
Task_AppIfHead_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppIfHead* self = (struct Task_AppIfHead*)base;
    struct App* app = self->app;
    int scene_id = -1;

    PT_BEGIN(&self->pt);

    if( self->kind == APP_IFHEAD_NPC )
    {
        /* IF_SETNPCHEAD carries the NPC type the server is talking through.
         * For a multiNpc that is the model-less shell, just like NPC_INFO.
         * Resolve it under this client's vars before asking for chathead
         * models. The resolver also makes every selected config
         * resident, so a cold child cannot be mistaken for a terminal shell. */
        PT_TASK_AWAITSELF_IF(CreateTask_NpcMultiResolve(app, self->npc_id, &self->resolved_npc_id));
        if( self->resolved_npc_id < 0 )
            PT_EXIT(&self->pt); /* positional -1: intentionally hidden */
        /* Load each head model (re-derived from persistent model_i; -1 slots
         * are skipped — reference NpcType.getHead ignores them). */
        for( self->model_i = 0;; self->model_i++ )
        {
            struct ToriRS_Npctype* npc =
                CacheProvider_NpctypeGet(app->provider, self->resolved_npc_id);
            if( !npc || self->model_i >= npc->heads_count )
                break;
            if( npc->heads[self->model_i] < 0 )
                continue;
            PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, npc->heads[self->model_i]));
        }
        scene_id = UITreeSceneBridge_EnsureNpcHead(&app->bridge, self->resolved_npc_id);
    }
    else if( self->kind == APP_IFHEAD_OBJ )
    {
        /* IF_SETOBJECT: objtype + its inventory model, then the lit interface
         * model (npc_id carries the obj id). */
        PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(app->provider, self->npc_id));
        {
            struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, self->npc_id);
            if( obj && obj->inventory_model_id > 0 )
                PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, obj->inventory_model_id));
        }
        scene_id = UITreeSceneBridge_EnsureObjModel(&app->bridge, self->npc_id);
    }
    else if( self->kind == APP_IFHEAD_MODEL )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->npc_id));
        scene_id = UITreeSceneBridge_EnsureModel(&app->bridge, self->npc_id);
    }
    else
    {
        /* Load the local player's real-appearance idk configs + head models,
         * then composite (reference ClientPlayer.getHeadModel). The idk configs
         * are usually already resident from the world body build; await the
         * appearance load first as a baseline. */
        PT_TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider));
        /* Ensure worn-equipment obj configs are resident so their head-model
         * ids (manhead/womanhead) can be gathered below — the body build loads
         * the wear models but never the head models (reference getHeadModel
         * pulls ObjType.getHeadModelNoCheck for slots >= 512). */
        for( self->slot_i = 0; self->slot_i < 12; self->slot_i++ )
        {
            struct WorldEntity_Player* lp = app_local_player(app);
            int slot = lp ? lp->appearance.slots[self->slot_i] : 0;
            if( Appearance_SlotKind(slot) == APPEARANCE_SLOT_OBJ )
                PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(app->provider, Appearance_SlotObj(slot)));
        }
        {
            struct WorldEntity_Player* lp = app_local_player(app);
            self->head_count = lp ? PlayerHeadModel_CollectHeadModelIds(
                                        app->provider,
                                        lp->appearance.slots,
                                        lp->gender,
                                        self->head_ids,
                                        APP_IFHEAD_MAX_HEADS)
                                  : 0;
        }
        for( self->model_i = 0; self->model_i < self->head_count; self->model_i++ )
            PT_TASK_AWAITSELF_IF(
                CreateTask_ModelLoad(app->provider, self->head_ids[self->model_i]));
        {
            struct WorldEntity_Player* lp = app_local_player(app);
            if( lp )
                scene_id = UITreeSceneBridge_EnsurePlayerHead(
                    &app->bridge, lp->appearance.slots, lp->appearance.colors, lp->gender);
        }
    }

    /* Compose only — app_if_head_poll binds the scene model onto the MODEL node
     * once it is mounted (the head packet usually precedes the interface). Force
     * a redraw so the poll runs now that the head is composited. */
    if( scene_id >= 0 )
        app->need_redraw = 1;
    else if( torirs_env_net_debug() )
        TORIRS_LOG(
            "if-head: component=0x%x kind=%d could not composite head (npc=%d)\n",
            (unsigned)self->component_id,
            (int)self->kind,
            self->npc_id);

    PT_END(&self->pt);
}

static void
Task_AppIfHead_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_AppIfHead_VTable = {
    .run = Task_AppIfHead_Run,
    .free = Task_AppIfHead_Free,
};

static void
app_if_head_enqueue(
    struct App* app,
    enum AppIfHeadKind kind,
    int component_id,
    int npc_id)
{
    struct Task_AppIfHead* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppIfHead_VTable;
    strncpy(task->task.name, "AppIfHead", sizeof(task->task.name) - 1);
    task->app = app;
    task->kind = kind;
    task->component_id = component_id;
    task->npc_id = npc_id;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Persist the head request keyed by component id (reference IfType.list keeps
 * model1Type/model1Id): re-applied by app_if_head_poll whenever the interface
 * (re)mounts, so it survives a head packet that lands before its chat interface
 * exists. Resets applied_gen so the next poll rebinds. */
static void
app_if_head_store(
    struct App* app,
    enum AppIfHeadKind kind,
    int com_id,
    int npc_id)
{
    int i;
    for( i = 0; i < app->if_head_count; i++ )
        if( app->if_heads[i].com_id == com_id )
            break;
    if( i == app->if_head_count )
    {
        if( app->if_head_count == app->if_head_cap )
        {
            int cap = app->if_head_cap ? app->if_head_cap * 2 : 16;
            app->if_heads = realloc(app->if_heads, (size_t)cap * sizeof(*app->if_heads));
            assert(app->if_heads);
            app->if_head_cap = cap;
        }
        app->if_heads[i].anim_id = -1; /* preserved across a head update (below) */
        app->if_head_count++;
    }
    app->if_heads[i].com_id = com_id;
    app->if_heads[i].kind = (int)kind;
    app->if_heads[i].npc_id = npc_id;
    app->if_heads[i].zoom = 0;
    app->if_heads[i].applied_gen = 0;
    app->need_redraw = 1;
}

/* ---- Revision-239 per-widget player compositions ----------------------- */

enum AppIfPlayerModelOp
{
    APP_IFPLAYER_SELF = 0,
    APP_IFPLAYER_BASECOLOUR,
    APP_IFPLAYER_BODYTYPE,
    APP_IFPLAYER_OBJ,
};

enum
{
    APP_IFPLAYER_MAX_MODELS = 64,
};

/* PlayerComposition's seven design-part -> equipment-slot table
 * (Statics.method8884 / class389.field4882 in the 239 client). */
static int const app_ifplayer_design_slots[PLAYER_APPEARANCE_PARTS] = {
    8, 11, 4, 6, 9, 7, 10,
};

static struct AppIfPlayerModel*
app_if_player_model_find(
    struct App* app,
    int com_id)
{
    for( int i = 0; i < app->if_player_model_count; i++ )
        if( app->if_player_models[i].com_id == com_id )
            return &app->if_player_models[i];
    return NULL;
}

/* Modern IfType creates its PlayerComposition by cloning the local player.
 * Keep both arrays: slots is the effective render layer, while identkit is the
 * body-under-equipment layer SELF(false) and BODYTYPE restore from. */
static struct AppIfPlayerModel*
app_if_player_model_get(
    struct App* app,
    int com_id)
{
    struct AppIfPlayerModel* model = app_if_player_model_find(app, com_id);
    struct WorldEntity_Player* lp;
    int i;

    if( model )
        return model;
    lp = app_local_player(app);
    if( !lp )
        return NULL;

    i = app->if_player_model_count;
    if( i == app->if_player_model_cap )
    {
        int cap = app->if_player_model_cap ? app->if_player_model_cap * 2 : 8;
        app->if_player_models =
            realloc(app->if_player_models, (size_t)cap * sizeof(*app->if_player_models));
        assert(app->if_player_models);
        app->if_player_model_cap = cap;
    }
    model = &app->if_player_models[i];
    memset(model, 0, sizeof(*model));
    model->com_id = com_id;
    model->scene_id = UITREE_SCENE_IF_PLAYER_MODEL_BASE + i;
    model->anim_id = -1;
    memcpy(model->slots, lp->appearance.slots, sizeof(model->slots));
    memcpy(model->identkit, lp->appearance.identkit, sizeof(model->identkit));
    memcpy(model->colors, lp->appearance.colors, sizeof(model->colors));
    model->gender = lp->gender;
    app->if_player_model_count++;
    return model;
}

static int
app_if_player_model_find_kit(
    struct App* app,
    int design_part,
    int body_type)
{
    /* IdkType.method8644(part, gender): female body-part ids are male + 7;
     * every other body type selects the male band. The reference takes the
     * first selectable id in config order. */
    int body_part_id = design_part + (body_type == 1 ? PLAYER_APPEARANCE_PARTS : 0);
    for( int id = 0; id < PLAYER_IDK_SCAN_MAX; id++ )
    {
        struct ToriRS_Idk* idk;
        if( !CacheProvider_IdkHas(app->provider, id) )
            break;
        idk = CacheProvider_IdkGet(app->provider, id);
        if( idk && !idk->not_selectable && idk->body_part_id == body_part_id )
            return id;
    }
    return -1;
}

struct Task_AppIfPlayerModel
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    enum AppIfPlayerModelOp op;
    int component_id;
    int arg0;
    int arg1;
    int cfg_i;
    int model_i;
    int model_count;
    int model_ids[APP_IFPLAYER_MAX_MODELS];
    int slots[12];
    int colors[5];
    int gender;
    uint32_t version;
};

static int
Task_AppIfPlayerModel_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppIfPlayerModel* self = (struct Task_AppIfPlayerModel*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    /* BODYTYPE scans the complete idk table; OBJ needs its wearpos triplet.
     * The task sits on the serial packet executor, preserving wire order while
     * either config load yields. */
    if( self->op == APP_IFPLAYER_BODYTYPE )
        PT_TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider));
    else if( self->op == APP_IFPLAYER_OBJ )
        PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(app->provider, self->arg0));

    {
        struct AppIfPlayerModel* model = app_if_player_model_get(app, self->component_id);
        struct WorldEntity_Player* lp = app_local_player(app);
        if( !model || !lp )
            PT_EXIT(&self->pt);

        if( self->op == APP_IFPLAYER_SELF )
        {
            memcpy(model->identkit, lp->appearance.identkit, sizeof(model->identkit));
            memcpy(model->colors, lp->appearance.colors, sizeof(model->colors));
            model->gender = lp->gender;
            memcpy(
                model->slots,
                self->arg0 ? lp->appearance.slots : lp->appearance.identkit,
                sizeof(model->slots));
        }
        else if( self->op == APP_IFPLAYER_BASECOLOUR )
        {
            if( self->arg0 >= 0 && self->arg0 < 5 )
                model->colors[self->arg0] = self->arg1;
        }
        else if( self->op == APP_IFPLAYER_BODYTYPE )
        {
            int body_type = self->arg0;
            if( model->gender != body_type )
            {
                model->gender = body_type;
                for( int part = 0; part < PLAYER_APPEARANCE_PARTS; part++ )
                {
                    int slot = app_ifplayer_design_slots[part];
                    /* PlayerComposition only remaps a design-kit value. Worn
                     * objs remain exactly where they are. Returning to the
                     * local player's type restores the cloned underneath kit;
                     * switching away takes the first selectable target kit. */
                    if( Appearance_SlotKind(model->slots[slot]) != APPEARANCE_SLOT_KIT )
                        continue;
                    if( body_type == lp->gender )
                    {
                        model->slots[slot] = model->identkit[slot];
                    }
                    else
                    {
                        int id = app_if_player_model_find_kit(app, part, body_type);
                        if( id >= 0 )
                            model->slots[slot] = Appearance_PackKit(id);
                    }
                }
            }
        }
        else
        {
            struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, self->arg0);
            if( obj && obj->wearpos >= 0 && obj->wearpos < 12 )
            {
                model->slots[obj->wearpos] = Appearance_PackObj(self->arg0);
                if( obj->wearpos2 >= 0 && obj->wearpos2 < 12 )
                    model->slots[obj->wearpos2] = 0;
                if( obj->wearpos3 >= 0 && obj->wearpos3 < 12 )
                    model->slots[obj->wearpos3] = 0;
            }
        }

        model->version++;
        if( model->version == 0 )
            model->version = 1;
        self->version = model->version;
        memcpy(self->slots, model->slots, sizeof(self->slots));
        memcpy(self->colors, model->colors, sizeof(self->colors));
        self->gender = model->gender;
    }

    /* Resolve all configs referenced by the snapshot before collecting model
     * ids. PlayerAppearanceLoad supplies the full idk table; worn objects are
     * loaded individually, then every gendered wear model is awaited. */
    PT_TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider));
    for( self->cfg_i = 0; self->cfg_i < 12; self->cfg_i++ )
    {
        if( Appearance_SlotKind(self->slots[self->cfg_i]) == APPEARANCE_SLOT_OBJ )
            PT_TASK_AWAITSELF_IF(
                CreateTask_ObjLoad(app->provider, Appearance_SlotObj(self->slots[self->cfg_i])));
    }
    self->model_count = PlayerModel_CollectAppearanceModelIds(
        app->provider, self->slots, self->gender, self->model_ids, APP_IFPLAYER_MAX_MODELS);
    for( self->model_i = 0; self->model_i < self->model_count; self->model_i++ )
        PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_ids[self->model_i]));

    {
        struct AppIfPlayerModel* model = app_if_player_model_find(app, self->component_id);
        /* A stale build must never overwrite a newer composition. This is
         * mostly defensive—the packet queue is serial—but also makes direct
         * harness calls deterministic. */
        if( model && model->version == self->version &&
            UITreeSceneBridge_BuildInterfacePlayerModel(
                &app->bridge, model->scene_id, self->slots, self->colors, self->gender) >= 0 )
        {
            model->built_version = self->version;
            model->applied_gen = 0;
            app->need_redraw = 1;
        }
    }

    PT_END(&self->pt);
}

static void
Task_AppIfPlayerModel_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_AppIfPlayerModel_VTable = {
    .run = Task_AppIfPlayerModel_Run,
    .free = Task_AppIfPlayerModel_Free,
};

static void
app_if_player_model_enqueue(
    struct App* app,
    enum AppIfPlayerModelOp op,
    int component_id,
    int arg0,
    int arg1)
{
    struct Task_AppIfPlayerModel* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppIfPlayerModel_VTable;
    strncpy(task->task.name, "AppIfPlayerModel", sizeof(task->task.name) - 1);
    task->app = app;
    task->op = op;
    task->component_id = component_id;
    task->arg0 = arg0;
    task->arg1 = arg1;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

void
App_SetInterfaceNpcHead(
    struct App* app,
    int component_id,
    int npc_id)
{
    assert(app);
    if( npc_id < 0 )
        return;
    app_if_head_store(app, APP_IFHEAD_NPC, component_id, npc_id);
    app_if_head_enqueue(app, APP_IFHEAD_NPC, component_id, npc_id);
}

void
App_SetInterfacePlayerHead(
    struct App* app,
    int component_id)
{
    assert(app);
    app_if_head_store(app, APP_IFHEAD_PLAYER, component_id, -1);
    app_if_head_enqueue(app, APP_IFHEAD_PLAYER, component_id, -1);
}

void
App_SetInterfacePlayerModelSelf(
    struct App* app,
    int component_id,
    int copy_objs)
{
    assert(app);
    app_if_player_model_enqueue(app, APP_IFPLAYER_SELF, component_id, copy_objs != 0, 0);
}

void
App_SetInterfacePlayerModelBaseColour(
    struct App* app,
    int component_id,
    int index,
    int colour)
{
    assert(app);
    app_if_player_model_enqueue(app, APP_IFPLAYER_BASECOLOUR, component_id, index, colour);
}

void
App_SetInterfacePlayerModelBodyType(
    struct App* app,
    int component_id,
    int body_type)
{
    assert(app);
    app_if_player_model_enqueue(app, APP_IFPLAYER_BODYTYPE, component_id, body_type, 0);
}

void
App_SetInterfacePlayerModelObj(
    struct App* app,
    int component_id,
    int obj_id)
{
    assert(app);
    if( obj_id < 0 )
        return;
    app_if_player_model_enqueue(app, APP_IFPLAYER_OBJ, component_id, obj_id, 0);
}

void
App_SetInterfaceModel(
    struct App* app,
    int component_id,
    int model_id)
{
    assert(app);
    if( model_id < 0 )
        return;
    app_if_head_store(app, APP_IFHEAD_MODEL, component_id, model_id);
    app_if_head_enqueue(app, APP_IFHEAD_MODEL, component_id, model_id);
}

void
App_SetInterfaceObjModel(
    struct App* app,
    int component_id,
    int obj_id,
    int zoom)
{
    assert(app);
    if( obj_id <= 0 )
        return;
    app_if_head_store(app, APP_IFHEAD_OBJ, component_id, obj_id);
    /* store resets zoom to 0; stamp the wire zoom for the poll's angle apply. */
    for( int i = 0; i < app->if_head_count; i++ )
        if( app->if_heads[i].com_id == component_id )
        {
            app->if_heads[i].zoom = zoom;
            break;
        }
    app_if_head_enqueue(app, APP_IFHEAD_OBJ, component_id, obj_id);
}

/* Bind any composited heads onto their MODEL nodes. Runs each redraw: an entry
 * applies once its scene model is ready (the load task has composited it) AND
 * its component is mounted, then only re-applies when the tree generation
 * changes (remount/rebuild) — mirroring the reference resolving getModel every
 * draw. Cheap: Ensure* is a cache hit after the first composite, and applied
 * entries at the current generation are skipped. */
static void
app_if_head_poll(struct App* app)
{
    if( app->if_head_count == 0 || !app->tree )
        return;

    for( int i = 0; i < app->if_head_count; i++ )
    {
        struct AppIfHead* head = &app->if_heads[i];
        int scene_id;
        int first_apply;

        if( head->applied_gen == app->tree->generation )
            continue;
        /* UI mutations can advance the tree generation every tick.  The
         * binding still re-applies as required, but its diagnostic should say
         * when this stored request first became visible to the tree, not flood
         * the trace once per unrelated UI mutation. */
        first_apply = head->applied_gen == 0;

        if( head->kind == APP_IFHEAD_PLAYER )
        {
            struct WorldEntity_Player* lp = app_local_player(app);
            scene_id =
                lp ? UITreeSceneBridge_EnsurePlayerHead(
                         &app->bridge, lp->appearance.slots, lp->appearance.colors, lp->gender)
                   : -1;
        }
        else if( head->kind == APP_IFHEAD_OBJ )
        {
            scene_id = UITreeSceneBridge_EnsureObjModel(&app->bridge, head->npc_id);
        }
        else if( head->kind == APP_IFHEAD_MODEL )
        {
            scene_id = UITreeSceneBridge_EnsureModel(&app->bridge, head->npc_id);
        }
        else
        {
            /* Keep the stored id as the shell, matching IfType.model1Id, but
             * resolve it every time the binding is retried. The selected child
             * owns the actual heads/recolours and is the bridge cache key. */
            int resolved_npc_id = App_NpctypeResolveMultiId(app, head->npc_id);
            scene_id = resolved_npc_id < 0
                           ? -1
                           : UITreeSceneBridge_EnsureNpcHead(&app->bridge, resolved_npc_id);
        }
        if( scene_id < 0 )
            continue; /* assets not composited yet — retry next frame */

        if( UITree_ApplyModel(app->tree, head->com_id, scene_id) )
        {
            /* Server IF_SETNPCHEAD reaches this async App path rather than the
             * CS2 host opcode path, so mirror the latter's opt-in trace here.
             * It makes a composed-but-currently-tab-hidden portrait observable
             * without changing its render or visibility state. */
            if( first_apply && head->kind == APP_IFHEAD_NPC && getenv("TORIRS_NPC_HEAD_DEBUG") )
                TORIRS_LOG(
                    "npc_head: npc=%d component=0x%08x scene=%d applied=1\n",
                    head->npc_id,
                    (unsigned)head->com_id,
                    scene_id);
            /* Reference IF_SETOBJECT: modelXAn/YAn from the objtype, modelZoom
             * = zoom2d * 100 / wire zoom (Client.ts:6342). */
            if( head->kind == APP_IFHEAD_OBJ )
            {
                struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, head->npc_id);
                if( obj && head->zoom > 0 )
                    UITree_ApplyModelAngle(
                        app->tree,
                        head->com_id,
                        obj->xan2d,
                        obj->yan2d,
                        (obj->zoom2d > 0 ? obj->zoom2d : 2000) * 100 / head->zoom);
            }
            if( head->anim_id >= 0 )
                UITree_ApplyModelAnim(app->tree, head->com_id, head->anim_id);
            head->applied_gen = app->tree->generation;
        }
        else if( torirs_env_net_debug() )
            TORIRS_LOG(
                "if-head: reapply com=%d npc=%d gen=%u missed (node not mounted?)\n",
                head->com_id,
                head->npc_id,
                app->tree->generation);
    }
}

/* Bind completed per-widget compositions after the target interface mounts,
 * and again after every tree rebuild. The composition stays in its own scene
 * slot; applying it only changes the addressed IfType. */
static void
app_if_player_model_poll(struct App* app)
{
    if( !app->tree )
        return;
    for( int i = 0; i < app->if_player_model_count; i++ )
    {
        struct AppIfPlayerModel* model = &app->if_player_models[i];
        if( model->built_version == 0 || model->built_version != model->version ||
            model->applied_gen == app->tree->generation )
            continue;
        if( UITree_ApplyModel(app->tree, model->com_id, model->scene_id) )
        {
            if( model->anim_id >= 0 )
                UITree_ApplyModelAnim(app->tree, model->com_id, model->anim_id);
            model->applied_gen = app->tree->generation;
        }
        else if( torirs_env_net_debug() )
            TORIRS_LOG(
                "if-player-model: reapply com=%d scene=%d gen=%u missed\n",
                model->com_id,
                model->scene_id,
                app->tree->generation);
    }
}

/*
 * Bind clientCode-328 MODEL widgets (the equipment-stats figure) to the LIVE
 * local player.
 *
 * The bake path (uitree_builder_bake / task_interface_open) composites a default
 * avatar for these nodes and pins it at readyanim frame 0, because at bake time
 * there is no player yet. That default is right for the character-design preview
 * (clientCode 327, which the reference genuinely poses once) and wrong here: 328
 * names the player, so it must wear what the player wears and move like them.
 *
 * Reference is xrsps `src/ui/gl/widgets-gl.ts`, which handles 327 and 328 in one
 * block and gives them the same viewing angles:
 *
 *     const angleX = 150;
 *     const angleY = ((Math.sin(cycleCntr / 40.0) * 256.0) | 0) & 2047;
 *     const angleZ = 0;
 *
 * — 84:4 ships `angles=(0,0,0)` in the cache, so without the override the figure
 * is viewed dead-on and stands perfectly still. The xAn is what tilts the camera
 * down onto it and the yAn swings it ±256/2048 (±45°) on a ~5s period. This is
 * exactly what `RS_ClientCode_Tick` already does for 327, but that pass is gated
 * to `APP_UI_LOGIC_CS1` and rev 230 is CS2, so 328 has to get it here.
 *
 * Its animation is the player's own **movement** track, frame included, not an
 * independently-ticked idle (xrsps: `getMovementSequenceState(localServerId)`
 * feeding `sequenceId` + `liveMovementFrame`). Reading the frame off the entity
 * every tick is also what stops the figure flickering when you equip something:
 * an appearance change rebuilds the composite, and a fresh composite is
 * registered in its rest pose, so a widget running its own frame clock would
 * restart from 0 and show that rest pose. Here the very next statement in
 * app_logic_tick — UITreeAnim_Advance — poses the new model at the entity's
 * current frame, in the same tick, before anything draws it.
 *
 * Runs each tick (not each redraw): the oscillation has to keep going on a
 * frame nothing else dirtied, and marking the node dirty is what asks for the
 * redraw. Re-merging is gated on the appearance actually changing, so the
 * steady state is two memcmps.
 */
static void
app_player_model_poll(struct App* app)
{
    struct WorldEntity_Player* lp;
    int changed;
    int scene_id;
    int seq_id;
    int seq_frame;
    int yan;
    int bound = 0;

    if( !app->tree || !app->world )
        return;
    lp = app_local_player(app);
    if( !lp )
        return; /* offline / not spawned yet — the baked default avatar stands */

    changed =
        !app->player_model.built || app->player_model.gender != lp->gender ||
        memcmp(app->player_model.slots, lp->appearance.slots, sizeof(app->player_model.slots)) !=
            0 ||
        memcmp(app->player_model.colors, lp->appearance.colors, sizeof(app->player_model.colors)) !=
            0;

    if( changed )
    {
        scene_id = UITreeSceneBridge_BuildLocalPlayerModel(
            &app->bridge, lp->appearance.slots, lp->appearance.colors, lp->gender);
        if( scene_id < 0 )
            return; /* nothing composited yet — retry next frame */
        memcpy(app->player_model.slots, lp->appearance.slots, sizeof(app->player_model.slots));
        memcpy(app->player_model.colors, lp->appearance.colors, sizeof(app->player_model.colors));
        app->player_model.gender = lp->gender;
        app->player_model.built = 1;
    }
    else
    {
        scene_id = app->bridge.local_player_scene_id;
        if( scene_id < 0 )
            return;
    }

    /* The entity's movement track — the one the walk/run/idle seqs live on, and
     * the one the viewport model is playing. Its readyanim is the fallback for
     * the window between spawn and the first cycle that stamps the track. */
    if( lp->animation.secondary.anim_id != (uint16_t)-1 && lp->animation.secondary.anim_id != 0 )
    {
        seq_id = lp->animation.secondary.anim_id;
        seq_frame = lp->animation.secondary.frame;
    }
    else
    {
        seq_id = lp->idle_animations.readyanim >= 0 ? lp->idle_animations.readyanim
                                                    : APP_PLAYER_SEQ_READY;
        seq_frame = 0;
    }

    /* Reference angles (above). loop_cycle is the client cycle counter, so this
     * is the same swing the design preview gets from RS_ClientCode_Tick. */
    yan = ((int)(sin((double)app->logic_cycle / 40.0) * 256.0)) & 0x7ff;

    for( int mi = 0; mi < app->tree->client_code.count; mi++ )
    {
        int32_t i = app->tree->client_code.slots[mi];
        struct UITreeComponent* node;
        assert(i >= 0 && (uint32_t)i < app->tree->component_count);
        node = &app->tree->components[i];
        if( node->freed || node->type != UIELEM_RS_MODEL )
            continue;
        if( node->behavior.client_code != UITREE_CLIENT_CODE_LOCAL_PLAYER_MODEL )
            continue;
        int const anim_changed =
            node->u.rs_model.anim_frame != seq_frame || node->u.rs_model.anim_seq_id != seq_id;
        if( node->u.rs_model.gamecache_model_id != scene_id || node->u.rs_model.xan != 150 ||
            node->u.rs_model.yan != yan || node->u.rs_model.zan != 0 || anim_changed )
            app->need_redraw = 1;
        (void)UITree_SetModelAt(app->tree, i, scene_id);
        (void)UITree_SetModelPoseAt(
            app->tree, i, node->u.rs_model.x_offset, node->u.rs_model.y_offset, 150, yan, 0, 0);
        /* The entity owns this clock; the UI driver must not advance it again. */
        (void)UITree_SetModelAnimationAt(app->tree, i, seq_id, seq_frame, 0, 1);
        bound = 1;
    }

    if( changed && getenv("TORIRS_ANIM_DEBUG") )
        TORIRS_LOG(
            "player_model: rebuilt cycle=%llu scene=%d seq=%d frame=%d bound=%d\n",
            (unsigned long long)app->logic_cycle,
            scene_id,
            seq_id,
            seq_frame,
            bound);
}

void
App_SetInterfaceModelAnim(
    struct App* app,
    int component_id,
    int anim_id)
{
    assert(app);
    /* Persist onto a matching head entry so it re-applies with the head after a
     * (re)mount (reference modelAnim lives on the same IfType as the head), and
     * apply immediately for the already-mounted / plain-model-widget case. */
    for( int i = 0; i < app->if_head_count; i++ )
    {
        if( app->if_heads[i].com_id == component_id )
        {
            app->if_heads[i].anim_id = anim_id;
            app->if_heads[i].applied_gen = 0;
            app->need_redraw = 1;
            break;
        }
    }
    for( int i = 0; i < app->if_player_model_count; i++ )
    {
        if( app->if_player_models[i].com_id == component_id )
        {
            app->if_player_models[i].anim_id = anim_id;
            app->if_player_models[i].applied_gen = 0;
            app->need_redraw = 1;
            break;
        }
    }
    UITree_ApplyModelAnim(app->tree, component_id, anim_id);
}

static struct Task_AppSpawn*
app_spawn_task_new(
    struct App* app,
    enum AppSpawnKind kind,
    int tile_x,
    int tile_z,
    int level)
{
    struct Task_AppSpawn* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppSpawn_VTable;
    strncpy(task->task.name, "AppSpawn", sizeof(task->task.name) - 1);
    task->app = app;
    /* Single capture point for every spawn kind: the enqueue happens while the
     * addressing SET_ACTIVE_WORLD is still in force. */
    task->view = app->active_world;
    task->kind = kind;
    task->tile_x = tile_x;
    task->tile_z = tile_z;
    task->level = level;
    PT_INIT(&task->pt);
    return task;
}

/*
 * Reconcile one plugin object's live element with its intent.
 *
 * Called after every mutation and after a scene rebuild, and cheap when
 * nothing moved -- which matters, because the natural way to write a plugin is
 * to restate the whole intent every tick.
 */
static void
app_plugin_object_sync(
    struct App* app,
    int handle)
{
    struct AppPluginObject* obj = app_plugin_object_at(app, handle);
    int world_x;
    int world_z;
    int world_y;

    assert(app);
    if( !obj )
        return;

    /* A change to what the MODEL is made of cannot be applied in place. */
    if( obj->element_id >= 0 &&
        (obj->built_source != obj->source || obj->built_model_id != obj->model_id ||
         obj->built_recolor_stamp != app_plugin_object_recolor_stamp(obj) ||
         obj->built_geometry_revision != app_plugin_object_geometry_revision(app, obj)) )
        app_plugin_object_teardown(app, obj);

    /* Nothing to draw yet, or nothing to draw at all. */
    if( obj->model_id < 0 || !app->world )
        return;

    if( !app_plugin_object_scene_pos(app, obj, &world_x, &world_z, &world_y) )
    {
        /* Walked off the scene. The intent survives -- the absolute tile is
         * still meaningful and the object comes back when the scene does. */
        if( obj->element_id >= 0 )
            app_plugin_object_teardown(app, obj);
        return;
    }

    if( obj->element_id < 0 )
    {
        if( obj->load_pending )
            return;
        /* One task per object per attempt. Without the latch a plugin polling
         * an object whose model is still loading would queue a task every
         * frame, and the exec pipeline is serial. */
        obj->load_pending = 1;
        struct Task_AppSpawn* task = app_spawn_task_new(app, APP_SPAWN_PLUGIN_OBJECT, 0, 0, 0);
        task->plugin_object = handle;
        ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
        return;
    }

    /* Live: position, orientation, level and visibility are applied in place. */
    ToriDraw_SceneElementSetPosition(
        app->scene, obj->element_id, world_x, world_y, world_z, obj->yaw);
    if( obj->world_index >= 0 )
    {
        struct WorldEntity_PluginObject* we =
            World_EntityPoolGet(&app->world->entities.plugin_object, obj->world_index);
        if( we )
        {
            we->level = obj->level;
            we->orientation.yaw = (uint16_t)obj->yaw;
            we->orientation.dst_yaw = (uint16_t)obj->yaw;
            World_DrawPositionSet(&we->draw_position, world_x, world_z);
            we->draw_position.y = (uint32_t)world_y;
        }
        World_PluginObjectSetActive(app->world, obj->world_index, obj->active != 0);
    }
    app->need_redraw = 1;
}

/*
 * Every plugin object, re-placed against a scene that has just been rebuilt.
 *
 * A rebuild frees every dynamic scene element, so the live half of each record
 * is already gone by the time this runs; what is left is the intent, keyed on
 * an ABSOLUTE tile, which is exactly the thing a rebuild does not invalidate.
 * That is the whole reason the plugin contract speaks in absolute tiles.
 */
static void
app_plugin_objects_rebuild(struct App* app)
{
    assert(app);
    for( int i = 0; i < APP_PLUGIN_OBJECTS_MAX; i++ )
    {
        struct AppPluginObject* obj = &app->plugin_objects[i];
        if( !obj->in_use )
            continue;
        obj->element_id = -1;
        obj->world_index = -1;
        obj->built_source = -1;
        obj->built_model_id = -1;
        obj->built_recolor_stamp = 0;
        obj->built_geometry_revision = 0;
        obj->load_pending = 0;
        app_plugin_object_sync(app, i);
    }
}

/*
 * Objects standing on a mesh that has been re-authored, rebuilt.
 *
 * Once a frame rather than on every mesh_vertex: authoring a shape is hundreds
 * of appends, and reconciling on each of them would build and throw away
 * hundreds of models to arrive at the one the plugin meant. The dirty flag is
 * what keeps the ordinary frame -- no plugin has touched any geometry -- free.
 *
 * It is also how a SHIPPED model gets on screen at all: its bytes cross the IO
 * queue, so an object pointed at one is created before there is anything to
 * build, and the arrival is what this notices.
 */
static void
app_plugin_geometry_settle(struct App* app)
{
    assert(app);
    if( !app->plugin_geometry_dirty )
        return;
    app->plugin_geometry_dirty = 0;
    for( int i = 0; i < APP_PLUGIN_OBJECTS_MAX; i++ )
    {
        struct AppPluginObject* obj = &app->plugin_objects[i];
        if( !obj->in_use )
            continue;
        if( obj->source != TORIRS_HOST_MODEL_MESH && obj->source != TORIRS_HOST_MODEL_ASSET )
            continue;
        if( obj->element_id >= 0 &&
            obj->built_geometry_revision == app_plugin_object_geometry_revision(app, obj) )
            continue;
        app_plugin_object_sync(app, i);
    }
}

/* -------------------------------------------------------- plugin assets */

/*
 * Where a plugin's SAVED asset lives: beside plugin_prefs.ini, under a
 * directory of the plugin's own.
 *
 * Derived from the prefs path rather than declared separately so that the two
 * cannot drift: TORIRS_PLUGIN_PREFS moves the client's plugin state somewhere
 * else, and a plugin's saved files are part of that state. An empty prefs path
 * means persistence is off for this run (a headless test), and the empty
 * result it produces is what makes a read fall straight through to the shipped
 * copy and a write refuse -- neither of which should leave a file behind.
 */
static void
app_plugin_asset_saved_path(
    struct App* app,
    char const* plugin,
    char const* name,
    char* out,
    size_t out_size)
{
    char const* prefs;
    char const* slash;

    assert(app);
    assert(plugin);
    assert(name);
    assert(out);
    assert(out_size > 0);

    out[0] = '\0';
    prefs = app->plugin_prefs_path;
    if( !prefs || !*prefs )
        return;

    slash = strrchr(prefs, '/');
    if( slash )
        snprintf(
            out,
            out_size,
            "%.*s/%s/%s/%s",
            (int)(slash - prefs),
            prefs,
            PLUGIN_ASSET_SAVED_DIR,
            plugin,
            name);
    else
        snprintf(out, out_size, "%s/%s/%s", PLUGIN_ASSET_SAVED_DIR, plugin, name);
}

static int
app_plugin_asset_read(
    void* user,
    char const* plugin,
    char const* name)
{
    struct App* app = (struct App*)user;
    char saved[TORIRS_IOITEM_MAX_PATH];

    assert(app);
    assert(plugin);
    assert(name);

    if( !app->plugins )
        return 0;
    app_plugin_asset_saved_path(app, plugin, name, saved, sizeof(saved));
    ToriRS_TaskQueue_Add(
        app->runner.queue, CreateTask_PluginAssetRead(app->plugins, plugin, name, saved));
    return 1;
}

static int
app_plugin_asset_write(
    void* user,
    char const* plugin,
    char const* name,
    void const* data,
    int size)
{
    struct App* app = (struct App*)user;
    char saved[TORIRS_IOITEM_MAX_PATH];

    assert(app);
    assert(plugin);
    assert(name);
    assert(data || size == 0);

    app_plugin_asset_saved_path(app, plugin, name, saved, sizeof(saved));
    if( !saved[0] )
    {
        /* Persistence is switched off for this run. Refusing loudly rather
         * than inventing a path: a client told not to write files must not
         * start writing them because a plugin asked. */
        TORIRS_ERR(
            "plugin: %s cannot save asset '%s'; plugin persistence is off for "
            "this run (TORIRS_PLUGIN_PREFS is empty)\n",
            plugin,
            name);
        return 0;
    }
    ToriRS_TaskQueue_Add(app->runner.queue, CreateTask_PluginAssetWrite(saved, data, size));
    return 1;
}

/* ---------------------------------------------------------- screenshots */

/*
 * A plugin asked for a frame. Record it; App_RunOnce takes it.
 *
 * Nothing is rendered here, and that is the point -- see the queue's own
 * comment in app.h. Refusing loudly when the queue is full rather than
 * silently dropping the request: a plugin that fills it is asking for four
 * pictures of one instant, and the only way it finds that out is being told.
 */
/*
 * Where a capture lands, as one path.
 *
 * An absolute destination is the user's own folder and is used as given. A
 * relative one -- and that includes the common case of no destination at all
 * -- lands under the plugin's saved-asset directory, so "Bob/Levels" sorts a
 * browser run's captures the same way it sorts a desktop one. The browser lane
 * has no path to name; without this it would have no way to organise them
 * either.
 *
 * `out` is emptied when there is nowhere to write: persistence off for this
 * run AND a destination that is not absolute. That is the one case with no
 * answer, and it is an answer.
 */
static void
app_plugin_screenshot_path(
    struct App* app,
    char const* plugin,
    char const* dir,
    char const* name,
    char* out,
    size_t out_size)
{
    assert(app);
    assert(plugin);
    assert(dir);
    assert(name);
    assert(out);
    assert(out_size > 0);

    if( dir[0] == '/' )
    {
        snprintf(out, out_size, "%s/%s", dir, name);
        return;
    }

    char base[TORIRS_IOITEM_MAX_PATH];

    app_plugin_asset_saved_path(app, plugin, "", base, sizeof(base));
    if( base[0] && dir[0] )
    {
        /* asset_saved_path ends in the trailing separator plus the empty name
         * it was handed, so the slash is already there. */
        snprintf(out, out_size, "%s%s/%s", base, dir, name);
    }
    else if( base[0] )
        snprintf(out, out_size, "%s%s", base, name);
    else
        out[0] = '\0';
}

static int
app_plugin_screenshot(
    void* user,
    char const* plugin,
    char const* dir,
    char const* name,
    char* out_path,
    int out_path_size);

/*
 * Ask for a picture of the NEXT frame, from whatever is drawing it.
 *
 * This is the only capture worth the name. TORIRS_EXIT_BMP renders a fresh
 * frame with App_Render into a malloc'd buffer -- that is the SOFTWARE
 * rasteriser, whatever --d3d9-zbuffer or --gl3 says on the command line --
 * so it can never show what a GPU lane actually put on the screen, and a
 * capture taken that way is silently useless for any question about GPU
 * state. A request made here is fulfilled in App_DrawComplete out of the
 * renderer's own read-back: glReadPixels on the GL lanes,
 * GetRenderTargetData on D3D9, the canvas itself on soft3d.
 *
 * Returns 0 and leaves out_path empty when every slot is taken or writing
 * files is refused.
 */
int
App_RequestScreenshot(
    struct App* app,
    char const* dir,
    char const* name,
    char* out_path,
    int out_path_size)
{
    return app_plugin_screenshot(app, "client", dir, name, out_path, out_path_size);
}

static int
app_plugin_screenshot(
    void* user,
    char const* plugin,
    char const* dir,
    char const* name,
    char* out_path,
    int out_path_size)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(plugin);
    assert(name);
    assert(out_path);
    assert(out_path_size > 0);

    out_path[0] = '\0';
    for( int i = 0; i < APP_PLUGIN_SCREENSHOTS_MAX; i++ )
    {
        struct AppPluginScreenshot* shot = &app->plugin_screenshots[i];
        size_t len;

        if( shot->in_use )
            continue;

        snprintf(shot->plugin, sizeof(shot->plugin), "%s", plugin);
        snprintf(shot->dir, sizeof(shot->dir), "%s", dir ? dir : "");
        snprintf(shot->name, sizeof(shot->name), "%s", name);
        /* The format is not the plugin's choice -- this writes PNG -- so a
         * name that does not say so is completed rather than trusted. A name
         * that already carries an extension is left alone, because a plugin
         * that wrote "kill-42.png" meant that file and not "kill-42.png.png". */
        len = strlen(shot->name);
        if( !strchr(shot->name, '.') && len + 4 < sizeof(shot->name) )
            snprintf(shot->name + len, sizeof(shot->name) - len, ".png");

        /* Resolved now rather than at the write, so the caller can be told
         * where its picture is going while it is still in a position to say
         * so. Refused here for the same reason app_plugin_asset_write refuses:
         * a client told not to write files must not start writing them because
         * a plugin asked -- and a refusal before the frame is spent is better
         * than one after it. */
        app_plugin_screenshot_path(
            app, shot->plugin, shot->dir, shot->name, shot->path, sizeof(shot->path));
        if( !shot->path[0] )
        {
            TORIRS_ERR(
                "plugin: %s cannot save screenshot '%s'; plugin persistence is off for "
                "this run (TORIRS_PLUGIN_PREFS is empty), so there is no folder to put it "
                "under and the destination is not an absolute path\n",
                shot->plugin,
                shot->name);
            return 0;
        }

        shot->in_use = 1;
        snprintf(out_path, (size_t)out_path_size, "%s", shot->path);
        return 1;
    }

    TORIRS_LOG(
        "plugin: %s asked for more than %d screenshots in one frame; '%s' was dropped\n",
        plugin,
        APP_PLUGIN_SCREENSHOTS_MAX,
        name);
    return 0;
}

/*
 * Where a capture's pixels come from when the lane could not supply any.
 *
 * A software re-render of the frame the emit buffer is still holding. It is
 * the same scene through the client's own rasteriser, which is NOT the same
 * thing as the frame that was presented: a GPU lane may have drawn it with
 * different textures, filtering and draw distance, and none of that is in
 * here. It exists so a lane with no readback (D3D9) and a run with no
 * renderer at all (headless) still produce a picture rather than nothing.
 *
 * Every lane that can read its own frame back should, and does.
 */
static int
app_capture_fallback_render(
    struct App* app,
    int* pixels,
    int width,
    int height)
{
    int saved_pick;

    assert(app);
    assert(pixels);

    /*
     * Disarm the world pick for the duration.
     *
     * App_Render arms it from the live mouse position and hands the hits to
     * App_PickFinish, which is how the click paths learn what is under the
     * pointer. This render is not the one they are reading, and letting it
     * publish a second pickset for the same frame would make a screenshot a
     * thing that can affect what a click does.
     */
    saved_pick = app->world_mouse_in_viewport;
    app->world_mouse_in_viewport = 0;
    App_Render(app, pixels, width, height);
    app->world_mouse_in_viewport = saved_pick;
    return 1;
}

/*
 * Encode one frame and hand every waiting capture a copy.
 *
 * One encode for all of them: the pending queue holds requests made during the
 * same frame, so they are requests for the same picture under different names.
 */
static void
app_plugin_screenshots_write(
    struct App* app,
    int const* pixels,
    int width,
    int height)
{
    unsigned char* rgb;
    void* png;
    size_t png_size = 0;

    assert(app);
    assert(pixels);

    /* Three channels, not four: the client's canvas has no alpha to carry
     * (the high byte is padding), and a PNG that claimed one would be half
     * again as large for nothing. */
    rgb = malloc((size_t)width * (size_t)height * 3);
    assert(rgb);
    for( int i = 0; i < width * height; i++ )
    {
        rgb[i * 3 + 0] = (unsigned char)((pixels[i] >> 16) & 0xFF);
        rgb[i * 3 + 1] = (unsigned char)((pixels[i] >> 8) & 0xFF);
        rgb[i * 3 + 2] = (unsigned char)(pixels[i] & 0xFF);
    }

    png = tdefl_write_image_to_png_file_in_memory_ex(rgb, width, height, 3, &png_size, 6, MZ_FALSE);
    free(rgb);
    assert(png);

    for( int i = 0; i < APP_PLUGIN_SCREENSHOTS_MAX; i++ )
    {
        struct AppPluginScreenshot* shot = &app->plugin_screenshots[i];

        if( !shot->in_use )
            continue;
        shot->in_use = 0;

        /* The destination was resolved -- and refused, when there was none --
         * back when the request was made, so a queued capture always has a
         * path to go to. */
        assert(shot->path[0]);
        ToriRS_TaskQueue_Add(
            app->runner.queue, CreateTask_PluginAssetWrite(shot->path, png, (int)png_size));
    }

    mz_free(png);
}

void
App_DrawComplete(
    struct App* app,
    App_FrameSupplier supplier,
    void* supplier_user)
{
    int const width = UITREE_LAYOUT_ROOT_W;
    int const height = UITREE_LAYOUT_ROOT_H;
    int pending = 0;
    int* pixels;

    assert(app);

    /*
     * Nobody waiting, nothing to do -- and this test comes FIRST, before the
     * supplier is so much as called. That ordering is the whole design: it is
     * what lets a lane hand over a glReadPixels here and pay for it only on
     * the frames a capture was asked for.
     */
    for( int i = 0; i < APP_PLUGIN_SCREENSHOTS_MAX; i++ )
        pending += app->plugin_screenshots[i].in_use;
    if( pending == 0 )
        return;

    /* A capture of the loading bar is not a capture of anything. Requests
     * survive the wait; a plugin cannot ask for one before it has started
     * anyway, so this only covers a boot that re-enters. */
    if( App_IsBooting(app, NULL) )
        return;

    pixels = malloc((size_t)width * (size_t)height * sizeof(int));
    assert(pixels);

    if( !supplier || !supplier(supplier_user, pixels, width, height) )
        app_capture_fallback_render(app, pixels, width, height);

    app_plugin_screenshots_write(app, pixels, width, height);
    free(pixels);
}

/* ------------------------------------------------- notable moments */

/*
 * The plugin layer's window onto "something worth reacting to happened".
 *
 * Three entry points because the game genuinely announces things three ways --
 * in the chatbox, on an interface, and as a number in UPDATE_STAT -- and one
 * recogniser behind them, so every plugin agrees about what a boss kill is.
 * See game/rs_game_events.c for why a level-up is NOT read out of prose.
 */
static void
app_dispatch_game_event(
    struct App* app,
    struct RS_GameEvent const* ev)
{
    char const* kind;

    assert(app);
    assert(ev);

    kind = RS_GameEvent_KindName(ev->kind);
    /* A recognised event with no name would be a recogniser that grew a kind
     * without naming it -- the one thing a plugin cannot work around. */
    assert(kind);
    PluginHost_GameEvent(app->plugins, kind, ev->subject, ev->value, ev->text);
}

void
App_NotifyChatMessage(
    struct App* app,
    int type,
    char const* sender,
    char const* text)
{
    struct RS_GameEvent ev;

    assert(app);
    if( !app->plugins )
        return;

    PluginHost_ChatMessage(app->plugins, type, sender, text);

    /*
     * Only the GAME channel is recognised, and that is a security property
     * rather than a filter.
     *
     * Every pattern the recogniser knows is a sentence a player can type. If
     * public chat fed it, standing in a bank and saying "Your Zulrah kill
     * count is: 122." would fire a boss kill in everyone's client -- taking
     * their screenshots, tripping their notifications, and writing to their
     * disk. The game channel is the server talking, and nobody else can put a
     * line on it.
     */
    if( type != RS_CHAT_TYPE_GAME )
        return;
    if( RS_GameEvent_FromText(RS_GAME_EVENT_SRC_CHAT, text, &ev) )
        app_dispatch_game_event(app, &ev);
}

void
App_NotifyInterfaceText(
    struct App* app,
    char const* text)
{
    struct RS_GameEvent ev;

    assert(app);
    if( !app->plugins )
        return;

    /* Not forwarded as a chat message: this is every journal line and every
     * button caption in the game, and a plugin reading "chat" must not have to
     * filter the interface out of it. Only the recogniser sees it, and it
     * matches exactly one pattern here. */
    if( RS_GameEvent_FromText(RS_GAME_EVENT_SRC_INTERFACE, text, &ev) )
        app_dispatch_game_event(app, &ev);
}

void
App_NotifyStatLevel(
    struct App* app,
    int skill,
    int base_level)
{
    struct RS_GameEvent ev;
    int previous;

    assert(app);
    if( skill < 0 || skill >= RS_PLAYER_STATS_SKILL_COUNT )
        return;

    /* Recorded whether or not anyone is listening: a plugin enabled halfway
     * through a session must not see its first stat update as a level-up. */
    previous = app->stats.last_seen_level[skill];
    app->stats.last_seen_level[skill] = base_level;

    if( !app->plugins )
        return;
    if( RS_GameEvent_FromStat(skill, previous, base_level, &ev) )
        app_dispatch_game_event(app, &ev);
}

/* ---- the engine seam the plugin host holds (declared in the bridge) ---- */

/*
 * Decode a plugin's model file and hold it at `handle`.
 *
 * RSCache_ModelNewDecode sniffs the format off the file's own trailer magic
 * (OB2 / OB3 / V2 / V3) and never consults the booted revision's profile,
 * which is the property that makes a shipped model portable: one file decodes
 * the same way under every cache this client boots. Nothing here may key off
 * app->provider for that reason.
 *
 * Returns 0 for bytes that are not a model. Not an assert: the plugin named a
 * file, and being told "that will not decode" is an answer it has to be able
 * to get.
 */
static int
app_plugin_model_publish(
    void* user,
    int handle,
    void const* data,
    int size)
{
    struct App* app = (struct App*)user;
    struct AppPluginAssetModel* shipped;
    struct RSCache_Model* decoded;

    assert(app);
    assert(data);
    if( handle < 0 || handle >= TORIRS_PLUGIN_MODELS_MAX )
        return 0;
    if( size <= 0 )
        return 0;

    decoded = RSCache_ModelNewDecode((uint8_t*)data, size);
    if( !decoded )
        return 0;

    shipped = &app->plugin_asset_models[handle];
    /* A republish of the same slot replaces what was there; the objects
     * standing on it rebuild on the revision change. */
    if( shipped->model )
        ToriRS_ModelFree(shipped->model);
    shipped->in_use = 1;
    shipped->model = ToriRS_ModelFromRSCache(decoded);
    RSCache_ModelFree(decoded);
    shipped->revision++;

    app->plugin_geometry_dirty = 1;
    app->need_redraw = 1;
    return shipped->model != NULL;
}

static void
app_plugin_model_release(
    void* user,
    int handle)
{
    struct App* app = (struct App*)user;
    struct AppPluginAssetModel* shipped;

    assert(app);
    shipped = app_plugin_asset_model_at(app, handle);
    if( !shipped )
        return;
    if( shipped->model )
        ToriRS_ModelFree(shipped->model);
    memset(shipped, 0, sizeof(*shipped));
    /* Objects built from it are still standing; the settle takes them down. */
    app->plugin_geometry_dirty = 1;
    app->need_redraw = 1;
}

static int
app_plugin_mesh_create(void* user)
{
    struct App* app = (struct App*)user;
    assert(app);

    for( int i = 0; i < APP_PLUGIN_MESHES_MAX; i++ )
    {
        struct ToriRS_PluginMesh* mesh = &app->plugin_meshes[i];
        if( mesh->in_use )
            continue;
        memset(mesh, 0, sizeof(*mesh));
        mesh->in_use = 1;
        /* Revisions start at 1 so that 0 can mean "stands on no mesh", which
         * is what app_plugin_object_geometry_revision answers for a destroyed
         * one -- and what tears the objects still standing on it down. */
        mesh->revision = 1;
        return i;
    }
    TORIRS_ERR("plugin: mesh table full (%d); mesh_create refused\n", APP_PLUGIN_MESHES_MAX);
    return -1;
}

static void
app_plugin_mesh_destroy(
    void* user,
    int handle)
{
    struct App* app = (struct App*)user;
    struct ToriRS_PluginMesh* mesh;

    assert(app);
    mesh = app_plugin_mesh_at(app, handle);
    if( !mesh )
        return;
    ToriRS_PluginMeshRelease(mesh);
    /* Objects built from it are still standing; the settle takes them down. */
    app->plugin_geometry_dirty = 1;
    app->need_redraw = 1;
}

static int
app_plugin_mesh_vertex(
    void* user,
    int handle,
    int x,
    int y,
    int z)
{
    struct App* app = (struct App*)user;
    struct ToriRS_PluginMesh* mesh;
    int index;

    assert(app);
    mesh = app_plugin_mesh_at(app, handle);
    if( !mesh )
        return -1;
    if( mesh->vertex_count >= TORIRS_PLUGIN_MESH_VERTICES_MAX )
    {
        TORIRS_ERR(
            "plugin: mesh %d is at its %d vertex ceiling; mesh_vertex refused\n",
            handle,
            TORIRS_PLUGIN_MESH_VERTICES_MAX);
        return -1;
    }
    /* Model coordinates are 16-bit in every renderer this feeds, so a plugin
     * that computed one outside that range has geometry that would wrap rather
     * than geometry that is merely large. */
    assert(x >= INT16_MIN && x <= INT16_MAX);
    assert(y >= INT16_MIN && y <= INT16_MAX);
    assert(z >= INT16_MIN && z <= INT16_MAX);

    index = mesh->vertex_count;
    ToriRS_PluginMeshGrow(mesh, /*faces=*/0, index + 1);
    mesh->vertices_x[index] = (int16_t)x;
    mesh->vertices_y[index] = (int16_t)y;
    mesh->vertices_z[index] = (int16_t)z;
    mesh->vertex_count = index + 1;
    mesh->revision++;
    app->plugin_geometry_dirty = 1;
    return index;
}

static int
app_plugin_mesh_face(
    void* user,
    int handle,
    int a,
    int b,
    int c,
    int hsl,
    int alpha)
{
    struct App* app = (struct App*)user;
    struct ToriRS_PluginMesh* mesh;
    int index;

    assert(app);
    mesh = app_plugin_mesh_at(app, handle);
    if( !mesh )
        return -1;
    if( mesh->face_count >= TORIRS_PLUGIN_MESH_FACES_MAX )
    {
        TORIRS_ERR(
            "plugin: mesh %d is at its %d face ceiling; mesh_face refused\n",
            handle,
            TORIRS_PLUGIN_MESH_FACES_MAX);
        return -1;
    }
    /* A face naming a vertex that has not been authored is a plugin bug that
     * would otherwise read past the vertex arrays in the rasteriser, three
     * layers from the call that caused it. */
    assert(a >= 0 && a < mesh->vertex_count);
    assert(b >= 0 && b < mesh->vertex_count);
    assert(c >= 0 && c < mesh->vertex_count);
    assert(hsl >= 0 && hsl <= 0xFFFF);
    assert(alpha >= 0 && alpha <= TORIRS_PLUGIN_MESH_ALPHA_MAX);

    index = mesh->face_count;
    ToriRS_PluginMeshGrow(mesh, /*faces=*/1, index + 1);
    mesh->face_a[index] = (int16_t)a;
    mesh->face_b[index] = (int16_t)b;
    mesh->face_c[index] = (int16_t)c;
    mesh->face_color[index] = (uint16_t)hsl;
    mesh->face_alpha[index] = (uint8_t)alpha;
    mesh->face_count = index + 1;
    mesh->revision++;
    app->plugin_geometry_dirty = 1;
    return index;
}

static int
app_plugin_object_create(void* user)
{
    struct App* app = (struct App*)user;
    assert(app);

    for( int i = 0; i < APP_PLUGIN_OBJECTS_MAX; i++ )
    {
        struct AppPluginObject* obj = &app->plugin_objects[i];
        if( obj->in_use )
            continue;
        memset(obj, 0, sizeof(*obj));
        obj->in_use = 1;
        obj->source = TORIRS_HOST_MODEL_CACHE;
        obj->model_id = -1;
        obj->seq_id = -1;
        obj->loop = 1;
        obj->level = -1;
        obj->element_id = -1;
        obj->world_index = -1;
        obj->built_source = -1;
        obj->built_model_id = -1;
        return i;
    }
    TORIRS_ERR(
        "plugin: world-object table full (%d); object_create refused\n", APP_PLUGIN_OBJECTS_MAX);
    return -1;
}

static void
app_plugin_object_destroy(
    void* user,
    int handle)
{
    struct App* app = (struct App*)user;
    struct AppPluginObject* obj;

    assert(app);
    obj = app_plugin_object_at(app, handle);
    if( !obj )
        return;
    app_plugin_object_teardown(app, obj);
    memset(obj, 0, sizeof(*obj));
    app->need_redraw = 1;
}

static void
app_plugin_object_set_model(
    void* user,
    int handle,
    int source,
    int id)
{
    struct App* app = (struct App*)user;
    struct AppPluginObject* obj;

    assert(app);
    obj = app_plugin_object_at(app, handle);
    if( !obj )
        return;
    if( obj->source == source && obj->model_id == id )
        return;
    obj->source = source;
    obj->model_id = id;
    app_plugin_object_sync(app, handle);
}

static void
app_plugin_object_recolor(
    void* user,
    int handle,
    int hsl_from,
    int hsl_to)
{
    struct App* app = (struct App*)user;
    struct AppPluginObject* obj;

    assert(app);
    obj = app_plugin_object_at(app, handle);
    if( !obj )
        return;
    if( obj->recolor_count >= TORIRS_PLUGIN_OBJECT_RECOLORS_MAX )
    {
        TORIRS_LOG(
            "plugin: world object %d already carries %d recolour pairs; "
            "the extra one is dropped\n",
            handle,
            TORIRS_PLUGIN_OBJECT_RECOLORS_MAX);
        return;
    }
    obj->recolor_from[obj->recolor_count] = hsl_from;
    obj->recolor_to[obj->recolor_count] = hsl_to;
    obj->recolor_count++;
    app_plugin_object_sync(app, handle);
}

static void
app_plugin_object_clear_recolors(
    void* user,
    int handle)
{
    struct App* app = (struct App*)user;
    struct AppPluginObject* obj;

    assert(app);
    obj = app_plugin_object_at(app, handle);
    if( !obj )
        return;
    obj->recolor_count = 0;
    app_plugin_object_sync(app, handle);
}

static void
app_plugin_object_set_anim(
    void* user,
    int handle,
    int seq_id,
    int loop)
{
    struct App* app = (struct App*)user;
    struct AppPluginObject* obj;

    assert(app);
    obj = app_plugin_object_at(app, handle);
    if( !obj )
        return;
    if( obj->seq_id == seq_id && obj->loop == (loop != 0) )
        return;
    obj->seq_id = seq_id;
    obj->loop = loop != 0;
    /* The sequence is bound onto the element, so a live object takes the new
     * one without the model being rebuilt. */
    if( obj->element_id >= 0 )
    {
        int const bound = app_plugin_object_seq_id(app, obj);
        ToriDraw_SceneElementSetAnimLoop(app->scene, obj->element_id, obj->loop != 0);
        if( bound >= 0 )
            app_world_apply_seq(app, obj->element_id, bound);
        else
            ToriDraw_SceneElementSetAnimation(app->scene, obj->element_id, NULL, true);
        app->need_redraw = 1;
    }
    else
        app_plugin_object_sync(app, handle);
}

static void
app_plugin_object_set_light(
    void* user,
    int handle,
    int ambient,
    int contrast)
{
    struct App* app = (struct App*)user;
    struct AppPluginObject* obj;

    assert(app);
    obj = app_plugin_object_at(app, handle);
    if( !obj )
        return;
    if( obj->ambient == ambient && obj->contrast == contrast )
        return;
    obj->ambient = ambient;
    obj->contrast = contrast;
    /* Lighting is baked into the face colours, so this is a model change --
     * tear down explicitly, because the built_* stamps do not cover it. */
    if( obj->element_id >= 0 )
        app_plugin_object_teardown(app, obj);
    app_plugin_object_sync(app, handle);
}

static void
app_plugin_object_set_position(
    void* user,
    int handle,
    int tile_x,
    int tile_z,
    int level,
    int height,
    int yaw)
{
    struct App* app = (struct App*)user;
    struct AppPluginObject* obj;

    assert(app);
    obj = app_plugin_object_at(app, handle);
    if( !obj )
        return;
    if( obj->tile_x == tile_x && obj->tile_z == tile_z && obj->level == level &&
        obj->height == height && obj->yaw == yaw )
        return;
    obj->tile_x = tile_x;
    obj->tile_z = tile_z;
    obj->level = level;
    obj->height = height;
    obj->yaw = yaw;
    app_plugin_object_sync(app, handle);
}

static void
app_plugin_object_set_active(
    void* user,
    int handle,
    int active)
{
    struct App* app = (struct App*)user;
    struct AppPluginObject* obj;

    assert(app);
    obj = app_plugin_object_at(app, handle);
    if( !obj )
        return;
    if( obj->active == (active != 0) )
        return;
    obj->active = active != 0;
    app_plugin_object_sync(app, handle);
}

static int
app_plugin_object_ready(
    void* user,
    int handle)
{
    struct App* app = (struct App*)user;
    struct AppPluginObject* obj;

    assert(app);
    obj = app_plugin_object_at(app, handle);
    return (obj && obj->element_id >= 0) ? 1 : 0;
}

static void
app_world_spawn_player(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    struct Task_AppSpawn* task = app_spawn_task_new(app, APP_SPAWN_PLAYER, tile_x, tile_z, level);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

static void
app_world_spawn_npc(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args)
{
    struct Task_AppSpawn* task = app_spawn_task_new(app, APP_SPAWN_NPC, tile_x, tile_z, level);
    task->npc_id =
        ToriRS_EnvNamedArgOrEnv(args, "id", "TORIRS_SPAWN_NPC", 3106 /* OSRS-era "Man" */);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Hotkey 7: ground item on the hovered tile — the same App_WorldObjStackAdd
 * the zone OBJ_ADD packet drives, so the right-click rows it produces are the
 * live path. TORIRS_SPAWN_OBJ overrides the id. */
static void
app_world_spawn_obj(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args)
{
    struct Task_AppSpawn* task = app_spawn_task_new(app, APP_SPAWN_OBJ, tile_x, tile_z, level);
    task->obj_id = ToriRS_EnvNamedArgOrEnv(
        args, "id", "TORIRS_SPAWN_OBJ", 1265 /* bronze pickaxe: named, with ground ops */);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Free-standing spotanim spawn (reference MapSpotAnim / MAP_ANIM zone packet):
 * enqueue an async spawn that awaits the spotanimtype + its model + seq, then
 * builds the world entity. Public so the MAP_ANIM executor can drive it. */
void
App_WorldSpotanimSpawn(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int spotanim_id,
    int height,
    int delay)
{
    struct Task_AppSpawn* task;
    assert(app);
    task = app_spawn_task_new(app, APP_SPAWN_SPOTANIM, scene_x, scene_z, level);
    task->spotanim_id = spotanim_id;
    task->spotanim_height = height;
    task->spotanim_delay = delay;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Server-driven projectile (reference ClientProj / MAP_PROJANIM). Public so the
 * zone-packet executor can drive it. Enqueues the spotanim + model + seq load,
 * then spawns the world projectile with the wire trajectory params. */
void
App_WorldProjectileSpawn(
    struct App* app,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    int level,
    int spotanim_id,
    int src_height,
    int dst_height,
    int start_delay,
    int end_delay,
    int peak,
    int arc,
    int target)
{
    struct Task_AppSpawn* task;
    assert(app);
    /* Destination tile and level go through the task's tile_x, tile_z and level
     * (seeded by app_spawn_task_new); source tile and level go through
     * src_tile_x, src_tile_z and src_level. */
    task = app_spawn_task_new(app, APP_SPAWN_PROJECTILE_SPOT, dst_x, dst_z, level);
    task->spotanim_id = spotanim_id;
    task->src_tile_x = src_x;
    task->src_tile_z = src_z;
    task->src_level = level;
    task->proj_src_height = src_height;
    task->proj_dst_height = dst_height;
    task->proj_start_delay = start_delay;
    task->proj_end_delay = end_delay;
    task->proj_peak = peak;
    task->proj_arc = arc;
    task->proj_target = target;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Zone LOC_ADD_CHANGE / LOC_DEL (reference locChangeCreate + locChangeDoQueue):
 * enqueue an async change that awaits the loc config + its models (+ seq), then
 * applies it via WorldBuilder_ApplyLocChange. loc_id < 0 = delete. Public so the
 * zone-packet executor can drive it. */
void
App_WorldLocChange(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle)
{
    static const char none[5][32] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };

    /* All five bits: the loctype's menu, unchanged. Not a "no data" stand-in —
     * it is the real menu of every loc placed by anything but a door script. */
    App_WorldLocChangeOps(app, scene_x, scene_z, level, loc_id, shape, angle, 0x1f, none);
}

void
App_WorldLocChangeOps(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int op_flags,
    const char ops[5][32])
{
    struct Task_AppSpawn* task;
    assert(app);
    assert(ops);
    task = app_spawn_task_new(app, APP_SPAWN_LOC_CHANGE, scene_x, scene_z, level);
    task->loc_id = loc_id;
    task->loc_shape = shape;
    task->loc_angle = angle;
    task->loc_op_flags = op_flags;
    memcpy(task->loc_ops, ops, sizeof(task->loc_ops));
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

static void
app_loc_change_apply_cb(
    void* user,
    int level,
    int x,
    int z,
    int loc_id,
    int shape,
    int angle)
{
    struct App* app = (struct App*)user;

    App_WorldLocChange(app, x, z, level, loc_id, shape, angle);
}

void
App_WorldLocMerge(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int start_cycle,
    int end_cycle,
    int player_pid)
{
    struct WorldEntity_Player* player = NULL;
    struct World* world;
    int old_type = -1;
    int old_angle = 0;
    int old_shape = shape;
    int idx;

    assert(app);
    /* Pre-login there is no world at all — that is a state, not a bug. Once
     * one exists, the mutation goes to whichever view the cursor addresses. */
    if( !app->world )
        return;
    world = App_ActiveWorldview(app)->world;
    if( !world->load_complete )
        return;

    idx = World_SceneryFindAt(world, scene_x, scene_z, level, shape);
    if( idx >= 0 )
    {
        struct WorldEntity_Scenery* old = World_EntityPoolGet(&world->entities.scenery, idx);
        if( old )
        {
            old_type = old->loc_id;
            old_angle = old->angle;
            old_shape = old->shape;
        }
    }

    /* Countdown LocChange: hide (new_type -1) after start_cycle ticks, restore
     * after end_cycle ticks — Client-TS locChangeCreate(..., t1+1, t2+1). */
    World_LocChangePush(
        world,
        level,
        World_LocShapeToLayer(shape),
        scene_x,
        scene_z,
        old_type,
        old_angle,
        old_shape,
        -1,
        0,
        0,
        start_cycle + 1,
        end_cycle + 1);

    if( player_pid == app->esync.local_pid )
        player = app_local_player(app);
    else
        player = World_PlayerGetByServerPid(world, player_pid);
    if( player )
    {
        player->loc_start_cycle = world->cycle + start_cycle;
        player->loc_stop_cycle = world->cycle + end_cycle;
        player->loc_merge_id = loc_id;
        player->loc_merge_shape = shape;
        player->loc_merge_angle = angle;
    }
}

/* Hotkey 5: spawn a free-standing spotanim on the hovered tile.
 * TORIRS_SPAWN_SPOTANIM / _HEIGHT / _DELAY override the defaults. */
static void
app_world_spawn_spotanim(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args)
{
    int spotanim_id = ToriRS_EnvNamedArgOrEnv(
        args, "id", "TORIRS_SPAWN_SPOTANIM", 74 /* a small, visible default effect */);
    int height = ToriRS_EnvNamedArgOrEnv(args, "height", "TORIRS_SPAWN_SPOTANIM_HEIGHT", 92);
    int delay = ToriRS_EnvNamedArgOrEnv(args, "delay", "TORIRS_SPAWN_SPOTANIM_DELAY", 0);
    App_WorldSpotanimSpawn(app, tile_x, tile_z, level, spotanim_id, height, delay);
}

/* Wire target-entity id (npc slot + 1) for a *synced* npc standing on a tile,
 * WORLD_PROJECTILE_TARGET_NONE when there is none. Only server-synced npcs can
 * be named: the wire encoding is the server's slot space, and offline spawns
 * deliberately sit outside it with server_slot -1. */
static int
app_world_npc_target_at_tile(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    struct World_EntityPool* pool;

    if( !app->world )
        return WORLD_PROJECTILE_TARGET_NONE;

    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        if( !npc || npc->server_slot < 0 )
            continue;
        if( npc->grid_position.x == tile_x && npc->grid_position.z == tile_z &&
            npc->grid_position.level == level )
            return npc->server_slot + 1;
    }
    return WORLD_PROJECTILE_TARGET_NONE;
}

/* Hotkey 0, two-press latch: first press marks the hovered tile as source,
 * second launches source -> hovered (same-tile press clears the latch). Firing
 * onto a synced npc targets *that entity*, so the arc follows it as it walks —
 * the tracking the MAP_PROJANIM target id drives against a live server. */
static void
app_world_spawn_projectile(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args)
{
    struct Task_AppSpawn* task;

    if( app->proj_src_tile_x < 0 )
    {
        app->proj_src_tile_x = tile_x;
        app->proj_src_tile_z = tile_z;
        app->proj_src_tile_level = level;
        TORIRS_LOG("spawn_projectile: source latched at %d,%d\n", tile_x, tile_z);
        return;
    }
    if( app->proj_src_tile_x == tile_x && app->proj_src_tile_z == tile_z )
    {
        app->proj_src_tile_x = -1;
        app->proj_src_tile_z = -1;
        TORIRS_LOG("spawn_projectile: latch cleared\n");
        return;
    }

    task = app_spawn_task_new(app, APP_SPAWN_PROJECTILE, tile_x, tile_z, level);
    task->model_id = ToriRS_EnvNamedArgOrEnv(
        args, "model", "TORIRS_SPAWN_PROJ_MODEL", 3081 /* v1 spawn-test spotanim model */);
    task->seq_id = ToriRS_EnvNamedArgOrEnv(
        args,
        "seq",
        "TORIRS_SPAWN_PROJ_SEQ",
        659 /* v1 spawn-test spotanim sequence (RUNESCAPE_PROJECTILE_SEQ_ID) */);
    task->src_tile_x = app->proj_src_tile_x;
    task->src_tile_z = app->proj_src_tile_z;
    task->src_level = app->proj_src_tile_level;
    task->proj_target = app_world_npc_target_at_tile(app, tile_x, tile_z, level);
    app->proj_src_tile_x = -1;
    app->proj_src_tile_z = -1;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Hotkey 6: hit every live player/npc for a test hitsplat + half health and
 * give each an overhead chat line, so the whole overlay pass (health bars,
 * hitmarks and overhead chat) can be exercised offline. Goes through the same
 * World_*AddHitmark / World_*SetChat the NPC_INFO/PLAYER_INFO ops use. */
static void
app_world_damage_test(struct App* app)
{
    struct World_EntityPool* pool;
    int damage = 1 + (app->logic_cycle % 30);
    /* Rev 239 does not use the legacy type-0/type-1 convention: its canonical
     * red damage splat is type 28 (sprite 1359). Keep type 0 only as the
     * supported fallback for older cache families with no rev-239 record. */
    int hitsplat_type =
        app->hitsplats.count > RS_HITSPLAT_OSRS239_DAMAGE ? RS_HITSPLAT_OSRS239_DAMAGE : 0;

    if( !app->world )
        return;
    pool = &app->world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        World_PlayerAddHitmark(app->world, i, hitsplat_type, damage, 5, 10);
        World_PlayerSetChat(app->world, i, "Hello there!", 0, 0);
        /* Exercise the overhead headicon pass too: icons 0 + 2 stacked. */
        {
            struct WorldEntity_Player* tpl = World_EntityPoolGet(pool, i);
            if( tpl )
                tpl->headicon = 0x5;
        }
    }
    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        World_NpcAddHitmark(app->world, i, hitsplat_type, damage, 5, 10);
        World_NpcSetChat(app->world, i, "Grrr!", 0, 0);
    }
}

/* Hotkey 4: apply an attached graphic (SPOTANIM mask) to every spawned entity —
 * the reference impact effect a projectile lands on its target. Exercises the
 * entity-spotanim companion-element path headlessly. TORIRS_SPAWN_SPOTANIM /
 * _HEIGHT / _DELAY reuse the free-standing overrides. */
static void
app_world_entity_spotanim_test(
    struct App* app,
    char const* args)
{
    struct World_EntityPool* pool;
    int spotanim_id = ToriRS_EnvNamedArgOrEnv(args, "id", "TORIRS_SPAWN_SPOTANIM", 74);
    int height = ToriRS_EnvNamedArgOrEnv(args, "height", "TORIRS_SPAWN_SPOTANIM_HEIGHT", 92);
    int delay = ToriRS_EnvNamedArgOrEnv(args, "delay", "TORIRS_SPAWN_SPOTANIM_DELAY", 0);

    if( !app->world )
        return;
    pool = &app->world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
        World_PlayerSetSpotanim(app->world, i, spotanim_id, height, delay);
    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
        World_NpcSetSpotanim(app->world, i, spotanim_id, height, delay);
}

/* A configured debug key is available only if no revconfig hotkey binding
 * already acted on the corresponding OSRS key this frame. */
static struct AppDebugHotkeyBinding const*
app_debug_world_key(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target)
{
    for( int i = 0; i < app->cfg.debug_hotkey_count; i++ )
    {
        enum LibToriRS_KeyCode key = app->cfg.debug_hotkeys[i].key;
        int vk = -1;
        int osrs_key;

        if( app->cfg.debug_hotkeys[i].target != target || !LibToriRS_Input_IsKeyDown(input, key) )
            continue;
        if( key >= TORIRSK_A && key <= TORIRSK_Z )
            vk = 65 + (key - TORIRSK_A);
        else if( key >= TORIRSK_0 && key <= TORIRSK_9 )
            vk = 48 + (key - TORIRSK_0);
        else
            switch( key )
            {
            case TORIRSK_ESCAPE:
                vk = TORIRS_VK_ESCAPE;
                break;
            case TORIRSK_RETURN:
                vk = TORIRS_VK_ENTER;
                break;
            case TORIRSK_BACKSPACE:
                vk = TORIRS_VK_BACKSPACE;
                break;
            case TORIRSK_DELETE:
                vk = TORIRS_VK_DELETE;
                break;
            case TORIRSK_SHIFT:
                vk = TORIRS_VK_SHIFT;
                break;
            case TORIRSK_CTRL:
                vk = TORIRS_VK_CTRL;
                break;
            case TORIRSK_TAB:
                vk = TORIRS_VK_TAB;
                break;
            case TORIRSK_SPACE:
                vk = TORIRS_VK_SPACE;
                break;
            case TORIRSK_LEFT:
                vk = 37;
                break;
            case TORIRSK_UP:
                vk = 38;
                break;
            case TORIRSK_RIGHT:
                vk = 39;
                break;
            case TORIRSK_DOWN:
                vk = 40;
                break;
            case TORIRSK_PAGE_UP:
                vk = 33;
                break;
            case TORIRSK_PAGE_DOWN:
                vk = 34;
                break;
            default:
                break;
            }
        osrs_key = LibToriRS_OsrsKeyFromVk(vk);
        if( osrs_key < 0 || osrs_key >= TORIRS_OSRSKEY_COUNT || !app->hotkey_consumed[osrs_key] )
            return &app->cfg.debug_hotkeys[i];
    }
    return NULL;
}

/* Manifest-configured spawn/test shortcuts act on the tile under the mouse, so
 * they no-op when nothing is hovered. */
static void
app_world_hotkeys(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out)
{
    struct AppDebugHotkeyBinding const* binding;
    /* Spawn hotkeys gate on the hovered world tile, not on onKey targets —
     * under the real gameframe there is always some visible onKey component
     * and gating on it made every press suppress itself. */
    (void)out;
    if( !app->world_active || !app_world_viewport_component_live(app) )
        return;
    /* Suppressed while any text input has focus, so spawn-digit keys type
     * instead. */
    if( app_text_input_focused(app) )
        return;
    if( app->world_hover_tile_x < 0 || app->world_hover_tile_z < 0 )
        return;

    if( app_debug_world_key(app, input, APP_DEBUG_HOTKEY_SPAWN_PLAYER) )
        app_world_spawn_player(
            app, app->world_hover_tile_x, app->world_hover_tile_z, app->world_hover_tile_level);
    if( (binding = app_debug_world_key(app, input, APP_DEBUG_HOTKEY_SPAWN_NPC)) )
        app_world_spawn_npc(
            app,
            app->world_hover_tile_x,
            app->world_hover_tile_z,
            app->world_hover_tile_level,
            binding->args);
    if( app_debug_world_key(app, input, APP_DEBUG_HOTKEY_DAMAGE_TEST) )
        app_world_damage_test(app);
    if( (binding = app_debug_world_key(app, input, APP_DEBUG_HOTKEY_ENTITY_SPOTANIM)) )
        app_world_entity_spotanim_test(app, binding->args);
    if( (binding = app_debug_world_key(app, input, APP_DEBUG_HOTKEY_SPAWN_SPOTANIM)) )
        app_world_spawn_spotanim(
            app,
            app->world_hover_tile_x,
            app->world_hover_tile_z,
            app->world_hover_tile_level,
            binding->args);
    if( (binding = app_debug_world_key(app, input, APP_DEBUG_HOTKEY_SPAWN_OBJ)) )
        app_world_spawn_obj(
            app,
            app->world_hover_tile_x,
            app->world_hover_tile_z,
            app->world_hover_tile_level,
            binding->args);
    if( (binding = app_debug_world_key(app, input, APP_DEBUG_HOTKEY_SPAWN_PROJECTILE)) )
        app_world_spawn_projectile(
            app,
            app->world_hover_tile_x,
            app->world_hover_tile_z,
            app->world_hover_tile_level,
            binding->args);
}

/* Per-frame world step: sim cycles, event drain (entity removals -> scene),
 * position sync, animation ticks. Runs every frame (cycles may be 0) so the
 * painter dynamic set stays fresh, and forces a redraw while active — but only
 * while a viewport is actually on screen; an unshown world does not tick. */
/* The level cutscene heights are measured against (reference minusedlevel) —
 * and the plane every ROOT-world judgment about the local player uses: mover
 * heights, the pick's reach filter, the minimap bake. ABOARD, the player's
 * own level is a DECK plane (the planking is authored at plane 1) and means
 * nothing to the root — the effective root plane is the HULL's. Without this
 * a level-1 rider had every shore npc's height sampled from the level-1
 * heightmap: the whole town floating on the wall tops. */
static int
app_cinema_level(struct App* app)
{
    int world_idx;
    struct WorldEntity_Player* player;

    if( app->aboard_view != WORLDVIEW_ROOT && Wevs_IsLive(&app->wevs, app->aboard_view) )
        return Wevs_Get(&app->wevs, app->aboard_view)->parent_level;
    if( !RS_EntitySync_FindPlayer(
            &app->esync,
            app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
            &world_idx,
            NULL) )
        return 0;
    player = World_EntityPoolGet(&app->world->entities.player, world_idx);
    return player ? player->grid_position.level : 0;
}

/* Scene-space position of a cutscene target. `height` is measured up from the
 * ground under the tile, and up is -y. */
static void
app_cinema_point(
    struct App* app,
    int local_x,
    int local_z,
    int height,
    int* out_x,
    int* out_y,
    int* out_z)
{
    int x = local_x * 128 + 64;
    int z = local_z * 128 + 64;
    *out_x = x;
    *out_z = z;
    *out_y = app_world_height(app, x, z, app_cinema_level(app)) - height;
}

/* Pitch/yaw that point the eye at the look-at target. Reference cinemaCamera
 * (Client-TS 3542): note the yaw multiplier is *negative* 325.949 — the scene
 * turns the opposite way to the mathematical angle. */
static void
app_cinema_angles(
    struct App* app,
    int* out_pitch,
    int* out_yaw)
{
    int tx, ty, tz, dx, dy, dz, distance, pitch;

    app_cinema_point(
        app,
        app->cam_script.look_lx,
        app->cam_script.look_lz,
        app->cam_script.look_height,
        &tx,
        &ty,
        &tz);

    dx = tx - app->world_camera_pos.x;
    dy = ty - app->world_camera_pos.y;
    dz = tz - app->world_camera_pos.z;
    distance = (int)sqrt((double)dx * dx + (double)dz * dz);

    pitch = (int)(atan2((double)dy, (double)distance) * 325.949) & 0x7ff;
    /* The scripted camera tips no further than the player's own may: the
     * profile's range, not a second copy of the reference's numbers. */
    pitch = app_world_clamp_pitch(app, pitch);

    *out_pitch = pitch;
    *out_yaw = (int)(atan2((double)dx, (double)dz) * -325.949) & 0x7ff;
}

void
App_CinemaCameraSnapPosition(struct App* app)
{
    app_cinema_point(
        app,
        app->cam_script.move_lx,
        app->cam_script.move_lz,
        app->cam_script.move_height,
        &app->world_camera_pos.x,
        &app->world_camera_pos.y,
        &app->world_camera_pos.z);
}

void
App_CinemaCameraSnapAngle(struct App* app)
{
    app_cinema_angles(app, &app->world_camera.pitch, &app->world_camera.yaw);
}

/* Ease one axis toward its target: a flat `rate` plus `rate2`/1000 of what is
 * left, never overshooting. */
static int
app_cinema_ease(
    int current,
    int target,
    int rate,
    int rate2)
{
    if( current < target )
    {
        current += rate + (target - current) * rate2 / 1000;
        if( current > target )
            current = target;
    }
    else if( current > target )
    {
        current -= rate + (current - target) * rate2 / 1000;
        if( current < target )
            current = target;
    }
    return current;
}

/* Reference cinemaCamera (Client-TS 3542). Runs every frame the script is up:
 * the camera walks toward the move-to point and turns toward the look-at
 * point, so a rate2 under 100 glides instead of cutting. */
static void
app_world_camera_cinema(struct App* app)
{
    int tx, ty, tz, pitch, yaw, delta;
    int rate = app->cam_script.move_rate;
    int rate2 = app->cam_script.move_rate2;

    if( !app->cam_script.scripted || !app->world )
        return;

    app_cinema_point(
        app,
        app->cam_script.move_lx,
        app->cam_script.move_lz,
        app->cam_script.move_height,
        &tx,
        &ty,
        &tz);

    app->world_camera_pos.x = app_cinema_ease(app->world_camera_pos.x, tx, rate, rate2);
    app->world_camera_pos.y = app_cinema_ease(app->world_camera_pos.y, ty, rate, rate2);
    app->world_camera_pos.z = app_cinema_ease(app->world_camera_pos.z, tz, rate, rate2);

    app_cinema_angles(app, &pitch, &yaw);
    rate = app->cam_script.look_rate;
    rate2 = app->cam_script.look_rate2;
    app->world_camera.pitch = app_cinema_ease(app->world_camera.pitch, pitch, rate, rate2);

    /* Yaw wraps, so ease along the short way round and stop when the sign of
     * the remaining turn flips — the linear helper cannot see past the seam. */
    delta = yaw - app->world_camera.yaw;
    if( delta > 1024 )
        delta -= 2048;
    else if( delta < -1024 )
        delta += 2048;

    if( delta > 0 )
        app->world_camera.yaw = (app->world_camera.yaw + rate + delta * rate2 / 1000) & 0x7ff;
    else if( delta < 0 )
        app->world_camera.yaw = (app->world_camera.yaw - rate - -delta * rate2 / 1000) & 0x7ff;

    if( delta != 0 )
    {
        int remaining = yaw - app->world_camera.yaw;
        if( remaining > 1024 )
            remaining -= 2048;
        else if( remaining < -1024 )
            remaining += 2048;
        if( (remaining < 0 && delta > 0) || (remaining > 0 && delta < 0) )
            app->world_camera.yaw = yaw;
    }

    /* TORIRS_CAM_DEBUG=1: trace the scripted camera. */
    if( getenv("TORIRS_CAM_DEBUG") )
        TORIRS_LOG(
            "cam eye=%d,%d,%d pitch=%d yaw=%d -> move=%d,%d h=%d look=%d,%d h=%d "
            "shake=%d%d%d%d%d\n",
            app->world_camera_pos.x,
            app->world_camera_pos.y,
            app->world_camera_pos.z,
            app->world_camera.pitch,
            app->world_camera.yaw,
            app->cam_script.move_lx,
            app->cam_script.move_lz,
            app->cam_script.move_height,
            app->cam_script.look_lx,
            app->cam_script.look_lz,
            app->cam_script.look_height,
            app->cam_script.shake[0],
            app->cam_script.shake[1],
            app->cam_script.shake[2],
            app->cam_script.shake[3],
            app->cam_script.shake[4]);

    app->need_redraw = 1;
}

/*
 * Orbit distance zoom (reference Statics.method6352, and the identical
 * expression inlined in client.method2068 and client.method2066).
 *
 * The follow camera's `pitch * 3 + 600` is a FIXED-VIEWPORT distance: the
 * reference then scales it by an endpoint pair interpolated over the world
 * viewport HEIGHT, exactly the way the projection scale is
 * (class159.method5357), and over the same `height - 334` in [0,100] band:
 *
 *     zoom = (far - near) * clamp(vpH - 334, 0, 100) / 100 + near
 *     distance = (pitch * 3 + 600) * zoom / 256
 *
 * near/far are client.field780/field747 — CS2 VIEWPORT_SETZOOM (6201), default
 * 256 and 320. A fixed 334-high viewport therefore leaves the distance alone,
 * and a resizable one (503 here) pulls the eye a full 25% further out.
 *
 * Missing this term is what docs/ORANGE_WEDGE.md 7(b) measured as "the C eye
 * sits ~15% too close" and left open. Distance is not just closeness: the eye is
 * `pivot - distance` along pitch/yaw, so a short distance lowers the eye by the
 * same fraction. That is the "camera is too low" symptom.
 */
static int
app_world_cam_dist_zoom(struct App* app)
{
    int near_zoom = app->host.viewport_zoom;
    int far_zoom = app->host.viewport_zoom_max;
    int d;

    if( near_zoom <= 0 )
        near_zoom = 256;
    if( far_zoom <= 0 )
        far_zoom = 320;
    /* No laid-out viewport yet: 334 is the reference's fixed height, which puts
     * the interpolation on its near endpoint and leaves the distance unscaled. */
    d = (app->world_view_valid ? app->world_emit_desc.h : 334) - 334;
    if( d < 0 )
        d = 0;
    if( d > 100 )
        d = 100;
    return (far_zoom - near_zoom) * d / 100 + near_zoom;
}

/* Reference followCamera + camFollow (Client-TS 3459/4669): a 1/16-eased
 * orbit anchor trails the player, arrow keys accumulate yaw/pitch velocity,
 * a terrain scan raises pitch so the eye stays above nearby ground, then the
 * eye is placed `pitch*3+600` behind the anchor along pitch/yaw. Sin/cos are
 * 16.16 (same tables as Pix3D). */
static void
app_world_camera_follow(struct App* app)
{
    int world_idx;
    struct WorldEntity_Player* player;
    int target_x, target_y, target_z;
    int pitch, yaw, distance;
    int aboard_y = 0;
    int aboard_y_valid = 0;

    /* U unlocked the camera: the follow update stands down and the W/A/S/D +
     * R/F debug keys own world_camera_pos until U relocks. Without this gate
     * the follow overwrites the eye every frame, which is why free flight only
     * ever worked offline. */
    if( app->camera_unlocked )
        return;
    if( app->cam_script.scripted || !app->net )
        return;

    /*
     * TORIRS_ORBIT_CAM=yaw[,pitch[,zoom_pct]] — pin the follow camera's angles
     * so a headless capture frames a chosen subject instead of wherever the
     * login left the camera.
     *
     * Distinct from TORIRS_WEDGE_CAM, which pins the eye in world coordinates:
     * that needs the subject's position, this only needs its direction from the
     * player, and it keeps the real follow path (anchor easing, pitch clamp,
     * zoom) so what is captured is the camera the game actually uses. Applied
     * before the step, every frame, with the easing velocities zeroed so the
     * angles cannot drift back.
     *
     * yaw is 0..2047, pitch is clamped into the profile's own
     * `[camera] pitch_flattest=`..`pitch_steepest=` (the same range the
     * middle-button drag allows), zoom is a percentage of `[camera] rest=` —
     * still a percentage here rather than a raw distance, because that is the
     * spelling every recorded TORIRS_ORBIT_CAM string in the tree uses. It is
     * clamped into the `zoom_closest=`..`zoom_furthest=` band, so a profile
     * that states a band of one ignores it exactly as the wheel does.
     */
    {
        static int resolved = 0;
        static int have = 0;
        static int cam_yaw = 0, cam_pitch = 0, cam_zoom = 0, cam_spin = 0;
        if( !resolved )
        {
            char const* spec = getenv("TORIRS_ORBIT_CAM");
            resolved = 1;
            if( spec )
            {
                cam_pitch = -1;
                cam_zoom = -1;
                have = sscanf(spec, "%d,%d,%d,%d", &cam_yaw, &cam_pitch, &cam_zoom, &cam_spin) >= 1;
            }
        }
        /* A fourth field spins the camera by that many yaw units per frame.
         * Finding the angle a subject sits at otherwise costs one boot per
         * guess; with a spin and TORIRS_BMP_SERIES a single boot returns a
         * filmstrip all the way round. */
        cam_yaw += cam_spin;
        if( have )
        {
            app->orbit_yaw = cam_yaw & 0x7ff;
            app->orbit_yaw_vel = 0;
            if( cam_pitch >= 0 )
            {
                app->orbit_pitch = app_world_clamp_pitch(app, cam_pitch);
                app->orbit_pitch_vel = 0;
            }
            if( cam_zoom > 0 )
                /* A percentage of THIS revision's rest, not of the reference
                 * 600. The server is saying "this much closer than normal",
                 * and normal is wherever this camera rests -- reading it
                 * against a constant makes the same packet mean two different
                 * views on two lanes. */
                app->world_cam_zoom = RevConfigProfile_CameraClampZoom(
                    &app->revconfig_profile, app->revconfig_profile.camera.rest * cam_zoom / 100);
        }
    }
    if( !RS_EntitySync_FindPlayer(
            &app->esync,
            app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
            &world_idx,
            NULL) )
        return;
    player = World_EntityPoolGet(&app->world->entities.player, world_idx);
    if( !player )
        return;

    target_x = (int)player->draw_position.x;
    target_z = (int)player->draw_position.z;

    /*
     * Aboard (SAILING_PLAN C5.3): the player's own coordinates are deck-local,
     * so the camera follows their deck position pushed through the hull
     * transform into root scene space — recomputed every frame, which is what
     * glues the focus to a gliding, turning hull. The look-at height composes
     * the same way the emit path does: the hull's y (root terrain under the
     * boat, kept by the interpolator) plus the deck's own height under the
     * actor. Nested hulls would need the parent chain composed; the camera
     * handles the root-parented case, which is every hull that exists today.
     */
    aboard_y_valid = 0;
    if( app->aboard_view != WORLDVIEW_ROOT && player->view_placement.view_id == app->aboard_view &&
        app_wev_actor_root_fine(app, &player->view_placement, &target_x, &target_z) )
    {
        struct Wev* wev = Wevs_Get(&app->wevs, app->aboard_view);
        struct Worldview* view = WorldviewRegistry_Get(&app->worldviews, app->aboard_view);
        /* The rider's OWN plane, same rule as their placement — the camera
         * plane in the deob is focus.getPlane(), the wire plane. */
        int cam_level = player->grid_position.level;

        if( cam_level < 0 )
            cam_level = 0;
        if( cam_level >= COLLISION_LEVELS )
            cam_level = COLLISION_LEVELS - 1;
        aboard_y = wev->y +
                   World_HeightAt(
                       view->world, player->view_placement.x, player->view_placement.z, cam_level) -
                   8 - 50;
        aboard_y_valid = 1;
    }

    /* Anchor: snap when >500 units out (teleport), else ease 1/16 — in FLOAT,
     * per the reference's client.method1605 (rev-239 deob):
     *
     *     field917 = (targetX - field917) * (dtNanos / 3.2e8) + field917;
     *
     * dt is one 20 ms client cycle here, and 2e7 / 3.2e8 is exactly 1/16, so
     * the rate is the reference's. What matters is the type: the old integer
     * form `orbit_x += (target_x - orbit_x) / 16` truncates the step to 0 once
     * the gap falls below 16, so the anchor stopped a permanent ~15 units short
     * on each axis (~21 units diagonally, a sixth of a tile) in whichever
     * direction the player last walked. The eye is built around that anchor, so
     * the player model swung round a point beside itself while orbiting — the
     * camera appeared to orbit the tile rather than the player. */
    Wev_SmoothCameraFocus(&app->orbit_x, &app->orbit_z, target_x, target_z);

    /* Arrow keys -> yaw/pitch velocity (impulse 24/12, halved decay). */
    if( app->cam_key_left )
        app->orbit_yaw_vel += (-app->orbit_yaw_vel - 24) / 2;
    else if( app->cam_key_right )
        app->orbit_yaw_vel += (24 - app->orbit_yaw_vel) / 2;
    else
        app->orbit_yaw_vel = app->orbit_yaw_vel / 2;

    if( app->cam_key_up )
        app->orbit_pitch_vel += (12 - app->orbit_pitch_vel) / 2;
    else if( app->cam_key_down )
        app->orbit_pitch_vel += (-app->orbit_pitch_vel - 12) / 2;
    else
        app->orbit_pitch_vel = app->orbit_pitch_vel / 2;

    app->orbit_yaw = (app->orbit_yaw + app->orbit_yaw_vel / 2) & 0x7ff;
    app->orbit_pitch = app_world_clamp_pitch(app, app->orbit_pitch + app->orbit_pitch_vel / 2);

    /* Terrain pitch clamp: scan the 9x9 tile block around the anchor for
     * ground higher than the anchor's; raise the minimum pitch so the eye
     * clears it. cameraPitchClamp is 24.8 fixed (clamp/256 = pitch units). */
    {
        struct Heightmap* hm = app->world ? app->world->heightmap : NULL;
        int level = player->grid_position.level;
        int orbit_ix = (int)app->orbit_x;
        int orbit_iz = (int)app->orbit_z;
        int orbit_tile_x = orbit_ix >> 7;
        int orbit_tile_z = orbit_iz >> 7;
        int orbit_y = app_world_height(app, orbit_ix, orbit_iz, level);
        int max_y = 0;
        int clamp;

        if( hm && orbit_tile_x > 3 && orbit_tile_z > 3 && orbit_tile_x < hm->size_x - 4 &&
            orbit_tile_z < hm->size_z - 4 )
        {
            for( int x = orbit_tile_x - 4; x <= orbit_tile_x + 4; x++ )
                for( int z = orbit_tile_z - 4; z <= orbit_tile_z + 4; z++ )
                {
                    /* Reference also bumps to level+1 on VisBelow (bridge)
                     * tiles; tile flags are applied at build time here, so
                     * the stored heights already match what is drawn. */
                    int y = orbit_y - heightmap_get(hm, x, z, level);
                    if( y > max_y )
                        max_y = y;
                }
        }
        clamp = max_y * 192;
        /* The same range the drag and the keys respect, in the 256ths this
         * clamp eases in -- `98048` and `32768` were exactly these two
         * products, written out. */
        if( clamp >
            app->revconfig_profile.camera.pitch_steepest * REVCONFIG_CAMERA_PITCH_CLAMP_SCALE )
            clamp =
                app->revconfig_profile.camera.pitch_steepest * REVCONFIG_CAMERA_PITCH_CLAMP_SCALE;
        if( clamp <
            app->revconfig_profile.camera.pitch_flattest * REVCONFIG_CAMERA_PITCH_CLAMP_SCALE )
            clamp =
                app->revconfig_profile.camera.pitch_flattest * REVCONFIG_CAMERA_PITCH_CLAMP_SCALE;
        if( clamp > app->camera_pitch_clamp )
            app->camera_pitch_clamp += (clamp - app->camera_pitch_clamp) / 24;
        else if( clamp < app->camera_pitch_clamp )
            app->camera_pitch_clamp += (clamp - app->camera_pitch_clamp) / 80;
    }

    pitch = app->orbit_pitch;
    if( app->camera_pitch_clamp / REVCONFIG_CAMERA_PITCH_CLAMP_SCALE > pitch )
        pitch = app->camera_pitch_clamp / REVCONFIG_CAMERA_PITCH_CLAMP_SCALE;
    yaw = app->orbit_yaw & 0x7ff;
    /*
     * Reference distance is `pitch * 3 + 600` (Client-TS camFollow) -- here
     * `pitch * pitch_distance + rest`, both stated by the profile -- later
     * scaled by a viewport-height zoom (`* viewportZoom / 256`,
     * client.method2068). `world_cam_zoom` is that 600 -- `[camera] rest=` --
     * moved by the wheel inside the `zoom_closest=`..`zoom_furthest=` band.
     *
     * `[camera] viewport_zoom=no` skips the interpolation: it is a later
     * client's way of zooming, and a revision that says it is the 2004 client
     * has said its camera has none. `rest=600` + `viewport_zoom=no` is then
     * Client-TS's expression exactly.
     *
     * The revision states that key and the player does not: a wheel switched
     * on in the settings moves world_cam_zoom inside its band and leaves this
     * term exactly as the revision left it.
     */
    distance = pitch * app->revconfig_profile.camera.pitch_distance + app->world_cam_zoom;
    if( app->revconfig_profile.camera.viewport_zoom )
        distance = distance * app_world_cam_dist_zoom(app) / 256;
    /*
     * The device's own dolly, last, over the whole distance -- pitch term
     * included, which is the point of it. The band under `world_cam_zoom`
     * moves the additive term only, so it buys less and less as the camera
     * tips over: overhead, `pitch * 3` is 1149 of the distance and no floor
     * the band can state is worth more than a few percent of it.
     * @see RevConfigCameraItem::distance_scale.
     */
    if( app->revconfig_profile.camera.distance_scale != REVCONFIG_CAMERA_DISTANCE_SCALE_DEFAULT )
        distance = distance * app->revconfig_profile.camera.distance_scale / 100;
    /* Not past the near plane. Anything closer than it is not a closer view,
     * it is a dropped one -- the anchor itself fails the `dz < near_plane_z`
     * test in ToriRS_WorldProject. The camera's own field, so TORIRS_NEAR_PLANE
     * moves both together. */
    if( distance < app->world_camera.near_plane_z )
        distance = app->world_camera.near_plane_z;
    /* Look-at height: the reference samples the ground under the ACTOR (not
     * under the eased anchor), takes the minimum over its footprint, then
     * drops 8, then the camera's own 50 — client.method1605:
     *   var14 = method1569(wv, actor.x, actor.z, level, footprintSize) - 8
     *   field753 = var14 - field999          (field999 defaults to 50)
     * method1569 degenerates to a single method1812 sample for a size-1
     * footprint, which the local player always has. */
    target_y =
        aboard_y_valid
            ? aboard_y
            : app_world_height(app, target_x, target_z, player->grid_position.level) - 8 - 50;
    target_x = (int)app->orbit_x;
    target_z = (int)app->orbit_z;

    ToriRS_OrbitCameraEye(
        target_x, target_y, target_z, pitch, yaw, distance, &app->world_camera_pos);
    app->world_camera.pitch = pitch;
    app->world_camera.yaw = yaw;

    /* TORIRS_ORBIT_DEBUG: the anchor's residual against the player it follows.
     * The orbit point is what the whole eye is built around, and an offset one
     * is invisible in a still frame — it only shows as the model swinging in a
     * circle while you rotate, which reads as "the camera orbits the tile".
     * This is the number that says whether it does: settled, it must go to 0. */
    if( torirs_env_orbit_debug() )
        TORIRS_LOG(
            "orbit: anchor=(%.3f,%.3f) player=(%d,%d) residual=(%.3f,%.3f) "
            "pitch=%d yaw=%d dist=%d look_y=%d eye=(%d,%d,%d)\n",
            (double)app->orbit_x,
            (double)app->orbit_z,
            (int)player->draw_position.x,
            (int)player->draw_position.z,
            (double)((float)(int)player->draw_position.x - app->orbit_x),
            (double)((float)(int)player->draw_position.z - app->orbit_z),
            pitch,
            yaw,
            distance,
            target_y,
            app->world_camera_pos.x,
            app->world_camera_pos.y,
            app->world_camera_pos.z);
}

/* Apply queued WorldEventKind_EntityRemoved: free the DYNAMIC scene element.
 * Must run before World_ResetSceneAlloc (which asserts the queue is empty) and
 * after bulk despawns in App_WorldRebuildShift — silent drops used to orphan
 * elements across ClearPool(STATIC) and climb the scene id high-water mark.
 * Parameterized over the world because every view's World feeds the one shared
 * scene: a boat view drains its own queue through here before its deck rebuild
 * (SAILING_PLAN C2), the root through the wrapper below. */
void
App_WorldDrainEntityRemovedFor(
    struct App* app,
    struct World* world)
{
    int count;

    assert(app);
    assert(world);

    count = World_EventsCount(world);
    for( int i = 0; i < count; i++ )
    {
        const struct World_Event* ev = World_EventsPeek(world, i);
        if( ev->kind == WorldEventKind_EntityRemoved && ev->element_id >= 0 )
        {
            /* The world releases its pool slot when it queues the removal, but
             * preserves an immutable NPC copy on that event. Prefer it over a
             * live lookup: the slot may be absent or may already belong to a
             * different NPC by the time this render-side drain runs. */
            if( app->plugins )
            {
                struct WorldEntity_NPC const* going = ev->removed_npc;
                struct ToriRS_NpcSnapshot snap;
                if( !going )
                    going = World_NpcGetByElementId(world, ev->element_id, NULL);
                if( going )
                    app_plugin_fill_npc_for_world(app, world, going, &snap);
                else
                {
                    memset(&snap, 0, sizeof(snap));
                    snap.server_slot = -1;
                    snap.npc_id = -1;
                    snap.base_npc_id = -1;
                    snap.element_id = ev->element_id;
                    /* Explicitly, because a zeroed health_ratio is the value
                     * that means DEAD and this snapshot knows nothing at all.
                     * A loot tracker reading it as a kill would open a record
                     * for every entity the render side cleaned up late. */
                    snap.health_ratio = -1;
                    snap.health_scale = -1;
                }
                PluginHost_NpcDespawn(app->plugins, &snap);
            }
            app_entity_spotanim_drop(app, ev->element_id);
            app_seq_bind_pending_drop(app, ev->element_id);
            if( app->scene )
                ToriDraw_SceneElementRemove(app->scene, ev->element_id);
        }
        else if( ev->kind == WorldEventKind_SpotanimStarted && ev->element_id >= 0 && app->scene )
        {
            /* The delayed map spotanim parked in app_world_spawn_spotanim_now
             * is drawing for the first time this cycle: rewind to frame 0 and
             * hand it back to the per-element tick.
             *
             * The rewind is not redundant with parking it. A sequence that was
             * not resident at spawn binds later through seq_bind_pending, and
             * that path catches the animation up by (now - start_cycle) — a
             * span measured from SPAWN, which for a delayed spotanim is the
             * wrong origin and can land it mid-sequence or past the end. Frame
             * 0 here is what makes the start independent of when the seq
             * happened to load. */
            struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, ev->element_id);
            if( el && el->anim_external )
            {
                el->anim_frame = 0;
                el->anim_cycle = 0;
                el->anim_external = false;
                ToriDraw_SceneAnimListInvalidate(app->scene);
            }
        }
    }
    World_EventsClear(world);
}

void
App_WorldDrainEntityRemoved(struct App* app)
{
    assert(app);
    /* No world before the first root rebuild is a legitimate boot state, not
     * a caller bug — the drain is simply a no-op then. */
    if( !app->world )
        return;
    App_WorldDrainEntityRemovedFor(app, app->world);
}

/*
 * Whether the water and lava texels are scrolled on the CPU this cycle.
 *
 * The GPU renderers (GLES2, GL3, D3D9) animate a texture in the shader from a
 * per-vertex scroll rate and a frame clock; they upload the texels once and
 * never read them again, so ToriDraw_TextureMapAnimate's per-cycle rotate of
 * every animated texture (128 KB of traffic per 128x128 texture) was dead
 * work on those lanes. The software rasteriser samples the texels directly
 * and still needs it. TORIRS_TEXANIM_CPU=1 forces the scroll on every lane
 * (the control arm on a GPU lane). Read once.
 */
static int
app_texture_anim_on_cpu(const struct App* app)
{
    static int forced = -1;

    assert(app);
    if( forced < 0 )
    {
        char const* v = getenv("TORIRS_TEXANIM_CPU");

        forced = (v && v[0] == '1') ? 1 : 0;
    }
    if( forced )
        return 1;
    return !app->renderer_animates_textures;
}

static void
app_world_frame(
    struct App* app,
    int cycles,
    float frame_cycles)
{
    struct World* world = app->world;

    /*
     * World entities (sailing, SAILING_PLAN C1): one interpolation step for
     * every live view's boats — root first, nested views via the worklist —
     * with heights re-sampled from the terrain under each hull.
     *
     * Ahead of the world gate, not behind it, because this call is what
     * advances the cycle clock that WORLDENTITY_INFO stamps its targets
     * with. The deob bumps client.field742 unconditionally in doCycle; gate
     * it and a rebuild — exactly when boats are arriving — freezes the clock
     * while packets keep stamping targets against it, so every segment that
     * lands during the gap shares one enqueue cycle and the whole backlog
     * expires the instant the clock moves again. The walk below is a no-op
     * when no entity is live, which is the boot case this gate covers.
     */
    Wevs_Frame(&app->wevs, frame_cycles, app_wev_terrain_height, app);
    /* The bob rides the same clock Wevs_Frame just advanced. */
    app_wev_advance_bobs(app);

    if( !app->world_active || !app->world_view_valid || !world )
        return;

    /* Mirror the local pid so the render cycle's dynamic pass can register the
     * local player first (reference addPlayers(true) precedence). */
    world->local_pid = app->esync.local_pid;

    /*
     * Movement, then the cycle work -- rev-239's steady-state order, where a
     * rendered frame runs client.method2324 -> method1894 and the logic loop
     * then runs method3606 -> method3520 for the cycles that elapsed.
     *
     * Movement is integrated here rather than inside World_Cycle because one
     * call of this covers however much of a 20ms cycle the frame actually
     * took. Advancing per whole cycle instead rounds every frame's travel down,
     * which is what made a player following a moving NPC drift back and then
     * lurch forward. Ahead of World_Cycle so the painter dynamics it publishes
     * describe where the actors are now, not where they were a frame ago.
     */
    World_MoversAdvance(world, frame_cycles);
    /* Membership before registration (SAILING_PLAN C5.1): the actors have just
     * been moved, and World_Cycle's dynamic pass has to already know which of
     * them belong to a deck rather than to the mainland. */
    app_wev_route_actors(app);
    World_Cycle(world, cycles);
    /* Only the root world is cycled; a deck advances nothing of its own but
     * still needs its painter's dynamic half rebuilt every tick. */
    app_wev_cycle_views(app);
    World_LocChangesTick(world, cycles, app_loc_change_apply_cb, app);
    App_WorldDrainEntityRemoved(app);

    app_world_sync_positions(app);
    /* Exactly one of these does anything: the follow cam returns early while a
     * cutscene is up, and the cinema cam returns early when one is not. */
    app_world_camera_cinema(app);
    app_world_camera_follow(app);
    /* Publish the orbit angles to the CS2 host (CAM_GETANGLE_XA/YA, CAM_GETYAW)
     * and take back anything CAM_FORCEANGLE snapped since the last tick. Both
     * sides speak the reference's orbitCameraPitch/Yaw units, which is what
     * app->orbit_pitch/orbit_yaw already hold, so no conversion is involved.
     * Order matters: mirror first, then apply a force, so a snap issued this
     * tick is not read back as "the camera moved there on its own". */
    RS_CS2Host_SetCameraAngles(&app->host, app->orbit_pitch, app->orbit_yaw);
    {
        int forced_pitch, forced_yaw;
        if( RS_CS2Host_TakeCameraForce(&app->host, &forced_pitch, &forced_yaw) )
        {
            app->orbit_pitch = forced_pitch;
            app->orbit_yaw = forced_yaw & 0x7ff;
            app->orbit_pitch_vel = 0;
            app->orbit_yaw_vel = 0;
        }
    }
    app_world_sync_entity_animations(app);
    /*
     * TORIRS_ELEMENT_ALIAS_CHECK=1: assert that no two live world entities
     * reference the same scene element.
     *
     * Scene element ids are recycled, and three separate subsystems key off
     * them -- the model (AppEntitySpotanim), the animation (AppSeqBindPending)
     * and the POSITION written every frame from the entity that owns the
     * element. If a despawn ever leaves an entity holding an id that has since
     * been handed to somebody else, both write to it: the survivor's model gets
     * swapped, its animation replaced, and it is dragged around by the other
     * entity's movement. "I called my familiar and the Queen's head moved with
     * me" is that last symptom. The first two are fixed by identity checks; this
     * detector is what proves whether the underlying aliasing still happens.
     */
    if( app->world && torirs_env_element_alias_check() )
    {
        struct World_EntityPool* pools[2] = { &app->world->entities.player,
                                              &app->world->entities.npc };
        static int seen_element[8192];
        static int seen_kind[8192];
        static int stamp = 0;
        stamp++;
        for( int k = 0; k < 2; k++ )
        {
            for( int i = World_EntityPoolHead(pools[k]); i != WORLD_ENTITY_NIL;
                 i = World_EntityPoolNext(pools[k], i) )
            {
                /* Both entity structs open with `int element_id`. */
                int const el = *(int const*)World_EntityPoolGet(pools[k], i);
                if( el < 0 || el >= (int)(sizeof(seen_element) / sizeof(seen_element[0])) )
                    continue;
                if( seen_element[el] == stamp )
                    TORIRS_LOG(
                        "element_alias: element=%d claimed by TWO live entities "
                        "(kinds %d and %d) -- position/model/anim will fight\n",
                        el,
                        seen_kind[el],
                        k);
                seen_element[el] = stamp;
                seen_kind[el] = k;
            }
        }
    }
    app_world_sync_entity_spotanims(app);

    /* Expire P_LOCMERGE markers once the ride window ends. */
    {
        struct World_EntityPool* pool = &world->entities.player;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Player* p = World_EntityPoolGet(pool, i);
            if( !p || p->loc_merge_id < 0 )
                continue;
            if( world->cycle < p->loc_start_cycle || world->cycle >= p->loc_stop_cycle )
                p->loc_merge_id = -1;
        }
    }

    for( int c = 0; c < cycles; c++ )
        app_world_tick_animations(app);

    /* Texture scroll (water/lava): dat2 texture defs carry direction/speed;
     * the map advances them per elapsed cycle (v1 runescape.c:3893). */
    if( cycles > 0 && app_texture_anim_on_cpu(app) )
    {
        struct ToriDraw_TextureState* tex_state = ToriDraw_SceneTexState(app->scene);
        /* The rotate buffer is the caller's now -- see the header. This client
         * does animate textures, so it keeps the 64 KB it always had. */
        static int tex_anim_scratch[TORIDRAW_TEXTURE_ANIM_SCRATCH_INTS];
        if( tex_state )
            ToriDraw_TextureMapAnimate(
                &tex_state->texture_map,
                cycles,
                tex_anim_scratch,
                TORIDRAW_TEXTURE_ANIM_SCRATCH_INTS);
    }

    app->need_redraw = 1;
}

static int
app_measure_text_cb(
    void* user,
    int font_id,
    char const* text)
{
    struct App* app = (struct App*)user;
    struct ToriDraw_Font* font = ToriDraw_SceneFontGet(app->scene, font_id);
    if( !font )
        return 0;
    assert(text);
    return ToriDraw2D_MeasureString(font, text);
}

#include "app_minimenu.u.c"

void
App_PlaySound(
    struct App* app,
    int sound_id,
    int loops,
    int delay)
{
    assert(app);
    if( getenv("TORIRS_SOUND_DEBUG") )
        TORIRS_LOG("sound: synth=%d loops=%d delay=%d\n", sound_id, loops, delay);
    RS_Audio_Synth(&app->audio, sound_id, loops, delay);
}

void
App_PlaySoundAt(
    struct App* app,
    int sound_id,
    int loops,
    int delay,
    int tile_x,
    int tile_z,
    int radius,
    int inner)
{
    assert(app);
    RS_Audio_SynthAt(&app->audio, sound_id, loops, delay, tile_x, tile_z, radius, inner);
}

void
App_SetAudioFeedback(
    struct App* app,
    const struct ToriRS_AudioFeedback* feedback)
{
    assert(app);
    if( feedback )
        app->audio_feedback = *feedback;
    else
        memset(&app->audio_feedback, 0, sizeof(app->audio_feedback));
}

void
App_SetAudioDevicePresent(
    struct App* app,
    bool present)
{
    assert(app);
    RS_Audio_SetDevicePresent(&app->audio, present);
}

void
App_PlaySong(
    struct App* app,
    int song_id,
    bool loop,
    int fade_out_ms,
    int fade_in_ms)
{
    assert(app);
    RS_Audio_Song(&app->audio, song_id, loop, fade_out_ms, fade_in_ms);
}

void
App_PlaySongWithSecondary(
    struct App* app,
    int primary_id,
    int secondary_id,
    int fade_out_ms,
    int fade_in_ms)
{
    assert(app);
    RS_Audio_SongWithSecondary(&app->audio, primary_id, secondary_id, fade_out_ms, fade_in_ms);
}

void
App_SwapSong(
    struct App* app,
    int fade_out_ms,
    int fade_in_ms)
{
    assert(app);
    RS_Audio_SongSwap(&app->audio, fade_out_ms, fade_in_ms);
}

void
App_PlayJingle(
    struct App* app,
    int jingle_id,
    int length_ms)
{
    assert(app);
    RS_Audio_Jingle(&app->audio, jingle_id, length_ms);
}

void
App_StopSong(
    struct App* app,
    int fade_out_ms)
{
    assert(app);
    RS_Audio_SongStop(&app->audio, fade_out_ms);
}

void
App_SetAmbientSound(
    struct App* app,
    int sound_id,
    int fade_ms)
{
    assert(app);
    RS_Audio_SetAmbient(&app->audio, sound_id, fade_ms);
}

int
App_DrainAudio(
    struct App* app,
    struct ToriRS_AudioCommand* out,
    int max)
{
    assert(app);
    return ToriRS_AudioQueue_Drain(&app->audio_out, out, max);
}

/* Most hooks a single canvas change dispatches. The gameframe registers one
 * onResize per open interface root (script 901 does it for the toplevel; panels
 * that lay themselves out register their own), so the real count is single
 * digits — this is a "something is looping" bound, not a budget. */
#define APP_RESIZE_HOOK_MAX 256

/* Dispatch a queue selected by the completed trigger=true layout pass. Ids,
 * rather than component-array indices, survive cc_create/cc_delete reallocating
 * the tree while a listener runs. */
static void
app_dispatch_resize_hook_ids(
    struct App* app,
    int const* ids,
    int count)
{
    assert(app);
    assert(ids);
    assert(count >= 0);

    for( int i = 0; i < count; i++ )
    {
        int32_t idx = UITree_FindByComponentId(app->tree, ids[i]);
        if( idx < 0 )
            continue;
        RS_CS2_DispatchHook(
            &app->host,
            &app->runner,
            ids[i],
            &UITree_Hooks(&app->tree->components[idx])->on_resize);
    }
}

int
App_SetCanvasSize(
    struct App* app,
    int width,
    int height)
{
    struct UITreeResizeHookSnapshot resize_before[APP_RESIZE_HOOK_MAX];
    int changed_ids[APP_RESIZE_HOOK_MAX];
    int resize_before_count = 0;
    int changed_count = 0;

    assert(app);

    /*
     * The floor is the FRAME's, and a plugin layout brings its own.
     *
     * APP_CANVAS_MIN_W/H is a fact about a revconfig gameframe -- its children
     * are insets off 765x503 and a smaller canvas gives them zero-sized
     * viewports -- so it is the right floor for exactly as long as that frame
     * is the one on screen. While a plugin arranges the frame it is not, and
     * clamping a phone-shaped layout up to a desktop canvas is how a mobile
     * frame ends up letterboxed inside the size it was written to avoid.
     *
     * And the frame's floor is not the CANVAS's: the lane docks its popout
     * strip inside the canvas, so the canvas has to hold the frame AND the
     * strip. @see App_CanvasFloorWidth.
     */
    {
        int min_w = App_CanvasFloorWidth(app);
        int min_h = APP_CANVAS_MIN_H;
        int plugin_min_w = 0;

        /* Height only: the width floor already carries the frame's own and the
         * strip's, and the strip is full-height by definition, so there is
         * nothing to add on this axis. */
        App_PluginLayoutMinSize(app, &plugin_min_w, &min_h);
        if( width < min_w )
            width = min_w;
        if( height < min_h )
            height = min_h;
    }

    /* All three copies are tested, not just the layout one: they are set
     * together here and nowhere else, so disagreement means somebody wrote one
     * of them directly and this is where that gets repaired. */
    if( width == UITREE_LAYOUT_ROOT_W && height == UITREE_LAYOUT_ROOT_H &&
        app->host.viewport_w == width && app->host.viewport_h == height )
        return 0;

    if( app->tree && app->tree->component_count > 0 )
    {
        /* Keep the cached pre-pass dimensions even when another mutation has
         * already invalidated layout. method3791 snapshots its old computed
         * fields immediately before recomputing; normalising pending changes
         * against the old canvas here would incorrectly erase a real resize. */
        resize_before_count =
            UITree_SnapshotResizeHooks(app->tree, resize_before, APP_RESIZE_HOOK_MAX);
        if( app->tree->resize_hooks.count > APP_RESIZE_HOOK_MAX )
            TORIRS_ERR("resize: more than %d onResize hooks; truncating\n", APP_RESIZE_HOOK_MAX);
    }

    UITree_LayoutSetRootSize(width, height);
    app->host.viewport_w = width;
    app->host.viewport_h = height;

    if( app->tree && app->tree->component_count > 0 )
    {
        /* Resolve BEFORE dispatching: the listeners read their own box back
         * through if_getwidth/if_getheight (toplevel_resize's very first
         * statements), so they have to see the new size, not the old one. */
        UITree_LayoutInvalidate(app->tree);
        UITree_LayoutResolve(app->tree, 0, 0, width, height);
        /* Physical canvas resize is method6192's trigger=true path. Build the
         * whole changed-size queue before its first listener runs: callbacks
         * can mutate the tree, but cannot retroactively change events already
         * queued by the completed layout pass. */
        changed_count = UITree_CollectResizedHookIds(
            app->tree, resize_before, resize_before_count, 1, changed_ids, APP_RESIZE_HOOK_MAX);
        app_dispatch_resize_hook_ids(app, changed_ids, changed_count);
        /* And again after: the listeners are all if_setsize/if_setposition. */
        UITree_LayoutInvalidate(app->tree);
        UITree_LayoutResolve(app->tree, 0, 0, width, height);
    }

    app->need_redraw = 1;
    /* TORIRS_REPORT, not TORIRS_LOG: the shipping lane compiles -DNDEBUG,
     * which strips TORIRS_LOG -- and this is the one line that says whether a
     * resize arrived at all, in the build every report comes from. */
    if( getenv("TORIRS_RESIZE_DEBUG") )
        TORIRS_REPORT("canvas: %dx%d\n", width, height);
    return 1;
}

/* True when this node, or anything it hangs off, is hidden — i.e. it does not
 * reach the screen. `behavior.hide` alone is not that test: hiding a container
 * leaves every descendant's own flag clear and its stale abs_* box intact, so a
 * caller that reads geometry off the flat component array sees a hidden
 * subtree's boxes as live ones. */
#if defined(TORIRS_CANVAS_CAPTURE)
#include "../tools/perf/canvas_chain_capture.u.h"
#endif
int
App_MeasureRightChromeStripWidth(struct App const* app)
{
#if defined(TORIRS_CANVAS_CAPTURE)
    canvas_chain_capture(app->tree, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
#endif
    if( app->tree && UITree_CanvasQueryCompactEnabled() )
        return UITree_CanvasMeasureCompact(app->tree, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H)
            .strip;
    /*
     * Memo, keyed on the tree publication this answer was read from.
     *
     * The scan below is over every component -- 7,142 in a logged-in frame --
     * and App_SyncFixedChromeInset asks for it once per frame from the main
     * loop, so it ran in full on every frame whether or not anything had moved.
     * An EIP profile of an in-world frame put it at 4.1% of non-raster work.
     *
     * The three key terms are the ones the scan actually reads: `dirty_gen`
     * covers the hide/free flags, `layout_resolve_seq` covers the resolved
     * boxes (a re-layout moves them without touching dirty_gen -- the same
     * reason the emit retain gate needs both), and `component_count` covers a
     * tree that grew or was rebuilt. Keyed on the tree pointer too, so two
     * trees cannot read each other's answer.
     */
    static struct UITree const* memo_tree = NULL;
    static uint32_t memo_dirty_gen;
    static uint32_t memo_layout_seq;
    static uint32_t memo_count;
    static int memo_value;

    int canvas_w;
    int canvas_h;
    int best;
    uint32_t i;

    assert(app);
    if( !app->tree || app->tree->component_count == 0 )
        return 0;

    if( memo_tree == app->tree && memo_dirty_gen == app->tree->dirty_gen &&
        memo_layout_seq == app->tree->layout_resolve_seq &&
        memo_count == app->tree->component_count )
        return memo_value;

    canvas_w = UITREE_LAYOUT_ROOT_W;
    canvas_h = UITREE_LAYOUT_ROOT_H;
    best = 0;

    /* Script 5355 docks the popout strip on the canvas right edge at full
     * height. Measure that geometry rather than naming interface 728 or the
     * 42/312 widths the CS2 embeds — those are content, and the strip width
     * changes when a panel opens. The mode checks matter: mounted interface
     * roots also commonly fill from a positive X to the right edge. Treating
     * one of those fill-width roots as chrome makes the canvas feed back into
     * its own next width and grow every frame. The strip itself is fixed-width,
     * parent-height, and right-anchored. */
    for( i = 0; i < app->tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &app->tree->components[i];
        int right;
        int w;

        /* Geometry first, visibility last. Every test here is an independent
         * reject, so the order is free to choose — and all of these read the
         * component record already in cache, while the visibility test below
         * chases `parent` to the root, a fresh cache line per hop. Almost
         * nothing in the tree is a full-height right-docked fixed-width box, so
         * paying for the ancestor walk on every component — 7,142 of them in a
         * logged-in frame — was most of this function. */
        w = c->position.abs_w;
        if( w <= 0 || c->position.abs_x <= 0 || c->position.width_mode != 0 ||
            c->position.height_mode != 1 || c->position.x_mode != 2 )
            continue;
        /* Near full canvas height: the strip, not a minimap orb or tab icon. */
        if( c->position.abs_h < canvas_h - 2 )
            continue;
        right = c->position.abs_x + w;
        if( right != canvas_w )
            continue;
        /* A wider candidate cannot lose to the ancestor walk, so only ask the
         * question when the answer can change the result. */
        if( w <= best )
            continue;
        /* Ancestors too: a speculatively baked panel (the CS2 runtime bakes a
         * pack the moment a script touches it) is hidden at its group root,
         * while the right-docked column inside it keeps a clear flag and a
         * full-height box — the exact signature this loop looks for. Measuring
         * that column grew the fixed canvas by a panel that was never open. */
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CHROME_STRIP_VISCHECK, 1);
        if( UITree_ComponentHiddenOrOrphaned(app->tree, (int32_t)i) )
            continue;
        best = w;
    }

    memo_tree = app->tree;
    memo_dirty_gen = app->tree->dirty_gen;
    memo_layout_seq = app->tree->layout_resolve_seq;
    memo_count = app->tree->component_count;
    memo_value = best;
    return best;
}

/*
 * The width the LANE's own frame needs, when the canvas it was given is too
 * narrow for it.
 *
 * The resizable OldSchool toplevels are not fluid all the way down: each one
 * carves the popout strip off the canvas (a full-height layer authored as
 * "parent width - <strip>") and then lays a single fixed-size block inside
 * what is left, clamped up to 765x503 by its own resize script. Handed less
 * than that, the script writes the floor anyway and the layout CENTRES the
 * block, so it hangs off both sides -- and every lane widget anchored to its
 * right edge (the XP counter, the tracker overlays) lands to the RIGHT of the
 * area a plugin frame was handed, under that frame's own right-hand furniture.
 *
 * Nobody notices this natively because APP_CANVAS_MIN_W is that same 765: the
 * canvas floor already holds the block. A plugin frame that declares
 * TORIRS_FRAME_CANVAS_WINDOW replaces that floor with its OWN minimum, and a
 * frame written for a phone declares a smaller one -- legitimately, for its
 * own furniture, which is all a frame offer can speak for. It does not replace
 * the lane's toplevel; it arranges over it. So the lane's floor still applies,
 * and this is where it is read: not from a constant (the mobile toplevel is
 * fluid and must stay able to lay out at phone widths) but from the lane's own
 * layout, at the one moment it states the number -- when the block it lays in
 * the carved area comes out WIDER than the carved area.
 *
 * 0 when everything fits, which is also the answer when there is no such
 * block. Only the overflow case is measurable: a block that fits tells us
 * nothing about how small it could have been, and needs nothing.
 */
int
App_MeasureLaneFrameCoreWidth(struct App const* app)
{
#if defined(TORIRS_CANVAS_CAPTURE)
    canvas_chain_capture(app->tree, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
#endif
    if( app->tree && UITree_CanvasQueryCompactEnabled() )
        return UITree_CanvasMeasureCompact(app->tree, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H)
            .core;
    /* Memoised on the same three terms as the strip scan above, and for the
     * same reason: this is a full pass over every component, asked once per
     * frame from App_CanvasFloorWidth. @see App_MeasureRightChromeStripWidth. */
    static struct UITree const* memo_tree = NULL;
    static uint32_t memo_dirty_gen;
    static uint32_t memo_layout_seq;
    static uint32_t memo_count;
    static int memo_value;

    int canvas_w;
    int canvas_h;
    int strip;
    int best;
    uint32_t i;

    assert(app);
    if( !app->tree || app->tree->component_count == 0 )
        return 0;

    if( memo_tree == app->tree && memo_dirty_gen == app->tree->dirty_gen &&
        memo_layout_seq == app->tree->layout_resolve_seq &&
        memo_count == app->tree->component_count )
        return memo_value;

    canvas_w = UITREE_LAYOUT_ROOT_W;
    canvas_h = UITREE_LAYOUT_ROOT_H;
    strip = App_MeasureRightChromeStripWidth(app);
    best = 0;

    for( i = 0; i < app->tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &app->tree->components[i];
        struct UITreeComponent const* p;

        /* A block with a size of its own -- not one that follows whatever it
         * is put in, which by construction can never overflow. */
        if( c->freed || c->position.width_mode != 0 || c->position.abs_w <= 0 )
            continue;
        if( c->parent < 0 || (uint32_t)c->parent >= app->tree->component_count )
            continue;
        p = &app->tree->components[c->parent];
        if( c->position.abs_w <= p->position.abs_w )
            continue;
        /*
         * The parent must be the CARVED AREA: full-canvas height, docked on
         * the left edge, and exactly the strip narrower than the canvas. That
         * signature is what separates the lane's frame from every other fixed
         * box that is bigger than the container it hangs in -- a 512x334 modal
         * parked inside the 42-column popout strip is the shape this would
         * otherwise measure, and it is not short of anything.
         */
        if( p->position.width_mode != 1 || p->position.height_mode != 1 )
            continue;
        if( p->position.abs_x != 0 || p->position.abs_w != canvas_w - strip )
            continue;
        if( p->position.abs_h < canvas_h - 2 )
            continue;
        if( c->position.abs_w <= best )
            continue;
        /* Ancestors too, for the reason the strip scan gives: a speculatively
         * baked panel keeps live-looking boxes under a hidden root. */
        if( UITree_ComponentHiddenOrOrphaned(app->tree, (int32_t)i) )
            continue;
        best = c->position.abs_w;
    }

    memo_tree = app->tree;
    memo_dirty_gen = app->tree->dirty_gen;
    memo_layout_seq = app->tree->layout_resolve_seq;
    memo_count = app->tree->component_count;
    memo_value = best;
    return best;
}

int
App_FixedCanvasWidth(struct App const* app)
{
    assert(app);
    return APP_CANVAS_MIN_W + App_MeasureRightChromeStripWidth(app);
}

int
App_CanvasFloorWidth(struct App const* app)
{
    int frame_min_w = APP_CANVAS_MIN_W;
    int frame_min_h = APP_CANVAS_MIN_H;

    assert(app);
    App_PluginLayoutMinSize(app, &frame_min_w, &frame_min_h);
    /* And never below the LANE's own frame, which a plugin layout arranges
     * over rather than replaces: its widgets are still laid out by the lane's
     * toplevel, and a toplevel handed less than it can use spills them out of
     * the area the plugin frame was given. @see App_MeasureLaneFrameCoreWidth. */
    {
        int const lane_core_w = App_MeasureLaneFrameCoreWidth(app);
        if( frame_min_w < lane_core_w )
            frame_min_w = lane_core_w;
    }
    /* The strip is CARVED OUT of the canvas -- the resizable toplevels lay
     * their own children out beside it (interface 728's rail is 42 columns of
     * the canvas on both of them, and a plugin layout gets the same canvas
     * less the same rail as its FRAME_BUILD area), so a canvas of exactly the
     * frame's floor leaves the frame 42 short of it. */
    return frame_min_w + App_MeasureRightChromeStripWidth(app);
}

int
App_SyncFixedChromeInset(struct App* app)
{
    int want_w;

    assert(app);
    if( App_WindowMode(app) != CS2VM_WINDOW_MODE_FIXED )
        return 0;
    want_w = App_FixedCanvasWidth(app);
    if( want_w == UITREE_LAYOUT_ROOT_W && APP_CANVAS_MIN_H == UITREE_LAYOUT_ROOT_H )
        return 0;
    return App_SetCanvasSize(app, want_w, APP_CANVAS_MIN_H);
}

int
App_SyncResizableCanvasFloor(struct App* app)
{
    int want_w;

    assert(app);
    if( App_WindowMode(app) != CS2VM_WINDOW_MODE_RESIZABLE )
        return 0;
    want_w = App_CanvasFloorWidth(app);
    /* Raise only. The canvas a resizable window follows is the window's, and
     * the window is the authority whenever it is big enough; shrinking back
     * belongs to the next TORIRS_CMD_WINDOW_RESIZE, which carries the size the
     * window actually is and is clamped by this same floor on the way in. */
    if( UITREE_LAYOUT_ROOT_W >= want_w )
        return 0;
    return App_SetCanvasSize(app, want_w, UITREE_LAYOUT_ROOT_H);
}

/* One window axis through the interface scale. Rounds down, so 100% is exact
 * and every other scale errs towards a slightly larger element rather than a
 * canvas that overruns the window it is stretched into. */
static int
app_ui_scaled_axis(
    struct App const* app,
    int window_px)
{
    int const percent = RS_CS2Host_UiScalePercent(&app->host);

    assert(app);
    assert(percent >= RS_CS2_UI_SCALE_MIN);
    if( window_px <= 0 )
        return window_px;
    return window_px * 100 / percent;
}

int
App_SyncUiScale(struct App* app)
{
    assert(app);
    if( !app->host.ui_scale_dirty )
        return 0;
    /* Nothing has told us how big the window is yet — a boot-time restore from
     * preferences lands here before the shell's first resize. Keep the flag:
     * the scale is real, it just has nothing to divide yet. */
    if( app->window_w <= 0 || app->window_h <= 0 )
        return 0;
    app->host.ui_scale_dirty = false;
    return App_SetCanvasSize(
        app, app_ui_scaled_axis(app, app->window_w), app_ui_scaled_axis(app, app->window_h));
}

int
App_WindowMode(struct App const* app)
{
    assert(app);
    return app->host.window_mode;
}

void
App_SetBootWindowMode(
    struct App* app,
    int mode)
{
    assert(app);
    if( mode != CS2VM_WINDOW_MODE_FIXED && mode != CS2VM_WINDOW_MODE_RESIZABLE )
        return;
    app->host.window_mode = mode;
    app->host.default_window_mode = mode;
    /*
     * Record it on the App's OWN config too, because that is where the boot
     * statement is looked for later.
     *
     * `app->cfg` is a COPY taken by App_Init, so a caller that settles the mode
     * after App_Init -- which the shell must, since the CS1 lane's mode is
     * derived from the resolved ui logic -- writes only its own local struct,
     * and the App is left believing nobody stated a mode. The preferences
     * restore (`app->cfg.window_mode` in Task_AppBoot) then hands the saved
     * default the lane instead, and a fixed-only 2004 frame ends up in a
     * resizable window it has no layout for: a 765x503 island in a grey field.
     *
     * Deliberately NOT window_mode_dirty: the shell is the caller and applies
     * the platform side directly. Raising it here would make the boot config
     * indistinguishable from a clientscript's SETWINDOWMODE on the next drain.
     */
    app->cfg.window_mode = mode;
}

void
App_SyncPluginLayoutCanvas(struct App* app)
{
    int want;

    assert(app);
    /*
     * A RELEASE does not force resizable back on.
     *
     * The lane had a window mode before any plugin frame was committed -- a dat1
     * world is fixed and says so in its manifest -- and a released layout
     * should hand that back rather than leave the client in whatever mode the
     * plugin wanted. A committed frame states its mode; native restates the
     * lane's default.
     *
     * Except that on a CS1 lane the native frame does not HAVE a resizable
     * mode to restate. Its gameframe is a baked 765x503 layout wrapped in one
     * `fixed_shell` rs_layer (revconfig `*_dat1_ui.ini`) that clips every
     * surface under it, and there is no CS2 setwindowmode and no relayout hook
     * on that lane to make it anything else -- so a bigger canvas leaves the
     * 2004 frame anchored in the top-left corner and the rest of the window
     * flat grey. That is not the lane's default being honoured, it is a mode
     * the frame on screen cannot be in.
     *
     * So the native answer on CS1 is FIXED whatever the default says, and the
     * default -- the command line, the manifest, or the saved preference,
     * whose out-of-the-box value is resizable -- only gets a say on CS2, where
     * the toplevel really does lay itself out to the canvas it is handed.
     *
     * The escape hatch is a PLUGIN frame that declares
     * TORIRS_FRAME_CANVAS_WINDOW (gameframe-layout's Modern Resizable, the
     * Stone Drawer): it takes the other branch, so selecting one still makes
     * this lane resizable, and releasing it comes back here and pins fixed
     * again.
     */
    want = !app->plugin_frame_active
               ? (App_UiLogic(app) == APP_UI_LOGIC_CS1 ? CS2VM_WINDOW_MODE_FIXED
                                                       : app->host.default_window_mode)
           : app->plugin_layout_canvas == TORIRS_FRAME_CANVAS_FIXED ? CS2VM_WINDOW_MODE_FIXED
                                                                    : CS2VM_WINDOW_MODE_RESIZABLE;
    if( app->host.window_mode == want )
        return;
    /*
     * Re-asserted every frame the committed plugin frame stands, not only when it changes.
     *
     * The window mode has other writers: a clientscript's setwindowmode, and
     * on login the saved Display-panel layout being applied. Either of those
     * lands AFTER frame selection, and a frame that only spoke once loses to them
     * silently -- the plugin goes on laying its frame out at the canvas it
     * asked for while the client uses the window's, which puts a 765x503 frame
     * in the corner of a 1440x900 canvas with the lane's own chrome spread
     * around it. Two gameframes at once, and neither wrong from where it is
     * standing.
     *
     * Whoever holds the frame decides how big the canvas is. Restating it is
     * how that stays true for longer than one frame.
     */
    app->host.window_mode = want;
    app->host.window_mode_dirty = true;
}

int
App_PluginLayoutFixedSize(
    struct App const* app,
    int* out_w,
    int* out_h)
{
    assert(app);
    if( !app->plugin_frame_active )
        return 0;
    if( app->plugin_layout_canvas != TORIRS_FRAME_CANVAS_FIXED )
        return 0;
    if( app->plugin_layout_fixed_w <= 0 || app->plugin_layout_fixed_h <= 0 )
        return 0;
    if( out_w )
        *out_w = app->plugin_layout_fixed_w;
    if( out_h )
        *out_h = app->plugin_layout_fixed_h;
    return 1;
}

int
App_PluginLayoutMinSize(
    struct App const* app,
    int* out_w,
    int* out_h)
{
    assert(app);
    if( !app->plugin_frame_active )
        return 0;
    if( app->plugin_layout_canvas != TORIRS_FRAME_CANVAS_WINDOW )
        return 0;
    /* A claim that named no minimum gets the client's, rather than a floor of
     * zero: "I did not say" and "any size at all" are different statements, and
     * only one of them should be able to produce a 1x1 canvas. */
    if( app->plugin_layout_fixed_w <= 0 || app->plugin_layout_fixed_h <= 0 )
        return 0;
    if( out_w )
        *out_w = app->plugin_layout_fixed_w;
    if( out_h )
        *out_h = app->plugin_layout_fixed_h;
    return 1;
}

void
App_PluginLayoutTick(struct App* app)
{
    int frame_candidate;

    assert(app);

    if( !app->plugins )
        return;
    /*
     * Name the cache gameframe's regions before providers query them. Frame
     * provision and the emit-fence rebind read these stamps. The
     * tree keeps the binder so the fence can re-run it after a rebuild.
     */
    if( app->app_state == APP_STATE_READY && app->tree && app->tree->root_index >= 0 )
    {
        UITree_FrameSetBinder(app->tree, app_plugin_frame_bind, app);
        UITree_FrameBind(app->tree);
    }
    bool const widgets_ready =
        app->app_state == APP_STATE_READY && app->tree && app->tree->root_index >= 0;
    PluginHost_WidgetsChanged(
        app->plugins,
        widgets_ready ? app->tree->instance_id : 0,
        widgets_ready ? app->tree->generation : 0);
    frame_candidate = PluginHost_FrameNeedsLayout(app->plugins) ? 1 : 0;
    if( !app->plugin_frame_active )
    {
        /* A plugin frame that ended while the tree was up: give the chrome back once,
         * then stop paying for the check. UITree_FrameRelease is idempotent,
         * and `plugin_layout_dirty` is what makes this happen exactly once. */
        if( app->plugin_layout_dirty && app->tree )
        {
            UITree_FrameRelease(app->tree);
            UITree_EnsureLayout(app->tree);
            app->plugin_layout_dirty = 0;
        }
        /* A candidate build is independent of committed ownership. Native is
         * deliberately still live here; fall through to the safe layout fence
         * only for the one attempt the host requested. */
        frame_candidate = PluginHost_FrameNeedsLayout(app->plugins) ? 1 : 0;
        if( frame_candidate )
            goto candidate_layout;
        /* app_plugin_frame_activate already restored the lane's default on the
         * selection transition. Do not restate it on every native-frame tick:
         * ordinary CS2/user window-mode changes own this state again now. */
        return;
    }
candidate_layout:
    if( !app->tree || app->tree->root_index < 0 )
        return;
    /*
     * Nothing is declared against a frame that is still being built.
     *
     * The gameframe bakes across many frames -- the root interface, its packs,
     * the scripts that rearrange them -- and a declaration made partway
     * through finds none of the roles and none of the chrome, then stands for
     * ever because it believes it succeeded. What that looks like is a client
     * drawing the plugin's stones UNDER its own untouched gameframe, which is
     * the one outcome worse than either frame alone.
     *
     * Marked dirty rather than merely skipped, so the first READY frame
     * declares even if nothing else changed.
     */
    if( app->app_state != APP_STATE_READY )
    {
        app->plugin_layout_dirty = 1;
        return;
    }

    /*
     * And nothing is declared against a tree that is not a gameframe at all.
     *
     * A committed plugin frame belongs to the game screen, not the title tree.
     * Its provider may remain running across logout, but the frame must not be
     * taken (UITree_FrameProvide) while the title tree bakes.
     *
     * A provider correctly declines to build outside the game, but taking the
     * frame here would still be destructive: the chrome collection hides every
     * root-group decoration it finds, and against the login screen that reads
     * as the login screen falling apart -- the plate and most of the background
     * gone, two strips of brazier left standing.
     *
     * Marked dirty rather than merely skipped, so the provider is asked again
     * on the first READY frame of the next session instead of inheriting
     * whatever the last one left behind.
     */
    if( app->screen != APP_SCREEN_GAME )
    {
        app->plugin_layout_dirty = 1;
        return;
    }

    /* Restate the selected offer's canvas policy. It has other
     * writers, and the last one to speak wins. */
    if( app->plugin_frame_active )
        App_SyncPluginLayoutCanvas(app);

    /*
     * Re-declare only when the last answer stopped being true.
     *
     * The cases are the canvas resizing, the gameframe being rebuilt (which
     * bumps the tree generation and drops the whole table with it), the boot
     * finishing, and the committed selection itself. Re-declaring every frame would work and
     * would cost a whole-tree walk per frame to collect the chrome again -- on
     * a rev-239 toplevel that is thousands of nodes for an answer that has not
     * changed since the window was last dragged.
     */
    /*
     * A generation move is only a reason when it moved a ROLE. The fence's
     * reassert already re-collects the chrome on every generation change;
     * what a fresh frame build adds is the provider re-placing its surfaces and the
     * answers it reads back ("does this frame have tab 7"), which change only
     * when a role's node did. On an OldSchool lane a cache timer recreates
     * its overlay nodes every logic tick, so without this gate the whole
     * layout was re-declared at frame rate. @see UITree_FrameSlotsStale.
     */
    if( !app->plugin_layout_dirty && UITree_FrameActive(app->tree) &&
        app->plugin_layout_generation != app->tree->generation &&
        app->plugin_layout_w == UITREE_LAYOUT_ROOT_W &&
        app->plugin_layout_h == UITREE_LAYOUT_ROOT_H && !UITree_FrameSlotsStale(app->tree) )
        app->plugin_layout_generation = app->tree->generation;

    if( frame_candidate || app->plugin_layout_dirty || !UITree_FrameActive(app->tree) ||
        app->plugin_layout_generation != app->tree->generation ||
        app->plugin_layout_w != UITREE_LAYOUT_ROOT_W ||
        app->plugin_layout_h != UITREE_LAYOUT_ROOT_H )
    {
        PluginHost_Layout(app->plugins, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        /* A provider may change selection from inside on_gameframe. The host
         * correctly abandons that transaction (its selection epoch moved),
         * which leaves dirty set and the effective frame released. Give the
         * replacement selection one bounded, sequential attempt now so native
         * chrome cannot leak into interaction between this tick and the final
         * publication tick. Resolve first because the replacement handler may
         * read widgets while laying out. A provider that keeps changing
         * selection cannot spin us: it gets at most this one retry. */
        if( app->plugin_frame_active && app->plugin_layout_dirty )
        {
            UITree_EnsureLayout(app->tree);
            PluginHost_Layout(app->plugins, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        }
        /* FrameProvide installs the chrome suppression and role binding and
         * invalidates the resolved boxes. Publish that before interaction
         * consumes them; a provider may also change frame selection from
         * inside its callback, and frame_activate drops the old effective
         * frame immediately. */
        UITree_EnsureLayout(app->tree);
    }
    /* UITree_EmitWalk keeps a final generation fence as well. It no longer
     * rewrites CS2 geometry: it only rebinds the standing semantic declaration
     * if topology somehow changed after this settled pass. */
}

/**
 * Does the client have somewhere for typed characters to go right now?
 *
 * Three sources, and they are the three places this client accepts text: the
 * login form's two fields, the chat line, and a plugin that asked for input.
 *
 * This exists for the soft keyboard. On a desktop the answer is not needed --
 * a physical keyboard is always there, and SDL's text-input mode only governs
 * whether TEXTINPUT events arrive, which the shell turns on once at boot and
 * never turns off. On a touch device it is the whole question: there is no
 * keyboard unless one is raised, and raising it at the wrong time covers half
 * the screen with something the user cannot type into.
 */
static int
app_wants_text_input(struct App const* app)
{
    assert(app);

    /* The login form, and only while it is the screen being shown: `focus`
     * keeps its last value across a screen change, so testing it alone would
     * raise the keyboard over the main menu. */
    if( (app->screen == APP_SCREEN_TITLE || app->screen == APP_SCREEN_CONNECTING) &&
        app->title.screen == RS_TITLE_LOGIN_FORM && app->title.focus >= 0 &&
        app->title.focus < RS_TITLE_FIELD_COUNT )
        return 1;

    if( app->chat_input_active )
        return 1;

    /* The mobile scripts' own request. @see App::vm_keyboard_open. */
    if( app->vm_keyboard_open )
        return 1;

    /* A plugin asked for it (torirs_plugin_bridge.u.c). Kept last so the
     * client's own fields win when both are true. */
    return app->text_input_on ? 1 : 0;
}

int
App_TakeTextInputChange(
    struct App* app,
    int* out_on)
{
    int wanted;

    assert(app);

    /*
     * Poll rather than wait for a writer, because the two client-side sources
     * are plain state that many code paths change -- clicking a field, pressing
     * Escape, submitting the form, a script closing the chat. Making each of
     * those remember to set a dirty flag would be a rule to keep, and the one
     * that forgot would leave the keyboard up over a screen with no field.
     */
    wanted = app_wants_text_input(app);
    if( wanted != app->text_input_effective )
    {
        app->text_input_effective = wanted;
        app->text_input_dirty = 1;
    }

    if( !app->text_input_dirty )
        return 0;
    app->text_input_dirty = 0;
    if( out_on )
        *out_on = app->text_input_effective;
    return 1;
}

int
App_TakeWindowModeChange(
    struct App* app,
    int* out_mode)
{
    assert(app);
    if( !app->host.window_mode_dirty )
        return 0;
    app->host.window_mode_dirty = false;
    if( out_mode )
        *out_mode = app->host.window_mode;
    return 1;
}

/**
 * Drain a Display-panel layout choice (0/1/2) raised by settings_client_mode.
 * Same split as App_TakeWindowModeChange: the App owns the flag; the shell
 * sends WINDOW_STATUS.
 */
int
App_TakeClientLayoutChange(
    struct App* app,
    int* out_mode)
{
    assert(app);
    if( !app->host.client_layout_dirty )
        return 0;
    app->host.client_layout_dirty = false;
    if( out_mode )
        *out_mode = app->host.client_layout_mode;
    return 1;
}

#include "app_frame.u.c"

int
App_FrameCapFps(struct App const* app)
{
    struct RevConfigFrameItem const* frame;

    assert(app);
    frame = &app->revconfig_profile.frame;
    if( frame->cap_source == REVCONFIG_FRAME_CAP_CS2 )
    {
        /* The cache's Limit Framerate row, when the player has picked one;
         * the profile's own number until then. */
        int const chosen = RS_CS2Host_FrameRateCapFps(&app->host);
        if( chosen > 0 )
            return chosen;
    }
    return frame->cap_fps > 0 ? frame->cap_fps : 0;
}

int
App_FrameSettled(struct App const* app)
{
    assert(app);
    return !app->runner_had_work && !app->runner.frame_settle_pending &&
           !app->exec_runner_had_work && !app->server_tick_open &&
           app->pending_clientscript_count == 0 && app->host.triggeroplocal_count == 0 &&
           !app->host.close_modal_requested && app->host.resume_pausebutton_component_id == -1 &&
           app->host.social_send_count == 0;
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

static void
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

static void
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

static void
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

/* Request a sequence load once (deduped through the entity seq tracker).
 *
 * The tracker is what stops this from re-queueing a load every world tick for
 * the whole of a sequence's load window: `ToriDraw_SceneAnimationHas` above only
 * goes true once the load LANDS, so between the request and the registration
 * that test says "missing" every tick.
 *
 * It is a fixed 64-entry table and it used to only *record* under the capacity
 * check while queueing unconditionally — so once 64 distinct entity sequences
 * had been seen in a session (an afternoon of walking and fighting passes that
 * easily; it is never pruned), the dedupe silently stopped working and every
 * tick queued another Dat2SequenceLoad for the same seq. Each one that landed
 * called ToriDraw_SceneAnimationAdd, which used to free the animation live
 * elements were already pointing at. Overwrite the oldest entry instead: the
 * table stays bounded, dedupe keeps working, and an evicted-but-still-loading
 * seq costs at worst one redundant task, which the registry now absorbs safely. */
static void
app_request_entity_seq(
    struct App* app,
    int seq_id)
{
    int const capacity =
        (int)(sizeof(app->entity_seq_loads.seq_ids) / sizeof(app->entity_seq_loads.seq_ids[0]));
    struct ToriRS_Task* task;

    if( seq_id < 0 || ToriDraw_SceneAnimationHas(app->scene, seq_id) )
        return;
    for( int i = 0; i < app->entity_seq_loads.count; i++ )
        if( app->entity_seq_loads.seq_ids[i] == seq_id )
            return;
    if( app->entity_seq_loads.count < capacity )
    {
        app->entity_seq_loads.seq_ids[app->entity_seq_loads.count++] = seq_id;
    }
    else
    {
        memmove(
            &app->entity_seq_loads.seq_ids[0],
            &app->entity_seq_loads.seq_ids[1],
            (size_t)(capacity - 1) * sizeof(app->entity_seq_loads.seq_ids[0]));
        app->entity_seq_loads.seq_ids[capacity - 1] = seq_id;
    }
    task = CreateTask_SequenceLoad(app->provider, app->scene, seq_id);
    if( task )
        ToriRS_TaskQueue_Add(app->runner.queue, task);
}

/* Bind one entity's World animation state onto its scene element (reference
 * getTempModel2 selection: primary when playing and undelayed — with the
 * secondary bound alongside for the walkmerge blend when it is a real walk —
 * else the secondary alone). */
static void
app_world_apply_entity_anim_tracks(
    struct App* app,
    int element_id,
    struct WorldEntityFacet_Animation const* anim,
    struct WorldEntityFacet_IdleAnimations const* idle)
{
    struct ToriDraw_SceneElement* el;
    int primary_active = anim->primary.anim_id != (uint16_t)-1 && anim->primary.anim_id != 0;
    int secondary_active = anim->secondary.anim_id != (uint16_t)-1 && anim->secondary.anim_id != 0;

    if( element_id < 0 || !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return;
    el = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !el )
        return;

    /* Frame sounds are NOT emitted from here. Entities are world-sim driven
     * (anim_external), and this runs once per rendered frame on whichever
     * single track ends up bound — so a sound was heard only when a render
     * happened to land on the frame carrying it, and the readyanim's sounds
     * went missing entirely for as long as an action animation covered it.
     * World_StepEntityAnimation announces every frame it crosses on both
     * tracks instead (app_world_anim_frame_sound is the listener), which is
     * where the reference emits them too. */

    if( primary_active )
        app_request_entity_seq(app, anim->primary.anim_id);
    if( secondary_active )
        app_request_entity_seq(app, anim->secondary.anim_id);

    if( primary_active && anim->primary.delay == 0 )
    {
        struct ToriDraw_Animation* pa =
            ToriDraw_SceneAnimationGet(app->scene, anim->primary.anim_id);
        /* Not registered yet: `app_request_entity_seq` above only queues the
         * load, so the first ticks after a spawn legitimately have no
         * animation. That is the caller's condition — the predicate asserts. */
        if( pa && ToriDraw_ElementAnimPlayable(pa) )
        {
            ToriDraw_ElementSetAnim(el, pa);
            el->anim_seq_id = anim->primary.anim_id;
            el->anim_frame = anim->primary.frame < pa->frame_count ? anim->primary.frame : 0;
            /* The walkmerge blend is a frame-animator operation (it masks
             * transform groups), so a skeletal primary never takes a secondary. */
            if( !pa->skeletal && secondary_active && idle &&
                anim->secondary.anim_id != (uint16_t)idle->readyanim )
            {
                struct ToriDraw_Animation* sa =
                    ToriDraw_SceneAnimationGet(app->scene, anim->secondary.anim_id);
                if( sa && sa->frame_count > 0 && sa->frames )
                {
                    el->secondary_animation = sa;
                    el->anim2_seq_id = anim->secondary.anim_id;
                    el->anim2_frame =
                        anim->secondary.frame < sa->frame_count ? anim->secondary.frame : 0;
                    return;
                }
            }
            el->secondary_animation = NULL;
            el->anim2_seq_id = -1;
            return;
        }
    }

    el->secondary_animation = NULL;
    el->anim2_seq_id = -1;
    if( secondary_active )
    {
        struct ToriDraw_Animation* sa =
            ToriDraw_SceneAnimationGet(app->scene, anim->secondary.anim_id);
        if( sa && ToriDraw_ElementAnimPlayable(sa) )
        {
            ToriDraw_ElementSetAnim(el, sa);
            el->anim_seq_id = anim->secondary.anim_id;
            el->anim_frame = anim->secondary.frame < sa->frame_count ? anim->secondary.frame : 0;
            return;
        }
    }

    ToriDraw_ElementSetAnim(el, NULL);
    el->anim_seq_id = -1;
    el->anim_frame = 0;
}

/* Held-item hiding/replacement (players only — NPCs render npctype models with
 * no worn slots). Reference ClientPlayer.getSequencedModel: while the PRIMARY
 * seq is actually driving frames (delay 0), its replaceheldleft/right override
 * the left-hand (appearance slot 5) / right-hand (slot 3) worn item before the
 * model is composited. The override is an appearance slot: a value >= 0 that is
 * not in the obj range draws no model there, i.e. the held item is hidden (e.g.
 * many emotes drop the weapon and shield); an obj-range value swaps in a
 * different obj (see pkt_player_appearance.h). The appearance model
 * is built once and cached on the scene element, so rebuild it only when the
 * effective override changes (anim start/stop), keyed by held_*_applied. */
static void
app_set_player_element_model(
    struct App* app,
    int element_id,
    int const slots[12],
    int const colors[5],
    int gender);

static void
app_world_apply_player_held_items(
    struct App* app,
    struct WorldEntity_Player* player)
{
    struct WorldEntityFacet_Animation const* anim = &player->animation;
    int want_left = -1;
    int want_right = -1;

    if( anim->primary.anim_id != (uint16_t)-1 && anim->primary.anim_id != 0 &&
        anim->primary.delay == 0 )
    {
        struct ToriDraw_Animation* prim =
            ToriDraw_SceneAnimationGet(app->scene, anim->primary.anim_id);
        if( prim && prim->frame_count > 0 )
        {
            /* Cache-sourced appearance slots — converted here so the override
             * is in the same vocabulary as the appearance it overwrites. */
            if( prim->replaceheldleft >= 0 )
                want_left = Appearance_FromCacheValue(prim->replaceheldleft);
            if( prim->replaceheldright >= 0 )
                want_right = Appearance_FromCacheValue(prim->replaceheldright);
        }
    }

    if( want_left == player->held_left_applied && want_right == player->held_right_applied )
        return;

    player->held_left_applied = want_left;
    player->held_right_applied = want_right;

    {
        int slots[12];
        memcpy(slots, player->appearance.slots, sizeof(slots));
        if( want_right >= 0 )
            slots[3] = want_right;
        if( want_left >= 0 )
            slots[5] = want_left;
        app_set_player_element_model(
            app, player->element_id, slots, player->appearance.colors, player->gender);
    }
}

/* Push World animation state to the entity scene elements each frame (the
 * per-element modulo tick skips anim_external elements). */
static void
app_world_sync_entity_animations(struct App* app)
{
    struct World_EntityPool* pool;

    pool = &app->world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        if( player )
        {
            app_world_apply_entity_anim_tracks(
                app, player->element_id, &player->animation, &player->idle_animations);
            app_world_apply_player_held_items(app, player);
        }
    }

    pool = &app->world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        if( npc )
            app_world_apply_entity_anim_tracks(
                app, npc->element_id, &npc->animation, &npc->idle_animations);
    }
}

/* ---- Entity attached-graphic (SPOTANIM mask): reference-accurate per-frame
 * Model.combine (ClientNpc/ClientPlayer.getTempModel). While an entity's
 * graphic is active its scene element's model is merge(body, spot): the body
 * part keeps its bones so the element's bound seq keeps animating it live in
 * the renderer, the spot part is pre-posed to the world-stepped spot frame
 * with its bones cleared (reference temp.labelFaces/labelVertices = null, so
 * the body seq cannot drive spot vertices) and raised by the wire height.
 * Assets load once through the async task pipeline; with both models resident
 * the combine itself is synchronous. Re-merged only when the spot frame
 * changes — between merges the visual is identical because the renderer poses
 * the body part live and the spot pose only advances with spot->frame. State
 * lives in app->entity_spotanims keyed by the body element id. ---- */

/* Find the entry for (element, owner). `owner_entity_id` 0 means "any owner",
 * used only when scanning for a free slot.
 *
 * An entry matching the element but NOT the owner is a recycled element id: the
 * previous owner despawned and this id was handed to somebody else. Drop it
 * WITHOUT restoring -- restoring would move the dead entity's body model onto
 * the new occupant. See the note on AppEntitySpotanim::owner_entity_id. */
static struct AppEntitySpotanim*
app_entity_spotanim_find(
    struct App* app,
    int body_element_id,
    int owner_entity_id)
{
    int count = (int)(sizeof(app->entity_spotanims) / sizeof(app->entity_spotanims[0]));
    for( int i = 0; i < count; i++ )
    {
        struct AppEntitySpotanim* entry = &app->entity_spotanims[i];
        if( entry->body_element_id != body_element_id )
            continue;
        if( owner_entity_id != 0 && entry->body_element_id >= 0 &&
            entry->owner_entity_id != owner_entity_id )
        {
            app_entity_spotanim_detach(app, entry, false);
            return NULL;
        }
        return entry;
    }
    return NULL;
}

/* End the combine: restore the entity's own model (ownership of the pristine
 * body snapshot moves back to the element) and free the spot base. `restore`
 * is false when the body element is already gone (despawn/scene teardown). */
static void
app_entity_spotanim_detach(
    struct App* app,
    struct AppEntitySpotanim* entry,
    bool restore)
{
    if( restore && entry->body && ToriDraw_SceneElementIsLive(app->scene, entry->body_element_id) )
    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = entry->body;
        /* The renderer's per-frame AnimateReset needs captured originals. */
        ToriDraw_ModelCaptureOriginalVertices(entry->body);
        ToriDraw_SceneElementSetModel(app->scene, entry->body_element_id, hnd);
        entry->body = NULL; /* ownership moved to the element */
        app->need_redraw = 1;
    }
    if( entry->body )
        ToriDraw_ModelFree(entry->body);
    if( entry->spot )
        ToriDraw_ModelFree(entry->spot);
    *entry = (struct AppEntitySpotanim){ .body_element_id = -1, .owner_entity_id = 0 };
}

/* EntityRemoved drain hook: the entity element (and with it the combined
 * model) is going away — free the snapshots without touching the element. */
static void
app_entity_spotanim_drop(
    struct App* app,
    int body_element_id)
{
    struct AppEntitySpotanim* entry = app_entity_spotanim_find(app, body_element_id, 0);
    if( entry )
        app_entity_spotanim_detach(app, entry, false);
}

static void
app_world_sync_one_entity_spotanim(
    struct App* app,
    struct WorldEntityFacet_EntitySpotanim const* spot,
    int element_id,
    int owner_entity_id)
{
    struct World* world = app->world;
    struct AppEntitySpotanim* entry = app_entity_spotanim_find(app, element_id, owner_entity_id);
    struct ToriRS_Spotanimtype* type;
    struct ToriDraw_Animation* anim;
    struct ToriDraw_SceneElement* el;
    int active = spot->id != -1 && world->cycle >= spot->last_cycle && spot->frame >= 0;
    int frame;
    int first_combine;

    if( !active )
    {
        if( entry )
            app_entity_spotanim_detach(app, entry, true);
        return;
    }
    /* Graphic replaced mid-flight: restore the body, rebuild for the new id. */
    if( entry && entry->spotanim_id != spot->id )
    {
        app_entity_spotanim_detach(app, entry, true);
        entry = NULL;
    }
    if( !entry )
    {
        entry = app_entity_spotanim_find(app, -1, 0);
        if( !entry )
            return; /* table full */
        *entry = (struct AppEntitySpotanim){
            .body_element_id = element_id,
            .owner_entity_id = owner_entity_id,
            .spotanim_id = spot->id,
            .applied_frame = -1,
        };
    }

    /* Asset gate: spotanimtype + model + seq must be resident. Kick the async
     * load chain once; the frame it lands the combine below runs synchronously
     * (reference precondition: SpotType.getTempModel2 assumes loaded). */
    type = CacheProvider_SpotanimtypeGet(app->provider, spot->id);
    anim = (type && type->seq >= 0) ? ToriDraw_SceneAnimationGet(app->scene, type->seq) : NULL;
    if( !type || !CacheProvider_ModelGet(app->provider, type->model) || !anim ||
        anim->frame_count <= 0 || !anim->frames || !anim->base )
    {
        if( !entry->load_enqueued )
        {
            struct Task_AppSpawn* task =
                app_spawn_task_new(app, APP_SPAWN_ENTITY_SPOTANIM, 0, 0, 0);
            task->spotanim_id = spot->id;
            task->entity_element_id = element_id;
            ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
            entry->load_enqueued = 1;
        }
        return;
    }

    if( !ToriDraw_SceneElementIsLive(app->scene, element_id) )
        return;
    el = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !el || !ToriDraw_ModelKindIsFull(el->model.kind) || !el->model.u.model.model )
        return;

    /* Snapshot the pristine body. Also re-snapshot when the element's model
     * changed under us — a held-item/appearance rebuild SetModel'd a fresh
     * body over our combined (`combined` is compared as an identity only,
     * never dereferenced: SetModel freed it). */
    if( !entry->body || el->model.u.model.model != entry->combined )
    {
        if( entry->body )
            ToriDraw_ModelFree(entry->body);
        /* The renderer poses the element model in place each draw; reset to
         * the rest pose so the snapshot is the true base. The element's model
         * no longer holds the pose the renderer last applied, and it must not
         * skip re-applying it. */
        ToriDraw_ModelAnimateReset(el->model.u.model.model);
        ToriDraw_SceneElementPoseInvalidate(app->scene, element_id);
        entry->body = ToriDraw_ModelCopy(el->model.u.model.model);
        entry->combined = NULL;
        entry->applied_frame = -1;
        if( !entry->body )
            return;
    }

    if( !entry->spot )
    {
        entry->spot = app_world_build_spotanim_model(app, type);
        if( !entry->spot )
            return;
    }

    frame = spot->frame < anim->frame_count ? spot->frame : anim->frame_count - 1;
    if( frame == entry->applied_frame && entry->combined )
        return; /* merged model already at this spot frame; body animates live */
    first_combine = entry->applied_frame < 0;

    {
        struct ToriDraw_Model* posed = ToriDraw_ModelCopy(entry->spot);
        struct ToriDraw_Model* parts[2];
        struct ToriDraw_Model* merged;
        if( !posed )
            return;
        /* Hole frames (missing archive) hold the rest pose, like the renderer. */
        if( anim->frames[frame].length > 0 )
            ToriDraw_ModelAnimateFrame(posed, anim->base, &anim->frames[frame]);
        /* Reference nulls the spot copy's labels before Model.combine. */
        ToriDraw_BonesFree(posed->vertex_bones);
        posed->vertex_bones = NULL;
        ToriDraw_BonesFree(posed->face_bones);
        posed->face_bones = NULL;
        /* Model y is negative-up: reference temp.translate(-spotanimHeight,0,0). */
        if( spot->height != 0 )
            ToriDraw_ModelTranslate(posed, 0, -spot->height, 0);

        parts[0] = entry->body;
        parts[1] = posed;
        merged = ToriDraw_ModelMerge(parts, 2);
        ToriDraw_ModelFree(posed);
        if( !merged )
            return;
        ToriDraw_ModelCaptureOriginalVertices(merged);
        {
            struct ToriDraw_ModelHandle hnd;
            memset(&hnd, 0, sizeof(hnd));
            hnd.kind = TORIDRAWMK_MODEL;
            hnd.u.model.model = merged;
            ToriDraw_SceneElementSetModel(app->scene, element_id, hnd);
        }
        entry->combined = merged;
        entry->applied_frame = frame;
        app->need_redraw = 1;
        if( first_combine && getenv("TORIRS_ANIM_DEBUG") )
            TORIRS_LOG(
                "entity_spotanim: combine id=%d element=%d seq=%d frame=%d height=%d\n",
                spot->id,
                element_id,
                type->seq,
                frame,
                spot->height);
    }
}

static void
app_world_sync_entity_spotanims(struct App* app)
{
    struct World_EntityPool* pool;
    int count = (int)(sizeof(app->entity_spotanims) / sizeof(app->entity_spotanims[0]));

    if( !app->world )
        return;

    /* Entries whose body element died outside the event drain (scene
     * teardown): free the snapshots. */
    for( int i = 0; i < count; i++ )
    {
        struct AppEntitySpotanim* entry = &app->entity_spotanims[i];
        if( entry->body_element_id >= 0 &&
            !ToriDraw_SceneElementIsLive(app->scene, entry->body_element_id) )
            app_entity_spotanim_detach(app, entry, false);
    }

    pool = &app->world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        if( player && player->element_id >= 0 )
            app_world_sync_one_entity_spotanim(
                app,
                &player->spotanim,
                player->element_id,
                WORLD_ENTITY_ID(WORLD_ENTITY_KIND_PLAYER, player->server_pid));
    }

    pool = &app->world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        if( npc && npc->multinpc_hidden && npc->element_id >= 0 )
        {
            struct AppEntitySpotanim* entry = app_entity_spotanim_find(app, npc->element_id, 0);
            if( entry )
                app_entity_spotanim_detach(app, entry, false);
        }
        else if( npc && npc->element_id >= 0 )
            app_world_sync_one_entity_spotanim(
                app,
                &npc->spotanim,
                npc->element_id,
                WORLD_ENTITY_ID(WORLD_ENTITY_KIND_NPC, npc->server_slot));
    }
}

int
App_WorldSpawnSyncedPlayer(
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    return app_world_spawn_player_now(app, scene_x, scene_z, level);
}

int
App_WorldSpawnSyncedNpc(
    struct App* app,
    int npc_id,
    int base_npc_id,
    int scene_x,
    int scene_z,
    int level)
{
    return app_world_spawn_npc_now(app, npc_id, base_npc_id, scene_x, scene_z, level);
}

/* See the declaration in app.h. This is the resident-config half of the
 * reference NPCType.method461/getMultiNPC walk. Packet application uses the
 * async CreateTask_NpcMultiLoad wrapper above so the first lookup of a cold
 * shell cannot fail before the config has had a chance to load. */
int
App_NpctypeResolveMultiId(
    struct App* app,
    int npc_id)
{
    assert(app);

    for( int guard = 0; guard < TORIRS_NPC_MULTI_MAX_DEPTH && npc_id >= 0; guard++ )
    {
        struct ToriRS_Npctype* npctype = CacheProvider_NpctypeGet(app->provider, npc_id);
        int resolved;

        if( !npctype || npctype->transform_count <= 0 )
            return npc_id;

        resolved = VarPManager_ResolveTransform(
            &app->varps,
            npctype->transforms,
            npctype->transform_count,
            npctype->transform_varbit,
            npctype->transform_varp);
        if( resolved < 0 )
            return -1;
        if( resolved == npc_id )
            return npc_id;
        npc_id = resolved;
    }
    return npc_id;
}

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
    if( app->minimap_flag_x >= 0 )
    {
        app->minimap_flag_x -= base_dx;
        app->minimap_flag_z -= base_dz;
        if( app->minimap_flag_x < 0 || app->minimap_flag_z < 0 ||
            app->minimap_flag_x >= world->_scene_size || app->minimap_flag_z >= world->_scene_size )
        {
            app->minimap_flag_x = -1;
            app->minimap_flag_z = -1;
        }
    }

    /* Camera position + orbit focus (deob field3239/field161/field1545/field73
     * -= dx<<7). Fine coords move with the scene base. */
    if( base_dx != 0 || base_dz != 0 )
    {
        app->world_camera_pos.x -= base_dx * 128;
        app->world_camera_pos.z -= base_dz * 128;
        app->orbit_x -= base_dx * 128;
        app->orbit_z -= base_dz * 128;
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

/*
 * LOC_ANIM: attach a sequence to the scenery element on a tile.
 *
 * ON THE SERIAL EXEC FIFO, BEHIND THE SAME PACKET'S LOC_ADD_CHANGE, and that is
 * the whole reason this is a task rather than a call.
 *
 * The reference applies a zone loc change to the scene the moment it reads it -
 * it has the whole cache in hand and a loctype's models are built on demand at
 * draw time - so a LOC_ADD_CHANGE and a LOC_ANIM in one enclosed update work in
 * the order they were written. This client cannot: a change has to wait for its
 * loc config and every model it names to become resident (the reference's own
 * `changeLocAvailable` gate), so `App_WorldLocChange` enqueues an async task.
 *
 * Applying the animation synchronously beside that made the two ops race, and
 * the loser was always the animation: `World_SceneryFindAt` ran before the
 * pending change had built the scenery, found the OLD loc or nothing at all,
 * and the sequence was dropped with no error to see. The loc then stood on
 * whatever frame its `anim=` config left it on - which is what "the loc is
 * stuck in a frame" is, every time.
 *
 * Content should not have to know any of this. The Theatre's acid pools and
 * exhumeds, the Inferno's collapsing flanks and every door script are all
 * entitled to add a loc and animate it in the same tick, exactly as the
 * reference lets them.
 *
 * The task awaits nothing itself. `ToriRS_TaskQueue_Run` runs the head task
 * until it yields and leaves the rest queued in order, so being enqueued after
 * the change IS the fix; with no change pending it lands in the same pass.
 */
void
App_WorldSceneryAnim(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_shape,
    int seq_id)
{
    struct Task_AppSpawn* task;

    assert(app);
    task = app_spawn_task_new(app, APP_SPAWN_LOC_ANIM, scene_x, scene_z, level);
    task->loc_shape = loc_shape;
    task->seq_id = seq_id;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

static void
app_world_scenery_anim_apply(
    struct App* app,
    struct World* world,
    int scene_x,
    int scene_z,
    int level,
    int loc_shape,
    int seq_id)
{
    int idx;
    assert(app);
    assert(world);
    idx = World_SceneryFindAt(world, scene_x, scene_z, level, loc_shape);
    if( idx >= 0 )
    {
        struct WorldEntity_Scenery* scenery = World_EntityPoolGet(&world->entities.scenery, idx);
        if( scenery && scenery->element_id >= 0 )
        {
            struct ToriDraw_SceneElement* element;

            /* Everything below writes to this element's model -- the quarter
             * turn is un-baked in place, and app_world_apply_seq captures a
             * bind pose and then poses it every frame. A static loc's model is
             * shared with its every other placement, so it has to become this
             * element's own before any of that. */
            ToriDraw_SceneElementModelForWrite(app->scene, scenery->element_id);
            element = ToriDraw_SceneElementGet(app->scene, scenery->element_id);

            /* A loc with no config animation is built as static scenery: its
             * map orientation is baked directly into its vertices. LOC_ANIM
             * turns that same loc into the reference client's DynamicObject,
             * whose order is the opposite — animate the unrotated model, then
             * apply the loc orientation while drawing it.
             *
             * Without this conversion, translation/rotation ops from a
             * packet-attached sequence run in world axes. The Inferno's
             * angle-3 falling walls are the visible failure: their inner
             * sections move behind the flanks even though 7559 and 7560 start
             * together. Undo the baked quarter-turn once and carry it as the
             * element yaw from then on. Shape 11 already owns the extra
             * diagonal half-turn in its draw yaw, so preserve that base.
             *
             * Config-animated locs already arrive in this representation;
             * their yaw is the desired value and the guard leaves them alone.
             */
            if( element && ToriDraw_ModelKindIsFull(element->model.kind) &&
                element->model.u.model.model &&
                (loc_shape == RSCACHE_LOC_SHAPE_SCENERY ||
                 loc_shape == RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL) )
            {
                int const angle = scenery->angle & 3;
                int const base_yaw = loc_shape == RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL ? 256 : 0;
                int const wanted_yaw = base_yaw + angle * 512;

                if( angle != 0 && element->world_position.yaw == base_yaw )
                {
                    ToriDraw_ModelOrient(element->model.u.model.model, (4 - angle) & 3);
                    ToriDraw_SceneElementSetPosition(
                        app->scene,
                        scenery->element_id,
                        element->world_position.x,
                        element->world_position.y,
                        element->world_position.z,
                        wanted_yaw);
                }
            }
            app_world_apply_seq(app, scenery->element_id, seq_id);
            app->need_redraw = 1;
        }
    }
}

void
App_WorldApplyNpcType(
    struct App* app,
    int world_idx,
    int element_id,
    int npc_type,
    int base_npc_type)
{
    struct ToriRS_Npctype* npctype;
    struct AppNpcEntityFacts facts;
    struct ToriDraw_Model* model;

    assert(app);
    npctype = CacheProvider_NpctypeGet(app->provider, npc_type);
    if( !npctype )
        return;
    /* Same gap-fill as the spawn path: the rung draws the body and names the
     * ops, the shell supplies the size and idle animation it does not state.
     * See app_npc_entity_facts. */
    app_npc_entity_facts(app, base_npc_type, npctype, &facts);
    if( getenv("TORIRS_ANIM_DEBUG") )
        TORIRS_LOG(
            "npc_retype: world_idx=%d element=%d type=%d\n", world_idx, element_id, npc_type);

    /* Retyping TO a model-less type must actually hide the npc. Building
     * nothing here would leave the old model mounted and the entity would keep
     * rendering as its previous form; an empty model is the retype's honest
     * result, and matches the spawn path's handling of the same content. */
    if( npctype->models_count <= 0 )
    {
        model = ToriDraw_ModelNew(0, 0, 0);
        if( model )
            ToriDraw_ModelSetBoundsCylinder(model);
    }
    else
    {
        model = app_world_build_npc_model(app, npc_type, npctype);
        if( !model )
            /* Unlike the models_count<=0 branch above, this is not an honest
             * "no body" result -- it's app_world_build_model failing to
             * resolve a type that DOES have models (see the matching log in
             * app_world_spawn_npc_now). Neither branch below then touches the
             * element, so it silently keeps whatever model it already had
             * (typically the 14-bit placeholder type's, for a large npc like
             * QBD that needed a same-packet TRANSFORMATION to reach its real
             * id) -- which reads in-game as "the npc never rendered" with no
             * trace of why. Log it so that's diagnosable. */
            TORIRS_ERR("npc_retype: npc %d models failed to load\n", npc_type);
    }
    /* The depth-test opt-in is a property of the npc TYPE, so a retype has to
     * re-decide it against the new type -- exactly as the spawn path does. */
    if( model )
        app_model_apply_import_render_flags(model, app_npc_wants_zbuffer(npc_type, npctype));

    if( model && element_id >= 0 && ToriDraw_SceneElementIsLive(app->scene, element_id) )
    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        ToriDraw_SceneElementSetModel(app->scene, element_id, hnd);
        {
            struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
            if( el )
                el->anim_external = true;
            ToriDraw_SceneAnimListInvalidate(app->scene);
        }
    }
    else if( model )
    {
        ToriDraw_ModelFree(model);
    }

    {
        /* Reference CHANGETYPE swaps walkanim_l/r (Client.ts 8460-8462). */
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = facts.readyanim,
            .walkanim = facts.walkanim,
            /* Opcodes 15/114 rather than the -1 pair that used to sit here.
             * `World_EntityFace` takes turnanim over walkanim and
             * `World_UpdateMoverMovementAndAnimation` takes runanim over
             * walkanim at speed; both were already written and neither had
             * anything to read. The run set gets the same left/right swap the
             * walk set does -- it is the same reference line (Client.ts
             * 8460-8462), which swaps the pair for every movement set. */
            .turnanim = facts.turnanim,
            .runanim = facts.runanim,
            .walkanim_b = facts.walkanim_b,
            .walkanim_r = facts.walkanim_l,
            .walkanim_l = facts.walkanim_r,
            /* Read off the DRAWN type, and deliberately not gap-filled from the
             * shell the way size and readyanim are: it is a bare boolean whose
             * absent value and whose authored-false value are the same bit, so
             * "the rung did not state it" cannot be expressed. Same reason
             * `turn_speed` is left out of AppNpcEntityFacts. */
            .idle_anim_restart = npctype->idle_anim_restart ? 1 : 0,
        };
        World_NpcSetType(app->world, world_idx, npc_type, facts.size, &idle);
    }
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
        /*
         * A newly-added NPC reaches its ready pose through
         * app_world_spawn_npc_now, but a revision-239 CHANGE_TYPE keeps the
         * same scene element.  Rebind the replacement type's ready sequence
         * here as well, so the element does not spend a frame (and an
         * async-load gap) driven by the former type's idle.
         *
         * Only when there is no transient animation running, though.  That is
         * the other half of the rule World_NpcSetType now states: a one-shot
         * survives a transmog, and unconditionally stamping the new readyanim
         * on top of it would put the stomp back one layer up — which is
         * exactly how the Queen Black Dragon's return-to-sleep kept being
         * erased.  When a primary track is live, the next
         * app_world_apply_entity_anim_tracks binds it onto the new model, and
         * the readyanim takes over on its own when the sequence ends.
         *
         * This is intentionally general rather than familiar-specific.  The
         * regular entity sync will take over with the new idle/walk state on
         * the next world tick, just as it does after a normal spawn.
         */
        int const primary_live = npc && npc->animation.primary.anim_id != (uint16_t)-1 &&
                                 npc->animation.primary.anim_id != 0;

        if( model && !primary_live && element_id >= 0 &&
            ToriDraw_SceneElementIsLive(app->scene, element_id) )
            app_world_apply_seq(app, element_id, facts.readyanim);
        if( npc )
        {
            npc->combat_level = npctype->combat_level;
            npc->alwaysontop = npctype->alwaysontop;
            npc->minimap_visible = npctype->minimap_visible;
            npc->interactable = npctype->interactable;
            npc->facing.turn_speed = npctype->turn_speed;
            snprintf(npc->name, sizeof(npc->name), "%s", npctype->name);
            for( int i = 0; i < 5; i++ )
                snprintf(
                    npc->actions[i].name, sizeof(npc->actions[i].name), "%s", npctype->actions[i]);
            /* The drawn type changed; base_npc_id -- the multinpc shell --
             * did not. A plugin keyed on the shell, which is how anything
             * tagging an npc has to be keyed, keeps its tag across this. */
            if( app->plugins )
            {
                struct ToriRS_NpcSnapshot retyped;
                app_plugin_fill_npc(app, npc, &retyped);
                PluginHost_NpcRetype(app->plugins, &retyped);
            }
            if( torirs_env_net_debug() )
                TORIRS_ERR(
                    "entity_sync: npc type replacement=%d element=%d tile=%d,%d size=%d "
                    "model=%s\n",
                    npc_type,
                    element_id,
                    npc->grid_position.x,
                    npc->grid_position.z,
                    npc->size,
                    model ? "installed" : "missing");
        }
    }
    app_sync_textures(app);
    app->need_redraw = 1;
}

/* Build the player appearance model from slots/colors/gender and hand it to the
 * scene element (SceneElementSetModel disposes the previous model; the element's
 * animation binding survives, so the current seq keeps driving the new model).
 * Shared by the appearance packet and the per-frame held-item swap. */
static void
app_set_player_element_model(
    struct App* app,
    int element_id,
    int const slots[12],
    int const colors[5],
    int gender)
{
    struct ToriDraw_Model* model =
        PlayerModel_BuildFromAppearance(app->provider, slots, colors, gender);
    if( model && element_id >= 0 && ToriDraw_SceneElementIsLive(app->scene, element_id) )
    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        ToriDraw_SceneElementSetModel(app->scene, element_id, hnd);
    }
    else if( model )
    {
        ToriDraw_ModelFree(model);
    }
}

/*
 * Team-cape id carried by this appearance (reference
 * ClientPlayer.decodeAppearance: while reading the 12 worn slots it keeps the
 * ObjType.team of every equipped obj, so the LAST non-zero one wins).
 *
 * Resolved off resident objtypes only, which is safe here and nowhere else:
 * task_exec_entity_info awaits a config load for all 12 slots before calling
 * this, for the same reason the model build below can look models up directly.
 */
static int
app_appearance_team(
    struct App* app,
    struct PktPlayerAppearance const* appearance)
{
    int team = 0;

    for( int s = 0; s < APPEARANCE_SLOT_COUNT; s++ )
    {
        int obj_id = Appearance_SlotObj(appearance->slots[s]);
        struct ToriRS_Objtype const* obj;
        if( obj_id < 0 )
            continue;
        obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
        if( obj && obj->team != 0 )
            team = obj->team;
    }
    return team;
}

void
App_WorldApplyPlayerAppearance(
    struct App* app,
    int world_idx,
    int element_id,
    struct PktPlayerAppearance const* appearance)
{
    assert(app && appearance);

    app_set_player_element_model(
        app, element_id, appearance->slots, appearance->colors, appearance->gender);

    {
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = appearance->readyanim,
            .walkanim = appearance->walkanim,
            .turnanim = appearance->turnanim,
            .runanim = appearance->runanim,
            .walkanim_b = appearance->walkanim_b,
            .walkanim_r = appearance->walkanim_r,
            .walkanim_l = appearance->walkanim_l,
        };
        World_PlayerSetAppearance(
            app->world,
            world_idx,
            appearance->slots,
            appearance->identkit,
            appearance->colors,
            &idle,
            appearance->name,
            appearance->combat_level,
            appearance->gender);

        /* Overhead prayer/skull headicon bitmask (reference
         * ClientPlayer.headicons, appearance g1). SetAppearance carries no
         * headicon field, so copy it onto the entity directly for the overlay
         * pass. */
        {
            struct WorldEntity_Player* ent =
                World_EntityPoolGet(&app->world->entities.player, world_idx);
            if( ent )
            {
                ent->headicon = appearance->headicon;
                ent->team = app_appearance_team(app, appearance);
                /* The element now holds the un-overridden base model; the
                 * per-frame held-item pass will rebuild if a seq demands it. */
                ent->held_left_applied = -1;
                ent->held_right_applied = -1;
            }
        }

        /* Local player's real name now known: sync it to the chatbox so the
         * public-chat local echo shows the login name instead of the default
         * "Player". The reference echoes with this.localPlayer.name
         * (Client.ts:3405-3417); the server echoes the same name through
         * PLAYER_INFO CHAT, so the two must match. */
        if( appearance->name[0] )
        {
            int local_idx = -1;
            if( RS_EntitySync_FindPlayer(
                    &app->esync,
                    app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
                    &local_idx,
                    NULL) &&
                local_idx == world_idx )
            {
                strncpy(app->chat.username, appearance->name, sizeof(app->chat.username) - 1);
                /* Same string to the CS2 host, which answers CHAT_PLAYERNAME
                 * with it — clientscript 223 builds the chatbox input line
                 * from that op, so the two spellings have one source. */
                snprintf(
                    app->host.local_player_name,
                    sizeof(app->host.local_player_name),
                    "%s",
                    appearance->name);
            }
        }
    }
    app_sync_textures(app);
    app->need_redraw = 1;
}

void
App_RefreshAfterTreeMutation(struct App* app)
{
    assert(app);
    UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    app_request_cs1_eval(app);
    app->need_redraw = 1;
}

bool
App_IsBooting(
    struct App* app,
    int* out_progress)
{
    assert(app);
    if( out_progress )
        *out_progress = app->boot_progress;
    return app->app_state == APP_STATE_BOOTING;
}

bool
App_BuildFrame(
    struct App* app,
    struct ToriRS_Frame* frame,
    int width,
    int height)
{
    assert(app);
    assert(frame);

    if( app->app_state == APP_STATE_BOOTING )
        return false;

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_BUILD)
    {
        ToriRS_FrameInit(frame);
        ToriRS_FrameSetScene(frame, app->scene);
        ToriRS_FrameSetCanvas(frame, width, height);
        ToriRS_FrameSetEmitBuffer(frame, &app->emit);

        /* World pass: paint the visibility-ordered command list for the current
         * camera and attach it so UITREE_EMIT_WORLD opens the 3D pass. */
        if( app_world_drawable(app) )
        {
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PAINT)
            {
                app_world_paint(app);
            }
            ToriRS_FrameSetWorld(
                frame,
                app->world,
                app->painter_buffer,
                &app->world_camera,
                app->world_camera_pos.x,
                app->world_camera_pos.y,
                app->world_camera_pos.z);
            /* Must follow SetWorld: it resets slot 0 to the root identity. */
            app_wev_bind_frame_xforms(app, frame);
        }
    }
    return true;
}

void
App_PickFinish(
    struct App* app,
    struct ToriRS_PickHits const* hits)
{
    assert(app);
    assert(hits);
    app_world_pick_finish(app, hits);
}

/* Damage drawing is off unless asked for, so the A/B is a process restart and
 * every measurement below is against a binary that also contains the other
 * arm. Read once; off is one predicted branch. */
static int
app_damage_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_DAMAGE") ? 1 : 0;
    return armed;
}

/* Set for one frame by the damage report to dump the descs it unions. */
static int g_damage_trace;

/*
 * TORIRS_DAMAGE_RECTS=1: present the live rectangles separately instead of
 * their bounding box.
 *
 * Off, because it measured slower -- see App::damage. Kept switchable
 * rather than deleted so the arm that produced that number still exists.
 */
static int
app_damage_rects_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_DAMAGE_RECTS") ? 1 : 0;
    return armed;
}

/*
 * Decide what this frame is allowed to leave unpresented. @see
 * App::damage.
 *
 * A single box by default, not a region list: the three live areas of an
 * in-world frame (viewport, minimap, the overlays inside them) are adjacent,
 * and BitBlt bills per call as well as per pixel.
 *
 * A desc contributes its box UNION its clip rather than their intersection.
 * Which of the two bounds the pixels it writes differs by kind, and the union
 * is the bound that is correct without knowing which -- an intersection would
 * be tighter and occasionally wrong, and being wrong here means stale pixels
 * that never repair.
 */
static void
app_compute_damage(
    struct App* app,
    int width,
    int height)
{
    assert(app);

    ToriRS_DamageRegionReset(&app->damage);
    if( !app_damage_armed() || !app->ui_retained_frame )
        return;

    for( int i = 0; i < app->emit.count; i++ )
    {
        struct UITreeEmitDesc const* d = &app->emit.cmds[i];
        int live = (d->kind == UITREE_EMIT_WORLD || d->kind == UITREE_EMIT_MINIMAP);

        /* Same predicate as the emitter's volatile count, and deliberately a
         * pointer test: a desc whose bytes are stable while a host buffer
         * behind it is refilled is exactly the thing a retained frame cannot
         * assume it has already drawn. */
        if( !live &&
            (d->minimap_dots || d->entity_overlays || d->worldmap_tiles || d->debug_prims) )
            live = 1;
        if( !live )
            continue;

        if( g_damage_trace )
            TORIRS_REPORT(
                "[damage] live kind=%d box=%d,%d %dx%d clip=%d,%d %dx%d\n",
                (int)d->kind,
                d->x,
                d->y,
                d->w,
                d->h,
                d->clip.x,
                d->clip.y,
                d->clip.w,
                d->clip.h);

        /* Box INTERSECT clip. A desc draws inside its own box and is then
         * further restricted by the enclosing scissor, so the pixels it can
         * touch are in both.
         *
         * The union of the two was tried first, on the theory that it was the
         * bound that stayed correct without knowing which one becomes the
         * raster scissor. It is correct and it is useless: WORLD and MINIMAP
         * both carry the root box as their clip, so the union is the whole
         * canvas and the damage rect never bounded anything -- measured at
         * 93% of frames retained and 0% of frames damaged, which is what a
         * bound that is always the screen looks like from the outside. */
        {
            int x0 = d->x > d->clip.x ? d->x : d->clip.x;
            int y0 = d->y > d->clip.y ? d->y : d->clip.y;
            int x1 = d->x + d->w;
            int y1 = d->y + d->h;
            int cx1 = d->clip.x + d->clip.w;
            int cy1 = d->clip.y + d->clip.h;

            if( cx1 < x1 )
                x1 = cx1;
            if( cy1 < y1 )
                y1 = cy1;
            ToriRS_DamageRegionAdd(&app->damage, x0, y0, x1 - x0, y1 - y0);
        }
    }
    g_damage_trace = 0;

    ToriRS_DamageRegionClamp(&app->damage, width, height, app_damage_rects_armed());
}

int
App_DamageRects(
    struct App const* app,
    struct ToriRS_DamageRect const** out_rects)
{
    assert(app);
    return ToriRS_DamageRegionRects(&app->damage, out_rects);
}

/*
 * TORIRS_DAMAGE_REPORT=1: how much of the canvas damage drawing actually
 * saved, printed once at exit.
 *
 * Here because "damage is on" and "damage is doing anything" are different
 * claims, and the frame rate cannot tell them apart: a gate that never fires
 * and a box that always covers the screen both read as no change. The two
 * numbers below separate them.
 */
static struct
{
    int64_t frames;
    int64_t retained;
    int64_t damaged;
    int64_t damaged_area;
    int64_t canvas_area;
} g_damage_stats;

static void
app_damage_report_dump(void)
{
    double area_pct;

    if( g_damage_stats.frames == 0 )
        return;
    area_pct = g_damage_stats.damaged > 0 ? 100.0 * (double)g_damage_stats.damaged_area /
                                                (double)g_damage_stats.canvas_area
                                          : 100.0;
    TORIRS_REPORT(
        "[damage] frames=%lld retained=%lld (%.1f%%) damaged=%lld (%.1f%%)\n",
        (long long)g_damage_stats.frames,
        (long long)g_damage_stats.retained,
        100.0 * (double)g_damage_stats.retained / (double)g_damage_stats.frames,
        (long long)g_damage_stats.damaged,
        100.0 * (double)g_damage_stats.damaged / (double)g_damage_stats.frames);
    TORIRS_REPORT("[damage] mean box on damaged frames: %.1f%% of canvas\n", area_pct);
}

static void
app_damage_note(
    struct App const* app,
    int width,
    int height)
{
    static int armed = -1;

    /* Periodic, not atexit: the measurement harness ends an arm with
     * taskkill /F, which runs no atexit handler, so an end-of-run dump is a
     * dump that never appears. */
    if( armed < 0 )
        armed = getenv("TORIRS_DAMAGE_REPORT") ? 1 : 0;
    if( !armed )
        return;

    g_damage_stats.frames++;
    if( app->ui_retained_frame )
        g_damage_stats.retained++;
    if( app->damage.valid )
    {
        g_damage_stats.damaged++;
        g_damage_stats.damaged_area += (int64_t)app->damage.w * app->damage.h;
        g_damage_stats.canvas_area += (int64_t)width * height;
    }
    if( g_damage_stats.frames % 600 == 0 )
    {
        app_damage_report_dump();
        TORIRS_REPORT(
            "[damage] box: %d,%d %dx%d valid=%d\n",
            app->damage.x,
            app->damage.y,
            app->damage.w,
            app->damage.h,
            app->damage.valid);
        /* Dump the contributing descs on the NEXT frame -- this one has
         * already unioned them. */
        g_damage_trace = 1;
    }
}

int
App_PresentDamage(
    struct App const* app,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    assert(app);
    assert(out_x);
    assert(out_y);
    assert(out_w);
    assert(out_h);
    return ToriRS_DamageRegionBox(&app->damage, out_x, out_y, out_w, out_h);
}

void
App_Render(
    struct App* app,
    int* pixels,
    int width,
    int height)
{
    struct ToriRS_Frame frame;

    assert(app);
    assert(pixels);
    assert(app->soft);

    app->frames_rendered++;

    if( !App_BuildFrame(app, &frame, width, height) )
    {
        /*
         * The startup progress bar. @see engine/boot_bar.h for why its
         * geometry is the one screen here that is not revconfig's.
         *
         * The deob has a second placement, 50 pixels BELOW centre, for when
         * the title screen is already up behind it. This client never needs
         * it: once the title tree is baked App_BuildFrame succeeds and the
         * panel's own bar takes over, so the only bar drawn here is the
         * centred one.
         */
        char const* caption;
        int caption_font_scene_id = -1;
        int percent = app->boot_progress;

        /*
         * What the boot task asked for, when it asked for anything.
         *
         * The render step does not decide what a load looks like: the task
         * that knows which stage it is at says so, and this obeys. That is
         * the whole point of the opt-in -- a task which never asks keeps the
         * old behaviour of settling silently, and one which does asks for a
         * specific picture rather than merely for a frame.
         *
         * A step that states no bar position (percent -1) leaves the bar
         * where the last stated one put it -- the profile's own contract
         * ("or -1 to leave the bar alone", rs_preload.h). Overriding with -1
         * would clamp to zero and walk the bar backwards.
         */
        if( app->runner.render.intent == TORIRS_RENDER_BOOT_BAR && app->runner.render.percent >= 0 )
            percent = app->runner.render.percent;

        /* Post-login (and any later quiet bake), the reference shows ONLY the
         * sentence: a black screen and "Loading - please wait.", no bar. The
         * bar belongs to the boot's own loading screen, which already ran to
         * 100 before the title. */
        if( App_BootTextOnly(app) )
        {
            for( int i = 0; i < width * height; i++ )
                pixels[i] = 0;
        }
        else
            BootBar_Draw((uint32_t*)pixels, width, height, percent);

        /*
         * The caption -- the same words and the same face the GPU lanes get
         * from App_BootBarCaption, so a boot does not read differently
         * depending on which renderer came up.
         *
         * Centred on the track and sitting on its baseline, where both
         * references put it, rather than in the middle of the canvas.
         */
        caption = App_BootBarCaption(app, &caption_font_scene_id);
        if( caption )
            app_boot_bar_caption(
                app,
                pixels,
                width,
                height,
                BootBar_OriginX(width) + BOOT_BAR_W / 2,
                BootBar_OriginY(height) + BOOT_BAR_TEXT_BASELINE,
                caption,
                caption_font_scene_id);
        return;
    }

    ToriRS_Soft3D_Init(app->soft, app->scene, pixels, width, height);

    /* Must follow BuildFrame: the emit list it publishes is what says which
     * regions are live this frame. */
    app_compute_damage(app, width, height);
    app_damage_note(app, width, height);
    /* World hittest rides the render: each visible model is tested against
     * the mouse point right after it projects (the only window where the
     * scene scratch holds its projection), then the raw hits classify into
     * the pickset + hover tile the click/hotkey paths consume next frame. */
    if( app_world_drawable(app) && app->world_mouse_in_viewport )
        ToriRS_Soft3D_SetPick(app->soft, app->world_mouse_x, app->world_mouse_y);

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_RENDER)
    {
        ToriRS_Soft3D_RenderFrame(app->soft, &frame);
    }

    /* deob method5761 / Client-TS REBUILD_NORMAL: while the scene rebuilds,
     * the game area shows "Loading - please wait." instead of the world. */
    if( app->world_load_server_driven && app->world_load_inflight )
        app_draw_rebuild_loading_overlay(app, pixels, width, height);

    /* And over the top of either: the session is gone. Last, so it is not the
     * thing a rebuild overlay covers — a reconnect drives a rebuild, and the
     * two would otherwise overlap with the wrong one winning. */
    if( app->net_lost )
        app_draw_connection_lost_overlay(app, pixels, width, height);

    if( torirs_env_frame_debug() )
        TORIRS_LOG(
            "frame: draws element=%d terrain=%d dropped not_live=%d no_model=%d\n",
            frame.dbg_emit_element,
            frame.dbg_emit_terrain,
            frame.dbg_drop_not_live,
            frame.dbg_drop_no_model);

    if( app->soft->pick_enabled )
        App_PickFinish(app, &app->soft->pick_hits);
}

int
App_WriteBmp(
    struct App* app,
    char const* path,
    int width,
    int height)
{
    assert(app);
    return UITreeCmd_WriteBmp(app->scene, app->emit.cmds, app->emit.count, path, width, height);
}

void
App_SetPluginChromeExec(
    struct App* app,
    struct ToriRSChromeExec const* exec,
    int kind,
    int explicit_choice)
{
    assert(app);
    assert(exec);
    app->plugin_exec_pending = *exec;
    /* A newly installed presentation needs the complete retained rail even
     * when the registry itself did not change. */
    ToriRSChromeRailSync_Init(&app->plugin_rail);
    app->plugin_rail_has_layout = 0;
    app->plugin_exec_explicit = explicit_choice ? 1 : 0;
    /* Carried so a refusal can name what refused. Without it the fallback
     * message said "the 'buffer' executor would not start", which is both
     * impossible and unhelpful. */
    app->plugin_exec_kind = kind;
}
