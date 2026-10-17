/*
 * quest-driver: reading a component's bound identity, text and presented
 * state.
 *
 * Owner: verbs-read (docs/ARCHITECT.md). Nobody else defines a function in this
 * file, and this file defines no function declared under another owner's
 * block in src/plugin/torirs_plugin_drive.h.
 *
 * Plan 5.6 says the text/presented reads this group's Lua half needs are
 * already in api.widgets ("No engine change"): api.widgets.get(component_id)
 * hands back a torirs.Widget, and :text()/:state().presented answer
 * chat.text/name and every presented-gate. In practice no part file but
 * core.lua can reach api.widgets: quest_driver/core.lua's core_bind captures
 * only api.drive as the chunk-local `api_drive` upvalue, and extending that
 * is core-scheduler's file, not this one. read.lua tried routing through a
 * bare `api_widgets` global that nothing ever assigned -- every read verb
 * indexed nil and raised, which is a Lua error the sandbox has no pcall to
 * catch, ending the whole conformance run (phase B finding 1).
 *
 * Fixed the way verbs-chat solved the identical problem for chatmenu's rows
 * (DriveChat_Options / DriveChat_OptionRow, which read through a revconfig
 * role rather than api.widgets -- "kept in verbs-chat rather than routed
 * through api.widgets: nothing else in this chunk exposes api.widgets to a
 * part file other than core.lua"): DriveRead_WidgetText/Presented/OwnHidden
 * below read struct App::tree directly -- a node-index lookup plus the same
 * component fields torirs_plugin_bridge.u.c's
 * PLUGIN_WIDGET_TEXT/PLUGIN_WIDGET_STATE cases read for the general plugin
 * API. Same data, a second driver-local door, in-ownership and with no
 * core.lua edit.
 *
 * The one thing neither api.widgets nor this file's text/presented reads can
 * answer is WHICH npc, obj or raw model a component was told to show --
 * c->u.rs_model.gamecache_model_id is the COMPOSITE scene model and cannot
 * tell "which npc is this" apart -- and that is what DriveRead_WidgetModel is
 * for, plus its PLUGIN_WIDGET_MODEL plumbing in torirs_plugin_host.h,
 * torirs_plugin_bridge.u.c and torirs_plugin_contract.h, which this group
 * also owns.
 */

#include "plugin/torirs_plugin_drive.h"

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER

#include "app.h"
#include "game/rs_title.h"
#include "plugin/torirs_plugin_lua.h"
#include "ui/uitree.h"
#include "ui/uitree_component_options.h"
#include "ui/uitree_input.h"
#include "ui/uitree_layout.h"

#include "lauxlib.h"
#include "lua.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/*
 * struct App::if_heads[].kind stores an `enum AppIfHeadKind`
 * (src/app/app_if_models.c) -- and that enum is declared INSIDE that .c
 * file, not in a header, so no other translation unit may spell its members
 * by name. The four raw values below are that enum's fixed, ordinary
 * (unnumbered, sequential) C layout as of app_if_models.c:16-25:
 *   0 = APP_IFHEAD_NPC, 1 = APP_IFHEAD_PLAYER, 2 = APP_IFHEAD_OBJ,
 *   3 = APP_IFHEAD_MODEL.
 * If that file ever assigns explicit values, or app_if_models.c's owner
 * exports the enum to app.h, this mapping (and its bridge.u.c twin under the
 * PLUGIN_WIDGET_MODEL case) must move onto the named constants instead.
 */
#define APP_IFHEAD_RAW_NPC 0
#define APP_IFHEAD_RAW_PLAYER 1
#define APP_IFHEAD_RAW_OBJ 2
#define APP_IFHEAD_RAW_MODEL 3

