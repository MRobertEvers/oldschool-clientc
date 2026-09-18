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
 * -- the entity exists, with an empty placeholder body when the models are
 * not resident -- and queues one of these on the ASSET runner, which loads
 * what is missing and then re-applies the type or appearance.
 *
 * Every task re-resolves its entities when the loads land, never by a world
 * index or scene element cached at queue time: both are recycled across
 * yields, and a stale pair would dress somebody else in this entity's body.
 * An npc load finds its npcs by walking the pool for the (base, resolved)
 * type it loaded; a player load finds its player by the server's pid and a
 * serial on the appearance it was queued for, so a completion the world
 * has moved past is a no-op.
 */

struct App;
struct PktPlayerAppearance;

/** Is every model and stance sequence of `npc_id` resident already? */
int
EntityAssets_NpcBodyResident(
    struct App* app,
    int npc_id);

/** Is every model in `model_ids` and every sequence in `seq_ids` resident? */
int
EntityAssets_PlayerBodyResident(
    struct App* app,
    int const* model_ids,
    int model_count,
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
 * Load the appearance's models and stance sequences, then re-apply the
 * appearance to the player with `server_pid` if its appearance serial is
 * still `serial`. The appearance and the ids are copied.
 */
struct ToriRS_Task*
CreateTask_PlayerBodyLand(
    struct App* app,
    int server_pid,
    unsigned serial,
    struct PktPlayerAppearance const* appearance,
    int const* model_ids,
    int model_count);

/**
 * Load sequence `seq_id`, then the obj configs and wear models it swaps into
 * the player's hands (replaceheldleft/right), then mark the player with
 * `server_pid` for a held-item rebuild.
 */
struct ToriRS_Task*
CreateTask_PlayerHeldLand(
    struct App* app,
    int server_pid,
    int seq_id);

#endif
