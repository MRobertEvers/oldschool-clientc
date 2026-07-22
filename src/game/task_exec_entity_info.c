#include "task_exec_entity_info.h"

#include "app.h"
#include "engine/entity_model_build.h"
#include "engine/player_appearance.h"
#include "game/rs_chat.h"
#include "game/rs_entity_sync.h"
#include "net/rev/pkt_npc_info.h"
#include "net/rev/pkt_player_appearance.h"
#include "net/rev/pkt_player_info.h"
#include "net/wordpack.h"
#include "world/entity_pathing.h"
#include "world/world.h"

#include <assert.h>
#include <rsbuffer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    ENTITY_INFO_OPS_MAX = 2048,
    /* Local-player sentinel while UPDATE_PID has not arrived. */
    ENTITY_INFO_LOCAL_SENTINEL = 2047,
    /* Classic human movement set — placeholder until the APPEARANCE mask
     * lands (reference always follows a spawn with an appearance update). */
    PLAYER_DEFAULT_SEQ_READY = 808,
};

static void
entity_debug_log(char const* fmt, int a, int b)
{
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(stderr, fmt, a, b);
}

/* ------------------------------------------------------------------ */
/* PLAYER_INFO                                                         */
/* ------------------------------------------------------------------ */

enum PlayerOpNeed
{
    PLAYER_NEED_NONE = 0,
    PLAYER_NEED_APPEARANCE,
    PLAYER_NEED_SEQ,
};

struct Task_ExecPlayerInfo
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    uint8_t const* data; /* borrowed from the owning packet task */
    int length;

    struct PktPlayerInfoReader reader;
    struct PktPlayerInfoOp* ops;
    int op_count;
    int op_i;

    int old_list[RS_ENTITY_SYNC_MAX_PLAYERS];
    int old_count;

    int cur_pid;       /* server slot the following ops target; -1 = none */
    int cur_world_idx; /* resolved world pool index for cur_pid; -1 = none */
    int cur_element_id;
    int need_ensure;

    struct PktPlayerAppearance app_decoded;
    int cfg_i;
    int model_ids[64];
    int model_count;
    int model_i;
    int seq_i;
    int pending_seq;
    int pending_delay;
};

static int
local_player_pid(struct RS_EntitySync const* esync)
{
    return esync->local_pid >= 0 ? esync->local_pid : ENTITY_INFO_LOCAL_SENTINEL;
}

/* Local player's scene tile (fallback: classic scene center) for relative
 * adds. Server-local coords are classic-scene relative; scene_off maps them
 * into our map-square-aligned scene. */
static void
player_local_tile(
    struct Task_ExecPlayerInfo* self,
    int* out_x,
    int* out_z,
    int* out_level)
{
    int world_idx;
    *out_x = self->app->scene_off_x + 52;
    *out_z = self->app->scene_off_z + 52;
    *out_level = 0;
    if( RS_EntitySync_FindPlayer(&self->app->esync, local_player_pid(&self->app->esync), &world_idx, NULL) )
    {
        struct WorldEntity_Player* player =
            World_EntityPoolGet(&self->app->world->entities.player, world_idx);
        if( player )
        {
            *out_x = player->grid_position.x;
            *out_z = player->grid_position.z;
            *out_level = player->grid_position.level;
        }
    }
}

/* Resolve the current target after a target-setting op; flags ensure when
 * the entity does not exist yet. Returns 1 when the op was fully handled. */
static int
player_target_op(
    struct Task_ExecPlayerInfo* self,
    struct PktPlayerInfoOp const* op)
{
    struct RS_EntitySync* esync = &self->app->esync;