enum DriveResult
DriveRead_WidgetModel(struct App* app, int component_id, int* out_kind, int* out_id)
{
    int i;

    assert(app);
    assert(out_kind);
    assert(out_id);

    *out_kind = DRIVE_WIDGET_MODEL_NONE;
    *out_id = -1;

    for( i = 0; i < app->if_head_count; i++ )
    {
        if( app->if_heads[i].com_id != component_id )
            continue;

        switch( app->if_heads[i].kind )
        {
        case APP_IFHEAD_RAW_NPC:
            *out_kind = DRIVE_WIDGET_MODEL_NPC;
            *out_id = app->if_heads[i].npc_id;
            break;
        case APP_IFHEAD_RAW_OBJ:
            *out_kind = DRIVE_WIDGET_MODEL_OBJ;
            *out_id = app->if_heads[i].npc_id;
            break;
        case APP_IFHEAD_RAW_MODEL:
            *out_kind = DRIVE_WIDGET_MODEL_MODEL;
            *out_id = app->if_heads[i].npc_id;
            break;
        case APP_IFHEAD_RAW_PLAYER:
        default:
            /* A player portrait carries no raw identity to check against --
             * it is always the local player -- so this stays NONE/-1. */
            break;
        }
        break;
    }
    return DRIVE_OK;
}

enum DriveResult
DriveRead_WidgetText(struct App* app, int64_t widget, char const** out_text)
{
    struct UITree* tree;
    int32_t idx;
    struct UITreeComponent const* c;

    assert(app);
    assert(out_text);

    *out_text = "";
    tree = app->tree;
    assert(tree);
    idx = PluginDrive_HandleNode(tree, widget);
    if( idx < 0 )
        return DRIVE_NOT_FOUND;

    c = &tree->components[idx];
    if( c->type == UIELEM_RS_TEXT && c->u.rs_text.text )
        *out_text = c->u.rs_text.text;
    return DRIVE_OK;
}

enum DriveResult
DriveRead_WidgetPresented(struct App* app, int64_t widget, int* out_presented)
{
    struct UITree* tree;
    int32_t idx;

    assert(app);
    assert(out_presented);

    *out_presented = 0;
    tree = app->tree;
    assert(tree);
    idx = PluginDrive_HandleNode(tree, widget);
    if( idx < 0 )
        return DRIVE_NOT_FOUND;

    *out_presented = !UITree_NodeOrAncestorDisplayHidden(tree, idx) &&
        UITree_NodeNativeVisible(tree, &app->ui_host, idx, app->hover_com_id);
    return DRIVE_OK;
}

enum DriveResult
DriveRead_WidgetOwnHidden(struct App* app, int64_t widget, int* out_own_hidden)
{
    struct UITree* tree;
    int32_t idx;

    assert(app);
    assert(out_own_hidden);

    /* Not-found reads as hidden -- the same default read.lua's own
     * (now-removed) api_widgets-backed helper used: "if not widget then
     * return true end". */
    *out_own_hidden = 1;
    tree = app->tree;
    assert(tree);
    idx = PluginDrive_HandleNode(tree, widget);
    if( idx < 0 )
        return DRIVE_NOT_FOUND;

    *out_own_hidden = tree->components[idx].behavior.hide != 0;
    return DRIVE_OK;
}

enum DriveResult
DriveRead_WidgetBounds(
    struct App* app,
    int64_t widget,
    int* out_x,
    int* out_y,
    int* out_width,
    int* out_height)
{
    struct UITree* tree;
    int32_t idx;

    assert(app);
    assert(out_x);
    assert(out_y);
    assert(out_width);
    assert(out_height);

    *out_x = 0;
    *out_y = 0;
    *out_width = 0;
    *out_height = 0;
    tree = app->tree;
    assert(tree);
    idx = PluginDrive_HandleNode(tree, widget);
    if( idx < 0 )
        return DRIVE_NOT_FOUND;
    UITree_EnsureLayoutFor(tree, idx);
    if( !UITree_NodeDrawnBounds(tree, idx, out_x, out_y, out_width, out_height) )
        return DRIVE_NOT_FOUND;
    return DRIVE_OK;
}

