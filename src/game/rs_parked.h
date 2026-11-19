#ifndef SRC_GAME_RS_PARKED_H
#define SRC_GAME_RS_PARKED_H

/*
 * PARKED WORK: one lookup, one entry per target, for what a packet (or a
 * script, or a varp) could not finish because something PURELY VISUAL was not
 * here yet.
 *
 * A packet task writes what is true now. What it needs to continue, it waits
 * for (Assets_Await). What only changes what is drawn or heard, it parks:
 * Park_Put records the target and what to apply, Park_Want names the asset it
 * waits for, and the task finishes. When the asset arrives the entry is
 * landed, at ONE point in the frame (the client's landing point, after the
 * parallel queue was pumped), by its kind's landing function: look the target
 * up again, check it still applies, apply it or drop it. The entry is freed
 * after that call unless the landing wanted the next link of a chain.
 *
 * The entry IS the record that something is pending. Tests and later packets
 * ask Park_Find("is something pending here?"); a later packet for the same
 * target replaces the entry (a delete overtakes an add still loading); a
 * rebuild, a logout, a closed interface or a removed entity cancels what
 * belonged to it (Park_Cancel).
 *
 * Rules that come from a single-threaded C program with no collector:
 *
 *  - A KEY IS NEVER A POINTER. Between parking and landing the entity, scene
 *    element or component can be freed and its slot reused. A key is an id
 *    plus a generation (ParkKey_*); the landing re-resolves it and treats
 *    "gone" as a normal result.
 *  - What to apply (`what`) is copied into the entry when it is parked and is
 *    plain values: no pointer it would have to free, nothing pointing into
 *    the cache (which keeps no reference counts and can evict between scenes).
 *    The landing reads the asset from the cache at that moment.
 *  - Every entry has one owner, the lookup, and one free: at landing, at a
 *    policy drop, at replacement or at cancel.
 *  - Capacity grows explicitly up to PARK_ENTRIES_MAX; reaching it is an
 *    assert.
 *
 * KINDS are registered at start-up, each by its family's own file
 * (Park_RegisterKind): an id from the family's reserved range below, a name,
 * the landing function and the kind's policy row. There is no central table a
 * family must edit. A put, want or landing of an unregistered kind asserts.
 *
 * The lookup is App-free: it knows the assets table (engine/asset_requests.h)
 * and nothing else. Its clock is the client's, read through a function the
 * client supplies, so a test drives it with a fake clock and fake loaders.
 */

#include "engine/asset_requests.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- kinds: one range per family ----------------------------------------
 *
 * Kind 0 is never a kind. Each family picks its kinds inside its own range,
 * in its own file, so two families cannot pick the same id and neither edits
 * the other's lines. A range is 32 ids; a family that needs more takes the
 * spare range at the end and says so here.
 */
enum ParkKindRange
{
    /* Reserved for src/game/test/rs_parked_test.c. */
    PARK_KINDS_TEST_FIRST = 1,
    PARK_KINDS_TEST_LAST = 31,
    /* app/app_parked_ground.c: ground items and widget icons. */
    PARK_KINDS_GROUND_FIRST = 32,
    PARK_KINDS_GROUND_LAST = 63,
    /* app/app_parked_effects.c: projectiles and spot animations. */
    PARK_KINDS_EFFECTS_FIRST = 64,
    PARK_KINDS_EFFECTS_LAST = 95,
    /* app/app_parked_locs.c: loc changes, loc animations, merges, zone resets,
     * zone packets for unbuilt views. */
    PARK_KINDS_LOCS_FIRST = 96,
    PARK_KINDS_LOCS_LAST = 127,
    /* app/app_parked_entities.c: entity bodies and animations. */
    PARK_KINDS_ENTITIES_FIRST = 128,
    PARK_KINDS_ENTITIES_LAST = 159,
    /* app/app_parked_if_assets.c: interface models, chatheads, inventory
     * icons. */
    PARK_KINDS_IF_ASSETS_FIRST = 160,
    PARK_KINDS_IF_ASSETS_LAST = 191,
    /* app/app_parked_mounts.c: the visual parts of interface mounts, player
     * models. */
    PARK_KINDS_MOUNTS_FIRST = 192,
    PARK_KINDS_MOUNTS_LAST = 223,
    /* app/app_parked_scripts.c: visual script operations. */
    PARK_KINDS_SCRIPTS_FIRST = 224,
    PARK_KINDS_SCRIPTS_LAST = 255,

