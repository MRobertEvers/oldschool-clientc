/*
 * quest-driver: var / inv / skill / message reads.
 *
 * Owner: core-state (docs/ARCHITECT.md). Nobody else defines a function in this
 * file, and this file defines no function declared under another owner's
 * block in src/plugin/torirs_plugin_drive.h.
 *
 * No revconfig role belongs here: every value is either a server-declared
 * content symbol resolved through DriveSymbol_Lookup or a raw engine struct.
 * var.expect exists to catch client != server, so the client value and
 * varps.var_serv[] are two different reads and must stay that way.
 *
 * There is a THIRD read, and it is not one of that pair: DriveState_VarpContent
 * reads the embedded server's own player varps directly.  It exists for the
 * vars the client can never hold at all -- a content-allocated varp above the
 * id the client's varp array can address is never transmitted, so both reads
 * of the pair answer not_found forever.  It cannot see a desync and is never
 * a substitute for the pair; see its banner in torirs_plugin_drive.h.
 */

#include "plugin/torirs_plugin_drive.h"

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER

#include "app.h"
#include "plugin/torirs_plugin_lua.h"

#include "game/rs_chat.h"
#include "game/rs_player_stats.h"
#include "inv/inv_manager.h"
#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_vessel.h"
#include "world/wev.h"
#include "varp/varp_manager.h"

#include "lauxlib.h"
#include "lua.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- var.* */

enum DriveResult
DriveState_Varp(struct App* app, int varp_id, int* out_value)
{
    assert(app);
    assert(out_value);
    assert(varp_id >= 0);
    if( varp_id >= app->varps.varp_count )
    {
        /* Content can allocate varps above the cache's highest declared id
         * (varp_manager.h TORIRSSERVER_VARP_SERVER_HEADROOM); an id this
         * client's table has not grown to cover yet is a legitimate "not
         * found", not a caller bug. */
        *out_value = 0;
        return DRIVE_NOT_FOUND;
    }
    *out_value = VarPManager_GetVarp(&app->varps, varp_id);
    return DRIVE_OK;
}

enum DriveResult
DriveState_Varbit(struct App* app, int varbit_id, int* out_value)
{
    assert(app);
    assert(out_value);
    assert(varbit_id >= 0);
    if( varbit_id >= app->varps.varbit_count )
    {
        *out_value = 0;
        return DRIVE_NOT_FOUND;
    }
    *out_value = VarPManager_GetVarbit(&app->varps, varbit_id);
    return DRIVE_OK;
}

enum DriveResult
DriveState_VarbitBaseVarp(struct App* app, int varbit_id, int* out_varp)
{
    int base;

    assert(app);
    assert(out_varp);
    assert(varbit_id >= 0);
    if( varbit_id >= app->varps.varbit_count )
    {
        *out_varp = -1;
        return DRIVE_NOT_FOUND;
    }
    base = VarPManager_VarbitBaseVar(&app->varps, varbit_id);
    if( base < 0 )
    {
        *out_varp = -1;
        return DRIVE_NOT_FOUND;
    }
    *out_varp = base;
    return DRIVE_OK;
}

enum DriveResult
DriveState_VarpServer(struct App* app, int varp_id, int* out_value)
{
    assert(app);
    assert(out_value);
    assert(varp_id >= 0);
    if( varp_id >= app->varps.varp_count )
    {
        *out_value = 0;
        return DRIVE_NOT_FOUND;
    }
    /* var_serv[] is the client's own record of the last server-confirmed
     * value (VarPManager_ApplySync/ApplySmall/ApplyLarge keep it), and
     * var.expect exists to catch a client that has drifted from it -- so this
     * must be a second, independent read, never the same array GetVarp reads. */
    *out_value = app->varps.var_serv[varp_id];
    return DRIVE_OK;
}

enum DriveResult
DriveState_VarbitServer(struct App* app, int varbit_id, int* out_value)
{
    struct VarPManager const* mgr;
    struct VarBitType const* type;
    int bit_count;
    int mask;

    assert(app);
    assert(out_value);
    assert(varbit_id >= 0);

    mgr = &app->varps;
    if( varbit_id >= mgr->varbit_count )
    {
        *out_value = 0;
        return DRIVE_NOT_FOUND;
    }
    type = &mgr->varbit_types[varbit_id];
    if( type->basevar < 0 || type->basevar >= mgr->varp_count )
    {
        *out_value = 0;
        return DRIVE_NOT_FOUND;
    }
    /* Mirrors VarPManager_GetVarbit's bit math exactly, sourced from
     * var_serv[] instead of var[] -- see the header comment on why this
     * duplication lives here instead of widening varp_manager.c. */
    bit_count = type->endbit - type->startbit + 1;
    if( bit_count <= 0 || bit_count >= VARP_MANAGER_READBIT_MAX )
    {
        *out_value = 0;
        return DRIVE_NOT_FOUND;
    }
    mask = mgr->readbit[bit_count];
    *out_value = (mgr->var_serv[type->basevar] >> type->startbit) & mask;
    return DRIVE_OK;
}