enum DriveResult
DriveRead_WidgetAt(struct App* app, int x, int y, int* out_component_id)
{
    int32_t hits[UITREE_INPUT_HIT_STACK_MAX];
    int count;

    assert(app);
    assert(out_component_id);
    assert(app->tree);

    *out_component_id = -1;
    count = UITree_CollectNodesAt(app->tree, &app->ui_host, x, y, hits, UITREE_INPUT_HIT_STACK_MAX);
    if( count > 0 )
        *out_component_id = app->tree->components[hits[0]].component_id;
    return DRIVE_OK;
}

enum DriveResult
DriveRead_TitleButton(
    struct App* app,
    char const* action,
    int* out_x,
    int* out_y,
    int* out_width,
    int* out_height)
{
    struct UITree* tree;
    int wanted;

    assert(app);
    assert(action);
    assert(out_x);
    assert(out_y);
    assert(out_width);
    assert(out_height);

    wanted = RS_Title_ActionFromName(action);
    assert(wanted != RS_TITLE_ACTION_NONE && "title_button: unknown action name");
    tree = app->tree;
    assert(tree);
    /* Called once per log-in press, on the title screen only. */
    UITREE_SCAN_METER(tree);
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* comp = &tree->components[i];
        if( comp->freed || comp->type != UIELEM_BUILTIN_LOGIN_BUTTON ||
            comp->u.login_button.action != wanted )
            continue;
        UITree_EnsureLayoutFor(tree, (int32_t)i);
        if( !UITree_NodeDrawnBounds(tree, (int32_t)i, out_x, out_y, out_width, out_height) )
            return DRIVE_NOT_FOUND;
        return DRIVE_OK;
    }
    return DRIVE_NOT_FOUND;
}

/* api_drive.title_button(action) -> ("ok", {x, y, width, height}) |
 * "not_found" */
static int
lua_drive_title_button(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    char const* action = PluginDrive_ArgString(L, 1);
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    enum DriveResult result;

    assert(app);
    result = DriveRead_TitleButton(app, action, &x, &y, &width, &height);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, width);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, height);
    lua_setfield(L, -2, "height");
    return 2;
}

static int
lua_drive_widget_text(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int64_t widget = PluginDrive_ArgHandle(L, 1);
    char const* text = "";
    enum DriveResult result;

    assert(app);
    result = DriveRead_WidgetText(app, widget, &text);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushstring(L, text);
    return 2;
}

static int
lua_drive_widget_presented(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int64_t widget = PluginDrive_ArgHandle(L, 1);
    int presented = 0;
    enum DriveResult result;

    assert(app);
    result = DriveRead_WidgetPresented(app, widget, &presented);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushboolean(L, presented);
    return 2;
}

static int
lua_drive_widget_own_hidden(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int64_t widget = PluginDrive_ArgHandle(L, 1);
    int own_hidden = 1;
    enum DriveResult result;

    assert(app);
    result = DriveRead_WidgetOwnHidden(app, widget, &own_hidden);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushboolean(L, own_hidden);
    return 2;
}

static int
lua_drive_widget_model(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int component_id = PluginDrive_ArgInt(L, 1);
    int kind = DRIVE_WIDGET_MODEL_NONE;
    int id = -1;
    enum DriveResult result;
    static char const* const KIND_NAMES[] = { "none", "npc", "obj", "model" };

    assert(app);
    result = DriveRead_WidgetModel(app, component_id, &kind, &id);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);

    lua_pushstring(L, DriveResultName(result));
    lua_createtable(L, 0, 2);
    assert(kind >= 0);
    assert(kind < (int)(sizeof(KIND_NAMES) / sizeof(KIND_NAMES[0])));
    lua_pushstring(L, KIND_NAMES[kind]);
    lua_setfield(L, -2, "kind");
    lua_pushinteger(L, id);
    lua_setfield(L, -2, "id");
    return 2;
}