    switch( op->kind )
    {
    case PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER:
        self->cur_pid = local_player_pid(esync);
        break;
    case PKT_PLAYER_INFO_OP_ADD_PLAYER_OLD_OPBITS_IDX:
        self->cur_pid = (int)op->_bitvalue < self->old_count ? self->old_list[op->_bitvalue] : -1;
        if( self->cur_pid >= 0 && esync->active_player_count < RS_ENTITY_SYNC_MAX_PLAYERS )
            esync->active_players[esync->active_player_count++] = self->cur_pid;
        break;
    case PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID:
        self->cur_pid = (int)op->_bitvalue;
        if( esync->active_player_count < RS_ENTITY_SYNC_MAX_PLAYERS )
            esync->active_players[esync->active_player_count++] = self->cur_pid;
        break;
    case PKT_PLAYER_INFO_OP_SET_PLAYER_OPBITS_IDX:
        self->cur_pid = (int)op->_bitvalue < esync->active_player_count
                            ? esync->active_players[op->_bitvalue]
                            : -1;
        break;
    case PKT_PLAYER_INFO_OP_CLEAR_PLAYER_OPBITS_IDX:
    {
        int pid = (int)op->_bitvalue < self->old_count ? self->old_list[op->_bitvalue] : -1;
        if( pid >= 0 )
            RS_EntitySync_RemovePlayer(esync, self->app->world, pid);
        self->cur_pid = -1;
        self->cur_world_idx = -1;
        self->cur_element_id = -1;
        return 1;
    }
    case PKT_PLAYER_INFO_OPBITS_COUNT_RESET:
        esync->active_player_count = 0;
        return 1;
    case PKT_PLAYER_INFO_OPBITS_INFO:
        return 1;
    default:
        return 0; /* not a target op */
    }

    /* Target changed: resolve, flag a spawn when unknown. */
    self->cur_world_idx = -1;
    self->cur_element_id = -1;
    self->need_ensure = 0;
    if( self->cur_pid >= 0 &&
        !RS_EntitySync_FindPlayer(esync, self->cur_pid, &self->cur_world_idx, &self->cur_element_id) )
        self->need_ensure = 1;
    return 1;
}

/* Spawn the current target with the default composited model; the
 * appearance mask that follows swaps in the real look. Assets are cached
 * (the task awaited them). */
static void
player_ensure_now(struct Task_ExecPlayerInfo* self)
{
    struct App* app = self->app;
    int tile_x, tile_z, level;
    int idx;

    self->need_ensure = 0;
    player_local_tile(self, &tile_x, &tile_z, &level);
    idx = App_WorldSpawnSyncedPlayer(app, tile_x, tile_z, level);
    if( idx < 0 )
        return;
    {
        struct WorldEntity_Player* player =
            World_EntityPoolGet(&app->world->entities.player, idx);
        if( player )
        {
            player->server_pid = self->cur_pid;
            self->cur_element_id = player->element_id;
        }
    }
    self->cur_world_idx = idx;
    RS_EntitySync_RegisterPlayer(&app->esync, self->cur_pid, self->cur_element_id, idx);
    entity_debug_log("entity_sync: player %d spawned (world idx %d)\n", self->cur_pid, idx);
}

/* Apply one non-target op that needs no cache IO. Returns a PlayerOpNeed for
 * the ops the protothread must await loads for. */
static enum PlayerOpNeed
player_apply_op(
    struct Task_ExecPlayerInfo* self,
    struct PktPlayerInfoOp const* op)
{
    struct App* app = self->app;
    struct World* world = app->world;
    int idx = self->cur_world_idx;

    if( idx < 0 )
        return PLAYER_NEED_NONE;