    PARK_KIND_MAX = 256
};

/* ---- keys ---------------------------------------------------------------- */

enum ParkKeyShape
{
    PARK_KEY_NONE = 0,
    /* An entity: `sub` = PARK_ENTITY_NPC / _PLAYER, `id` = its slot (the
     * server index), `gen` = its spawn serial. */
    PARK_KEY_ENTITY,
    /* A place in a view: `id` = the view, `x`/`z` = the tile, `level`,
     * `layer` (a loc's layer, or a ground stack's 0), `gen` = the view's
     * generation (what changes when the view's slot is reused). */
    PARK_KEY_VIEW,
    /* A scripted projectile slot: `id` = the slot, `gen` = its serial. */
    PARK_KEY_PROJECTILE,
    /* An interface component: `id` = the component id (group << 16 | child),
     * `gen` = its interface's close serial. */
    PARK_KEY_COMPONENT,
    /* One thing of its kind, by an id that is never reused for something
     * else (a song slot, a skybox): `id`, `gen` = whatever generation the
     * owner keeps (0 if none). */
    PARK_KEY_ID,
};

enum ParkEntityType
{
    PARK_ENTITY_NPC = 0,
    PARK_ENTITY_PLAYER = 1,
};

/* Compared field by field: two keys are the same target when every field is
 * equal. Always made with a constructor, which zeroes what a shape does not
 * use. */
struct ParkKey
{
    uint8_t shape; /* enum ParkKeyShape */
    uint8_t sub;   /* PARK_KEY_ENTITY: enum ParkEntityType */
    uint8_t level;
    uint8_t layer;
    int32_t id;
    int32_t x;
    int32_t z;
    uint32_t gen;
};

static inline struct ParkKey
ParkKey_Entity(
    enum ParkEntityType type,
    int slot,
    uint32_t spawn_serial)
{
    struct ParkKey k = { 0 };
    k.shape = PARK_KEY_ENTITY;
    k.sub = (uint8_t)type;
    k.id = slot;
    k.gen = spawn_serial;
    return k;
}

static inline struct ParkKey
ParkKey_View(
    int view,
    int tile_x,
    int tile_z,
    int level,
    int layer,
    uint32_t view_gen)
{
    struct ParkKey k = { 0 };
    k.shape = PARK_KEY_VIEW;
    k.id = view;
    k.x = tile_x;
    k.z = tile_z;
    k.level = (uint8_t)level;
    k.layer = (uint8_t)layer;
    k.gen = view_gen;
    return k;
}

static inline struct ParkKey
ParkKey_Projectile(
    int slot,
    uint32_t serial)
{
    struct ParkKey k = { 0 };
    k.shape = PARK_KEY_PROJECTILE;
    k.id = slot;
    k.gen = serial;
    return k;
}

static inline struct ParkKey
ParkKey_Component(
    int component_id,
    uint32_t close_serial)
{
    struct ParkKey k = { 0 };
    k.shape = PARK_KEY_COMPONENT;
    k.id = component_id;
    k.gen = close_serial;
    return k;
}

static inline struct ParkKey
ParkKey_Id(
    int id,
    uint32_t gen)
{
    struct ParkKey k = { 0 };
    k.shape = PARK_KEY_ID;
    k.id = id;
    k.gen = gen;
    return k;
}

static inline bool
ParkKey_Equal(
    struct ParkKey a,
    struct ParkKey b)
{
    return a.shape == b.shape && a.sub == b.sub && a.level == b.level && a.layer == b.layer &&
           a.id == b.id && a.x == b.x && a.z == b.z && a.gen == b.gen;
}

/* ---- policy: one row per kind, stated by the family that registers it ----
 *
 * What happens when the asset cannot be served (give up after N / back off
 * forever / remember the failure) and who is landed (the one entry / every
 * entry waiting on the asset) are the ASSET kind's policy, in the assets
 * table's one table (asset_requests.c): the lookup follows them. What is the
 * PARKED kind's to say is below.
 */

