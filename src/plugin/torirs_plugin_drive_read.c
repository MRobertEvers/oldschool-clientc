/*
 * quest-driver: reading a component's bound identity (heads and item models).
 *
 * Owner: verbs-read (docs/ARCHITECT.md). Nobody else defines a function in this
 * file, and this file defines no function declared under another owner's
 * block in src/plugin/torirs_plugin_drive.h.
 *
 * The text/presented reads this group's Lua half needs are already in
 * api.widgets (plan 5.6: "No engine change"): api.widgets.get(component_id)
 * (the same reverse lookup verbs-chat's chat.options/options_title use for a
 * cc_create'd row with no content symbol) hands back a torirs.Widget, and
 * :text()/:state().presented answer chat.text/name and every presented-gate.
 * The one thing api.widgets cannot answer is WHICH npc, obj or raw model a
 * component was told to show -- c->u.rs_model.gamecache_model_id is the
 * COMPOSITE scene model and cannot tell "which npc is this" apart -- and
 * that is what DriveRead_WidgetModel is for, plus its PLUGIN_WIDGET_MODEL
 * plumbing in torirs_plugin_host.h, torirs_plugin_bridge.u.c and
 * torirs_plugin_contract.h, which this group also owns.
 *
 * Like chat.lua (see its file header), read.lua's roles/symbols need
 * api.widgets reachable from a part file other than core.lua, which today
 * captures only api.drive as the chunk-local `api_drive` upvalue
 * (quest_driver/core.lua's core_bind). That one-line addition
 * (`api_widgets = api.widgets`) is core-scheduler's file and is reported,
 * not made here -- see this pass's report.
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
