#ifndef SRC_GAME_RS_EVENT_RING_H
#define SRC_GAME_RS_EVENT_RING_H

/*
 * The event vocabulary both clients raise from server packets, and the ring
 * they keep it in.
 *
 * ONE VOCABULARY. The real client (struct App's drive ring, stamped at the
 * `DRIVE_STAMP:` sites and read by the quest driver, plugin/torirs_plugin_drive.c)
 * and the headless client (the scriptrun bot, scriptrun/scriptrun_core.c, read
 * by torirsserver/torirs_server_scriptrun.c) raise the same kinds from the same
 * packets, and a test's `await{ event = "<name>" }` names them by the strings
 * below. DRIVE_EVENT_* (plugin/torirs_plugin_drive.h) and SCRIPTRUN_EV_*
 * (scriptrun/scriptrun_core.h) are the two owners' spellings of this enum; the
 * per-kind payload table is in torirs_plugin_drive.h.
 *
 * The names are Lua-visible: tests under test/ and script/ match on them, so a
 * name never changes and a new kind goes on the end.
 *
 * ONE RING, TWO OWNERS. The ring never allocates and never refuses a stamp.
 * Its storage, its capacity and what a full ring does are the owner's, passed
 * at every call, so a zeroed owner (a calloc'd App in a test, a fresh core) is
 * an empty ring with no init call:
 *
 *   - the real client: 512 entries, RS_EVENT_RING_COUNT_DROPS -- the oldest
 *     entry goes and `dropped` counts it; a reader whose cursor fell off the
 *     end is refused (RS_EventRing_Read answers 0) rather than skipping.
 *   - the headless client: 1024 entries, RS_EVENT_RING_OVERWRITE -- the oldest
 *     entry goes and nothing counts it; its reader clamps its cursor to what
 *     the ring still holds (torirs_server_scriptrun.c bot_step_once).
 *
 * Serials are 1-based, consecutive and monotonic for the ring's life; 0 is
 * "no event".
 */

#include <stdint.h>

enum RS_EventKind
{
    RS_EVENT_NONE = 0,
    RS_EVENT_SUB_OPENED,
    RS_EVENT_SUB_MOUNTED,
    RS_EVENT_SUB_CLOSED,
    RS_EVENT_CHAT_OPENED,
    RS_EVENT_SLOT_MOUNTED,
    RS_EVENT_RESUME_ANSWERED,
    RS_EVENT_VARP_CHANGED,
    RS_EVENT_INV_CHANGED,
    RS_EVENT_OBJ_ADDED,
    RS_EVENT_OBJ_REMOVED,
    RS_EVENT_MAP_FLAG,
    RS_EVENT_CHAT_MESSAGE,
    RS_EVENT_SERVER_TICK,
    RS_EVENT_NPC_SPAWN,
    RS_EVENT_NPC_DESPAWN,
    RS_EVENT_NPC_RETYPE,
    RS_EVENT_INV_PACKET,
    RS_EVENT_NPC_SEQ,
    RS_EVENT_NPC_FACE,
    RS_EVENT_KIND_COUNT
};

/** "sub_opened", ... (kind 0 is "none"). The name a Lua await predicate
 *  matches on. Never NULL; an out-of-range kind asserts. */
char const*
RS_EventKindName(int kind);

/** The kind named `name`, including RS_EVENT_NONE for "none"; -1 when no kind
 *  has that name (a test's typo is a runtime answer, not a contract
 *  violation). */
int
RS_EventKindFromName(char const* name);

/** One stamped event. a..d are per kind (torirs_plugin_drive.h's table). */
struct RS_Event
{
    uint32_t serial; /**< 1-based, consecutive; 0 is "no event" */
    int32_t kind;    /**< enum RS_EventKind */
    int32_t cycle;   /**< the owner's world cycle at the stamp */
    int32_t a, b, c, d;
};

/** What a full ring does with the entry the next stamp overwrites. Either way
 *  the oldest entry goes: the policies differ only in whether it is counted. */
enum RS_EventRingFull
{
    RS_EVENT_RING_OVERWRITE = 0, /**< silently (the headless client) */
    RS_EVENT_RING_COUNT_DROPS,   /**< counted in `dropped` (the real client) */
};

/** The ring's bookkeeping. All zero is an empty ring. The entries live beside
 *  it in the owner, `capacity` of them. */
struct RS_EventRing
{
    /** Serial of the newest entry; 0 before the first stamp. */
    uint32_t newest_serial;
    /** Serial of the oldest entry the ring still holds; 0 while empty. */
    uint32_t oldest_serial;
    /** Where the next stamp lands. */
    int write_index;
    /** How many entries are live, at most the owner's capacity. */
    int count;
    /** Entries a full RS_EVENT_RING_COUNT_DROPS ring overwrote. */
    uint32_t dropped;
};

/**
 * Stamp `ev` (its kind, cycle and a..d; its serial is assigned here) into the
 * ring. A full ring overwrites its oldest entry by `full`'s rule. Returns the
 * stored entry.
 */
struct RS_Event const*
RS_EventRing_Push(
    struct RS_EventRing* ring,
    struct RS_Event* entries,
    int capacity,
    enum RS_EventRingFull full,
    struct RS_Event const* ev);

/**
 * Copy every entry newer than `after_serial` into `out`, oldest first, at most
 * `cap` of them. `*out_count` is the number written; `*out_serial` the serial
 * of the last one written, or the ring's newest when none was, so a truncated
 * read re-polled from `*out_serial` resumes where it stopped.
 *
 * Returns 1, or 0 (refused, nothing written) when `after_serial` is older than
 * the oldest surviving entry: the caller missed events and must say so. A
 * cursor of 0 means "everything the ring still holds" and is never refused.
 */
int
RS_EventRing_Read(
    struct RS_EventRing const* ring,
    struct RS_Event const* entries,
    int capacity,
    uint32_t after_serial,
    struct RS_Event* out,
    int cap,
    int* out_count,
    uint32_t* out_serial);

/** The held entry with serial `serial`, which must lie between the oldest and
 *  the newest the ring holds. */
struct RS_Event const*
RS_EventRing_At(
    struct RS_EventRing const* ring,
    struct RS_Event const* entries,
    int capacity,
    uint32_t serial);

#endif
