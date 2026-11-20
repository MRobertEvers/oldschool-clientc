#ifndef SRC_REPLAY_REPLAY_NPC_TYPES_H
#define SRC_REPLAY_REPLAY_NPC_TYPES_H

/*
 * The npc types a replay's world ledger reads NPC_INFO by.
 *
 * A typed BODY/HEAD customisation block carries its recolour and retexture
 * lists with no count: the npc TYPE's lists decide it (deob Statics ~47320,
 * field1237.field5139 / field5158 .length). The clients answer that from
 * their npc configs (GameProtoNpcTypeLengths); the ledger is built in one
 * pass over the whole recording, before most of those configs are resident,
 * so it is answered from this: the two counts of every npc record of the
 * cache the client opened, read once when the replay opens (about 50 ms for
 * cache.osrs242's 16,892 records).
 */

#include <stdint.h>

struct RSCache;
struct RSCache_Dat2Disk;

struct ReplayNpcTypes
{
    /** Npc ids 0 .. count - 1. */
    int count;
    uint8_t* present;
    uint8_t* recolours;
    uint8_t* retextures;
};

/** A table of `count` ids, none present (ReplayNpcTypes_Set fills it). */
struct ReplayNpcTypes*
ReplayNpcTypes_New(int count);

void
ReplayNpcTypes_Set(
    struct ReplayNpcTypes* types,
    int npc_id,
    int recolours,
    int retextures);

/**
 * Every npc record of the dat2 cache in `cache_dir`, decoded with `profile`
 * (the client's). NULL, and says why, when the directory has no such cache
 * or keeps its npcs in sharded groups (the RS2 layout, which no recording
 * this replays uses).
 */
struct ReplayNpcTypes*
ReplayNpcTypes_NewFromCacheDir(
    char const* cache_dir,
    struct RSCache const* profile);

/** Accepts NULL. */
void
ReplayNpcTypes_Free(struct ReplayNpcTypes* types);

/**
 * GameProtoNpcTypeLengths by type, `ctx` the table: asked with `old` -1 (the
 * world ledger resolves an npc list index itself). A type the cache does not
 * have has no lists (0 and 0), as the reference's default type.
 */
int
ReplayNpcTypes_Lengths(
    void* ctx,
    int old,
    int npc_type,
    int head,
    int* recolours,
    int* retextures);

#endif
