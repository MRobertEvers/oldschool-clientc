/*
 * The agent server's editor verbs (docs/AGENT_API.md): editor (the state a
 * person reads off the status bar and the panels), actions and action (the
 * hotkey table by id), tile_to_screen, screen_to_tile and world_click (the
 * World, by tile), and camera.
 *
 * Everything is read from struct EditorUi (editor/editor_ui.h) and asked of
 * the App through the same services the panels use
 * (struct EditorUiAppServices: the hover, the scene window, the camera), so
 * this file states no rule of the editor's a second time. The one thing it
 * computes itself is where a tile is on screen, with the projection the
 * App's overlays and the quest driver share (render/torirs_world_projection.h)
 * over the frame's camera and the World's viewport.
 */

#include "agent/agent_verbs.h"

#if !defined(__EMSCRIPTEN__)

#include "app.h"
#include "editor/editor.h"
#include "editor/editor_doc.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_panel.h"
#include "editor/editor_ui.h"
#include "editor/editor_view.h"
#include "editor/live/live_panel.h"
#include "editor/panels/edpanels.h"
#include "input/torirs_input.h"
#include "render/torirs_world_projection.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_live.h"
#include "ui/uitree_debug_overlay.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct EditorUi*
Agent_EditorUi(const struct AgentCtx* ctx)
{
    assert(ctx);
    assert(ctx->app);
    if( !ctx->app->editor || !ctx->app->editor_panel.built )
        return NULL;
    return ctx->app->editor_panel.ui;
}

struct EditorUi*
Agent_NeedEditor(struct AgentCtx* ctx)
{
    struct EditorUi* eui = Agent_EditorUi(ctx);

    if( eui )
        return eui;
    if( !ctx->app->editor )
        Agent_ReplyError(ctx, "not an editor window: this client was not booted with the map editor (hello's mode is "
                              "\"game\")");
    else
        Agent_ReplyError(ctx, "the editor's panels are not up yet: wait until \"ready\" and ask again");
    return NULL;
}

/** The level a request means when it names none, and the one the next edit
 *  goes to: the pinned plane, else the ground. */
static int
agent_edit_level(const struct EditorUi* eui)
{
    return eui->tools.edit_level >= 0 ? eui->tools.edit_level : 0;
}

/** The console lines said since `serial`, oldest first, as "lines". */
static void
agent_write_console_since(
    struct AgentJsonWriter* out,
    const struct EditorUi* eui,
    int serial)
{
    int fresh = eui->console_serial - serial;

    if( fresh < 0 || fresh > eui->console_count )
        fresh = eui->console_count;
    if( fresh > 40 )
        fresh = 40;
    AgentJsonW_Key(out, "lines");
    AgentJsonW_ArrBegin(out);
    for( int i = fresh - 1; i >= 0; i-- )
        AgentJsonW_Str(out, Editor_UiConsoleLine(eui, i));
    AgentJsonW_ArrEnd(out);
}

/* ---- editor ---------------------------------------------------------------------------- */

/** The World's camera as a pose read on `level`'s ground: 1 with `out`, 0
 *  when the window has no world or the eye is outside the loaded scene. */
static int
agent_camera_read(
    struct EditorUi* eui,
    int level,
    struct EditorViewCamera* out)
{
    if( !eui->app_services.world_camera )
        return 0;
    return eui->app_services.world_camera(eui->app_services.user_data, level, out);
}

static void
agent_write_camera(
    struct AgentJsonWriter* out,
    const struct EditorViewCamera* camera)
{
    AgentJsonW_ObjBegin(out);
    /* The tile the camera looks at, its plane, and the pose around it. */
    AgentJsonW_KInt(out, "x", camera->x / 128);
    AgentJsonW_KInt(out, "z", camera->z / 128);
    AgentJsonW_KInt(out, "level", camera->level);
    AgentJsonW_KInt(out, "yaw", camera->yaw);
    AgentJsonW_KInt(out, "pitch", camera->pitch);
    AgentJsonW_KInt(out, "zoom", camera->zoom);
    AgentJsonW_ObjEnd(out);
}