/* ---------------------------------------------------------------- inv.* */

enum DriveResult
DriveState_VarpContent(struct App* app, int varp_id, int* out_value)
{
    struct ToriRSServer* srv;
    struct ToriRSServerPlayer const* player;

    assert(app);
    assert(out_value);
    assert(varp_id >= 0);
    (void)app; /* the server's copy: this read never touches app->varps */

    *out_value = 0;
    srv = PluginDrive_EmbedWorld();
    if( !srv )
        /* Socket-server run -- the server is on the other end of a wire and
         * its varps are not ours to read.  Same answer DriveCore_Cheat gives
         * for the same reason. */
        return DRIVE_UNSUPPORTED;
    player = srv->active_player;
    if( !player )
        /* Nobody logged in yet.  drive_world_ready() gates the scheduler on
         * exactly this, so a quest never sees it -- but a verb must not
         * dereference its way through the one frame that could. */
        return DRIVE_NOT_FOUND;
    if( varp_id >= TORIRSSERVER_VARP_COUNT )
        return DRIVE_NOT_FOUND;
    *out_value = (int)player->varps[varp_id];
    return DRIVE_OK;
}

enum DriveResult
DriveState_InvCount(struct App* app, int container_id, int obj_id, int* out_total)
{
    assert(app);
    assert(out_total);
    assert(container_id >= 0);
    assert(obj_id > 0);
    if( !InvManager_FindContainer(&app->invs, container_id) )
    {
        /* The container has not been populated yet (e.g. before login) --
         * a runtime state the caller can legitimately ask about, not a bug. */
        *out_total = 0;
        return DRIVE_NOT_FOUND;
    }
    *out_total = InvManager_Total(&app->invs, container_id, obj_id);
    return DRIVE_OK;
}

enum DriveResult
DriveState_InvSlot(struct App* app, int container_id, int slot, int* out_obj_id, int* out_count)
{
    struct InvContainer const* container;

    assert(app);
    assert(out_obj_id);
    assert(out_count);
    assert(container_id >= 0);

    container = InvManager_FindContainer(&app->invs, container_id);
    if( !container )
    {
        *out_obj_id = -1;
        *out_count = 0;
        return DRIVE_NOT_FOUND;
    }
    if( slot < 0 || slot >= container->slot_count )
    {
        /* i is a legitimate caller ordinal (plan 5.7): out of today's
         * capacity is a runtime answer, not a contract violation. */
        *out_obj_id = -1;
        *out_count = 0;
        return DRIVE_NOT_FOUND;
    }
    /* An empty-but-in-range slot (obj_id == INV_MANAGER_EMPTY_OBJ_ID) is
     * still DRIVE_OK -- "the slot exists and is empty" is what was asked. */
    *out_obj_id = InvManager_GetObj(&app->invs, container_id, slot);
    *out_count = InvManager_GetNum(&app->invs, container_id, slot);
    return DRIVE_OK;
}

enum DriveResult
DriveState_InvCapacity(struct App* app, int container_id, int* out_capacity)
{
    struct InvContainer const* container;

    assert(app);
    assert(out_capacity);
    assert(container_id >= 0);

    container = InvManager_FindContainer(&app->invs, container_id);
    if( !container )
    {
        *out_capacity = 0;
        return DRIVE_NOT_FOUND;
    }
    *out_capacity = container->slot_count;
    return DRIVE_OK;
}

/* -------------------------------------------------------------- skill */