/* api_drive.widget_bounds(component_id) -> ("ok", {x, y, width, height}) |
 * "not_found" */
static int
lua_drive_widget_bounds(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int64_t widget = PluginDrive_ArgHandle(L, 1);
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    enum DriveResult result;

    assert(app);
    result = DriveRead_WidgetBounds(app, widget, &x, &y, &width, &height);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, width);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, height);
    lua_setfield(L, -2, "height");
    return 2;
}

/* api_drive.widget_at(x, y) -> ("ok", component_id | -1) */
static int
lua_drive_widget_at(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int x = PluginDrive_ArgInt(L, 1);
    int y = PluginDrive_ArgInt(L, 2);
    int component_id = -1;
    enum DriveResult result;

    assert(app);
    result = DriveRead_WidgetAt(app, x, y, &component_id);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, component_id);
    return 2;
}

/* api_drive.widget_node(handle) -> ("ok", {node, component_id, parent,
 * parent_component_id, dynamic, sub, freed}) | "not_found"
 *
 * Which tree node a widget handle (PluginDrive_Handle) resolves to: the
 * diagnostic behind a press that names the wrong node -- a clientscript child
 * is told from its parent by its sub alone. */
static int
lua_drive_widget_node(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int64_t widget = PluginDrive_ArgHandle(L, 1);
    struct UITreeComponent const* c;
    int32_t node;

    assert(app);
    if( !app->tree )
        return PluginDrive_PushResult(L, DRIVE_NOT_VISIBLE, NULL);
    node = PluginDrive_HandleNode(app->tree, widget);
    if( node < 0 )
        return PluginDrive_PushResult(L, DRIVE_NOT_FOUND, NULL);
    c = &app->tree->components[node];
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, node);
    lua_setfield(L, -2, "node");
    lua_pushinteger(L, c->component_id);
    lua_setfield(L, -2, "component_id");
    lua_pushinteger(L, c->parent);
    lua_setfield(L, -2, "parent");
    lua_pushinteger(L, c->parent >= 0 ? app->tree->components[c->parent].component_id : -1);
    lua_setfield(L, -2, "parent_component_id");
    lua_pushboolean(L, c->dynamic != 0);
    lua_setfield(L, -2, "dynamic");
    lua_pushinteger(L, c->dynamic ? c->dynamic_child_index : -1);
    lua_setfield(L, -2, "sub");
    lua_pushboolean(L, c->freed != 0);
    lua_setfield(L, -2, "freed");
    return 2;
}

/* `src` with its <tag> markup dropped, into `out` (cap bytes, NUL-ended). */
static void
drive_read_plain(char const* src, char* out, size_t cap)
{
    size_t n = 0;
    int in_tag = 0;

    assert(src);
    assert(out);
    assert(cap > 0);
    for( ; *src && n + 1 < cap; src++ )
    {
        if( *src == '<' )
            in_tag = 1;
        else if( *src == '>' && in_tag )
            in_tag = 0;
        else if( !in_tag )
            out[n++] = *src;
    }
    out[n] = '\0';
}

/*
 * api_drive.widget_find_op(layer, op, name) -> result, {handle, node, op,
 * label, x, y, width, height, presented}: the component under `layer` (its
 * whole subtree, clientscript children included) whose right-click op `op`
 * reads exactly `op` and whose option name contains `name` (nil: any),
 * markup dropped -- the row a player would pick, read from the tree instead
 * of hunted for with the pointer. `handle` is the drive handle of that exact
 * node (a dynamic child's is its parent's component id plus its sub), `op`
 * the 1-based op slot. not_found when no component under the layer offers it.
 */
