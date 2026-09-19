/*
 * quest-driver: the dialogue seam: resume arming, pause-pending, modal close, meslayer mode.
 *
 * Owner: verbs-chat (docs/ARCHITECT.md). Nobody else defines a function in this
 * file, and this file defines no function declared under another owner's
 * block in src/plugin/torirs_plugin_drive.h.
 *
 * D1: a dialogue row's real action is RESUME_PAUSEBUTTON with action_index
 * -1, never a numbered IF_BUTTON, so every continue and every option choice
 * drives app->host.resume_pausebutton_component_id.
 * D6: pause_pending is compared BY COMPONENT ID -- every sub-interface mount
 * calls UITree_SetPausePending(tree, -1) unconditionally, so presence alone
 * says nothing.
 */

#include "plugin/torirs_plugin_drive.h"

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER

#include "app.h"
#include "plugin/torirs_plugin_lua.h"

#include "game/rs_minimenu_build.h" /* RS_MINIMENU_EVENT_CLICK */
#include "game/varc_ids.h"
#include "net/net.h" /* struct ToriRS_Network, for app->net->rev->revision */

#include "lauxlib.h"
#include "lua.h"

#include <assert.h>
#include <string.h>

/*
 * The interface mounted under chat_modal_host, or DRIVE_NOT_VISIBLE.
 *
 * D3: liveness is UITree_GroupPresent(tree, group_id), never
 * interface_parents alone (a record can outlive its container across a
 * reclaim -- see UITree_ReclaimInterfaceGroup's own comment) and never
 * modal_host_uid (D4: set on open, never cleared on close). chat_modal_host
 * itself (chatbox:chatmodal) is a STATIC role that resolves whenever the
 * chatbox interface is open at all; what varies is which GROUP is mounted
 * under it, which is what interface_parents answers.
 */
enum DriveResult
DriveChat_ModalGroup(struct App* app, int* out_interface_id)
{
    int32_t host_idx;
    int host_component_id;
    int record;

    assert(app);
    assert(out_interface_id);

    *out_interface_id = -1;

    if( !app->tree )
        return DRIVE_NOT_VISIBLE;

    host_idx = UITree_RoleNodeByName(app->tree, &app->ui_roles, "chat_modal_host");
    if( host_idx < 0 )
        return DRIVE_NOT_VISIBLE;

    host_component_id = app->tree->components[host_idx].component_id;
    record = UITree_InterfaceParentFind(app->tree, host_component_id);
    if( record < 0 )
        return DRIVE_NOT_VISIBLE;

    if( !UITree_GroupPresent(app->tree, app->tree->interface_parents[record].group_id) )
        return DRIVE_NOT_VISIBLE;

    *out_interface_id = app->tree->interface_parents[record].group_id;
    return DRIVE_OK;
}

enum DriveResult
DriveChat_Resume(struct App* app, int component_id)
{
    assert(app);

    /* D6: outstanding is UITree_PausePendingActive, not presence of a prior
     * arm -- every sub-interface mount clears the latch unconditionally, so
     * this is the one guard that actually distinguishes "a player cannot
     * double-submit" from "nothing has answered the last click yet". */
    if( app->tree && UITree_PausePendingActive(app->tree) )
        return DRIVE_REFUSED;

    app->host.resume_pausebutton_component_id = component_id;
    return DRIVE_OK;
}

enum DriveResult
DriveChat_PausePending(struct App* app, int* out_component_id)
{
    int32_t idx;

    assert(app);
    assert(out_component_id);

    idx = app->tree ? UITree_PausePendingIndex(app->tree) : -1;
    *out_component_id = idx >= 0 ? app->tree->components[idx].component_id : -1;
    return DRIVE_OK;
}

enum DriveResult
DriveChat_CloseModal(struct App* app)
{
    assert(app);
    /* Idempotent by construction: setting an already-set flag changes
     * nothing, and app_cs2_flush_notifications drains it the same tick
     * (src/app/app_cs2_flush.c:142-165), which is also what aborts any
     * outstanding COUNTDIALOG/NAMEDIALOG wait server-side
     * (ToriRSServer_WorldCloseModalEx). */
    app->host.close_modal_requested = true;
    return DRIVE_OK;
}

enum DriveResult
DriveChat_MeslayerMode(struct App* app, int* out_mode)
{
    struct VarCIds const* ids;

    assert(app);
    assert(out_mode);

    ids = varc_ids_for_revision(app->net && app->net->rev ? (int)app->net->rev->revision : 0);
    if( ids->meslayer_mode < 0 )
    {
        *out_mode = -1;
        return DRIVE_NOT_FOUND;
    }
    /* -1 unset, 0/1 ordinary chat, everything else a live prompt -- the
     * caller (chat.kind/chat.count/chat.name_entry) does the bucketing;
     * this seam only ever hands back the raw varc read. */
    *out_mode = VarCManager_GetInt(&app->varcs, ids->meslayer_mode);
    return DRIVE_OK;
}