enum ParkNewerPolicy
{
    /* A Park_Put for a key that already has an entry of the kind replaces it:
     * the older entry is freed unlanded. Newest wins. */
    PARK_NEWER_REPLACE = 0,
    /* Every put is its own entry; Park_Find answers the newest. */
    PARK_NEWER_KEEP_BOTH,
};

enum ParkLatePolicy
{
    /* Land whenever it arrives. The landing is told 0 for lateness. */
    PARK_LATE_LAND = 0,
    /* Land, and tell the landing how many cycles after its arrival (the
     * packet's) it is landing, so a delay counted from arrival is shortened
     * by the wait. */
    PARK_LATE_LAND_SHORTENED,
    /* Land if it lands within `late_limit_cycles` of arrival; dropped
     * (freed unlanded) after. */
    PARK_LATE_DROP_AFTER,
};

enum ParkFailedPolicy
{
    /* The asset table gave up on it (or remembers it failed): the landing is
     * still called, with ASSET_OUTCOME_FAILED, and applies whatever the
     * target shows when the asset is absent. */
    PARK_FAILED_LAND = 0,
    /* ... the entry is freed unlanded. */
    PARK_FAILED_DROP,
};

struct ParkPolicy
{
    enum ParkNewerPolicy newer;
    enum ParkLatePolicy late;
    uint32_t late_limit_cycles; /* PARK_LATE_DROP_AFTER */
    enum ParkFailedPolicy failed;
};

/* ---- entries ------------------------------------------------------------- */

/* The largest `what` an entry carries. A family whose payload is larger
 * raises it (Park_Put asserts). */
#define PARK_WHAT_MAX 128

enum ParkEntryState
{
    /* Wants an asset (`wants`), or waits for its owner's Park_Ready, or for
     * its `ready_at` cycle. */
    PARK_ENTRY_WAITING = 1,
    /* Arrived: lands at the next landing point, in arrival order. */
    PARK_ENTRY_READY,
};

/* Why an entry is landing. */
enum ParkReason
{
    /* The asset it wanted came back (see `outcome`). */
    PARK_REASON_ASSET = 0,
    /* Its owner said it can land (Park_Ready / Park_ReadyWhere). */
    PARK_REASON_READY,
    /* Its ready_at cycle came. */
    PARK_REASON_TIME,
};

struct ParkEntry
{
    uint16_t kind;
    uint8_t state;   /* enum ParkEntryState */
    uint8_t reason;  /* enum ParkReason, once READY */
    uint8_t outcome; /* enum AssetOutcome, for PARK_REASON_ASSET */
    struct ParkKey key;
    /* The scene it was parked in, as the client counts scenes; 0 for a kind
     * that does not belong to a scene (an interface's, an entity's own). */
    uint32_t scene_serial;
    /* Arrival order: entries that land together land in this order. Unique
     * and increasing for the life of the lookup. */
    uint32_t order;
    /* The client cycle it was parked on: delays and lifetimes count from
     * arrival, not from landing. */
    uint32_t arrived_cycle;
    /* What it waits for now; kind < 0 when nothing. Updated as a chain is
     * discovered (a landing that wants the next link). */
    struct AssetKey wants;
    /* Lands at the first landing point on or after this cycle; 0 = not timed. */
    uint32_t ready_at;
    uint32_t what_size;
    union
    {
        unsigned char bytes[PARK_WHAT_MAX];
        uint64_t align_u64;
        double align_d;
        void* align_p;
    } what;
};

/** The entry's payload, as copied by Park_Put. */
static inline void const*
Park_What(struct ParkEntry const* entry)
{
    return entry->what.bytes;
}

/* A handle on one entry: an index plus a generation, so a handle kept past
 * the entry's free answers "gone" instead of reaching its slot's next owner. */
struct ParkRef
{
    int32_t index; /* -1: none */
    uint32_t gen;
};

static inline bool
ParkRef_Valid(struct ParkRef ref)
{
    return ref.index >= 0;
}