static int
lua_drive_widget_find_op(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int64_t layer = PluginDrive_ArgHandle(L, 1);
    char const* op = PluginDrive_ArgString(L, 2);
    char const* name = lua_isnoneornil(L, 3) ? NULL : PluginDrive_ArgString(L, 3);
    struct UITree* tree;
    int32_t root;
    int32_t n;
    char seen[512];
    size_t seen_len = 0;
    int nodes = 0;

    assert(app);
    assert(op);
    seen[0] = '\0';
    tree = app->tree;
    if( !tree )
        return PluginDrive_PushResult(L, DRIVE_NOT_VISIBLE, NULL);
    root = PluginDrive_HandleNode(tree, layer);
    if( root < 0 )
        return PluginDrive_PushResult(L, DRIVE_NOT_FOUND, NULL);
    /* tree-walk-exempt: one layer's subtree, asked on demand by a test
     * driver, never by the frame loop. */
    n = tree->components[root].first_child;
    while( n >= 0 && n != root )
    {
        struct UITreeComponent const* c = &tree->components[n];
        nodes++;
        if( !c->freed && UITree_HasMenuOptions(c) )
        {
            struct UITreeMenuOptions const* opts = UITree_MenuOptions(c);
            char option[UITREE_MENU_OPTION_LEN];
            drive_read_plain(opts->option, option, sizeof(option));
            /* What a miss reports: each op on offer, "op:option". */
            for( int i = 0; i < UITREE_MENU_OPTION_SLOTS && seen_len + 1 < sizeof(seen); i++ )
            {
                char label[UITREE_MENU_OPTION_LEN];
                int wrote;
                drive_read_plain(opts->ops[i], label, sizeof(label));
                if( label[0] == '\0' )
                    continue;
                wrote = snprintf(seen + seen_len, sizeof(seen) - seen_len, "%s%s:%s", seen_len ? " | " : "",
                    label, option);
                if( wrote < 0 || (size_t)wrote >= sizeof(seen) - seen_len )
                    break;
                seen_len += (size_t)wrote;
            }
            for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
            {
                char label[UITREE_MENU_OPTION_LEN];
                int x = 0, y = 0, w = 0, h = 0;
                int64_t handle;
                drive_read_plain(opts->ops[i], label, sizeof(label));
                if( label[0] == '\0' || strcmp(label, op) != 0 )
                    continue;
                if( name && !strstr(option, name) )
                    continue;
                if( c->dynamic && c->parent >= 0 )
                    handle = PluginDrive_Handle(tree->components[c->parent].component_id, c->dynamic_child_index);
                else
                    handle = c->component_id;
                UITree_EnsureLayoutFor(tree, n);
                UITree_NodeDrawnBounds(tree, n, &x, &y, &w, &h);
                lua_pushstring(L, DriveResultName(DRIVE_OK));
                lua_createtable(L, 0, 10);
                lua_pushinteger(L, handle);
                lua_setfield(L, -2, "handle");
                lua_pushinteger(L, n);
                lua_setfield(L, -2, "node");
                lua_pushinteger(L, i + 1);
                lua_setfield(L, -2, "op");
                lua_pushstring(L, option);
                lua_setfield(L, -2, "option");
                lua_pushinteger(L, x);
                lua_setfield(L, -2, "x");
                lua_pushinteger(L, y);
                lua_setfield(L, -2, "y");
                lua_pushinteger(L, w);
                lua_setfield(L, -2, "width");
                lua_pushinteger(L, h);
                lua_setfield(L, -2, "height");
                lua_pushboolean(L, !UITree_NodeOrAncestorDisplayHidden(tree, n) &&
                    UITree_NodeNativeVisible(tree, &app->ui_host, n, app->hover_com_id));
                lua_setfield(L, -2, "presented");
                return 2;
            }
        }
        if( c->first_child >= 0 )
        {
            n = c->first_child;
            continue;
        }
        while( n != root && tree->components[n].next_sibling < 0 )
            n = tree->components[n].parent;
        if( n == root )
            break;
        n = tree->components[n].next_sibling;
    }
    {
        char detail[600];
        snprintf(detail, sizeof(detail), "%d node(s); ops: %s", nodes, seen_len ? seen : "none");
        return PluginDrive_PushResult(L, DRIVE_NOT_FOUND, detail);
    }
}

