#ifndef SRC_GAME_RS_NPC_BODY_LANDS_H
#define SRC_GAME_RS_NPC_BODY_LANDS_H

/*
 * Which npc bodies are on their way in: one load per (base, resolved) type
 * however many npcs of that type a packet spawns, so that the load's
 * completion dresses every one of them.
 *
 * A type is registered when its load is made and forgotten when the load
 * lands or is thrown away. The table is fixed; a type arriving while it is
 * full is simply not registered, and the caller loads it without the dedupe.
 * Forgetting moves the last entry into the gap, so the order of entries is
 * not kept.
 *
 * This does not know about a task, a cache or a world. The caller asks
 * whether a type is in flight before it makes a load, registers the type
 * when it does, remembers whether the registration took, and forgets it at
 * the load's end.
 */

#define RS_NPC_BODY_LANDS_MAX 64

struct RS_NpcBodyLands
{
    int base[RS_NPC_BODY_LANDS_MAX];
    int type[RS_NPC_BODY_LANDS_MAX];
    int count;
};

/** A load for (base_npc_id, npc_id) is registered and has not ended. */
int
RS_NpcBodyLands_InFlight(
    struct RS_NpcBodyLands const* lands,
    int base_npc_id,
    int npc_id);

/**
 * Register a load for (base_npc_id, npc_id). Returns 1 when it was
 * registered, 0 when the table is full. Does not look for a duplicate: the
 * caller has asked InFlight first.
 */
int
RS_NpcBodyLands_Register(
    struct RS_NpcBodyLands* lands,
    int base_npc_id,
    int npc_id);

/** The load for (base_npc_id, npc_id) ended. No-op when it is not registered. */
void
RS_NpcBodyLands_Forget(
    struct RS_NpcBodyLands* lands,
    int base_npc_id,
    int npc_id);

#endif
