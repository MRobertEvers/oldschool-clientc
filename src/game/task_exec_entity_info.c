#include "task_exec_entity_info.h"

#include "app.h"
#include "engine/entity_model_build.h"
#include "engine/player_appearance.h"
#include "game/rs_chat.h"
#include "game/rs_entity_sync.h"
#include "net/net.h"
#include "net/rev/packets/pkt_npc_info.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "net/rev/packets/pkt_player_info.h"
#include "net/wordpack.h"
#include "toridraw_animation.h"
#include "toridraw_scene.h"
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
    int held_vals[2]; /* replaceheld left/right, as canonical appearance slots */
};

static int
local_player_pid(struct RS_EntitySync const* esync)
{
    return esync->local_pid >= 0 ? esync->local_pid : ENTITY_INFO_LOCAL_SENTINEL;
}

/* Local player's scene tile (fallback: classic scene center) for relative
 * adds. Server-local coords are classic-scene relative ((zone-6)*8), which
 * matches our scene base after REBUILD_NORMAL.
 *
 * The tile is routeX[0]/routeZ[0], NOT grid_position: the reference bases
 * every relative add on `localPlayer.routeX[0] + dx` (Client.ts:8034, 8369),
 * which is the player's *authoritative destination* tile — updated the moment
 * a walk step is pushed. grid_position only catches up when the draw position
 * arrives, so mid-walk it lags by one or more tiles and every NPC/player
 * added or teleported during that window landed on the wrong grid square
 * while the local player looked correct. */