    switch( op->kind )
    {
    case PKT_PLAYER_INFO_OPBITS_WALKDIR:
        World_PlayerPathPushStep(world, idx, WORLD_PATHSTEP_WALK, (int)op->_bitvalue);
        break;
    case PKT_PLAYER_INFO_OPBITS_RUNDIR:
        World_PlayerPathPushStep(world, idx, WORLD_PATHSTEP_RUN, (int)op->_bitvalue);
        break;
    case PKT_PLAYER_INFO_OP_LOCAL_XZLEVEL:
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
        World_PlayerPathJump(
            world,
            idx,
            op->_local_xz_level.jump,
            app->scene_off_x + op->_local_xz_level.x,
            app->scene_off_z + op->_local_xz_level.z);
        if( player )
            player->grid_position.level = op->_local_xz_level.level;
        break;
    }
    case PKT_PLAYER_INFO_OP_DELTA_XZ:
    {
        int lx, lz, llevel;
        player_local_tile(self, &lx, &lz, &llevel);
        World_PlayerPathJump(
            world, idx, op->_delta_xz.jump, lx + op->_delta_xz.dx, lz + op->_delta_xz.dz);
        break;
    }
    case PKT_PLAYER_INFO_OP_APPEARANCE:
        if( PktPlayerAppearance_Decode(
                &self->app_decoded, op->_appearance.appearance, op->_appearance.len) )
            return PLAYER_NEED_APPEARANCE;
        break;
    case PKT_PLAYER_INFO_OP_SEQUENCE:
        self->pending_seq = op->_sequence.sequence_id;
        self->pending_delay = op->_sequence.delay;
        return PLAYER_NEED_SEQ;
    case PKT_PLAYER_INFO_OP_FACE_ENTITY:
    {
        int entity_id = op->_face_entity.entity_id == 65535 ? -1 : op->_face_entity.entity_id;
        World_PlayerFaceEntity(world, idx, entity_id);
        break;
    }
    case PKT_PLAYER_INFO_OP_FACE_COORD:
        /* Wire is absolute half-tiles ((tile << 1) + 1); store scene tiles. */
        World_PlayerFaceCoord(
            world,
            idx,
            (op->_face_coord.x >> 1) - world->_base_tile_x,
            (op->_face_coord.z >> 1) - world->_base_tile_z);
        break;
    case PKT_PLAYER_INFO_OP_SAY:
        if( op->_say.text )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
            RS_Chat_AddMessage(
                &app->chat,
                RS_CHAT_TYPE_PUBLIC,
                player && player->name[0] ? player->name : "Player",
                op->_say.text);
        }
        break;
    case PKT_PLAYER_INFO_OP_CHAT:
        if( op->_chat.data && op->_chat.length > 0 )
        {
            struct RSCache_Buffer chatbuf;
            char* text;
            RSCache_BufferInit(&chatbuf, (uint8_t*)op->_chat.data, op->_chat.length);
            text = wordpack_unpack(&chatbuf, op->_chat.length);
            if( text )
            {
                struct WorldEntity_Player* player =
                    World_EntityPoolGet(&world->entities.player, idx);
                RS_Chat_AddMessage(
                    &app->chat,
                    RS_CHAT_TYPE_PUBLIC,
                    player && player->name[0] ? player->name : "Player",
                    text);
                free(text);
            }
        }
        break;
    case PKT_PLAYER_INFO_OP_DAMAGE:
    case PKT_PLAYER_INFO_OP_DAMAGE2:
        World_PlayerAddHitmark(
            world,
            idx,
            op->_damage.damage_type,
            op->_damage.damage,
            op->_damage.health,
            op->_damage.total_health);
        break;
    case PKT_PLAYER_INFO_OP_SPOTANIM:
        World_PlayerSetSpotanim(
            world,
            idx,
            op->_spotanim.spotanim_id,
            op->_spotanim.height_delay >> 16,
            op->_spotanim.height_delay & 0xffff);
        break;
    case PKT_PLAYER_INFO_OP_EXACT_MOVE:
        /* Exact-move tile coords are classic-scene local. */
        World_PlayerSetExactMove(
            world,
            idx,
            app->scene_off_x + op->_exactmove.start_x,
            app->scene_off_z + op->_exactmove.start_z,
            app->scene_off_x + op->_exactmove.end_x,
            app->scene_off_z + op->_exactmove.end_z,
            op->_exactmove.start_cycle_delta,
            op->_exactmove.end_cycle_delta,
            op->_exactmove.facing);
        break;
    default:
        break;
    }
    return PLAYER_NEED_NONE;
}

static struct ToriRS_Task*
player_slot_cfg_task(struct Task_ExecPlayerInfo* self)
{
    int value = self->app_decoded.slots[self->cfg_i];
    if( value >= 0x100 && value < 0x200 )
        return CreateTask_IdkLoad(self->app->provider, value - 0x100);
    if( value >= 0x200 )
        return CreateTask_ObjLoad(self->app->provider, value - 0x200);
    return NULL;
}

static struct ToriRS_Task*
player_appearance_seq_task(struct Task_ExecPlayerInfo* self)
{
    int ids[7] = {
        self->app_decoded.readyanim,   self->app_decoded.turnanim,
        self->app_decoded.walkanim,    self->app_decoded.walkanim_b,
        self->app_decoded.walkanim_l,  self->app_decoded.walkanim_r,
        self->app_decoded.runanim,
    };
    int seq_id = ids[self->seq_i];
    if( seq_id < 0 )
        return NULL;
    return CreateTask_SequenceLoad(self->app->provider, self->app->scene, seq_id);
}