static enum AgentVerbResult
verb_editor(struct AgentCtx* ctx)
{
    struct EditorUi* eui = Agent_NeedEditor(ctx);
    struct App* app = ctx->app;
    struct AgentJsonWriter* out;
    const struct Editor_LiveModel* live;
    struct EditorViewCamera camera;
    struct EditorUiRect world;
    int hover[3];
    int base_x = 0;
    int base_z = 0;
    int size = 0;

    if( !eui )
        return AGENT_VERB_DONE;
    live = Editor_LivePanelModel(eui);
    world = Editor_UiWorldRect(eui);

    out = Agent_ReplyBegin(ctx);
    /* Closed panels (the editor's own toggle key) leave the world on screen
     * and nothing to press. */
    AgentJsonW_KBool(out, "panels_open", app->editor_panel.visible != 0);
    AgentJsonW_KInt(out, "tool", (int)eui->tools.tool);
    AgentJsonW_KStr(out, "tool_name", Editor_ToolName(eui->tools.tool));
    /* -1: Auto, the plane under the pointer. */
    AgentJsonW_KInt(out, "level", eui->tools.edit_level);
    AgentJsonW_KStr(out, "workspace", Editor_UiWorkspaceName(eui->workspace));
    if( eui->lane.known )
    {
        AgentJsonW_KStr(out, "lane", eui->lane.name);
        if( eui->lane.changes_known )
            AgentJsonW_KInt(out, "lane_changes", eui->lane.changes);
    }
    AgentJsonW_KBool(out, "writable", app->editor->writable != 0 && (!eui->lane.known || eui->lane.writable));
    if( !app->editor->writable && app->editor->read_only_reason[0] )
        AgentJsonW_KStr(out, "read_only_reason", app->editor->read_only_reason);
    /* Open documents with edits no save has taken yet. */
    AgentJsonW_KInt(out, "unsaved", Editor_DocUnsavedCount(&app->editor->doc));

    AgentJsonW_Key(out, "squares_open");
    AgentJsonW_ArrBegin(out);
    for( int i = 0; i < app->editor->doc.square_count; i++ )
    {
        const struct Editor_Square* square = &app->editor->doc.squares[i];
        if( !square->loaded )
            continue;
        AgentJsonW_ObjBegin(out);
        AgentJsonW_KInt(out, "x", square->map_x);
        AgentJsonW_KInt(out, "z", square->map_z);
        /* "" is the content tree's maps/. */
        if( app->editor->doc.square_storage[i][0] )
            AgentJsonW_KStr(out, "storage", app->editor->doc.square_storage[i]);
        if( square->dirty_map || square->dirty_loc )
            AgentJsonW_KBool(out, "unsaved", 1);
        AgentJsonW_ObjEnd(out);
    }
    AgentJsonW_ArrEnd(out);

    AgentJsonW_Key(out, "selection");
    AgentJsonW_ObjBegin(out);
    AgentJsonW_KInt(out, "locs", eui->tools.loc_count);
    if( eui->tools.loc_count > 0 )
    {
        const struct Editor_LocRef* first = &eui->tools.locs[0];
        AgentJsonW_Key(out, "first_loc");
        AgentJsonW_ObjBegin(out);
        AgentJsonW_KInt(out, "id", first->loc_id);
        AgentJsonW_KInt(out, "x", first->map_x * 64 + first->x);
        AgentJsonW_KInt(out, "z", first->map_z * 64 + first->z);
        AgentJsonW_KInt(out, "level", first->level);
        AgentJsonW_KInt(out, "shape", first->shape);
        AgentJsonW_KInt(out, "rotation", first->rotation);
        AgentJsonW_ObjEnd(out);
    }
    if( eui->tools.tile_valid )
    {
        int const tile[3] = { eui->tools.tile_x, eui->tools.tile_z, eui->tools.tile_level };
        AgentJsonW_KInts(out, "tile", tile, 3);
    }
    if( eui->tools.rect.valid )
    {
        int const rect[5] = { eui->tools.rect.x0, eui->tools.rect.z0, eui->tools.rect.x1, eui->tools.rect.z1,
            eui->tools.rect.level };
        /* [x0, z0, x1, z1, level], both corners inside. */
        AgentJsonW_KInts(out, "rect", rect, 5);
    }
    AgentJsonW_KInt(out, "spawns", Editor_Spawns_SelectionCount(eui));
    AgentJsonW_ObjEnd(out);

    /* What the catalog has picked: what Place stamps. */
    if( eui->tools.pick_kind >= 0 && eui->tools.pick_id >= 0 )
    {
        static const char* const k_kinds[] = { "loc", "npc", "obj" };
        AgentJsonW_Key(out, "pick");
        AgentJsonW_ObjBegin(out);
        AgentJsonW_KStr(out, "kind", eui->tools.pick_kind < 3 ? k_kinds[eui->tools.pick_kind] : "?");
        AgentJsonW_KInt(out, "id", eui->tools.pick_id);
        AgentJsonW_KStr(out, "name", eui->tools.pick_name);
        AgentJsonW_ObjEnd(out);
    }

    if( eui->app_services.hover_tile
        && eui->app_services.hover_tile(eui->app_services.user_data, &hover[0], &hover[1], &hover[2]) )
        AgentJsonW_KInts(out, "hover_tile", hover, 3);

    /* The 3D view's box on the surface (all zero while the World panel is
     * closed), and the tiles the loaded scene holds. */
    AgentJsonW_KRect(out, "world_rect", world.x, world.y, world.w, world.h);
    if( eui->app_services.scene_window
        && eui->app_services.scene_window(eui->app_services.user_data, &base_x, &base_z, &size) )
    {
        AgentJsonW_Key(out, "scene");
        AgentJsonW_ObjBegin(out);
        AgentJsonW_KInt(out, "x", base_x);
        AgentJsonW_KInt(out, "z", base_z);
        AgentJsonW_KInt(out, "size", size);
        AgentJsonW_ObjEnd(out);
    }
    if( agent_camera_read(eui, agent_edit_level(eui), &camera) )
    {
        AgentJsonW_Key(out, "camera");
        agent_write_camera(out, &camera);
    }

    AgentJsonW_KStr(out, "status", Editor_UiStatusText(eui));
    if( live )
    {
        /* As the Live panel words it: a RUN in flight, else how the last one
         * ended, else none yet. */
        const char* run = Editor_Live_Busy(eui) == 1 ? "running"
                          : live->run_serial > 0     ? (live->run_status == 0 ? "done" : "stopped")
                                                     : "idle";
        AgentJsonW_KStr(out, "run", run);
        if( live->run_step != TORIRSMAPED_RUN_STEP_IDLE )
            AgentJsonW_KStr(out, "run_step", ToriRSMapEd_RunStepName(live->run_step));
        if( live->run_serial > 0 && live->run_message[0] )
            AgentJsonW_KStr(out, "run_message", live->run_message);
        AgentJsonW_KStr(out, "live", ToriRSMapEd_LiveStateName(live->state));
        if( live->message[0] )
            AgentJsonW_KStr(out, "live_message", live->message);
    }
    else
        AgentJsonW_KStr(out, "run", "idle");
    AgentJsonW_KInt(out, "console_cursor", eui->console_serial);
    Agent_ReplyEnd(ctx);
    return AGENT_VERB_DONE;
}

