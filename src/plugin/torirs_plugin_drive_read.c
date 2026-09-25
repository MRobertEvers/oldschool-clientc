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
#include "plugin/torirs_plugin_lua.h"

#include "lauxlib.h"
#include "lua.h"

#include <assert.h>

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
DriveRead_WidgetText(struct App* app, int component_id, char const** out_text)
{
    struct UITree* tree;
    int32_t idx;
    struct UITreeComponent const* c;

    assert(app);
    assert(out_text);

    *out_text = "";
    tree = app->tree;
    assert(tree);
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return DRIVE_NOT_FOUND;

    c = &tree->components[idx];
    if( c->type == UIELEM_RS_TEXT && c->u.rs_text.text )
        *out_text = c->u.rs_text.text;
    return DRIVE_OK;
}

enum DriveResult
DriveRead_WidgetPresented(struct App* app, int component_id, int* out_presented)
{
    struct UITree* tree;
    int32_t idx;

    assert(app);
    assert(out_presented);

    *out_presented = 0;
    tree = app->tree;
    assert(tree);
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return DRIVE_NOT_FOUND;

    *out_presented = !UITree_NodeOrAncestorDisplayHidden(tree, idx) &&
        UITree_NodeNativeVisible(tree, &app->ui_host, idx, app->hover_com_id);
    return DRIVE_OK;
}

enum DriveResult
DriveRead_WidgetOwnHidden(struct App* app, int component_id, int* out_own_hidden)
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
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return DRIVE_NOT_FOUND;

    *out_own_hidden = tree->components[idx].behavior.hide != 0;
    return DRIVE_OK;
}

static int
lua_drive_widget_text(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int component_id = PluginDrive_ArgInt(L, 1);
    char const* text = "";
    enum DriveResult result;

    assert(app);
    result = DriveRead_WidgetText(app, component_id, &text);
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
    int component_id = PluginDrive_ArgInt(L, 1);
    int presented = 0;
    enum DriveResult result;

    assert(app);
    result = DriveRead_WidgetPresented(app, component_id, &presented);
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
    int component_id = PluginDrive_ArgInt(L, 1);
    int own_hidden = 1;
    enum DriveResult result;

    assert(app);
    result = DriveRead_WidgetOwnHidden(app, component_id, &own_hidden);
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

static struct LuaFn const LUA_DRIVE_READ_FNS[] = {
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
