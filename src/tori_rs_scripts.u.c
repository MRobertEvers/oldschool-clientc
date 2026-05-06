#ifndef TORI_RS_SCRIPTS_U_C
#define TORI_RS_SCRIPTS_U_C

#include "tori_rs.h"

#include "osrs/core/revision.h"

#include <assert.h>

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
    case SCRIPT_INIT_UI:
        set_name(out, revision_lua_init_ui_path(revision_active()));
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
    case SCRIPT_PKT_DISPATCH:
        set_name(out, revision_lua_cacherev_load_path(revision_active()));
        out->args = LuaGameType_NewVoid();
        break;
    case SCRIPT_LOAD_CULLMAP:
        set_name(out, "game/load_cullmap.lua");
        out->args = LuaGameType_NewVarTypeArraySpread(4);
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.load_cullmap.viewport_w));
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.load_cullmap.viewport_h));
        LuaGameType_VarTypeArrayPush(out->args, LuaGameType_NewInt(item->args.u.load_cullmap.near_clip_z));
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.load_cullmap.draw_radius));
        break;
    case SCRIPT_SPAWN_ELEMENT:
        set_name(out, "game/spawn_element.lua");
        out->args = LuaGameType_NewVarTypeArraySpread(5);
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.spawn_element.world_x));
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.spawn_element.world_z));
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.spawn_element.world_level));
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.spawn_element.model_id));
        LuaGameType_VarTypeArrayPush(
            out->args, LuaGameType_NewInt(item->args.u.spawn_element.seq_id));
        break;
    default:
        assert(false && "Unknown script kind");
        break;
    }
}

#endif