/* ---- actions, action -------------------------------------------------------------------- */

/** The EDITOR_HK_CTX_* mask live now: what the App's key dispatcher resolved
 *  the keys in on its last frame; G alone before it has run. */
static unsigned
agent_live_contexts(const struct EditorUi* eui)
{
    return (eui->hotkey_contexts_valid ? eui->hotkey_contexts : 0u) | EDITOR_HK_CTX_G;
}

static enum AgentVerbResult
verb_actions(struct AgentCtx* ctx)
{
    struct EditorUi* eui = Agent_NeedEditor(ctx);
    struct AgentJsonWriter* out;
    const char* text = NULL;
    unsigned contexts;

    if( !eui )
        return AGENT_VERB_DONE;
    if( Agent_ArgString(ctx, "text", &text) < 0 )
        return AGENT_VERB_DONE;
    contexts = agent_live_contexts(eui);

    out = Agent_ReplyBegin(ctx);
    AgentJsonW_Key(out, "actions");
    AgentJsonW_ArrBegin(out);
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo((enum Editor_HotkeyAction)a);

        if( text && !strstr(info->id, text) && !strstr(info->name, text) && !strstr(info->group, text) )
            continue;
        AgentJsonW_ObjBegin(out);
        AgentJsonW_KStr(out, "id", info->id);
        AgentJsonW_KStr(out, "group", info->group);
        AgentJsonW_KStr(out, "name", info->name);
        /* In the `key` verb's own spelling ("ctrl+s"), so a chord read here
         * can be pressed there. */
        AgentJsonW_Key(out, "chords");
        AgentJsonW_ArrBegin(out);
        for( int c = 0; c < EDITOR_HK_CHORDS; c++ )
        {
            char chord[48];
            Editor_ChordFormat(eui->hotkeys.bind[a].chord[c], chord, (int)sizeof(chord));
            if( chord[0] )
                AgentJsonW_Str(out, chord);
        }
        AgentJsonW_ArrEnd(out);
        /* Its chord would fire now: a context it lives in is live (a loc is
         * selected, the pointer is over the World, ...). `action` runs it
         * whatever this says, as the command palette does. */
        AgentJsonW_KBool(out, "enabled", (info->contexts & contexts) != 0);
        /* A held action is a key held over the World (the camera's flight,
         * the click accelerators): `key` down / up, not `action`. */
        if( info->kind == EDITOR_HK_HELD )
            AgentJsonW_KBool(out, "held", 1);
        AgentJsonW_ObjEnd(out);
    }
    AgentJsonW_ArrEnd(out);
    Agent_ReplyEnd(ctx);
    return AGENT_VERB_DONE;
}

static int
action_step(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    struct EditorUi* eui = Agent_EditorUi(ctx);
    struct AgentJsonWriter* out;
    const struct Editor_HotkeyActionInfo* info;

    if( ctx->frame < op->until_frame )
        return 0;
    if( !eui )
    {
        Agent_ReplyError(ctx, "the editor closed while the action ran");
        return 1;
    }
    info = Editor_HotkeyInfo((enum Editor_HotkeyAction)op->ints[0]);
    out = Agent_ReplyBegin(ctx);
    AgentJsonW_KStr(out, "action", info->id);
    AgentJsonW_KStr(out, "name", info->name);
    /* Who ran it: the editor's panels at once, or the app a frame later (the
     * ones that need the world under the pointer). */
    AgentJsonW_KStr(out, "by", op->ints[2] ? "app" : "editor");
    /* What it said: a refusal is a sentence in the status bar and the
     * console ("Place refused: ..."), never silence. */
    AgentJsonW_KStr(out, "status", Editor_UiStatusText(eui));
    agent_write_console_since(out, eui, op->ints[1]);
    AgentJsonW_KInt(out, "console_cursor", eui->console_serial);
    Agent_ReplyEnd(ctx);
    return 1;
}