enum DriveResult
DriveState_Skill(struct App* app, int stat_index, struct DriveSkillSnapshot* out_snapshot)
{
    assert(app);
    assert(out_snapshot);
    /* stat_index only ever arrives here already resolved by
     * DriveSymbol_Lookup(DRIVE_SYMBOL_STAT, ...); an id outside the table is
     * that trampoline's bug, not a runtime state this verb can be asked
     * about, so it asserts rather than answering not_found (plan 5.7: "skill
     * (name) -> ok/not_found" -- the not_found belongs to the symbol lookup,
     * not this read). */
    assert(stat_index >= 0);
    assert(stat_index < RS_PLAYER_STATS_SKILL_COUNT);

    out_snapshot->level = app->stats.current_level[stat_index];
    out_snapshot->base_level = app->stats.base_level[stat_index];
    out_snapshot->experience = app->stats.xp[stat_index];
    /* The pre-login table is a fresh account's, not an empty one:
     * last_seen_level != 0 is the only honest "has the server told us yet". */
    out_snapshot->stated = app->stats.last_seen_level[stat_index] != 0;
    return DRIVE_OK;
}

/* ------------------------------------------------------------- msg.* */

enum DriveResult
DriveState_Messages(struct App* app, struct DriveChatMessage* out, int cap, int* out_count)
{
    int count;
    int i;

    assert(app);
    assert(out);
    assert(cap > 0);
    assert(out_count);

    count = app->chat.message_count;
    if( count > cap )
        count = cap;

    for( i = 0; i < count; i++ )
    {
        out[i].type = app->chat.messages[i].type;
        out[i].serial = app->chat.messages[i].serial;
        snprintf(
            out[i].sender, sizeof(out[i].sender), "%s", app->chat.messages[i].sender);
        snprintf(out[i].text, sizeof(out[i].text), "%s", app->chat.messages[i].text);
    }
    *out_count = count;
    return DRIVE_OK;
}

enum DriveResult
DriveState_MessageSerial(struct App* app, int* out_serial)
{
    assert(app);
    assert(out_serial);
    /* "The serial the next message will exceed" (header): the newest line's
     * serial, or 0 while the ring is empty -- RS_ChatMessage.serial is 1 at
     * the first line ever added and 0 means "never set" (RS_Chat_Init zeroes
     * the struct), so 0 is already the correct floor with no message yet. */
    *out_serial = app->chat.message_count > 0 ? app->chat.messages[0].serial : 0;
    return DRIVE_OK;
}

/* ------------------------------------------------------------- Lua thunks */

static int
lua_drive_varp(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int varp_id = PluginDrive_ArgInt(L, 1);
    int value = 0;
    enum DriveResult result;

    assert(app);
    result = DriveState_Varp(app, varp_id, &value);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, value);
    return 2;
}

static int
lua_drive_varbit(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int varbit_id = PluginDrive_ArgInt(L, 1);
    int value = 0;
    enum DriveResult result;

    assert(app);
    result = DriveState_Varbit(app, varbit_id, &value);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, value);
    return 2;
}

static int
lua_drive_varbit_base(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int varbit_id = PluginDrive_ArgInt(L, 1);
    int base_varp = -1;
    enum DriveResult result;

    assert(app);
    result = DriveState_VarbitBaseVarp(app, varbit_id, &base_varp);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, base_varp);
    return 2;
}

static int
lua_drive_var_server(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int varp_id = PluginDrive_ArgInt(L, 1);
    int value = 0;
    enum DriveResult result;

    assert(app);
    result = DriveState_VarpServer(app, varp_id, &value);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, value);
    return 2;
}

static int
lua_drive_varbit_server(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int varbit_id = PluginDrive_ArgInt(L, 1);
    int value = 0;
    enum DriveResult result;

    assert(app);
    result = DriveState_VarbitServer(app, varbit_id, &value);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, value);
    return 2;
}

static int
lua_drive_var_content(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int varp_id = PluginDrive_ArgInt(L, 1);
    int value = 0;
    enum DriveResult result;

    assert(app);
    result = DriveState_VarpContent(app, varp_id, &value);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, value);
    return 2;
}

static int
lua_drive_inv_count(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int container_id = PluginDrive_ArgInt(L, 1);
    int obj_id = PluginDrive_ArgInt(L, 2);
    int total = 0;
    enum DriveResult result;

    assert(app);
    result = DriveState_InvCount(app, container_id, obj_id, &total);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, total);
    return 2;
}

static int
lua_drive_inv_slot(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int container_id = PluginDrive_ArgInt(L, 1);
    int slot = PluginDrive_ArgInt(L, 2);
    int obj_id = -1;
    int count = 0;
    enum DriveResult result;

    assert(app);
    result = DriveState_InvSlot(app, container_id, slot, &obj_id, &count);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, obj_id);
    lua_setfield(L, -2, "obj_id");
    lua_pushinteger(L, count);
    lua_setfield(L, -2, "count");
    return 2;
}

