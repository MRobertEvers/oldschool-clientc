/*
 * quest-driver: an npc type's record and a sequence's length, read-only.
 *
 * Owner: the waves loop's seam `npc_record_reads`
 * (docs/minigames/waves_loop/SEAM_TRIAGE_2026-10-05.md, TEST-2 in
 * docs/minigames/waves_loop/CONTENT_BUGS.md). The Lua half is
 * script/plugins/quest_driver/world.lua (t.npc.record, t.npc.pose,
 * t.seq.length).
 *
 * Why this exists: a wave unit's spec table states an npc's levels, bonuses,
 * model, ready and walk sequences, sounds and animation lengths, and before
 * this no driver verb could read any of them -- every test that wanted those
 * rows had to restate the spec or leave them unmeasured. These verbs read the
 * values the running game holds, from the side that owns each:
 *
 *   client  the cache npc record (struct ToriRS_Npctype) the client resolved
 *           for that id -- what it draws, animates and plays sounds from --
 *           and the sequence the client steps (struct ToriDraw_Animation,
 *           the same lookup World_SeqSource uses for every entity).
 *   server  the embedded server's content block for that id (struct
 *           ToriRSServerNpcDef: the levels and bonuses combat rolls with,
 *           the attack, defend and death sequences and sounds the engine
 *           plays) and its own cache read (struct ToriRSServerNpcInfo).
 *
 * Every field says which side it came from by the sub-table it sits in, so a
 * test can never compare the client's record with itself and call it a
 * measurement of the server.
 *
 * Residency. The client resolves an npc record when a copy of it first comes
 * into view and a sequence when an entity first plays it; neither verb starts
 * a load. A record or sequence the client has not resolved is reported as
 * such (`client_reason`, or not_found from seq_length), never invented.
 *
 * npc_record's server half is absent (`server_reason`) on a socket-server
 * run, which has no world in this process (PluginDrive_EmbedWorld).
 */

#include "plugin/torirs_plugin_drive.h"

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER

#include "app.h"
#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "engine/world_seq_source_toridraw.h"
#include "plugin/torirs_plugin_lua.h"
#include "toridraw_animation.h"
#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_content.h"
#include "world/entity_pool.h"

#include "lauxlib.h"
#include "lua.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The twelve combat bonuses in enum ToriRSServerCombatParam order (param ids
 * 0..11 of an OldSchool npc or obj record). */
static char const* const DRIVE_RECORD_BONUS_NAMES[TORIRSSERVER_PARAM_BONUS_COUNT] = {
    "stabattack",   "slashattack",  "crushattack",   "magicattack",
    "rangeattack",  "stabdefence",  "slashdefence",  "crushdefence",
    "magicdefence", "rangedefence", "strengthbonus", "prayerbonus",
};

static void
drive_record_push_int(
    struct lua_State* L,
    char const* name,
    int value)
{
    assert(L);
    assert(name);
    lua_pushinteger(L, (lua_Integer)value);
    lua_setfield(L, -2, name);
}

static void
drive_record_push_bool(
    struct lua_State* L,
    char const* name,
    int value)
{
    assert(L);
    assert(name);
    lua_pushboolean(L, value ? 1 : 0);
    lua_setfield(L, -2, name);
}

/* A content symbol for an id, or nil: names only, for a reader of the row
 * (ToriRSServer_ContentSymbolName is a linear scan, so this is never on a
 * frame path -- a record read is one call per test row). */
static void
drive_record_push_symbol(
    struct lua_State* L,
    char const* field,
    enum ToriRSServerPackKind kind,
    int id)
{
    char const* name;

    assert(L);
    assert(field);
    name = id >= 0 ? ToriRSServer_ContentSymbolName(kind, id) : NULL;
    if( name )
        lua_pushstring(L, name);
    else
        lua_pushnil(L);
    lua_setfield(L, -2, field);
}

/* A seq id field and its symbol beside it (`readyanim` and
 * `readyanim_name`). -1 stays -1 with a nil name. */
static void
drive_record_push_seq(
    struct lua_State* L,
    char const* field,
    int seq_id)
{
    char name_field[64];

    assert(L);
    assert(field);
    drive_record_push_int(L, field, seq_id);
    snprintf(name_field, sizeof(name_field), "%s_name", field);
    drive_record_push_symbol(L, name_field, TORIRSSERVER_PACK_SEQ, seq_id);
}