static int
Task_ExecPlayerInfo_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_ExecPlayerInfo* self = (struct Task_ExecPlayerInfo*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    self->ops = calloc(ENTITY_INFO_OPS_MAX, sizeof(*self->ops));
    assert(self->ops);
    self->op_count = pkt_player_info_reader_read(
        &self->reader, self->data, self->length, self->ops, ENTITY_INFO_OPS_MAX);

    /* Snapshot the tracked list: ADD_OLD indexes the PREVIOUS packet's
     * order while the walk rebuilds the live list in place. */
    self->old_count = app->esync.active_player_count;
    memcpy(
        self->old_list,
        app->esync.active_players,
        (size_t)self->old_count * sizeof(self->old_list[0]));
    app->esync.active_player_count = 0;
    self->cur_pid = -1;
    self->cur_world_idx = -1;
    self->cur_element_id = -1;

    for( self->op_i = 0; self->op_i < self->op_count; self->op_i++ )
    {
        int consumed = player_target_op(self, &self->ops[self->op_i]);

        if( self->need_ensure )
        {
            TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider));
            TASK_AWAITSELF_IF(
                CreateTask_SequenceLoad(app->provider, app->scene, PLAYER_DEFAULT_SEQ_READY));
            player_ensure_now(self);
        }

        if( !consumed )
        {
            int need = player_apply_op(self, &self->ops[self->op_i]);
            if( need == PLAYER_NEED_APPEARANCE )
            {
                for( self->cfg_i = 0; self->cfg_i < 12; self->cfg_i++ )
                {
                    TASK_AWAITSELF_IF(player_slot_cfg_task(self));
                }
                self->model_count = PlayerModel_CollectAppearanceModelIds(
                    app->provider,
                    self->app_decoded.slots,
                    self->app_decoded.gender,
                    self->model_ids,
                    (int)(sizeof(self->model_ids) / sizeof(self->model_ids[0])));
                for( self->model_i = 0; self->model_i < self->model_count; self->model_i++ )
                {
                    TASK_AWAITSELF_IF(
                        CreateTask_ModelLoad(app->provider, self->model_ids[self->model_i]));
                }
                for( self->seq_i = 0; self->seq_i < 7; self->seq_i++ )
                {
                    TASK_AWAITSELF_IF(player_appearance_seq_task(self));
                }
                if( self->cur_world_idx >= 0 )
                    App_WorldApplyPlayerAppearance(
                        app, self->cur_world_idx, self->cur_element_id, &self->app_decoded);
            }
            else if( need == PLAYER_NEED_SEQ )
            {
                TASK_AWAITSELF_IF(
                    self->pending_seq >= 0
                        ? CreateTask_SequenceLoad(app->provider, app->scene, self->pending_seq)
                        : NULL);
                if( self->cur_world_idx >= 0 )
                    World_PlayerSetPrimaryAnimation(
                        app->world, self->cur_world_idx, self->pending_seq, self->pending_delay);
            }
        }
    }

    app->need_redraw = 1;

    PT_END(&self->pt);
}

static void
Task_ExecPlayerInfo_Free(struct ToriRS_Task* base)
{
    struct Task_ExecPlayerInfo* self = (struct Task_ExecPlayerInfo*)base;
    if( self->ops )
    {
        pkt_player_info_ops_free(self->ops, self->op_count);
        free(self->ops);
    }
    free(self);
}

static struct ToriRS_TaskVTable Task_ExecPlayerInfo_VTable = {
    .run = Task_ExecPlayerInfo_Run,
    .free = Task_ExecPlayerInfo_Free,
};

struct ToriRS_Task*
CreateTask_ExecPlayerInfo(
    struct App* app,
    uint8_t const* data,
    int length)
{
    struct Task_ExecPlayerInfo* task;

    assert(app);
    if( !data || length <= 0 )
        return NULL;
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ExecPlayerInfo_VTable;
    strncpy(task->task.name, "ExecPlayerInfo", sizeof(task->task.name) - 1);
    task->app = app;
    task->data = data;
    task->length = length;
    PT_INIT(&task->pt);
    return &task->task;
}