/* ---- landing ------------------------------------------------------------- */

struct Park;

enum ParkLandResult
{
    /* Applied, or dropped because the target is gone or no longer wants it:
     * the entry is freed. */
    PARK_LAND_DONE = 0,
    /* The landing asked for the next link (Park_Want on `ref`) or set a time
     * (Park_ReadyAt): the entry is kept. Anything else keeps nothing: a
     * KEEP with the entry neither wanting nor timed is an assert. */
    PARK_LAND_KEEP,
};

struct ParkArrival
{
    enum ParkReason reason;
    /* PARK_REASON_ASSET: what the asset table answered. Never DROPPED: a
     * dropped asset frees the entry unlanded. */
    enum AssetOutcome outcome;
    /* Cycles since arrival under PARK_LATE_LAND_SHORTENED, 0 otherwise. */
    uint32_t late_cycles;
};

/**
 * One per kind. `entry` is a copy made for the call: it stays valid whatever
 * the landing does to the lookup (put, cancel, want). `ref` names the entry
 * itself, for a landing that keeps it (Park_Want / Park_ReadyAt, then
 * PARK_LAND_KEEP). `ctx` is the one given to Park_RegisterKind.
 */
typedef enum ParkLandResult (*ParkLandFn)(
    void* ctx,
    struct Park* park,
    struct ParkRef ref,
    struct ParkEntry const* entry,
    struct ParkArrival const* arrival);

/* ---- the lookup ---------------------------------------------------------- */

/** The client's cycle, read at each put and each landing. */
typedef uint32_t (*ParkClockFn)(void* data);

/**
 * An empty lookup over `assets` (not owned; it must outlive the lookup's
 * last want). `clock` gives the cycle entries arrive and land on.
 */
struct Park*
Park_New(
    struct Assets* assets,
    ParkClockFn clock,
    void* clock_data);

/** NULL is accepted. Every entry is freed unlanded. */
void
Park_Free(struct Park* park);

/** Register a kind: once per kind, at start-up, from its family's file. */
void
Park_RegisterKind(
    struct Park* park,
    int kind,
    char const* name,
    ParkLandFn land,
    void* ctx,
    struct ParkPolicy const* policy);

bool
Park_KindRegistered(
    struct Park const* park,
    int kind);

char const*
Park_KindName(
    struct Park const* park,
    int kind);

/**
 * Park `what` (`what_size` bytes, copied) for `key`. Under PARK_NEWER_REPLACE
 * an entry of the kind already parked on the same key is freed first. The
 * new entry waits for nothing until Park_Want, Park_Ready or Park_ReadyAt.
 */
struct ParkRef
Park_Put(
    struct Park* park,
    int kind,
    struct ParkKey key,
    uint32_t scene_serial,
    void const* what,
    size_t what_size);

/**
 * The entry waits for `asset` (replacing whatever it waited for). The want is
 * registered with the assets table, which starts a load if none is in flight;
 * an asset already here, or remembered failed, makes the entry ready at once
 * (it still lands at the landing point, never inside this call). Returns
 * what Assets_Want answered.
 */
enum AssetState
Park_Want(
    struct Park* park,
    struct ParkRef ref,
    struct AssetKey asset);

/** The entry can land: its owner's condition holds (a view was built). */
void
Park_Ready(
    struct Park* park,
    struct ParkRef ref);

/** The entry lands at the first landing on or after `cycle` (> 0). */
void
Park_ReadyAt(
    struct Park* park,
    struct ParkRef ref,
    uint32_t cycle);

/** The live entry `ref` names, or NULL when it is gone. Valid until the next
 *  call that changes the lookup. */
struct ParkEntry const*
Park_Get(
    struct Park const* park,
    struct ParkRef ref);

/** The newest entry of `kind` on `key`; index -1 when nothing is pending. */
struct ParkRef
Park_Find(
    struct Park const* park,
    int kind,
    struct ParkKey key);

/** Free one entry unlanded. A ref that is gone is a no-op (the target the
 *  caller remembered may have landed meanwhile). */
void
Park_Remove(
    struct Park* park,
    struct ParkRef ref);

