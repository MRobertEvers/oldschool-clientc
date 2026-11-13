#ifndef RS_GROUP_STORE_H
#define RS_GROUP_STORE_H

/*
 * GROUP_FULL / GROUP_VAR: the groups the player's account belongs to and each
 * group's variables, read by the group_* clientscript commands (deob class630,
 * class638). A group's shape comes from its config (group 75); the wire names
 * only the values.
 *
 * A variable is packed as bits 20..30 the group's config id, 18..19 the scope
 * (0 the group's own, 1 and 2 per-member), 16..17 the base type, 0..15 the slot.
 */

#include "engine/torirs_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RS_GROUP_SLOTS 256
#define RS_GROUP_SCOPES 3

struct RS_GroupValue
{
    signed char base; /* 0 int, 1 long, 2 string */
    int32_t int_value;
    int64_t long_value;
    char* string_value; /* owned; never NULL for a string */
};

struct RS_GroupInstance
{
    int def_id;
    int index;
    int64_t uid;
    int value_count[RS_GROUP_SCOPES];
    struct RS_GroupValue* values[RS_GROUP_SCOPES];
};

struct RS_GroupStore
{
    struct RS_GroupInstance* slots[RS_GROUP_SLOTS];
};

/** A group appeared (uid >= 0) or left (uid == -1): deob class630.method13772,
 *  which runs clientscript 85 with these three. */
typedef void (*RS_GroupStore_NotifyFn)(void* user, int def_id, int index, int64_t uid);

void
RS_GroupStore_Free(struct RS_GroupStore* store);

/** GROUP_FULL (deob class630.method13768). Stops at the first record it cannot
 *  read: records carry no length. */
void
RS_GroupStore_ApplyFull(
    struct RS_GroupStore* store,
    struct ToriRS_GroupTypes const* types,
    uint8_t const* data,
    int length,
    RS_GroupStore_NotifyFn notify,
    void* user);

/** One GROUP_VAR update (deob class630.method13783): ignored when the slot is
 *  empty or holds another config's group. Copies `string_value`. */
void
RS_GroupStore_ApplyVar(
    struct RS_GroupStore* store,
    int index,
    int packed,
    int32_t int_value,
    int64_t long_value,
    char const* string_value);

struct RS_GroupInstance const*
RS_GroupStore_FindUid(
    struct RS_GroupStore const* store,
    int64_t uid);
/* --- session checkpoint (replay/replay_checkpoint.h) --------------------- */
struct RS_GroupStore*
RS_GroupStore_CheckpointTake(struct RS_GroupStore const* store);

void
RS_GroupStore_CheckpointRestore(
    struct RS_GroupStore* store,
    struct RS_GroupStore const* cp);

/** Accepts NULL. */
void
RS_GroupStore_CheckpointFree(struct RS_GroupStore* cp);

size_t
RS_GroupStore_CheckpointBytes(struct RS_GroupStore const* cp);

int
RS_GroupStore_CheckpointEqual(
    struct RS_GroupStore const* a,
    struct RS_GroupStore const* b,
    char* why,
    int why_cap);

#endif
