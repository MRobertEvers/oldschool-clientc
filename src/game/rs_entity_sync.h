#ifndef SRC_GAME_RS_ENTITY_SYNC_H
#define SRC_GAME_RS_ENTITY_SYNC_H

/*
 * Protocol-side entity bookkeeping for PLAYER_INFO / NPC_INFO (v0
 * gameproto_exec's active lists + the World entity registry): the server
 * addresses entities by its own slot numbers (players 0..2046, npcs
 * 0..8190) and, after the first ADD, only by position in the tracked list.
 * This maps those slots to World pool indices / scene elements.
 */

#include "world/entity_registry.h"

struct World;

#define RS_ENTITY_SYNC_MAX_PLAYERS 2048
#define RS_ENTITY_SYNC_MAX_NPCS 2048

struct RS_EntitySync
{
    /** Server player slot of the local player (UPDATE_PID); -1 until known. */
    int local_pid;

    /*
     * Tracked-list order the server addresses by (values = server slots).
     *
     * ---------------------------------------------------------------------
     * INVARIANT: these lists are POSITIONAL, and the DECODER defines the
     * positions. Exactly one entry must be appended per npc/player the decoder
     * counted, in the decoder's order -- including entries this side cannot
     * resolve, which are recorded as -1.
     * ---------------------------------------------------------------------
     *
     * Extended info (appearance, chat, hitsplats, SEQUENCE, TRANSFORMATION,
     * SPOTANIM, ...) is NOT addressed by server slot. It is addressed by the
     * entity's index in the list both sides rebuild during this packet, and
     * PKT_*_INFO_OP_SET_*_OPBITS_IDX resolves it as `active_*[idx]`. The
     * decoders advance that index unconditionally for every entry they keep or
     * add (`list_idx++` in osrs239_entity_info.c, `new_idx++` in
     * pkt_npc_info.c / pkt_player_info.c). A consumer that skips an append
     * therefore leaves its list one short, and every remaining block in the
     * packet applies to the entity AFTER its intended target.
     *
     * That failure is silent -- nothing errors, every op applies, to the wrong
     * entity. It shipped for years and surfaced as "the steel titan's attack
     * animation plays on the Queen Black Dragon" and "the QBD disappears after
     * her wake animation" (a TRANSFORMATION retyping the wrong npc), and only
     * when a familiar was present to make the lists differ in length.
     *
     * Guarded by test_npc_info_unresolvable_entry_keeps_list_positions and
     * test_player_info_unresolvable_entry_keeps_list_positions
     * (`make -C src test-entity-info-shrink`).
     *
     * The local player is deliberately absent from active_players: the decoder
     * gives it the 2047 sentinel rather than a list position, and the executor
     * resolves it through local_pid. The rev-239 player stream is slot-
     * addressed rather than positional and never emits the OLD/SET ops at all.
     */
    int active_players[RS_ENTITY_SYNC_MAX_PLAYERS];
    int active_player_count;
    int active_npcs[RS_ENTITY_SYNC_MAX_NPCS];
    int active_npc_count;

    /** WORLD_ENTITY_ID(kind, server slot) -> element_id + world pool index. */
    struct World_EntityRegistry registry;
};

void
RS_EntitySync_Init(struct RS_EntitySync* sync);

void
RS_EntitySync_Free(struct RS_EntitySync* sync);

/** Drop every tracked entity (logout / rebuild): despawns through World. */
void
RS_EntitySync_Clear(
    struct RS_EntitySync* sync,
    struct World* world);

/** Lookup by server slot. Returns 1 and fills the outs when known. */
int
RS_EntitySync_FindPlayer(
    struct RS_EntitySync* sync,
    int server_pid,
    int* out_world_idx,
    int* out_element_id);

int
RS_EntitySync_FindNpc(
    struct RS_EntitySync* sync,
    int server_slot,
    int* out_world_idx,
    int* out_element_id);

void
RS_EntitySync_RegisterPlayer(
    struct RS_EntitySync* sync,
    int server_pid,
    int element_id,
    int world_idx);

void
RS_EntitySync_RegisterNpc(
    struct RS_EntitySync* sync,
    int server_slot,
    int element_id,
    int world_idx);

/** Despawn + unregister by server slot (safe when unknown). */
void
RS_EntitySync_RemovePlayer(
    struct RS_EntitySync* sync,
    struct World* world,
    int server_pid);

void
RS_EntitySync_RemoveNpc(
    struct RS_EntitySync* sync,
    struct World* world,
    int server_slot);

#endif