static enum AgentVerbResult
verb_action(struct AgentCtx* ctx)
{
    struct EditorUi* eui = Agent_NeedEditor(ctx);
    struct AgentOp* op;
    const struct Editor_HotkeyActionInfo* info;
    const struct Editor_CmdPalList* list;
    const char* id = NULL;
    int action;
    int row = -1;
    int serial;
    int app_bound = 0;

    if( !eui )
        return AGENT_VERB_DONE;
    /* "action", not "id": a request's "id" is the protocol's own. */
    if( !Agent_NeedString(ctx, "action", &id) )
        return AGENT_VERB_DONE;
    action = Editor_HotkeyFind(id);
    if( action < 0 )
    {
        Agent_ReplyError(ctx, "there is no action \"%s\": actions lists their ids", id);
        return AGENT_VERB_DONE;
    }
    info = Editor_HotkeyInfo((enum Editor_HotkeyAction)action);
    if( info->kind == EDITOR_HK_HELD )
    {
        Agent_ReplyError(ctx, "\"%s\" is a held action (%s): hold its chord with key's \"down\" and \"up\" instead", id,
            info->name);
        return AGENT_VERB_DONE;
    }
    if( !ctx->app->editor_panel.visible )
    {
        Agent_ReplyError(ctx, "the editor's panels are closed, and its actions run only while they are open: open "
                              "them with the map editor's toggle key first");
        return AGENT_VERB_DONE;
    }
    serial = eui->console_serial;

    /*
     * As the command palette's row runs an action, which is the one existing
     * way to run one by name: where the menu bar does (Editor_HotkeyRun), and
     * when that answers 0 -- only the App can perform it: the camera, the
     * eyedroppers, Paste at the hovered tile -- left for the App's key
     * dispatcher to take on its next frame
     * (Editor_CmdPalette_TakePendingAction -> app_hk_run).
     *
     * The palette holds that queue and gives no way in but its row, so an
     * App-bound action goes through the row itself: the palette is opened,
     * searched for the action's id and its row run, all within this call, so
     * it is never drawn. Only those few pay for the search (the palette asks
     * the catalog about what was typed, as it does for a person).
     */
    if( !Editor_HotkeyRun(eui, (enum Editor_HotkeyAction)action) )
    {
        app_bound = 1;
        if( !Editor_CmdPalette_Open(eui) )
        {
            Agent_ReplyError(ctx, "this window has no command palette to run \"%s\" through", id);
            return AGENT_VERB_DONE;
        }
        Editor_CmdPalette_SetQuery(eui, info->id);
        list = Editor_CmdPalette_List(eui);
        for( int r = 0; list && r < list->row_count && row < 0; r++ )
            if( list->row_kind[r] == EDITOR_CMDPAL_ROW_ITEM && list->items[list->row_item[r]].kind == EDITOR_CMDPAL_KIND_ACTION
                && list->items[list->row_item[r]].target == action )
                row = r;
        if( row < 0 )
        {
            Editor_CmdPalette_Close(eui);
            Agent_ReplyError(ctx, "the command palette does not offer \"%s\", so it cannot be run by name: press its "
                                  "chord with key instead",
                id);
            return AGENT_VERB_DONE;
        }
        (void)Editor_CmdPalette_RunRow(eui, row);
    }
    ctx->app->need_redraw = 1;

    /* Two frames: the one the App takes an App-bound action on, and the one
     * that draws what it did. */
    op = Agent_OpArm(ctx, action_step, 0);
    op->ints[0] = action;
    op->ints[1] = serial;
    op->ints[2] = app_bound;
    op->until_frame = ctx->frame + 2;
    return AGENT_VERB_PENDING;
}

/* ---- the World by tile ------------------------------------------------------------------- */

enum AgentTileSight
{
    AGENT_TILE_VISIBLE = 0,
    /** Not in the loaded scene at all. */
    AGENT_TILE_NOT_LOADED,
    /** Behind the camera, or there is no world view this frame. */
    AGENT_TILE_BEHIND,
    /** Projects outside the World panel's body. */
    AGENT_TILE_OFF_VIEW,
    /** Inside it, under a panel drawn over the World. */
    AGENT_TILE_COVERED,
    /** On the loaded scene's first row or column, which the overlay
     *  projection does not place. */
    AGENT_TILE_SCENE_EDGE,
};

static const char*
agent_tile_sight_name(enum AgentTileSight sight)
{
    static const char* const k_names[] = { "visible",  "not_loaded", "behind_camera",
                                           "off_view", "covered",    "scene_edge" };
    return k_names[sight];
}

/** The same, as the end of "it is ...". */
static const char*
agent_tile_sight_words(enum AgentTileSight sight)
{
    static const char* const k_words[] = {
        "visible",
        "not in the loaded scene",
        "behind the camera",
        "outside the World view",
        "under a panel drawn over the World",
        "on the loaded scene's edge row, where nothing is placed on screen",
    };
    return k_words[sight];
}

/**
 * Where ABSOLUTE tile (x, z) of `level` is on the surface: its centre on that
 * level's ground, through the frame's camera into the World's viewport --
 * app_world_project_at's arithmetic (the App's overlays, ghosts and gizmo),
 * from its public parts.
 */