/* ------------------------------------------------------------------ */
/* NPC_INFO                                                            */
/* ------------------------------------------------------------------ */

enum NpcOpNeed
{
    NPC_NEED_NONE = 0,
    NPC_NEED_SPAWN,
    NPC_NEED_SEQ,
    NPC_NEED_CHANGE_TYPE,
};

struct Task_ExecNpcInfo
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    uint8_t const* data;
    int length;

    struct PktNpcInfoReader reader;
    struct PktNpcInfoOp* ops;
    int op_count;
    int op_i;

    int old_list[RS_ENTITY_SYNC_MAX_NPCS];
    int old_count;

    int cur_slot;
    int cur_world_idx;
    int cur_element_id;

    int pending_npc_type;
    int model_i;
    int seq_i;
    int pending_seq;
    int pending_delay;
};

/* Reads npctype->models[model_i] fresh each call (config already loaded). */
static struct ToriRS_Task*
npc_model_task(struct Task_ExecNpcInfo* self)
{
    struct ToriRS_Npctype* npctype =
        CacheProvider_NpctypeGet(self->app->provider, self->pending_npc_type);
    if( !npctype || self->model_i >= npctype->models_count )
        return NULL;
    return CreateTask_ModelLoad(self->app->provider, npctype->models[self->model_i]);
}

static int
npc_model_count(struct Task_ExecNpcInfo* self)
{
    struct ToriRS_Npctype* npctype =
        CacheProvider_NpctypeGet(self->app->provider, self->pending_npc_type);
    return npctype ? npctype->models_count : 0;
}

static struct ToriRS_Task*
npc_idle_seq_task(struct Task_ExecNpcInfo* self)
{
    struct ToriRS_Npctype* npctype =
        CacheProvider_NpctypeGet(self->app->provider, self->pending_npc_type);
    int ids[5];
    int seq_id;
    if( !npctype )
        return NULL;
    ids[0] = npctype->readyanim;
    ids[1] = npctype->walkanim;
    ids[2] = npctype->walkanim_b;
    ids[3] = npctype->walkanim_r;
    ids[4] = npctype->walkanim_l;
    seq_id = ids[self->seq_i];
    if( seq_id < 0 )
        return NULL;
    return CreateTask_SequenceLoad(self->app->provider, self->app->scene, seq_id);
}

static void
npc_local_tile(
    struct Task_ExecNpcInfo* self,
    int* out_x,
    int* out_z,
    int* out_level)
{
    int world_idx;
    struct RS_EntitySync* esync = &self->app->esync;
    *out_x = self->app->scene_off_x + 52;
    *out_z = self->app->scene_off_z + 52;
    *out_level = 0;
    if( RS_EntitySync_FindPlayer(
            esync,
            esync->local_pid >= 0 ? esync->local_pid : ENTITY_INFO_LOCAL_SENTINEL,
            &world_idx,
            NULL) )
    {
        struct WorldEntity_Player* player =
            World_EntityPoolGet(&self->app->world->entities.player, world_idx);
        if( player )
        {
            *out_x = player->grid_position.x;
            *out_z = player->grid_position.z;
            *out_level = player->grid_position.level;
        }
    }
}

static void
npc_spawn_now(struct Task_ExecNpcInfo* self)
{
    struct App* app = self->app;
    int tile_x, tile_z, level;
    int idx;

    npc_local_tile(self, &tile_x, &tile_z, &level);
    idx = App_WorldSpawnSyncedNpc(app, self->pending_npc_type, tile_x, tile_z, level);
    if( idx < 0 )
        return;
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, idx);
        if( npc )
        {
            npc->server_slot = self->cur_slot;
            self->cur_element_id = npc->element_id;
        }
    }
    self->cur_world_idx = idx;
    RS_EntitySync_RegisterNpc(&app->esync, self->cur_slot, self->cur_element_id, idx);
    entity_debug_log("entity_sync: npc %d spawned (world idx %d)\n", self->cur_slot, idx);
}

/* Target/list bookkeeping. Returns 1 when the op was fully handled. */
static int
npc_target_op(
    struct Task_ExecNpcInfo* self,
    struct PktNpcInfoOp const* op)
{
    struct RS_EntitySync* esync = &self->app->esync;