static void
drive_record_push_int_list(
    struct lua_State* L,
    char const* field,
    int const* values,
    int count)
{
    assert(L);
    assert(field);
    assert(count >= 0);
    lua_createtable(L, count, 0);
    for( int i = 0; i < count; i++ )
    {
        lua_pushinteger(L, (lua_Integer)values[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, field);
}

/* The client half: the cache record as this client resolved it. */
static void
drive_record_push_client(
    struct lua_State* L,
    struct ToriRS_Npctype const* type)
{
    assert(L);
    assert(type);

    lua_createtable(L, 0, 40);
    drive_record_push_int(L, "id", type->id);
    lua_pushstring(L, type->name);
    lua_setfield(L, -2, "name");
    drive_record_push_int(L, "size", type->size);
    drive_record_push_int(L, "combat_level", type->combat_level);
    drive_record_push_int(L, "category", type->category);
    drive_record_push_int_list(L, "models", type->models, type->models_count);
    drive_record_push_int_list(L, "heads", type->heads, type->heads_count);
    drive_record_push_seq(L, "readyanim", type->readyanim);
    drive_record_push_seq(L, "walkanim", type->walkanim);
    drive_record_push_seq(L, "walkanim_b", type->walkanim_b);
    drive_record_push_seq(L, "walkanim_r", type->walkanim_r);
    drive_record_push_seq(L, "walkanim_l", type->walkanim_l);
    drive_record_push_seq(L, "turnanim_l", type->turnanim_l);
    drive_record_push_seq(L, "turnanim_r", type->turnanim_r);
    drive_record_push_seq(L, "runanim", type->runanim);
    drive_record_push_int(L, "turn_speed", type->turn_speed);
    drive_record_push_bool(L, "idle_anim_restart", type->idle_anim_restart);
    drive_record_push_int(L, "width_scale", type->width_scale);
    drive_record_push_int(L, "height_scale", type->height_scale);
    drive_record_push_int(L, "height", type->height);
    drive_record_push_int(L, "head_icon_group", type->head_icon_group);
    drive_record_push_int(L, "head_icon_index", type->head_icon_index);
    drive_record_push_bool(L, "interactable", type->interactable);
    drive_record_push_bool(L, "minimap_visible", type->minimap_visible);
    /* Movement sounds (npc opcode 134): the only sounds an npc RECORD
     * carries. Combat sounds are the server's (server.attack_sound ...). */
    drive_record_push_int(L, "sound_idle", type->sound_idle);
    drive_record_push_int(L, "sound_crawl", type->sound_crawl);
    drive_record_push_int(L, "sound_walk", type->sound_walk);
    drive_record_push_int(L, "sound_run", type->sound_run);
    drive_record_push_int(L, "sound_radius", type->sound_radius);
    drive_record_push_int(L, "transform_count", type->transform_count);

    /* params: { [param id] = value } and param_names: { [param id] = symbol }.
     * A string param keeps its string. */
    lua_createtable(L, 0, type->param_count);
    for( int i = 0; i < type->param_count; i++ )
    {
        struct ToriRS_Param const* param = &type->params[i];

        if( param->string_value )
            lua_pushstring(L, param->string_value);
        else
            lua_pushinteger(L, (lua_Integer)param->int_value);
        lua_rawseti(L, -2, param->key);
    }
    lua_setfield(L, -2, "params");
    lua_createtable(L, 0, type->param_count);
    for( int i = 0; i < type->param_count; i++ )
    {
        char const* name =
            ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_PARAM, type->params[i].key);

        if( !name )
            continue;
        lua_pushstring(L, name);
        lua_rawseti(L, -2, type->params[i].key);
    }
    lua_setfield(L, -2, "param_names");
    lua_setfield(L, -2, "client");
}

/* The server half: the content block combat rolls with and the server's own
 * cache read. `def` is NULL when no content block names the id: the engine
 * then fights it with ToriRSServer_ContentNpcDefault(), and that is what is
 * reported, with `authored` false. */
static void
drive_record_push_server(
    struct lua_State* L,
    int npc_id)
{
    const struct ToriRSServerNpcDef* def;
    const struct ToriRSServerNpcInfo* info;
    int authored;

    assert(L);
    assert(npc_id >= 0);

    def = ToriRSServer_ContentNpc(npc_id);
    authored = def != NULL;
    if( !def )
        def = ToriRSServer_ContentNpcDefault();
    assert(def);
    info = ToriRSServer_NpcInfo(npc_id);
    assert(info);

    lua_createtable(L, 0, 48);
    drive_record_push_int(L, "id", npc_id);
    drive_record_push_bool(L, "authored", authored);
    if( authored && def->symbol )
        lua_pushstring(L, def->symbol);
    else
        lua_pushnil(L);
    lua_setfield(L, -2, "symbol");
    if( info->name )
        lua_pushstring(L, info->name);
    else
        lua_pushnil(L);
    lua_setfield(L, -2, "name");
    drive_record_push_int(L, "combat_level", info->combat_level);
    drive_record_push_int(L, "size", info->size);

    /* The levels the content block authored -- what npc_basestat answers and
     * every combat roll starts from (a drained copy's current level is base
     * minus its own drain, which this static read does not include). */
    drive_record_push_int(L, "hitpoints", def->hitpoints);
    drive_record_push_int(L, "attack", def->attack);
    drive_record_push_int(L, "strength", def->strength);
    drive_record_push_int(L, "defence", def->defence);
    drive_record_push_int(L, "ranged", def->ranged);
    drive_record_push_int(L, "magic", def->magic);

    lua_createtable(L, 0, TORIRSSERVER_PARAM_BONUS_COUNT);
    for( int i = 0; i < TORIRSSERVER_PARAM_BONUS_COUNT; i++ )
    {
        lua_pushinteger(L, (lua_Integer)def->bonus[i]);
        lua_setfield(L, -2, DRIVE_RECORD_BONUS_NAMES[i]);
    }
    lua_setfield(L, -2, "bonus");

    drive_record_push_int(L, "attackrate", def->attackrate);
    drive_record_push_int(L, "attackrange", def->attackrange);
    drive_record_push_int(L, "damagetype", def->damagetype);
    drive_record_push_seq(L, "attack_anim", def->attack_anim);
    drive_record_push_seq(L, "defend_anim", def->defend_anim);
    drive_record_push_seq(L, "death_anim", def->death_anim);
    drive_record_push_int(L, "attack_sound", def->attack_sound);
    drive_record_push_int(L, "defend_sound", def->defend_sound);
    drive_record_push_int(L, "death_sound", def->death_sound);
    drive_record_push_symbol(L, "attack_sound_name", TORIRSSERVER_PACK_SYNTH, def->attack_sound);
    drive_record_push_symbol(L, "defend_sound_name", TORIRSSERVER_PACK_SYNTH, def->defend_sound);
    drive_record_push_symbol(L, "death_sound_name", TORIRSSERVER_PACK_SYNTH, def->death_sound);
    drive_record_push_int(L, "respawnrate", def->respawnrate);
    drive_record_push_int(L, "death_delay", def->death_delay);
    drive_record_push_int(L, "wanderrange", def->wanderrange);
    drive_record_push_int(L, "huntrange", def->huntrange);
    drive_record_push_int(L, "maxrange", def->maxrange);
    drive_record_push_bool(L, "aggressive", def->huntmode == TORIRSSERVER_HUNT_AGGRESSIVE);
    drive_record_push_bool(L, "retaliate", def->retaliate);
    drive_record_push_bool(L, "givechase", def->givechase);
    lua_setfield(L, -2, "server");
}

/* api.drive.npc_record(npc_id)
 *   -> "ok", { id, client = {...} | nil, client_reason = text | nil,
 *              server = {...} | nil, server_reason = text | nil }
 *
 * Always `ok` for a valid id: which half is missing, and why, is the
 * answer, and world.lua turns it into the verb's result. A negative id is
 * a test bug and raises. */
static int
lua_drive_npc_record(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    struct ToriRSServer* srv = PluginDrive_EmbedWorld();
    int npc_id = PluginDrive_ArgInt(L, 1);
    struct ToriRS_Npctype* type = NULL;

    assert(app);
    if( npc_id < 0 )
        return luaL_error(L, "drive.npc_record: npc id %d is negative", npc_id);

    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_createtable(L, 0, 5);
    drive_record_push_int(L, "id", npc_id);

    if( app->provider )
        type = CacheProvider_NpctypeGet(app->provider, npc_id);
    if( type )
    {
        drive_record_push_client(L, type);
    }
    else
    {
        lua_pushstring(L, app->provider
                              ? "the client has not resolved this npc record (it resolves one"
                                " when a copy first comes into view)"
                              : "the client has no cache provider yet");
        lua_setfield(L, -2, "client_reason");
    }

    if( srv )
    {
        drive_record_push_server(L, npc_id);
    }
    else
    {
        lua_pushstring(L, "no embedded server in this process (a socket-server run)");
        lua_setfield(L, -2, "server_reason");
    }
    return 2;
}

/* api.drive.seq_length(seq_id)
 *   -> "ok", { seq_id, name, frames, cycles, lengths = {...}, skeletal,
 *              frame_step, max_loops, priority,
 *              frame_sounds = {{frame, id, loops, radius}...} }
 *    | "not_found", detail   (the client has not resolved that sequence)
 *
 * `cycles` sums each frame's length exactly as the client steps it
 * (world_seq_source_toridraw.c's frame_duration: a frame of length 0 or a
 * skeletal frame is one cycle), so it is the time the client takes to play
 * the sequence once. 30 client cycles are one game tick. `frame` in a frame
 * sound is 0-based, as the client indexes frames. */
static int
lua_drive_seq_length(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int seq_id = PluginDrive_ArgInt(L, 1);
    struct ToriDraw_Animation const* anim;
    char detail[160];
    int cycles = 0;

    assert(app);
    if( seq_id < 0 )
        return luaL_error(L, "drive.seq_length: seq id %d is negative", seq_id);

    anim = WorldSeqSourceToriDraw_Animation(&app->seq_source, seq_id);
    if( !anim || anim->frame_count <= 0 )
    {
        snprintf(detail, sizeof(detail),
                 "seq %d is not resolved on the client (it resolves a sequence when an entity"
                 " first plays it)",
                 seq_id);
        return PluginDrive_PushResult(L, DRIVE_NOT_FOUND, detail);
    }

    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_createtable(L, 0, 12);
    drive_record_push_int(L, "seq_id", seq_id);
    drive_record_push_symbol(L, "name", TORIRSSERVER_PACK_SEQ, seq_id);
    drive_record_push_int(L, "frames", anim->frame_count);
    drive_record_push_bool(L, "skeletal", anim->frames == NULL);
    lua_createtable(L, anim->frame_count, 0);
    for( int frame = 0; frame < anim->frame_count; frame++ )
    {
        int length = 1;

        if( anim->frames && anim->frames[frame].delay > 0 )
            length = anim->frames[frame].delay;
        cycles += length;
        lua_pushinteger(L, (lua_Integer)length);
        lua_rawseti(L, -2, frame + 1);
    }
    lua_setfield(L, -2, "lengths");
    drive_record_push_int(L, "cycles", cycles);
    drive_record_push_int(L, "frame_step", anim->frame_step);
    drive_record_push_int(L, "max_loops", anim->max_loops);
    drive_record_push_int(L, "priority", anim->priority);

    lua_createtable(L, anim->frame_sounds.count, 0);
    for( int i = 0; i < anim->frame_sounds.count; i++ )
    {
        struct ToriDraw_AnimFrameSound const* sound = &anim->frame_sounds.sounds[i];

        lua_createtable(L, 0, 4);
        drive_record_push_int(L, "frame", anim->frame_sounds.frame_indices[i]);
        drive_record_push_int(L, "id", sound->id);
        drive_record_push_int(L, "loops", sound->loops);
        drive_record_push_int(L, "radius", sound->radius);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "frame_sounds");
    return 2;
}

/* world_cycle.c's anim_step_active: 0xFFFF and 0 both mean "no track" (a
 * fresh entity's zeroed track is 0), the rule DriveNpcRow.anim_id uses. */
static int
drive_record_track_seq(struct WorldEntityFacet_AnimationStep const* step)
{
    assert(step);
    if( step->anim_id == (uint16_t)-1 || step->anim_id == 0 )
        return -1;
    return (int)step->anim_id;
}

/* Which of the npc's movement set the movement track is playing. */
static char const*
drive_record_pose_kind(
    struct WorldEntityFacet_IdleAnimations const* idle,
    int pose_seq)
{
    assert(idle);
    if( pose_seq < 0 )
        return "none";
    /* A record whose walkanim IS its readyanim (Jal-MejRah's 7577 is both)
     * plays one seq standing and moving; the track cannot say which. */
    if( pose_seq == idle->readyanim )
        return pose_seq == idle->walkanim ? "ready_or_walk" : "ready";
    if( pose_seq == idle->walkanim )
        return "walk";
    if( pose_seq == idle->runanim )
        return "run";
    if( pose_seq == idle->turnanim )
        return "turn";
    if( pose_seq == idle->walkanim_b )
        return "walk_back";
    if( pose_seq == idle->walkanim_l )
        return "walk_left";
    if( pose_seq == idle->walkanim_r )
        return "walk_right";
    return "other";
}

/* api.drive.npc_pose(client_slot)
 *   -> "ok", { slot, npc_id, base_npc_id, pose_seq, pose_frame, pose_kind,
 *              action_seq, action_frame, readyanim, walkanim, turnanim,
 *              runanim, walkanim_b, walkanim_l, walkanim_r }
 *    | "not_found", detail   (no client npc has that NPC_INFO slot)
 *
 * The movement track (`animation.secondary`: the ready, walk, turn or run
 * sequence the client is stepping under any action) and the action track,
 * side by side, with the movement set the entity was given from its record.
 * DriveNpcRow.anim_id is the action track only, so it reads -1 at rest and
 * while walking; this is the other track. */
static int
lua_drive_npc_pose(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int client_slot = PluginDrive_ArgInt(L, 1);
    struct World_EntityPool* pool;
    char detail[96];

    assert(app);
    if( client_slot < 0 )
        return luaL_error(L, "drive.npc_pose: slot %d is negative", client_slot);
    if( !app->world )
        return PluginDrive_PushResult(L, DRIVE_NOT_FOUND, "no world loaded");

    pool = &app->world->entities.npc;
    /* The npc entity pool (a few hundred), walked once per test read. */
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC const* npc = World_EntityPoolGet(pool, i);
        struct WorldEntityFacet_IdleAnimations const* idle;
        int pose_seq;
        int action_seq;

        if( !npc || npc->server_slot != client_slot )
            continue;
        idle = &npc->idle_animations;
        pose_seq = drive_record_track_seq(&npc->animation.secondary);
        action_seq = drive_record_track_seq(&npc->animation.primary);

        lua_pushstring(L, DriveResultName(DRIVE_OK));
        lua_createtable(L, 0, 24);
        drive_record_push_int(L, "slot", client_slot);
        drive_record_push_int(L, "npc_id", npc->npc_id);
        drive_record_push_int(L, "base_npc_id", npc->base_npc_id);
        drive_record_push_seq(L, "pose_seq", pose_seq);
        drive_record_push_int(L, "pose_frame", pose_seq >= 0 ? npc->animation.secondary.frame : 0);
        lua_pushstring(L, drive_record_pose_kind(idle, pose_seq));
        lua_setfield(L, -2, "pose_kind");
        drive_record_push_seq(L, "action_seq", action_seq);
        drive_record_push_int(L, "action_frame",
                              action_seq >= 0 ? npc->animation.primary.frame : 0);
        drive_record_push_seq(L, "readyanim", idle->readyanim);
        drive_record_push_seq(L, "walkanim", idle->walkanim);
        drive_record_push_seq(L, "turnanim", idle->turnanim);
        drive_record_push_seq(L, "runanim", idle->runanim);
        drive_record_push_int(L, "walkanim_b", idle->walkanim_b);
        drive_record_push_int(L, "walkanim_l", idle->walkanim_l);
        drive_record_push_int(L, "walkanim_r", idle->walkanim_r);
        return 2;
    }
    snprintf(detail, sizeof(detail), "no client npc has slot %d", client_slot);
    return PluginDrive_PushResult(L, DRIVE_NOT_FOUND, detail);
}

static struct LuaFn const LUA_DRIVE_RECORD_FNS[] = {
    {"npc_record", lua_drive_npc_record},
    {"seq_length", lua_drive_seq_length},
    {"npc_pose", lua_drive_npc_pose},
    {NULL, NULL},
};

void
PluginDriveRecord_RegisterLua(
    struct lua_State* L,
    void* script)
{
    assert(L);
    assert(script);
    PluginLua_AppendModule(L, script, LUA_DRIVE_RECORD_FNS);
}

#else /* !TORIRS_EMBED_SERVER */

/* No embedded server, no driver: nothing here is reachable. ISO C wants a
 * translation unit to declare something. */
typedef int PluginDriveRecord_Unused;

#endif /* TORIRS_EMBED_SERVER */
