#ifndef TORI_RS_SCRIPTS_U_C
#define TORI_RS_SCRIPTS_U_C

#include "tori_rs.h"

static void
set_name(
    struct LuaGameScript* out,
    const char* name)
{
    strncpy(out->name, name, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
}

void
script_convert_to_lua(
    struct ScriptQueueItem* item,
    struct LuaGameScript* out)
{
    switch( item->args.tag )
    {
    case SCRIPT_INIT:
        set_name(out, "empty.lua");
        out->args = LuaGameType_NewVoid();
        break;
    case SCRIPT_LOAD_SCENE_DAT:
        set_name(out, "init_cache_dat.lua");
        out->args = LuaGameType_NewVarTypeArraySpread(4);
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.load_scene_dat.wx_sw));
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.load_scene_dat.wz_sw));
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.load_scene_dat.wx_ne));
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.load_scene_dat.wz_ne));
        break;
    case SCRIPT_PKT_REBUILD_NORMAL:
        set_name(out, "pkt_rebuild_normal.lua");
        out->args = LuaGameType_NewVarTypeArraySpread(2);
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.rebuild_normal.zonex));
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.rebuild_normal.zonez));
        break;
    default:
        assert(false && "Unknown script kind");
        break;
    }
}

#endif