static enum AgentTileSight
agent_tile_project(
    struct AgentCtx* ctx,
    struct EditorUi* eui,
    int x,
    int z,
    int level,
    int* out_x,
    int* out_y)
{
    struct App* app = ctx->app;
    struct EditorUiRect const world = Editor_UiWorldRect(eui);
    int scene_x;
    int scene_z;
    int fine_x;
    int fine_z;

    *out_x = 0;
    *out_y = 0;
    if( !app->world )
        return AGENT_TILE_NOT_LOADED;
    scene_x = x - app->world->_base_tile_x;
    scene_z = z - app->world->_base_tile_z;
    if( scene_x < 0 || scene_z < 0 || scene_x >= app->world->_scene_size || scene_z >= app->world->_scene_size )
        return AGENT_TILE_NOT_LOADED;
    fine_x = scene_x * 128 + 64;
    fine_z = scene_z * 128 + 64;
    /* The overlay projection refuses the scene's first row and column, and
     * keeps the reference's near plane at 50 (app_world_project_at). */
    if( fine_x < 128 || fine_z < 128 )
        return AGENT_TILE_SCENE_EDGE;
    if( !app->world_view_valid || !app->frame_view )
        return AGENT_TILE_BEHIND;
    if( !ToriRS_WorldProjectPoint(&app->frame_view->world_camera, &app->frame_view->world_camera_pos,
            app->world_emit_desc.x, app->world_emit_desc.y, app->world_emit_desc.w, app->world_emit_desc.h, 50, fine_x,
            World_HeightAt(app->world, fine_x, fine_z, level), fine_z, out_x, out_y) )
        return AGENT_TILE_BEHIND;
    if( !Editor_UiWorldShown(eui) || *out_x < world.x || *out_y < world.y || *out_x >= world.x + world.w
        || *out_y >= world.y + world.h )
        return AGENT_TILE_OFF_VIEW;
    if( App_ChromePointerOwned(app, *out_x, *out_y) )
        return AGENT_TILE_COVERED;
    return AGENT_TILE_VISIBLE;
}

/** x, z and the optional level of a tile request: 1, or 0 with the error
 *  written. */
static int
agent_arg_tile(
    struct AgentCtx* ctx,
    const struct EditorUi* eui,
    int* out_x,
    int* out_z,
    int* out_level)
{
    *out_level = agent_edit_level(eui);
    if( !Agent_NeedInt(ctx, "x", out_x) || !Agent_NeedInt(ctx, "z", out_z) )
        return 0;
    if( Agent_ArgInt(ctx, "level", out_level) < 0 )
        return 0;
    if( *out_level < 0 || *out_level > 3 )
    {
        Agent_ReplyError(ctx, "\"level\" is 0..3, not %d", *out_level);
        return 0;
    }
    return 1;
}

static void
agent_reply_not_loaded(
    struct AgentCtx* ctx,
    int x,
    int z)
{
    struct App const* app = ctx->app;

    if( !app->world )
        Agent_ReplyError(ctx, "no square is open, so tile %d,%d is nowhere on screen: open m%d_%d first", x, z, x / 64,
            z / 64);
    else
        Agent_ReplyError(ctx, "tile %d,%d is not in the loaded scene (tiles %d,%d to %d,%d): open its square m%d_%d "
                              "first",
            x, z, app->world->_base_tile_x, app->world->_base_tile_z,
            app->world->_base_tile_x + app->world->_scene_size - 1,
            app->world->_base_tile_z + app->world->_scene_size - 1, x / 64, z / 64);
}

static enum AgentVerbResult
verb_tile_to_screen(struct AgentCtx* ctx)
{
    struct EditorUi* eui = Agent_NeedEditor(ctx);
    struct AgentJsonWriter* out;
    enum AgentTileSight sight;
    int x = 0;
    int z = 0;
    int level = 0;
    int px;
    int py;

    if( !eui || !agent_arg_tile(ctx, eui, &x, &z, &level) )
        return AGENT_VERB_DONE;
    sight = agent_tile_project(ctx, eui, x, z, level, &px, &py);
    if( sight == AGENT_TILE_NOT_LOADED )
    {
        agent_reply_not_loaded(ctx, x, z);
        return AGENT_VERB_DONE;
    }
    out = Agent_ReplyBegin(ctx);
    /* No point for a tile the projection cannot place: it has none. */
    if( sight != AGENT_TILE_BEHIND && sight != AGENT_TILE_SCENE_EDGE )
    {
        AgentJsonW_KInt(out, "x", px);
        AgentJsonW_KInt(out, "y", py);
    }
    AgentJsonW_KBool(out, "visible", sight == AGENT_TILE_VISIBLE);
    if( sight != AGENT_TILE_VISIBLE )
        AgentJsonW_KStr(out, "why", agent_tile_sight_name(sight));
    AgentJsonW_KInt(out, "level", level);
    Agent_ReplyEnd(ctx);
    return AGENT_VERB_DONE;
}

/** 1 when (x, y) is a point of the World the pointer can rest on; else 0 with
 *  the error written. */
