#include "app.h"

#if defined(TORIRS_WEB_CACHE_IDB)
#include "platform/dat2_web_store.h"
#include "platform/web_cache_boot.h"
#endif

#include "bmp.h"
/* Screenshot encoding. Already linked for the cache codecs; the PNG writer
 * rides along, so a plugin capture costs no new dependency. */
#include "miniz.h"
#include "bootmanifest/bootmanifest.h"
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
#if defined(TORIRS_PLATFORM_WEB)
#include <emscripten.h>

/*
 * Ask the page to open the command-panel tab (`[editor:boot] panel=tab`).
 *
 * EM_JS rather than EM_ASM, matching main.c: EM_ASM is rejected in `-std=c*`
 * modes and this file is C11. The page owns the channel -- this only asks, and
 * a page that defines no hook (or a browser that blocks the popup) leaves the
 * editor running without its chrome rather than failing the boot.
 */
EM_JS(void, web_editor_open_panel_tab, (void), {
    if( typeof window.torirsOpenPanelTab === 'function' )
        window.torirsOpenPanelTab();
    else
        console.warn('[torirs] panel=tab, but the page defines no torirsOpenPanelTab()');
});
#endif

#include "engine/torirs_chrome_skin_baked.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat1/dat1_tasks.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/entity_model_build.h"
#include "engine/player_appearance.h"
#include "engine/task_obj_model_load.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_model_inst_cache.h"
#include "engine/torirs_worldmap_from_rscache.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "engine/uitree_cmd_render.h"
#include "engine/world_builder/task_world_load.h"
#include "engine/world_builder/world_builder.h"
#include "game/rs_attack_option.h"
#include "game/rs_client_trigger.h"
#include "game/rs_clientcode.h"
#include "game/rs_cs2_dispatch.h"
#include "game/rs_game_events.h"
#include "game/rs_gameproto_exec.h"
#include "game/rs_minimenu_build.h"
#include "plugin/torirs_plugin_lua.h"
#include "plugin/task_plugin_io.h"
#include "plugin/torirs_plugin_lua.h"
#include "plugin/torirs_plugin_registry.h"
#include "game/rs_minimenu_cross.h"
#include "game/rs_worldmap.h"
#include "game/rs_worldmap_render.h"
#include "game/varc_ids.h"
#include "game/task_cs1_run.h"
#include "game/task_cs2_run.h"
#include "game/task_exec_entity_info.h"
#include "game/task_gameproto_exec.h"
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
#include "platform/platform_sdl2_renderer_soft3d.h"
#include "render/torirs_frame.h"
#include "render/torirs_pick.h"
#include "toridraw.h"
#include "toridraw_model_transform.h"
#include "ui/uitree_build.h"
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

static struct AppDebugHotkeyBinding const*
app_debug_binding_down(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target)
{
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

enum
{
    APP_LOGIC_TICK_MS = 20,
    APP_MAX_CATCHUP_TICKS = 5,
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
     * abandon, not a stream to replay. It has to sit above any legitimate
     * hitch: a slow map load or a GC pause is hundreds of milliseconds, not
     * seconds.
     */
    APP_NET_TIMEOUT_MS = 15000,
    APP_NET_STALL_MS = 4000,
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
    /* Wheel zoom, as a percentage of the follow camera's natural orbit
     * distance. The bounds keep the eye outside the player model at one end
     * and inside the scene's draw distance at the other. */
    APP_WORLD_ZOOM_DEFAULT_PCT = 100,
    APP_WORLD_ZOOM_MIN_PCT = 40,
    APP_WORLD_ZOOM_MAX_PCT = 360,
    APP_WORLD_ZOOM_STEP_PCT = 10,
    /* Free camera (offline / scripted): no orbit distance to scale, so a notch
     * dollies along the view axis instead. */
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
    int x = 0, y = 0, w = 0, h = 0;

    if( app->slots.chat_index < 0 )
        return 0;
    node = &app->tree->components[app->slots.chat_index];
    UITree_LayoutGetBounds(&node->position, &x, &y, &w, &h);
    if( out_x )
        *out_x = x;
    if( out_y )
        *out_y = y;
    if( out_font_id )
        *out_font_id = node->u.chat.font_id > 0 ? node->u.chat.font_id : 1;
    return 1;
}

/* Component id of a chatbox child, for the revisions whose chatbox is widgets. */
static int
app_chatbox_uid(
    struct App const* app,
    int child)
{
    return (app->cfg.chatbox.interface_id << 16) | (child & 0xffff);
}

/* True when a canvas-space point lands inside the chat region's bounds. Used to
 * decide chat input focus on a left click.
 *
 * Both eras answer it, because focus is not a dat1 idea: a revconfig tree tags
 * its chat region with a slot (`slots.chat_index`), and a cache tree has the
 * chatbox interface's own root instead (rev 230: 162:0, the node whose onKey
 * hook is the typed line). Only one of the two exists in any given boot. */
static int
app_point_in_chat(
    struct App const* app,
    int x,
    int y)
{
    struct UITreeComponent const* node;
    int32_t idx = app->slots.chat_index;
    int bx = 0, by = 0, bw = 0, bh = 0;

    if( idx < 0 && RS_ChatWidgets_Enabled(&app->cfg.chatbox) )
        idx = UITree_FindByComponentId(app->tree, app_chatbox_uid(app, 0));
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
    assert(app);
    /* A ToriRSChrome text field under the caret, in EITHER instance -- set by
     * ToriRSChrome_MouseDown and cleared by a click elsewhere, so this is
     * exactly "a chrome panel has a field being typed into" (the map editor's
     * Height field; a plugin's colour or note field). Without it, typing a
     * height value like "8" both edited the field AND fired the
     * map-editor-toggle hotkey on the same keystroke -- one more instance of
     * the bug this function exists to kill everywhere at once. */
    return app->chat_input_active || app->chat.social_input_open ||
           app->chat.dialog_input_open || app_iface_text_input_focused(app) ||
           app_chrome_holds_keyboard(app);
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
 * The unfocused chat line.
 *
 * The focused line ends in the reference's `*` caret; this one ends in an
 * ellipsis and says what to press, because "no caret" on its own is a
 * difference nobody reads — the symptom players report is that typing does
 * nothing, not that a character is missing. The colour is a muted grey against
 * the chatbox's tan, so the prompt reads as chrome rather than as a message
 * somebody sent.
 */
#define APP_CHAT_PROMPT_TEXT "<col=4a443a>Press Enter to chat...</col>"

/*
 * Paint the prompt over the script-owned input line while the chat is
 * unfocused (see RS_ChatWidgetLayout.prompt_child).
 *
 * Every frame rather than once on the focus edge: while unfocused the cache can
 * still rewrite that component for its own reasons — the filter tabs recompose
 * the line to change the mode icon — and a one-shot write would be silently
 * undone by the next one of those. The text is compared first, so the ordinary
 * case is a read and the tree is not marked dirty 50 times a second.
 */
static void
app_chat_prompt_apply(struct App* app)
{
    struct UITreeComponent const* node;
    int32_t idx;
    int uid;

    assert(app);
    if( app->cfg.chatbox.prompt_child < 0 || app->chat_input_active )
        return;

    uid = app_chatbox_uid(app, app->cfg.chatbox.prompt_child);
    idx = UITree_FindByComponentId(app->tree, uid);
    if( idx < 0 )
        return;
    node = &app->tree->components[idx];
    if( node->type == UIELEM_RS_TEXT && node->u.rs_text.text &&
        strcmp(node->u.rs_text.text, APP_CHAT_PROMPT_TEXT) == 0 )
        return;
    UITree_ApplyText(app->tree, uid, APP_CHAT_PROMPT_TEXT);
}

/*
 * Chat input focus, for the frame.
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

    /* The loc editor took W/A/S/D/R/Space/Backspace for the frame and has
     * already forced the focus flags off; do not hand them back under it. */
    if( app->locedit_visible )
        return 0;
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
    {
        app->need_redraw = 1;
        /* Focus regained: undo the prompt by asking the era's own script to
         * recompose the line from the input state it keeps (rev 230: 223 out of
         * %varcstring335). Enqueued like any other clientscript; the frame's
         * pump runs it. */
        if( app->chat_input_active && app->cfg.chatbox.input_script > 0 )
            RS_CS2_RunScript(
                &app->host, &app->runner, app->cfg.chatbox.input_script, NULL, 0, 0, NULL, 0);
    }
    return suppress;
}

/*
 * May this component's onKey hook see the frame's typed keys?
 *
 * The chatbox's hook (script 73 at rev 230) is the typed line itself, so it
 * sees a key only while the line has focus — otherwise every keystroke lands in
 * a line the player is not looking at, which is what the chatbox used to do.
 * Everything else is the mirror image: while the line has focus no other onKey
 * runs, and the gameframe's key handler (script 905, the one that switches
 * tabs) is the reason that matters.
 *
 * Revisions with no widget chatbox have neither hook and gate nothing.
 */
static int
app_key_target_accepts(
    struct App const* app,
    int com_id,
    int chat_keys_suppressed)
{
    assert(app);
    if( !RS_ChatWidgets_Enabled(&app->cfg.chatbox) )
        return 1;
    if( ((com_id >> 16) & 0xffff) == app->cfg.chatbox.interface_id )
        return app->chat_input_active && !chat_keys_suppressed;
    return !app->chat_input_active;
}

/*
 * Rebuild the chat presentation (called before every emit).
 *
 * Two shapes, one per era, and only one of them is live in any given boot. A
 * revision whose chatbox is *widgets* (rev 230's interface 162, declared in the
 * manifest's `[ui:chatbox]`) has its 500 line components written; a revision
 * whose chatbox is a *surface* has the flattened draw view rebuilt for
 * `emit_chat`. See rs_chat_widgets.h for why they cannot be the same code.
 *
 * The widget path returns early rather than falling through: a dat2 tree has no
 * chat *region* either, so the surface path below would zero the view and, more
 * to the point, running both would draw the messages twice.
 */
static void
app_chat_build_view(struct App* app)
{
    struct RS_ChatFilters filters = app_chat_filters(app);
    int font_id = 1;

    if( RS_ChatWidgets_Enabled(&app->cfg.chatbox) )
    {
        RS_ChatWidgets_Apply(app->tree, &app->chat, &filters, &app->cfg.chatbox);
        app_chat_prompt_apply(app);
        memset(&app->chat_view, 0, sizeof(app->chat_view));
        return;
    }

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
        app->slots.chat_com_id != -1,
        app->chat_input_active || app->chat.social_input_open || app->chat.dialog_input_open,
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

static unsigned
app_if_events_for_node(
    struct App const* app,
    int com_id);

static int
app_if_events_override_get(
    struct App const* app,
    int com_id,
    int sub_id,
    unsigned* out_events);

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
    return (int)app_if_events_for_node((struct App const*)user, com_id);
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
    if( !app_if_events_override_get((struct App const*)user, com_id, -1, &events) )
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
    struct AppIfEvents* replacement;
    int replacement_count = 0;
    int replacement_cap;

    assert(app);
    if( from > to )
    {
        int swap = from;
        from = to;
        to = swap;
    }

    /* IfSetEventsV2 replaces only the addressed interval. Preserve the pieces
     * on either side of an overlap; one component may have many independently
     * armed dynamic-child ranges. */
    replacement_cap = app->if_event_count + 3;
    replacement = malloc((size_t)replacement_cap * sizeof(*replacement));
    assert(replacement);
    for( int i = 0; i < app->if_event_count; i++ )
    {
        struct AppIfEvents old = app->if_events[i];
        int old_from = old.from;
        int old_to = old.to;

        if( old.com_id != com_id || old.to < from || old.from > to )
        {
            replacement[replacement_count++] = old;
            continue;
        }
        if( old_from < from )
        {
            old.from = old_from;
            old.to = from - 1;
            replacement[replacement_count++] = old;
        }
        if( old_to > to )
        {
            old.to = old_to;
            old.from = to + 1;
            replacement[replacement_count++] = old;
        }
    }
    replacement[replacement_count].com_id = com_id;
    replacement[replacement_count].from = from;
    replacement[replacement_count].to = to;
    replacement[replacement_count].events = events;
    replacement_count++;
    free(app->if_events);
    app->if_events = replacement;
    app->if_event_count = replacement_count;
    app->if_event_cap = replacement_cap;

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
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
    /*
     * Everything IF_SETEVENTS armed, dropped.
     *
     * Called on IF_OPENTOP, because that is when the real client drops it: the
     * root change rebuilds the widget state and the events map is part of it.
     * The table is not freed, only emptied — the next root re-arms into the
     * same allocation.
     */
    if( app )
        app->if_event_count = 0;
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
    for( int i = 0; i < app->if_event_count; i++ )
    {
        if( app->if_events[i].com_id == com_id && app->if_events[i].from <= sub_id &&
            app->if_events[i].to >= sub_id )
            return app->if_events[i].events;
    }
    return 0;
}

static int
app_if_events_override_get(
    struct App const* app,
    int com_id,
    int sub_id,
    unsigned* out_events)
{
    assert(app);
    for( int i = 0; i < app->if_event_count; i++ )
    {
        if( app->if_events[i].com_id == com_id && app->if_events[i].from <= sub_id &&
            app->if_events[i].to >= sub_id )
        {
            if( out_events )
                *out_events = (unsigned)app->if_events[i].events;
            return 1;
        }
    }
    return 0;
}

/*
 * The events governing a node, including the ones armed on its container.
 *
 * `IF_SETEVENTS` carries a sub-id RANGE, and that is not decoration: it is how
 * the server arms a list whose entries do not exist yet. The emotes tab is the
 * clearest case — interface 216's onload `cc_create`s one cell per emote, so at
 * login there is nothing to address but the container, and the server says
 * "slots 0..55 of `emote:contents` have op 1".
 *
 * `App_IfEventsGet` matches a component id exactly, so a dynamic child found
 * nothing and every emote click was dropped by the arming gate. The right-click
 * menu still offered "Perform Bow", because the row comes from the cache's own
 * op list — so the tab hovered, highlighted and named the verb, and clicking did
 * nothing at all.
 *
 * A dynamic child therefore asks its parent, and the sub-id has to fall inside
 * the declared range: a container armed for slots 0..27 must not arm slot 30.
 * A static component is unchanged — it has no parent range to inherit and its
 * own entry is the answer.
 */
static void
app_if_button_target(
    struct App const* app,
    int com_id,
    int* out_com,
    int* out_sub)
{
    int32_t idx;
    struct UITreeComponent const* node;
    int32_t parent;

    *out_com = com_id;
    *out_sub = -1;
    assert(app);
    if( !app->tree )
        return;

    idx = UITree_FindByComponentId(app->tree, com_id);
    if( idx < 0 )
        return;
    node = &app->tree->components[idx];
    if( !node->dynamic )
        return;

    /* A dynamic child is addressed as (container, index within it) — the two
     * fields RSProt's If3Button carries as `combinedId` and `sub`, and the whole
     * reason `sub` exists. Its own component id is a runtime allocation the
     * server has never heard of. */
    parent = node->parent;
    if( parent < 0 || (uint32_t)parent >= app->tree->component_count )
        return;
    *out_com = app->tree->components[parent].component_id;
    *out_sub = node->dynamic_child_index;
}

unsigned
App_IfEventsGetEffective(
    struct App const* app,
    int com_id)
{
    int target;
    int sub;
    unsigned events;
    int32_t idx;

    assert(app);

    /* rev239 class545.method12093 first consults the server's per-widget /
     * per-child override table and falls back to the widget's decoded flags
     * when no entry exists. An override whose value is zero is therefore
     * meaningful: it disables cache-authored ops and must not be confused
     * with an absent entry. */
    if( app_if_events_override_get(app, com_id, -1, &events) )
        return events;

    app_if_button_target(app, com_id, &target, &sub);
    if( target != com_id && app_if_events_override_get(app, target, sub, &events) )
        return events;

    if( !app->tree )
        return 0;
    idx = UITree_FindByComponentId(app->tree, com_id);
    if( idx >= 0 )
        return (unsigned)app->tree->components[idx].behavior.click_mask;
    return 0;
}

static unsigned
app_if_events_for_node(
    struct App const* app,
    int com_id)
{
    return App_IfEventsGetEffective(app, com_id);
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
 * The plugin veto sits BEFORE the builder runs, and that ordering is the whole
 * point of putting it here rather than around ToriRS_Network_SendRaw. Every
 * net_out_* builder encrypts its opcode by advancing the outbound ISAAC
 * stream, so a packet that is built and then discarded leaves the cipher one
 * step ahead of the server's and every opcode after it decrypts to garbage. A
 * veto that never lets the builder run costs the stream nothing.
 *
 * The packet is identified to plugins by the stringified builder call, trimmed
 * to its leading identifier by the host. That is what lets one macro make all
 * sixty send sites observable: there is no packet-name argument anywhere on
 * this path to pass instead, and a hand-written builder-to-enum table would be
 * sixty more chances to label a send wrong.
 */
#define APP_NET_SEND(app, builder_call)                                                            \
    do                                                                                             \
    {                                                                                              \
        if( (app)->net && (app)->net->state == TORIRS_NET_GAME &&                                  \
            !PluginHost_PacketOutVeto((app)->plugins, #builder_call) )                             \
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
    if( screen_x < app->worldmap_box_x - 32 ||
        screen_x > app->worldmap_box_x + app->worldmap_box_w + 32 ||
        screen_y < app->worldmap_box_y - 32 ||
        screen_y > app->worldmap_box_y + app->worldmap_box_h + 32 )
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
    app->worldmap_box_x = box_x;
    app->worldmap_box_y = box_y;
    app->worldmap_box_w = box_w;
    app->worldmap_box_h = box_h;
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
                fprintf(
                    stderr,
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
                fprintf(stderr, "worldmap FORCE_MAP id=%d: Area() returned NULL\n", map_id);
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
        fprintf(
            stderr,
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

    map_x = display_x + (mouse_x - (app->worldmap_box_x + app->worldmap_box_w / 2)) *
                            RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp;
    map_y = display_y - (mouse_y - (app->worldmap_box_y + app->worldmap_box_h / 2)) *
                            RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp;

    source =
        RS_WorldMap_DisplayToSource(app->host.worldmap, ToriRS_WorldMapPackCoord(0, map_x, map_y));
    if( source < 0 )
        return;
    ToriRS_WorldMapUnpackCoord(source, &plane, &abs_x, &abs_z);
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
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
    for( ;; )
    {
        struct UITreeComponent const* n = &tree->components[idx];
        if( n->behavior.hide )
            return 0;
        if( n->parent < 0 )
            return UITree_RootIsDisplayable(tree, idx);
        idx = n->parent;
    }
}

/*
 * Drag to pan the world map.
 *
 * Anchored, like the reference (OsrsClient.updateWorldMapDrag): the grab records
 * where the view was, and every frame sets the view to that origin plus the
 * *total* pointer delta converted to tiles. Accumulating per-frame deltas
 * instead loses the sub-tile remainder on every step, so the map slides behind
 * the pointer over a long drag.
 *
 * Unclamped, also like the reference: dragging past the edge of the map is
 * allowed and dragging back brings it straight back. A clamp on the centre
 * parks the view in a corner of the area, where most of the surface is legitimately
 * off-map and the map appears to have stopped loading.
 *
 * The surface has no widget-level drag — it is a builtin, and the pan lives in
 * the CS2 world map state — so the press is picked up here from the box the
 * emit walk recorded.
 */
static void
app_worldmap_drag_tick(
    struct App* app,
    struct LibToriRS_Input* input,
    int pointer_consumed)
{
    int mouse_x = input->curr.mouse_x;
    int mouse_y = input->curr.mouse_y;

    /* Idle frames skip the tree scan; an in-progress drag still reaches its
     * release handling below. */
    if( !app->worldmap_drag_active && !LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) )
        return;

    if( !app_worldmap_surface_live(app) )
    {
        app->worldmap_drag_active = 0;
        return;
    }

    if( app->worldmap_box_w <= 0 || app->worldmap_box_h <= 0 || !app->host.worldmap )
    {
        app->worldmap_drag_active = 0;
        return;
    }

    /*
     * The map's own chrome sits *inside* the surface box — the close X, the key
     * panel, the search field, the zoom buttons — so "the pointer is in the box"
     * is not "the pointer is on the map". A clickable component under the
     * pointer owns the press: hover_com_id is -1 over bare map and a real id
     * over anything else, which is exactly the distinction needed. Without it,
     * closing the map also teleported the player to whatever tile the X was
     * drawn over.
     */
    if( !app->worldmap_drag_active && !pointer_consumed && !app->interact.minimenu.visible &&
        app->hover_com_id < 0 && LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) &&
        mouse_x >= app->worldmap_box_x && mouse_x < app->worldmap_box_x + app->worldmap_box_w &&
        mouse_y >= app->worldmap_box_y && mouse_y < app->worldmap_box_y + app->worldmap_box_h )
    {
        int display_x = 0;
        int display_y = 0;
        RS_WorldMap_DisplayPosition(app->host.worldmap, &display_x, &display_y);
        if( display_x < 0 || display_y < 0 )
            return;
        app->worldmap_drag_active = 1;
        app->worldmap_drag_x = mouse_x;
        app->worldmap_drag_y = mouse_y;
        app->worldmap_drag_display_x = display_x;
        app->worldmap_drag_display_y = display_y;
        app->worldmap_drag_moved = 0;
    }

    if( !app->worldmap_drag_active )
        return;

    if( !LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT) ||
        input->curr.mouse_button_up[TORIRSM_LEFT] )
    {
        /* Released without ever panning: this was a click on the map, and the
         * server is the one that decides what a click there means (the
         * reference's ClickWorldMap — a teleport for staff, ignored for
         * everyone else). A drag that moved the view is not also a click. */
        if( !app->worldmap_drag_moved )
            app_worldmap_click(app, mouse_x, mouse_y);
        app->worldmap_drag_active = 0;
        return;
    }

    {
        int scale_fp = RS_WorldMap_ZoomScaleFp(app->host.worldmap);
        int dx = mouse_x - app->worldmap_drag_x;
        int dy = mouse_y - app->worldmap_drag_y;
        int next_x;
        int next_y;
        int current_x = 0;
        int current_y = 0;

        if( scale_fp <= 0 )
            scale_fp = RS_WORLDMAP_ZOOM_SCALE_ONE;
        /* Screen y grows downward, map y northward, and the map moves opposite
         * the pointer — the tile under the cursor stays under it. */
        next_x = app->worldmap_drag_display_x - dx * RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp;
        next_y = app->worldmap_drag_display_y + dy * RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp;

        RS_WorldMap_DisplayPosition(app->host.worldmap, &current_x, &current_y);
        if( next_x == current_x && next_y == current_y )
            return;

        RS_WorldMap_SetDisplayPosition(app->host.worldmap, next_x, next_y);
        app->worldmap_drag_moved = 1;
        app->need_redraw = 1;
    }
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
    int dots_scene, marker_scene;

    app->minimap_dot_count = 0;
    *out_dots = app->minimap_dots;
    if( !world || !world->load_complete || !local )
        return 0;
    px = (int)local->draw_position.x;
    pz = (int)local->draw_position.z;
    dots_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPDOTS);
    marker_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPMARKER);

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
                if( icon->level != local->grid_position.level )
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
            if( icon->level != local->grid_position.level )
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
            if( !stack || stack->grid_position.level != local->grid_position.level )
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
            if( !npc || npc->multinpc_hidden || !npc->minimap_visible ||
                !npc->interactable ||
                npc->grid_position.level != local->grid_position.level )
                continue;
            app_minimap_push_dot(
                app, (int)npc->draw_position.x - px, (int)npc->draw_position.z - pz, dots_scene, 1);
        }
        pool = &world->entities.player;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
            if( !player || player == local ||
                player->grid_position.level != local->grid_position.level )
                continue;
            app_minimap_push_dot(
                app,
                (int)player->draw_position.x - px,
                (int)player->draw_position.z - pz,
                dots_scene,
                2);
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
static int
app_world_proj_scale(struct App* app)
{
    struct ToriDraw_Camera const* cam = &app->world_camera;
    int scale;
    if( cam->proj_mode == TORIDRAW_PROJ_MODE_FOV )
        scale = toridraw_proj_scale_from_fov(cam->fov_rpi2048);
    else
        scale = cam->proj_scale;
    return scale > 0 ? scale : TORIDRAW_PROJ_SCALE_DEFAULT;
}

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
    int dx, dy, dz, tmp;
    int sin_pitch, cos_pitch, sin_yaw, cos_yaw;
    int scale;

    if( !app->world || !app->world_view_valid )
        return 0;
    if( fine_x < 128 || fine_z < 128 )
        return 0;

    dx = fine_x - app->world_camera_pos.x;
    dy = world_y - app->world_camera_pos.y;
    dz = fine_z - app->world_camera_pos.z;

    sin_pitch = ToriDraw_Sin(app->world_camera.pitch);
    cos_pitch = ToriDraw_Cos(app->world_camera.pitch);
    sin_yaw = ToriDraw_Sin(app->world_camera.yaw);
    cos_yaw = ToriDraw_Cos(app->world_camera.yaw);

    tmp = (dz * sin_yaw + dx * cos_yaw) >> 16;
    dz = (dz * cos_yaw - dx * sin_yaw) >> 16;
    dx = tmp;

    tmp = (dy * cos_pitch - dz * sin_pitch) >> 16;
    dz = (dy * sin_pitch + dz * cos_pitch) >> 16;
    dy = tmp;

    if( dz < 50 )
        return 0;

    scale = app_world_proj_scale(app);
    *out_x = app->world_emit_desc.x + app->world_emit_desc.w / 2 + (dx * scale / dz);
    *out_y = app->world_emit_desc.y + app->world_emit_desc.h / 2 + (dy * scale / dz);
    return 1;
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
    {
        struct WorldEntity_Player* local = app_local_player(app);
        if( local )
            level = local->grid_position.level;
    }
    ground_y = app_world_height(app, fine_x, fine_z, level);
    return app_world_project_at(app, fine_x, fine_z, ground_y - height_above_ground, out_x, out_y);
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
    if( !el || el->model.kind != TORIDRAWMK_MODEL )
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
    int cap = (int)(sizeof(app->entity_overlays) / sizeof(app->entity_overlays[0]));
    if( app->entity_overlay_count >= cap )
        return;
    app->entity_overlays[app->entity_overlay_count++] = *item;
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
    int font_id)
{
    int height = app_entity_model_height(app, element_id);
    int screen_x, screen_y;

    assert(chat);
    if( chat->timer <= 0 || chat->message[0] == '\0' || font_id < 0 )
        return;
    if( !app_world_project(
            app, (int)draw_position->x, (int)draw_position->z, height, &screen_x, &screen_y) )
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
    int headicons_scene)
{
    int height = app_entity_model_height(app, element_id);
    int screen_x, screen_y;
    int y_off = 30;

    if( headicons == 0 || headicons_scene <= 0 )
        return;
    if( !app_world_project(
            app, (int)draw_position->x, (int)draw_position->z, height + 15, &screen_x, &screen_y) )
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
    if( !app_world_project(
            app, (int)draw_position->x, (int)draw_position->z, height + 15, &screen_x, &screen_y) )
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
    return app_overlay_outline_element_model_trans(
        app, element_id, color, APP_OUTLINE_FILL_TRANS);
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
            fprintf(
                stderr,
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
            app_overlay_outline_element_model(
                app, scenery->element_id, APP_OUTLINE_COLOR_HOVER);
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
                app->world, panel->sel_scene_x, panel->sel_scene_z, panel->sel_level,
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
        static int const corner[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
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
                    app, (base_x + corner[c][0]) * 128, (base_z + corner[c][1]) * 128, plane_y,
                    &screen_x, &screen_y) )
                continue;
            px[count] = screen_x;
            py[count] = screen_y;
            count++;
        }
        if( count == 0 )
            return;

        hull_size = ToriDraw_ConvexHull(px, py, count, hull_x, hull_y);
        app_overlay_push_polygon_filled(
            app, hull_x, hull_y, hull_size, APP_OUTLINE_COLOR_EDITOR_SELECT, APP_OUTLINE_FILL_TRANS);
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

/* Its keyboard half alone -- what a native-widget executor's window needs. */
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
enum
{
    APP_FONT_B12_CACHE_ID = 496,
    APP_FONT_B12_DAT1_SLOT = 2,
    /* Hitsplat numbers use p11 (reference `this.p11.centreString`). RevConfig
     * orders the dat1 title jagfile fonts p11/p12/b12/q8 as slots 0-3; the
     * dat2 fonts table keeps them adjacent with b12 at 496. */
    APP_FONT_P11_CACHE_ID = 494,
    APP_FONT_P11_DAT1_SLOT = 0,
    /* Rebuild loading overlay (deob / Client-TS `p12.centreString`). */
    APP_FONT_P12_CACHE_ID = 495,
    APP_FONT_P12_DAT1_SLOT = 1,
};

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
        app_world_project(
            app, (int)draw_position->x, (int)draw_position->z, height + 15, &screen_x, &screen_y) )
    {
        app_overlay_build_healthbar(app, combat, screen_x, screen_y);
    }
    else if(
        combat->healthbar_type < 0 && combat->combat_cycle > cycle + 100 &&
        combat->total_health > 0 &&
        app_world_project(
            app, (int)draw_position->x, (int)draw_position->z, height + 15, &screen_x, &screen_y) )
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
        if( !app_world_project(
                app,
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
            int splat_sprite = RS_Hitsplats_SpriteFor(&app->hitsplats, combat->damage_types[i]);
            int splat_scene = -1;
            int splat_frame = 0;

            if( splat_sprite >= 0 )
                splat_scene = UITreeSceneBridge_EnsureSprite(&app->bridge, splat_sprite);
            if( splat_scene < 0 && hitmarks_scene > 0 )
            {
                splat_scene = hitmarks_scene;
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
    RS_ClientOpActiveSet(&task->host->clientop, (enum RS_ClientOpKind)task->kind, &task->ctx);
    return 1;
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
    ToriRS_TaskQueue_Add(app->runner.queue, &task->task);

    RS_CS2_RunScript(&app->host, &app->runner, script_id, NULL, 0, 0, NULL, 0);
}

/** The clientscript bound to `trigger` for this subject, or -1. Narrowest form
 *  first, exactly as `ClientScript::Get` walks them. */
static int
app_client_trigger_script(struct App* app, int trigger, int subject, int category)
{
    int id;

    assert(app);

    if( !app->provider )
        return -1;
    id = CacheProvider_ClientScriptIdByNameHash(
        app->provider, RS_ClientTriggerNameHash(RS_ClientTriggerHashSubject(trigger, subject)));
    if( id < 0 && category > 0 )
        id = CacheProvider_ClientScriptIdByNameHash(
            app->provider,
            RS_ClientTriggerNameHash(RS_ClientTriggerHashCategory(trigger, category)));
    if( id < 0 )
        id = CacheProvider_ClientScriptIdByNameHash(
            app->provider, RS_ClientTriggerNameHash(RS_ClientTriggerHashGlobal(trigger)));
    return id;
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
    fprintf(
        stderr,
        "trigger: %s %d (subject=%d category=%d) -> script %d\n",
        what,
        trigger,
        subject,
        category,
        script_id);
}

/** Fire an npc trigger with the npc as the active subject. */
static void
app_client_trigger_npc(struct App* app, struct WorldEntity_NPC* npc, int trigger)
{
    struct ToriRS_Npctype* type;
    struct RS_ClientOpContext ctx;
    int script_id;

    assert(app);
    assert(npc);

    if( !app->world )
        return;
    type = CacheProvider_NpctypeGet(app->provider, npc->npc_id);
    script_id =
        app_client_trigger_script(app, trigger, npc->npc_id, type ? type->category : 0);
    app_client_trigger_debug(
        "npc", trigger, npc->npc_id, type ? type->category : 0, script_id);
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
app_client_trigger_loc(struct App* app, struct WorldEntity_Scenery* loc, int trigger)
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
    app_client_trigger_debug(
        "loc", trigger, loc->loc_id, type ? type->category : 0, script_id);
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
    snprintf(ctx.name, sizeof(ctx.name), "%s", loc->name);
    app_client_trigger_queue(app, &ctx, script_id);
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

/*
 * ---------------------------------------------------------------------------
 * Scripted entity overlays (game/rs_entity_overlay.h).
 * ---------------------------------------------------------------------------
 *
 * The host owns the records and the UITree owns the layers; what is left is
 * the part that needs a camera and a scene, and that is here.
 */

/** Split a packed CS2 coord into SCENE tiles. False when the world has no
 *  scene or the tile is outside the loaded window. */
static bool
app_overlay_coord_to_scene(
    struct App const* app,
    int coord,
    int* out_x,
    int* out_z,
    int* out_level)
{
    assert(app);
    assert(out_x);
    assert(out_z);
    assert(out_level);

    if( !app->world || coord < 0 )
        return false;

    int const level = (coord >> 28) & 0x3;
    int const abs_x = (coord >> 14) & 0x3fff;
    int const abs_z = coord & 0x3fff;
    int const x = abs_x - app->world->_base_tile_x;
    int const z = abs_z - app->world->_base_tile_z;

    if( x < 0 || z < 0 || x >= app->world->_scene_size || z >= app->world->_scene_size )
        return false;
    *out_x = x;
    *out_z = z;
    *out_level = level;
    return true;
}

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

    if( !app_overlay_coord_to_scene(app, coord, &x, &z, &level) )
        return 0;
    scenery = World_SceneryFindByLocId(app->world, x, z, level, loc_type);
    if( !scenery )
        return 0;
    *out_layer = World_LocShapeToLayer(scenery->shape);
    snprintf(out_name, (size_t)name_cap, "%s", scenery->name);
    return 1;
}

/* COORD_INSCENE (6951). */
static int
app_cs2_coord_in_scene(void* user, int coord)
{
    struct App* app = (struct App*)user;
    int x;
    int z;
    int level;

    assert(app);
    return app_overlay_coord_to_scene(app, coord, &x, &z, &level) ? 1 : 0;
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
app_overlay_anchor(struct App* app, struct RS_Overlay const* item)
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
        if( !app_overlay_coord_to_scene(app, item->coord, &x, &z, &level) )
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

    if( !app_world_project(app, fine_x, fine_z, height, &out.top_x, &out.top_y) )
        return out;
    if( !app_world_project(app, fine_x, fine_z, height / 2, &out.mid_x, &out.mid_y) )
        return out;
    if( !app_world_project(app, fine_x, fine_z, -15, &out.foot_x, &out.foot_y) )
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

    /* The parent IS the world rect: it is what clips the overlays (see
     * UITree_ComponentClipsChildren), and the App is the only thing that knows
     * the rect. A tree whose world has not been emitted yet has no rect and so
     * no overlays -- which is right, because there is nothing to anchor to. */
    {
        struct UITreeElemPosition* pos = &app->tree->components[parent].position;
        int const w = app->world_view_valid ? app->world_emit_desc.w : 0;
        int const h = app->world_view_valid ? app->world_emit_desc.h : 0;
        int const x = app->world_view_valid ? app->world_emit_desc.x : 0;
        int const y = app->world_view_valid ? app->world_emit_desc.y : 0;
        if( pos->kind != UIPOS_XY || pos->x != x || pos->y != y || pos->width != w ||
            pos->height != h )
        {
            pos->kind = UIPOS_XY;
            pos->x = x;
            pos->y = y;
            pos->width = w;
            pos->height = h;
            UITree_LayoutInvalidateBoxes(app->tree);
        }
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
            RS_CS2Host_OverlayReap(&app->host, i);
            continue;
        }
        if( !anchor.ok )
            continue; /* live, just not on screen this frame. */

        int32_t const node = UITree_FindByComponentId(app->tree, item->component_id);
        if( node < 0 )
            continue;

        struct UITreeComponent* c = &app->tree->components[node];
        int const w = c->position.width;
        int const h = c->position.height;
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
            c->position.x = x;
            c->position.y = y;
            c->is_dirty = 1;
            UITree_LayoutInvalidateBoxes(app->tree);
        }
    }
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
            font_id,
            hitmarks_scene,
            npctype ? npctype->height : -1);
        app_overlay_build_npc_headicon(
            app,
            npc->element_id,
            npctype,
            &npc->draw_position,
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
            font_id,
            hitmarks_scene,
            -1);
        app_overlay_build_player_headicons(
            app, player->element_id, player->headicon, &player->draw_position, headicons_scene);
    }

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
                app, npc->element_id, &npc->chat, &npc->draw_position, chat_font);
        }

        pool = &world->entities.player;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);
            if( !player || player->element_id < 0 )
                continue;
            app_overlay_build_chat(
                app, player->element_id, &player->chat, &player->draw_position, chat_font);
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
    if( getenv("TORIRS_OVERLAY_DEBUG") && app->entity_overlay_count > 0 )
    {
        fprintf(
            stderr,
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
            fprintf(
                stderr,
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

/**
 * Both chrome instances' display lists, back to back.
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
app_chrome_merged_prims(struct App* app, int* out_count)
{
    int dbg_count = 0;
    int win_count = 0;
    struct ToriRSChromePrim const* dbg = ToriRSChrome_Prims(&app->dbg_ui, &dbg_count);
    struct ToriRSChromePrim const* win = ToriRSChrome_Prims(&app->plugin_ui, &win_count);
    int total;

    assert(out_count);

    /*
     * Nothing to merge: hand the developer chrome's own array straight out, so
     * the common case -- no plugin window open -- costs exactly what it did
     * before this existed.
     *
     * ANY bound executor other than the buffer one is the other way to get
     * here: the plugin window is being presented somewhere else -- its own OS
     * window, the DOM, a tool window, the interface tree -- so putting its
     * prims in the canvas as well would draw it twice, in two places, both of
     * them live. That is not only a surface executor's problem: a native-widget
     * executor rebuilds the window out of foreign controls and the chrome's own
     * display list is exactly what must NOT also appear.
     */
    if( win_count == 0 || app->plugin_exec_kind != TORIRS_CHROME_EXEC_BUFFER )
    {
        *out_count = dbg_count;
        return dbg;
    }

    total = dbg_count + win_count;
    if( total > APP_CHROME_PRIMS_MAX )
        total = APP_CHROME_PRIMS_MAX;

    /* Exact, not a heuristic: the serials move on every rebuild, including one
     * that changed a string without changing the prim count. */
    if( app->chrome_merged_dbg != app->dbg_ui.build_serial ||
        app->chrome_merged_win != app->plugin_ui.build_serial )
    {
        int n = dbg_count < total ? dbg_count : total;
        memcpy(app->chrome_merged, dbg, (size_t)n * sizeof(*dbg));
        if( n < total )
            memcpy(
                &app->chrome_merged[n], win, (size_t)(total - n) * sizeof(*win));
        app->chrome_merged_dbg = app->dbg_ui.build_serial;
        app->chrome_merged_win = app->plugin_ui.build_serial;
        app->chrome_merged_count = total;
    }
    *out_count = app->chrome_merged_count;
    return app->chrome_merged;
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
    case UITREE_HOST_GET_SCROLLBAR_SCENE:
        return UITreeSceneBridge_ScrollbarSceneId(&app->bridge);
    case UITREE_HOST_GET_STATIC_SPRITE_SCENE:
        return UITreeSceneBridge_StaticSpriteSceneId(
            &app->bridge, (enum StaticSpriteSlot)req->u.static_sprite.slot);
    case UITREE_HOST_GET_ENTITY_OVERLAYS:
        *req->u.get_entity_overlays.out_clip_x = app->world_emit_desc.x;
        *req->u.get_entity_overlays.out_clip_y = app->world_emit_desc.y;
        *req->u.get_entity_overlays.out_clip_w = app->world_emit_desc.w;
        *req->u.get_entity_overlays.out_clip_h = app->world_emit_desc.h;
        return app_build_entity_overlays(app, req->u.get_entity_overlays.out_items);
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
         * keeps the eye anchor. */
        struct WorldEntity_Player* local_player = app_local_player(app);
        int anchor_x = local_player ? (int)local_player->draw_position.x : app->world_camera_pos.x;
        int anchor_z = local_player ? (int)local_player->draw_position.z : app->world_camera_pos.z;
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
        return 1;
    case UITREE_HOST_GET_TAB_ENABLED:
        return RS_UISlots_TabEnabled(&app->slots, req->u.tab_enabled.tabno);
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
        return (int)app_if_events_for_node(app, req->u.get_if_events.com_id);
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

/* Demo content until real state sync exists: seed the worn/backpack/bank
 * containers so item-bearing interfaces have something to show. */
static void
seed_inv_defaults(struct InvManager* invs)
{
    static int const k_worn_items[] = { 1153, 1007, 1725, 1333, 1115, 1201,
                                        1189, 1063, 1067, 2564, 882 };
    static int const k_backpack_items[] = { 1333 };
    /* Bank contents so interface 12 renders its item grid + tab row. */
    static int const k_bank_items[] = { 995,  1333, 1153, 1007, 1725, 1115, 1201, 1189, 1063,
                                        1067, 2564, 882,  4151, 1305, 1319, 1215, 1231, 1147,
                                        1163, 1079, 1093, 861,  1163, 1704, 2550, 6585, 1725,
                                        3105, 1387, 1275, 1291, 4587, 1215, 1333, 995,  1038 };

    assert(invs);
    assert(InvManager_ResolveSource(invs, INV_MANAGER_SOURCE_NAME_WORN) >= 0);
    assert(InvManager_ResolveSource(invs, INV_MANAGER_SOURCE_NAME_BACKPACK) >= 0);

    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_WORN,
        k_worn_items,
        NULL,
        (int)(sizeof(k_worn_items) / sizeof(k_worn_items[0]))));
    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_BACKPACK,
        k_backpack_items,
        NULL,
        (int)(sizeof(k_backpack_items) / sizeof(k_backpack_items[0]))));
    assert(InvManager_EnsureContainer(invs, INV_MANAGER_CONTAINER_BANK, 800, "bank") >= 0);
    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_BANK,
        k_bank_items,
        NULL,
        (int)(sizeof(k_bank_items) / sizeof(k_bank_items[0]))));
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

/* True while any item slot still needs a first rasterization attempt. */
static int
app_inv_needs_icons(struct App const* app)
{
    for( int ci = 0; ci < app->invs.container_count; ci++ )
    {
        struct InvContainer const* c = &app->invs.containers[ci];
        if( !c->slots )
            continue;
        for( int s = 0; s < c->slot_count; s++ )
            if( c->slots[s].obj_id > 0 && c->slots[s].scene_id == INV_MANAGER_NO_SCENE_ID )
                return 1;
    }
    return 0;
}

struct Task_InvIconReconcile
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int obj_ids[APP_INV_ICON_BATCH_MAX];
    int counts[APP_INV_ICON_BATCH_MAX];
    int n;
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

    /* Rasterize every pending slot from whatever is now resident and stamp the
     * scene id (or the failed sentinel) back onto the slot. */
    for( int ci = 0; ci < app->invs.container_count; ci++ )
    {
        struct InvContainer* c = &app->invs.containers[ci];
        if( !c->slots )
            continue;
        for( int s = 0; s < c->slot_count; s++ )
        {
            struct InvSlot* slot = &c->slots[s];
            int scene_id;
            if( slot->obj_id <= 0 || slot->scene_id != INV_MANAGER_NO_SCENE_ID )
                continue;
            scene_id = UITreeSceneBridge_EnsureObjIcon(
                &app->bridge, slot->obj_id, slot->obj_count > 0 ? slot->obj_count : 1);
            slot->scene_id = scene_id >= 0 ? scene_id : APP_INV_ICON_SCENE_FAILED;
            slot->atlas_index = 0;
        }
    }

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

    if( app->inv_icon_reconcile_inflight || !app_inv_needs_icons(app) )
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
        fprintf(stderr, "tex_wants drained %d:", id_count);
        for( int i = 0; i < id_count; i++ )
            fprintf(stderr, " %d", ids[i]);
        fprintf(stderr, "\n");
    }

    for( int i = 0; i < id_count; i++ )
    {
        int const id = ids[i];
        int already_pending = 0;

        if( id >= 0 && id < 2048 && app->bridge.texture_failed[id] )
        {
            if( app_tex_trace_enabled() )
                fprintf(stderr, "tex_trace: want id=%d -> skip (already failed)\n", id);
            continue;
        }
        if( UITreeSceneBridge_TextureResident(&app->bridge, id) )
        {
            if( app_tex_trace_enabled() )
                fprintf(stderr, "tex_trace: want id=%d -> skip (already resident)\n", id);
            continue;
        }

        /* A model may be rebuilt while its first texture request is still in
         * flight. Do the pending-set test before creating the task; the old
         * order queued another decoder for every rebuild and only deduplicated
         * the publish list afterwards. */
        for( int p = 0; p < app->tex_pending_count; p++ )
            if( app->tex_pending[p] == id )
            {
                already_pending = 1;
                break;
            }
        if( already_pending )
        {
            if( app_tex_trace_enabled() )
                fprintf(stderr, "tex_trace: want id=%d -> skip (already pending)\n", id);
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
                fprintf(stderr, "tex_trace: want id=%d -> already in provider\n", id);
            ready[ready_count++] = id;
            continue;
        }

        {
            struct ToriRS_Task* task = CreateTask_TextureLoad(app->provider, id);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
            if( app_tex_trace_enabled() )
                fprintf(
                    stderr,
                    "tex_trace: want id=%d -> load task %s\n",
                    id,
                    task ? "queued" : "REFUSED (provider returned no task)");
        }
        if( app->tex_pending_count < 512 )
            app->tex_pending[app->tex_pending_count++] = id;
        else if( app_tex_trace_enabled() )
            fprintf(stderr, "tex_trace: want id=%d -> DROPPED (pending list full)\n", id);
    }

    if( ready_count > 0 )
    {
        int published = UITreeSceneBridge_PublishTextures(&app->bridge, ready, ready_count);
        if( app_tex_trace_enabled() )
            fprintf(
                stderr,
                "tex_trace: immediate publish %d ready -> %d published\n",
                ready_count,
                published);
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

    if( app->tex_pending_count == 0 )
        return;

    for( int i = 0; i < app->tex_pending_count; i++ )
    {
        int id = app->tex_pending[i];

        if( id < 0 || id >= 2048 || app->bridge.texture_failed[id] )
        {
            if( app_tex_trace_enabled() )
                fprintf(stderr, "tex_trace: poll id=%d -> dropped (failed/out of range)\n", id);
            continue;
        }
        if( UITreeSceneBridge_TextureResident(&app->bridge, id) )
        {
            if( app_tex_trace_enabled() )
                fprintf(stderr, "tex_trace: poll id=%d -> dropped (resident)\n", id);
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
                fprintf(
                    stderr,
                    "tex_trace: poll id=%d -> MARKED FAILED (queue idle, not in provider)\n",
                    id);
        }
        else
        {
            app->tex_pending[kept++] = id;
        }
    }
    app->tex_pending_count = kept;

    if( ready_count > 0 )
    {
        int published = UITreeSceneBridge_PublishTextures(&app->bridge, ready, ready_count);
        if( app_tex_trace_enabled() )
            fprintf(
                stderr,
                "tex_trace: publish %d ready -> %d published (%d still pending)\n",
                ready_count,
                published,
                app->tex_pending_count);
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
        /* The terminal config was loaded by the walk above. Load its complete
         * body and movement set before the caller mounts/replaces the model. */
        for( self->model_i = 0;; self->model_i++ )
        {
            struct ToriRS_Npctype* npctype =
                CacheProvider_NpctypeGet(app->provider, self->resolved_npc_id);
            if( !npctype || self->model_i >= npctype->models_count )
                break;
            PT_TASK_AWAITSELF_IF(
                CreateTask_ModelLoad(app->provider, npctype->models[self->model_i]));
        }
        for( self->seq_i = 0; self->seq_i < 5; self->seq_i++ )
        {
            int seq_id = -1;
            struct ToriRS_Npctype* npctype =
                CacheProvider_NpctypeGet(app->provider, self->resolved_npc_id);
            if( npctype )
            {
                int seqs[5] = {
                    npctype->readyanim,  npctype->walkanim,   npctype->walkanim_b,
                    npctype->walkanim_r, npctype->walkanim_l,
                };
                seq_id = seqs[self->seq_i];
            }
            if( seq_id >= 0 )
                PT_TASK_AWAITSELF_IF(CreateTask_SequenceLoad(app->provider, app->scene, seq_id));
        }
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
    struct World_EntityPool* pool;
    enum
    {
        MAX_REFRESH = 256
    };
    struct
    {
        int x, z, level, loc_id, shape, angle;
    } pending[MAX_REFRESH];
    int n = 0;

    if( !app || !app->world || !app->world->load_complete || !app->provider )
        return;
    if( varp_id < 0 )
        return;

    pool = &app->world->entities.scenery;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery* sc = World_EntityPoolGet(pool, i);
        struct ToriRS_Location* loc;
        int x, z, level, shape, angle, loc_id;
        int dup;

        if( !sc )
            continue;
        loc = CacheProvider_LocationGet(app->provider, sc->loc_id);
        if( !app_loc_transform_depends_on_varp(app, loc, varp_id) )
            continue;

        x = sc->grid_position.x;
        z = sc->grid_position.z;
        level = sc->grid_position.level;
        loc_id = sc->loc_id;
        shape = sc->shape;
        angle = sc->angle;

        /* L-walls register two pool halves on the same tile+shape — one refresh. */
        dup = 0;
        for( int j = 0; j < n; j++ )
        {
            if( pending[j].x == x && pending[j].z == z && pending[j].level == level &&
                pending[j].shape == shape )
            {
                dup = 1;
                break;
            }
        }
        if( dup )
            continue;
        if( n >= MAX_REFRESH )
            break;
        pending[n].x = x;
        pending[n].z = z;
        pending[n].level = level;
        pending[n].loc_id = loc_id;
        pending[n].shape = shape;
        pending[n].angle = angle;
        n++;
    }

    for( int i = 0; i < n; i++ )
        App_WorldLocChange(
            app,
            pending[i].x,
            pending[i].z,
            pending[i].level,
            pending[i].loc_id,
            pending[i].shape,
            pending[i].angle);
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
        app->player_attack_option =
            RS_AttackOption_FromVarp(VarPManager_GetVarp(&app->varps, varp_id));
        break;
    case RS_ATTACK_OPTION_CLIENTCODE_NPC:
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
    printf(
        "app: cache profile epoch=%s game=%s revision=%d quirks=%s\n",
        RSCache_EpochName(profile.epoch),
        RSCache_GameName(profile.game),
        profile.revision,
        quirks_buf);

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

/*
 * Draw a chrome display list into an arbitrary buffer.
 *
 * Lent to a SURFACE executor so a second window is drawn by the same code as
 * the first: the same prim list, the same ToriRS_Frame translator, the same
 * software backend. A second rasteriser for the aux window would be a second
 * set of rounding, a second baseline convention, and a second place for the
 * chrome to be almost right.
 *
 * The frame is hand-built rather than walked out of the tree, because the tree
 * describes the GAME canvas -- its layout, its viewport, its world. This window
 * holds one thing, so the emit buffer is exactly one desc long.
 */
void
App_ChromeRasterise(
    void* user,
    int* pixels,
    int width,
    int height,
    struct ToriRSChromePrim const* prims,
    int count)
{
    struct App* app = user;
    struct UITreeEmitDesc desc;
    struct ToriRS_Frame frame;
    struct ToriRS_Soft3D soft;

    assert(app);
    assert(pixels);
    if( !app->scene || count <= 0 )
        return;

    memset(&desc, 0, sizeof(desc));
    desc.kind = UITREE_EMIT_DEBUG_OVERLAY;
    desc.debug_prims = prims;
    desc.debug_prim_count = count;
    for( int i = 0; i < TORIRS_CHROME_FONT_SLOT_COUNT; i++ )
        desc.debug_font_id[i] = UITreeSceneBridge_EnsureDebugFont(&app->bridge, i);
    desc.debug_skin_scene_id = UITreeSceneBridge_EnsureChromeSkin(&app->bridge);
    for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT; i++ )
        desc.debug_skin_atlas[i] = i;
    desc.clip.x = 0;
    desc.clip.y = 0;
    desc.clip.w = width;
    desc.clip.h = height;

    ToriRS_FrameInit(&frame);
    ToriRS_FrameSetScene(&frame, app->scene);
    /* The surface's own size, not the game canvas's: every clip in the list
     * below is measured against this, and a frame that thinks it is 765 wide
     * would clip a 360-wide window's chrome to a box off the right of it. */
    ToriRS_FrameSetCanvas(&frame, width, height);
    /* One desc, handed directly: the buffer form exists for the tree's whole
     * emit walk, and this window is a single display list. */
    ToriRS_FrameSetEmit(&frame, &desc, 1);
    ToriRS_Soft3D_Init(&soft, app->scene, pixels, width, height);
    ToriRS_Soft3D_RenderFrame(&soft, &frame);
}

int
App_SetChromeScale(struct App* app, int scale)
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
    app->plugin_panel = -1;
    app->plugin_button_node = -1;
    app->plugin_panel_built_for = -1;
    app->plugin_panel_built_rev = -1;
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
    app->settings_colour_panel = ToriRSChrome_PanelAdd(
        &app->dbg_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 40, 0, "Colour");
    ToriRSChrome_PanelSetFramed(&app->dbg_ui, app->settings_colour_panel, 1);
    ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->settings_colour_panel, 0);
    app->settings_colour_visible = 0;
    app->settings_colour_pick = -1;
    app->settings_colour_default_btn = -1;
    app->settings_colour_close_btn = -1;
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
    snprintf(app->locedit_name, sizeof(app->locedit_name), "%s", scenery->name);
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
    snprintf(app->locedit_name, sizeof(app->locedit_name), "%s", scenery->name);
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
     * them (src/input/torirs_keymap.c: 96 left, 97 right, 102 home, 103 end). */
    case 96:
        return TORIRS_CHROME_KEY_LEFT;
    case 97:
        return TORIRS_CHROME_KEY_RIGHT;
    case 102:
        return TORIRS_CHROME_KEY_HOME;
    case 103:
        return TORIRS_CHROME_KEY_END;
    default:
        return TORIRS_CHROME_KEY_NONE;
    }
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
    if( input->curr.mouse_button_down[TORIRSM_LEFT] &&
        ToriRSChrome_MouseDown(ui, input->curr.mouse_x, input->curr.mouse_y) )
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
 * The keyboard half of the routing above, on its own because a NATIVE-WIDGET
 * executor needs exactly this half: its clicks arrive as component clicks on
 * the interface tree, but its text fields are the model's, so typing still has
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
    if( !app_text_input_focused(app) && app_debug_key_down(app, input, APP_DEBUG_HOTKEY_LOC_EDITOR) )
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
        fprintf(stderr, "hover_footprint: %d\n", app->hover_footprint);
    }

    /* Map editor panel. Gated on the session existing, so binding this key in a
     * manifest with no [editor:boot] is inert rather than a panel with nothing
     * behind it. */
    if( app->editor && !app_text_input_focused(app) &&
        app_debug_key_down(app, input, APP_DEBUG_HOTKEY_MAP_EDITOR) )
    {
        Editor_PanelSetVisible(
            &app->editor_panel, &app->dbg_ui, !app->editor_panel.visible);
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
app_settings_colour_commit(struct App* app, uint32_t rgb)
{
    assert(app);
    if( app->settings_colour_req.varp_id < 0 )
        return;
    RS_CS2Host_ScriptWriteVarp(
        &app->host, app->settings_colour_req.varp_id, (int)(rgb & 0xFFFFFFu) + 1);
    app->need_redraw = 1;
}

/** Put the picker beside the swatch that opened it, clamped onto the canvas. */
static void
app_settings_colour_place(struct App* app, int component_id)
{
    int const scale = ToriRSChrome_Scale(&app->dbg_ui);
    int const width = 230 * scale;
    int x = (UITREE_LAYOUT_ROOT_W - width) / 2;
    int y = UITREE_LAYOUT_ROOT_H / 4;
    int32_t idx;

    assert(app);
    idx = app->tree && component_id >= 0 ? UITree_FindByComponentId(app->tree, component_id) : -1;
    if( idx >= 0 )
    {
        struct UITreeComponent const* c = &app->tree->components[idx];
        /* LEFT of the swatch: a colour row's swatch is docked on the panel's
         * right edge, and a picker that covered it would hide the one thing
         * the player is watching change. */
        x = c->position.abs_x - width - 8 * scale;
        y = c->position.abs_y - 8 * scale;
    }

    if( x < 0 )
        x = 0;
    if( y < 0 )
        y = 0;
    if( x + width > UITREE_LAYOUT_ROOT_W )
        x = UITREE_LAYOUT_ROOT_W - width;
    /* The axis popup drops BELOW the row, so the panel needs room under it as
     * well as for itself; two thirds down is as low as it may sit. */
    if( y > UITREE_LAYOUT_ROOT_H * 2 / 3 )
        y = UITREE_LAYOUT_ROOT_H * 2 / 3;

    ToriRSChrome_PanelSetFixedWidth(&app->dbg_ui, app->settings_colour_panel, width);
    ToriRSChrome_PanelMove(&app->dbg_ui, app->settings_colour_panel, x, y);
}

static void
app_settings_colour_open(struct App* app, struct RS_CS2SettingsColourRequest const* req)
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
        fprintf(
            stderr,
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
app_settings_colour_tick(struct App* app, struct LibToriRS_Input* input)
{
    struct RS_CS2SettingsColourRequest req;
    int activated;

    assert(app);
    (void)input;

    if( RS_CS2Host_TakeSettingsColourRequest(&app->host, &req) )
        app_settings_colour_open(app, &req);

    if( !app->settings_colour_visible )
        return;

    /*
     * The panel's own Close button hid it; the flag above is this side's idea
     * of whether the picker is up, and left unreconciled the next click on the
     * same swatch would "reopen" something that is already open.
     */
    if( app->settings_colour_panel >= 0 &&
        !app->dbg_ui.panels[app->settings_colour_panel].visible )
    {
        app->settings_colour_visible = 0;
        return;
    }

    /*
     * Follow the row out of existence.
     *
     * All Settings is built and torn down by clientscripts, so closing the
     * panel -- or switching to another category, which rebuilds every row --
     * takes the swatch with it. A picker still floating over the game after
     * its row is gone has nothing to point at and no way to be dismissed
     * except its own button.
     */
    if( app->settings_colour_req.component_id >= 0 && app->tree &&
        UITree_FindByComponentId(app->tree, app->settings_colour_req.component_id) < 0 )
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
        int const hsl =
            ToriRSChrome_Hsl16NearestRgb((uint32_t)app->settings_colour_req.default_colour);
        (void)ToriRSChrome_TakeActivated(&app->dbg_ui);
        ToriRSChrome_ColorPickSet(&app->dbg_ui, app->settings_colour_pick, hsl);
        app_settings_colour_commit(app, ToriRSChrome_Hsl16ToRgb(hsl));
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

void
App_SetWorldRenderMode(
    struct App* app,
    enum ToriRS_WorldRenderMode mode)
{
    if( app )
        app->world_render_mode = mode;
}

/* Defined with the other map-editor helpers below; App_Init registers it as
 * the editor session's shared-state sink. */
static void
app_editor_on_state(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count);

void
App_Init(
    struct App* app,
    struct AppConfig const* cfg)
{
    assert(app);
    assert(cfg);
    memset(app, 0, sizeof(*app));
    app->cfg = *cfg;

    /* Not zero: the reference boots both Attack options at Hidden and only
     * ever leaves that state when varp clientcode 18/22 arrives (see
     * rs_attack_option.h). Zeroing them here would left-click-attack against a
     * server that never sends the setting. */
    app->player_attack_option = RS_ATTACK_OPTION_DEFAULT;
    app->npc_attack_option = RS_ATTACK_OPTION_DEFAULT;

    ToriDraw_Init();

    /* Phase 1: task runtime + disk. The runner owns the async pipeline every
     * other phase loads through. */
    app->runner.io = ToriRS_IO_New();
    app->runner.queue = ToriRS_TaskQueue_New();
    app->runner.px = PlatformX_IO_New();
    assert(app->runner.px != NULL);

    /* Serial game-action pipeline: own queue + io slots, SHARED platform
     * pump (there is exactly one IO backend). */
    app->exec_runner.io = ToriRS_IO_New();
    app->exec_runner.queue = ToriRS_TaskQueue_New();
    app->exec_runner.px = app->runner.px;

#if defined(TORIRS_WEB_CACHE_IDB)
    /*
     * The browser has no cache *directory*, but on this lane it does have a
     * cache: a keyed record store the page hydrated from IndexedDB, wearing a
     * dat2 face (see platform/dat2_web_store.h). So the disk opens normally and
     * everything below this point — table id resolution, the map XTEA gate,
     * reference tables, the archive decode path — is the code the desktop build
     * runs, against the same struct.
     *
     * The store was opened before main() by the JS5 metadata barrier, which had
     * to run first: this constructor decodes reference tables, and it is not a
     * tolerant reader.
     */
    {
        struct Dat2WebStore* store = WebCacheBoot_Store();
        struct RSCache_Dat2Store ops;

        if( !store )
        {
            fprintf(
                stderr,
                "app: no browser record store for %s — the JS5 prime did not run\n",
                cfg->cache_dir ? cfg->cache_dir : "(unnamed cache)");
        }
        assert(store != NULL);
        ops = Dat2WebStore_Ops(store);
        app->dat2_disk = RSCache_Dat2DiskNewFromStore(cfg->cache_dir, &ops);
        assert(app->dat2_disk != NULL);
        PlatformX_IO_InitDat2Disk(app->runner.px, app->dat2_disk);
    }
#elif defined(TORIRS_PLATFORM_WEB)
    /* The browser has no cache directory to open. Every read the disk layer
     * would have answered goes to the IO server instead, which holds the real
     * cache and therefore also the things only an open cache can answer:
     * logical-table resolution, the map XTEA gate, and the dat1 versionlist
     * map_index. Leaving both disk handles NULL is what keeps that honest —
     * anything here that tried to read locally would fault rather than quietly
     * answer from an empty cache. */
    (void)cfg->cache_dir;
#else
    if( cfg->cache_kind == APP_CACHE_DAT1 )
    {
        app->dat1_disk = RSCache_Dat1DiskNewFromDirectory(cfg->cache_dir);
        if( !app->dat1_disk )
            fprintf(
                stderr,
                "app: no dat1 cache at %s (expected main_file_cache.dat; pass --dat2 for a "
                "js5 cache)\n",
                cfg->cache_dir);
        assert(app->dat1_disk != NULL);
        PlatformX_IO_InitDat1Disk(app->runner.px, app->dat1_disk);
        /* No xtea step: dat1 archives are not encrypted. */
    }
    else
    {
        app->dat2_disk = RSCache_Dat2DiskNewFromDirectory(cfg->cache_dir);
        if( !app->dat2_disk )
            fprintf(
                stderr,
                "app: no dat2 cache at %s (expected main_file_cache.dat2; pass --dat1 for a "
                "317-era cache)\n",
                cfg->cache_dir);
        assert(app->dat2_disk != NULL);
        /* Map archives may be xtea-encrypted (OldSchool below 237; RS2 dat2
         * from 414). Keys load into the rscache global table the disk layer
         * consults on archive fetch — only when the identity gate says so. */
        {
            struct RSCache probe = RSCache_ProfileForIdentity(
                cfg->cache_game, cfg->cache_epoch, cfg->cache_revision, cfg->cache_quirks);
            if( RSCache_MapLocsEncrypted(&probe) )
            {
                char xtea_path[1024];
                snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", cfg->cache_dir);
                if( RSCache_XteaConfigLoadKeys(xtea_path) <= 0 )
                    fprintf(stderr, "app: no xtea keys at %s (world maps may fail)\n", xtea_path);
            }
            else
            {
                printf("app: map archives are unencrypted at this revision\n");
            }
        }
        PlatformX_IO_InitDat2Disk(app->runner.px, app->dat2_disk);
    }
#endif
    /* What the boot manifest said about the cache. A local backend ignores it
     * (it has the disk); a remote one needs it to know what to open. */
    PlatformX_IO_InitCacheId(
        app->runner.px,
        cfg->cache_epoch,
        cfg->cache_game,
        cfg->cache_revision,
        cfg->cache_quirks,
        cfg->cache_dir);
    PlatformX_IO_InitConfigPath(app->runner.px, cfg->config_dir);
    PlatformX_IO_InitScriptPath(app->runner.px, cfg->script_dir);

    /* Phase 2: asset pipeline (provider is a view over the build cache). */
    if( cfg->cache_kind == APP_CACHE_DAT1 )
    {
        app->dat1_bc = dat1_buildcache_new();
        app->provider = dat1_buildcache_as_provider(app->dat1_bc);
    }
    else
    {
        app->dat2_bc = dat2_buildcache_new();
        app->provider = dat2_buildcache_as_provider(app->dat2_bc);
    }
    app_provider_set_cache_profile(app, cfg);
    if( !TorirsModelInstCache_Init(&app->model_inst_cache) )
        assert(0 && "model_inst_cache init");

    /* Phase 3: renderer scene + id bridge (bridge needs scene + provider). */
    /* The scene carries a depth buffer for the models that ask for one
     * (TORIDRAW_MODEL_FLAG_ZBUFFER — see app_npc_wants_zbuffer). It is
     * allocated lazily on the first such model, so a session that never draws
     * one never pays for it. The painter's sort remains what everything else
     * uses: it is what OSRS content was authored against and is right for it.
     * Imported content — models built for a z-buffered client, whose parts
     * genuinely interpenetrate — has no correct face order to find, and this is
     * how those get the answer their geometry assumes. */
    app->scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_DEPTH_16K | TORIDRAW_SCENE_MODEL_ZBUFFER,
        TORIDRAW_SCRATCH_BUFFER_VERYHIGH_16K);
    assert(app->scene);
    UITreeSceneBridge_Init(&app->bridge, app->scene, app->provider);

    /* Phase 4: game state (host needs tree + provider + invs + varps, then
     * the bridge for icon rasterization). */
    app->tree = UITree_New(256);
    assert(app->tree);
    /* Which side of a minimap/compass mask is the window is a property of the
     * era's art, not of the widget — see UITree.mask_keep_opaque. RS2 (634)
     * ships a stencil, OldSchool a corner cover. */
    app->tree->mask_keep_opaque = RSCache_IsRs2Dat2(CacheProvider_Profile(app->provider)) ? 1 : 0;
    InvManager_Init(&app->invs);
    VarPManager_Init(&app->varps);
    VarCManager_Init(&app->varcs);
    LootStore_Init(&app->loot);

    /* Phase 4b: world sim + builder. The World is a pure simulation that
     * references scene elements/assets by integer id; the builder keeps it in
     * sync with the shared scene from cache data. */
    app->world = World_New();
    assert(app->world);
    World_SetScene(app->world, app->scene);
    app->world_builder = WorldBuilder_New(app->world, app->provider, app->scene, &app->varps);
    assert(app->world_builder);
    app->painter_buffer = painter_buffer_new();
    assert(app->painter_buffer);
    /* v1 GameRunescape camera defaults; repositioned on world load complete. */
    /* Both knobs are populated; proj_mode picks. See graphics/projection.h. */
    app->world_camera.proj_mode = TORIDRAW_PROJ_MODE_SCALE;
    app->world_camera.proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT;
    app->world_camera.fov_rpi2048 = TORIDRAW_PROJ_FOV_DEFAULT;
    /* 50 also load-bearing for the raster, not just for what gets drawn: the
     * near plane is what keeps projected coordinates inside the kernels' 16.16
     * edge representation (+/-32,768 px). Lowering it moves the largest models
     * into overflow range -- see the note at the near clip in
     * graphics/projection.u.c before changing it or TORIRS_NEAR_PLANE. */
    app->world_camera.near_plane_z =
        (getenv("TORIRS_NEAR_PLANE") ? atoi(getenv("TORIRS_NEAR_PLANE")) : 50);
    app->world_camera.pitch = 148;
    app->world_camera_pos.z = -800;
    app->orbit_pitch = 128; /* reference orbitCameraPitch default */
    app->orbit_yaw = 0;
    app->world_zoom_pct = APP_WORLD_ZOOM_DEFAULT_PCT;
    app->world_hover_tile_x = -1;
    app->world_hover_tile_z = -1;
    app->world_hover_tile_level = 0;
    /* -2, not -1: -1 is "no tile", a state the refreshers must still be run
     * for once, and seeding them equal to it would skip that first run. */
    app->highlight_last_hover_coord = -2;
    app->highlight_last_local_coord = -2;
    app->highlight_last_dest_coord = -2;
    app->highlight_last_mouseover = -2;
    app->world_map_scene_id = -1;
    app->worldmap_render = RS_WorldMapRender_New();
    app->worldmap_overview_scene_id = 0;
    app->worldmap_overview_area_id = -1;
    app->minimap_flag_x = -1;
    app->minimap_flag_z = -1;
    /*
     * The plugin host.
     *
     * Built here so a plugin's config is readable the moment anything asks,
     * but NOT started: PluginHost_Start runs after the saved settings have
     * been applied, or every plugin would spend its first frames on defaults
     * and then jump when the ini arrived. TORIRS_PLUGINS=0 switches the whole
     * layer off, which is what the headless parity harnesses use to prove a
     * change is theirs and not a plugin's.
     */
    {
        char const* plugins_env = getenv("TORIRS_PLUGINS");
        if( !plugins_env || atoi(plugins_env) != 0 )
        {
            struct ToriRS_PluginEngine engine = app_plugin_engine(app);
            app->plugin_panel = -1;
            app->plugin_panel_built_for = 0;
            app->plugins = PluginHost_New(&engine);
            app->plugin_prefs_path = PluginPrefs_Path();
            PluginLua_Bind(app->plugins);
            PluginRegistry_RegisterAll(app->plugins);
        }
    }
    app->rebuild_zone_x = -1;
    app->rebuild_zone_z = -1;
    app->proj_src_tile_x = -1;
    app->proj_src_tile_z = -1;
    app->proj_src_tile_level = 0;
    /* Element id 0 is valid, so free entity-spotanim slots must be -1. */
    for( size_t i = 0; i < sizeof(app->entity_spotanims) / sizeof(app->entity_spotanims[0]); i++ )
        app->entity_spotanims[i].body_element_id = -1;

    seed_inv_defaults(&app->invs);
    RS_EntitySync_Init(&app->esync);
    RS_Audio_Init(&app->audio);
    ToriRS_AudioQueue_Reset(&app->audio_out);
    app->inv_drag_com_id = -1;
    app->reboot_ticks = 0;
    RS_Social_Init(&app->social);
    /* Reference reset path: idkDesignGender = male, then validateIdkDesign().
     * The kit scan itself waits for the idk configs, so the clientCode tick
     * resolves the parts the first time the preview asks for a rebuild. */
    RS_IdkDesign_Init(&app->idk_design);
    RS_Chat_Init(&app->chat, "Player");
    /* No hardcoded welcome line: the server sends the real "Welcome to
     * RuneScape." MESSAGE_GAME packet on login (reference has no client-side
     * welcome message; its only "Welcome to RuneScape" is the login title). */
    app->chat_source.line_at = app_chat_line_at;
    app->chat_source.user = app;
    RS_CS2Host_Init(&app->host, app->tree, app->provider, &app->invs, &app->varps, &app->varcs);
    app->host.loot = &app->loot;
    app->host.events_override_for_component = app_cs2_events_override_for_component;
    app->host.events_user = app;
    app->host.loc_at_coord = app_cs2_loc_at_coord;
    app->host.coord_in_scene = app_cs2_coord_in_scene;
    app->host.world_user = app;
    /*
     * State the starting gain once, so a backend is never left guessing at a
     * volume the game already has an opinion about -- the mixer's own buses
     * come up wide open, which is not what the client wants said on its behalf.
     *
     * Read from the option store rather than restating a default here: that is
     * what interface 116 shows, what the preferences file is diffed against,
     * and (RS_CS2Host_OptionDefault) what makes a fresh client boot muted. A
     * second opinion in this file is a client that is audibly one thing while
     * its settings panel says another.
     *
     * Setting the master pushes all three buses, each scaled by it, so this is
     * the whole boot state in one call. A saved volume lands a tick later, when
     * the boot task's preferences restore reaches the tick's snapshot.
     */
    RS_Audio_SetMasterVolume(
        &app->audio,
        (RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_MASTER_VOLUME) *
             TORIRS_AUDIO_VOLUME_MAX +
         50) /
            100,
        &app->audio_out);
    /* Publish the boot canvas through the one setter so the host's viewport
     * copy starts out agreeing with the layout root. main.c may already have
     * moved the root (TORIRS_ROOT_SIZE, which must be applied before App_Init
     * because the open path lays out immediately); without this the host kept
     * its own 765x503 and VIEWPORT_GETEFFECTIVESIZE lied for the whole run. */
    App_SetCanvasSize(app, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    /* The skills tab reads levels and xp through STAT / STAT_BASE / STAT_XP,
     * which need somewhere to read them from. */
    RS_CS2Host_SetStats(&app->host, &app->stats);
    /* The friends and ignore panels read every row they draw through the
     * FRIEND_* / IGNORE_* opcodes, and clientscript 681 writes the chat filter
     * modes through CHAT_SETFILTER — the same three ints the IF1 privacy bar
     * cycles, handed over by pointer so there is only ever one copy. */
    RS_CS2Host_SetSocial(
        &app->host, &app->social, app->slots.chat_filter_mode, app->social.node_id);
    RS_CS2Host_SetBridge(&app->host, &app->bridge);
    /* Close the reactive loop: a varp update from the *server* flags a
     * var-transmit re-dispatch on the host, so interfaces react to value changes
     * and not only to unhide. See app_varp_server_update for why script-side
     * writes and varcs stay out of the transmit ring. ChangeFn remorphs
     * multilocs for both optimistic and server value changes. */
    VarPManager_SetChangeCallback(&app->varps, app_varp_change, app);
    VarPManager_SetServerUpdateCallback(&app->varps, app_varp_server_update, app);
    RS_PlayerStats_Init(&app->stats);
    RS_CS1Host_Init(&app->cs1_host, app->tree, app->provider, &app->invs, &app->varps, &app->stats);

    /* Phase 5: frame state. */
    UITree_EmitBufferInit(&app->emit);
    UITree_HostInit(&app->ui_host);
    app->ui_host.user = app;
    app->ui_host.request = app_host_request;
    UIInteraction_Init(&app->interact);
    UIHoverText_Reset(&app->hover_text);
    app_debug_overlay_init(app);
    app->hover_com_id = -1;
    app->clicked_com_id = -1;
    app->need_redraw = 1;

    /* Client-behaviour era. Resolved unconditionally — an offline boot still
     * clicks locs, so it still needs an approach model. Precedence matches the
     * rest of the boot parameters: manifest > env > derived from the cache. */
    {
        char const* era_name = cfg->features_era;
        if( !era_name || !era_name[0] )
            era_name = getenv("TORIRS_FEATURES_ERA");
        app->features = era_name && era_name[0] ? ToriRS_Features_ByName(era_name) : NULL;
        if( era_name && era_name[0] && !app->features )
            fprintf(stderr, "app: unknown features era '%s', deriving from cache\n", era_name);
        /* Audio behaviour is era-dependent too: monophonic effects are a 2004
         * client property, not a general one. */
        if( !app->features )
            app->features =
                ToriRS_Features_ForCache(cfg->cache_game, cfg->cache_epoch, cfg->cache_revision);
        assert(app->features);

        /*
         * Per-item overrides on top of the era.
         *
         * The era getters hand back shared static singletons, so an override
         * has to be written into the app's own copy — otherwise one manifest
         * key would move the table every other boot in the process reads. This
         * is the copy app->features points at from here on; the singleton stays
         * the pristine statement of what the era is.
         */
        app->features_storage = *app->features;
        app->features = &app->features_storage;
        {
            char const* env = getenv("TORIRS_GROUND_CLICK_NEAREST");
            int model = -1;
            if( cfg->features_ground_click_nearest_set )
                model = cfg->features_ground_click_nearest;
            if( env && env[0] )
            {
                int from_env = ToriRS_Features_NearestModelByName(env);
                if( from_env < 0 )
                    fprintf(
                        stderr,
                        "app: TORIRS_GROUND_CLICK_NEAREST must be "
                        "ring3|box10_rect|none, got '%s'\n",
                        env);
                else
                    model = from_env;
            }
            if( model >= 0 )
                app->features_storage.ground_click_nearest_model = model;
        }
        /*
         * The two permissive ground-click extensions. Every era table leaves
         * them off — the client is deob-exact unless a boot asks otherwise —
         * so this is the only place either can be turned on.
         */
        if( cfg->features_ground_click_unbounded )
            app->features_storage.ground_click_nearest_unbounded = 1;
        if( cfg->features_ground_click_offmap )
            app->features_storage.ground_click_offmap_nearest = 1;
        {
            char const* env = getenv("TORIRS_GROUND_CLICK_UNBOUNDED");
            if( env && env[0] )
                app->features_storage.ground_click_nearest_unbounded = env[0] != '0';
            env = getenv("TORIRS_GROUND_CLICK_OFFMAP");
            if( env && env[0] )
                app->features_storage.ground_click_offmap_nearest = env[0] != '0';
        }
        if( cfg->features_painter_draw_distance_set )
            app->features_storage.painter_draw_distance = cfg->features_painter_draw_distance;

        /*
         * The mover model, on top of the era.
         *
         * It needs an override where the other era fields do not, because
         * ToriRS_Features_ForCache decides the era from cache *lineage* and has
         * exactly one table for everything that is not dat2+oldschool -- so a
         * rev-377 or rev-634 boot lands on the lostcity table and inherits its
         * 2004 per-cycle mover. Those lanes are not reproducing the 2004
         * client, and `[features:boot] mover=frame` in their manifests is how
         * they say so. TORIRS_MOVER_MODEL is the same switch for an A/B without
         * editing a manifest.
         */
        {
            int model = -1;

            if( cfg->features_mover_model_set )
                model = cfg->features_mover_model;
            {
                char const* env = getenv("TORIRS_MOVER_MODEL");
                if( env && env[0] )
                {
                    int from_env = ToriRS_Features_MoverModelByName(env);
                    if( from_env < 0 )
                        fprintf(
                            stderr,
                            "app: unknown TORIRS_MOVER_MODEL '%s' "
                            "(cycle|frame)\n",
                            env);
                    else
                        model = from_env;
                }
            }
            if( model >= 0 )
                app->features_storage.mover_model = model;
        }

        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "app: features era=%s ground_click_nearest=%s "
                "unbounded=%d offmap=%d painter_draw_distance=%d\n",
                app->features->name,
                ToriRS_Features_NearestModelName(app->features->ground_click_nearest_model),
                app->features->ground_click_nearest_unbounded,
                app->features->ground_click_offmap_nearest,
                ToriRS_Features_PainterDrawDistance(app->features));
        RS_Audio_SetFeatures(&app->audio, app->features);
        /* And the world sim: the actor mover is era-dependent too (rev-239
         * integrates movement per rendered frame, the 2004 client per 20ms
         * cycle -- enum ToriRS_MoverModel). Pointed at app->features_storage
         * rather than the singleton so a manifest override reaches it, and set
         * after the overrides above for the same reason. */
        World_SetFeatures(app->world, app->features);
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "app: world mover=%s\n",
                ToriRS_Features_MoverModelName(World_MoverModel(app->world)));

        /* Model lighting: era defaults for the two xrsps-vs-Client-TS
         * divergences, then [render:light] overrides, then push the regimes
         * into toridraw (compiled-in actor/scene profiles unless overridden). */
        app->npc_light_uses_type_ambient_contrast =
            app->features->npc_light_uses_type_ambient_contrast;
        app->player_head_light_ambient = app->features->player_head_light_ambient;
        if( cfg->light_npc_type_ambient_contrast_set )
            app->npc_light_uses_type_ambient_contrast = cfg->light_npc_type_ambient_contrast;
        if( cfg->light_player_head_ambient_set )
            app->player_head_light_ambient = cfg->light_player_head_ambient;

        {
            struct ToriDraw_LightProfile actor = *ToriDraw_LightActorProfile();
            struct ToriDraw_LightProfile scene = *ToriDraw_LightSceneProfile();
            int actor_override = 0;
            int scene_override = 0;

            if( cfg->light_actor_ambient_set )
            {
                actor.ambient = cfg->light_actor_ambient;
                actor_override = 1;
            }
            if( cfg->light_actor_attenuation_set )
            {
                actor.attenuation = cfg->light_actor_attenuation;
                actor_override = 1;
            }
            if( cfg->light_actor_set )
            {
                actor.src_x = cfg->light_actor_x;
                actor.src_y = cfg->light_actor_y;
                actor.src_z = cfg->light_actor_z;
                actor_override = 1;
            }
            if( cfg->light_scene_ambient_set )
            {
                scene.ambient = cfg->light_scene_ambient;
                scene_override = 1;
            }
            if( cfg->light_scene_attenuation_set )
            {
                scene.attenuation = cfg->light_scene_attenuation;
                scene_override = 1;
            }
            if( cfg->light_scene_set )
            {
                scene.src_x = cfg->light_scene_x;
                scene.src_y = cfg->light_scene_y;
                scene.src_z = cfg->light_scene_z;
                scene_override = 1;
            }
            if( actor_override || scene_override )
                ToriDraw_LightSetProfiles(
                    actor_override ? &actor : NULL, scene_override ? &scene : NULL);
        }
        app->bridge.npc_light_uses_type_ambient_contrast =
            app->npc_light_uses_type_ambient_contrast;
        app->bridge.player_head_light_ambient = app->player_head_light_ambient;
    }

    /*
     * Phase 5b: the world map editor (opt-in, and mutually exclusive with a
     * GAME server in practice).
     *
     * Constructed before the networking phase below on purpose: an editor boot
     * states no `[net:boot]`, so that phase does not run at all and this is the
     * last thing built. The editor is the second writer of world state — the
     * first being the packet layer that is absent here — and it reaches the
     * world through the same seams that layer would.
     *
     * The session is a client of ToriRSMapEd, and the manifest picks the
     * deployment the same way [net:boot] picks the game server:
     * `server=embed` (default) hosts the server inside this process over
     * content_dir, `server=tcp` dials the torirsmaped daemon — which is what
     * enables the editor even without a content_dir of its own, since the
     * daemon owns the tree in that deployment.
     */
    if( (cfg->editor_content_dir && cfg->editor_content_dir[0])
        || cfg->editor_server == BOOTMANIFEST_EDITOR_SERVER_TCP )
    {
        struct EditorHost editor_host = { NULL, NULL };
        char editor_label[600];
        int editor_ok;

        if( cfg->editor_server == BOOTMANIFEST_EDITOR_SERVER_TCP )
        {
            char const* maped_host =
                cfg->editor_server_host && cfg->editor_server_host[0]
                    ? cfg->editor_server_host
                    : "localhost";
            snprintf(
                editor_label,
                sizeof(editor_label),
                "maped://%s:%d",
                maped_host,
                cfg->editor_server_port > 0 ? cfg->editor_server_port
                                            : TORIRSMAPED_DEFAULT_PORT);
            /* A client with a world is a VIEWER; `client=` joins a Client
             * another connection already started, so several processes can
             * share one selection. */
            editor_ok = Editor_HostOpenMapEdTcp(
                &editor_host,
                maped_host,
                cfg->editor_server_port,
                TORIRSMAPED_ROLE_VIEWER,
                (uint32_t)cfg->editor_client_id);
        }
        else
        {
            snprintf(
                editor_label,
                sizeof(editor_label),
                "%s (embedded ToriRSMapEd)",
                cfg->editor_content_dir);
            editor_ok = Editor_HostOpenMapEdEmbed(
                &editor_host, cfg->editor_content_dir, cfg->editor_repo_root);
        }

        /* An unreachable daemon is a deployment state, not a build defect:
         * boot the client without the editor and say why, rather than booting
         * an editor whose every operation would fail one at a time. */
        if( !editor_ok )
        {
            fprintf(
                stderr,
                "app: cannot reach ToriRSMapEd at %s — the map editor is disabled "
                "this session\n",
                editor_label);
            goto editor_skipped;
        }

        app->editor = malloc(sizeof(*app->editor));
        assert(app->editor);
        Editor_OpenHost(
            app->editor,
            &editor_host,
            editor_label,
            CacheProvider_Profile(app->provider));
        /* The Client id is printed because it is the handle another PROCESS
         * needs to join this session: `torirsmapedctl --client <id>`. */
        fprintf(
            stderr,
            "app: map editor over %s (%s, client %u)\n",
            editor_label,
            app->editor->writable ? "writable" : "read-only, another server holds it",
            Editor_HostMapEdClientId(&app->editor->host));
        /* Open on boot: this manifest asked for an editor, so the panel is the
         * point of the session rather than a debug aid to go find. */
        Editor_PanelInit(&app->editor_panel, &app->dbg_ui);
        /* The selection relay, both halves: the panel publishes its latch
         * through the session, and the Client's state facts land back on the
         * panel — which is how a controller connection follows this viewer's
         * clicks, and how this panel will follow a detached viewer's. */
        app->editor_panel.editor = app->editor;
        Editor_SetStateCallback(app->editor, app_editor_on_state, app);
        Editor_PanelSetVisible(
            &app->editor_panel,
            &app->dbg_ui,
            cfg->editor_panel == BOOTMANIFEST_EDITOR_PANEL_INPROCESS);

        /*
         * panel=tab: the panel lives in a second browser tab instead of in
         * this window, so the in-process rows stay hidden and the page is
         * asked to open it. The world view keeps the whole canvas.
         *
         * Only the *open* happens here. Whether that tab ever attaches is the
         * channel's business (torirs_channel.js), and the renderer never waits
         * on it -- a blocked popup or a closed tab leaves an editor that still
         * edits, just without its chrome, which is why nothing below this is
         * conditional on the tab appearing.
         */
        if( cfg->editor_panel == BOOTMANIFEST_EDITOR_PANEL_TAB )
        {
#if defined(TORIRS_PLATFORM_WEB)
            web_editor_open_panel_tab();
#else
            /* Unreachable: bootmanifest refuses panel=tab on a native build.
             * Kept as a loud stop rather than a silent skip, so a future
             * platform that reaches here has to decide what it means. */
            assert(0 && "panel=tab reached a build with no tabs to open");
#endif
        }
    editor_skipped:;
    }

    /* Phase 6: networking (opt-in). The default RSA key is the rev_245_2 Lost
     * City pair (v0 tori_rs_init); TORIRS_RSA_EXP/MOD override it. */
    if( cfg->connect_target && cfg->connect_target[0] )
    {
        /* RSA key precedence: env > manifest (cfg) > built-in default pair. */
        char const* rsa_e = getenv("TORIRS_RSA_EXP");
        char const* rsa_n = getenv("TORIRS_RSA_MOD");
        if( !rsa_e )
            rsa_e = cfg->rsa_exp;
        if( !rsa_n )
            rsa_n = cfg->rsa_mod;
        if( !rsa_e )
            rsa_e = "81f390b2cf8ca7039ee507975951d5a0b15a87bf8b3f99c966834118c50fd94d";
        if( !rsa_n )
            rsa_n = "88c38748a58228f7261cdc340b5691d7d0975dee0ecdb717609e6bf971eb3fe723ef9d130e468"
                    "6813739768ad9472eb46d8bfcc042c1a5fcb05e931f632eea5d";

        char const* rev_name = cfg->rev_name;
        if( !rev_name || !rev_name[0] )
            rev_name = getenv("TORIRS_REV");
        struct GameProtoRevTable const* rev =
            rev_name && rev_name[0] ? GameProtoRev_ByName(rev_name) : GameProtoRev_LC254();
        if( !rev )
        {
            fprintf(stderr, "app: unknown protocol rev '%s', using lc254\n", rev_name);
            rev = GameProtoRev_LC254();
        }

        /* Manifest login params override the table defaults, but env still wins
         * (the lazy TORIRS_JAG_CRC parse in the rev getter already ran). */
        if( cfg->jag_crc_set && !getenv("TORIRS_JAG_CRC") )
            GameProtoRev_SetJagChecksums(rev, cfg->jag_crc);
        if( cfg->client_version > 0 )
            GameProtoRev_SetClientVersion(rev, cfg->client_version);

        app->net = calloc(1, sizeof(struct ToriRS_Network));
        assert(app->net);
        ToriRS_Network_Init(app->net, rev, rsa_e, rsa_n);
        app->net_enabled = 1;
        /* IF1 button clicks now notify the server (reference IF_BUTTON /
         * RESUME_PAUSEBUTTON). */
        app->button_sink.user = app;
        app->button_sink.if_button = app_send_if_button;
        app->button_sink.resume_pausebutton = app_send_resume_pausebutton;
        app->button_sink.close_modal = app_send_close_modal;
        ToriRS_Network_ConnectLogin(
            app->net,
            cfg->connect_target,
            cfg->connect_user ? cfg->connect_user : "guest",
            cfg->connect_pass ? cfg->connect_pass : "");
    }
}

int
App_UiLogic(struct App const* app)
{
    assert(app);
    if( app->cfg.ui_logic == APP_UI_LOGIC_CS1 || app->cfg.ui_logic == APP_UI_LOGIC_CS2 )
        return app->cfg.ui_logic;
    /* DEFAULT: derive from cache format (bit-identical to the legacy keying). */
    return app->cfg.cache_kind == APP_CACHE_DAT1 ? APP_UI_LOGIC_CS1 : APP_UI_LOGIC_CS2;
}

/*
 * Write out a settings change that has not reached its settle window yet.
 *
 * Quitting is exactly when that happens: the player turns the music down and
 * closes the client, and the tick that would have queued the save never comes.
 *
 * Stepped here against a private IO rather than handed to the App's runner:
 * that queue may hold tasks parked on state a shutting-down client will never
 * produce, so draining it could return with the save still queued. One task
 * against one IO list terminates on both backends — the platform answers a
 * client-file item inline in Process.
 */
static void
app_prefs_flush(struct App* app)
{
    struct ToriRS_Task* task;
    struct ToriRS_IO io;
    int guard = 0;

    if( !app->prefs_path )
        return;
    if( !RS_Prefs_CaptureFromHost(&app->prefs, &app->host) && !app->prefs_dirty_cycle )
        return; /* everything the player chose is already on disk */
    app->prefs_dirty_cycle = 0;

    memset(&io, 0, sizeof(io));
    task = CreateTask_PrefsSave(&app->prefs, app->prefs_path);
    while( task_run(task, &io) == PT_YIELDED && guard++ < 8 )
        PlatformX_IO_Process(app->runner.px, &io);
    task_free(task);
}

void
App_Shutdown(struct App* app)
{
    assert(app);
    app_prefs_flush(app);
    /* Plugins first: EV_STOP handlers may still read world state and the
     * config store, and both are torn down below. */
    PluginHost_Free(app->plugins);
    app->plugins = NULL;
    if( app->editor )
    {
        /* Releases the content-tree lock. Unsaved edits are NOT written here:
         * a save is something the user asks for, and silently flushing on exit
         * would put edits on disk that were abandoned on purpose. */
        if( Editor_DocHasUnsaved(&app->editor->doc) )
            fprintf(stderr, "app: map editor closing with unsaved edits\n");
        Editor_Close(app->editor);
        free(app->editor);
        app->editor = NULL;
    }
    if( app->net )
    {
        ToriRS_Network_Free(app->net);
        free(app->net);
        app->net = NULL;
    }
    UITree_EmitBufferFree(&app->emit);
    RS_WorldMapRender_Free(app->worldmap_render);
    app->worldmap_render = NULL;
    RS_CS2Host_Free(&app->host);
    if( app->painter_buffer )
    {
        free(app->painter_buffer->commands);
        free(app->painter_buffer);
    }
    RS_Audio_Shutdown(&app->audio);
    /* The bed holds borrowed pointers into this table, so it has to outlive the
     * audio layer's teardown. */
    RS_Soundscapes_Free(&app->soundscapes);
    RS_Healthbars_Free(&app->healthbars);
    RS_EntitySync_Free(&app->esync);
    WorldBuilder_Free(app->world_builder);
    World_Free(app->world);
    VarPManager_Free(&app->varps);
    VarCManager_Free(&app->varcs);
    LootStore_Free(&app->loot);
    InvManager_Free(&app->invs);
    UITree_Free(app->tree);
    if( app->builder_active )
        UITreeBuilder_Free(&app->builder);
    UITreeSceneBridge_Free(&app->bridge);
    TorirsModelInstCache_Free(&app->model_inst_cache);
    ToriDraw_SceneFree(app->scene);
    /* Only the pair matching cfg.cache_kind was ever created; both frees assert
     * on NULL, so the unused side must not be handed to them. */
    if( app->dat2_bc )
        dat2_buildcache_free(app->dat2_bc);
    if( app->dat1_bc )
        dat1_buildcache_free(app->dat1_bc);
    PlatformX_IO_Free(app->runner.px);
    if( app->dat2_disk )
        RSCache_Dat2DiskFree(app->dat2_disk);
    if( app->dat1_disk )
        RSCache_Dat1DiskFree(app->dat1_disk);
    ToriRS_TaskQueue_Free(app->exec_runner.queue);
    ToriRS_IO_Free(app->exec_runner.io);
    ToriRS_TaskQueue_Free(app->runner.queue);
    ToriRS_IO_Free(app->runner.io);
    /* After the queues: freeing a task releases its VM back into the pool. */
    CS2VM2_PoolDrain();
    /* Also after the queues. A parked entity-info task borrows the scratch and
     * hands it back from its _Free, so releasing it any earlier would leave that
     * _Free freeing a pointer this call already returned to the allocator. */
    Task_EntityInfoScratchFree(app);
    free(app->if_heads);
    free(app->if_player_models);
    free(app->if_hides);
}

static int
app_font_b12_cache_id(struct App const* app)
{
    return app->cfg.cache_kind == APP_CACHE_DAT1 ? APP_FONT_B12_DAT1_SLOT : APP_FONT_B12_CACHE_ID;
}

/* Scene font for hitsplat numbers; queues the load on a miss the same way
 * app_minimenu_font_scene_id does, and returns -1 until it lands.
 *
 * -1, not 0: scene font ids ARE cache font ids, and dat1 p11 is cache id 0 —
 * the same trap that once left every p11 label invisible. */
static int
app_hitsplat_font_scene_id(struct App* app)
{
    int font_cache_id =
        app->cfg.cache_kind == APP_CACHE_DAT1 ? APP_FONT_P11_DAT1_SLOT : APP_FONT_P11_CACHE_ID;
    int scene_id = UITreeSceneBridge_EnsureFont(&app->bridge, font_cache_id);
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
    int scene_id = UITreeSceneBridge_EnsureFont(&app->bridge, font_cache_id);
    if( scene_id <= 0 )
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

/* World_HeightFn: projectiles/movers track terrain height (world units).
 *
 * Line port of Client-TS getAvH (Client.ts:5288), bridge clause included: the
 * scene push-down moves a bridge column's *geometry* from cache level 1 into
 * paint level 0 (WorldBuilder_RebuildCenterzoneEnd, reference World.pushDown),
 * but the heightmap keeps raw cache levels. So a mover standing on a
 * LinkBelow column has to sample level+1 or it sinks to the underpass floor —
 * the "player walks under the bridge" symptom. */
static int
app_world_height(
    void* userdata,
    int world_x,
    int world_z,
    int level)
{
    struct App* app = (struct App*)userdata;
    int real_level = level;

    if( !app->world || !app->world->heightmap )
        return 0;

    /* getAvH out-of-scene guard (Client.ts:5296): a tile outside [0,scene_size)
     * has no heightmap column, so the reference returns a flat 0 rather than
     * sampling. Without this an entity spawned/projected past the scene edge
     * (e.g. a border NPC at tile 105 in a 104-wide scene) drives an unguarded
     * base-corner read in heightmap_get_interpolated straight off the array. */
    {
        int tile_x = world_x >> 7;
        int tile_z = world_z >> 7;
        int scene_size = app->world->_scene_size;
        if( tile_x < 0 || tile_z < 0 || tile_x >= scene_size || tile_z >= scene_size )
            return 0;
    }

    if( level < WORLD_MAP_TERRAIN_LEVELS - 1 &&
        (World_TileFlagGet(app->world, world_x >> 7, world_z >> 7, 1) &
         RSCACHE_FLOFLAG_LINK_BELOW) != 0 )
        real_level = level + 1;
    return heightmap_get_interpolated(app->world->heightmap, world_x, world_z, real_level);
}

/* World_SeqSource getters: seq timing resolved from the scene animation
 * registry (ToriDraw_Animation carries the seq-config meta). Unloaded ids
 * return the world_cycle defaults, which freeze that track until the lazy
 * seq load lands (app_request_entity_seq). */
static struct ToriDraw_Animation*
app_seq_anim(
    void* userdata,
    int seq_id)
{
    struct App* app = (struct App*)userdata;
    if( !app->scene || seq_id < 0 )
        return NULL;
    return ToriDraw_SceneAnimationGet(app->scene, seq_id);
}

static int
app_seq_frame_count(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim ? anim->frame_count : 0;
}

static int
app_seq_frame_duration(
    void* userdata,
    int seq_id,
    int frame)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    /* Skeletal seqs carry no per-frame lengths — their curves are sampled one
     * tick per client cycle, so every frame is a single cycle long. */
    if( !anim || !anim->frames || frame < 0 || frame >= anim->frame_count )
        return 1;
    return anim->frames[frame].delay > 0 ? anim->frames[frame].delay : 1;
}

static int
app_seq_frame_step(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim ? anim->frame_step : 0;
}

static int
app_seq_max_loops(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim && anim->max_loops > 0 ? anim->max_loops : 99;
}

static int
app_seq_priority(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim ? anim->priority : 5;
}

static int
app_seq_duplicate_behavior(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim ? anim->duplicate_behavior : -1;
}

static int
app_seq_preanim_move(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim ? anim->preanim_move : 0;
}

static int
app_seq_postanim_move(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim ? anim->postanim_move : 0;
}

static int
app_seq_stretches(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = app_seq_anim(userdata, seq_id);
    return anim ? anim->stretches : 0;
}

/* World_SeqSource.spotanim_seq: resolve a spotanim id to its animation seq id so
 * the world can step an entity's attached-graphic frame. -1 when the id is
 * invalid or the spotanimtype is not yet resident (the world then waits). */
static int
app_spotanim_seq(
    void* userdata,
    int spotanim_id)
{
    struct App* app = (struct App*)userdata;
    struct ToriRS_Spotanimtype* spot =
        spotanim_id >= 0 ? CacheProvider_SpotanimtypeGet(app->provider, spotanim_id) : NULL;
    return spot ? spot->seq : -1;
}

/* Plot one loc mapscene Pix8 into the baked minimap ARGB (reference drawDetail
 * + Pix8.plotSprite). The C bake places tile (sx,sz)'s top-left at
 * (sx*4, (height-sz)*4) and a loc extends north (+z) over `loc_l` tiles, so the
 * footprint's top pixel row is (height - sz - (loc_l-1))*4. The sprite is
 * centered in the loc_w x loc_l footprint (reference offsetX/offsetY) and shifted
 * by its own crop origin (Pix8.xof/yof). Transparent (alpha 0) source pixels,
 * i.e. palette index 0, are skipped like the reference plot. */
static void
app_plot_mapscene_sprite(
    uint32_t* dst,
    int pw,
    int ph,
    struct ToriDraw_Sprite const* spr,
    int sx,
    int sz,
    int map_height,
    int loc_w,
    int loc_l)
{
    int base_x = sx * 4 + (loc_w * 4 - spr->width) / 2 + spr->crop_x;
    int base_y = (map_height - sz - (loc_l - 1)) * 4 + (loc_l * 4 - spr->height) / 2 + spr->crop_y;

    for( int y = 0; y < spr->height; y++ )
    {
        int dy = base_y + y;
        uint32_t const* src_row;
        uint32_t* dst_row;
        if( dy < 0 || dy >= ph )
            continue;
        src_row = spr->pixels_argb + (size_t)y * spr->width;
        dst_row = dst + (size_t)dy * pw;
        for( int x = 0; x < spr->width; x++ )
        {
            int dx = base_x + x;
            uint32_t px = src_row[x];
            if( dx < 0 || dx >= pw || (px >> 24) == 0 )
                continue;
            dst_row[dx] = px;
        }
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

        app_plot_mapscene_sprite(
            argb, pw, ph, spr, icon->x, icon->z, world->minimap->height, icon->width, icon->length);
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
        fprintf(
            stderr,
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
    if( local->grid_position.level == app->world_map_level &&
        baked_minimap_seq == app->world->minimap_seq )
        return;
    baked_minimap_seq = app->world->minimap_seq;
    app_rebuild_world_map(app, local->grid_position.level);
    app->need_redraw = 1;
}

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
    int chunks[2] = { 50, 50 };
    struct ToriRS_Task* task;

    /* Same seam as CacheProvider_TrimDerivedCaches inside Task_WorldLoad:
     * previous scene's instance bases are no longer live. */
    TorirsModelInstCache_Clear(&app->model_inst_cache);

    if( !chunks_xz )
    {
        char const* env;

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
            int env_x;
            int env_z;
            if( sscanf(env, "%d,%d", &env_x, &env_z) == 2 )
            {
                chunks[0] = env_x;
                chunks[1] = env_z;
            }
            else
            {
                fprintf(
                    stderr,
                    "TORIRS_WORLD_MAP must be \"x,z\", got '%s' - using %d,%d\n",
                    env,
                    chunks[0],
                    chunks[1]);
            }
        }
        chunks_xz = chunks;
        chunk_pair_count = 1;
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
            Editor_LoadSquare(
                app->editor, app->provider, chunks_xz[i * 2], chunks_xz[i * 2 + 1]);
    }

    task = CreateTask_WorldLoad(
        app->provider,
        app->world_builder,
        chunks_xz,
        chunk_pair_count,
        -1,
        -1,
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
            fprintf(
                stderr,
                "edit: click belongs to the minimenu (visible=%d swallow=%d)\n",
                app->interact.minimenu.visible,
                app->interact.swallow_left_click);
        return;
    }
    if( app->input_frame_consumed )
    {
        if( getenv("TORIRS_EDIT_DEBUG") && input->curr.mouse_button_up[TORIRSM_LEFT] )
            fprintf(stderr, "edit: click swallowed (input_frame_consumed)\n");
        return;
    }
    if( !input->curr.mouse_button_up[TORIRSM_LEFT] )
        return;
    if( getenv("TORIRS_EDIT_DEBUG") )
        fprintf(
            stderr,
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
            &app->editor_panel, app, app->world_hover_tile_x, app->world_hover_tile_z,
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
        app, app->ghost_x, app->ghost_z, app->ghost_level, -1, app->ghost_shape,
        app->ghost_angle);
    /* The slot the ghost sat in belonged to someone: restore them, or the
     * hover reads as a deletion. Scene-only, like the ghost itself -- the
     * document never knew about either. */
    if( app->ghost_displaced_valid )
        App_WorldLocChange(
            app, app->ghost_x, app->ghost_z, app->ghost_level, app->ghost_displaced_loc_id,
            app->ghost_displaced_shape, app->ghost_displaced_angle);
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
        int const hover_ok = app->world_hover_tile_x >= 0 &&
                             !app->interact.minimenu.visible && !app->input_frame_consumed;

        want = 0;
        if( panel->visible && hover_ok && panel->tool == EDITOR_TOOL_LOC_PLACE &&
            panel->cat_picked_id >= 0 &&
            panel->cat_kind == CACHEPROVIDER_CATALOG_LOC )
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

    /* Translucency, once the async add has produced an element. Forcing the
     * ELEMENT's model is safe because world elements own their model copies
     * (Task_WorldLoad and ApplyLocChange both copy) -- a shared instance
     * would ghost every placement of this loc on screen. */
    if( app->ghost_active && !app->ghost_alpha_done && app->world && app->scene )
    {
        int const idx = World_SceneryFindAt(
            app->world, app->ghost_x, app->ghost_z, app->ghost_level, app->ghost_shape);
        if( idx >= 0 )
        {
            struct WorldEntity_Scenery* scenery =
                World_EntityPoolGet(&app->world->entities.scenery, idx);
            struct ToriDraw_SceneElement* element =
                scenery && ToriDraw_SceneElementIsLive(app->scene, scenery->element_id)
                    ? ToriDraw_SceneElementGet(app->scene, scenery->element_id)
                    : NULL;
            /* The handle is a tagged union; only a full model carries faces
             * to fade (a sprite billboard has none). */
            struct ToriDraw_Model* model =
                element && element->model.kind == TORIDRAWMK_MODEL ? element->model.u.model.model
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
app_preview_raster(struct App* app, struct ToriDraw_ModelHandle hnd)
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
                struct ToriRS_Task* task =
                    CreateTask_ObjModelLoad(app->provider, ids, counts, 1);
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
        struct ToriRS_Location* cfg =
            CacheProvider_LocationGet(app->provider, preview_id);
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
app_world_spawn_npc(struct App* app, int tile_x, int tile_z, int level, char const* args);
static void
app_world_spawn_obj(struct App* app, int tile_x, int tile_z, int level, char const* args);

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
    fprintf(stderr, "editor: opening m%d_%d\n", chunks[0], chunks[1]);
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

    count = Editor_DrainRebuilds(
        app->editor, app->provider, squares, EDITOR_REBUILD_QUEUE_MAX);
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
        PluginHost_WorldLoaded(
            app->plugins, app->world->_base_tile_x, app->world->_base_tile_z);
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
            struct World_SeqSource seq_source = {
                .userdata = app,
                .frame_count = app_seq_frame_count,
                .frame_duration = app_seq_frame_duration,
                .frame_step = app_seq_frame_step,
                .max_loops = app_seq_max_loops,
                .priority = app_seq_priority,
                .duplicate_behavior = app_seq_duplicate_behavior,
                .preanim_move = app_seq_preanim_move,
                .postanim_move = app_seq_postanim_move,
                .stretches = app_seq_stretches,
                .spotanim_seq = app_spotanim_seq,
            };
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
            app_rebuild_world_map(app, local ? local->grid_position.level : 0);
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
        fprintf(stderr, "app: world load incomplete\n");
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

    if( !getenv("TORIRS_POS_DEBUG") || !app->world )
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
    const char* env = getenv("TORIRS_HPROF");
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
        fprintf(stderr, "hprof L%d z=%3d:", lvl, z);
        for( int x = x0; x <= x1; x++ )
            fprintf(stderr, " %5d", app_world_height(app, x * 128 + 64, z * 128 + 64, lvl));
        fprintf(stderr, "\n");
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
    const char* env = getenv("TORIRS_TFLAGS");
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
                        fprintf(
                            stderr,
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
        fprintf(stderr, "tflags L%d: vis_below=%d link_below=%d\n", lv, n_vis, n_link);
    }
}

/* TORIRS_TPROJ=x0,x1,z0,z1: project each tile centre to screen, once per load.
 * Turns "which tile is that artifact" from a guess into a lookup. */
static void
app_debug_tile_project(struct App* app)
{
    static int ticks = 0;
    const char* env = getenv("TORIRS_TPROJ");
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
                fprintf(stderr, "tproj tile=%d,%d screen=%d,%d\n", x, z, sx, sy);
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

    if( !getenv("TORIRS_BRIDGE_DEBUG") || !app->world || !app->world->load_complete )
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
            fprintf(
                stderr,
                "bridge: scene=%d,%d abs=%d,%d y0=%d y1=%d\n",
                x,
                z,
                app->world->_base_tile_x + x,
                app->world->_base_tile_z + z,
                heightmap_get_interpolated(app->world->heightmap, x * 128 + 64, z * 128 + 64, 0),
                heightmap_get_interpolated(app->world->heightmap, x * 128 + 64, z * 128 + 64, 1));
        }
    }
    fprintf(stderr, "bridge: %d link-below columns in scene\n", count);
}

/*
 * World projection scale — recomputed per layout, the reference behaviour
 * (docs/ORANGE_WEDGE.md §4/§11/§12, promoted to the default per §11.7).
 *
 * The reference client recomputes the world projection scale from the world
 * viewport HEIGHT on every layout (class159.method5357:
 * scale = viewportHeight * zoom / 334, zoom interpolated between the two
 * VIEWPORT_SETFOV endpoints over height-334 in [0,100]). Leaving it at the
 * compile-time proj_scale = 512 is what drew the Inferno 2.68x magnified —
 * at a 503-high world viewport the reference lands on 191/192.
 *
 * ON by default. TORIRS_WEDGE_SCALE values:
 *   (unset) | 1 | auto        recompute per class159.method5357 (default)
 *   0 | off                   legacy constant scale 512, for A/B comparison
 *   <n>                       force the linear scale to n (n >= 8), for bisection
 *   TORIRS_WEDGE_ZOOM=<n>,<f>   override the decoded SETFOV endpoints (auto mode)
 *   TORIRS_WEDGE_FOV_DEBUG=1    log the SETFOV decode and the resulting scale
 *
 * The scale reaches the kernels exactly, through ToriDraw_Camera.proj_scale.
 *
 * TORIRS_WORLD_FOV=<n> drives the camera's OTHER knob instead: it switches the
 * world camera to TORIDRAW_PROJ_MODE_FOV and sets fov_rpi2048 to n (units of
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
    {
        char const* e = getenv("TORIRS_WORLD_FOV");
        int v;
        cached = -1;
        if( e && e[0] != '\0' && sscanf(e, "%d", &v) == 1 && v > 0 )
        {
            /* Clamped, not rejected: an out-of-domain angle would otherwise
             * mirror the world (see TORIDRAW_PROJ_FOV_MAX). */
            if( v < TORIDRAW_PROJ_FOV_MIN )
                v = TORIDRAW_PROJ_FOV_MIN;
            if( v > TORIDRAW_PROJ_FOV_MAX )
                v = TORIDRAW_PROJ_FOV_MAX;
            cached = v;
        }
    }
    return cached;
}

/* 0 = auto (recompute — the default), -1 = forced off (legacy constant 512),
 * >0 = forced linear scale. */
static int
app_wedge_scale_mode(void)
{
    static int cached = -2;
    if( cached == -2 )
    {
        char const* e = getenv("TORIRS_WEDGE_SCALE");
        cached = 0;
        if( e && e[0] != '\0' )
        {
            if( strcmp(e, "0") == 0 || strcmp(e, "off") == 0 )
                cached = -1;
            else if( strcmp(e, "1") == 0 || strcmp(e, "auto") == 0 )
                cached = 0;
            else
            {
                int v = atoi(e);
                cached = v >= 8 ? v : 0;
            }
        }
    }
    return cached;
}

static void
app_apply_wedge_scale(struct App* app)
{
    int mode = app_wedge_scale_mode();
    int fov_override = app_world_fov_override();
    int vp_h;
    int near_zoom;
    int far_zoom;
    int d;
    int zoom;
    int scale;

    if( fov_override > 0 )
    {
        app->world_camera.proj_mode = TORIDRAW_PROJ_MODE_FOV;
        app->world_camera.fov_rpi2048 = fov_override;
        if( getenv("TORIRS_WEDGE_FOV_DEBUG") )
        {
            static int logged = 0;
            if( !logged )
            {
                logged = 1;
                fprintf(
                    stderr,
                    "wedge: fov mode fov_rpi2048=%d -> realised scale %d\n",
                    fov_override,
                    toridraw_proj_scale_from_cot16(toridraw_proj_cot16_from_fov(fov_override)));
            }
        }
        return;
    }
    if( mode < 0 )
        return;
    if( !app->world_view_valid )
        return;

    vp_h = app->world_emit_desc.h;
    if( vp_h < 1 )
        return;

    if( mode > 0 )
    {
        scale = mode;
        zoom = 0;
        near_zoom = 0;
        far_zoom = 0;
    }
    else
    {
        near_zoom = app->host.viewport_zoom_near;
        far_zoom = app->host.viewport_zoom_far;
        {
            char const* z = getenv("TORIRS_WEDGE_ZOOM");
            int zn, zf;
            if( z && sscanf(z, "%d,%d", &zn, &zf) == 2 )
            {
                near_zoom = zn;
                far_zoom = zf;
            }
        }
        if( near_zoom < 1 )
            near_zoom = 256;
        if( far_zoom < 1 )
            far_zoom = 256;

        d = vp_h - 334;
        if( d < 0 )
            zoom = near_zoom;
        else if( d >= 100 )
            zoom = far_zoom;
        else
            zoom = (far_zoom - near_zoom) * d / 100 + near_zoom;

        scale = (int)((double)vp_h * (double)zoom / 334.0);
    }
    if( scale < 1 )
        scale = 1;

    /* Exact: the kernels multiply by proj_scale directly. */
    app->world_camera.proj_mode = TORIDRAW_PROJ_MODE_SCALE;
    app->world_camera.proj_scale = scale;

    if( getenv("TORIRS_WEDGE_FOV_DEBUG") )
    {
        static int last = -1;
        if( scale != last )
        {
            last = scale;
            fprintf(
                stderr,
                "wedge: scale mode=%d vp_h=%d zoom(near=%d far=%d)=%d -> scale=%d "
                "realised=%d\n",
                mode,
                vp_h,
                near_zoom,
                far_zoom,
                zoom,
                scale,
                toridraw_proj_scale_from_cot16(toridraw_proj_cot16(
                    app->world_camera.proj_mode,
                    app->world_camera.proj_scale,
                    app->world_camera.fov_rpi2048)));
        }
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
    if( getenv("TORIRS_WORLD_VIEW_DEBUG") )
    {
        int kinds[24] = { 0 };
        for( int i = 0; i < app->emit.count; i++ )
            if( app->emit.cmds[i].kind >= 0 && app->emit.cmds[i].kind < 24 )
                kinds[app->emit.cmds[i].kind]++;
        fprintf(
            stderr,
            "worldview: node_index=%d emit_count=%d kinds:",
            App_WorldNodeIndex(app),
            app->emit.count);
        for( int k = 0; k < 24; k++ )
            if( kinds[k] )
                fprintf(stderr, " %d:%d", k, kinds[k]);
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
        fprintf(stderr, " WORLDRECT=%d,%d %dx%d idx=%d\n", wx, wy, ww, wh, widx);
        /* Anything drawn after the world that covers a meaningful slice of it. */
        for( int i = widx + 1; i < app->emit.count; i++ )
        {
            struct UITreeEmitDesc* d = &app->emit.cmds[i];
            int ox = d->x > wx ? d->x : wx, oy = d->y > wy ? d->y : wy;
            int ex = (d->x + d->w) < (wx + ww) ? (d->x + d->w) : (wx + ww);
            int ey = (d->y + d->h) < (wy + wh) ? (d->y + d->h) : (wy + wh);
            int area = (ex - ox) > 0 && (ey - oy) > 0 ? (ex - ox) * (ey - oy) : 0;
            if( area > (ww * wh) / 20 )
                fprintf(
                    stderr,
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

/* The async boot protothread: builds the root tree (RevConfig or cache
 * interface open), awaits the overlay font, then runs the synchronous
 * seeding steps and flips the app READY. All IO flows through the platform
 * pump via the per-frame task stepping — nothing here blocks the frame loop. */
struct Task_AppBoot
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
};

static int
Task_AppBoot_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppBoot* self = (struct Task_AppBoot*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    app->boot_progress = 10;

    /*
     * Varbit types, before anything that can run a script.
     *
     * A varbit read happens deep inside CS2 execution with nowhere to yield to a
     * load, so the table has to be resident first. Without it every varbit reads 0
     * and any script branching on one silently takes the zero path.
     *
     * Both eras, different storage: dat2 splits config group 14 into one file per id,
     * dat1 packs them into a single `varbit.dat` inside the config jagfile. CS1 reads
     * varbits through the same VarPManager as CS2, so both need it.
     */
    /*
     * Varplayer types first, and only on dat2.
     *
     * The client reads one field off a varplayer record — `clientcode`, the
     * marker that a varp drives behaviour baked into the client rather than a
     * script (sound volume; the Controls panel's two Attack options). Nothing
     * loaded this table before, so on an OldSchool cache every clientcode read
     * 0 and none of that behaviour could fire.
     *
     * Before the varbits because SetVarpTypes reallocates the var value arrays
     * and SetVarbitTypes does not: the other order works only for as long as
     * nothing has written a varp in between.
     *
     * dat1 keeps varplayers in `varp.dat` inside the config jagfile; that path
     * has no loader and the classic revisions this tree boots have no
     * clientcode-driven settings, so it stays untyped there.
     */
    /*
     * Everything from here to `boot_config_ready = 1` is ONCE PER SESSION, not
     * once per boot — and this task boots again on every root remount (the
     * Display panel's Fixed/Classic/Modern switch, App_OpenRootInterface).
     *
     * `VarPManager_SetVarpTypes` reallocates the varp *value* arrays, so a
     * second pass over the loads below calloc-zeroes every varp the server has
     * sent this session. Measured on a layout switch: varp 1737 went
     * -2147483648 -> 0 across the remount, which clears varbit 8119
     * (`has_displayname_transmitter`), and the chatbox's own onload then paints
     * "You must set a name before you can chat." over the input line. The name
     * bit is only the visible one; run/attack-style/prayer/bank/settings varps
     * go with it, and the server transmits a varp on WRITE, so nothing puts
     * them back until content happens to write each one again.
     *
     * The seeds further down are the same shape: they are what the client
     * believes before a server speaks, so re-running them mid-session would
     * overwrite what the server said with a boot-time default.
     */
    if( !app->boot_config_ready && app->cfg.cache_kind != APP_CACHE_DAT1 )
        PT_TASK_AWAITSELF_IF(CreateTask_Dat2VarpLoad(app->provider, &app->varps));

    /*
     * The clientscript index's reference table.
     *
     * Loaded once, for its group IDENTIFIERS: the cache binds a script to
     * "this npc appeared" / "this loc was placed" through the group NAME and
     * nothing else, so without this table 218 loc scripts and 23 npc scripts
     * in cache.osrs239 are unreachable. See game/rs_client_trigger.h.
     */
    if( !app->boot_config_ready )
        PT_TASK_AWAITSELF_IF(CreateTask_ClientScriptTableLoad(app->provider));

    if( !app->boot_config_ready && app->cfg.cache_kind == APP_CACHE_DAT1 )
        PT_TASK_AWAITSELF_IF(CreateTask_Dat1VarbitLoad(app->provider, &app->varps));
    else if( !app->boot_config_ready )
        PT_TASK_AWAITSELF_IF(CreateTask_Dat2VarbitLoad(app->provider, &app->varps));

    /* Hitsplat types. dat1 has no such config group — it keeps the splat
     * graphics in a "hitmarks" sprite archive, which the static-sprite path
     * binds — so this is a dat2-only load and an absent group is not an error. */
    if( !app->boot_config_ready && app->cfg.cache_kind != APP_CACHE_DAT1 )
        PT_TASK_AWAITSELF_IF(CreateTask_Dat2HitsplatLoad(app->provider, &app->hitsplats));

    /* Healthbar types. dat2 only for the same reason as hitsplats -- dat1 has
     * no such config group, and the table's defaults are the reference's own
     * constructor, so an absent group draws exactly what the hardcoded 30-wide
     * bar used to. */
    if( !app->boot_config_ready && app->cfg.cache_kind != APP_CACHE_DAT1 )
        PT_TASK_AWAITSELF_IF(CreateTask_Dat2HealthbarLoad(app->provider, &app->healthbars));

    /*
     * Ambient soundscapes. dat2 only, and OldSchool 231+ within that -- an
     * absent group leaves the table empty, which the audio layer reads as the
     * pre-231 meaning of AMBIENTSOUND_START (the id is a sound effect). Binding
     * the table unconditionally is deliberate: the empty case is a supported
     * reading, not a failure to load.
     */
    if( !app->boot_config_ready && app->cfg.cache_kind != APP_CACHE_DAT1 )
        PT_TASK_AWAITSELF_IF(CreateTask_Dat2SoundscapeLoad(app->provider, &app->soundscapes));
    RS_Audio_SetSoundscapes(&app->audio, &app->soundscapes);

    /*
     * The settings the player chose last launch, before anything reads one.
     *
     * Volumes are device settings: no packet carries them to a server and none
     * carries them back, so the file this reads is the only thing standing
     * between "turn the music down" and a client that is loud again tomorrow.
     * Restoring into the option store (not the varps) is the right direction —
     * the store is what GAMEOPTION_GET answers with, and the four varps below
     * are then seeded from it, so both halves of interface 116 agree.
     */
    if( !app->boot_config_ready )
    {
        app->prefs_path = RS_Prefs_Path();
        RS_Prefs_Defaults(&app->prefs);
    }
    if( !app->boot_config_ready && app->prefs_path )
        PT_TASK_AWAITSELF_IF(CreateTask_PrefsLoad(&app->prefs, app->prefs_path));
    if( !app->boot_config_ready )
        RS_Prefs_ApplyToHost(&app->prefs, &app->host);

    /*
     * Plugins: read the script manifest, compile each script, apply saved
     * settings, then start everything. Awaited here, on the boot path, for the
     * same reason the settings above are -- a plugin that starts after the
     * first frame has already missed events, and one that starts before its
     * saved config arrives runs a frame on defaults and then jumps.
     *
     * Everything it touches goes through the IO queue, so this is the same
     * code on the native lanes and in the browser, where a synchronous read
     * does not exist.
     */
    if( !app->boot_config_ready && app->plugins )
        PT_TASK_AWAITSELF_IF(
            CreateTask_PluginBoot(app->plugins, PluginManifest_Path(), PluginPrefs_Path()));

    /*
     * A window mode the manifest or command line stated wins over the saved
     * one. App_SetBootWindowMode has already run (it must, before the root's
     * scripts call getwindowmode), so the restore above just overwrote it;
     * `cfg.window_mode` is 0 when nothing said anything, which is when the
     * saved default is the only opinion there is. Same precedence as a server
     * VARP over the seeded volumes: an explicit instruction for this run beats
     * what the last run happened to leave behind.
     */
    if( !app->boot_config_ready && app->cfg.window_mode )
        app->host.default_window_mode = app->cfg.window_mode;

    /*
     * Seed the four audio volumes.
     *
     * The option store holds the real volumes -- the restore above, or the
     * defaults -- but the varps those sliders actually read are whatever
     * SetVarpTypes left behind, which is zero. Interface 116 believes the
     * varps: script 7101 greys every bobble while %var3796 <= 0 and script 9254
     * shows the mute cross on all four icons, so a client playing at the
     * restored volume would come up looking muted, and the first click on
     * "Mute" would read the zero, take script 9255's unmute branch and turn the
     * volume *up*.
     *
     * A client that boots muted (RS_CS2Host_OptionDefault) is not that bug: the
     * master option really is zero, so the cross the panel draws is the truth
     * and the first click on it is a genuine unmute.
     *
     * These are ordinary player varps, so a server that sends VARP_SMALL/LARGE
     * for them overwrites this — which is the right precedence. It matters only
     * where nothing does, which is every standalone boot and ToriRSServer today.
     *
     * Optimistic (not the server setter) so the ChangeFn runs and the host
     * snapshot stays in agreement with the varps; and before the tree is built,
     * so 116 constructs its bobbles green rather than needing a repaint.
     */
    if( !app->boot_config_ready )
    {
        VarPManager_SetVarpOptimistic(
            &app->varps,
            RS_CS2_VARP_MASTER_VOLUME,
            RS_CS2Host_GetOption(
                &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_MASTER_VOLUME));
        VarPManager_SetVarpOptimistic(
            &app->varps,
            RS_CS2_VARP_MUSIC_VOLUME,
            RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_GAME, RS_CS2_GAMEOPTION_MUSIC_VOLUME));
        VarPManager_SetVarpOptimistic(
            &app->varps,
            RS_CS2_VARP_SOUND_VOLUME,
            RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_GAME, RS_CS2_GAMEOPTION_SOUND_VOLUME));
        VarPManager_SetVarpOptimistic(
            &app->varps,
            RS_CS2_VARP_AREA_VOLUME,
            RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_GAME, RS_CS2_GAMEOPTION_AREA_VOLUME));
    }

    /* Baseline for the change detection below: what is in the host now is what
     * is on disk, so nothing written here counts as a change worth saving. */
    if( !app->boot_config_ready )
    {
        RS_Prefs_CaptureFromHost(&app->prefs, &app->host);
        app->prefs_dirty_cycle = 0;
    }
    if( getenv("TORIRS_AUDIO_TRACE") || getenv("TORIRS_AUDIO_DEBUG") )
        fprintf(
            stderr,
            "audio: seeded volume varps master(%d)=%d music(%d)=%d "
            "sfx(%d)=%d area(%d)=%d\n",
            RS_CS2_VARP_MASTER_VOLUME,
            VarPManager_GetVarp(&app->varps, RS_CS2_VARP_MASTER_VOLUME),
            RS_CS2_VARP_MUSIC_VOLUME,
            VarPManager_GetVarp(&app->varps, RS_CS2_VARP_MUSIC_VOLUME),
            RS_CS2_VARP_SOUND_VOLUME,
            VarPManager_GetVarp(&app->varps, RS_CS2_VARP_SOUND_VOLUME),
            RS_CS2_VARP_AREA_VOLUME,
            VarPManager_GetVarp(&app->varps, RS_CS2_VARP_AREA_VOLUME));

    /*
     * Seed the "interface resizing" setting, for the same reason and with the
     * same precedence as the four volumes above: it is a client setting nobody
     * transmits, and the zero SetVarbitTypes leaves behind is a value, not an
     * absence.
     *
     * Which era owns the id — and the whole account of what the two branches do
     * — is in features.h. In short: at zero the cache's interface-window helper
     * (clientscript 1898, and 1904 for the skill guide) positions a modal's
     * panel at `if_getx/if_gety(mainmodal)`, the slot's *parent-relative*
     * origin, inside the modal's own root. In resizable mode the slot is
     * centred in `hud_container_front`, so that applies the centring offset
     * twice and every main modal sits down-and-right of the hole clientscript
     * 910 dims for it, half-under the sidebar and the chatbox.
     *
     * Optimistic, before the tree is built, and overridable by a server VARP —
     * all three for the reasons stated above the volumes.
     */
    if( !app->boot_config_ready && app->features->varbit_interface_resizing > 0 )
        VarPManager_SetVarbitOptimistic(&app->varps, app->features->varbit_interface_resizing, 1);

    /* End of the once-per-session half. A remount from here down rebuilds the
     * tree against the var state the session already has. */
    app->boot_config_ready = 1;

    /* `[ui:varc]` — the var writes that accompany the login IF_OPENSUB burst.
     * Before the tree opens, because the root's onLoad scripts branch on them.
     * Skipped when networked, for the same reason [ui:gameframe] is. */
    if( app->cfg.varc_seed_count > 0 && !app->net_enabled )
    {
        for( int i = 0; i < app->cfg.varc_seed_count; i++ )
            VarCManager_SetInt(
                &app->varcs, app->cfg.varc_seeds[i].id, app->cfg.varc_seeds[i].value);
        printf("varc: seeded %d client vars from the manifest\n", app->cfg.varc_seed_count);
    }

    /* One root-build path. A manifest that names no RevConfig at all still comes
     * through here: the builder synthesises the single rs_iface mount of
     * boot_interface_id, which is what the old open-the-interface-directly
     * branch produced (TS parity: WidgetManager.setRootInterface(groupId) — any
     * group can be the root, no hardcoded 161 chrome required). */
    PT_TASK_AWAITSELF(CreateTask_UITreeBuild(&app->builder));
    printf(
        "RevConfigBuild done: iface=%d ui=%s inline=%s tree_components=%u sprites=%d "
        "fonts=%d onloads=%d inv_hooks=%d var_hooks=%d mounts=%d\n",
        app->boot_interface_id,
        app->cfg.revconfig_ui_ini ? app->cfg.revconfig_ui_ini : "-",
        app->cfg.revconfig_inline_ini ? app->cfg.revconfig_inline_ini : "-",
        app->tree->component_count,
        app->builder.sprite_count,
        app->builder.font_count,
        app->builder.onload_count,
        app->host.inv_transmit_hook_count,
        app->host.var_transmit_hook_count,
        app->tree->interface_parent_count);

    app->boot_progress = 60;

    /* Shared b12 fallback before configured overlay models are bound. */
    PT_TASK_AWAITSELF_IF(CreateTask_FontLoad(app->provider, app_font_b12_cache_id(app)));

    app->boot_progress = 75;

    if( getenv("TORIRS_ANIM_DEBUG") )
    {
        for( uint32_t i = 0; i < app->tree->component_count; i++ )
        {
            struct UITreeComponent const* node = &app->tree->components[i];
            if( node->freed || node->type != UIELEM_RS_MODEL )
                continue;
            fprintf(
                stderr,
                "anim_debug: com=0x%x model=%d seq=%d\n",
                node->component_id,
                node->u.rs_model.gamecache_model_id,
                node->u.rs_model.anim_seq_id);
        }
    }

    app_bind_configured_overlays(app);

    /*
     * `[ui:gameframe]` — the login IF_OPENSUB burst, from the manifest.
     *
     * A gameframe root is a frame of empty slots; every panel in it (chat box,
     * orbs, the sidebar tabs, the inventory) arrives as a separate IF_OPENSUB
     * once the server has the player. Offline there is no server, so the frame
     * renders as chrome around nothing. Listing the mounts in the manifest
     * reproduces the burst without inventing a client-side default: the values
     * are whatever the era's server actually sends (for rev 634 they are read
     * off Void's `openGamframe` — see docs/RS2_634_CLIENT_REFERENCES.md).
     *
     * Skipped when networked: the server sends the real sequence, and mounting
     * on top of it would fight whatever it opens.
     */
    if( app->cfg.gameframe_mount_count > 0 && !app->net_enabled )
    {
        for( int i = 0; i < app->cfg.gameframe_mount_count; i++ )
        {
            struct BootManifestGameframeMount const* mount = &app->cfg.gameframe_mounts[i];
            int parent = mount->parent_interface_id > 0 ? mount->parent_interface_id
                                                        : app->boot_interface_id;
            App_OpenSubInterface(
                app, (parent << 16) | (mount->component & 0xFFFF), mount->interface_id, 0);
        }
        printf(
            "gameframe: queued %d sub-interface mounts under iface %d\n",
            app->cfg.gameframe_mount_count,
            app->boot_interface_id);
    }

    /* Tab/interface-slot state seeds from the baked tree (INI componentno= and
     * selected= drive it; nothing here is hardcoded). */
    RS_UISlots_InitFromTree(&app->slots, app->tree);

    /* Queue model-widget sequences (they land through the frame pump and
     * render at rest pose meanwhile) and apply whatever is already loaded. */
    UITreeAnim_RequestMissing(
        app->tree, app->scene, app->provider, app->runner.queue, &app->seq_loads);
    UITreeAnim_Advance(app->tree, app->scene, 0);

    app_sync_textures(app);

    /* No viewport component in the opened interface -> no map at all. Trees
     * that grow one later (a mounted interface) load lazily in App_RunOnce.
     * When networked, the server's REBUILD_NORMAL is the sole world-load driver
     * (its region + scene base match the entities); a default region 50,50 load
     * here would race it and clobber the rebuilt scene, so skip it. */
    if( App_WorldNodeIndex(app) >= 0 && !app->net_enabled )
        app_world_load_begin(app, NULL, 0);

    app_chat_build_view(app);
    app->emit.count = 0;
    UITree_EmitWalk(app->tree, &app->ui_host, &app->emit, -1);
    /* Prime the viewport cache so the first App_Render can draw the world
     * without waiting for a App_RunOnce pass to latch it. */
    app_update_world_viewport(app);

    app->boot_progress = 100;
    app->app_state = APP_STATE_READY;
    app->pending_tree_refresh = 1;
    app->need_redraw = 1;

    PT_END(&self->pt);
}

static void
Task_AppBoot_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_AppBoot_VTable = {
    .run = Task_AppBoot_Run,
    .free = Task_AppBoot_Free,
};

void
App_OpenRootInterface(
    struct App* app,
    int interface_id)
{
    struct Task_AppBoot* task;

    assert(app);
    memset(&app->open_stats, 0, sizeof(app->open_stats));

    if( getenv("TORIRS_CS2_TRACE") )
        g_cs2_trace_mode = 2;

    /* Display Fixed/Classic/Modern remount: drop the live gameframe before the
     * new root bakes. Without this, InterfaceOpen would add 548/164 beside the
     * old 161 forest, and IF_OPENSUB targets for the new top race the boot. */
    if( app->tree && app->tree->root_index >= 0 )
    {
        UITree_Clear(app->tree);
        app->host.inv_transmit_hook_count = 0;
        app->host.var_transmit_hook_count = 0;
        app->host.stat_transmit_hook_count = 0;
    }

    /* RevConfig build, always: the config names the whole gameframe (chrome
     * widgets plus the cache interface packs mounted under them), and a manifest
     * that declares none of it still gets the plain `interface_id` mount
     * synthesised for it. That is what keeps declared sibling order meaningful —
     * a pack is baked *under* the rs_iface node its layout record placed, so
     * whatever the CS2 scripts then do to it stays inside that subtree.
     *
     * The CS2 host is passed only for dat2 — dat1 interface packs carry IF1
     * scripts, which the CS1 host evaluates on the tick instead. */
    if( app->builder_active )
        UITreeBuilder_Free(&app->builder); /* display-mode remount: re-Init below */
    UITreeBuilder_InitEx(
        &app->builder,
        app->provider,
        app->tree,
        &app->invs,
        App_UiLogic(app) == APP_UI_LOGIC_CS1 ? NULL : &app->host,
        app->cfg.revconfig_ui_ini,
        app->cfg.revconfig_cache_ini);
    if( app->cfg.revconfig_inline_ini )
        strncpy(
            app->builder.inline_ini_path,
            app->cfg.revconfig_inline_ini,
            sizeof(app->builder.inline_ini_path) - 1);
    /* A componentno-less rs_iface means "the root", which is whatever we are
     * being asked to root to — the manifest's boot interface on the first call,
     * the server's IF_SETTOPLEVEL group on a display-mode remount. */
    app->builder.root_interface_id = interface_id;
    /* Bake remaps sprite/font ids to scene ids so the tree renders directly. */
    app->builder.bridge = &app->bridge;
    app->builder_active = 1;

    app->app_state = APP_STATE_BOOTING;
    app->boot_interface_id = interface_id;
    app->boot_progress = 0;
    /* The booted gameframe root is what IF_GETTOP reports: this client mounts
     * every server sub-interface into it and treats a differing server
     * IF_OPENTOP as informational (see rs_gameproto_exec), so the two agree. */
    app->host.top_interface_id = interface_id;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppBoot_VTable;
    strncpy(task->task.name, "AppBoot", sizeof(task->task.name) - 1);
    task->app = app;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->runner.queue, &task->task);
}

/* IF_OPENSUB wrapper: mount a cache interface pack under a component slot of an
 * already-open root, then relayout + re-request CS1 over the new subtree. Runs
 * on the serial exec pipeline so a mount a packet triggers completes before the
 * next packet is popped (packet order holds), mirroring rs_ui_slots' slot mount.
 * type -1 means close (unmount via CreateTask_InterfaceOpenSub with iface<=0). */
struct Task_OpenSubRefresh
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int target_uid;
    int interface_id;
    int type;
};

static int
Task_OpenSubRefresh_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_OpenSubRefresh* self = (struct Task_OpenSubRefresh*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);
    /* IF_OPENTOP remounts on the asset runner; IF_OPENSUB rides the exec
     * pipeline. Wait out APP_STATE_BOOTING so the new root's slots exist
     * before mount_pack_under_target asserts.
     *
     * TASK_AWAIT_STATE, not a bare PT_YIELD loop, and the difference is a
     * deadlock. Only Task_AppBoot on app->runner clears APP_STATE_BOOTING, and
     * app->runner is stepped by the frame loop — which cannot get its turn back
     * while this queue is still settling. A plain yield here therefore spins
     * app_logic_tick's exec drain forever at 100% CPU: measured on every
     * Display-panel layout switch, which sends IF_OPENTOP and its IF_OPENSUB
     * mounts in one burst. */
    TASK_AWAIT_STATE(base, &self->pt, app->app_state != APP_STATE_BOOTING);
    if( self->interface_id > 0 && UITree_FindByComponentId(app->tree, self->target_uid) < 0 )
    {
        fprintf(
            stderr,
            "if-opensub: target 0x%08x missing after boot (iface=%d); skip\n",
            (unsigned)self->target_uid,
            self->interface_id);
        PT_EXIT(&self->pt);
    }
    if( self->interface_id > 0 )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_InterfaceOpenSub(
            app->provider,
            app->tree,
            &app->host,
            &app->invs,
            &app->bridge,
            self->target_uid,
            self->interface_id,
            self->type,
            &app->open_stats));
    }
    else
    {
        /* IF_CLOSESUB: for most slots, hide the outgoing group the way a
         * replacing mount does (hide + hide_unmounted on its roots —
         * task_interface_open step 4, which the next mount of that group
         * knows how to undo), then drop the mount record. chatbox:chatmodal
         * is the exception — dialogue packs reclaim instead of hide so
         * alternating chat_left/chat_right cannot leave shadowed text.
         * Hiding the SLOT here was wrong: nothing ever un-hides a slot, so
         * after one close every later mount into it — the same panel
         * reopened, or a different one — laid out and never drew. The mount
         * task asserts interface_id>0, so a close never routes through it. */
        {
            struct timespec close_t0;
            struct timespec close_t1;
            uint64_t close_ns;
            int rec = UITree_InterfaceParentFind(app->tree, self->target_uid);
            clock_gettime(CLOCK_MONOTONIC, &close_t0);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_IFACE_CLOSE, 1);
            if( rec >= 0 )
            {
                int old_group = app->tree->interface_parents[rec].group_id;
                UITreeIfaceStats_NoteClose(old_group);
                if( self->target_uid == UITREE_CHATBOX_CHATMODAL_UID )
                {
                    /* Dialogue packs alternate in chatmodal (chat_left /
                     * chat_right). Hide-reuse leaves baked strings that
                     * shadow FindByComponentId / remount. Reclaim so the
                     * next open fresh-bakes; App_IfTextSet reapplies. */
                    UITree_ReclaimInterfaceGroup(app->tree, old_group);
                }
                else
                {
                    struct UITreeNodeSet const* gset = UITree_GroupNodes(app->tree, old_group);
                    int gi;
                    TORIRS_PERF_COUNT(
                        TORIRS_PERF_CTR_IFACE_GROUP_SCAN_NODES, gset ? (int64_t)gset->count : 0);
                    if( gset )
                    {
                        for( gi = 0; gi < gset->count; gi++ )
                        {
                            int32_t idx = gset->slots[gi];
                            struct UITreeComponent* c;
                            assert(idx >= 0 && (uint32_t)idx < app->tree->component_count);
                            c = &app->tree->components[idx];
                            if( c->freed || c->component_id < 0 )
                                continue;
                            if( c->parent >= 0 &&
                                ((app->tree->components[c->parent].component_id >> 16) & 0xffff) ==
                                    old_group )
                                continue;
                            if( !c->behavior.hide )
                                c->behavior.hide_unmounted = 1;
                            c->behavior.hide = 1;
                        }
                    }
                }
                RS_CS2Host_ClearHooksForInterfaceGroup(&app->host, old_group);
            }
            UITree_InterfaceParentClear(app->tree, self->target_uid);
            clock_gettime(CLOCK_MONOTONIC, &close_t1);
            close_ns = (uint64_t)(close_t1.tv_sec - close_t0.tv_sec) * 1000000000ull +
                       (uint64_t)(close_t1.tv_nsec - close_t0.tv_nsec);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_IFACE_CLOSE_NS, (int64_t)close_ns);
        }
        /*
         * Then tell the tree a sub-interface went away.
         *
         * The mount path runs these hooks (task_interface_open step 8) and this
         * one did not, which is asymmetric and was wrong: the gameframe's
         * `on_sub_change` is what decides whether the sidebar shows the tab
         * strip or whatever replaced it. Opening the bank hid the tabs and
         * closing it left them hidden, so the sidebar came back blank.
         */
        PT_TASK_AWAITSELF_IF(CreateTask_CS2SubChangeDispatch(&app->host));
    }
    App_RefreshAfterTreeMutation(app);
    PT_END(&self->pt);
}

static void
Task_OpenSubRefresh_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_OpenSubRefresh_VTable = {
    .run = Task_OpenSubRefresh_Run,
    .free = Task_OpenSubRefresh_Free,
};

static void
app_enqueue_open_sub(
    struct App* app,
    int target_uid,
    int interface_id,
    int type)
{
    struct Task_OpenSubRefresh* task;

    assert(app);
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_OpenSubRefresh_VTable;
    strncpy(task->task.name, "OpenSubRefresh", sizeof(task->task.name) - 1);
    task->app = app;
    task->target_uid = target_uid;
    task->interface_id = interface_id;
    task->type = type;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

void
App_OpenSubInterface(
    struct App* app,
    int target_uid,
    int interface_id,
    int type)
{
    assert(app);
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "if-opensub: mount iface=%d under uid=0x%08x (%d<<16|%d) type=%d\n",
            interface_id,
            (unsigned)target_uid,
            (target_uid >> 16) & 0xffff,
            target_uid & 0xffff,
            type);
    app_enqueue_open_sub(app, target_uid, interface_id, type);
}

void
App_CloseSubInterface(
    struct App* app,
    int target_uid)
{
    assert(app);
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "if-closesub: unmount uid=0x%08x (%d<<16|%d)\n",
            (unsigned)target_uid,
            (target_uid >> 16) & 0xffff,
            target_uid & 0xffff);
    /* iface_id <= 0 makes the mount task just clear the slot. */
    app_enqueue_open_sub(app, target_uid, -1, 0);
}

void
App_MoveSubInterface(
    struct App* app,
    int source_uid,
    int dest_uid)
{
    int idx;
    int group_id;
    int type;

    assert(app);
    if( !app->tree || source_uid < 0 || dest_uid < 0 )
        return;
    idx = UITree_InterfaceParentFind(app->tree, source_uid);
    if( idx < 0 )
        return;
    group_id = app->tree->interface_parents[idx].group_id;
    type = app->tree->interface_parents[idx].type;
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "if-movesub: group=%d type=%d src=0x%08x dest=0x%08x\n",
            group_id,
            type,
            (unsigned)source_uid,
            (unsigned)dest_uid);
    App_CloseSubInterface(app, source_uid);
    App_OpenSubInterface(app, dest_uid, group_id, type);
}

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
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
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

/* Shared per-frame completion polls for async work (world load, textures,
 * deferred seq binds, tree refresh). Not run while BOOTING. */
static void
app_async_polls(struct App* app)
{
    /* World-load completion is no longer polled here: Task_WorldLoad runs
     * App_WorldLoadFinish itself at its synchronous tail (via on_done, or the
     * REBUILD_NORMAL path's inline call after it awaits the load). */
    if( app_tex_trace_enabled() )
        fprintf(stderr, "tex_trace: --- frame %d ---\n", ++g_tex_trace_frame);
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
        if( app->app_state == APP_STATE_READY )
            app_async_polls(app);
        if( main_stat == TASK_RUNNER_IDLE && exec_stat == TASK_RUNNER_IDLE &&
            app->app_state == APP_STATE_READY && !app->pending_tree_refresh &&
            !app->world_load_inflight )
            break;
    }
    if( guard <= 0 )
        fprintf(stderr, "app: boot wait exceeded step guard\n");
}

/* Defined below with the other interface-model binders; driven from the tick
 * so the figure's oscillation keeps running even on a frame nothing else
 * dirtied — the same reason RS_ClientCode_Tick drives the design preview's. */
static void
app_player_model_poll(struct App* app);

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
    int queued = 0;

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
                (app_if_events_for_node(app, trig.component_id) & (1u << trig.op_index)) )
            {
                int target = trig.component_id;
                int sub = -1;
                int obj_id = app->tree->components[idx].item_id;
                app_if_button_target(app, trig.component_id, &target, &sub);
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
        UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        if( !app_cs2_enqueue_followups(app) )
        {
            app->runner.frame_settle_pending = 0;
            return TASK_RUNNER_IDLE;
        }
    }
}

/* Only packets which can change interface/CS2-visible state open a visual
 * server-tick transaction. SERVER_TICK_END is not a universal reply fence:
 * immediate world feedback is also sent between scheduled ticks (the map flag
 * after MOVE_GAMECLICK is the common case). Treating every inbound packet as a
 * tick opener retained the framebuffer until the next 600ms server cycle.
 *
 * This deliberately includes transmit sources as well as direct IF_* writes:
 * a later clientscript in the same tick must observe varp/inventory/stat and
 * social changes as part of the same UI transaction. */
static int
app_packet_may_mutate_ui(enum GameProtoPktName packet_type)
{
    if( packet_type >= PKT_NAME_IF_OPENCHAT && packet_type <= PKT_NAME_IF_SETPLAYERMODEL_SELF )
        return 1;

    switch( packet_type )
    {
    case PKT_NAME_TUT_OPEN:
    case PKT_NAME_UPDATE_INV_STOP_TRANSMIT:
    case PKT_NAME_UPDATE_INV_FULL:
    case PKT_NAME_UPDATE_INV_PARTIAL:
    case PKT_NAME_UPDATE_IGNORELIST:
    case PKT_NAME_CHAT_FILTER_SETTINGS:
    case PKT_NAME_UPDATE_FRIENDLIST:
    case PKT_NAME_FRIENDLIST_LOADED:
    case PKT_NAME_UPDATE_RUNWEIGHT:
    case PKT_NAME_UPDATE_STAT:
    case PKT_NAME_UPDATE_RUNENERGY:
    case PKT_NAME_TRIGGER_ONDIALOGABORT:
    case PKT_NAME_RUNCLIENTSCRIPT:
    case PKT_NAME_VARP_SMALL:
    case PKT_NAME_VARP_LARGE:
    case PKT_NAME_VARP_SYNC:
    case PKT_NAME_VARP_RESET:
        return 1;
    default:
        return 0;
    }
}

/* --- connection loss and re-establishment -------------------------------
 *
 * The reference shape, from Client-TS `lostCon`/`logout` (Client.ts:2699,
 * 2734) and the deob's gameState 40: forget the world, say so over the
 * viewport, and ask for the session back. Nothing here returns to a login
 * screen — this client has none — so an exhausted retry budget leaves the
 * message up instead.
 */

void
App_NetSessionReset(struct App* app)
{
    assert(app);
    RS_EntitySync_Clear(&app->esync, app->world);
    /* The reference's game-state reset puts both Attack options back to
     * Hidden rather than recomputing them from the varp table it is about to
     * clear (rs_attack_option.h): a re-established session onto an account
     * whose setting is the default 0 would otherwise keep the previous
     * session's choice until its own VARP arrived. */
    app->player_attack_option = RS_ATTACK_OPTION_DEFAULT;
    app->npc_attack_option = RS_ATTACK_OPTION_DEFAULT;
    app->need_redraw = 1;
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

    fprintf(stderr, "net: connection lost (%s) — attempting to reestablish\n", why);
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
        fprintf(
            stderr,
            "net: session re-established after %d attempt(s)\n",
            app->net_reconnect_attempts);
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
        fprintf(
            stderr, "net: giving up after %d reconnect attempts\n", app->net_reconnect_attempts);
        app->net_reconnect_failed = 1;
        app->need_redraw = 1;
        return;
    }

    app->net_reconnect_attempts++;
    app->net_reconnect_at_ms = now_ms + APP_NET_RECONNECT_DELAY_MS;
    fprintf(stderr, "net: reconnect attempt %d\n", app->net_reconnect_attempts);
    if( !ToriRS_Network_Reconnect(app->net) )
    {
        app->net_reconnect_failed = 1;
        app->need_redraw = 1;
    }
}

/*
 * XP-drop panel probe (TORIRS_XPDROP_DEBUG=1).
 *
 * Interface 122 is entirely client-driven and its failure modes all present the
 * same way — no drops — so the probe prints the three states that separate them
 * and nothing else. Component ids are the cache's (122:2 statlistener holds the
 * stat-transmit hook, 122:17 drops_container, 122:18..24 the seven rows); they
 * are stable across rev 230 and 239.
 */
#define APP_XPDROP_IFACE 122
#define APP_XPDROP_COM(child) ((APP_XPDROP_IFACE << 16) | (child))
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
    fprintf(
        stderr, "xpdrop: t=%d clock=%d %s\n", (int)app->logic_cycle, app->host.client_clock, line);
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
            extern uint64_t PlatformSDL2_TicksUs(void);

            if( slow_ms < 0 )
            {
                char const* v = getenv("TORIRS_PKT_SLOW_MS");
                slow_ms = (v && v[0]) ? atoi(v) : 0;
            }
            t0 = slow_ms > 0 ? PlatformSDL2_TicksUs() : 0;
            stat = TaskRunner_SettleFrame(&app->exec_runner);
            if( slow_ms > 0 && last_exec_packet_type >= 0 )
            {
                uint64_t dt = PlatformSDL2_TicksUs() - t0;
                if( dt >= (uint64_t)slow_ms * 1000u )
                    fprintf(
                        stderr,
                        "pkt_slow: type=%d %.2f ms cycle=%llu\n",
                        last_exec_packet_type,
                        dt / 1000.0,
                        (unsigned long long)app->logic_cycle);
            }
        }

        if( stat != TASK_RUNNER_IDLE )
        {
            if( getenv("TORIRS_FRAME_LATCH") )
            {
                struct ToriRS_Task* head = app->exec_runner.queue->head;
                fprintf(
                    stderr,
                    "frame_latch: exec parked stat=%d head=%s blocked=%d io_pending=%d cycle=%d\n",
                    (int)stat,
                    head ? head->name : "(none)",
                    head ? head->blocked : -1,
                    PlatformX_IO_Pending(app->exec_runner.px, app->exec_runner.io),
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

            /*
             * Decoded, and not yet applied to anything.
             *
             * A plugin may drop the packet here. That is a live wire and is
             * meant to be: PLAYER_INFO and NPC_INFO carry extended-info blocks
             * indexed by list position, so dropping one desyncs entity
             * bookkeeping for the rest of the session. The fence is the one
             * packet that may never be dropped -- without it the clientscript
             * gate never closes and the UI stops updating -- so that is an
             * assert rather than a silently honoured request.
             */
            if( app->plugins && PluginHost_PacketIn(app->plugins, (int)packet.packet_type, -1) )
            {
                assert(packet.packet_type != PKT_NAME_SERVER_TICK_END);
                gameproto_free(&packet);
                continue;
            }
            /* Once a revision has demonstrated explicit tick fences,
             * retain only packets that participate in an atomic UI/CS2
             * transaction. World feedback is valid between those ticks.
             * SERVER_TICK_END clears this after its exec task has run. */
            if( app->server_tick_fence_seen && app_packet_may_mutate_ui(packet.packet_type) &&
                packet.packet_type != PKT_NAME_SERVER_TICK_END )
            {
                if( !app->server_tick_open )
                {
                    app->server_tick_open_cycle = app->logic_cycle;
                    if( getenv("TORIRS_FRAME_LATCH") )
                        fprintf(
                            stderr,
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
            ToriRS_TaskQueue_Add(
                app->exec_runner.queue, CreateTask_GameProtoExec(app, &packet));
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
        (!app->server_tick_fence_seen || app->logic_cycle - app->pending_clientscript_cycle >=
                                             APP_CLIENTSCRIPT_FENCE_MAX_CYCLES) )
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

/* One 20ms client tick: clock, widget timers, animation loads + advance. */
static int
app_logic_tick(struct App* app)
{
    int redraw = 0;

    app->logic_cycle++;

    /* Before the packet pump, so a handler that samples world state sees the
     * cycle it was told about rather than one already half-advanced by this
     * tick's packets. The 600ms server cadence is a different event
     * (EV_SERVER_TICK, raised at the tick fence). */
    PluginHost_LogicTick(app->plugins, (int)app->logic_cycle);

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_TICK_PACKETS)
    {
        if( app_pump_net_packets(app) )
            redraw = 1;
    }

    /* No widget hook may observe a half-applied packet/interface transaction.
     * The next logic tick resumes the serial runner; the shell keeps presenting
     * the preceding committed framebuffer in the meantime. */
    if( app->exec_runner_had_work || app->server_tick_open || app->pending_clientscript_count > 0 )
        return redraw;

    /* Rasterize inventory item icons that the server's inv packets left
     * unresolved (queued on the same serial pipeline, so it runs after the
     * packets that dirtied the slots). */
    app_inv_icon_reconcile_tick(app);

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
            char const* cheat = getenv("TORIRS_NET_CHEAT");
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

                app->net_cheat_sent = 1;
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
                        }
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
                    char const* p = getenv("TORIRS_NET_CHEAT");
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
                listener_x = (int)local->draw_position.x >> 7;
                listener_z = (int)local->draw_position.z >> 7;
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
    }

    /*
     * The two other coords the highlight refreshers read, and the three edges
     * that re-run them.
     *
     * Clientscripts 5197 / 5204 / 5210 mark the hovered, current and
     * destination tile. Each takes no arguments and reads its subject from an
     * opcode -- `_6950`, `coord`, `_3330` -- so the client's whole job is to
     * run each one again when its subject changes. Nothing in the cache calls
     * them; the reference client does, on exactly these edges.
     *
     * Edge-triggered and not per-frame. Each script clears its group and
     * re-adds one tile, so running them every frame would be three script
     * dispatches and six highlight ops a frame to say what has not changed --
     * and they are also what CLEARS the group when the setting is switched
     * off, so they must run at least once after any change either way.
     */
    {
        int dest_coord = -1;
        int hover_coord = -1;

        if( app->world && app->world->load_complete && app->minimap_flag_x >= 0 )
        {
            int const x = app->world->_base_tile_x + app->minimap_flag_x;
            int const z = app->world->_base_tile_z + app->minimap_flag_z;
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
         * What the pointer is on, for MINIMENU_TYPE and the target getters.
         *
         * The nearest non-terrain pick of the frame -- the same one the
         * client's own left click would act on, and the same rule
         * app_plugin_hover_entity follows. Terrain is skipped: over open
         * ground the answer is "nothing", which is what clientscript 5350
         * bails on.
         */
        {
            struct RS_ClientOpContext mo;
            int minimenu_type = RS_MINIMENU_TYPE_NONE;

            memset(&mo, 0, sizeof(mo));
            mo.kind = -1;
            mo.uid = -1;
            mo.type = -1;
            mo.coord = -1;
            mo.layer = -1;

            if( app->world )
            {
                for( int i = 0; i < app->world_pickset.count; i++ )
                {
                    struct World_Picked const* hit = &app->world_pickset.items[i];
                    int const base_x = app->world->_base_tile_x;
                    int const base_z = app->world->_base_tile_z;

                    if( hit->type == WORLD_PICK_NPC )
                    {
                        struct WorldEntity_NPC* npc =
                            World_NpcGetByElementId(app->world, hit->element_id, NULL);
                        if( !npc )
                            continue;
                        mo.kind = RS_CLIENTOP_NPC;
                        minimenu_type = RS_MINIMENU_TYPE_NPC;
                        mo.uid = npc->server_slot;
                        mo.type = npc->npc_id;
                        mo.coord = RS_CLIENTOP_COORD(
                            npc->grid_position.level,
                            base_x + npc->grid_position.x,
                            base_z + npc->grid_position.z);
                        snprintf(mo.name, sizeof(mo.name), "%s", npc->name);
                    }
                    else if( hit->type == WORLD_PICK_SCENERY )
                    {
                        struct WorldEntity_Scenery* loc =
                            World_SceneryGetByElementId(app->world, hit->element_id);
                        if( !loc )
                            continue;
                        mo.kind = RS_CLIENTOP_LOC;
                        minimenu_type = RS_MINIMENU_TYPE_LOC;
                        mo.type = loc->loc_id;
                        /* Half of a loc's identity: a tile holds one loc per
                         * layer, and the scripted-overlay store keys on it. */
                        mo.layer = World_LocShapeToLayer(loc->shape);
                        mo.coord = RS_CLIENTOP_COORD(
                            loc->grid_position.level,
                            base_x + loc->grid_position.x,
                            base_z + loc->grid_position.z);
                        snprintf(mo.name, sizeof(mo.name), "%s", loc->name);
                    }
                    else if( hit->type == WORLD_PICK_OBJSTACK )
                    {
                        struct WorldEntity_ObjStack* stack =
                            World_ObjStackGetByElementId(app->world, hit->element_id);
                        if( !stack )
                            continue;
                        mo.kind = RS_CLIENTOP_OBJ;
                        minimenu_type = RS_MINIMENU_TYPE_OBJ;
                        mo.type = stack->obj_id;
                        mo.coord = RS_CLIENTOP_COORD(
                            stack->grid_position.level,
                            base_x + stack->grid_position.x,
                            base_z + stack->grid_position.z);
                        snprintf(mo.name, sizeof(mo.name), "%s", stack->name);
                    }
                    else if( hit->type == WORLD_PICK_PLAYER )
                    {
                        struct WorldEntity_Player* pl =
                            World_PlayerGetByElementId(app->world, hit->element_id);
                        if( !pl )
                            continue;
                        mo.kind = RS_CLIENTOP_PLAYER;
                        minimenu_type = RS_MINIMENU_TYPE_PLAYER;
                        mo.coord = RS_CLIENTOP_COORD(
                            pl->grid_position.level,
                            base_x + pl->grid_position.x,
                            base_z + pl->grid_position.z);
                        snprintf(mo.name, sizeof(mo.name), "%s", pl->name);
                    }
                    else
                        continue;
                    break;
                }
            }

            snprintf(
                app->host.clientop.mouseover_target,
                sizeof(app->host.clientop.mouseover_target),
                "%s",
                mo.name);
            app->host.clientop.menu_open = app->interact.minimenu.visible;
            RS_ClientOpMouseoverSet(&app->host.clientop, &mo, minimenu_type);

            /*
             * Publishing is ALL the client does here.
             *
             * The cache drives the mouseover highlighter itself: clientscript
             * 4726 is re-armed on a gameframe component timer and calls 5350
             * every tick. Measured -- with the client's own edge-triggered call
             * removed, 5350 still ran 89 times over the same window. Adding one
             * would be a second driver for an idempotent script, which is only
             * waste.
             *
             * The three TILE refreshers are different and are driven below:
             * nothing in the cache calls those at all.
             */
            if( getenv("TORIRS_CLIENTOP_DEBUG") )
            {
                int const subject = mo.kind < 0 ? -1 : (mo.kind * 4096) ^ mo.uid ^ mo.type;
                if( subject != app->highlight_last_mouseover )
                {
                    app->highlight_last_mouseover = subject;
                    fprintf(
                        stderr,
                        "mouseover: type=%d kind=%d uid=%d id=%d '%s'\n",
                        minimenu_type,
                        mo.kind,
                        mo.uid,
                        mo.type,
                        mo.name);
                }
            }
        }

        /* Only once the world is up: before that the scripts would clear a
         * group and re-add a tile from a scene that is about to be replaced,
         * and the group is rebuilt by the login initialiser anyway. */
        if( app->world && app->world->load_complete )
        {
            if( hover_coord != app->highlight_last_hover_coord )
            {
                app->highlight_last_hover_coord = hover_coord;
                RS_CS2_RunScript(
                    &app->host, &app->runner, app->host.script_highlight_hover_tile,
                    NULL, 0, 0, NULL, 0);
            }
            if( app->host.local_coord != app->highlight_last_local_coord )
            {
                app->highlight_last_local_coord = app->host.local_coord;
                RS_CS2_RunScript(
                    &app->host, &app->runner, app->host.script_highlight_current_tile,
                    NULL, 0, 0, NULL, 0);
            }
            if( dest_coord != app->highlight_last_dest_coord )
            {
                app->highlight_last_dest_coord = dest_coord;
                RS_CS2_RunScript(
                    &app->host, &app->runner, app->host.script_highlight_dest_tile,
                    NULL, 0, 0, NULL, 0);
            }
        }
    }

    RS_CS2Host_Tick(&app->host);

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
                    RS_Chat_AddMessage(&app->chat, RS_CHAT_TYPE_PRIVATE_TO, shown, send.text);
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
                fprintf(
                    stderr,
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
                app->runner.queue,
                CreateTask_PluginSave(app->plugins, app->plugin_prefs_path));
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

    /*
     * if_triggeroplocal — CS2 asked the client to notify the server of a
     * synthetic button click (skill-guide Quest XP View-journal). Real
     * rev-239 wire is IF_SCRIPT_TRIGGER; over rev-230 this is IF_BUTTON1
     * with the typed arg (quest id) in the sub field → last_slot.
     */
    {
        struct RS_CS2TriggerOpLocal trig;
        int guard = 0;

        while( guard++ < RS_CS2_HOST_TRIGGEROPLOCAL_MAX * 4 &&
               RS_CS2Host_TakeTriggerOpLocal(&app->host, &trig) )
        {
            if( !app->net )
                continue;
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
#if !defined(TORIRS_PERF_DISABLE)
        if( g_torirs_perf_enabled || stats_enabled )
#else
        if( stats_enabled )
#endif
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
            fprintf(
                stderr,
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
            for( uint32_t i = 0; i < app->tree->component_count; i++ )
            {
                struct UITreeComponent const* node = &app->tree->components[i];
                if( node->freed || node->type != UIELEM_RS_MODEL )
                    continue;
                if( node->u.rs_model.anim_seq_id < 0 )
                    continue;
                fprintf(
                    stderr,
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

    return redraw;
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
        int wy = app_world_height(app, wx, wz, local_level);
        if( npcpos_debug )
            fprintf(
                stderr, "plrpos: tile=%d,%d lvl=%d(local %d) w=%d,%d y=%d\n",
                player->grid_position.x, player->grid_position.z,
                player->grid_position.level, local_level, wx, wz, wy);
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
        int wy = app_world_height(app, wx, wz, local_level);
        if( npcpos_debug )
            fprintf(
                stderr,
                "npcpos: id=%d tile=%d,%d lvl=%d(local %d) w=%d,%d y=%d size=%d "
                "h0=%d h1=%d h2=%d lb=%d\n",
                npc->npc_id, npc->grid_position.x, npc->grid_position.z,
                npc->grid_position.level, local_level, wx, wz, wy, npc->size,
                heightmap_get_interpolated(app->world->heightmap, wx, wz, 0),
                heightmap_get_interpolated(app->world->heightmap, wx, wz, 1),
                heightmap_get_interpolated(app->world->heightmap, wx, wz, 2),
                (World_TileFlagGet(app->world, wx >> 7, wz >> 7, 1) &
                 RSCACHE_FLOFLAG_LINK_BELOW) != 0);
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
                if( anim && anim->frame_count == play_frames &&
                    !ToriDraw_AnimationAdvanceObjectFrame(anim, &element->anim_frame) )
                    ToriDraw_SceneElementSetAnimation(app->scene, element_id, NULL, true);
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

    font_cache_id =
        app->cfg.cache_kind == APP_CACHE_DAT1 ? APP_FONT_P12_DAT1_SLOT : APP_FONT_P12_CACHE_ID;
    scene_id = UITreeSceneBridge_EnsureFont(&app->bridge, font_cache_id);
    if( scene_id < 0 )
    {
        struct ToriRS_Task* task = CreateTask_FontLoad(app->provider, font_cache_id);
        if( task )
            ToriRS_TaskQueue_Add(app->runner.queue, task);
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
        char const* dd = getenv("TORIRS_DRAW_DISTANCE");
        int v = ToriRS_Features_PainterDrawDistance(app->features);
        int from_env;
        if( dd && dd[0] != '\0' && sscanf(dd, "%d", &from_env) == 1 )
            v = from_env;
        painter_set_draw_distance(painter, v);
    }
    radius = painter_get_draw_distance(painter);

    nocull = getenv("TORIRS_PAINTER_NOCULL");
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
        char const* dc = getenv("TORIRS_WEDGE_DRAWCENTER");
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

    cull_mode = getenv("TORIRS_PAINTER_CULL");
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
                toridraw_proj_cot16(
                    app->world_camera.proj_mode,
                    app->world_camera.proj_scale,
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
            fprintf(
                stderr,
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

        fprintf(
            stderr,
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
    params.proj_mode = app->world_camera.proj_mode;
    params.proj_scale = app->world_camera.proj_scale;
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
        static int px, py, pz, ppitch, pyaw;
        if( !resolved )
        {
            char const* wc = getenv("TORIRS_WEDGE_CAM");
            resolved = 1;
            if( wc && sscanf(wc, "%d,%d,%d,%d,%d", &px, &py, &pz, &ppitch, &pyaw) == 5 )
                have = 1;
        }
        if( have )
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
            app->world_camera.pitch += jitter;
            if( app->world_camera.pitch < 128 )
                app->world_camera.pitch = 128;
            if( app->world_camera.pitch > 383 )
                app->world_camera.pitch = 383;
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

    app_update_painter_cull(app, cam_sx, cam_sz);

    /* Planar occluders: project shadows for this eye. TORIRS_OCCLUDERS=0
     * disables (mirrors TORIRS_PAINTER_NOCULL=1). Default is on. */
    {
        struct SceneOccluders* occ = painter_get_occluders(app->world->painter);
        const char* env_occ = getenv("TORIRS_OCCLUDERS");
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
            if( getenv("TORIRS_OCCLUDERS_DEBUG") )
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
                    fprintf(
                        stderr,
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
    painter_wedgelog_set_eye(
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
    else if( g_torirs_painter_force == 1 ||
             (g_torirs_painter_force == 0 && getenv("TORIRS_PAINTER_W3D")) )
        painter_paint_world3d(app->world->painter, app->painter_buffer, cam_sx, cam_sz, cam_slevel);
    else
        painter_paint_bucket(app->world->painter, app->painter_buffer, cam_sx, cam_sz, cam_slevel);

    app->world_camera_pos.x = shake_x;
    app->world_camera_pos.y = shake_y;
    app->world_camera_pos.z = shake_z;
    app->world_camera.pitch = shake_pitch;
    app->world_camera.yaw = shake_yaw;

    /* TORIRS_PAINT_DEBUG: what the painter actually emitted this frame, by kind.
     * Scene elements existing is not the same as being painted — the bucket
     * flood-fill, the level mask and the cull map each drop work silently. */
    if( getenv("TORIRS_PAINT_DEBUG") )
    {
        int by_kind[16] = { 0 };
        for( int i = 0; i < app->painter_buffer->command_count; i++ )
            by_kind[app->painter_buffer->commands[i]._bf_kind & 0xF]++;
        fprintf(
            stderr,
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
                fprintf(stderr, " %d:%d", k, by_kind[k]);
        fprintf(stderr, "\n");
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
        const char* env = getenv("TORIRS_TILETABLE");
        /* Wait for the paint the caller means. The table is only interesting
         * after a teleport into an instance, and printing on the first paint
         * silently describes wherever the player logged in — the same trap that
         * made TORIRS_HPROF report Lumbridge's relief for the arena. */
        const char* at = getenv("TORIRS_TILETABLE_AT");
        int want_paint = at ? atoi(at) : 600;
        int x0, x1, z0, z1;
        paints++;
        if( env && !done && paints >= want_paint &&
            sscanf(env, "%d,%d,%d,%d", &x0, &x1, &z0, &z1) == 4 )
        {
            done = 1;
            fprintf(stderr, "tile     lvl flags elem  set order\n");
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
                        fprintf(
                            stderr,
                            "%3d,%-3d  L%d  0x%02x %5d 0x%x ",
                            x,
                            z,
                            lv,
                            (unsigned)World_TileFlagGet(app->world, x, z, lv),
                            World_TerrainElementAt(app->world, x, z, lv),
                            painter_tile_get_terrain_levels(app->world->painter, x, z, lv));
                        if( order < 0 )
                            fprintf(stderr, "-\n");
                        else if( order_count > 1 )
                            fprintf(stderr, "%d  DRAWN %dx\n", order, order_count);
                        else
                            fprintf(stderr, "%d\n", order);
                    }
            /* The other half of the comparison. A terrain order is only
             * meaningful against the scenery it is supposed to be behind, and
             * both have to be read off the SAME buffer — the render-command
             * sequence TORIRS_DRAW_ORDER prints is a filtered renumbering, so
             * the two cannot be lined up across tools. */
            fprintf(stderr, "locs overlapping the rect (same numbering)\n");
            for( int i = 0; i < app->painter_buffer->command_count; i++ )
            {
                struct PaintersElementCommand* c = &app->painter_buffer->commands[i];
                struct WorldEntity_Scenery* sc;
                if( c->_bf_kind != PNTR_CMD_ELEMENT )
                    continue;
                sc = World_SceneryGetByElementId(app->world, (int)c->_entity._bf_entity);
                if( !sc )
                    continue;
                if( sc->grid_position.x < x0 - 8 || sc->grid_position.x > x1 + 8 ||
                    sc->grid_position.z < z0 - 8 || sc->grid_position.z > z1 + 8 )
                    continue;
                fprintf(
                    stderr,
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

    if( !app->world_active || !app->world_view_valid )
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
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
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
            if( getenv("TORIRS_NET_DEBUG") )
                fprintf(
                    stderr,
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

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
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
    tile_x = ((int)player->draw_position.x + rel_x) >> 7;
    tile_z = ((int)player->draw_position.z - rel_y) >> 7;
    if( tile_x < 0 || tile_z < 0 || tile_x >= app->world->_scene_size ||
        tile_z >= app->world->_scene_size )
        return 0;

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "minimap: click=%d,%d rel=%d,%d scene=%d,%d abs=%d,%d\n",
            center_x,
            center_y,
            rel_x,
            rel_y,
            tile_x,
            tile_z,
            app->world->_base_tile_x + tile_x,
            app->world->_base_tile_z + tile_z);
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
    int player_level = player ? player->grid_position.level : -1;

    ToriRS_PickHitsClassify(app->world, hits, player_level, &app->world_pickset, &result);
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

    if( getenv("TORIRS_WORLD_PICK_DEBUG") )
    {
        fprintf(
            stderr,
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
            fprintf(
                stderr,
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
                scenery ? scenery->name : "");
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
    if( getenv("TORIRS_KEY_DEBUG") &&
        (app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_TOGGLE) ||
         app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_MORE) ||
         app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_LESS) ||
         app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_MORE_100) ||
         app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_LESS_100)) )
        fprintf(
            stderr,
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
    if( !app->world_active || !app->world_view_valid )
        return;
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
    app->cam_key_left = LibToriRS_Input_IsKeyHeld(input, TORIRSK_LEFT);
    app->cam_key_right = LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT);
    app->cam_key_up = LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP);
    app->cam_key_down = LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN);
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
        fprintf(stderr, "camera: %s\n", app->camera_unlocked ? "UNLOCKED" : "locked");
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
            fprintf(stderr, "paintlimit: %d\n", logged);
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
        if( node->freed )
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
                fprintf(
                    stderr,
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
    fprintf(
        stderr,
        "cam_%s: %s yaw=%d pitch=%d zoom=%d%% eye=%d,%d,%d\n",
        what,
        follow_cam ? "orbit" : "free",
        follow_cam ? app->orbit_yaw : app->world_camera.yaw,
        follow_cam ? app->orbit_pitch : app->world_camera.pitch,
        app->world_zoom_pct,
        app->world_camera_pos.x,
        app->world_camera_pos.y,
        app->world_camera_pos.z);
}

/* Middle-button rotate and wheel zoom over the world viewport. Both gestures
 * are properties of the WORLD element (revconfig mmb_rotate= / wheel_zoom=),
 * read off the cached emit desc — the same desc whose rect app_world_mouse_gate
 * uses to decide the pointer is on the scene rather than on the interface.
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

    if( !app->world_active || !app->world_view_valid )
    {
        app->cam_mmb_active = 0;
        return;
    }
    /* The same split app_world_camera_keys makes: online and out of a cutscene
     * the orbit follow cam owns the angles, otherwise the free camera does. */
    follow_cam = app->net && !app->cam_script.scripted;

    if( app->world_emit_desc.world_mmb_rotate )
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
                    app->orbit_pitch += dy * APP_WORLD_MMB_PITCH_PER_PX;
                    if( app->orbit_pitch < 128 )
                        app->orbit_pitch = 128;
                    if( app->orbit_pitch > 383 )
                        app->orbit_pitch = 383;
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
    if( app->world_emit_desc.world_wheel_zoom && input->curr.mouse_wheel_y != 0 &&
        !out->wheel_consumed && !app->interact.minimenu.visible &&
        /* The chrome's claim is checked HERE, not inferred from consumed
         * flags: this runs long after the overlay handled input, when
         * input_frame_consumed is 1 on every frame. A wheel over a panel or
         * an open dropdown belongs to the chrome even when the chrome had
         * nothing to do with it -- consumed into nothing beats zooming the
         * world behind a panel. */
        !ToriRSChrome_WantsWheel(&app->dbg_ui, mouse_x, mouse_y) &&
        app_world_mouse_gate(app, mouse_x, mouse_y) )
    {
        if( follow_cam )
        {
            app->world_zoom_pct -= input->curr.mouse_wheel_y * APP_WORLD_ZOOM_STEP_PCT;
            if( app->world_zoom_pct < APP_WORLD_ZOOM_MIN_PCT )
                app->world_zoom_pct = APP_WORLD_ZOOM_MIN_PCT;
            if( app->world_zoom_pct > APP_WORLD_ZOOM_MAX_PCT )
                app->world_zoom_pct = APP_WORLD_ZOOM_MAX_PCT;
        }
        else
        {
            app_camera_move_forward(app, input->curr.mouse_wheel_y * APP_WORLD_ZOOM_FREECAM_STEP);
        }
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
 * why spawns copy registry models instead of sharing handles. */
static int
app_world_scene_element_create(
    struct App* app,
    struct ToriDraw_Model* model,
    int world_x,
    int world_y,
    int world_z)
{
    struct ToriDraw_ModelHandle hnd;
    int element_id = ToriDraw_SceneElementAddPool(app->scene, TORIDRAW_SCENE_POOL_DYNAMIC);

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

/* A registered animation that can actually pose a model: either a classic
 * frame/framemap track or a skeletal (Animaya) matrix palette. Everything else
 * is the empty sentinel a failed load leaves behind. */
static int
app_anim_playable(struct ToriDraw_Animation const* anim)
{
    assert(anim);
    if( anim->frame_count <= 0 )
        return 0;
    return (anim->frames && anim->base) || anim->skeletal != NULL;
}

/* Point a scene element at an animation, selecting the pose path. Skeletal
 * sequences carry no bones, so the element has to be flagged for
 * ToriDraw_ModelAnimateSkeletal instead of the frame animator. */
static void
app_element_set_anim(
    struct ToriDraw_SceneElement* el,
    struct ToriDraw_Animation* anim)
{
    el->animation = anim;
    el->is_skeletal = anim && anim->skeletal != NULL;
    el->skeletal_animation = anim ? anim->skeletal : NULL;
    el->skeletal_play_frames = el->is_skeletal ? anim->frame_count : 0;
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
                if( anim->frame_count == play_frames &&
                    !ToriDraw_AnimationAdvanceObjectFrame(anim, &element->anim_frame) )
                    ToriDraw_SceneElementSetAnimation(app->scene, element_id, NULL, true);
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
    if( app_anim_playable(anim) )
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        ToriDraw_SceneElementSetAnimationSeq(app->scene, element_id, seq_id);
        ToriDraw_SceneElementSetAnimation(app->scene, element_id, anim, true);
        if( el )
            app_element_set_anim(el, anim);
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
                (el && el->model.kind == TORIDRAWMK_MODEL) ? el->model.u.model.model : NULL;
            fprintf(
                stderr,
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
        fprintf(
            stderr,
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
    if( app->seq_bind_pending_count <
        (int)(sizeof(app->seq_bind_pending) / sizeof(app->seq_bind_pending[0])) )
    {
        app->seq_bind_pending[app->seq_bind_pending_count].element_id = element_id;
        app->seq_bind_pending[app->seq_bind_pending_count].seq_id = seq_id;
        app->seq_bind_pending[app->seq_bind_pending_count].start_cycle = start_cycle;
        app->seq_bind_pending_count++;
    }
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
    int kept = 0;
    for( int i = 0; i < app->seq_bind_pending_count; i++ )
    {
        if( app->seq_bind_pending[i].element_id == element_id )
            continue;
        app->seq_bind_pending[kept++] = app->seq_bind_pending[i];
    }
    app->seq_bind_pending_count = kept;
}

/* Per-frame: bind deferred element/sequence pairs whose loads landed. */
static void
app_world_bind_pending_seqs(struct App* app)
{
    int kept = 0;
    for( int i = 0; i < app->seq_bind_pending_count; i++ )
    {
        struct AppSeqBindPending* pend = &app->seq_bind_pending[i];
        if( !ToriDraw_SceneElementIsLive(app->scene, pend->element_id) )
            continue; /* element despawned while loading */
        if( app_world_try_bind_seq(app, pend->element_id, pend->seq_id, pend->start_cycle) )
        {
            app->need_redraw = 1;
            continue;
        }
        app->seq_bind_pending[kept++] = *pend;
    }
    app->seq_bind_pending_count = kept;
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
    if( !*list )
        return false;
    while( *list )
    {
        char* end = NULL;
        long const id = strtol(list, &end, 10);
        if( end == list )
            break;
        if( id == npc_id )
            return true;
        list = (*end == ',') ? end + 1 : end;
        if( !*list )
            break;
    }
    return false;
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
        fprintf(stderr, "spawn_player: player model unavailable\n");
        return -1;
    }
    reg = ToriDraw_SceneModelGet(app->scene, scene_model_id);
    if( reg.kind != TORIDRAWMK_MODEL || !reg.u.model.model )
        return -1;
    copy = ToriDraw_ModelCopy(reg.u.model.model);
    if( !copy )
        return -1;
    ToriDraw_ModelSetBoundsCylinder(copy);
    ToriDraw_ModelCaptureOriginalVertices(copy);

    world_y = app_world_height(app, world_x, world_z, level);
    element_id = app_world_scene_element_create(app, copy, world_x, world_y, world_z);
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
    fprintf(
        stderr,
        "spawn_player: element=%d tile=%d,%d level=%d\n",
        element_id,
        tile_x,
        tile_z,
        level);
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
    extern uint64_t PlatformSDL2_TicksUs(void);

    if( bd_us < 0 )
    {
        char const* v = getenv("TORIRS_SPAWN_BREAKDOWN");
        bd_us = (v && v[0]) ? atoi(v) : 0;
        spawn_log = getenv("TORIRS_SPAWN_LOG") ? 1 : 0;
    }
    bd_t0 = bd_us ? PlatformSDL2_TicksUs() : 0;

    npctype = CacheProvider_NpctypeGet(app->provider, npc_id);
    if( !npctype )
    {
        /* Once per id. The server re-sends the same missing npc every time the
         * player walks back into its zone, and on Windows an unbuffered stderr
         * write costs milliseconds -- see the spawn narration below. Which ids
         * are unavailable is the whole diagnostic; the repeat count is not. */
        if( !app_warn_once_npc(npc_id) )
            fprintf(stderr, "spawn_npc: npc %d unavailable\n", npc_id);
        return -1;
    }

    bd_t = bd_us ? PlatformSDL2_TicksUs() : 0;
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
        bd_model = PlatformSDL2_TicksUs() - bd_t;
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
        fprintf(stderr, "spawn_npc: npc %d models failed to load\n", npc_id);
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
    bd_t = bd_us ? PlatformSDL2_TicksUs() : 0;
    element_id = app_world_scene_element_create(app, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return -1;
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
            el->anim_external = true;
        ToriDraw_SceneAnimListInvalidate(app->scene);
    }
    if( bd_us )
        bd_elem = PlatformSDL2_TicksUs() - bd_t;

    bd_t = bd_us ? PlatformSDL2_TicksUs() : 0;
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
        bd_world = PlatformSDL2_TicksUs() - bd_t;

    bd_t = bd_us ? PlatformSDL2_TicksUs() : 0;
    app_world_apply_seq(app, element_id, facts.readyanim);
    if( bd_us )
        bd_seq = PlatformSDL2_TicksUs() - bd_t;
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
    bd_t = bd_us ? PlatformSDL2_TicksUs() : 0;
    if( spawn_log )
        fprintf(
            stderr,
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
        bd_log = PlatformSDL2_TicksUs() - bd_t;

    bd_t = bd_us ? PlatformSDL2_TicksUs() : 0;
    app_sync_textures(app);
    if( bd_us )
    {
        uint64_t total;

        bd_tex = PlatformSDL2_TicksUs() - bd_t;
        total = PlatformSDL2_TicksUs() - bd_t0;
        if( total >= (uint64_t)bd_us )
            fprintf(
                stderr,
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
            struct ToriRS_PluginNpcSnap snap;
            app_plugin_fill_npc(app, spawned, &snap);
            PluginHost_NpcSpawn(app->plugins, &snap);
        }
    }
    if( idx != WORLD_ENTITY_NIL )
    {
        struct WorldEntity_NPC* spawned = World_EntityPoolGet(&app->world->entities.npc, idx);
        if( spawned )
            app_client_trigger_npc(app, spawned, RS_TRIGGER_NPC_ADD);
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

    model_ids[0] = model_id;
    model = app_world_build_model(app, model_ids, 1, NULL, 128, 128, APP_LIGHT_ACTOR, 0, 0);
    if( !model )
    {
        fprintf(stderr, "spawn_projectile: model %d failed to load\n", model_id);
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

    element_id = app_world_scene_element_create(app, model, src_x, src_y, src_z);
    if( element_id < 0 )
        return;

    World_ProjectileSpawn(
        app->world,
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
    fprintf(
        stderr,
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

/* Server-driven projectile (reference ClientProj / MAP_PROJANIM). Builds the
 * transformed spotanim model (recolour/resize/angle/lighting), spawns the world
 * projectile with the wire trajectory params, and binds the spotanim seq so the
 * model animates in flight. SYNCHRONOUS — the spotanimtype, its model and its
 * seq must already be resident (Task_AppSpawn awaits them). src_height/dst_height
 * are raw wire bytes (×4, matching Client.ts h1/h2). */
static void
app_world_spawn_projectile_spot_now(
    struct App* app,
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

    spot = CacheProvider_SpotanimtypeGet(app->provider, spotanim_id);
    if( !spot )
    {
        fprintf(stderr, "spawn_projectile_spot: spotanim %d not resident\n", spotanim_id);
        return;
    }

    model = app_world_build_spotanim_model(app, spot);
    if( !model )
    {
        fprintf(
            stderr,
            "spawn_projectile_spot: spotanim %d model %d failed\n",
            spotanim_id,
            spot->model);
        return;
    }

    src_x = src_tile_x * 128 + 64;
    src_z = src_tile_z * 128 + 64;
    dst_x = dst_tile_x * 128 + 64;
    dst_z = dst_tile_z * 128 + 64;
    /* World y is negative-up: reference y = getAvH(src) - h1, h1 = src_height*4. */
    src_y = app_world_height(app, src_x, src_z, src_level) - src_height * 4;

    element_id = app_world_scene_element_create(app, model, src_x, src_y, src_z);
    if( element_id < 0 )
        return;

    /* peak -> angle, arc -> startpos, end_height = dst_height*4 (World computes
     * dst y as height_fn(dst) - end_height, matching getAvH(dst) - h2). `target`
     * goes through in its wire encoding so World re-aims the arc at that
     * entity's live position every cycle (reference addProjectiles). */
    World_ProjectileSpawn(
        app->world,
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

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "spawn_projectile_spot: element=%d spotanim=%d model=%d seq=%d "
            "%d,%d -> %d,%d t1=%d t2=%d target=%d\n",
            element_id,
            spotanim_id,
            spot->model,
            spot->seq,
            src_tile_x,
            src_tile_z,
            dst_tile_x,
            dst_tile_z,
            start_delay,
            end_delay,
            target);
    app_sync_textures(app);
    app->need_redraw = 1;
}

/* Total client cycles one loop of a seq takes TO PLAY HERE. Drives the
 * free-standing spotanim's single-shot lifetime, so it has to agree with
 * whatever actually steps the frames — which is
 * ToriDraw_AnimationAdvanceObjectCycles, and that implements the rev239
 * `while (cycle > delay) cycle -= delay` walk, not Client-TS MapSpotAnim's
 * `getDuration(frame) + 1` subtraction.
 *
 * The two differ by one cycle per frame. Trace the rev239 walk: from a zero
 * counter the first frame needs delay+1 cycles to trip a STRICT `>`, and it
 * then leaves 1 behind, so every later frame costs exactly its own delay. The
 * loop is therefore sum(delay) + 1 cycles long, where summing (delay + 1) is
 * sum(delay) + frame_count.
 *
 * Overstating it by frame_count - 1 is not harmless: the sequence ends, the
 * element drops its animation and snaps back to the un-posed base model, and
 * the spotanim then sits frozen in that pose until the lifetime finally
 * expires. On a 37-frame splash that is 36 cycles of dead frame — the visible
 * "it plays, then freezes" at the end of every map spotanim.
 *
 * `delay <= 0` counts as 1 because the advance treats it that way. */
static int
app_seq_total_duration(
    struct App* app,
    int seq_id)
{
    int frames = app_seq_frame_count(app, seq_id);
    int total = 1;
    if( frames <= 0 )
        return 1;
    for( int f = 0; f < frames; f++ )
    {
        int delay = app_seq_frame_duration(app, seq_id, f);
        total += delay > 0 ? delay : 1;
    }
    return total > 0 ? total : 1;
}

/* Free-standing spotanim (reference MapSpotAnim / MAP_ANIM). SYNCHRONOUS — the
 * spotanimtype, its model and its seq must already be resident (Task_AppSpawn
 * awaits them). Builds the transformed model, spawns the world entity with a
 * single-shot lifetime equal to one seq loop, and binds the seq so the element
 * animates per-tick. */
static void
app_world_spawn_spotanim_now(
    struct App* app,
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

    spot = CacheProvider_SpotanimtypeGet(app->provider, spotanim_id);
    if( !spot )
    {
        fprintf(stderr, "spawn_spotanim: spotanim %d not resident\n", spotanim_id);
        return;
    }

    model = app_world_build_spotanim_model(app, spot);
    if( !model )
    {
        fprintf(stderr, "spawn_spotanim: spotanim %d model %d failed\n", spotanim_id, spot->model);
        return;
    }

    world_x = tile_x * 128 + 64;
    world_z = tile_z * 128 + 64;
    /* Reference y = getAvH(x,z) - height; world y is negative-up so subtracting
     * raises the effect above the ground by `height`. */
    world_y = app_world_height(app, world_x, world_z, level) - height;

    element_id = app_world_scene_element_create(app, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return;

    lifetime = app_seq_total_duration(app, spot->seq);

    World_SpotanimSpawn(
        app->world, element_id, level, world_x, world_z, world_y, 0, delay, lifetime);
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

    fprintf(
        stderr,
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
app_plugin_object_at(struct App* app, int handle)
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

/* The model ids this object's source needs resident before it can be built:
 * a CACHE object names its model directly, a SPOTANIM object names it through
 * the spotanimtype. Returns -1 when it is not knowable yet. */
static int
app_plugin_object_model_id(struct App* app, struct AppPluginObject const* obj)
{
    assert(app);
    assert(obj);

    if( obj->source == TORIRS_PLUGIN_MODEL_SPOTANIM )
    {
        struct ToriRS_Spotanimtype* spot =
            obj->model_id >= 0 ? CacheProvider_SpotanimtypeGet(app->provider, obj->model_id) : NULL;
        return spot ? spot->model : -1;
    }
    return obj->model_id;
}

/* The sequence the object should play: the plugin's if it named one, else the
 * spotanimtype's own. -1 = no animation. */
static int
app_plugin_object_seq_id(struct App* app, struct AppPluginObject const* obj)
{
    assert(app);
    assert(obj);

    if( obj->seq_id >= 0 )
        return obj->seq_id;
    if( obj->source == TORIRS_PLUGIN_MODEL_SPOTANIM )
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
app_plugin_object_build_model(struct App* app, struct AppPluginObject const* obj)
{
    struct ToriRS_Spotanimtype const* spot = NULL;
    struct ToriDraw_Model* model;
    int model_id;
    int retextured = 0;

    assert(app);
    assert(obj);

    if( obj->source == TORIRS_PLUGIN_MODEL_SPOTANIM )
        spot = obj->model_id >= 0 ? CacheProvider_SpotanimtypeGet(app->provider, obj->model_id)
                                  : NULL;
    model_id = app_plugin_object_model_id(app, obj);
    if( model_id < 0 )
        return NULL;

    {
        struct ToriRS_Model* rs = CacheProvider_ModelGet(app->provider, model_id);
        model = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
    }
    if( !model )
        return NULL;

    /* The spotanimtype's own recolours first, so the plugin's pairs are stated
     * against the colours it can actually see on the finished graphic. */
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
app_plugin_object_teardown(struct App* app, struct AppPluginObject* obj)
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
    *out_world_y =
        app_world_height(app, *out_world_x, *out_world_z, obj->level) - obj->height;
    return 1;
}

/* Build the element and hand it to World. SYNCHRONOUS -- the model and seq
 * must be resident. */
static void
app_plugin_object_materialize_now(struct App* app, int handle)
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

    element_id = app_world_scene_element_create(app, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return;

    obj->element_id = element_id;
    obj->built_source = obj->source;
    obj->built_model_id = obj->model_id;
    obj->built_recolor_stamp = app_plugin_object_recolor_stamp(obj);
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
    const struct Task_AppSpawn* self)
{
    int idx;
    struct WorldEntity_Scenery* sc;

    assert(app);
    assert(self);
    if( self->loc_id < 0 )
        return; /* a pure delete has no placement to describe */
    idx = World_SceneryFindAt(app->world, self->tile_x, self->tile_z, self->level, self->loc_shape);
    if( idx < 0 )
        return; /* the spawn was refused (unknown loc, off-scene) — nothing to dress */
    sc = World_EntityPoolGet(&app->world->entities.scenery, idx);
    if( !sc )
        return;

    for( int i = 0; i < 5; i++ )
    {
        if( (self->loc_op_flags & (1 << i)) == 0 )
        {
            sc->actions[i].name[0] = '\0';
            continue;
        }
        if( self->loc_ops[i][0] == '\0' )
            continue;
        snprintf(sc->actions[i].name, sizeof(sc->actions[i].name), "%s", self->loc_ops[i]);
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
            int idx =
                app_world_spawn_npc_now(
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
            App_WorldObjStackAdd(app, self->tile_x, self->tile_z, self->level, self->obj_id, 1);
        }
        else
            fprintf(stderr, "spawn_obj: obj %d has no inventory model\n", self->obj_id);
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
        app_world_spawn_spotanim_now(
            app,
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
        app_world_spawn_projectile_spot_now(
            app,
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
                (obj && obj->source == TORIRS_PLUGIN_MODEL_SPOTANIM) ? obj->model_id : -1;
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
        app_world_scenery_anim_apply(
            app, self->tile_x, self->tile_z, self->level, self->loc_shape, self->seq_id);
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
            PT_TASK_AWAITSELF_IF(CreateTask_LocLoad(app->provider, self->loc_id));
            for( self->model_i = 0;; self->model_i++ )
            {
                {
                    struct ToriRS_Location* cfg =
                        CacheProvider_LocationGet(app->provider, self->loc_id);
                    int entries = 0;
                    if( cfg && cfg->models && cfg->lengths )
                        entries = cfg->shapes ? cfg->shapes_and_model_count : 1;
                    if( self->model_i >= entries )
                        break;
                }
                for( self->loc_model_j = 0;; self->loc_model_j++ )
                {
                    {
                        struct ToriRS_Location* cfg =
                            CacheProvider_LocationGet(app->provider, self->loc_id);
                        if( !cfg || self->loc_model_j >= cfg->lengths[self->model_i] )
                            break;
                        self->model_id = cfg->models[self->model_i][self->loc_model_j];
                    }
                    if( self->model_id >= 0 )
                        PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
                }
            }
            {
                struct ToriRS_Location* cfg =
                    CacheProvider_LocationGet(app->provider, self->loc_id);
                self->seq_id = cfg ? cfg->seq_id : -1;
            }
            if( self->seq_id >= 0 )
                PT_TASK_AWAITSELF_IF(
                    CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id));
        }
        if( app->world_builder && app->world && app->world->load_complete )
        {
            int old_type = -1;
            int old_angle = 0;
            int old_shape = -1;
            int old_idx = World_SceneryFindAt(
                app->world, self->tile_x, self->tile_z, self->level, self->loc_shape);
            if( old_idx >= 0 )
            {
                struct WorldEntity_Scenery* old =
                    World_EntityPoolGet(&app->world->entities.scenery, old_idx);
                if( old )
                {
                    old_type = old->loc_id;
                    old_angle = old->angle;
                    old_shape = old->shape;
                }
            }
            World_LocChangePush(
                app->world,
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
                app->world->cycle,
                -1);
            WorldBuilder_ApplyLocChange(
                app->world_builder,
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
            app_loc_change_apply_ops(app, self);
            app_sync_textures(app);
            app->need_redraw = 1;
        }
    }
    else
    {
        PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_id));
        app_world_spawn_projectile_now(
            app,
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
    else if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
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
                fprintf(
                    stderr,
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
        else if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
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
        else if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
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
        if( node->u.rs_model.gamecache_model_id != scene_id || node->u.rs_model.xan != 150 ||
            node->u.rs_model.yan != yan || node->u.rs_model.zan != 0 ||
            node->u.rs_model.anim_frame != seq_frame || node->u.rs_model.anim_seq_id != seq_id )
        {
            UITree_MarkNodeDirty(app->tree, i);
            app->need_redraw = 1;
        }
        node->u.rs_model.gamecache_model_id = scene_id;
        node->u.rs_model.xan = 150;
        node->u.rs_model.yan = yan;
        node->u.rs_model.zan = 0;
        node->u.rs_model.anim_seq_id = seq_id;
        /* Frame comes from the entity, so anim_hold is what keeps
         * UITreeAnim_Advance from running a second, independent clock on it. */
        node->u.rs_model.anim_hold = 1;
        node->u.rs_model.anim_frame = seq_frame;
        node->u.rs_model.anim_frame_cycle = 0;
        bound = 1;
    }

    if( changed && getenv("TORIRS_ANIM_DEBUG") )
        fprintf(
            stderr,
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
app_plugin_object_sync(struct App* app, int handle)
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
         obj->built_recolor_stamp != app_plugin_object_recolor_stamp(obj)) )
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
        struct Task_AppSpawn* task =
            app_spawn_task_new(app, APP_SPAWN_PLUGIN_OBJECT, 0, 0, 0);
        task->plugin_object = handle;
        ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
        return;
    }

    /* Live: position, level and visibility are applied to the entity. */
    ToriDraw_SceneElementSetPosition(app->scene, obj->element_id, world_x, world_y, world_z, 0);
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
        obj->load_pending = 0;
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
app_plugin_asset_read(void* user, char const* plugin, char const* name)
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
app_plugin_asset_write(void* user, char const* plugin, char const* name, void const* data, int size)
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
        fprintf(
            stderr,
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
static int
app_plugin_screenshot(void* user, char const* plugin, char const* dir, char const* name)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(plugin);
    assert(name);

    for( int i = 0; i < APP_PLUGIN_SCREENSHOTS_MAX; i++ )
    {
        struct AppPluginScreenshot* shot = &app->plugin_screenshots[i];
        size_t len;

        if( shot->in_use )
            continue;

        shot->in_use = 1;
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
        return 1;
    }

    fprintf(
        stderr,
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
app_capture_fallback_render(struct App* app, int* pixels, int width, int height)
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
app_plugin_screenshots_write(struct App* app, int const* pixels, int width, int height)
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
        char path[TORIRS_IOITEM_MAX_PATH];

        if( !shot->in_use )
            continue;
        shot->in_use = 0;

        /*
         * An absolute destination is the user's own folder and is used as
         * given. A relative one -- and that includes the common case of no
         * destination at all -- lands under the plugin's saved-asset
         * directory, so "Bob/Levels" sorts a browser run's captures the same
         * way it sorts a desktop one. The browser lane has no path to name;
         * without this it would have no way to organise them either.
         */
        if( shot->dir[0] == '/' )
            snprintf(path, sizeof(path), "%s/%s", shot->dir, shot->name);
        else
        {
            char base[TORIRS_IOITEM_MAX_PATH];

            app_plugin_asset_saved_path(app, shot->plugin, "", base, sizeof(base));
            if( base[0] && shot->dir[0] )
            {
                /* asset_saved_path ends in the trailing separator plus the
                 * empty name it was handed, so the slash is already there. */
                snprintf(path, sizeof(path), "%s%s/%s", base, shot->dir, shot->name);
            }
            else if( base[0] )
                snprintf(path, sizeof(path), "%s%s", base, shot->name);
            else
                path[0] = '\0';
        }

        if( !path[0] )
        {
            /* Same refusal as app_plugin_asset_write, for the same reason: a
             * client told not to write files must not start writing them
             * because a plugin asked. */
            fprintf(
                stderr,
                "plugin: %s cannot save screenshot '%s'; plugin persistence is off for "
                "this run (TORIRS_PLUGIN_PREFS is empty), so there is no folder to put it "
                "under and the destination is not an absolute path\n",
                shot->plugin,
                shot->name);
            continue;
        }
        ToriRS_TaskQueue_Add(
            app->runner.queue, CreateTask_PluginAssetWrite(path, png, (int)png_size));
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
app_dispatch_game_event(struct App* app, struct RS_GameEvent const* ev)
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
        obj->source = TORIRS_PLUGIN_MODEL_CACHE;
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
    fprintf(
        stderr,
        "plugin: world-object table full (%d); object_create refused\n",
        APP_PLUGIN_OBJECTS_MAX);
    return -1;
}

static void
app_plugin_object_destroy(void* user, int handle)
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
app_plugin_object_set_model(void* user, int handle, int source, int id)
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
app_plugin_object_recolor(void* user, int handle, int hsl_from, int hsl_to)
{
    struct App* app = (struct App*)user;
    struct AppPluginObject* obj;

    assert(app);
    obj = app_plugin_object_at(app, handle);
    if( !obj )
        return;
    if( obj->recolor_count >= TORIRS_PLUGIN_OBJECT_RECOLORS_MAX )
    {
        fprintf(
            stderr,
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
app_plugin_object_clear_recolors(void* user, int handle)
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
app_plugin_object_set_anim(void* user, int handle, int seq_id, int loop)
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
app_plugin_object_set_light(void* user, int handle, int ambient, int contrast)
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
app_plugin_object_set_active(void* user, int handle, int active)
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
app_plugin_object_ready(void* user, int handle)
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

/* Read one non-negative integer from an action's comma-separated `a=` payload
 * (`id=…`, `height=…`, and so on). Malformed/absent values keep the default. */
static int
app_spawn_arg(
    int builtin,
    char const* args,
    char const* arg_name,
    char const* env_name)
{
    char const* env;
    int value = builtin;
    size_t name_len;

    assert(arg_name && env_name);
    name_len = strlen(arg_name);
    while( args && *args )
    {
        char const* end = strchr(args, ',');
        size_t len = end ? (size_t)(end - args) : strlen(args);
        if( len > name_len + 1 && strncmp(args, arg_name, name_len) == 0 && args[name_len] == '=' )
        {
            char* parsed_end = NULL;
            long parsed = strtol(args + name_len + 1, &parsed_end, 0);
            if( parsed_end != args + name_len + 1 && parsed >= 0 && parsed_end == args + len )
                value = (int)parsed;
        }
        args = end ? end + 1 : NULL;
    }
    env = getenv(env_name);
    if( env )
        value = (int)strtol(env, NULL, 0);
    return value;
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
    task->npc_id = app_spawn_arg(3106 /* OSRS-era "Man" */, args, "id", "TORIRS_SPAWN_NPC");
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
    task->obj_id = app_spawn_arg(
        1265 /* bronze pickaxe: named, with ground ops */, args, "id", "TORIRS_SPAWN_OBJ");
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
    int old_type = -1;
    int old_angle = 0;
    int old_shape = shape;
    int idx;

    assert(app);
    if( !app->world || !app->world->load_complete )
        return;

    idx = World_SceneryFindAt(app->world, scene_x, scene_z, level, shape);
    if( idx >= 0 )
    {
        struct WorldEntity_Scenery* old = World_EntityPoolGet(&app->world->entities.scenery, idx);
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
        app->world,
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
        player = World_PlayerGetByServerPid(app->world, player_pid);
    if( player )
    {
        player->loc_start_cycle = app->world->cycle + start_cycle;
        player->loc_stop_cycle = app->world->cycle + end_cycle;
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
    int spotanim_id = app_spawn_arg(
        74 /* a small, visible default effect */, args, "id", "TORIRS_SPAWN_SPOTANIM");
    int height = app_spawn_arg(92, args, "height", "TORIRS_SPAWN_SPOTANIM_HEIGHT");
    int delay = app_spawn_arg(0, args, "delay", "TORIRS_SPAWN_SPOTANIM_DELAY");
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
        fprintf(stderr, "spawn_projectile: source latched at %d,%d\n", tile_x, tile_z);
        return;
    }
    if( app->proj_src_tile_x == tile_x && app->proj_src_tile_z == tile_z )
    {
        app->proj_src_tile_x = -1;
        app->proj_src_tile_z = -1;
        fprintf(stderr, "spawn_projectile: latch cleared\n");
        return;
    }

    task = app_spawn_task_new(app, APP_SPAWN_PROJECTILE, tile_x, tile_z, level);
    task->model_id = app_spawn_arg(
        3081 /* v1 spawn-test spotanim model */, args, "model", "TORIRS_SPAWN_PROJ_MODEL");
    task->seq_id = app_spawn_arg(
        659 /* v1 spawn-test spotanim sequence (RUNESCAPE_PROJECTILE_SEQ_ID) */,
        args,
        "seq",
        "TORIRS_SPAWN_PROJ_SEQ");
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
    int spotanim_id = app_spawn_arg(74, args, "id", "TORIRS_SPAWN_SPOTANIM");
    int height = app_spawn_arg(92, args, "height", "TORIRS_SPAWN_SPOTANIM_HEIGHT");
    int delay = app_spawn_arg(0, args, "delay", "TORIRS_SPAWN_SPOTANIM_DELAY");

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
    if( !app->world_active || !app->world_view_valid )
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
/* The level cutscene heights are measured against (reference minusedlevel). */
static int
app_cinema_level(struct App* app)
{
    int world_idx;
    struct WorldEntity_Player* player;

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
    if( pitch < 128 )
        pitch = 128;
    else if( pitch > 383 )
        pitch = 383;

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
        printf(
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
 * viewport HEIGHT, exactly the way the projection scale is (app_world_proj_scale
 * / class159.method5357), and over the same `height - 334` in [0,100] band:
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
    int inv_pitch, inv_yaw;
    int off_x, off_y, off_z;

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
     * yaw is 0..2047, pitch 128..383 (the same range the middle-button drag
     * allows), zoom is the follow distance as a percentage.
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
                app->orbit_pitch = cam_pitch < 128 ? 128 : (cam_pitch > 383 ? 383 : cam_pitch);
                app->orbit_pitch_vel = 0;
            }
            if( cam_zoom > 0 )
                app->world_zoom_pct = cam_zoom;
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
    if( app->orbit_x - (float)target_x < -500.0f || app->orbit_x - (float)target_x > 500.0f ||
        app->orbit_z - (float)target_z < -500.0f || app->orbit_z - (float)target_z > 500.0f )
    {
        app->orbit_x = (float)target_x;
        app->orbit_z = (float)target_z;
    }
    else
    {
        app->orbit_x += ((float)target_x - app->orbit_x) / 16.0f;
        app->orbit_z += ((float)target_z - app->orbit_z) / 16.0f;
    }

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
    app->orbit_pitch += app->orbit_pitch_vel / 2;
    if( app->orbit_pitch < 128 )
        app->orbit_pitch = 128;
    if( app->orbit_pitch > 383 )
        app->orbit_pitch = 383;

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
        if( clamp > 98048 )
            clamp = 98048;
        if( clamp < 32768 )
            clamp = 32768;
        if( clamp > app->camera_pitch_clamp )
            app->camera_pitch_clamp += (clamp - app->camera_pitch_clamp) / 24;
        else if( clamp < app->camera_pitch_clamp )
            app->camera_pitch_clamp += (clamp - app->camera_pitch_clamp) / 80;
    }

    pitch = app->orbit_pitch;
    if( app->camera_pitch_clamp / 256 > pitch )
        pitch = app->camera_pitch_clamp / 256;
    yaw = app->orbit_yaw & 0x7ff;
    /* Reference distance is `(pitch * 3 + 600) * viewportZoom / 256`
     * (client.method2068); our own wheel zoom scales that. At 100% (the
     * default, and the only value the reference has) the last term is exact. */
    distance = (pitch * 3 + 600) * app_world_cam_dist_zoom(app) / 256;
    distance = distance * app->world_zoom_pct / APP_WORLD_ZOOM_DEFAULT_PCT;
    off_x = 0;
    off_y = 0;
    off_z = distance;

    /* Look-at height: the reference samples the ground under the ACTOR (not
     * under the eased anchor), takes the minimum over its footprint, then
     * drops 8, then the camera's own 50 — client.method1605:
     *   var14 = method1569(wv, actor.x, actor.z, level, footprintSize) - 8
     *   field753 = var14 - field999          (field999 defaults to 50)
     * method1569 degenerates to a single method1812 sample for a size-1
     * footprint, which the local player always has. */
    target_y = app_world_height(app, target_x, target_z, player->grid_position.level) - 8 - 50;
    target_x = (int)app->orbit_x;
    target_z = (int)app->orbit_z;

    inv_pitch = (2048 - pitch) & 0x7ff;
    inv_yaw = (2048 - yaw) & 0x7ff;
    if( inv_pitch != 0 )
    {
        int sin = ToriDraw_Sin(inv_pitch);
        int cos = ToriDraw_Cos(inv_pitch);
        int tmp = (off_y * cos - distance * sin) >> 16;
        off_z = (off_y * sin + distance * cos) >> 16;
        off_y = tmp;
    }
    if( inv_yaw != 0 )
    {
        int sin = ToriDraw_Sin(inv_yaw);
        int cos = ToriDraw_Cos(inv_yaw);
        int tmp = (off_z * sin + off_x * cos) >> 16;
        off_z = (off_z * cos - off_x * sin) >> 16;
        off_x = tmp;
    }

    app->world_camera_pos.x = target_x - off_x;
    app->world_camera_pos.y = target_y - off_y;
    app->world_camera_pos.z = target_z - off_z;
    app->world_camera.pitch = pitch;
    app->world_camera.yaw = yaw;

    /* TORIRS_ORBIT_DEBUG: the anchor's residual against the player it follows.
     * The orbit point is what the whole eye is built around, and an offset one
     * is invisible in a still frame — it only shows as the model swinging in a
     * circle while you rotate, which reads as "the camera orbits the tile".
     * This is the number that says whether it does: settled, it must go to 0. */
    if( getenv("TORIRS_ORBIT_DEBUG") )
        fprintf(
            stderr,
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
 * elements across ClearPool(STATIC) and climb the scene id high-water mark. */
void
App_WorldDrainEntityRemoved(struct App* app)
{
    struct World* world;
    int count;

    assert(app);
    world = app->world;
    if( !world )
        return;

    count = World_EventsCount(world);
    for( int i = 0; i < count; i++ )
    {
        const struct World_Event* ev = World_EventsPeek(world, i);
        if( ev->kind == WorldEventKind_EntityRemoved && ev->element_id >= 0 )
        {
            /*
             * Ahead of the element being freed, so a plugin's last look at the
             * npc is a whole one.
             *
             * The pool entry may already be gone by the time this drain runs
             * -- the event exists to clean up the RENDER side after the world
             * side has let go. When it is, the snapshot carries the element id
             * and nothing else, which is still enough for a plugin to drop
             * per-element state; nothing is invented to fill the gap.
             */
            if( app->plugins )
            {
                struct WorldEntity_NPC* going =
                    World_NpcGetByElementId(world, ev->element_id, NULL);
                struct ToriRS_PluginNpcSnap snap;
                if( going )
                    app_plugin_fill_npc(app, going, &snap);
                else
                {
                    memset(&snap, 0, sizeof(snap));
                    snap.server_slot = -1;
                    snap.npc_id = -1;
                    snap.base_npc_id = -1;
                    snap.element_id = ev->element_id;
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

static void
app_world_frame(
    struct App* app,
    int cycles,
    float frame_cycles)
{
    struct World* world = app->world;

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
    World_Cycle(world, cycles);
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
    if( app->world && getenv("TORIRS_ELEMENT_ALIAS_CHECK") )
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
                    fprintf(
                        stderr,
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
    if( cycles > 0 )
    {
        struct ToriDraw_TextureState* tex_state = ToriDraw_SceneTexState(app->scene);
        if( tex_state )
            ToriDraw_TextureMapAnimate(&tex_state->texture_map, cycles);
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

/* Snapshot the armed use/target selection for the minimenu builder (reference
 * useMode/targetMode). objsel and targetsel are mutually exclusive — arming one
 * clears the other. */
static struct RS_MinimenuSelection
app_minimenu_selection(struct App const* app)
{
    struct RS_MinimenuSelection sel = { .mode = RS_MINIMENU_SELECT_NONE };
    if( app->objsel.active )
    {
        sel.mode = RS_MINIMENU_SELECT_USE_ITEM;
        snprintf(sel.obj_name, sizeof(sel.obj_name), "%s", app->objsel.name);
        sel.obj_slot = app->objsel.slot;
        sel.obj_com_id = app->objsel.component_id;
    }
    else if( app->targetsel.active )
    {
        sel.mode = RS_MINIMENU_SELECT_TARGET;
        snprintf(sel.target_op, sizeof(sel.target_op), "%s", app->targetsel.op);
        sel.target_mask = app->targetsel.mask;
        sel.target_mask_held_bit = app->features ? app->features->target_mask_held : 0;
    }
    return sel;
}

/*
 * Mouseover text, rebuilt every frame from a scratch menu at the pointer.
 *
 * This is the client half of what the reference gets from the cache: script
 * 4726 (re-armed each cycle by 4725) bails on minimenu_isopen, reads
 * minimenu_entry / _numops / _type, and has proc 4727 draw one line. Building
 * the same menu the right click would show is exactly what those opcodes
 * report, so both the line drawn here and the CS2 snapshot come from one pass.
 */
static void
app_hover_text_update(
    struct App* app,
    int mouse_x,
    int mouse_y)
{
    struct UIMinimenu scratch;
    char prev[UITREE_HOVERTEXT_LEN];
    bool const prev_visible = app->hover_text.visible;
    int click_in_world;

    snprintf(prev, sizeof(prev), "%s", app->hover_text.text);

    /* 4726's first gate: no hover line while the Choose Option popup is up. */
    if( app->interact.minimenu.visible || mouse_x < 0 || mouse_y < 0 ||
        mouse_x >= UITREE_LAYOUT_ROOT_W || mouse_y >= UITREE_LAYOUT_ROOT_H )
    {
        app->hover_text.visible = false;
        app->hover_text.text[0] = '\0';
    }
    else
    {
        click_in_world = app_world_mouse_gate(app, mouse_x, mouse_y) && app_world_drawable(app);
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
                .selection = app_minimenu_selection(app),
                .player_ops = (char const(*)[40])app->player_ops,
                .player_ops_primary = app->player_ops_primary,
                .player_attack_option = app->player_attack_option,
                .npc_attack_option = app->npc_attack_option,
                .world = app->world,
                /* Same rule the click paths use: world rows only when the
                 * pointer is over bare viewport. */
                .world_pickset = click_in_world ? &app->world_pickset : NULL,
                .click_in_world = click_in_world != 0,
                .locedit_active = app->locedit_visible != 0,
                .mapedit_select_active = app_mapedit_select_active(app),
            };
            UIMinimenu_Reset(&scratch);
            scratch.font_id = app->hover_text.font_id;
            RS_Minimenu_Build(&mctx, mouse_x, mouse_y, &scratch);
            app_plugin_menu_build(app, &scratch, 1);
        }
        UIHoverText_Compose(&scratch, &app->hover_text);
    }

    /* Anchor at the world viewport's top-left (4726's container origin), or
     * the canvas when no viewport is on screen. */
    if( app->world_view_valid )
    {
        app->hover_text.x = app->world_emit_desc.clip.x + APP_HOVERTEXT_INSET_X;
        app->hover_text.y = app->world_emit_desc.clip.y + APP_HOVERTEXT_INSET_Y;
        app->hover_text.w = app->world_emit_desc.clip.w - APP_HOVERTEXT_INSET_X;
    }
    else
    {
        app->hover_text.x = APP_HOVERTEXT_INSET_X;
        app->hover_text.y = APP_HOVERTEXT_INSET_Y;
        app->hover_text.w = UITREE_LAYOUT_ROOT_W - APP_HOVERTEXT_INSET_X;
    }

    if( app->hover_text.visible != prev_visible || strcmp(app->hover_text.text, prev) != 0 )
        app->need_redraw = 1;
}

/*
 * Give "Walk here" a destination when this click's pickset holds no terrain.
 *
 * OFF unless features->ground_click_offmap_nearest says otherwise, because the
 * reference has no such destination to give: no ground triangle contained the
 * point, so nothing was recorded and nothing is sent. See the field.
 *
 * Only the click paths call this: resolving the tile sweeps the scene, and the
 * hover-text builder runs the same menu every frame for a row whose text does
 * not depend on the tile. A pickset that DOES hold terrain is left alone — the
 * row targets the picked tile there, exactly as before.
 */
static void
app_minimenu_ctx_ground_fallback(
    struct App* app,
    struct RS_MinimenuBuildCtx* mctx,
    int click_x,
    int click_y)
{
    int x = 0, z = 0, level = 0;

    if( !app->features->ground_click_offmap_nearest )
        return;
    if( !mctx->click_in_world || !mctx->world_pickset )
        return;
    for( int i = 0; i < mctx->world_pickset->count; i++ )
        if( mctx->world_pickset->items[i].type == WORLD_PICK_TERRAIN )
            return;
    if( !app_world_nearest_ground_tile(app, click_x, click_y, &x, &z, &level) )
        return;
    mctx->ground_fallback_valid = true;
    mctx->ground_fallback_x = x;
    mctx->ground_fallback_z = z;
    mctx->ground_fallback_level = level;
}

/* ------------------------------------------------------------- client ops --
 *
 * The rows the CACHE installed with CLIENTOP_* (6700..6709): "Mark tile" on a
 * tile, "Tag" and "Tag-All" on an npc, "Lookup" on a player. Client-side rows
 * that run a clientscript and are never sent to a server. See rs_clientop.h.
 */

/*
 * Held SHIFT is what makes them appear, and the cache says so rather than this
 * client deciding it: setting 112's own description is "When enabled, hold
 * shift and right-click the ground to place highlights". Nothing in the
 * installing scripts tests a key, so the gate is the client's to apply — and
 * without it every right click on the ground would carry a "Mark tile" row
 * above "Walk here".
 */
static bool
app_clientop_armed(struct App const* app)
{
    assert(app);
    if( !app->plugin_input )
        return false;
    return LibToriRS_Input_IsKeyHeld(app->plugin_input, TORIRSK_SHIFT);
}

/** Append every installed op of `kind`, labelled against `subject` (which may
 *  be NULL or empty for a tile, which names nothing). */
static void
app_clientop_add_rows(
    struct App* app,
    struct UIMinimenu* menu,
    enum RS_ClientOpKind kind,
    char const* subject,
    struct UIMinimenuPick pick)
{
    char text[UITREE_MINIMENU_OPTION_LEN];

    assert(app);
    assert(menu);

    for( int slot = 0; slot < RS_CLIENTOP_SLOT_MAX; slot++ )
    {
        struct RS_ClientOpSlot const* op = RS_ClientOpGet(&app->host.clientop, kind, slot);
        if( !op )
            continue;

        /* "Tag @whi@Goblin", the shape every other targeted row uses; a tile
         * names nothing, so its row is the bare label. */
        if( subject && subject[0] )
            snprintf(text, sizeof(text), "%s @whi@%s", op->label, subject);
        else
            snprintf(text, sizeof(text), "%s", op->label);

        UIMinimenu_AddOption(
            menu, text, RS_MINIMENU_ACTION_CLIENTOP,
            RS_MINIMENU_CLIENTOP_INDEX(kind, slot), pick);
    }
}

/*
 * Add the client-op rows to a freshly built menu.
 *
 * Driven off the ROWS the builder produced rather than off the pickset, for
 * the same reason the npc-highlight plugin is: the rows are what the menu is
 * about, and a pick the builder decided not to offer (a non-interactive wall,
 * an npc filtered out by the attack setting) is not something the user can act
 * on and must not grow an op either.
 *
 * The tile row is the exception -- it hangs off the TERRAIN pick, which is the
 * "Walk here" row, and that row is offered even when it is inert.
 */
static void
app_clientop_menu_build(struct App* app, struct UIMinimenu* menu, int hover_pass)
{
    bool seen[RS_CLIENTOP_KIND_COUNT] = { false, false, false, false, false };
    int const count = menu->option_count;

    assert(app);
    assert(menu);

    /* The hover pass runs every frame to compose the top-left readout; a row
     * added there would be offered as the LEFT-click action. */
    if( hover_pass || !app->world || !app_clientop_armed(app) )
        return;

    for( int i = 0; i < count; i++ )
    {
        struct UIMinimenuOption const* opt = &menu->options[i];
        enum RS_ClientOpKind kind;
        char const* subject = NULL;

        switch( opt->pick.kind )
        {
        case UI_MINIMENU_PICK_NPC:
        {
            struct WorldEntity_NPC* npc =
                World_NpcGetByElementId(app->world, opt->pick.id, NULL);
            if( !npc )
                continue;
            kind = RS_CLIENTOP_NPC;
            subject = npc->name;
            break;
        }
        case UI_MINIMENU_PICK_SCENERY:
        {
            struct WorldEntity_Scenery* loc =
                World_SceneryGetByElementId(app->world, opt->pick.id);
            if( !loc )
                continue;
            kind = RS_CLIENTOP_LOC;
            subject = loc->name;
            break;
        }
        case UI_MINIMENU_PICK_OBJ:
        {
            struct WorldEntity_ObjStack* stack =
                World_ObjStackGetByElementId(app->world, opt->pick.id);
            if( !stack )
                continue;
            kind = RS_CLIENTOP_OBJ;
            subject = stack->name;
            break;
        }
        case UI_MINIMENU_PICK_PLAYER:
        {
            struct WorldEntity_Player* player =
                World_PlayerGetByElementId(app->world, opt->pick.id);
            if( !player )
                continue;
            kind = RS_CLIENTOP_PLAYER;
            subject = player->name;
            break;
        }
        case UI_MINIMENU_PICK_TERRAIN:
            /* An inert Walk here row (a click on the sky) carries no tile, and
             * a "Mark tile" over nothing would mark the corner of the map. */
            if( opt->pick.secondary_id < 0 )
                continue;
            kind = RS_CLIENTOP_TILE;
            break;
        default:
            continue;
        }

        /* One set of rows per KIND, not per row: an npc with five ops already
         * has five rows in this menu, and adding "Tag" beside each of them
         * would offer the same op five times. */
        if( seen[kind] )
            continue;
        seen[kind] = true;
        app_clientop_add_rows(app, menu, kind, subject, opt->pick);
    }
}

/*
 * Run a client op: record what it is about, then queue its script.
 *
 * NOT a begin/run/clear bracket. RS_CS2_RunScript queues a task; the script
 * runs during the frame's settle, well below this call, so clearing on the way
 * out would land before it ever started and every context op would read -1 --
 * which is exactly what the first version of this did.
 *
 * The context is scoped by the script's IDENTITY instead, and left standing
 * afterwards: only a root frame of `op->script_id` can read it. See
 * RS_ClientOpContext::script_id.
 */
static int
app_clientop_run(struct App* app, struct UIMinimenuOption const* opt)
{
    struct RS_ClientOpContext ctx;
    struct RS_ClientOpSlot const* op;
    int const kind = RS_MINIMENU_CLIENTOP_KIND(opt->action_index);
    int const slot = RS_MINIMENU_CLIENTOP_SLOT(opt->action_index);
    int base_x;
    int base_z;

    assert(app);
    assert(opt);

    if( !app->world )
        return 1;
    op = RS_ClientOpGet(&app->host.clientop, (enum RS_ClientOpKind)kind, slot);
    if( !op )
        return 1;

    base_x = app->world->_base_tile_x;
    base_z = app->world->_base_tile_z;

    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = kind;
    ctx.uid = -1;
    ctx.type = -1;
    ctx.coord = -1;

    switch( (enum RS_ClientOpKind)kind )
    {
    case RS_CLIENTOP_NPC:
    {
        struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt->pick.id, NULL);
        if( !npc )
            return 1;
        /* The uid IS the server slot here -- see RS_ClientOpContext::uid for
         * why that is allowed to differ from the reference's. */
        ctx.uid = npc->server_slot;
        ctx.type = npc->npc_id;
        ctx.coord = RS_CLIENTOP_COORD(
            npc->grid_position.level,
            base_x + npc->grid_position.x,
            base_z + npc->grid_position.z);
        snprintf(ctx.name, sizeof(ctx.name), "%s", npc->name);
        break;
    }
    case RS_CLIENTOP_LOC:
    {
        struct WorldEntity_Scenery* loc = World_SceneryGetByElementId(app->world, opt->pick.id);
        if( !loc )
            return 1;
        ctx.type = loc->loc_id;
        ctx.coord = RS_CLIENTOP_COORD(
            loc->grid_position.level,
            base_x + loc->grid_position.x,
            base_z + loc->grid_position.z);
        snprintf(ctx.name, sizeof(ctx.name), "%s", loc->name);
        break;
    }
    case RS_CLIENTOP_OBJ:
    {
        struct WorldEntity_ObjStack* stack =
            World_ObjStackGetByElementId(app->world, opt->pick.id);
        if( !stack )
            return 1;
        ctx.type = stack->obj_id;
        ctx.coord = RS_CLIENTOP_COORD(
            stack->grid_position.level,
            base_x + stack->grid_position.x,
            base_z + stack->grid_position.z);
        snprintf(ctx.name, sizeof(ctx.name), "%s", stack->name);
        break;
    }
    case RS_CLIENTOP_PLAYER:
    {
        struct WorldEntity_Player* player =
            World_PlayerGetByElementId(app->world, opt->pick.id);
        if( !player )
            return 1;
        ctx.coord = RS_CLIENTOP_COORD(
            player->grid_position.level,
            base_x + player->grid_position.x,
            base_z + player->grid_position.z);
        snprintf(ctx.name, sizeof(ctx.name), "%s", player->name);
        break;
    }
    case RS_CLIENTOP_TILE:
        /* A TERRAIN pick carries scene tile x/z and the level, in
         * secondary/tertiary/quaternary -- see the Walk here row. */
        ctx.coord = RS_CLIENTOP_COORD(
            opt->pick.quaternary_id,
            base_x + opt->pick.secondary_id,
            base_z + opt->pick.tertiary_id);
        break;
    default:
        return 1;
    }

    if( getenv("TORIRS_CLIENTOP_DEBUG") )
        fprintf(
            stderr,
            "clientop: %s slot %d '%s' -> script %d (uid=%d type=%d coord=%d '%s')\n",
            RS_ClientOpKindName((enum RS_ClientOpKind)kind),
            slot,
            op->label,
            op->script_id,
            ctx.uid,
            ctx.type,
            ctx.coord,
            ctx.name);

    ctx.script_id = op->script_id;
    RS_ClientOpContextBegin(&app->host.clientop, &ctx);
    RS_CS2_RunScript(&app->host, &app->runner, op->script_id, NULL, 0, 0, NULL, 0);
    return 1;
}

/* Build + show the minimenu for a right click (reference openMenu: width from
 * the widest row, centered on the click, clamped to the canvas). The tree
 * node stays unpositioned — emit and the interact gesture read the model. */
static void
app_minimenu_open(
    struct App* app,
    int click_x,
    int click_y,
    int click_in_world)
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
        .selection = app_minimenu_selection(app),
        .player_ops = (char const(*)[40])app->player_ops,
        .player_ops_primary = app->player_ops_primary,
        .player_attack_option = app->player_attack_option,
        .npc_attack_option = app->npc_attack_option,
        .world = app->world,
        .world_pickset = &app->world_pickset,
        .click_in_world = click_in_world != 0,
        .locedit_active = app->locedit_visible != 0,
        .mapedit_select_active = app_mapedit_select_active(app),
    };
    struct UIMinimenu* menu = &app->interact.minimenu;
    struct UIMinimenuLayout layout;
    int content_w = 0;
    int line_box = 0;

    app_minimenu_ctx_ground_fallback(app, &mctx, click_x, click_y);
    RS_Minimenu_Build(&mctx, click_x, click_y, menu);
    /* Before the plugins, so their re-sort covers these rows too -- the client
     * ops are the cache's, and a plugin adding a row after them must not leave
     * the menu half-sorted. */
    app_clientop_menu_build(app, menu, 0);
    app_plugin_menu_build(app, menu, 0);

    /* TORIRS_MINIMENU_DEBUG=1: the world pickset that fed the rows plus every
     * row built from it — the one place to see why a loc/obj came up bare. */
    if( getenv("TORIRS_MINIMENU_DEBUG") )
    {
        /* The two Attack options ride this dump because a missing or
         * right-click-only Attack row is otherwise indistinguishable from a
         * pick that never happened — and Hidden is what both settings hold
         * until the server transmits varp clientcode 18/22. The local combat
         * level rides it for the same reason: under "Depends on combat levels"
         * the setting alone does not say whether a row was sunk, the
         * comparison against THIS number does. */
        {
            struct WorldEntity_Player* lp =
                app->world ? World_PlayerGetByServerPid(app->world, app->world->local_pid) : NULL;
            fprintf(
                stderr,
                "minimenu: open at %d,%d in_world=%d picks=%d attackopt player=%d npc=%d "
                "mylevel=%d\n",
                click_x,
                click_y,
                click_in_world,
                click_in_world ? app->world_pickset.count : 0,
                app->player_attack_option,
                app->npc_attack_option,
                lp ? lp->combat_level : -1);
        }
        if( click_in_world )
            for( int i = 0; i < app->world_pickset.count; i++ )
            {
                struct World_Picked const* picked = &app->world_pickset.items[i];
                fprintf(
                    stderr,
                    "  pick[%d] type=%d element=%d tile=%d,%d,%d\n",
                    i,
                    (int)picked->type,
                    picked->element_id,
                    picked->tile_x,
                    picked->tile_z,
                    picked->tile_level);
            }
        for( int i = 0; i < menu->option_count; i++ )
            fprintf(
                stderr,
                "  row[%d] action=%d op=%d kind=%d id=%d \"%s\"\n",
                i,
                menu->options[i].action,
                menu->options[i].action_index,
                (int)menu->options[i].pick.kind,
                menu->options[i].pick.id,
                menu->options[i].text);
    }

    /* The popup is sized from measured text, so a font the scene cannot hand
     * back is not a cosmetic miss: PrepareShow falls back to a per-character
     * estimate and the rows draw past the border. The boot-time resolve can
     * land before the b12 load does, so re-resolve on a miss and carry the id
     * to the node that draws the rows — measure and draw must share a font. */
    {
        struct ToriDraw_Font* font = ToriDraw_SceneFontGet(app->scene, menu->font_id);
        if( !font )
        {
            int const resolved = app_minimenu_font_scene_id(app);
            if( resolved > 0 )
            {
                menu->font_id = resolved;
                for( uint32_t i = 0; i < app->tree->component_count; i++ )
                    if( !app->tree->components[i].freed &&
                        app->tree->components[i].type == UIELEM_BUILTIN_MINIMENU )
                    {
                        app->tree->components[i].u.minimenu.font_id = resolved;
                        break;
                    }
                font = ToriDraw_SceneFontGet(app->scene, resolved);
            }
        }
        if( font )
            line_box = ToriDraw_FontLineBoxHeight(font);
    }
    if( UIMinimenu_PrepareShow(menu, line_box, app_measure_text_cb, app, &layout, &content_w) )
    {
        UIMinimenu_ShowAt(
            menu, layout, content_w, click_x, click_y, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        app->need_redraw = 1;
        /* Geometry beside the rows: a popup too narrow for its own text is a
         * measure that returned nothing, and this line is what says so. */
        if( getenv("TORIRS_MINIMENU_DEBUG") )
            fprintf(
                stderr,
                "minimenu: font=%d line_box=%d content_w=%d width=%d height=%d\n",
                menu->font_id,
                line_box,
                content_w,
                menu->width,
                menu->height);
    }
}

/* IF3 inventory paint (`interface_inv_draw_slot_big`) installs
 * `cc_setonop(cc_settrans_temporarily(...))` — the modern stand-in for
 * Client-TS `selectedArea` flash (transPlotSprite 128 for ~15 cycles). That
 * flash is for Wear/Drop/INV_BUTTON/IF_BUTTON only: `OPHELDT_START` ("Use")
 * arms useMode with a white outline and must stay opaque (Client.ts:9183 /
 * TYPE_INV draw 9700 vs selectedArea branch 9752). */
static void
app_inv_cell_op_flash(
    struct App* app,
    int com_id,
    int slot,
    int op_index)
{
    int32_t idx = -1;
    int hook_com_id = -1;
    struct UITreeRuntimeScriptHook const* hook;
    struct UITreeRuntimeScriptHook hook_copy;

    assert(app);
    if( !app->tree )
        return;
    if( !UITree_ObjCellDynamicAtSlot(app->tree, com_id, slot, &idx, NULL, NULL) )
        idx = UITree_FindByComponentId(app->tree, com_id);
    if( idx < 0 )
        return;
    hook = UITree_ResolveClickHook(app->tree, idx, &hook_com_id);
    if( !hook || hook->script_id <= 0 )
        return;
    /* A SNAPSHOT, and it has to be a deep one: the dispatch below can run
     * scripts that rewrite this very hook, and a slot's arguments are owned
     * allocations now, so the old `hook_copy = *hook` aliased tails the
     * dispatch could free underneath it. */
    UITree_HookInitCopy(&hook_copy, hook);
    RS_CS2_SetEventOp(&app->host, op_index > 0 ? op_index : 1, 0);
    RS_CS2_DispatchHook(&app->host, &app->runner, hook_com_id, &hook_copy);
    RS_CS2_SetEventOp(&app->host, 1, 0);
    UITree_HookClear(&hook_copy);
}

/* Target-mode visuals are script-owned. The spellbook registers
 * IF_SETONTARGETENTER/LEAVE on every targetable spell; those hooks set outline
 * 2 (white) while armed and restore outline 0 when the selection ends. */
static void
app_targetsel_dispatch_hook(
    struct App* app,
    int entering)
{
    int32_t idx;
    struct UITreeRuntimeScriptHook hook;

    assert(app);
    if( !app->tree || app->targetsel.component_id < 0 )
        return;
    idx = UITree_FindByComponentId(app->tree, app->targetsel.component_id);
    if( idx < 0 )
        return;
    hook = entering ? UITree_Hooks(&app->tree->components[idx])->on_target_enter
                    : UITree_Hooks(&app->tree->components[idx])->on_target_leave;
    RS_CS2_DispatchHook(&app->host, &app->runner, app->targetsel.component_id, &hook);
}

static void
app_targetsel_clear(struct App* app)
{
    assert(app);
    if( !app->targetsel.active )
        return;
    app_targetsel_dispatch_hook(app, 0);
    app->targetsel.active = 0;
    app->need_redraw = 1;
}

/* Disarm every "armed" selection at once — the "Use <item> with..." pick and
 * the spell/target pick.
 *
 * The reference clears both together at the tail of doAction (Client.ts:9506),
 * so anything that ends a selection ends BOTH; the two modes are mutually
 * exclusive at arm time and there is no path that should retire one while
 * leaving the other lit. Every "clicked off" site funnels through here so a new
 * armed mode added later cannot be forgotten at one of them. Returns nonzero
 * when something was actually armed. */
static int
app_selection_clear(struct App* app)
{
    int was_armed;

    assert(app);
    was_armed = app->objsel.active || app->targetsel.active;
    if( !was_armed )
        return 0;
    /* TORIRS_CLICK_DEBUG=1: which armed mode a click retired. "Use is stuck" and
     * "Use was never armed" produce the same screen, and the outline is script
     * -painted, so the state itself has to say so. */
    if( getenv("TORIRS_CLICK_DEBUG") )
        fprintf(stderr, "selclear: obj=%d tgt=%d\n", app->objsel.active, app->targetsel.active);
    app->objsel.active = 0;
    app_targetsel_clear(app);
    app->need_redraw = 1;
    return 1;
}

/* Execute one selected (or defaulted) menu row: cross feedback + hook
 * dispatch with the row's op index (v1 ui_click_use_minimenu_option). The
 * cross colour comes from the action alone (RS_Minimenu_CrossModeForAction,
 * reference doAction) and is decided once: an action the reference gives no
 * cross — UI buttons, inventory ops, Examine, Cancel — leaves a cross already
 * in flight running rather than clearing or recolouring it. Returns nonzero
 * when a CS2 hook was dispatched. */
/* Send the held-item / component op for the current inventory pick. Returns 1
 * when a use-mode selection or examine consumed the click without a packet. */
static int
app_minimenu_inv_action(
    struct App* app,
    struct UIMinimenuOption const* opt)
{
    int obj_id = opt->pick.tertiary_id;
    int slot = opt->pick.secondary_id;
    int com_id = opt->pick.id;

    /*
     * "Use <held> with <this item>" / "<spell> <this item>": the previous click
     * armed objsel or targetsel and this one completes it (reference OPHELDU /
     * OPHELDT).
     *
     * NO `app_inv_cell_op_flash` on either. Client-TS does set selectedArea for
     * both (Client.ts:9299/9322), but our IF3 stand-in for that flash is to
     * dispatch the cell's own on_op with an op index — and these two rows are
     * not component ops, so there is no index to report and passing 1 fabricates
     * one. On rev-239's backpack op 1 is a LIVE op: the inventory slot builder
     * puts the shift-click-drop handler there (script 6014, see
     * `shift-click-drop-chain`), and it answers by naming the real Drop op
     * through `cc_triggerop`. So every use-on between two carried items sent its
     * OPHELDU and then an IF_BUTTON7 that dropped the target on the floor —
     * "use knife on logs" put the logs on the ground. The flash is cosmetic; the
     * op it fabricated was not. The `OPHELDT_START` arming case below already
     * skips the flash for the same reason.
     */
    if( app->objsel.active )
    {
        APP_NET_SEND(
            app,
            net_out_opheldu(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                obj_id,
                slot,
                com_id,
                app->objsel.obj_id,
                app->objsel.slot,
                app->objsel.component_id));
        app_selection_clear(app);
        return 1;
    }

    if( opt->action == REVCONFIG_MINIMENU_TGT_HELD && app->targetsel.active )
    {
        APP_NET_SEND(
            app,
            net_out_opheldt(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                obj_id,
                slot,
                com_id,
                app->targetsel.component_id));
        app_selection_clear(app);
        return 1;
    }

    switch( opt->action )
    {
    case REVCONFIG_MINIMENU_OPHELD1:
    case REVCONFIG_MINIMENU_OPHELD2:
    case REVCONFIG_MINIMENU_OPHELD3:
    case REVCONFIG_MINIMENU_OPHELD4:
    case REVCONFIG_MINIMENU_OPHELD5:
        APP_NET_SEND(
            app,
            net_out_opheld(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                opt->action_index + 1,
                obj_id,
                slot,
                com_id));
        /* selectedArea / cc_settrans_temporarily — not for Use (below). */
        app_inv_cell_op_flash(app, com_id, slot, opt->action_index + 1);
        return 1;
    case REVCONFIG_MINIMENU_INV_BUTTON1:
    case REVCONFIG_MINIMENU_INV_BUTTON2:
    case REVCONFIG_MINIMENU_INV_BUTTON3:
    case REVCONFIG_MINIMENU_INV_BUTTON4:
    case REVCONFIG_MINIMENU_INV_BUTTON5:
        APP_NET_SEND(
            app,
            net_out_inv_button(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                opt->action_index + 1,
                obj_id,
                slot,
                com_id));
        app_inv_cell_op_flash(app, com_id, slot, opt->action_index + 1);
        return 1;
    case REVCONFIG_MINIMENU_IF_BUTTON:
        /* Component ops 1..10 on an inventory cell (bank withdraw ladder,
         * farming tools, backpack, …). Rev239's collapsed IF_BUTTONX also
         * carries the object id; without it the server cannot distinguish a
         * held-item operation from a plain component click. method3476 sends
         * only when the effective op bit is armed, though on_op still runs. */
        if( opt->action_index < 0 || opt->action_index >= 10 )
            return 0;
        {
            unsigned events = 0;
            int const op_num = opt->action_index + 1;
            if( !app_if_events_override_get(app, com_id, slot, &events) )
                events = App_IfEventsGetEffective(app, com_id);
            if( events & (1u << op_num) )
            {
                APP_NET_SEND(
                    app,
                    net_out_if_button_obj_op(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        op_num,
                        com_id,
                        slot,
                        obj_id));
            }
        }
        app_inv_cell_op_flash(app, com_id, slot, opt->action_index + 1);
        return 1;
    case REVCONFIG_MINIMENU_OPHELDT_START:
    {
        /* "Use <item>": enter selection mode; the next click targets it.
         * No selectedArea / settrans flash — white outline only. */
        struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
        app_targetsel_clear(app);
        app->objsel.active = 1;
        app->objsel.obj_id = obj_id;
        app->objsel.slot = slot;
        app->objsel.component_id = com_id;
        /* Precision caps the copy at sizeof(objsel.name)-1 so this is
         * provably in-bounds regardless of obj->name's declared size, rather
         * than relying on snprintf's implicit (GCC-unprovable) truncation. */
        snprintf(
            app->objsel.name,
            sizeof(app->objsel.name),
            "%.*s",
            (int)sizeof(app->objsel.name) - 1,
            obj && obj->name[0] ? obj->name : "item");
        if( getenv("TORIRS_CLICK_DEBUG") )
            fprintf(stderr, "selarm: obj '%s' slot=%d com=0x%x\n", app->objsel.name, slot, com_id);
        app->need_redraw = 1;
        return 1;
    }
    case REVCONFIG_MINIMENU_OPHELD6:
    {
        /* Examine: client-side chat print (no packet). Reference doAction
         * OP_HELD6: huge stacks show the exact count, else the config desc,
         * else the generic fallback. */
        struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
        char const* name = (obj && obj->name[0]) ? obj->name : "item";
        int count = opt->pick.quaternary_id;
        char line[TORIRS_DESC_MAX + 32];
        if( count >= 100000 )
            snprintf(line, sizeof(line), "%d x %s", count, name);
        else if( obj && obj->desc[0] )
            snprintf(line, sizeof(line), "%s", obj->desc);
        else
            snprintf(line, sizeof(line), "It's a %s.", name);
        RS_Chat_AddMessage(&app->chat, RS_CHAT_TYPE_GAME, NULL, line);
        app->need_redraw = 1;
        return 1;
    }
    default:
        return 0;
    }
}

#include "ui/uitree_inv_view.h"
#include "ui/uitree_scroll.h"

/* Filled item cell under a canvas point, of either shape the tree can express
 * one in — a TYPE_INV grid slot or a rev-230 CS2 item child. Resolution and
 * the obj lookup both live in UITree_ObjCellForNode / InvManager so the drag
 * machine and the right-click builder cannot disagree about which slot was
 * pressed. Returns false when the point is over no filled cell. */
static bool
app_obj_cell_at(
    struct App* app,
    int px,
    int py,
    struct UITreeObjCell* out)
{
    if( !UITree_ObjCellAt(app->tree, &app->ui_host, px, py, out) )
        return false;
    if( out->kind == UITREE_OBJ_CELL_GRID )
    {
        struct InvSlot inv_slot;
        if( !InvManager_GetSlot(&app->invs, out->inv_source_id, out->slot, &inv_slot) )
            return false;
        if( inv_slot.obj_id <= 0 )
            return false;
        out->obj_id = inv_slot.obj_id;
        out->obj_count = inv_slot.obj_count;
    }
    /* IF_SETEVENTS is the rev-230 authority for drag / drop / Use-target bits
     * (deob method5697). Without this, CS2 cells kept a hardcoded can_drag=1. */
    UITree_ObjCellApplyEvents(out, App_IfEventsGetAt(app, out->component_id, out->slot));
    return true;
}

static int
app_minimenu_run_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y);

static int
app_minimenu_use_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y);

/* Run the default (top) menu row for a click at (x, y) — the reference
 * doAction(menuNumEntries - 1) short-click path. UI rows only (no world
 * pickset): used by the inventory slot machine below, where the click is by
 * construction over a component. */
static void
app_run_default_ui_row(
    struct App* app,
    int click_x,
    int click_y)
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
        .world = app->world,
        .world_pickset = NULL,
        .click_in_world = false,
        .locedit_active = app->locedit_visible != 0,
        .mapedit_select_active = app_mapedit_select_active(app),
    };
    struct UIMinimenu scratch;
    int default_idx;

    mctx.selection = app_minimenu_selection(app);
    UIMinimenu_Reset(&scratch);
    scratch.font_id = app->interact.minimenu.font_id;
    RS_Minimenu_Build(&mctx, click_x, click_y, &scratch);
    app_plugin_menu_build(app, &scratch, 0);
    default_idx = RS_Minimenu_DefaultOptionIndex(&scratch);
    /*
     * TORIRS_CLICK_DEBUG=1: the same readout the generic left-click path
     * prints, for the clicks that never reach it.
     *
     * A click on a FILLED item cell is claimed by the drag machine and resolved
     * here instead, so `clickdbg` never sees the one case where the armed
     * selection decides the answer — a spell aimed at an inventory item. The
     * selection is printed with the rows because the difference between "no
     * cast row was built" and "the cast row was built but Wear outranked it" is
     * the whole question, and neither is visible from the outcome.
     */
    if( getenv("TORIRS_CLICK_DEBUG") )
    {
        fprintf(
            stderr,
            "invclick: at %d,%d rows=%d default=%d selmode=%d mask=0x%x\n",
            click_x,
            click_y,
            scratch.option_count,
            default_idx,
            (int)mctx.selection.mode,
            (unsigned)mctx.selection.target_mask);
        for( int i = 0; i < scratch.option_count; i++ )
            fprintf(
                stderr,
                "  row[%d] '%s' action=%d\n",
                i,
                scratch.options[i].text,
                scratch.options[i].action);
    }
    if( default_idx >= 0 )
    {
        struct UIMinimenu saved = app->interact.minimenu;
        app->interact.minimenu = scratch;
        app_minimenu_use_option(app, default_idx, click_x, click_y);
        app->interact.minimenu = saved;
    }
    else
        /* Cancel-only menu on a FILLED cell, which is what clicking the armed
         * item itself produces: add_inv_slot_select_row skips the arming slot,
         * and while a mode is armed the plain ops are not built, so the menu has
         * no row and nothing ran. The reference still runs doAction on that
         * Cancel row and its tail drops useMode/targetMode — clicking the lit
         * item again is how you cancel. Without this it could never be
         * un-selected by clicking it. */
        app_selection_clear(app);
}

/*
 * A real drag was released at (mx, my): find the destination slot — any
 * container that IF_SETEVENTS armed as a drag target (bit 20), not only the
 * source — and tell the server (deob field804 / method1239 IfButtonD).
 *
 * An empty slot is a legal drop target and is exactly what a filled-cell
 * hit-test cannot see: a TYPE_INV grid has no obj there, and rev 230's paint
 * script hides the child of an empty cell outright. Filled cells go through
 * UITree_ObjCellAt; empty CS2 slots walk each drag-target parent's children.
 *
 * CS1 / dat1 (2004 Client.ts): optimistic InvManager swap then classic
 * INV_BUTTOND with a mode byte (same-container only). CS2 / rev-230 runs
 * onDragComplete synchronously before class108.method3759 sends the dual-
 * endpoint IfButtonD packet. Inventory hooks such as bankside_reorder and
 * interface_inv_dragcomplete_swap_big repaint the two cells from the unchanged
 * local container, making the gesture optimistic without mutating server state.
 */
static int
app_inv_resolve_drop(
    struct App* app,
    int mouse_x,
    int mouse_y,
    int* out_com,
    int* out_slot,
    int* out_obj,
    int32_t* out_node)
{
    struct UITreeObjCell dest;
    int i;

    *out_com = -1;
    *out_slot = -1;
    *out_obj = -1;
    *out_node = -1;

    /* Filled cell under the cursor (any container). */
    if( UITree_ObjCellAt(app->tree, &app->ui_host, mouse_x, mouse_y, &dest) )
    {
        int events = App_IfEventsGetAt(app, dest.component_id, dest.slot);
        UITree_ObjCellApplyEvents(&dest, events);
        /* CS1 / no events: can_drop stays 1 from the grid default. */
        if( dest.can_drop )
        {
            *out_com = dest.component_id;
            *out_slot = dest.slot;
            *out_node = dest.node_index;
            if( dest.kind == UITREE_OBJ_CELL_GRID )
            {
                struct InvSlot inv_slot;
                if( InvManager_GetSlot(&app->invs, dest.inv_source_id, dest.slot, &inv_slot) &&
                    inv_slot.obj_id > 0 )
                    *out_obj = inv_slot.obj_id;
            }
            else if( dest.obj_id > 0 )
            {
                *out_obj = dest.obj_id;
            }
            return 1;
        }
    }

    /* Empty CS2 slots: walk every IF_SETEVENTS entry with bit 20. */
    for( i = 0; i < app->if_event_count; i++ )
    {
        int com = app->if_events[i].com_id;
        int events = app->if_events[i].events;
        int32_t node = -1;
        int slot;

        if( (events & UITREE_FLAG_DRAG_ON) == 0 )
            continue;
        slot = UITree_ObjCellDynamicSlotNodeAt(app->tree, com, mouse_x, mouse_y, &node);
        if( slot < 0 )
            continue;
        /* from/to of -1 mean a plain widget (not a sub-id range). */
        if( app->if_events[i].from >= 0 && app->if_events[i].to >= 0 &&
            (slot < app->if_events[i].from || slot > app->if_events[i].to) )
            continue;
        *out_com = com;
        *out_slot = slot;
        *out_node = node;
        if( node >= 0 && app->tree->components[node].item_id > 0 )
            *out_obj = app->tree->components[node].item_id;
        return 1;
    }
    return 0;
}

static void
app_inv_drag_drop(
    struct App* app,
    int mouse_x,
    int mouse_y)
{
    int to_slot = -1;
    int src_obj = -1;
    int dst_obj = -1;
    int dst_com = -1;
    int32_t src_node = -1;
    int32_t dst_node = -1;

    if( app->inv_drag_source_id >= 0 )
    {
        struct InvSlot inv_slot;
        if( InvManager_GetSlot(
                &app->invs, app->inv_drag_source_id, app->inv_drag_from_slot, &inv_slot) &&
            inv_slot.obj_id > 0 )
            src_obj = inv_slot.obj_id;
    }
    else
    {
        int obj = 0;
        if( UITree_ObjCellDynamicAtSlot(
                app->tree, app->inv_drag_com_id, app->inv_drag_from_slot, &src_node, &obj, NULL) &&
            obj > 0 )
            src_obj = obj;
    }

    if( !app_inv_resolve_drop(app, mouse_x, mouse_y, &dst_com, &to_slot, &dst_obj, &dst_node) )
        return;

    /* Same cell: not a drop. Cross-container with the same slot index is fine. */
    if( dst_com == app->inv_drag_com_id && to_slot == app->inv_drag_from_slot )
        return;

    if( App_UiLogic(app) == APP_UI_LOGIC_CS1 )
    {
        /* 2004 Client.ts: apply locally so the drag feels instant; the
         * server's UPDATE_INV echo repaints either way. Same-container only —
         * cross-container waits on the server. */
        if( dst_com == app->inv_drag_com_id )
        {
            if( app->inv_drag_source_id >= 0 )
            {
                InvManager_SwapSlots(
                    &app->invs, app->inv_drag_source_id, app->inv_drag_from_slot, to_slot);
                RS_CS2Host_NotifyInvChanged(
                    &app->host, InvManager_ContainerForSource(&app->invs, app->inv_drag_source_id));
            }
            else
            {
                UITree_ObjCellDynamicSwap(
                    app->tree, app->inv_drag_com_id, app->inv_drag_from_slot, to_slot);
            }
        }
        APP_NET_SEND(
            app,
            net_out_inv_buttond(
                app->net->rev,
                app->net->random_out,
                _nsbuf,
                sizeof(_nsbuf),
                app->inv_drag_com_id,
                src_obj,
                app->inv_drag_from_slot,
                dst_com,
                dst_obj,
                to_slot,
                0));
        return;
    }

    /* Rev-230: onDragComplete first, then the dual-endpoint packet. The
     * gamepack invokes this ScriptEvent synchronously; use an isolated queue so
     * an unrelated yielding clientscript cannot delay the optimistic redraw
     * until after the server's UPDATE_INV echo. */
    {
        int hook_com = app->inv_drag_com_id;
        struct UITreeRuntimeScriptHook const* hook = NULL;
        int32_t hook_idx = src_node;
        int bx = 0, by = 0, bw = 0, bh = 0;
        int offx = 0, offy = 0;
        int32_t parent_idx;

        if( hook_idx < 0 )
            hook_idx = UITree_FindByComponentId(app->tree, app->inv_drag_com_id);
        if( hook_idx >= 0 )
        {
            hook = &UITree_Hooks(&app->tree->components[hook_idx])->on_drag_complete;
            hook_com = app->tree->components[hook_idx].component_id;
            if( hook->script_id <= 0 )
            {
                /* Fall back to the container: some paint scripts put the hook
                 * on the parent layer rather than every CC_CREATE child. */
                parent_idx = UITree_FindByComponentId(app->tree, app->inv_drag_com_id);
                if( parent_idx >= 0 )
                {
                    hook = &UITree_Hooks(&app->tree->components[parent_idx])->on_drag_complete;
                    hook_com = app->inv_drag_com_id;
                }
            }
        }

        parent_idx = UITree_FindByComponentId(app->tree, app->inv_drag_com_id);
        if( parent_idx >= 0 )
        {
            UITree_LayoutGetBounds(&app->tree->components[parent_idx].position, &bx, &by, &bw, &bh);
            UITree_AccumScrollOffset(app->tree, parent_idx, &offx, &offy);
        }

        if( hook && hook->script_id > 0 )
        {
            struct UITreeRuntimeScriptHook hook_copy;
            struct ToriRS_TaskQueue* drag_queue = ToriRS_TaskQueue_New();
            struct TaskRunner drag_runner = {
                .queue = drag_queue,
                .io = app->runner.io,
                .px = app->runner.px,
            };
            int target_id = dst_com;
            UITree_HookInitCopy(&hook_copy, hook);
            if( dst_node >= 0 )
                target_id = app->tree->components[dst_node].component_id;
            RS_CS2_SetEventOp(&app->host, 1, 0);
            RS_CS2_SetEventMouse(&app->host, mouse_x - (bx - offx), mouse_y - (by - offy));
            RS_CS2_SetEventDragTarget(&app->host, app->tree, target_id);
            RS_CS2_DispatchHook(&app->host, &drag_runner, hook_com, &hook_copy);
            TaskRunner_Drain(&drag_runner);
            ToriRS_TaskQueue_Free(drag_queue);
            UITree_HookClear(&hook_copy);
            app->need_redraw = 1;
        }
    }

    APP_NET_SEND(
        app,
        net_out_inv_buttond(
            app->net->rev,
            app->net->random_out,
            _nsbuf,
            sizeof(_nsbuf),
            app->inv_drag_com_id,
            src_obj,
            app->inv_drag_from_slot,
            dst_com,
            dst_obj,
            to_slot,
            0));
}

/* A press has become a real drag once it clears the deadzone and the dead
 * time — the same test the release branch uses to choose swap over click, so
 * the ghost and the swap can never disagree about what a gesture was. */
static int
app_inv_drag_promoted(struct App const* app)
{
    int dead_time = app->inv_drag_dead_time > 0 ? app->inv_drag_dead_time : 5;
    return app->inv_drag_can_drag && app->inv_drag_threshold && app->inv_drag_cycles >= dead_time;
}

/*
 * Whether emit should ghost the armed slot at trans 128.
 *
 * From the *press*, not from the promotion. The reference sets `objDragArea`
 * on the mouse-down that arms the cell and the inventory draw fades that one
 * icon for as long as it is non-zero (Client.ts:8589 arms, :10207 fades); the
 * deadzone and the dead-time only zero the (dx, dy) the faded icon is drawn
 * at, so a press that never moves still fades in place and un-fades on
 * release. Waiting for promotion here is what "items don't dim when the left
 * button is down" was.
 *
 * Gated on `can_drag` for the same reason the reference gates arming on
 * `com.objSwap || com.objReplace`: a container that cannot be rearranged shows
 * no drag feedback at all. At IF3 that flag is the IF_SETEVENTS drag depth, so
 * a worn slot stays solid while a backpack or bank cell fades.
 */
static int
app_inv_drag_ghosting(struct App const* app)
{
    if( app->inv_drag_com_id < 0 )
        return 0;
    if( App_UiLogic(app) == APP_UI_LOGIC_CS1 )
        return 1;
    return app->inv_drag_can_drag;
}

/* Inventory slot press/drag/click (reference objDrag* machine, Client.ts
 * mouseLoop 8584 + gameLoop 2476 for CS1; rev-230 Deobfuscator class415 for
 * CS2):
 *  - left press over a FILLED slot arms; nothing fires on the down edge.
 *  - each held cycle: cycles++, >5px of travel sets the grab threshold, and
 *    the emit offset dx/dy is recomputed (+-5px deadzone, zero before 5
 *    cycles) so a promoted drag icon follows the mouse at trans 128.
 *  - release: real drag (app_inv_drag_promoted) resolves the slot under
 *    the mouse — CS1 does an optimistic local swap + classic INV_BUTTOND;
 *    CS2 fires onDragComplete then the dual-endpoint IfButtonD and waits
 *    for the server echo (no local item mutation). Anything else is a
 *    SHORT CLICK and runs the default menu row (how a left click submits
 *    OPHELD*).
 * Ghosting: from arm time on both logics, for any cell the interface says may
 * be rearranged — see app_inv_drag_ghosting. While armed the generic node drag is suppressed
 * (tree->anti_drag; the reference freezes mouseLoop/buildMinimenu during
 * objDragArea != 0) so the grid's ancestors can never pick the press up as
 * a whole-panel drag. */
static void
app_inv_drag_tick(
    struct App* app,
    struct LibToriRS_Input* input,
    int pointer_consumed)
{
    int mx = input->curr.mouse_x;
    int my = input->curr.mouse_y;

    /* Never arm while the right-click popup is open: its option rows overlap
     * the grid and the reference routes those clicks through the menu first. */
    if( app->inv_drag_com_id < 0 && !pointer_consumed && !app->interact.minimenu.visible &&
        LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) )
    {
        struct UITreeObjCell cell;
        if( app_obj_cell_at(app, mx, my, &cell) )
        {
            struct UITreeComponent const* node =
                (cell.node_index >= 0 && (uint32_t)cell.node_index < app->tree->component_count)
                    ? &app->tree->components[cell.node_index]
                    : NULL;
            app->inv_drag_com_id = cell.component_id;
            app->inv_drag_can_drag = cell.can_drag;
            app->inv_drag_from_slot = cell.slot;
            app->inv_drag_source_id = cell.inv_source_id;
            app->inv_drag_cycles = 0;
            app->inv_drag_grab_x = mx;
            app->inv_drag_grab_y = my;
            app->inv_drag_threshold = 0;
            app->inv_drag_dead_zone = (node && node->drag_dead_zone) ? node->drag_dead_zone : 5;
            app->inv_drag_dead_time = (node && node->drag_dead_time) ? node->drag_dead_time : 5;
            app->inv_drag_dx = 0;
            app->inv_drag_dy = 0;
            /* Both logics fade the armed cell on the down edge, so the frame
             * that arms is a frame that changed. */
            app->need_redraw = 1;
        }
    }

    if( app->inv_drag_com_id < 0 )
    {
        if( app->tree )
            app->tree->anti_drag = 0;
        return;
    }
    if( app->tree )
        app->tree->anti_drag = 1;

    if( !input->curr.mouse_button_up[TORIRSM_LEFT] &&
        LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT) )
    {
        int dx = mx - app->inv_drag_grab_x;
        int dy = my - app->inv_drag_grab_y;
        int zone = app->inv_drag_dead_zone > 0 ? app->inv_drag_dead_zone : 5;
        int dead_time = app->inv_drag_dead_time > 0 ? app->inv_drag_dead_time : 5;
        int was_promoted;

        /* Non-draggable (IF_SETEVENTS drag-depth 0): the press still counts as
         * a click on release, but it never promotes to a drag. */
        if( !app->inv_drag_can_drag )
            return;

        was_promoted = app_inv_drag_promoted(app);
        app->inv_drag_cycles++;
        if( dx > zone || dx < -zone || dy > zone || dy < -zone )
            app->inv_drag_threshold = 1;

        /* Visual offset: reference zeroes each axis inside the dead zone and
         * both until the dead-time cycles pass. */
        if( dx < zone && dx > -zone )
            dx = 0;
        if( dy < zone && dy > -zone )
            dy = 0;
        if( app->inv_drag_cycles < dead_time )
        {
            dx = 0;
            dy = 0;
        }
        if( dx != app->inv_drag_dx || dy != app->inv_drag_dy ||
            (!was_promoted && app_inv_drag_promoted(app)) )
        {
            app->inv_drag_dx = dx;
            app->inv_drag_dy = dy;
            app->need_redraw = 1;
        }
        return;
    }

    /* Released. */
    if( app_inv_drag_promoted(app) )
        app_inv_drag_drop(app, mx, my);
    else
    {
        app_run_default_ui_row(app, mx, my);
    }
    app->inv_drag_com_id = -1;
    app->inv_drag_dx = 0;
    app->inv_drag_dy = 0;
    if( app->tree )
        app->tree->anti_drag = 0;
    app->need_redraw = 1;
}

static int
app_minimenu_run_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y)
{
    struct UIMinimenu* menu = &app->interact.minimenu;
    struct UIMinimenuOption opt;

    if( option_index < 0 || option_index >= menu->option_count )
        return 0;
    opt = menu->options[option_index];
    UIMinimenu_Hide(menu);
    app->need_redraw = 1;

    /* Reference doAction has no CANCEL branch at all: dismissing the menu is
     * not an interaction and must not disturb a running cross. */
    if( opt.action == REVCONFIG_MINIMENU_CANCEL )
        return 0;

    {
        /*
         * Every op row paints its cross here, unconditionally, exactly like
         * the reference (Client-TS doAction sets crossMode = 2 in each branch
         * before the packet goes out).
         *
         * A WALK row does NOT. The reference's walk branch touches no cross at
         * all — it only re-arms the hittest — and the cross is set one frame
         * later, inside the block that emits the move, so a walk that resolves
         * to nothing leaves the screen alone. Deob client.java:9305 puts the
         * two in the same basic block, guarded by class112.method3951():
         *
         *     field760 = ...; field714 = ...;   // cross x, y
         *     field800 = 1282583357;            // crossMode 1
         *     field910 = 0;                     // crossCycle
         *
         * Painting it up front here is what made a dead click on the sky look
         * like an executed one. UI_MINIMENU_PICK_TERRAIN shows it on success.
         */
        enum UICrossMode cross_mode = RS_Minimenu_CrossModeForAction(opt.action);
        if( cross_mode != UI_CROSS_OFF && cross_mode != UI_CROSS_WALK )
            UICross_Show(&app->cross, cross_mode, click_x, click_y);
    }

    /* method5229 marks component operations above targetPriority with the
     * +2000 low-priority form. doAction removes that bias before comparing
     * the action id; keeping it made the row render correctly but bypass every
     * inventory/UI dispatch case (for example Deposit-1 arrived as 2231
     * instead of IF_BUTTON 231). */
    opt.action = UIMinimenu_ActionNormalize(opt.action);

    /*
     * The plugins get the row before the engine acts on it.
     *
     * After the cross and the normalize, so a handler sees the action id the
     * dispatcher below will switch on; before every branch of that dispatch,
     * so CONSUME actually suppresses the behaviour rather than racing it. For
     * a plugin-owned row there is no engine behaviour to fall through to and
     * this always returns 1 -- which is what makes menu_add a complete feature
     * rather than a row that draws and does nothing.
     */
    if( app->plugins )
    {
        struct ToriRS_PluginMenuRow row;
        memset(&row, 0, sizeof(row));
        row.text = opt.text;
        row.action = opt.action;
        row.pick_kind = (int)opt.pick.kind;
        row.npc_slot = -1;
        row.player_pid = -1;
        row.target_id = -1;
        if( opt.pick.kind == UI_MINIMENU_PICK_NPC && app->world )
        {
            struct WorldEntity_NPC* npc =
                World_NpcGetByElementId(app->world, opt.pick.id, NULL);
            if( npc )
                row.npc_slot = npc->server_slot;
            row.target_id = opt.pick.secondary_id;
        }
        else if( opt.pick.kind == UI_MINIMENU_PICK_PLAYER )
            row.player_pid = opt.pick.secondary_id;
        else if(
            opt.pick.kind == UI_MINIMENU_PICK_SCENERY || opt.pick.kind == UI_MINIMENU_PICK_OBJ )
            row.target_id = opt.pick.secondary_id;

        if( PluginHost_MenuSelect(app->plugins, &row, click_x, click_y) )
            return 1;
    }

    /* A cache-installed client op. After the plugins, so a plugin may still
     * veto one; before every engine branch, because there is no engine
     * behaviour behind this action id to fall through to. */
    if( opt.action == RS_MINIMENU_ACTION_CLIENTOP )
        return app_clientop_run(app, &opt);

    /*
     * Examine (OPLOC6/OPNPC6/OPOBJ6): resolved locally from the config desc, no
     * packet (Client-TS doAction OP_LOC6/OP_NPC6/OP_OBJ6). Look the desc up from
     * the config by the entity's type id, and fall back to "It's a <name>."
     * when the config carries none. Must run before the pick.kind switch or the
     * scenery/NPC cases would mis-send OPLOC1/OPNPC1.
     *
     * Where the text comes from is worth stating, because the two halves differ.
     * An OBJ record states its own examine in every era. An npc's and a loc's
     * were retired from Jagex's configs in 2006 -- rev-239 OldSchool sends those
     * two from the server (deob: menu ops 1002/1004/1013 each build a packet and
     * print what comes back), so `cache.osrs239` states not one of them. This
     * client answers Examine off the record, so the text is carried in the
     * content pack instead, under the same opcode 3 the field has always had:
     * tools/import_examine.py writes configs/examine.{npc,loc} and the bake puts
     * them in the npc and loc archives. A pristine cache therefore still shows
     * the fallback, and a content-baked one does not.
     */
    if( opt.action == REVCONFIG_MINIMENU_OPLOC6 || opt.action == REVCONFIG_MINIMENU_OPNPC6 ||
        opt.action == REVCONFIG_MINIMENU_OPOBJ6 )
    {
        char const* name = NULL;
        char const* desc = NULL;
        char line[TORIRS_DESC_MAX + 32];
        if( app->world && opt.action == REVCONFIG_MINIMENU_OPLOC6 )
        {
            struct WorldEntity_Scenery* scenery =
                World_SceneryGetByElementId(app->world, opt.pick.id);
            if( scenery )
            {
                struct ToriRS_Location* loc =
                    CacheProvider_LocationGet(app->provider, scenery->loc_id);
                if( loc && loc->desc[0] )
                    desc = loc->desc;
                if( scenery->name[0] )
                    name = scenery->name;
            }
        }
        else if( app->world && opt.action == REVCONFIG_MINIMENU_OPOBJ6 )
        {
            struct WorldEntity_ObjStack* stack =
                World_ObjStackGetByElementId(app->world, opt.pick.id);
            if( stack )
            {
                struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, stack->obj_id);
                if( obj && obj->desc[0] )
                    desc = obj->desc;
                if( stack->name[0] )
                    name = stack->name;
            }
        }
        else if( app->world )
        {
            struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt.pick.id, NULL);
            if( npc )
            {
                struct ToriRS_Npctype* npctype =
                    CacheProvider_NpctypeGet(app->provider, npc->npc_id);
                if( npctype && npctype->desc[0] )
                    desc = npctype->desc;
                if( npc->name[0] )
                    name = npc->name;
            }
        }
        if( desc )
            snprintf(line, sizeof(line), "%s", desc);
        else
            snprintf(line, sizeof(line), "It's a %s.", name ? name : "mystery");
        RS_Chat_AddMessage(&app->chat, RS_CHAT_TYPE_GAME, NULL, line);
        app->need_redraw = 1;
        return 0; /* handled locally; no CS2 task was dispatched */
    }

    /* Loc editor "Select" row (rs_minimenu_world.c add_scenery_rows, gated on
     * locedit_active): entirely client-side, same shape as the Examine
     * intercept above -- must also run before the pick.kind switch, or a
     * scenery-kind pick with this action would fall into the
     * UI_MINIMENU_PICK_SCENERY case and mis-send an OPLOC. The `locedit_visible`
     * re-check guards a row clicked in the one frame the tool closed on. */
    if( opt.action == RS_MINIMENU_ACTION_LOCEDIT_SELECT )
    {
        if( app->locedit_visible )
            app_loc_editor_select_element(app, opt.pick.id);
        return 0; /* handled locally; no CS2 task was dispatched */
    }

    /* Its ground twin. The pick already carries the exact tile the terrain hit
     * named — secondary/tertiary are scene x/z and quaternary the mesh level —
     * so nothing is re-derived here and the panel cannot describe a different
     * tile than the row that was clicked. */
    if( opt.action == RS_MINIMENU_ACTION_LOCEDIT_SELECT_TERRAIN )
    {
        if( app->locedit_visible )
            app_loc_editor_select_terrain(
                app, opt.pick.secondary_id, opt.pick.tertiary_id, opt.pick.quaternary_id);
        return 0; /* handled locally; no CS2 task was dispatched */
    }

    /* Map editor SELECT tool's pair of the two rows above -- same shape, a
     * different panel's latch. The `app_mapedit_select_active` re-check
     * guards a row clicked in the one frame the tool closed or switched off
     * SELECT. */
    if( opt.action == RS_MINIMENU_ACTION_MAPEDIT_SELECT )
    {
        if( app_mapedit_select_active(app) )
            Editor_PanelSelectLoc(&app->editor_panel, app, opt.pick.id);
        return 0; /* handled locally; no CS2 task was dispatched */
    }

    if( opt.action == RS_MINIMENU_ACTION_MAPEDIT_SELECT_TERRAIN )
    {
        if( app_mapedit_select_active(app) )
            Editor_PanelSelectTerrain(
                &app->editor_panel, app, opt.pick.secondary_id, opt.pick.tertiary_id,
                opt.pick.quaternary_id);
        return 0; /* handled locally; no CS2 task was dispatched */
    }

    /* Arm target mode from a spell/prayer button (reference TGT_BUTTON): the
     * next click on a valid target casts. objsel and targetsel are mutually
     * exclusive. */
    if( opt.action == REVCONFIG_MINIMENU_TGT_BUTTON )
    {
        int32_t idx = UITree_FindByComponentId(app->tree, opt.pick.id);
        app->objsel.active = 0;
        app_targetsel_clear(app);
        app->targetsel.active = 1;
        app->targetsel.component_id = opt.pick.id;
        app->targetsel.mask = 0;
        app->targetsel.op[0] = '\0';
        if( idx >= 0 )
        {
            struct UITreeComponent const* node = &app->tree->components[idx];
            app->targetsel.mask = node->behavior.target_mask;
            /* targetsel.op only feeds the "Cast <spell> on ..." prompt text
             * built below. Each %s below is precision-capped so the sum of
             * parts is provably within sizeof(targetsel.op), rather than
             * relying on snprintf's implicit (GCC-unprovable) truncation. */
            if( node->behavior.button_type == REVCONFIG_BUTTON_TYPE_TARGET )
            {
                /* Classic targetOp = "<verb-prefix> <base> <verb-suffix>", the
                 * verb split on its first space (Client-TS TGT_BUTTON). */
                char const* verb = UITree_MenuOptions(node)->target_verb;
                char const* base = UITree_MenuOptions(node)->target_base;
                char const* space = strchr(verb, ' ');
                if( space )
                {
                    int prefix_len = (int)(space - verb);
                    if( prefix_len > 20 )
                        prefix_len = 20;
                    snprintf(
                        app->targetsel.op,
                        sizeof(app->targetsel.op),
                        "%.*s %.*s%.*s",
                        prefix_len,
                        verb,
                        20,
                        base,
                        20,
                        space);
                }
                else
                    snprintf(
                        app->targetsel.op,
                        sizeof(app->targetsel.op),
                        "%.*s %.*s",
                        31,
                        verb,
                        31,
                        base);
            }
            else
            {
                /* IF3 (deob method523 + the world rows that read it back): the
                 * verb is whole, its base is the component's opBase — IF3 has no
                 * targetText — and the target's own name is joined on with an
                 * arrow rather than a verb suffix, so a row reads
                 * "Cast <col>Wind Strike</col> -> <col>Goblin</col>". Built into
                 * one string here because that is the shape the world/inventory
                 * row builders append the target's name to. */
                snprintf(
                    app->targetsel.op,
                    sizeof(app->targetsel.op),
                    "%.*s %.*s ->",
                    29,
                    UITree_MenuOptions(node)->target_verb,
                    29,
                    UITree_MenuOptions(node)->option);
            }
        }
        if( getenv("TORIRS_CLICK_DEBUG") )
            fprintf(
                stderr,
                "selarm: tgt com=0x%x mask=0x%x op='%s'\n",
                app->targetsel.component_id,
                (unsigned)app->targetsel.mask,
                app->targetsel.op);
        app_targetsel_dispatch_hook(app, 1);
        app->need_redraw = 1;
        return 1;
    }

    /* "Use <held> with <world target>" (reference USEHELD_ONLOC/NPC/OBJ) and
     * "<spell> <world target>" (TGT_LOC/NPC/OBJ). Both read the world pick and
     * the armed selection; handled here so the pick.kind switch stays the
     * plain-op path. */
    if( app->world &&
        (opt.action == REVCONFIG_MINIMENU_USEHELD_ONLOC ||
         opt.action == REVCONFIG_MINIMENU_USEHELD_ONNPC ||
         opt.action == REVCONFIG_MINIMENU_USEHELD_ONOBJ ||
         opt.action == REVCONFIG_MINIMENU_USEHELD_ONPLAYER ||
         opt.action == REVCONFIG_MINIMENU_TGT_LOC || opt.action == REVCONFIG_MINIMENU_TGT_NPC ||
         opt.action == REVCONFIG_MINIMENU_TGT_OBJ || opt.action == REVCONFIG_MINIMENU_TGT_PLAYER) )
    {
        int abs_x = opt.pick.tertiary_id + app->world->_base_tile_x;
        int abs_z = opt.pick.quaternary_id + app->world->_base_tile_z;
        switch( opt.action )
        {
        case REVCONFIG_MINIMENU_USEHELD_ONLOC:
            /* Reference interactWithLoc returns before sending anything when
             * the placed loc cannot be resolved (typecode2 == -1). */
            if( !app_try_move_loc(
                    app,
                    opt.pick.id,
                    opt.pick.tertiary_id,
                    opt.pick.quaternary_id,
                    app->ctrl_held) )
                break;
            APP_NET_SEND(
                app,
                net_out_oplocu(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    abs_x,
                    abs_z,
                    opt.pick.secondary_id,
                    app->objsel.obj_id,
                    app->objsel.slot,
                    app->objsel.component_id));
            break;
        case REVCONFIG_MINIMENU_TGT_LOC:
            if( !app_try_move_loc(
                    app,
                    opt.pick.id,
                    opt.pick.tertiary_id,
                    opt.pick.quaternary_id,
                    app->ctrl_held) )
                break;
            APP_NET_SEND(
                app,
                net_out_oploct(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    abs_x,
                    abs_z,
                    opt.pick.secondary_id,
                    app->targetsel.component_id));
            break;
        case REVCONFIG_MINIMENU_USEHELD_ONOBJ:
            app_try_move_obj(app, opt.pick.tertiary_id, opt.pick.quaternary_id, app->ctrl_held);
            APP_NET_SEND(
                app,
                net_out_opobju(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    abs_x,
                    abs_z,
                    opt.pick.secondary_id,
                    app->objsel.obj_id,
                    app->objsel.slot,
                    app->objsel.component_id));
            break;
        case REVCONFIG_MINIMENU_TGT_OBJ:
            app_try_move_obj(app, opt.pick.tertiary_id, opt.pick.quaternary_id, app->ctrl_held);
            APP_NET_SEND(
                app,
                net_out_opobjt(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    abs_x,
                    abs_z,
                    opt.pick.secondary_id,
                    app->targetsel.component_id));
            break;
        case REVCONFIG_MINIMENU_USEHELD_ONNPC:
        {
            struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt.pick.id, NULL);
            if( npc && npc->server_slot >= 0 )
            {
                app_try_move_npc(app, npc, app->ctrl_held);
                APP_NET_SEND(
                    app,
                    net_out_opnpcu(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        npc->server_slot,
                        app->objsel.obj_id,
                        app->objsel.slot,
                        app->objsel.component_id));
            }
            break;
        }
        case REVCONFIG_MINIMENU_TGT_NPC:
        {
            struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt.pick.id, NULL);
            if( npc && npc->server_slot >= 0 )
            {
                app_try_move_npc(app, npc, app->ctrl_held);
                APP_NET_SEND(
                    app,
                    net_out_opnpct(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        npc->server_slot,
                        app->targetsel.component_id));
            }
            break;
        }
        case REVCONFIG_MINIMENU_USEHELD_ONPLAYER:
        {
            struct WorldEntity_Player* player = World_PlayerGetByElementId(app->world, opt.pick.id);
            if( player && player->server_pid >= 0 && player->server_pid != app->world->local_pid )
            {
                app_try_move_player(app, player, app->ctrl_held);
                APP_NET_SEND(
                    app,
                    net_out_opplayeru(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        player->server_pid,
                        app->objsel.obj_id,
                        app->objsel.slot,
                        app->objsel.component_id));
            }
            break;
        }
        case REVCONFIG_MINIMENU_TGT_PLAYER:
        {
            struct WorldEntity_Player* player = World_PlayerGetByElementId(app->world, opt.pick.id);
            if( player && player->server_pid >= 0 && player->server_pid != app->world->local_pid )
            {
                app_try_move_player(app, player, app->ctrl_held);
                APP_NET_SEND(
                    app,
                    net_out_opplayert(
                        app->net->rev,
                        app->net->random_out,
                        _nsbuf,
                        sizeof(_nsbuf),
                        player->server_pid,
                        app->targetsel.component_id));
            }
            break;
        }
        default:
            break;
        }
        /* The selection was just consumed by the target it was aimed at. */
        app_selection_clear(app);
        return 0;
    }

    switch( opt.pick.kind )
    {
    case UI_MINIMENU_PICK_UI:
    case UI_MINIMENU_PICK_INV_SLOT:
    {
        int32_t idx = UITree_FindByComponentId(app->tree, opt.pick.id);
        struct UITreeRuntimeScriptHook const* hook;
        struct UITreeRuntimeScriptHook hook_copy;
        int hook_com_id = -1;

        if( idx < 0 )
            return 0;
        /*
         * An inventory op is the server's to answer, so it is sent before any
         * hook is looked at. It used to be the fallback for "this component
         * has no CS2 hook", which held only for the backpack: rev 230's worn
         * slots DO carry an onop hook (the container owns the "Remove" verb),
         * and resolving it first meant clicking Remove ran a script and sent
         * nothing — the helmet stayed on.
         */
        /* Whatever app_minimenu_inv_action claimed — a packet sent, an
         * Examine printed, a "Use" armed — is the whole of this click. The
         * container's own hook must not also fire: rev 230's worn slots carry
         * an onop script beside their "Remove" verb, and running it instead of
         * sending left the helmet on. */
        if( opt.pick.kind == UI_MINIMENU_PICK_INV_SLOT && app_minimenu_inv_action(app, &opt) )
            return 0;
        /*
         * A numbered op on a plain IF3 widget goes to the server as IF_BUTTON<n>
         * *and* runs the local onop hook — both, not either. The world map orb
         * is the clearest case: its onop is `opsound(...)`, nothing more, and
         * the whole of "Open World Map" happens server-side. The events mask is
         * the gate (bit n enables op n), so a component the server never armed
         * stays purely client-side, which is what rev 230 means by "no
         * clickable-by-default".
         */
        {
            int if_button_sent = 0;
            if( opt.pick.kind == UI_MINIMENU_PICK_UI && opt.action_index >= 0 &&
                opt.action_index < 10 && app->net )
            {
                int const op_num = opt.action_index + 1;
                unsigned const events = app_if_events_for_node(app, opt.pick.id);
                /* The events mask is the whole of "is this op the server's", so it
                 * is the one number worth printing beside the row that carries it —
                 * a component the server never armed produces a perfectly good menu
                 * row and sends nothing. */
                if( getenv("TORIRS_CLICK_DEBUG") )
                    fprintf(
                        stderr,
                        "clickdbg: op%d on com=0x%x events=0x%x net=%d\n",
                        op_num,
                        opt.pick.id,
                        events,
                        app->net ? 1 : 0);
                if( events & (1u << op_num) )
                {
                    int target;
                    int sub;

                    app_if_button_target(app, opt.pick.id, &target, &sub);
                    if( getenv("TORIRS_CLICK_DEBUG") )
                        fprintf(
                            stderr,
                            "clickdbg: send op%d target=0x%x sub=%d state=%d\n",
                            op_num,
                            target,
                            sub,
                            app->net ? (int)app->net->state : -1);
                    APP_NET_SEND(
                        app,
                        net_out_if_button_op(
                            app->net->rev,
                            app->net->random_out,
                            _nsbuf,
                            sizeof(_nsbuf),
                            op_num,
                            target,
                            sub));
                    if_button_sent = 1;
                }
                else if(
                    opt.action == REVCONFIG_MINIMENU_IF_BUTTON && opt.action_index == 0 &&
                    (events & 0x1u) )
                {
                    /* EVENT_CLICK (bit 0): minimenu rows use action_index 0, so
                     * the bit-(action_index+1) check above looks at bit 1 and
                     * misses. Choice-menu rows are dynamic children of a
                     * container the server armed across a sub range — answer
                     * with IF_BUTTON1 (parent, sub) so last_slot names the row
                     * and the server can resume p_pausebutton. Static continue
                     * prompts (no sub) keep the CS2 / plain IF_BUTTON path. */
                    int target;
                    int sub;

                    app_if_button_target(app, opt.pick.id, &target, &sub);
                    if( sub >= 0 )
                    {
                        if( getenv("TORIRS_CLICK_DEBUG") )
                            fprintf(
                                stderr,
                                "clickdbg: EVENT_CLICK choice target=0x%x sub=%d\n",
                                target,
                                sub);
                        APP_NET_SEND(
                            app,
                            net_out_if_button_op(
                                app->net->rev,
                                app->net->random_out,
                                _nsbuf,
                                sizeof(_nsbuf),
                                1,
                                target,
                                sub));
                        if_button_sent = 1;
                    }
                }
            }
            hook = UITree_ResolveClickHook(app->tree, idx, &hook_com_id);
            if( !hook || hook->script_id <= 0 )
            {
                /* Already answered the server (choice-menu IF_BUTTON1, or a
                 * numbered op). Do not also sink a plain IF_BUTTON with the
                 * dynamic child's runtime id. */
                if( if_button_sent )
                    return 0;
                /* IF1-style static buttons have no CS2 hook — the button engine
                 * applies buttonType/varp semantics locally (+ server notify via
                 * the sink once networking attaches). */
                if( RS_IF1_ApplyButtonClick(app, opt.pick.id, opt.action) )
                    return 0;
                fprintf(
                    stderr,
                    "minimenu: no hook for com=0x%x action=%d op=%d\n",
                    opt.pick.id,
                    opt.action,
                    opt.action_index);
                return 0;
            }
        }
        UITree_HookInitCopy(&hook_copy, hook);
        RS_CS2_SetEventOp(&app->host, opt.action_index >= 0 ? opt.action_index + 1 : 1, 0);
        RS_CS2_SetEventMouse(&app->host, click_x, click_y);
        RS_CS2_DispatchHook(&app->host, &app->runner, hook_com_id, &hook_copy);
        RS_CS2_SetEventOp(&app->host, 1, 0);
        RS_CS2_PumpTransmits(&app->host, &app->runner);
        UITree_HookClear(&hook_copy);
        return 1;
    }
    case UI_MINIMENU_PICK_TERRAIN:
        /* Walk here (reference tryMove type 0): BFS route + MOVE_GAMECLICK
         * waypoints; no local prediction — the PLAYER_INFO echo moves the
         * player. */
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "minimenu: walk-click scene=%d,%d abs=%d,%d\n",
                opt.pick.secondary_id,
                opt.pick.tertiary_id,
                app->world ? app->world->_base_tile_x + opt.pick.secondary_id : -1,
                app->world ? app->world->_base_tile_z + opt.pick.tertiary_id : -1);
        /* Cross only if the move actually went out — the reference sets
         * crossMode in the same block that emits the packet (Client-TS
         * `if (success)`, deob client.java:9305 under method3951()). A click
         * the router refuses leaves the screen alone. */
        if( app_try_move(
                app, opt.pick.secondary_id, opt.pick.tertiary_id, 0, 0, 0, 0, app->ctrl_held) )
            UICross_Show(&app->cross, UI_CROSS_WALK, click_x, click_y);
        return 0;
    case UI_MINIMENU_PICK_NPC:
    {
        struct WorldEntity_NPC* npc = World_NpcGetByElementId(app->world, opt.pick.id, NULL);
        if( !npc || npc->server_slot < 0 )
            return 0; /* not yet server-synced */
        /* Reference OP_NPC1..5 / USEHELD_ONNPC walk toward the NPC (tryMove
         * type 2) on the same click; the OP is sent regardless of the route. */
        app_try_move_npc(app, npc, app->ctrl_held);
        if( app->objsel.active )
        {
            APP_NET_SEND(
                app,
                net_out_opnpcu(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    npc->server_slot,
                    app->objsel.obj_id,
                    app->objsel.slot,
                    app->objsel.component_id));
            app_selection_clear(app);
        }
        else
        {
            APP_NET_SEND(
                app,
                net_out_opnpc(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    opt.action_index + 1,
                    npc->server_slot));
        }
        return 0;
    }
    case UI_MINIMENU_PICK_PLAYER:
    {
        struct WorldEntity_Player* player = World_PlayerGetByElementId(app->world, opt.pick.id);
        /* Menu never emits local-player rows; refuse if one slips through. */
        if( !player || player->server_pid < 0 || player->server_pid == app->world->local_pid )
            return 0;
        app_try_move_player(app, player, app->ctrl_held);
        if( app->objsel.active )
        {
            APP_NET_SEND(
                app,
                net_out_opplayeru(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    player->server_pid,
                    app->objsel.obj_id,
                    app->objsel.slot,
                    app->objsel.component_id));
            app_selection_clear(app);
        }
        else
        {
            APP_NET_SEND(
                app,
                net_out_opplayer(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    opt.action_index + 1,
                    player->server_pid));
        }
        return 0;
    }
    case UI_MINIMENU_PICK_SCENERY:
    {
        int abs_x = opt.pick.tertiary_id + app->world->_base_tile_x;
        int abs_z = opt.pick.quaternary_id + app->world->_base_tile_z;
        int loc_id = opt.pick.secondary_id;
        /* Reference interactWithLoc: pathfind toward the loc (tryMove type 2)
         * on the same click, then send the OP below regardless of the walk
         * result. The scene tile is the pick's (tertiary,quaternary). */
        if( !app_try_move_loc(
                app, opt.pick.id, opt.pick.tertiary_id, opt.pick.quaternary_id, app->ctrl_held) )
            return 0; /* reference interactWithLoc: typecode2 == -1 sends nothing */
        if( app->objsel.active )
        {
            APP_NET_SEND(
                app,
                net_out_oplocu(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    abs_x,
                    abs_z,
                    loc_id,
                    app->objsel.obj_id,
                    app->objsel.slot,
                    app->objsel.component_id));
            app_selection_clear(app);
        }
        else
        {
            APP_NET_SEND(
                app,
                net_out_oploc(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    opt.action_index + 1,
                    abs_x,
                    abs_z,
                    loc_id));
        }
        return 0;
    }
    case UI_MINIMENU_PICK_OBJ:
    {
        /* Ground item (reference OP_OBJ1..5 / USEHELD_ONOBJ): the wire
         * carries the tile + obj id, not the scene element. */
        int abs_x = opt.pick.tertiary_id + app->world->_base_tile_x;
        int abs_z = opt.pick.quaternary_id + app->world->_base_tile_z;
        int obj_id = opt.pick.secondary_id;
        /* Reference obj doAction: pathfind to the exact tile (tryMove type 2),
         * and on failure retry a 1x1 approach so an adjacent tile still arrives;
         * the OP is sent below on the same click either way. */
        app_try_move_obj(app, opt.pick.tertiary_id, opt.pick.quaternary_id, app->ctrl_held);
        if( app->objsel.active )
        {
            APP_NET_SEND(
                app,
                net_out_opobju(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    abs_x,
                    abs_z,
                    obj_id,
                    app->objsel.obj_id,
                    app->objsel.slot,
                    app->objsel.component_id));
            app_selection_clear(app);
        }
        else
        {
            APP_NET_SEND(
                app,
                net_out_opobj(
                    app->net->rev,
                    app->net->random_out,
                    _nsbuf,
                    sizeof(_nsbuf),
                    opt.action_index + 1,
                    abs_x,
                    abs_z,
                    obj_id));
        }
        return 0;
    }
    case UI_MINIMENU_PICK_NONE:
        /* Cancel, and the "Walk here" row of a click that hit no terrain and
         * for which no fallback tile could be resolved (no world loaded, no
         * viewport, a scene with no ground at all). Nothing to run — not an
         * unhandled kind, so it must not warn. */
        return 0;
    default:
        fprintf(stderr, "minimenu: unhandled pick kind %d\n", (int)opt.pick.kind);
        return 0;
    }
}

/* Execute one menu row, then apply the reference doAction tail
 * (Client.ts:9506): every executed row clears useMode/targetMode EXCEPT the two
 * arming rows (USEHELD_START / TGT_BUTTON), which return early. So any click
 * that isn't "arm Use" or "arm spell" cancels a pending selection — Walk here, a
 * plain op, a UI button, Cancel — i.e. clicking off anything that can't be a use
 * target drops the white outline. The arming rows set the selection inside
 * run_option and must survive; they alone skip the clear. */
static int
app_minimenu_use_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y)
{
    struct UIMinimenu* menu = &app->interact.minimenu;
    int action = -1;
    int result;

    if( option_index >= 0 && option_index < menu->option_count )
        action = UIMinimenu_ActionNormalize(menu->options[option_index].action);

    result = app_minimenu_run_option(app, option_index, click_x, click_y);

    if( action != REVCONFIG_MINIMENU_OPHELDT_START && action != REVCONFIG_MINIMENU_TGT_BUTTON )
        app_selection_clear(app);
    return result;
}

void
App_PlaySound(
    struct App* app,
    int sound_id,
    int loops,
    int delay)
{
    assert(app);
    if( getenv("TORIRS_SOUND_DEBUG") )
        fprintf(stderr, "sound: synth=%d loops=%d delay=%d\n", sound_id, loops, delay);
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

    if( width < APP_CANVAS_MIN_W )
        width = APP_CANVAS_MIN_W;
    if( height < APP_CANVAS_MIN_H )
        height = APP_CANVAS_MIN_H;

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
            fprintf(
                stderr, "resize: more than %d onResize hooks; truncating\n", APP_RESIZE_HOOK_MAX);
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
    if( getenv("TORIRS_RESIZE_DEBUG") )
        fprintf(stderr, "canvas: %dx%d\n", width, height);
    return 1;
}

/* True when this node, or anything it hangs off, is hidden — i.e. it does not
 * reach the screen. `behavior.hide` alone is not that test: hiding a container
 * leaves every descendant's own flag clear and its stale abs_* box intact, so a
 * caller that reads geometry off the flat component array sees a hidden
 * subtree's boxes as live ones. */
static int
component_hidden_or_orphaned(
    struct UITree const* tree,
    int32_t idx)
{
    int guard;
    assert(tree);
    for( guard = 0; idx >= 0 && guard < 256; guard++ )
    {
        struct UITreeComponent const* c;
        assert((uint32_t)idx < tree->component_count);
        c = &tree->components[idx];
        if( c->freed || c->behavior.hide )
            return 1;
        idx = c->parent;
    }
    return 0;
}

int
App_MeasureRightChromeStripWidth(struct App const* app)
{
    int canvas_w;
    int canvas_h;
    int best;
    uint32_t i;

    assert(app);
    if( !app->tree || app->tree->component_count == 0 )
        return 0;

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

        /* Ancestors too: a speculatively baked panel (the CS2 runtime bakes a
         * pack the moment a script touches it) is hidden at its group root,
         * while the right-docked column inside it keeps a clear flag and a
         * full-height box — the exact signature this loop looks for. Measuring
         * that column grew the fixed canvas by a panel that was never open. */
        if( component_hidden_or_orphaned(app->tree, (int32_t)i) )
            continue;
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
        if( w > best )
            best = w;
    }
    return best;
}

int
App_FixedCanvasWidth(struct App const* app)
{
    assert(app);
    return APP_CANVAS_MIN_W + App_MeasureRightChromeStripWidth(app);
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
    /* Deliberately NOT window_mode_dirty: the shell is the caller and applies
     * the platform side directly. Raising it here would make the boot config
     * indistinguishable from a clientscript's SETWINDOWMODE on the next drain. */
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
                /* The raw window size is latched before the scale divides it:
                 * the canvas is what the client draws, but the window is what
                 * a later scale change has to be recomputed from. Both fixed
                 * and resizable pushes are latched — leaving fixed restores
                 * the real window size through the same command. */
                app->window_w = cmd->width;
                app->window_h = cmd->height;
                app->host.ui_scale_dirty = false;
                App_SetCanvasSize(
                    app, app_ui_scaled_axis(app, cmd->width), app_ui_scaled_axis(app, cmd->height));
            }
            break;
        default:
            fprintf(stderr, "cmdbus: unhandled command type %u\n", header.type);
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
app_frame_latch_note(struct App* app, char const* reason)
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
        fprintf(stderr, "%s\n", line);
        frames = 0;
    }
}

int
App_RunOnce(
    struct App* app,
    uint64_t now_ms,
    struct LibToriRS_Input* input)
{
    struct UIInteractOut out;
    int ran_cs2 = 0;

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
    PluginHost_FrameStart(app->plugins, now_ms);

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
             * "Clear your highlighted tiles" (117) and "Clear your highlighted
             * NPCs" (267) both reach clientscript 3969, whose switch has no
             * case for either -- the only thing it does for them is write the
             * setting id to varbit 9657, which is how they get here at all.
             * The reference clears the group natively; so does this.
             *
             * Group 6 for both, and they are different groups: the tile
             * markers live in the TILE kind's 6 (clientscript 4763 sets it up)
             * and the tagged npcs in the NPC kind's (6688 adds to it). Group
             * ids are per kind, which is exactly why clearing one does not
             * touch the other.
             */
            if( setting_id == APP_SETTING_CLEAR_TILE_MARKERS )
                RS_HighlightClear(&app->host.highlight, RS_HIGHLIGHT_TILE, 6);
            else if( setting_id == APP_SETTING_CLEAR_NPC_TAGS )
                RS_HighlightClear(&app->host.highlight, RS_HIGHLIGHT_NPC, 6);
            PluginHost_Setting(app->plugins, setting_id, setting_value);
        }
    }

    /* Developer overlay first: its toggle key has to latch during a boot too,
     * and its readout has to be current before this frame's emit rebuild. */
    app_debug_overlay_tick(app, input);
    /* Right after the developer overlay, because it shares that instance: the
     * tick above is what routed this frame's mouse into dbg_ui, and the
     * picker's own drain has to run against the activation that produced. */
    app_settings_colour_tick(app, input);
    /* Plugin settings beside the other developer chrome, and before the loc
     * editor for the same reason it runs before the map editor: whoever is
     * open drains the shared activation latch, so order decides who sees a
     * click first. This one only takes activations it owns. */
    app_plugin_panel_tick(app, input);
    /* Loc editor next, same reasoning -- and it has to run before anything
     * downstream reads input_frame_consumed for click-to-walk. */
    app_loc_editor_tick(app, input);
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

    /* Pump ordinary async work with a frame budget.  A CS2 transaction is the
     * exception: cooperative yields are drained to completion, and a genuine
     * external wait retains the last settled frame until it can resume. */
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_ASYNC)
    {
        int booting = app->app_state == APP_STATE_BOOTING;
        int budget = booting ? 512 : 32;
        enum TaskRunnerStat stat = TASK_RUNNER_IDLE;
        int steps = 0;
        int settling_cs2 = !booting && (app->runner_had_work || app->runner.frame_settle_pending);

        if( settling_cs2 )
        {
            stat = app_settle_cs2_frame(app);
        }
        else
        {
            for( int i = 0; i < budget; i++ )
            {
                steps++;
                if( booting )
                    app->boot_steps++;
                stat = TaskRunner_Step(&app->runner);
                if( stat == TASK_RUNNER_IDLE )
                    break;
            }
        }
        if( booting )
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
        if( settling_cs2 && stat != TASK_RUNNER_IDLE )
            app->runner_had_work = 1;
        /* Tree-affecting async work (CS2 hooks/transmits) finished: refresh. */
        if( settling_cs2 && stat == TASK_RUNNER_IDLE )
        {
            app->runner_had_work = 0;
            app->pending_tree_refresh = 1;
        }
    }

    if( app->app_state == APP_STATE_BOOTING )
    {
        /* Loading screen frame; no logic/interaction until the tree exists. */
        app->last_logic_ms = now_ms;
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
         * Snapping, not truncating.
         *
         * The frame pacer aims at 20ms and the logic tick is 20ms, so the two
         * beat against each other: a frame that lands at 19.6ms elapsed
         * divides to 0 ticks, the next lands at 39.2ms and divides to 2. The
         * world then advances twice in one frame and not at all in the other,
         * and since the renderer does not interpolate between ticks that reads
         * as a visible hitch even though no frame was late. Round to the
         * nearest tick instead of flooring: anything within half a tick of the
         * boundary counts as one tick, and genuine stalls still divide out to
         * the real count and take the bounded catch-up path below.
         *
         * Rounding up pushes last_logic_ms past now_ms, so the next frame's
         * subtraction must not be done in unsigned -- it would wrap to ~2^64
         * and clamp straight to APP_MAX_CATCHUP_TICKS every frame.
         */
        uint64_t elapsed_ms = now_ms > app->last_logic_ms ? now_ms - app->last_logic_ms : 0;
        int ticks = (int)((elapsed_ms + APP_LOGIC_TICK_MS / 2) / APP_LOGIC_TICK_MS);
        if( ticks > 0 )
        {
            app->last_logic_ms += (uint64_t)ticks * APP_LOGIC_TICK_MS;
            if( ticks > APP_MAX_CATCHUP_TICKS )
                ticks = APP_MAX_CATCHUP_TICKS;
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_LOGIC_TICKS, ticks);
            for( int t = 0; t < ticks; t++ )
            {
                TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_CS2)
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

                    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_CS2)
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
        }
        else
        {
            ticks = 0;
        }

        /* World sim cycles == client 20ms ticks (v1 world_cycle cadence); the
         * movers get the frame's real elapsed time in the same unit. Clamped
         * to the logic catch-up budget so a stalled frame does not fling
         * every actor down its route in one step. */
        {
            uint64_t frame_ms = now_ms > app->last_mover_ms ? now_ms - app->last_mover_ms : 0;
            float frame_cycles;

            if( app->last_mover_ms == 0 )
                frame_ms = 0; /* first frame has no elapsed time to spend */
            else if( frame_ms > APP_MAX_CATCHUP_TICKS * APP_LOGIC_TICK_MS )
                frame_ms = APP_MAX_CATCHUP_TICKS * APP_LOGIC_TICK_MS;
            app->last_mover_ms = now_ms;
            frame_cycles = (float)frame_ms / (float)APP_LOGIC_TICK_MS;
            app_world_frame(app, ticks, frame_cycles);
        }
    }

    /* Resume a parked packet pipeline at frame rate, not tick rate. A packet
     * task that yielded for an asynchronous cache read (the norm on web past
     * READY) has its response delivered by the platform pump at the top of
     * the very next frame; leaving the resume to the next 20ms logic tick
     * held the visual latch a full tick per read, and a hitsplat whose
     * sprite+sound chain was several reads deep froze the world for that
     * many ticks — every server cycle, in combat. When nothing is parked
     * this does not run at all, so tick cadence is otherwise untouched. */
    if( app->exec_runner_had_work || app->server_tick_open ||
        app->pending_clientscript_count > 0 )
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

        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_CS2)
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_CS2_SETTLE)
        {
            stat = app_settle_cs2_frame(app);
        }
        if( stat != TASK_RUNNER_IDLE )
        {
            app->runner_had_work = 1;
            app_frame_latch_note(
                app, stat == TASK_RUNNER_BLOCKED ? "cs2_blocked" : "cs2_pending");
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
            app->runner_had_work              ? "settled:runner_had_work"
            : app->runner.frame_settle_pending ? "settled:frame_settle_pending"
            : app->exec_runner_had_work        ? "settled:exec_runner_had_work"
            : app->server_tick_open            ? "settled:server_tick_open"
                                               : "settled:pending_clientscripts");
        return 0;
    }
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

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_INTERACT)
    {
        UITree_InteractFrame(&app->interact, app->tree, &app->ui_host, input, now_ms, &out);
    }

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
    app->world_mouse_in_viewport =
        app_world_mouse_gate(app, input->curr.mouse_x, input->curr.mouse_y);
    app->world_mouse_x = input->curr.mouse_x;
    app->world_mouse_y = input->curr.mouse_y;
    if( !app->world_mouse_in_viewport )
    {
        app->world_hover_tile_x = -1;
        app->world_hover_tile_z = -1;
        World_PickSetReset(&app->world_pickset);
    }

    /* Mouseover text before any click handling: the reference recomputes it
     * every cycle from the same menu the click paths build. */
    app_hover_text_update(app, input->curr.mouse_x, input->curr.mouse_y);

    /* Minimenu gesture results (see interact_minimenu): option selected on
     * mousedown -> dispatch; right press with no menu open -> build + show. */
    if( out.minimenu_select >= 0 )
    {
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
     * The CS2 chrome executor's own components, before the game sees them.
     *
     * Its widgets ARE interface components, so without this a click on the
     * plugin window's checkbox would also build a minimenu for whatever the
     * component looks like to the game -- a rectangle with no ops, which is
     * "Walk here". Recognised by component id: the executor allocates from a
     * private high range precisely so this is a bounds test.
     */
    int chrome_took_click = 0;
    if( out.clicked_com_id >= TORIRS_CHROME_CS2_ID_BASE &&
        out.clicked_com_id < TORIRS_CHROME_CS2_ID_END &&
        (app_plugin_button_click(app, out.clicked_com_id) ||
         ToriRSChromeExecCs2_Click(out.clicked_com_id)) )
    {
        chrome_took_click = 1;
        app->input_frame_consumed = 1;
        app->need_redraw = 1;
    }

    if( !chrome_took_click && app->inv_drag_com_id < 0 && !pressed_filled_obj &&
        out.clicked_com_id >= 0 && !out.minimenu_closed && out.minimenu_select < 0 )
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
            .world = app->world,
            .world_pickset = NULL, /* UI hit: mouse was over a component */
            .click_in_world = false,
            /* Honour an armed "Use"/spell selection so the left-click default
             * row matches the right-click menu (reference doAction runs the
             * same chooseDefaultMenuEntry over the useMode/targetMode menu). */
            .selection = app_minimenu_selection(app),
            .locedit_active = app->locedit_visible != 0,
            .mapedit_select_active = app_mapedit_select_active(app),
        };
        struct UIMinimenu scratch;
        int default_idx;

        UIMinimenu_Reset(&scratch);
        scratch.font_id = app->interact.minimenu.font_id;
        RS_Minimenu_Build(&mctx, out.clicked_x, out.clicked_y, &scratch);
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
            fprintf(
                stderr,
                "clickdbg: com=0x%x rows=%d default=%d\n",
                out.clicked_com_id,
                scratch.option_count,
                default_idx);
            for( int i = 0; i < scratch.option_count; i++ )
                fprintf(
                    stderr,
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
    if( getenv("TORIRS_NET_DEBUG") && (out.left_click_miss || out.clicked_com_id >= 0) )
        fprintf(
            stderr,
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
    if( app->inv_drag_com_id < 0 && out.left_click_miss && !out.minimenu_closed &&
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
            .world = app->world,
            .world_pickset = &app->world_pickset,
            .click_in_world = true,
            /* Honour an armed "Use"/spell selection so a left-click on a world
             * target casts/uses (the TGT and USEHELD_ON rows) instead of
             * falling back to the target's default op. Without this a left-click
             * on an NPC with a spell armed built the plain ops and defaulted to
             * Attack (walk-to-melee "run up"), while the right-click menu cast. */
            .selection = app_minimenu_selection(app),
            .locedit_active = app->locedit_visible != 0,
            .mapedit_select_active = app_mapedit_select_active(app),
        };
        struct UIMinimenu scratch;
        int default_idx;

        UIMinimenu_Reset(&scratch);
        scratch.font_id = app->interact.minimenu.font_id;
        app_minimenu_ctx_ground_fallback(app, &mctx, out.left_click_miss_x, out.left_click_miss_y);
        RS_Minimenu_Build(&mctx, out.left_click_miss_x, out.left_click_miss_y, &scratch);
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
    if( app->inv_drag_com_id < 0 && out.left_click_miss && !out.minimenu_closed &&
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
     * OPHELD1-5 / INV_BUTTON / IF_BUTTON re-fire on_op from inv_action. */
    if( app->inv_drag_com_id >= 0 )
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
        app_chat_focus_tick(app, input, out.minimenu_consumed_pointer, &chat_submit_pending);

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

    /* Keyboard broadcast: every event this frame times every visible onKey
     * handler (reference OsrsClient key dispatch). Unlike the intent loop above
     * this re-resolves each component id immediately before dispatching it
     * rather than snapshotting hooks up front -- a broadcast runs many scripts
     * in one frame, and an earlier one can CC_CREATE (realloc components[]) or
     * CC_DELETEALL (reclaim the slot), so a target collected during the scan may
     * be gone by its turn. Same reasoning as the on_timer loop. */
    for( int e = 0; e < out.key_event_count && !plugin_ate_keys && !chrome_ate_keys; e++ )
    {
        for( int t = 0; t < out.key_target_count; t++ )
        {
            struct UIKeyTarget const* target = &out.key_targets[t];
            int32_t idx;
            if( !app_key_target_accepts(app, target->component_id, chat_keys_suppressed) )
                continue;
            idx = UITree_FindByComponentId(app->tree, target->component_id);
            if( idx < 0 )
                continue;
            /* Re-check the hook too: the id may have been reclaimed and handed
             * to a different node since collection. */
            if( UITree_Hooks(&app->tree->components[idx])->on_key.script_id <= 0 )
                continue;
            RS_CS2_SetEventMouse(
                &app->host, out.key_mouse_x - target->abs_x, out.key_mouse_y - target->abs_y);
            RS_CS2_SetEventKey(
                &app->host, out.key_events[e].key_typed, out.key_events[e].key_pressed);
            if( getenv("TORIRS_KEY_DEBUG") )
                fprintf(
                    stderr,
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
                idx = UITree_FindByComponentId(app->tree, target->component_id);
                if( idx < 0 )
                    continue;
                hook = down ? &UITree_Hooks(&app->tree->components[idx])->on_key_down
                            : &UITree_Hooks(&app->tree->components[idx])->on_key_up;
                if( hook->script_id <= 0 )
                    continue;
                RS_CS2_SetEventMouse(
                    &app->host, out.key_mouse_x - target->abs_x, out.key_mouse_y - target->abs_y);
                RS_CS2_SetEventKey(&app->host, codes[e], 0);
                if( getenv("TORIRS_KEY_DEBUG") )
                    fprintf(
                        stderr,
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
    if( app->slots.chat_index >= 0 && !app->locedit_visible )
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
            int left_held = LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);
            if( left_held && !out.minimenu_consumed_pointer && app->slots.chat_com_id == -1 &&
                app_chat_region(app, &rx, &ry, NULL) )
            {
                struct RS_ChatFilters filters = app_chat_filters(app);
                app->chat_scroll_cycle++;
                if( RS_Chat_ScrollbarInput(
                        &app->chat,
                        &filters,
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
            if( app_chat_region(app, &rx, &ry, NULL) )
            {
                struct UITreeComponent const* node = &app->tree->components[app->slots.chat_index];
                int bx = 0, by = 0, bw = 0, bh = 0;
                UITree_LayoutGetBounds(&node->position, &bx, &by, &bw, &bh);
                if( input->curr.mouse_x >= bx && input->curr.mouse_x < bx + bw &&
                    input->curr.mouse_y >= by && input->curr.mouse_y < by + bh )
                {
                    struct RS_ChatFilters filters = app_chat_filters(app);
                    RS_Chat_Scroll(&app->chat, &filters, input->curr.mouse_wheel_y);
                    app->need_redraw = 1;
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
     * here rather than in the focus tick because both submit paths -- the
     * clientscript dispatched in the keyboard broadcast, and the dat1
     * RS_Chat_HandleKey loop -- had to have the frame's Enter first. */
    if( chat_submit_pending && app->chat_input_active )
    {
        app->chat_input_active = 0;
        app->need_redraw = 1;
    }

    app_world_camera_keys(app, input, &out);
    app_world_camera_mouse(app, input, &out);
    /* Before the debug world hotkeys: a configured binding claims its key so
     * the same press cannot also spawn something. */
    app_ui_hotkeys(app, input);
    app_world_hotkeys(app, input, &out);
    app_inv_drag_tick(app, input, out.minimenu_consumed_pointer);
    app_worldmap_drag_tick(app, input, out.minimenu_consumed_pointer);

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

            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_CS2)
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
                        if( pickup_out.intents[i].hook )
                            hook_copies[i] = *pickup_out.intents[i].hook;
                    for( int i = 0; i < pickup_out.intent_count; i++ )
                    {
                        struct UIIntent const* intent = &pickup_out.intents[i];
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
        if( app->if_text_count > 0 && app->tree->generation != app->if_text_applied_gen )
        {
            app->if_text_applied_gen = app->tree->generation;
            for( int i = 0; i < app->if_text_count; i++ )
            {
                bool ok =
                    UITree_ApplyText(app->tree, app->if_texts[i].com_id, app->if_texts[i].text);
                if( !ok && getenv("TORIRS_NET_DEBUG") )
                    fprintf(
                        stderr,
                        "if_settext: reapply com=%d gen=%u missed\n",
                        app->if_texts[i].com_id,
                        app->tree->generation);
            }
        }
        if( app->if_hide_count > 0 && app->tree->generation != app->if_hide_applied_gen )
        {
            app->if_hide_applied_gen = app->tree->generation;
            for( int i = 0; i < app->if_hide_count; i++ )
            {
                bool ok =
                    UITree_ApplyHide(app->tree, app->if_hides[i].com_id, app->if_hides[i].hide);
                if( !ok && getenv("TORIRS_NET_DEBUG") )
                    fprintf(
                        stderr,
                        "if_sethide: reapply com=%d gen=%u missed\n",
                        app->if_hides[i].com_id,
                        app->tree->generation);
            }
        }
        if( app->if_colour_count > 0 && app->tree->generation != app->if_colour_applied_gen )
        {
            app->if_colour_applied_gen = app->tree->generation;
            for( int i = 0; i < app->if_colour_count; i++ )
            {
                bool ok = UITree_ApplyColour(
                    app->tree, app->if_colours[i].com_id, app->if_colours[i].colour);
                if( !ok && getenv("TORIRS_NET_DEBUG") )
                    fprintf(
                        stderr,
                        "if_setcolour: reapply com=%d gen=%u missed\n",
                        app->if_colours[i].com_id,
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
            char const* force_show = getenv("TORIRS_FORCE_SHOW_SLOT");
            if( force_show )
            {
                int want = (int)strtol(force_show, NULL, 0);
                int32_t idx = UITree_FindByComponentId(app->tree, want);
                if( idx >= 0 )
                    app->tree->components[idx].behavior.hide = 0;
            }
        }
        /* Publication invariant: an emit list is a frame commit, not a view of
         * whatever intermediate state the cooperative schedulers reached. */
        assert(App_FrameSettled(app));
        app_entity_overlay_layout(app);
        app->emit.count = 0;
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_EMIT)
        {
            UITree_EmitWalk(app->tree, &app->ui_host, &app->emit, app->hover_com_id);
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

int
App_FrameSettled(struct App const* app)
{
    assert(app);
    return !app->runner_had_work && !app->runner.frame_settle_pending &&
           !app->exec_runner_had_work && !app->server_tick_open &&
           app->pending_clientscript_count == 0;
}

int
App_InputFrameConsumed(struct App const* app)
{
    assert(app);
    return app->input_frame_consumed;
}

void
App_SendIdkDesign(
    struct App* app,
    int gender,
    int const kits[RS_IDK_DESIGN_PARTS],
    int const colours[RS_IDK_DESIGN_COLOURS])
{
    assert(app && kits && colours);
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
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
    int i;
    assert(app);
    for( i = 0; i < app->if_text_count; i++ )
        if( app->if_texts[i].com_id == com_id )
            break;
    if( i == app->if_text_count )
    {
        if( app->if_text_count == app->if_text_cap )
        {
            int cap = app->if_text_cap ? app->if_text_cap * 2 : 64;
            app->if_texts = realloc(app->if_texts, (size_t)cap * sizeof(*app->if_texts));
            assert(app->if_texts);
            app->if_text_cap = cap;
        }
        app->if_texts[i].com_id = com_id;
        app->if_texts[i].text = NULL;
        app->if_text_count++;
    }
    free(app->if_texts[i].text);
    app->if_texts[i].text = strdup(text ? text : "");
    {
        bool applied = UITree_ApplyText(app->tree, com_id, text);
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
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
    int i;
    assert(app);
    for( i = 0; i < app->if_colour_count; i++ )
        if( app->if_colours[i].com_id == com_id )
            break;
    if( i == app->if_colour_count )
    {
        if( app->if_colour_count == app->if_colour_cap )
        {
            int cap = app->if_colour_cap ? app->if_colour_cap * 2 : 64;
            app->if_colours = realloc(app->if_colours, (size_t)cap * sizeof(*app->if_colours));
            assert(app->if_colours);
            app->if_colour_cap = cap;
        }
        app->if_colours[i].com_id = com_id;
        app->if_colour_count++;
    }
    app->if_colours[i].colour = colour;
    {
        bool applied = UITree_ApplyColour(app->tree, com_id, colour);
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr, "if_setcolour: com=%d colour=%06x applied=%d\n", com_id, colour,
                (int)applied);
    }
    app->need_redraw = 1;
}

void
App_IfHideSet(
    struct App* app,
    int com_id,
    int hide)
{
    int i;
    assert(app);
    for( i = 0; i < app->if_hide_count; i++ )
        if( app->if_hides[i].com_id == com_id )
            break;
    if( i == app->if_hide_count )
    {
        if( app->if_hide_count == app->if_hide_cap )
        {
            int cap = app->if_hide_cap ? app->if_hide_cap * 2 : 64;
            app->if_hides = realloc(app->if_hides, (size_t)cap * sizeof(*app->if_hides));
            assert(app->if_hides);
            app->if_hide_cap = cap;
        }
        app->if_hides[i].com_id = com_id;
        app->if_hide_count++;
    }
    app->if_hides[i].hide = hide ? 1 : 0;
    {
        bool applied = UITree_ApplyHide(app->tree, com_id, hide);
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(stderr, "if_sethide: com=%d hide=%d applied=%d\n", com_id, hide, (int)applied);
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
    app_if_button_target(app, com_id, &target, &sub);
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(stderr, "if_button: com=%d target=%d sub=%d\n", com_id, target, sub);
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
    app_if_button_target(app, com_id, &target, &sub);
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(stderr, "resume_pausebutton: com=%d target=%d sub=%d\n", com_id, target, sub);
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
        if( pa && app_anim_playable(pa) )
        {
            app_element_set_anim(el, pa);
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
        if( sa && app_anim_playable(sa) )
        {
            app_element_set_anim(el, sa);
            el->anim_seq_id = anim->secondary.anim_id;
            el->anim_frame = anim->secondary.frame < sa->frame_count ? anim->secondary.frame : 0;
            return;
        }
    }

    app_element_set_anim(el, NULL);
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
    if( !el || el->model.kind != TORIDRAWMK_MODEL || !el->model.u.model.model )
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
         * the rest pose so the snapshot is the true base. */
        ToriDraw_ModelAnimateReset(el->model.u.model.model);
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
            fprintf(
                stderr,
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
static void
app_plugin_obj_notify(struct App* app, int idx, enum ToriRS_PluginEvent which)
{
    struct WorldEntity_ObjStack* stack;
    struct ToriRS_PluginObjSnap snap;

    assert(app);
    if( !app->plugins || !app->world || idx < 0 )
        return;
    stack = World_EntityPoolGet(&app->world->entities.obj_stack, idx);
    if( !stack )
        return;

    app_plugin_fill_obj(app, stack, &snap);
    switch( which )
    {
    case TORIRS_PLUGIN_EV_OBJ_SPAWN:
        PluginHost_ObjSpawn(app->plugins, &snap);
        break;
    case TORIRS_PLUGIN_EV_OBJ_COUNT:
        PluginHost_ObjCount(app->plugins, &snap);
        break;
    default:
        PluginHost_ObjDespawn(app->plugins, &snap);
        break;
    }
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
    int model_ids[1];
    int world_x = scene_x * 128 + 64;
    int world_z = scene_z * 128 + 64;
    int world_y;
    int element_id;
    int existing;

    assert(app);
    existing = World_ObjStackFind(app->world, scene_x, scene_z, level, obj_id);
    if( existing >= 0 )
    {
        World_ObjStackSetCount(app->world, existing, count);
        app_plugin_obj_notify(app, existing, TORIRS_PLUGIN_EV_OBJ_COUNT);
        app->need_redraw = 1;
        return existing;
    }

    obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
    if( !obj || obj->inventory_model_id <= 0 )
        return -1;
    model_ids[0] = obj->inventory_model_id;
    {
        struct AppModelRecolorSpec recolors = {
            .recolors_from = obj->recolors_from,
            .recolors_to = obj->recolors_to,
            .recolor_count = obj->recolor_count,
        };
        model = app_world_build_model(
            app, model_ids, 1, &recolors, 128, 128, APP_LIGHT_SCENE, obj->contrast, obj->ambient);
    }
    if( !model )
        return -1;

    /* LocType.raiseobject: world Y is negative-up, so subtracting raise lifts
     * the stack onto the table (Client-TS objs.y - objs.height). */
    world_y = app_world_height(app, world_x, world_z, level) -
              World_ObjRaiseGet(app->world, scene_x, scene_z, level);
    element_id = app_world_scene_element_create(app, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return -1;

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
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
            app->world, element_id, scene_x, scene_z, level, obj_id, count, obj->name, actions32);
        app_plugin_obj_notify(app, idx, TORIRS_PLUGIN_EV_OBJ_SPAWN);
        return idx;
    }
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
                app_plugin_obj_notify(app, i, TORIRS_PLUGIN_EV_OBJ_DESPAWN);
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

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(stderr, "rebuild_shift: dx=%d dz=%d\n", base_dx, base_dz);
    /* Projectiles/spotanims/far stacks queued EntityRemoved above — free their
     * DYNAMIC scene elements now so the next frame does not race a full queue. */
    App_WorldDrainEntityRemoved(app);
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
    assert(app);
    idx = World_ObjStackFind(app->world, scene_x, scene_z, level, obj_id);
    if( idx >= 0 )
    {
        /* Ahead of the release, so a plugin's last look at the stack is a
         * whole one -- the same ordering the npc despawn path uses. */
        app_plugin_obj_notify(app, idx, TORIRS_PLUGIN_EV_OBJ_DESPAWN);
        World_ObjStackDel(app->world, idx);
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
    assert(app);
    idx = World_ObjStackFind(app->world, scene_x, scene_z, level, obj_id);
    if( idx < 0 )
        return;
    World_ObjStackSetCount(app->world, idx, count);
    app_plugin_obj_notify(app, idx, TORIRS_PLUGIN_EV_OBJ_COUNT);
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
    assert(app);
    /* obj_id -1 = any, so this drains the tile one stack at a time. */
    while( (idx = World_ObjStackFind(app->world, scene_x, scene_z, level, -1)) >= 0 )
    {
        app_plugin_obj_notify(app, idx, TORIRS_PLUGIN_EV_OBJ_DESPAWN);
        World_ObjStackDel(app->world, idx);
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
    int scene_x,
    int scene_z,
    int level,
    int loc_shape,
    int seq_id)
{
    int idx;
    assert(app);
    idx = World_SceneryFindAt(app->world, scene_x, scene_z, level, loc_shape);
    if( idx >= 0 )
    {
        struct WorldEntity_Scenery* scenery =
            World_EntityPoolGet(&app->world->entities.scenery, idx);
        if( scenery && scenery->element_id >= 0 )
        {
            struct ToriDraw_SceneElement* element =
                ToriDraw_SceneElementGet(app->scene, scenery->element_id);

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
            if( element && element->model.kind == TORIDRAWMK_MODEL &&
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
        fprintf(
            stderr,
            "npc_retype: world_idx=%d element=%d type=%d\n",
            world_idx,
            element_id,
            npc_type);

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
            fprintf(stderr, "npc_retype: npc %d models failed to load\n", npc_type);
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
        World_NpcSetType(
            app->world, world_idx, npc_type, facts.size, &idle);
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
                struct ToriRS_PluginNpcSnap retyped;
                app_plugin_fill_npc(app, npc, &retyped);
                PluginHost_NpcRetype(app->plugins, &retyped);
            }
            if( getenv("TORIRS_NET_DEBUG") )
                fprintf(
                    stderr,
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

void
App_Render(
    struct App* app,
    int* pixels,
    int width,
    int height)
{
    struct ToriRS_Frame frame;
    struct ToriRS_Soft3D soft;

    assert(app);
    assert(pixels);

    if( !App_BuildFrame(app, &frame, width, height) )
    {
        /* Font-free loading screen: dark clear + centered progress bar.
         * Deliberately independent of every asset pipeline (they are what is
         * still loading). */
        int bar_w = width / 3;
        int bar_h = 12;
        int bar_x = (width - bar_w) / 2;
        int bar_y = (height - bar_h) / 2;
        int fill_w = bar_w * (app->boot_progress < 0 ? 0 : app->boot_progress) / 100;

        for( int i = 0; i < width * height; i++ )
            pixels[i] = 0x000000;
        for( int y = bar_y - 1; y <= bar_y + bar_h; y++ )
            for( int x = bar_x - 1; x <= bar_x + bar_w; x++ )
            {
                if( y < 0 || y >= height || x < 0 || x >= width )
                    continue;
                int border = (y < bar_y || y >= bar_y + bar_h || x < bar_x || x >= bar_x + bar_w);
                int filled = !border && (x - bar_x) < fill_w;
                pixels[y * width + x] = border ? 0x8b0000 : (filled ? 0x8b0000 : 0x000000);
            }
        return;
    }

    ToriRS_Soft3D_Init(&soft, app->scene, pixels, width, height);

    /* World hittest rides the render: each visible model is tested against
     * the mouse point right after it projects (the only window where the
     * scene scratch holds its projection), then the raw hits classify into
     * the pickset + hover tile the click/hotkey paths consume next frame. */
    if( app_world_drawable(app) && app->world_mouse_in_viewport )
        ToriRS_Soft3D_SetPick(&soft, app->world_mouse_x, app->world_mouse_y);

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_RENDER)
    {
        ToriRS_Soft3D_RenderFrame(&soft, &frame);
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

    if( getenv("TORIRS_FRAME_DEBUG") )
        fprintf(
            stderr,
            "frame: draws element=%d terrain=%d dropped not_live=%d no_model=%d\n",
            frame.dbg_emit_element,
            frame.dbg_emit_terrain,
            frame.dbg_drop_not_live,
            frame.dbg_drop_no_model);

    if( soft.pick_enabled )
        App_PickFinish(app, &soft.pick_hits);
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
    struct App* app, struct ToriRSChromeExec const* exec, int kind, int explicit_choice)
{
    assert(app);
    assert(exec);
    app->plugin_exec_pending = *exec;
    app->plugin_exec_explicit = explicit_choice ? 1 : 0;
    /* Carried so a refusal can name what refused. Without it the fallback
     * message said "the 'buffer' executor would not start", which is both
     * impossible and unhelpful. */
    app->plugin_exec_kind = kind;
}
