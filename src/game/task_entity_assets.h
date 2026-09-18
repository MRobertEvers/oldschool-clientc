#ifndef SRC_GAME_TASK_ENTITY_ASSETS_H
#define SRC_GAME_TASK_ENTITY_ASSETS_H

#include "asyncio.h"

/*
 * The asset loads an entity packet used to await inline, moved off the packet
 * pipeline.
 *
 * NPC_INFO and PLAYER_INFO are applied by the exec runner, which is strict
 * FIFO because the server chose the order of its packets. The handlers used
 * to await each spawned npc's body -- config, models, five stance sequences,
 * each sequence its frames and framemap -- before moving to the next op, and
 * while that await was parked so was every packet behind it: other players
 * stopped moving, chat stopped arriving, the server tick's fence stopped. On
 * a streamed cache that is three or four round trips per new npc type, in a
 * line, per packet. The renderer kept drawing a world that had stopped
 * receiving time.
 *
 * The reference client never waits there: it applies the packet and fetches
 * the model lazily at draw time, drawing nothing for the entity until it is
 * in. These tasks are that shape. The packet handler applies the op at once
 * and queues one of these on the ASSET runner.
 *
 * An npc load re-applies the type when it lands: it finds its npcs by
 * walking the pool for the (base, resolved) type it loaded, never by a world
 * index or scene element cached at queue time -- both are recycled across
 * yields, and a stale pair would dress somebody else in this entity's body.
 *
 * The player loads apply nothing. A player's body is derived from its
 * appearance (and the held-item override of its playing seq) every frame by
 * app_world_reconcile_player_body, which rebuilds only when that body is
 * wholly resident and keeps the last whole one until then. So a player load
 * is a plain fetch: it names no player, and a completion the world has moved
 * past needs no detecting -- the reconcile reads the entity as it is now.
 */

struct App;

/** Is every model and stance sequence of `npc_id` resident already? */
int
EntityAssets_NpcBodyResident(
    struct App* app,
    int npc_id);

/** Is every config and model of the appearance `slots` (for `gender`) and
 * every sequence in `seq_ids` resident? */
int
EntityAssets_PlayerBodyResident(
    struct App* app,
    int const slots[12],
    int gender,
    int const* seq_ids,
    int seq_count);

/**
 * Load npc `npc_id`'s body and stances, then re-apply the type to EVERY live
 * npc that is still that type (base `base_npc_id`, resolved `npc_id`).
 *
 * One per type in flight: a packet that spawns a crowd of one type queues
 * one load, not one per npc, and the completion walks the pool. NULL when
 * that type's load is already on its way.
 */
struct ToriRS_Task*
CreateTask_NpcBodyLand(
    struct App* app,
    int base_npc_id,
    int npc_id);

/**
 * Fetch the configs of the appearance `slots`, the models they name for
 * `gender`, and the stance sequences `seq_ids`. The arrays are copied.
 */
struct ToriRS_Task*
CreateTask_PlayerBodyLand(
    struct App* app,
    int const slots[12],
    int gender,
    int const* seq_ids,
    int seq_count);

/**
 * Load sequence `seq_id`, then the obj configs and wear models it swaps into
 * the player's hands (replaceheldleft/right), gendered as the player with
 * `server_pid` is when the configs are in.
 */
struct ToriRS_Task*
CreateTask_PlayerHeldLand(
    struct App* app,
    int server_pid,
    int seq_id);

#endif