static void
player_local_tile(
    struct Task_ExecPlayerInfo* self,
    int* out_x,
    int* out_z,
    int* out_level)
{
    int world_idx;
    *out_x = 52;
    *out_z = 52;
    *out_level = 0;
    if( self->app->npc_update_origin_valid )
    {
        *out_x = self->app->npc_update_origin_x;
        *out_z = self->app->npc_update_origin_z;
    }
    if( RS_EntitySync_FindPlayer(&self->app->esync, local_player_pid(&self->app->esync), &world_idx, NULL) )
    {
        struct WorldEntity_Player* player =
            World_EntityPoolGet(&self->app->world->entities.player, world_idx);
        if( player )
        {
            if( !self->app->npc_update_origin_valid )
            {
                *out_x = player->pathing.route_x[0];
                *out_z = player->pathing.route_z[0];
            }
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
    case PKT_PLAYER_INFO_OP_REMOVE_PLAYER_PID:
        /* The v5 form of the drop: the payload IS the pid, because that stream
         * addresses players by their slot rather than by a position in the
         * previous packet's tracked list. */
        RS_EntitySync_RemovePlayer(esync, self->app->world, (int)op->_bitvalue);
        self->cur_pid = -1;
        self->cur_world_idx = -1;
        self->cur_element_id = -1;
        return 1;
    case PKT_PLAYER_INFO_OPBITS_COUNT_RESET:
    {
        /* Client-TS getPlayerPosOldVis: when the wire 8-bit count is less than
         * the previous tracked length, indices [count, oldCount) are removed.
         * Plane-change zeros tracked_count and sends count=0 — without this,
         * old-floor players linger in the world pool (drawn on minusedlevel,
         * rejected by pick). */
        int keep = (int)op->_bitvalue;
        for( int i = keep; i < self->old_count; i++ )
            RS_EntitySync_RemovePlayer(esync, self->app->world, self->old_list[i]);
        esync->active_player_count = 0;
        return 1;
    }
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
            op->_local_xz_level.x,
            op->_local_xz_level.z);
        if( player )
            player->grid_position.level = op->_local_xz_level.level;
        break;
    }
    case PKT_PLAYER_INFO_OP_ABS_XZLEVEL:
    {
        /* The v5 stream's world coordinate, brought into the scene the same way
         * the projectile and map-flag decoders do: the scene's south-west
         * corner is the rebuild's centre zone less the six zones of margin the
         * client keeps on each side. */
        struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
        int origin_x = (app->rebuild_zone_x - 6) * 8;
        int origin_z = (app->rebuild_zone_z - 6) * 8;
        int next_x = op->_local_xz_level.x - origin_x;
        int next_z = op->_local_xz_level.z - origin_z;
        int step_type =
            op->_local_xz_level.has_move_speed &&
                    op->_local_xz_level.move_speed == PKT_PLAYER_TRAVERSAL_RUN
                ? WORLD_PATHSTEP_RUN
                : WORLD_PATHSTEP_WALK;
        struct CollisionMap* collision = NULL;

        if( player && player->grid_position.level == op->_local_xz_level.level &&
            op->_local_xz_level.level >= 0 && op->_local_xz_level.level < COLLISION_LEVELS )
            collision = world->collision_maps[op->_local_xz_level.level];

        World_PlayerPathJumpCollisionAware(
            world,
            idx,
            collision,
            op->_local_xz_level.jump,
            next_x,
            next_z,
            step_type);
        if( player && op->_local_xz_level.has_move_speed )
        {
            entity_debug_log(
                "entity_sync: player traversal=%d run=%d\n",
                op->_local_xz_level.move_speed,
                player->pathing.route_run[0]);
        }
        if( player )
            player->grid_position.level = op->_local_xz_level.level;
        entity_debug_log(
            "entity_sync: abs move to scene %d,%d\n",
            next_x,
            next_z);
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
    {
        /* The block's layout is the revision's, not this layer's: a rev whose
         * appearance shape moved states its own reader
         * (GameProtoRevTable.appearance_decode) and NULL means the classic
         * block. Either way what lands in `app_decoded` is the same canonical
         * appearance — see pkt_player_appearance.h. */
        struct GameProtoRevTable const* rev = app->net ? app->net->rev : NULL;
        int decoded =
            rev && rev->appearance_decode
                ? rev->appearance_decode(
                      op->_appearance.appearance, op->_appearance.len, &self->app_decoded)
                : PktPlayerAppearance_Decode(
                      &self->app_decoded, op->_appearance.appearance, op->_appearance.len);
        if( decoded )
            return PLAYER_NEED_APPEARANCE;
        break;
    }
    case PKT_PLAYER_INFO_OP_SEQUENCE:
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "player_info: sequence idx=%d id=%d delay=%d\n",
                idx,
                op->_sequence.sequence_id,
                op->_sequence.delay);
        self->pending_seq = op->_sequence.sequence_id;
        self->pending_delay = op->_sequence.delay;
        return PLAYER_NEED_SEQ;
    case PKT_PLAYER_INFO_OP_FACE_ENTITY:
    {
        int entity_id = op->_face_entity.entity_id == 65535 ? -1 : op->_face_entity.entity_id;
        if( op->_face_entity.modern )
            World_PlayerBeginModernFacing(world, idx, op->_face_entity.movement_mode);
        World_PlayerFaceEntityDetailed(
            world,
            idx,
            entity_id,
            op->_face_entity.has_fallback_angle ? op->_face_entity.fallback_angle : -1,
            op->_face_entity.instant);
        break;
    }
    case PKT_PLAYER_INFO_OP_FACE_COORD:
        /* Raw wire half-tiles: World_Cycle converts them (the reference keeps
         * faceSquareX/Z absolute and subtracts mapBuildBase at use time, so
         * 0,0 stays the "no target" sentinel). */
        if( op->_face_coord.modern )
            World_PlayerBeginModernFacing(world, idx, op->_face_coord.movement_mode);
        World_PlayerFaceCoord(world, idx, op->_face_coord.x, op->_face_coord.z);
        if( op->_face_coord.instant )
        {
            struct WorldEntity_Player* player =
                World_EntityPoolGet(&world->entities.player, idx);
            player->facing.instant = true;
        }
        entity_debug_log(
            "entity_sync: player faces coord %d,%d\n", op->_face_coord.x, op->_face_coord.z);
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
            /* Forced chat draws overhead in plain yellow (colour/effect 0). */
            World_PlayerSetChat(world, idx, op->_say.text, 0, 0);
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
                /* colourEffect: high byte = chatColour, low byte = chatEffect
                 * (reference Client.ts:8164). */
                World_PlayerSetChat(
                    world, idx, text, op->_chat.colour_effect >> 8,
                    op->_chat.colour_effect & 0xff);
                free(text);
            }
        }
        break;
    case PKT_PLAYER_INFO_OP_DAMAGE:
    case PKT_PLAYER_INFO_OP_DAMAGE2:
        World_PlayerAddHitmarkTimed(
            world,
            idx,
            op->_damage.damage_type,
            op->_damage.damage,
            op->_damage.health,
            op->_damage.total_health,
            op->_damage.delay,
            op->_damage.slots);
        break;
    case PKT_PLAYER_INFO_OP_HEADBAR:
        if( op->_headbar.remove )
            World_PlayerClearHealthbar(world, idx);
        else
        {
            int width = op->_headbar.start_fill;
            if( op->_headbar.end_fill > width )
                width = op->_headbar.end_fill;
            World_PlayerSetHealthbar(world, idx, op->_headbar.end_fill, width);
        }
        break;
    case PKT_PLAYER_INFO_OP_SPOTANIM:
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "player_info: spotanim idx=%d id=%d height=%d delay=%d\n",
                idx,
                op->_spotanim.spotanim_id,
                op->_spotanim.height_delay >> 16,
                op->_spotanim.height_delay & 0xffff);
        World_PlayerSetSpotanim(
            world,
            idx,
            op->_spotanim.spotanim_id,
            op->_spotanim.height_delay >> 16,
            op->_spotanim.height_delay & 0xffff);
        break;
    case PKT_PLAYER_INFO_OP_EXACT_MOVE:
    {
        int sx = op->_exactmove.start_x;
        int sz = op->_exactmove.start_z;
        int ex = op->_exactmove.end_x;
        int ez = op->_exactmove.end_z;

        if( op->_exactmove.relative )
        {
            struct WorldEntity_Player* player =
                World_EntityPoolGet(&world->entities.player, idx);
            sx += player->pathing.route_x[0];
            sz += player->pathing.route_z[0];
            ex += player->pathing.route_x[0];
            ez += player->pathing.route_z[0];
        }
        /* The client half of MOCK230_EXT_DEBUG's exact-move line. Without the
         * pair, "the obstacle did not glide" cannot be split into "the server
         * never set the mask" and "the client dropped the block". */
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(stderr,
                    "exactmove player idx=%d (%d,%d)->(%d,%d) cycles %d..%d "
                    "facing=%d yaw=%d\n",
                    idx, sx, sz, ex, ez, op->_exactmove.start_cycle_delta,
                    op->_exactmove.end_cycle_delta, op->_exactmove.facing,
                    (int)op->_exactmove.facing_is_yaw);
        World_PlayerSetExactMoveDetailed(
            world,
            idx,
            sx,
            sz,
            ex,
            ez,
            op->_exactmove.start_cycle_delta,
            op->_exactmove.end_cycle_delta,
            op->_exactmove.facing,
            op->_exactmove.facing_is_yaw);
        break;
    }
    case PKT_PLAYER_INFO_OP_FACE_ANGLE:
        if( op->_face_angle.modern )
            World_PlayerBeginModernFacing(world, idx, op->_face_angle.movement_mode);
        World_PlayerFaceAngle(world, idx, op->_face_angle.angle, op->_face_angle.instant);
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
    switch( Appearance_SlotKind(value) )
    {
    case APPEARANCE_SLOT_KIT:
        return CreateTask_IdkLoad(self->app->provider, Appearance_SlotKit(value));
    case APPEARANCE_SLOT_OBJ:
        return CreateTask_ObjLoad(self->app->provider, Appearance_SlotObj(value));
    case APPEARANCE_SLOT_EMPTY:
    default:
        return NULL;
    }
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
    /* A revision whose stream is a different CODEC states its own reader
     * (GameProtoRevTable.player_info_read); NULL means the classic bitstream.
     * Both produce the same op array, so nothing below this line branches. */
    {
        struct GameProtoRevTable const* rev = app->net ? app->net->rev : NULL;

        self->op_count =
            rev && rev->player_info_read
                ? rev->player_info_read(
                      self->data, self->length, self->ops, ENTITY_INFO_OPS_MAX)
                : pkt_player_info_reader_read(
                      &self->reader, self->data, self->length, self->ops, ENTITY_INFO_OPS_MAX);
    }

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

                /* Held-item replacement (reference ClientPlayer.getTempModel2 via
                 * SeqType.replaceheldleft/right, opcodes 6/7): a woodcutting/
                 * mining-style seq swaps a worn item for an obj that is NOT part
                 * of the player's appearance, so its config + wear models were
                 * never fetched by the APPEARANCE path. Ensure them now — the
                 * per-frame swap (app_world_apply_player_held_items) builds the
                 * player model synchronously and silently drops any wear model
                 * that is not resident, so the swapped item would otherwise never
                 * appear. The reference defers the model build until
                 * ObjType.checkWearModel loads; we pre-load instead. Loading is
                 * idempotent, so re-issuing on every SEQ is cheap. */
                {
                    struct ToriDraw_Animation* prim =
                        self->pending_seq >= 0
                            ? ToriDraw_SceneAnimationGet(app->scene, self->pending_seq)
                            : NULL;
                    self->held_vals[0] =
                        Appearance_FromCacheValue(prim ? prim->replaceheldleft : -1);
                    self->held_vals[1] =
                        Appearance_FromCacheValue(prim ? prim->replaceheldright : -1);
                }
                /* Obj configs first — the seq's replaceheld values are appearance
                 * slots, so only an obj-range one names an obj; anything lower
                 * hides the item and needs no model. */
                for( self->cfg_i = 0; self->cfg_i < 2; self->cfg_i++ )
                    TASK_AWAITSELF_IF(
                        Appearance_SlotKind(self->held_vals[self->cfg_i]) ==
                                APPEARANCE_SLOT_OBJ
                            ? CreateTask_ObjLoad(
                                  app->provider,
                                  Appearance_SlotObj(self->held_vals[self->cfg_i]))
                            : NULL);
                /* Then their gendered wear models (slot 3 = right hand, slot 5 =
                 * left hand — same appearance encoding the swap feeds the build). */
                {
                    struct WorldEntity_Player* held_player = World_EntityPoolGet(
                        &app->world->entities.player, self->cur_world_idx);
                    int held_slots[12];
                    for( int k = 0; k < 12; k++ )
                        held_slots[k] = -1;
                    held_slots[3] =
                        Appearance_SlotKind(self->held_vals[1]) == APPEARANCE_SLOT_OBJ
                            ? self->held_vals[1]
                            : -1;
                    held_slots[5] =
                        Appearance_SlotKind(self->held_vals[0]) == APPEARANCE_SLOT_OBJ
                            ? self->held_vals[0]
                            : -1;
                    self->model_count = PlayerModel_CollectAppearanceModelIds(
                        app->provider,
                        held_slots,
                        held_player ? held_player->gender : 0,
                        self->model_ids,
                        (int)(sizeof(self->model_ids) / sizeof(self->model_ids[0])));
                }
                for( self->model_i = 0; self->model_i < self->model_count; self->model_i++ )
                    TASK_AWAITSELF_IF(
                        CreateTask_ModelLoad(app->provider, self->model_ids[self->model_i]));

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
    *out_x = 52;
    *out_z = 52;
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
            /* routeX[0], not grid_position — see player_local_tile. */
            *out_x = player->pathing.route_x[0];
            *out_z = player->pathing.route_z[0];
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
    {
        /* Client-TS getNpcPosOldVis: when the wire 8-bit count is less than
         * the previous tracked length, indices [count, oldCount) are removed.
         * Plane-change zeros tracked_count and sends count=0 — without this,
         * old-floor NPCs linger in the world pool (drawn on minusedlevel,
         * rejected by pick → visible but Walk-here only). */
        int keep = (int)op->_bitvalue;
        for( int i = keep; i < self->old_count; i++ )
            RS_EntitySync_RemoveNpc(esync, self->app->world, self->old_list[i]);
        esync->active_npc_count = 0;
        return 1;
    }
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
            if( op->_face_entity.modern )
                World_NpcBeginModernFacing(world, idx, op->_face_entity.movement_mode);
            World_NpcFaceEntityDetailed(
                world,
                idx,
                entity_id,
                op->_face_entity.has_fallback_angle ? op->_face_entity.fallback_angle : -1,
                op->_face_entity.instant);
            entity_debug_log("entity_sync: npc %d faces entity %d\n", self->cur_slot, entity_id);
        }
        break;
    case PKT_NPC_INFO_OP_FACE_COORD:
        /* Raw wire half-tiles — see the player branch. */
        if( idx >= 0 )
        {
            if( op->_face_coord.modern )
                World_NpcBeginModernFacing(world, idx, op->_face_coord.movement_mode);
            World_NpcFaceCoord(world, idx, op->_face_coord.x, op->_face_coord.z);
            if( op->_face_coord.instant )
            {
                struct WorldEntity_NPC* npc =
                    World_EntityPoolGet(&world->entities.npc, idx);
                npc->facing.instant = true;
            }
            entity_debug_log(
                "entity_sync: npc faces coord %d,%d\n", op->_face_coord.x, op->_face_coord.z);
        }
        break;
    case PKT_NPC_INFO_OP_SAY:
        /* NPC forced chat: plain yellow overhead (colour/effect 0). */
        if( idx >= 0 && op->_say.text )
            World_NpcSetChat(world, idx, op->_say.text, 0, 0);
        break;
    case PKT_NPC_INFO_OP_DAMAGE:
        if( idx >= 0 )
            World_NpcAddHitmarkTimed(
                world,
                idx,
                op->_damage.damage_type,
                op->_damage.damage,
                op->_damage.health,
                op->_damage.total_health,
                op->_damage.delay,
                op->_damage.slots);
        break;
    case PKT_NPC_INFO_OP_HEADBAR:
        if( idx >= 0 )
        {
            if( op->_headbar.remove )
                World_NpcClearHealthbar(world, idx);
            else
            {
                int width = op->_headbar.start_fill;
                if( op->_headbar.end_fill > width )
                    width = op->_headbar.end_fill;
                World_NpcSetHealthbar(world, idx, op->_headbar.end_fill, width);
            }
        }
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
    case PKT_NPC_INFO_OP_EXACT_MOVE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolGet(&world->entities.npc, idx);
            int sx = op->_exactmove.start_x;
            int sz = op->_exactmove.start_z;
            int ex = op->_exactmove.end_x;
            int ez = op->_exactmove.end_z;

            if( op->_exactmove.relative )
            {
                sx += npc->pathing.route_x[0];
                sz += npc->pathing.route_z[0];
                ex += npc->pathing.route_x[0];
                ez += npc->pathing.route_z[0];
            }
            World_NpcSetExactMoveDetailed(
                world,
                idx,
                sx,
                sz,
                ex,
                ez,
                op->_exactmove.start_cycle_delta,
                op->_exactmove.end_cycle_delta,
                op->_exactmove.facing,
                op->_exactmove.facing_is_yaw);
        }
        break;
    case PKT_NPC_INFO_OP_FACE_ANGLE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolGet(&world->entities.npc, idx);
            if( op->_face_angle.modern )
                World_NpcBeginModernFacing(world, idx, op->_face_angle.movement_mode);
            int angle = op->_face_angle.spawn && npc->facing.turn_speed == 0
                            ? 0
                            : op->_face_angle.angle;
            World_NpcFaceAngle(world, idx, angle, op->_face_angle.instant);
        }
        break;
    case PKT_NPC_INFO_OP_SPAWN_CYCLE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolGet(&world->entities.npc, idx);
            npc->spawn_cycle = (uint32_t)op->_bitvalue;
        }
        break;
    case PKT_NPC_INFO_OP_VISIBLE_OPS:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolGet(&world->entities.npc, idx);
            npc->visible_ops = (uint8_t)op->_bitvalue;
        }
        break;
    case PKT_NPC_INFO_OP_NAME_CHANGE:
        if( idx >= 0 && op->_name_change.name )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolGet(&world->entities.npc, idx);
            strncpy(npc->name, op->_name_change.name, sizeof(npc->name) - 1);
            npc->name[sizeof(npc->name) - 1] = '\0';
        }
        break;
    case PKT_NPC_INFO_OP_LEVEL_CHANGE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolGet(&world->entities.npc, idx);
            npc->combat_level = (int)(uint32_t)op->_bitvalue;
        }
        break;
    case PKT_NPC_INFO_OP_BAS_CHANGE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolGet(&world->entities.npc, idx);
            uint32_t mask = op->_bas_change.mask;

            if( mask & (1u << 0) ) npc->idle_animations.turnanim = op->_bas_change.turnanim;
            if( mask & (1u << 2) ) npc->idle_animations.walkanim = op->_bas_change.walkanim;
            if( mask & (1u << 3) ) npc->idle_animations.walkanim_b = op->_bas_change.walkanim_b;
            if( mask & (1u << 4) ) npc->idle_animations.walkanim_l = op->_bas_change.walkanim_l;
            if( mask & (1u << 5) ) npc->idle_animations.walkanim_r = op->_bas_change.walkanim_r;
            if( mask & (1u << 6) ) npc->idle_animations.runanim = op->_bas_change.runanim;
            if( mask & (1u << 14) ) npc->idle_animations.readyanim = op->_bas_change.readyanim;
        }
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
    /* The new-npc record's slot and type widths are revision state, not
     * constants — see GameProtoRevTable.npc_type_bits. Zero widths (no net
     * layer, as in the unit tests) take the classic defaults. */
    pkt_npc_info_reader_init(
        &self->reader,
        app->net && app->net->rev ? app->net->rev->npc_slot_bits : 0,
        app->net && app->net->rev ? app->net->rev->npc_type_bits : 0);
    {
        struct GameProtoRevTable const* rev = app->net ? app->net->rev : NULL;

        self->op_count =
            rev && rev->npc_info_read
                ? rev->npc_info_read(self->data, self->length, self->ops, ENTITY_INFO_OPS_MAX)
                : pkt_npc_info_reader_read(
                      &self->reader, self->data, self->length, self->ops, ENTITY_INFO_OPS_MAX);
    }

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