static int
lua_drive_inv_capacity(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int container_id = PluginDrive_ArgInt(L, 1);
    int capacity = 0;
    enum DriveResult result;

    assert(app);
    result = DriveState_InvCapacity(app, container_id, &capacity);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, capacity);
    return 2;
}

static int
lua_drive_skill(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int stat_index = PluginDrive_ArgInt(L, 1);
    struct DriveSkillSnapshot snapshot;
    enum DriveResult result;

    assert(app);
    result = DriveState_Skill(app, stat_index, &snapshot);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);
    lua_pushstring(L, DriveResultName(result));
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, snapshot.level);
    lua_setfield(L, -2, "level");
    lua_pushinteger(L, snapshot.base_level);
    lua_setfield(L, -2, "base_level");
    lua_pushinteger(L, snapshot.experience);
    lua_setfield(L, -2, "experience");
    lua_pushboolean(L, snapshot.stated);
    lua_setfield(L, -2, "stated");
    return 2;
}

/* app->chat.messages[] is capped at RS_CHAT_MESSAGE_MAX; a quest test never
 * needs more than that in one read. */
#define DRIVE_STATE_MESSAGES_MAX RS_CHAT_MESSAGE_MAX

static int
lua_drive_messages(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int cap = PluginDrive_ArgOptInt(L, 1, DRIVE_STATE_MESSAGES_MAX);
    struct DriveChatMessage out[DRIVE_STATE_MESSAGES_MAX];
    int count = 0;
    int i;
    enum DriveResult result;

    assert(app);
    if( cap <= 0 )
        cap = 1;
    if( cap > DRIVE_STATE_MESSAGES_MAX )
        cap = DRIVE_STATE_MESSAGES_MAX;

    result = DriveState_Messages(app, out, cap, &count);
    if( result != DRIVE_OK )
        return PluginDrive_PushResult(L, result, NULL);

    lua_pushstring(L, DriveResultName(result));
    lua_createtable(L, count, 0);
    for( i = 0; i < count; i++ )
    {
        lua_createtable(L, 0, 4);
        lua_pushinteger(L, out[i].type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, out[i].serial);
        lua_setfield(L, -2, "serial");
        lua_pushstring(L, out[i].sender);
        lua_setfield(L, -2, "sender");
        lua_pushstring(L, out[i].text);
        lua_setfield(L, -2, "text");
        lua_rawseti(L, -2, i + 1);
    }
    return 2;
}

static int
lua_drive_message_serial(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int serial = -1;
    enum DriveResult result;

    assert(app);
    result = DriveState_MessageSerial(app, &serial);
    lua_pushstring(L, DriveResultName(result));
    lua_pushinteger(L, serial);
    return 2;
}


/* ------------------------------------------------------------- sail.* */

/*
 * api_drive.vessel() -> ("ok", reading) | ("unsupported", nil).
 *
 * The quest driver's one vessel read (script/plugins/quest_driver/sail.lua).
 * Two halves, labelled as such, and neither stands in for the other:
 *
 *   - SERVER: the embedded server's hull under the bound player's feet
 *     (ToriRSServer_VesselAtTile -- a rider stands on the deck's pool
 *     tiles, so "aboard" is a question about the tile, exactly as every
 *     server seam asks it). Hull tile, yaw, command state, sails, anchor,
 *     helm, and the arrival latch's counters (torirs_server_vessel.c
 *     vessel_update_zones), which is what a test awaits to know a sea leg
 *     reached a square content binds.
 *   - CLIENT: whether this client has the hull's world entity live under the
 *     same view id, and where it draws it. A server hull the client never
 *     heard of is a wire failure, and a sail verb must be able to say so.
 *
 * Like DriveState_VarpContent this is a read of the server's own state for
 * the socket-less embed only; a socket run answers `unsupported`.
 */
static char const*
drive_state_vessel_state_name(enum ToriRSServerVesselState state)
{
    switch( state )
    {
    case TORIRSSERVER_VESSEL_IDLE:
        return "idle";
    case TORIRSSERVER_VESSEL_HEADING:
        return "heading";
    case TORIRSSERVER_VESSEL_TARGET:
        return "target";
    }
    return "?";
}

static void
drive_state_set_int(struct lua_State* L, char const* key, int value)
{
    assert(L);
    assert(key);
    lua_pushinteger(L, value);
    lua_setfield(L, -2, key);
}