/* ---- scopes: what a cancel, or a Park_ReadyWhere, applies to ------------ */

enum ParkScopeKind
{
    /* Everything (a logout). */
    PARK_SCOPE_ALL = 0,
    /* Entries parked in scene serial `id` (a rebuild of that scene). */
    PARK_SCOPE_SCENE,
    /* Entries keyed on view `id` (PARK_KEY_VIEW): a view torn down. */
    PARK_SCOPE_VIEW,
    /* Entries keyed on entity (`sub`, `id`), any spawn serial: an entity
     * removed. */
    PARK_SCOPE_ENTITY,
    /* Entries keyed on a component of interface group `id`: an interface
     * closed. */
    PARK_SCOPE_GROUP,
    /* Every entry of kind `id`. */
    PARK_SCOPE_KIND,
    /* The entries of kind `id` on exactly `key`. */
    PARK_SCOPE_KEY,
};

struct ParkScope
{
    enum ParkScopeKind kind;
    int32_t id;
    uint8_t sub;
    struct ParkKey key;
};

static inline struct ParkScope
ParkScope_All(void)
{
    struct ParkScope s = { PARK_SCOPE_ALL, 0, 0, { 0 } };
    return s;
}

static inline struct ParkScope
ParkScope_Scene(uint32_t scene_serial)
{
    struct ParkScope s = { PARK_SCOPE_SCENE, (int32_t)scene_serial, 0, { 0 } };
    return s;
}

static inline struct ParkScope
ParkScope_View(int view)
{
    struct ParkScope s = { PARK_SCOPE_VIEW, view, 0, { 0 } };
    return s;
}

static inline struct ParkScope
ParkScope_Entity(
    enum ParkEntityType type,
    int slot)
{
    struct ParkScope s = { PARK_SCOPE_ENTITY, slot, (uint8_t)type, { 0 } };
    return s;
}

static inline struct ParkScope
ParkScope_Group(int group)
{
    struct ParkScope s = { PARK_SCOPE_GROUP, group, 0, { 0 } };
    return s;
}

static inline struct ParkScope
ParkScope_Kind(int kind)
{
    struct ParkScope s = { PARK_SCOPE_KIND, kind, 0, { 0 } };
    return s;
}

static inline struct ParkScope
ParkScope_Key(
    int kind,
    struct ParkKey key)
{
    struct ParkScope s = { PARK_SCOPE_KEY, kind, 0, key };
    return s;
}

/** Free every entry in `scope` unlanded. Returns how many. */
int
Park_Cancel(
    struct Park* park,
    struct ParkScope scope);

/** Make every waiting entry in `scope` ready. Returns how many. */
int
Park_ReadyWhere(
    struct Park* park,
    struct ParkScope scope);

/* ---- the landing point --------------------------------------------------- */

/** The function and context the lookup's wants carry in the assets table:
 *  the client lands them with Assets_LandFor(assets, fn, park). */
AssetLandFn
Park_AssetLandFn(void);

/**
 * Land ready entries, oldest arrival first, at most `budget` (> 0) of them.
 * Entries that become ready during this call (a landing whose next link was
 * already here) wait for the next call. Ready entries past the budget stay
 * ready for the next call. Returns how many landing functions were called.
 * Not re-entrant: a landing that calls it asserts.
 */
int
Park_Land(
    struct Park* park,
    int budget);

/** Is the lookup inside Park_Land? */
bool
Park_Landing(struct Park const* park);

/* ---- counts -------------------------------------------------------------- */

struct ParkStats
{
    uint32_t put;
    uint32_t replaced;
    uint32_t landed;
    /* Freed unlanded by a policy: too late, failed, or the asset dropped. */
    uint32_t dropped;
    uint32_t cancelled;
};

struct ParkStats
Park_Stats(struct Park const* park);

/** Live entries of `kind` (0 for every kind). */
int
Park_Count(
    struct Park const* park,
    int kind);

/** Entries ready to land (or due by time) at the next landing. */
int
Park_ReadyCount(struct Park const* park);

/** The ceiling: reaching it is an assert. */
#define PARK_ENTRIES_MAX 65536

#endif /* SRC_GAME_RS_PARKED_H */