static int
agent_need_world_point(
    struct AgentCtx* ctx,
    struct EditorUi* eui,
    int x,
    int y)
{
    struct EditorUiRect const world = Editor_UiWorldRect(eui);

    if( !Agent_NeedOnSurface(ctx, x, y) )
        return 0;
    if( !Editor_UiWorldShown(eui) || world.w <= 0 )
    {
        Agent_ReplyError(ctx, "the World panel is closed, so no point of the surface is a tile: open it (action "
                              "view.world)");
        return 0;
    }
    if( x < world.x || y < world.y || x >= world.x + world.w || y >= world.y + world.h )
    {
        Agent_ReplyError(ctx, "(%d, %d) is outside the World view, which is [%d, %d, %d, %d]", x, y, world.x, world.y,
            world.w, world.h);
        return 0;
    }
    if( App_ChromePointerOwned(ctx->app, x, y) )
    {
        Agent_ReplyError(ctx, "(%d, %d) is under a panel drawn over the World, so the pointer there is on the panel",
            x, y);
        return 0;
    }
    return 1;
}

/** Frames between parking the pointer and reading the hover: the frame that
 *  takes the move, the frame that renders the pick at it, and one to spare. */
#define AGENT_HOVER_FRAMES 3
/** More frames world_click gives the hover to become the tile it aimed at
 *  before it refuses the click. */
#define AGENT_HOVER_WAIT_FRAMES 8

static void
screen_to_tile_finish(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    struct EditorUi* eui = Agent_EditorUi(ctx);
    struct AgentJsonWriter* out;
    int tile[3];

    if( !eui || !eui->app_services.hover_tile
        || !eui->app_services.hover_tile(eui->app_services.user_data, &tile[0], &tile[1], &tile[2]) )
    {
        Agent_ReplyError(ctx, "no tile is under (%d, %d): the view shows nothing there (the sky, or ground no open "
                              "square holds)",
            op->ints[0], op->ints[1]);
        return;
    }
    out = Agent_ReplyBegin(ctx);
    AgentJsonW_KInt(out, "x", tile[0]);
    AgentJsonW_KInt(out, "z", tile[1]);
    /* The level the next edit there goes to. */
    AgentJsonW_KInt(out, "level", tile[2]);
    Agent_ReplyEnd(ctx);
}

static enum AgentVerbResult
verb_screen_to_tile(struct AgentCtx* ctx)
{
    struct EditorUi* eui = Agent_NeedEditor(ctx);
    struct AgentOp* op;
    int x = 0;
    int y = 0;

    if( !eui || !Agent_NeedInt(ctx, "x", &x) || !Agent_NeedInt(ctx, "y", &y) )
        return AGENT_VERB_DONE;
    if( !agent_need_world_point(ctx, eui, x, y) )
        return AGENT_VERB_DONE;
    /* The editor's own hover is the answer: the pick the frame stamps at the
     * pointer, on the pinned plane when a level is pinned. So the pointer
     * goes there, as a person's would to read the status bar. */
    op = Agent_ScriptArm(ctx, NULL, screen_to_tile_finish);
    op->ints[0] = x;
    op->ints[1] = y;
    Agent_ScriptMove(op, x, y);
    Agent_ScriptFrames(op, AGENT_HOVER_FRAMES);
    return AGENT_VERB_PENDING;
}

/** Slots of AgentOp.ints for world_click. */
enum
{
    WC_X = 0,
    WC_Z,
    WC_LEVEL,
    WC_PX,
    WC_PY,
    WC_FORCE,
    WC_SERIAL,
};

static int
world_click_check(
    struct AgentCtx* ctx,
    struct AgentOp* op,
    int value)
{
    struct EditorUi* eui = Agent_EditorUi(ctx);
    int tile[3];

    (void)value;
    if( !eui )
    {
        Agent_ReplyError(ctx, "the editor closed before the click");
        return 0;
    }
    if( op->ints[WC_FORCE] )
        return 1;
    if( eui->app_services.hover_tile
        && eui->app_services.hover_tile(eui->app_services.user_data, &tile[0], &tile[1], &tile[2])
        && tile[0] == op->ints[WC_X] && tile[1] == op->ints[WC_Z] && tile[2] == op->ints[WC_LEVEL] )
        return 1;
    /* The hover is the pick of the last frame that RENDERED with the pointer
     * there: a client busy loading may take a few more frames to draw one. */
    if( op->phase < AGENT_HOVER_WAIT_FRAMES )
    {
        op->phase++;
        ctx->app->need_redraw = 1;
        return 2;
    }
    if( !eui->app_services.hover_tile
        || !eui->app_services.hover_tile(eui->app_services.user_data, &tile[0], &tile[1], &tile[2]) )
    {
        Agent_ReplyError(ctx,
            "tile %d,%d was not clicked: with the pointer on it at (%d, %d) the editor hovers no tile there (a "
            "panel, the menu, or nothing drawn); \"force\": true clicks the point anyway",
            op->ints[WC_X], op->ints[WC_Z], op->ints[WC_PX], op->ints[WC_PY]);
        return 0;
    }
    Agent_ReplyError(ctx,
        "tile %d,%d level %d was not clicked: the pointer at its point (%d, %d) is on tile %d,%d level %d -- "
        "something nearer the camera is in front, or the edit level is another; pin the level (action level.pin%d), "
        "move the camera, or pass \"force\": true to click the point anyway",
        op->ints[WC_X], op->ints[WC_Z], op->ints[WC_LEVEL], op->ints[WC_PX], op->ints[WC_PY], tile[0], tile[1],
        tile[2], op->ints[WC_LEVEL]);
    return 0;
}

