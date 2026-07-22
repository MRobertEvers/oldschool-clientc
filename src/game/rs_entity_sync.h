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

    /** Tracked-list order the server addresses by (values = server slots). */
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