    switch( op->kind )
    {
    case PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX:
        self->cur_slot = (int)op->_bitvalue < self->old_count ? self->old_list[op->_bitvalue] : -1;
        if( self->cur_slot >= 0 && esync->active_npc_count < RS_ENTITY_SYNC_MAX_NPCS )
            esync->active_npcs[esync->active_npc_count++] = self->cur_slot;
        break;
    case PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID:
        self->cur_slot = (int)op->_bitvalue;
        if( esync->active_npc_count < RS_ENTITY_SYNC_MAX_NPCS )
            esync->active_npcs[esync->active_npc_count++] = self->cur_slot;
        break;
    case PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX:
        self->cur_slot = (int)op->_bitvalue < esync->active_npc_count
                             ? esync->active_npcs[op->_bitvalue]
                             : -1;
        break;
    case PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX:
    {
        int slot = (int)op->_bitvalue < self->old_count ? self->old_list[op->_bitvalue] : -1;
        if( slot >= 0 )
            RS_EntitySync_RemoveNpc(esync, self->app->world, slot);
        self->cur_slot = -1;
        self->cur_world_idx = -1;
        self->cur_element_id = -1;
        return 1;
    }
    case PKT_NPC_INFO_OPBITS_COUNT_RESET:
        esync->active_npc_count = 0;
        return 1;
    case PKT_NPC_INFO_OPBITS_INFO:
        return 1;
    default:
        return 0;
    }

    self->cur_world_idx = -1;
    self->cur_element_id = -1;
    if( self->cur_slot >= 0 )
        RS_EntitySync_FindNpc(esync, self->cur_slot, &self->cur_world_idx, &self->cur_element_id);
    return 1;
}

static enum NpcOpNeed
npc_apply_op(
    struct Task_ExecNpcInfo* self,
    struct PktNpcInfoOp const* op)
{
    struct App* app = self->app;
    struct World* world = app->world;
    int idx = self->cur_world_idx;

    switch( op->kind )
    {
    case PKT_NPC_INFO_OPBITS_NPCTYPE:
        /* Arrives right after ADD_NEW; spawn once the config/models load. */
        self->pending_npc_type = (int)op->_bitvalue;
        if( self->cur_slot >= 0 && idx < 0 )
            return NPC_NEED_SPAWN;
        break;
    case PKT_NPC_INFO_OPBITS_WALKDIR:
        if( idx >= 0 )
            World_NpcPathPushStep(world, idx, WORLD_PATHSTEP_WALK, (int)op->_bitvalue);
        break;
    case PKT_NPC_INFO_OPBITS_RUNDIR:
        if( idx >= 0 )
            World_NpcPathPushStep(world, idx, WORLD_PATHSTEP_RUN, (int)op->_bitvalue);
        break;
    case PKT_NPC_INFO_OP_DELTA_XZ:
        if( idx >= 0 )
        {
            int lx, lz, llevel;
            npc_local_tile(self, &lx, &lz, &llevel);
            World_NpcPathJump(
                world, idx, op->_delta_xz.jump, lx + op->_delta_xz.dx, lz + op->_delta_xz.dz);
        }
        break;
    case PKT_NPC_INFO_OP_SEQUENCE:
        if( idx >= 0 )
        {
            self->pending_seq = op->_sequence.sequence_id;
            self->pending_delay = op->_sequence.delay;
            return NPC_NEED_SEQ;
        }
        break;
    case PKT_NPC_INFO_OP_FACE_ENTITY:
        if( idx >= 0 )
        {
            int entity_id = op->_face_entity.entity_id == 65535 ? -1 : op->_face_entity.entity_id;
            World_NpcFaceEntity(world, idx, entity_id);
        }
        break;
    case PKT_NPC_INFO_OP_FACE_COORD:
        /* Wire is absolute half-tiles ((tile << 1) + 1); store scene tiles. */
        if( idx >= 0 )
            World_NpcFaceCoord(
                world,
                idx,
                (op->_face_coord.x >> 1) - world->_base_tile_x,
                (op->_face_coord.z >> 1) - world->_base_tile_z);
        break;
    case PKT_NPC_INFO_OP_SAY:
        /* Overhead text rendering is a flagged follow-on; nothing to store. */
        break;
    case PKT_NPC_INFO_OP_DAMAGE:
        if( idx >= 0 )
            World_NpcAddHitmark(
                world,
                idx,
                op->_damage.damage_type,
                op->_damage.damage,
                op->_damage.health,
                op->_damage.total_health);
        break;
    case PKT_NPC_INFO_OP_CHANGE_TYPE:
        self->pending_npc_type = op->_change_type.npc_type;
        if( idx >= 0 )
            return NPC_NEED_CHANGE_TYPE;
        break;
    case PKT_NPC_INFO_OP_SPOTANIM:
        if( idx >= 0 )
            World_NpcSetSpotanim(
                world,
                idx,
                op->_spotanim.spotanim_id,
                op->_spotanim.height_delay >> 16,
                op->_spotanim.height_delay & 0xffff);
        break;
    default:
        break;
    }
    return NPC_NEED_NONE;
}