/*
 * api_drive.widget_ops_at(x, y) -> result, {{node, component_id, option,
 * ops = {label, ...}, x, y, width, height}, ...}: the components the UI hit
 * test stacks at canvas (x, y), topmost first, each with the right-click ops
 * it offers right now (markup dropped; a slot with no op is ""). What a
 * press at that pixel would be offered, read from the tree, no click.
 */
static int
lua_drive_widget_ops_at(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int x = PluginDrive_ArgInt(L, 1);
    int y = PluginDrive_ArgInt(L, 2);
    int32_t hits[UITREE_INPUT_HIT_STACK_MAX];
    int count;

    assert(app);
    if( !app->tree )
        return PluginDrive_PushResult(L, DRIVE_NOT_VISIBLE, NULL);
    count = UITree_CollectNodesAt(app->tree, &app->ui_host, x, y, hits, UITREE_INPUT_HIT_STACK_MAX);
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_createtable(L, count, 0);
    for( int h = 0; h < count; h++ )
    {
        struct UITreeComponent const* c = &app->tree->components[hits[h]];
        struct UITreeMenuOptions const* opts = UITree_MenuOptions(c);
        char text[UITREE_MENU_OPTION_LEN];
        int bx = 0, by = 0, bw = 0, bh = 0;

        UITree_NodeDrawnBounds(app->tree, hits[h], &bx, &by, &bw, &bh);
        lua_createtable(L, 0, 8);
        lua_pushinteger(L, hits[h]);
        lua_setfield(L, -2, "node");
        lua_pushinteger(L, c->component_id);
        lua_setfield(L, -2, "component_id");
        drive_read_plain(opts->option, text, sizeof(text));
        lua_pushstring(L, text);
        lua_setfield(L, -2, "option");
        lua_createtable(L, UITREE_MENU_OPTION_SLOTS, 0);
        for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
        {
            drive_read_plain(opts->ops[i], text, sizeof(text));
            lua_pushstring(L, text);
            lua_rawseti(L, -2, i + 1);
        }
        lua_setfield(L, -2, "ops");
        lua_pushinteger(L, bx);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, by);
        lua_setfield(L, -2, "y");
        lua_pushinteger(L, bw);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, bh);
        lua_setfield(L, -2, "height");
        lua_rawseti(L, -2, h + 1);
    }
    return 2;
}

static struct LuaFn const LUA_DRIVE_READ_FNS[] = {
    {"widget_ops_at", lua_drive_widget_ops_at},
    {"widget_find_op", lua_drive_widget_find_op},
    {"widget_node", lua_drive_widget_node},
    {"widget_bounds", lua_drive_widget_bounds},
    {"widget_at", lua_drive_widget_at},
    {"title_button", lua_drive_title_button},
    {"widget_model", lua_drive_widget_model},
    {"widget_text", lua_drive_widget_text},
    {"widget_presented", lua_drive_widget_presented},
    {"widget_own_hidden", lua_drive_widget_own_hidden},
    {NULL, NULL},
};

void
PluginDriveRead_RegisterLua(struct lua_State* L, void* script)
{
    assert(L);
    assert(script);
    PluginLua_AppendModule(L, script, LUA_DRIVE_READ_FNS);
}

#else /* !TORIRS_EMBED_SERVER */

/* No embedded server, no driver. The registrar stays so the module assembly
 * in torirs_plugin_drive.c needs no second spelling. */
void
PluginDriveRead_RegisterLua(struct lua_State* L, void* script)
{
    (void)L;
    (void)script;
}

#endif /* TORIRS_EMBED_SERVER */