static void
world_click_finish(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    struct EditorUi* eui = Agent_EditorUi(ctx);
    struct AgentJsonWriter* out = Agent_ReplyBegin(ctx);
    int const tile[3] = { op->ints[WC_X], op->ints[WC_Z], op->ints[WC_LEVEL] };

    AgentJsonW_KInts(out, "tile", tile, 3);
    AgentJsonW_KInt(out, "x", op->ints[WC_PX]);
    AgentJsonW_KInt(out, "y", op->ints[WC_PY]);
    if( eui )
    {
        /* What the click did, in the editor's own words. */
        AgentJsonW_KStr(out, "status", Editor_UiStatusText(eui));
        agent_write_console_since(out, eui, op->ints[WC_SERIAL]);
        AgentJsonW_KInt(out, "locs_selected", eui->tools.loc_count);
    }
    /* A right click opens the menu: ui lists its rows, ui_click presses one. */
    if( ctx->app->frame_view && ctx->app->frame_view->minimenu && ctx->app->frame_view->minimenu->visible )
        AgentJsonW_KBool(out, "minimenu", 1);
    Agent_ReplyEnd(ctx);
}

static enum AgentVerbResult
verb_world_click(struct AgentCtx* ctx)
{
    struct EditorUi* eui = Agent_NeedEditor(ctx);
    struct AgentOp* op;
    enum AgentTileSight sight;
    unsigned mods = 0;
    int button = TORIRSM_LEFT;
    int force = 0;
    int x = 0;
    int z = 0;
    int level = 0;
    int px;
    int py;

    if( !eui || !agent_arg_tile(ctx, eui, &x, &z, &level) )
        return AGENT_VERB_DONE;
    if( Agent_ArgButton(ctx, &button) < 0 || Agent_ArgMods(ctx, &mods) < 0 || Agent_ArgBool(ctx, "force", &force) < 0 )
        return AGENT_VERB_DONE;
    sight = agent_tile_project(ctx, eui, x, z, level, &px, &py);
    if( sight == AGENT_TILE_NOT_LOADED )
    {
        agent_reply_not_loaded(ctx, x, z);
        return AGENT_VERB_DONE;
    }
    if( sight != AGENT_TILE_VISIBLE )
    {
        Agent_ReplyError(ctx, "tile %d,%d level %d cannot be clicked: it is %s; move the camera to it first (camera "
                              "x z)",
            x, z, level, agent_tile_sight_words(sight));
        return AGENT_VERB_DONE;
    }

    op = Agent_ScriptArm(ctx, world_click_check, world_click_finish);
    op->ints[WC_X] = x;
    op->ints[WC_Z] = z;
    op->ints[WC_LEVEL] = level;
    op->ints[WC_PX] = px;
    op->ints[WC_PY] = py;
    op->ints[WC_FORCE] = force;
    op->ints[WC_SERIAL] = eui->console_serial;
    Agent_ScriptModsDown(ctx, op, mods);
    Agent_ScriptMove(op, px, py);
    Agent_ScriptFrames(op, AGENT_HOVER_FRAMES);
    /* The press goes down only on the tile that was asked for. */
    Agent_ScriptCall(op, 0);
    Agent_ScriptDown(op, button, px, py);
    Agent_ScriptFrames(op, 1);
    Agent_ScriptUp(op, button, px, py);
    Agent_ScriptModsUp(ctx, op, mods);
    return AGENT_VERB_PENDING;
}

/* ---- camera ------------------------------------------------------------------------------ */