static int
Task_ExecNpcInfo_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_ExecNpcInfo* self = (struct Task_ExecNpcInfo*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    self->ops = calloc(ENTITY_INFO_OPS_MAX, sizeof(*self->ops));
    assert(self->ops);
    self->op_count = pkt_npc_info_reader_read(
        &self->reader, self->data, self->length, self->ops, ENTITY_INFO_OPS_MAX);

    self->old_count = app->esync.active_npc_count;
    memcpy(
        self->old_list,
        app->esync.active_npcs,
        (size_t)self->old_count * sizeof(self->old_list[0]));
    app->esync.active_npc_count = 0;
    self->cur_slot = -1;
    self->cur_world_idx = -1;
    self->cur_element_id = -1;

    for( self->op_i = 0; self->op_i < self->op_count; self->op_i++ )
    {
        if( !npc_target_op(self, &self->ops[self->op_i]) )
        {
            int need = npc_apply_op(self, &self->ops[self->op_i]);
            if( need == NPC_NEED_SPAWN || need == NPC_NEED_CHANGE_TYPE )
            {
                TASK_AWAITSELF_IF(CreateTask_NpcLoad(app->provider, self->pending_npc_type));
                for( self->model_i = 0; self->model_i < npc_model_count(self); self->model_i++ )
                {
                    TASK_AWAITSELF_IF(npc_model_task(self));
                }
                for( self->seq_i = 0; self->seq_i < 5; self->seq_i++ )
                {
                    TASK_AWAITSELF_IF(npc_idle_seq_task(self));
                }
                if( self->cur_world_idx < 0 )
                    npc_spawn_now(self);
                else
                    App_WorldApplyNpcType(
                        app, self->cur_world_idx, self->cur_element_id, self->pending_npc_type);
            }
            else if( need == NPC_NEED_SEQ )
            {
                TASK_AWAITSELF_IF(
                    self->pending_seq >= 0
                        ? CreateTask_SequenceLoad(app->provider, app->scene, self->pending_seq)
                        : NULL);
                if( self->cur_world_idx >= 0 )
                    World_NpcSetPrimaryAnimation(
                        app->world, self->cur_world_idx, self->pending_seq, self->pending_delay);
            }
        }
    }

    app->need_redraw = 1;

    PT_END(&self->pt);
}

static void
Task_ExecNpcInfo_Free(struct ToriRS_Task* base)
{
    struct Task_ExecNpcInfo* self = (struct Task_ExecNpcInfo*)base;
    if( self->ops )
    {
        pkt_npc_info_ops_free(self->ops, self->op_count);
        free(self->ops);
    }
    free(self);
}

static struct ToriRS_TaskVTable Task_ExecNpcInfo_VTable = {
    .run = Task_ExecNpcInfo_Run,
    .free = Task_ExecNpcInfo_Free,
};

struct ToriRS_Task*
CreateTask_ExecNpcInfo(
    struct App* app,
    uint8_t const* data,
    int length)
{
    struct Task_ExecNpcInfo* task;

    assert(app);
    if( !data || length <= 0 )
        return NULL;
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ExecNpcInfo_VTable;
    strncpy(task->task.name, "ExecNpcInfo", sizeof(task->task.name) - 1);
    task->app = app;
    task->data = data;
    task->length = length;
    PT_INIT(&task->pt);
    return &task->task;
}