static int
lua_drive_vessel(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    struct ToriRSServer* srv;
    struct ToriRSServerPlayer* player;
    struct ToriRSServerVessel* vessel;

    assert(app);
    srv = PluginDrive_EmbedWorld();
    if( !srv )
        return PluginDrive_PushResult(L, DRIVE_UNSUPPORTED, NULL);
    player = srv->active_player;
    if( !player )
        return PluginDrive_PushResult(L, DRIVE_NOT_FOUND, NULL);

    vessel = ToriRSServer_VesselAtTile(srv, player->x, player->z);
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_createtable(L, 0, 24);
    lua_pushboolean(L, vessel != NULL);
    lua_setfield(L, -2, "aboard");
    drive_state_set_int(L, "player_x", player->x);
    drive_state_set_int(L, "player_z", player->z);
    drive_state_set_int(L, "player_level", player->level);
    if( !vessel )
        return 2;

    drive_state_set_int(L, "handle", vessel->index);
    drive_state_set_int(L, "serial", vessel->serial);
    drive_state_set_int(L, "view_id", vessel->view_id);
    drive_state_set_int(L, "config_id", vessel->config_id);
    drive_state_set_int(L, "level", vessel->level);
    drive_state_set_int(L, "hull_x", vessel->fine_x >> 7);
    drive_state_set_int(L, "hull_z", vessel->fine_z >> 7);
    drive_state_set_int(L, "fine_x", vessel->fine_x);
    drive_state_set_int(L, "fine_z", vessel->fine_z);
    drive_state_set_int(L, "angle", vessel->angle);
    drive_state_set_int(L, "heading", vessel->heading);
    drive_state_set_int(L, "speed_tier", vessel->speed_tier);
    lua_pushstring(L, drive_state_vessel_state_name(vessel->state));
    lua_setfield(L, -2, "state");
    lua_pushboolean(L, vessel->sails_set != 0);
    lua_setfield(L, -2, "sails_set");
    lua_pushboolean(L, vessel->anchored != 0);
    lua_setfield(L, -2, "anchored");
    lua_pushboolean(L, player->navigating_vessel == vessel->index &&
                           player->navigating_vessel_serial == vessel->serial);
    lua_setfield(L, -2, "at_helm");
    drive_state_set_int(L, "arrivals", vessel->arrivals);
    lua_pushstring(L, vessel->arrival_last);
    lua_setfield(L, -2, "arrival_last");

    /* The client's half: the same view id, live, and where it is drawn. */
    if( vessel->view_id > 0 && Wevs_IsLive(&app->wevs, vessel->view_id) )
    {
        struct Wev* wev = Wevs_Get(&app->wevs, vessel->view_id);

        lua_pushboolean(L, 1);
        lua_setfield(L, -2, "client_live");
        drive_state_set_int(L, "client_hull_x", wev->x >> 7);
        drive_state_set_int(L, "client_hull_z", wev->z >> 7);
        drive_state_set_int(L, "client_angle", wev->angle);
    }
    else
    {
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "client_live");
    }
    return 2;
}

static struct LuaFn const LUA_DRIVE_STATE_FNS[] = {
    {"varp", lua_drive_varp},
    {"varbit", lua_drive_varbit},
    {"varbit_base", lua_drive_varbit_base},
    {"var_server", lua_drive_var_server},
    {"varbit_server", lua_drive_varbit_server},
    {"var_content", lua_drive_var_content},
    {"inv_count", lua_drive_inv_count},
    {"inv_slot", lua_drive_inv_slot},
    {"inv_capacity", lua_drive_inv_capacity},
    {"skill", lua_drive_skill},
    {"messages", lua_drive_messages},
    {"message_serial", lua_drive_message_serial},
    {"vessel", lua_drive_vessel},
    {NULL, NULL},
};

void
PluginDriveState_RegisterLua(struct lua_State* L, void* script)
{
    assert(L);
    assert(script);
    PluginLua_AppendModule(L, script, LUA_DRIVE_STATE_FNS);
}

#else /* !TORIRS_EMBED_SERVER */

/* No embedded server, no driver. The registrar stays so the module assembly
 * in torirs_plugin_drive.c needs no second spelling. */
void
PluginDriveState_RegisterLua(struct lua_State* L, void* script)
{
    (void)L;
    (void)script;
}

#endif /* TORIRS_EMBED_SERVER */