enum DriveResult
DriveChat_ClickArmed(struct App* app, int component_id, int* out_armed)
{
    int32_t idx;
    unsigned effective;

    assert(app);
    assert(out_armed);

    *out_armed = 0;
    if( !app->tree )
        return DRIVE_NOT_VISIBLE;

    idx = UITree_FindByComponentId(app->tree, component_id);
    if( idx < 0 || app->tree->components[idx].freed )
        return DRIVE_NOT_VISIBLE;

    /* The same test add_component_rows makes per row (rs_minimenu_build.c:1073),
     * through the effective (IF_SETEVENTS-overridden) mask, not the
     * cache-authored one alone. */
    effective = App_IfEventsGetEffective(app, component_id);
    /* objectbox has no continue child (D1): if_addresumebutton arms
     * objectbox:universe itself, a PLAIN (non-dynamic) leaf with no
     * cache-baked click_mask of its own -- unlike chat_left:continue and
     * friends, which read armed off that baked mask regardless. The
     * server's SS_OP_IF_ADDRESUMEBUTTON (torirs_server_scripts.c) always
     * registers its range starting at sub 0 (never sub -1, "a plain
     * component has no sub-ids" per that handler's own banner), while
     * UIIfEventTable_Effective's own-identity probe for a non-dynamic node
     * is sub -1 (uitree_if_events.c: UIIfEventTable_Effective ->
     * UIIfEventTable_Lookup(table, com_id, -1, ...)) -- a range that starts
     * at 0 never covers -1, so the dynamic arm is invisible to Effective()
     * for exactly this shape of component. Fall back to the same sub=0 the
     * server actually wrote before concluding "not armed". */
    if( !(effective & RS_MINIMENU_EVENT_CLICK) )
        effective = (unsigned)App_IfEventsGetAt(app, component_id, 0);
    *out_armed = (effective & RS_MINIMENU_EVENT_CLICK) ? 1 : 0;
    return DRIVE_OK;
}

/* ---------------------------------------------------------- chatmenu rows */

static char const* const DRIVE_CHAT_OPTION_ROW_ROLE[DRIVE_CHAT_OPTIONS_ROW_MAX] = {
    "dialog_options_row_1",
    "dialog_options_row_2",
    "dialog_options_row_3",
    "dialog_options_row_4",
    "dialog_options_row_5",
};

/* The tree index a role names, or -1. Shared by DriveChat_Options (which
 * wants its text) and DriveChat_OptionRow (which wants its component id):
 * both are "resolve this cc_create'd role right now", and cc_create'd rows
 * carry no content-pack symbol for DriveUi_Component to resolve instead. */
static int32_t
drive_chat_role_index(struct App* app, char const* role)
{
    if( !app->tree )
        return -1;
    return UITree_RoleNodeByName(app->tree, &app->ui_roles, role);
}

/* NULL when the role is not live right now, or is live but is not a text
 * node -- both are "nothing to read", not a fault: a role can resolve to a
 * different node shape across a cache revision this driver has not seen. */
static char const*
drive_chat_role_text(struct App* app, char const* role)
{
    int32_t idx;
    struct UITreeComponent const* node;

    idx = drive_chat_role_index(app, role);
    if( idx < 0 )
        return NULL;
    node = &app->tree->components[idx];
    if( node->type != UIELEM_RS_TEXT )
        return NULL;
    return node->u.rs_text.text ? node->u.rs_text.text : "";
}

enum DriveResult
DriveChat_Options(struct App* app, struct DriveChatOptions* out)
{
    char const* text;
    int i;

    assert(app);
    assert(out);

    memset(out, 0, sizeof(*out));

    /* Readiness floor (plan 5.5): rows 1 AND 2 both resolving. Row 1 alone
     * can answer mid-rebuild -- RUNCLIENTSCRIPT is held to the tick fence
     * (app_cs2_flush.c:71), so the container can be mounted and still be
     * empty for a frame. */
    if( !drive_chat_role_text(app, DRIVE_CHAT_OPTION_ROW_ROLE[0]) ||
        !drive_chat_role_text(app, DRIVE_CHAT_OPTION_ROW_ROLE[1]) )
        return DRIVE_NOT_VISIBLE;

    text = drive_chat_role_text(app, "dialog_options_title");
    if( text )
        snprintf(out->title, sizeof(out->title), "%s", text);

    for( i = 0; i < DRIVE_CHAT_OPTIONS_ROW_MAX; i++ )
    {
        text = drive_chat_role_text(app, DRIVE_CHAT_OPTION_ROW_ROLE[i]);
        if( !text )
            break;
        snprintf(out->rows[i], sizeof(out->rows[i]), "%s", text);
        out->row_count = i + 1;
    }
    return DRIVE_OK;
}