static enum AgentVerbResult
verb_camera(struct AgentCtx* ctx)
{
    struct EditorUi* eui = Agent_NeedEditor(ctx);
    struct AgentJsonWriter* out;
    struct EditorViewCamera camera;
    int x = 0;
    int z = 0;
    int level;
    int yaw = 0;
    int pitch = 0;
    int zoom = 0;
    int has_x;
    int has_z;
    int has_level;
    int has_yaw;
    int has_pitch;
    int has_zoom;
    int have_pose;

    if( !eui )
        return AGENT_VERB_DONE;
    level = agent_edit_level(eui);
    has_x = Agent_ArgInt(ctx, "x", &x);
    if( has_x < 0 )
        return AGENT_VERB_DONE;
    has_z = Agent_ArgInt(ctx, "z", &z);
    if( has_z < 0 )
        return AGENT_VERB_DONE;
    has_level = Agent_ArgInt(ctx, "level", &level);
    if( has_level < 0 )
        return AGENT_VERB_DONE;
    has_yaw = Agent_ArgInt(ctx, "yaw", &yaw);
    if( has_yaw < 0 )
        return AGENT_VERB_DONE;
    has_pitch = Agent_ArgInt(ctx, "pitch", &pitch);
    if( has_pitch < 0 )
        return AGENT_VERB_DONE;
    has_zoom = Agent_ArgInt(ctx, "zoom", &zoom);
    if( has_zoom < 0 )
        return AGENT_VERB_DONE;
    if( level < 0 || level > 3 )
    {
        Agent_ReplyError(ctx, "\"level\" is 0..3, not %d", level);
        return AGENT_VERB_DONE;
    }
    if( has_x != has_z )
    {
        Agent_ReplyError(ctx, "the tile to look at is \"x\" and \"z\" together");
        return AGENT_VERB_DONE;
    }
    if( has_zoom && (zoom < 64 || zoom > 100000) )
    {
        Agent_ReplyError(ctx, "\"zoom\" is the eye's distance in fine units (128 a tile), 64..100000, not %d", zoom);
        return AGENT_VERB_DONE;
    }
    if( !eui->app_services.world_camera || !eui->app_services.world_camera_set )
    {
        Agent_ReplyError(ctx, "this window has no world camera");
        return AGENT_VERB_DONE;
    }

    have_pose = agent_camera_read(eui, level, &camera);
    if( has_x || has_level || has_yaw || has_pitch || has_zoom )
    {
        /* A camera that cannot be read (no eye in the scene yet) still has
         * somewhere to go when the tile is given: the level photograph's
         * defaults (TORIRS_EDITOR_LEVEL, app_map_editor.c). */
        if( !have_pose )
        {
            if( !has_x )
            {
                Agent_ReplyError(ctx, "the camera is not over the loaded scene, so there is no pose to change: give "
                                      "the tile to look at (\"x\" and \"z\")");
                return AGENT_VERB_DONE;
            }
            memset(&camera, 0, sizeof(camera));
            camera.pitch = 400;
            camera.zoom = 1800;
        }
        if( has_x )
        {
            camera.x = x * 128 + 64;
            camera.z = z * 128 + 64;
        }
        camera.level = level;
        if( has_yaw )
            camera.yaw = yaw & 0x7ff;
        if( has_pitch )
            camera.pitch = pitch;
        if( has_zoom )
            camera.zoom = zoom;
        if( !eui->app_services.world_camera_set(eui->app_services.user_data, &camera) )
        {
            agent_reply_not_loaded(ctx, camera.x / 128, camera.z / 128);
            return AGENT_VERB_DONE;
        }
        ctx->app->need_redraw = 1;
        /* As it landed: the pitch is clamped to what the view allows. */
        have_pose = agent_camera_read(eui, level, &camera);
    }
    if( !have_pose )
    {
        Agent_ReplyError(ctx, "the camera is not over the loaded scene (no square is open, or the eye is outside "
                              "it): open a square, or set \"x\" and \"z\"");
        return AGENT_VERB_DONE;
    }
    out = Agent_ReplyBegin(ctx);
    AgentJsonW_Key(out, "camera");
    agent_write_camera(out, &camera);
    Agent_ReplyEnd(ctx);
    return AGENT_VERB_DONE;
}

/* ---- the table ---------------------------------------------------------------------------- */

const struct AgentVerb g_agent_verbs_editor[] = {
    { "editor", "", AGENT_VERB_EDITOR, verb_editor,
        "The editor's state: tool, level, lane, unsaved, open squares, selection, hover tile, World rect, camera, "
        "status sentence, RUN and live state." },
    { "actions", "text", AGENT_VERB_EDITOR, verb_actions,
        "Every hotkey action: id, group, name, chords, whether its chord would fire now." },
    { "action", "action", AGENT_VERB_EDITOR, verb_action,
        "Run a hotkey action by its id (action: \"file.save_all\") as the command palette runs it; replies with "
        "what the editor said." },
    { "tile_to_screen", "x z level", AGENT_VERB_EDITOR, verb_tile_to_screen,
        "Where a world tile's centre is on the surface, and whether it can be seen." },
    { "screen_to_tile", "x y", AGENT_VERB_EDITOR | AGENT_VERB_INPUT, verb_screen_to_tile,
        "The tile the editor hovers at a point of the World view (moves the pointer there)." },
    { "world_click", "x z level button mods force", AGENT_VERB_EDITOR | AGENT_VERB_INPUT, verb_world_click,
        "Click a world tile with the armed tool, after checking the editor hovers that tile under the pointer." },
    { "camera", "x z level yaw pitch zoom", AGENT_VERB_EDITOR, verb_camera,
        "The World camera's pose; with any argument, put it there (x, z: the tile to look at)." },
    { NULL, NULL, 0, NULL, NULL },
};

#else /* __EMSCRIPTEN__ */

struct EditorUi*
Agent_EditorUi(const struct AgentCtx* ctx)
{
    (void)ctx;
    return NULL;
}

struct EditorUi*
Agent_NeedEditor(struct AgentCtx* ctx)
{
    (void)ctx;
    return NULL;
}

const struct AgentVerb g_agent_verbs_editor[] = {
    { NULL, NULL, 0, NULL, NULL },
};

#endif
