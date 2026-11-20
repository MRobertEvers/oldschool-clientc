#ifndef SRC_GAME_TASK_ENTITY_ASSETS_H
#define SRC_GAME_TASK_ENTITY_ASSETS_H

#include "asyncio.h"
#include "game/rs_pending.h"

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
 * An npc load applies nothing: each npc waiting on it has its own parallel
 * task (App_EntityNpcBodyWant), keyed on the npc's server slot and spawn
 * serial -- never a world index or scene element cached at queue time, both
 * recycled across yields -- which mounts the body when the load ends.
 *
 * The player loads apply nothing. A player's body is derived from its
 * appearance (and the held-item override of its playing seq) every frame by
 * app_world_reconcile_player_body, which rebuilds only when that body is
 * wholly resident and keeps the last whole one until then. So a player load
 * is a plain fetch: it names no player, and a completion the world has moved
 * past needs no detecting -- the reconcile reads the entity as it is now.
 */

struct App;

/** Is every model of `npc_id` and every stance sequence it plays under the
 * shell `base_npc_id` (-1: none) resident already? */
int
EntityAssets_NpcBodyResident(
    struct App* app,
    int npc_id,
    int base_npc_id);

/** Is every config and model of the appearance `slots` (for `gender`) and
 * every sequence in `seq_ids` resident? */
int
EntityAssets_PlayerBodyResident(
    struct App* app,
    int const slots[12],
    int gender,
    int const* seq_ids,
    int seq_count);

/* An npc's body (ASSET_KIND_NPC_BODY, id = the drawn type, variant = the
 * shell) is LOADED here and APPLIED by a parallel task per npc
 * (App_EntityNpcBodyWant, app/app_tasks_entities.c): one load per (type,
 * shell) in flight however many npcs wait on it. */

/** Register the loaders of the composite kinds this unit owns. */
void
EntityAssets_RegisterLoaders(struct App* app);

/* The pending-index kind of a player's body fetch: the LAST id of the
 * entities family's range (game/rs_pending.h), so app/app_tasks_entities.c's
 * kinds, counted up from the first, never meet it. */
#define ENTITY_ASSETS_KIND_PLAYER_BODY RS_PENDING_KINDS_ENTITIES_LAST

/* The most stances an appearance's fetch names (ready, turn, walk x4, run). */
#define ENTITY_ASSETS_PLAYER_SEQS_MAX 7

/**
 * Want the body of the appearance `slots` (for `gender`) with the stance
 * sequences `seq_ids`: the configs, the models they name, the stances. Every
 * read goes through the assets table (CONFIG, MODEL, SEQUENCE), so two
 * fetches for bodies that share parts read each part once; and the fetch
 * itself is ONE parallel task per distinct body in flight, listed in the
 * pending index (ENTITY_ASSETS_KIND_PLAYER_BODY, EntityAssets_PlayerBodyKey),
 * so an appearance block naming a body already on its way adds nothing.
 * Nothing at all when the body is resident. The arrays are copied. Returns 1
 * when it added a fetch.
 */
int
EntityAssets_WantPlayerBody(
    struct App* app,
    int const slots[12],
    int gender,
    int const* seq_ids,
    int seq_count);

/** The key a body fetch is listed under: a hash of the slots, the gender and
 *  the stances, generation 0 (a found fetch is compared whole, so two bodies
 *  whose hashes meet are still told apart). */
struct RS_PendingKey
EntityAssets_PlayerBodyKey(
    int const slots[12],
    int gender,
    int const* seq_ids,
    int seq_count);

/** Body fetches pending (listed, not cancelled). */
int
EntityAssets_PlayerBodyPending(struct App const* app);

/**
 * A transmogged player's body (ASSET_KIND_PLAYER_TRANSMOG): npc `npc_id`'s
 * config chain (multinpc rungs included), then the models and stances of the
 * type it selects. A plain fetch like a player body -- the per-frame
 * reconcile mounts it. One load per shell in flight however many appearance
 * blocks name it, and none at all once what it selects is resident.
 */
void
EntityAssets_WantPlayerTransmog(
    struct App* app,
    int npc_id);

/**
 * What sequence `seq_id` swaps into a player's hands (ASSET_KIND_PLAYER_HELD,
 * variant = `gender`): the sequence, then the obj configs and wear models of
 * its replaceheldleft/right. One load per (seq, gender) in flight, and none
 * once they are resident.
 */
void
EntityAssets_WantPlayerHeld(
    struct App* app,
    int seq_id,
    int gender);

/** EntityAssets_WantPlayerHeld for the player with `server_pid`, gendered as
 *  that player is now (0 when it has no entity). */
void
EntityAssets_WantPlayerHeldForPid(
    struct App* app,
    int server_pid,
    int seq_id);

#endif