enum DriveResult
DriveChat_OptionRow(struct App* app, int row, int* out_component_id)
{
    int32_t idx;

    assert(app);
    assert(out_component_id);
    assert(row >= 1);
    assert(row <= DRIVE_CHAT_OPTIONS_ROW_MAX);

    *out_component_id = -1;
    idx = drive_chat_role_index(app, DRIVE_CHAT_OPTION_ROW_ROLE[row - 1]);
    if( idx < 0 )
        return DRIVE_NOT_VISIBLE;

    *out_component_id = app->tree->components[idx].component_id;
    return DRIVE_OK;
}

static int
lua_drive_modal_group(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int interface_id = -1;
    enum DriveResult result;

    assert(app);
    result = DriveChat_ModalGroup(app, &interface_id);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, interface_id);
    return 2;
}

static int
lua_drive_resume(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int component_id = PluginDrive_ArgInt(L, 1);
    enum DriveResult result;

    assert(app);
    result = DriveChat_Resume(app, component_id);
    return PluginDrive_PushResult(L, result, NULL);
}

static int
lua_drive_pause_pending(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int component_id = -1;
    enum DriveResult result;

    assert(app);
    result = DriveChat_PausePending(app, &component_id);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, component_id);
    return 2;
}

static int
lua_drive_close_modal(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    enum DriveResult result;

    assert(app);
    result = DriveChat_CloseModal(app);
    return PluginDrive_PushResult(L, result, NULL);
}

static int
lua_drive_meslayer_mode(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int mode = -1;
    enum DriveResult result;

    assert(app);
    result = DriveChat_MeslayerMode(app, &mode);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, mode);
    return 2;
}

static int
lua_drive_click_armed(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int component_id = PluginDrive_ArgInt(L, 1);
    int armed = 0;
    enum DriveResult result;

    assert(app);
    result = DriveChat_ClickArmed(app, component_id, &armed);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushboolean(L, armed);
    return 2;
}


static int
lua_drive_options(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    struct DriveChatOptions options;
    enum DriveResult result;
    int i;

    assert(app);
    result = DriveChat_Options(app, &options);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);

    lua_pushstring(L, DriveResultName(result));
    lua_createtable(L, 0, 2);
    lua_pushstring(L, options.title);
    lua_setfield(L, -2, "title");
    lua_createtable(L, options.row_count, 0);
    for( i = 0; i < options.row_count; i++ )
    {
        lua_pushstring(L, options.rows[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "rows");
    return 2;
}

static int
lua_drive_option_row(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int row = PluginDrive_ArgInt(L, 1);
    int component_id = -1;
    enum DriveResult result;

    assert(app);
    if( row < 1 || row > DRIVE_CHAT_OPTIONS_ROW_MAX )
        return luaL_error(L, "drive.option_row: row must be 1..%d", DRIVE_CHAT_OPTIONS_ROW_MAX);
    result = DriveChat_OptionRow(app, row, &component_id);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, component_id);
    return 2;
}

static struct LuaFn const LUA_DRIVE_CHAT_FNS[] = {
    {"modal_group", lua_drive_modal_group},
    {"resume", lua_drive_resume},
    {"pause_pending", lua_drive_pause_pending},
    {"close_modal", lua_drive_close_modal},
    {"meslayer_mode", lua_drive_meslayer_mode},
    {"click_armed", lua_drive_click_armed},
    {"options", lua_drive_options},
    {"option_row", lua_drive_option_row},
    {NULL, NULL},
};

void
PluginDriveChat_RegisterLua(struct lua_State* L, void* script)
{
    assert(L);
    assert(script);
    PluginLua_AppendModule(L, script, LUA_DRIVE_CHAT_FNS);
}

#else /* !TORIRS_EMBED_SERVER */

/* No embedded server, no driver. The registrar stays so the module assembly
 * in torirs_plugin_drive.c needs no second spelling. */
void
PluginDriveChat_RegisterLua(struct lua_State* L, void* script)
{
    (void)L;
    (void)script;
}

#endif /* TORIRS_EMBED_SERVER */
