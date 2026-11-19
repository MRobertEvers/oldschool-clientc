#ifndef SRC_ENGINE_ASSET_REQUESTS_H
#define SRC_ENGINE_ASSET_REQUESTS_H

/*
 * THE ASSETS TABLE: one entry per asset that is wanted and not here, and at
 * most one load in flight for it.
 *
 * It answers the question no single tracker answered for the whole client:
 * "is this already loading?". Before it, every owner kept its own answer --
 * a table of npc bodies in flight, two sequence trackers, a pending-texture
 * list and a failed-texture bitmap, a flag per spot-animation slot, per npc,
 * per audio entry, per skybox, a retry clock for inventory icons, an attempt
 * cap for ground stacks, a guard on the music player, a flag on the maps
 * reference table -- and each had its own way of failing: a full table that
 * silently stopped deduping, a flag a new request cleared under a running
 * load, a failure never retried, a load queued every tick.
 *
 * An entry is keyed by (kind, id, variant): the variant is what else names
 * the asset (an obj model's count variant, an npc body's base type, a held
 * item's gender, a song's source). It is wanted / loading / failed; an asset
 * that is HERE has no entry -- residency is the cache's to answer, through the
 * kind's `resident` function, because the cache can evict between scenes and
 * an entry claiming "here" would lie.
 *
 * Two ways to ask:
 *
 *  - Assets_Await(key, &join): REQUIRED. The calling task counts the load on
 *    one of its join fields and waits with PT_TASK_JOIN. The count drops when
 *    the attempt in flight ENDS, landed or not -- a task waiting on a load
 *    must always be released, and it re-reads the cache to see what it got.
 *  - Assets_Want(key, waiter): VISUAL. The waiter -- an opaque handle and a
 *    landing function -- is called from Assets_Land once the asset has
 *    arrived or the kind's policy has given up on it. A NULL waiter only asks
 *    for the load (a prefetch, or an owner that polls residency itself).
 *
 * Either way a loader is started only if none is in flight for the entry. The
 * client supplies one loader per kind (Assets_SetLoader): `start` puts a load
 * on the parallel queue and later reports its end with Assets_Finished (or
 * Assets_Abandoned when the task is freed before it ends). The table does not
 * know about tasks, caches or the App: a test drives it with fake loaders.
 *
 * WHAT DIFFERS PER KIND IS POLICY, stated once in the table in
 * asset_requests.c (Assets_Policy):
 *   - fail:   give up after N attempts / retry with back-off forever /
 *             remember the failure;
 *   - late:   land anyway / land with the delay shortened (the waiter is told
 *             how late it is) / drop after a deadline;
 *   - newer:  keep both / a newer request of the kind replaces the older (one
 *             loader of the kind in flight, the older's waiters dropped);
 *   - landed: the one entry (every waiter is landed) / every entry waiting on
 *             the asset (waiters with the same landing function and context
 *             collapse into one call, which finds every target itself).
 *
 * Capacities grow explicitly up to a fixed ceiling; reaching it is an assert,
 * never a silent stop of dedupe.
 *
 * Landing happens only in Assets_Land, which the client calls at the points
 * where each kind lands. A landing function may call back into the table.
 */

#include <stdbool.h>
#include <stdint.h>

enum AssetKind
{
    /* A cache model. */
    ASSET_KIND_MODEL = 0,
    /* A texture, published to the scene's texture map. */
    ASSET_KIND_TEXTURE,
    /* A sprite group. */
    ASSET_KIND_SPRITE,
    /* A sequence and its frames or skeletal set (always registers: a failed
     * one registers an empty sentinel, so the cache remembers). */
    ASSET_KIND_SEQUENCE,
    /* A spot-animation: its type, model and sequence. */
    ASSET_KIND_SPOTANIM,
    /* An npc's body as a composite: id = the type drawn, variant = its base. */
    ASSET_KIND_NPC_BODY,
    /* What a sequence puts in a player's hands: id = seq, variant = gender. */
    ASSET_KIND_PLAYER_HELD,
    /* The npc a transmogged player is drawn as: id = the shell npc. */
    ASSET_KIND_PLAYER_TRANSMOG,
    /* An obj's model as a composite: id = obj, variant = count. */
    ASSET_KIND_OBJ_MODEL,
    /* The inventory icon pass: one entry, id 0. Loaded = no slot lacks an
     * icon; a pass that resolved nothing is a failed attempt. */
    ASSET_KIND_INV_ICONS,
    /* An interface group's component pack. */
    ASSET_KIND_IF_GROUP,
    /* A sound effect's clip. */
    ASSET_KIND_SOUND,
    /* A music track or jingle: id = song, variant = source. */
    ASSET_KIND_MUSIC,
    /* The skybox model: id = model. */
    ASSET_KIND_SKYBOX,
    /* A cache reference table: id = the table. A shared prerequisite. */
    ASSET_KIND_REF_TABLE,

    ASSET_KIND_COUNT
};

#define ASSET_KIND_BIT(kind) (1u << (unsigned)(kind))
#define ASSET_KIND_ALL ((1u << (unsigned)ASSET_KIND_COUNT) - 1u)

struct AssetKey
{
    int32_t kind; /* enum AssetKind */
    int32_t id;
    int32_t variant;
};

static inline struct AssetKey
AssetKey_Make(
    enum AssetKind kind,
    int id,
    int variant)
{
    struct AssetKey key = { (int32_t)kind, (int32_t)id, (int32_t)variant };
    return key;
}

/* ---- policy --------------------------------------------------------------- */

enum AssetFailPolicy
{
    /* `fail_attempts` loads in a row came back without the asset: every
     * waiter lands FAILED and the entry is forgotten. A later want starts a
     * fresh count. */
    ASSET_FAIL_GIVE_UP = 0,
    /* Retry after a wait that doubles from `backoff_first_ms` up to
     * `backoff_max_ms`, for as long as anyone wants it. Never gives up. */
    ASSET_FAIL_BACKOFF,
    /* The first failure is final for the session: the waiters land FAILED and
     * every later want answers FAILED at once. */
    ASSET_FAIL_REMEMBER,
};

enum AssetLatePolicy
{
    /* Land whenever it arrives. */
    ASSET_LATE_LAND = 0,
    /* Land, and tell the waiter how long after its want it is landing, so a
     * delay counted from arrival is shortened by the load. */
    ASSET_LATE_LAND_SHORTENED,
    /* Land if it arrives within `late_limit_ms` of the want; drop after. */
    ASSET_LATE_DROP_AFTER,
};

enum AssetNewerPolicy
{
    /* Requests of the kind are independent. */
    ASSET_NEWER_KEEP_BOTH = 0,
    /* A want for another asset of the kind replaces every older one: their
     * waiters land DROPPED, and the newer load starts when the older loader
     * (which cannot be stopped mid-read) has ended. */
    ASSET_NEWER_REPLACE,
};

enum AssetLandedPolicy
{
    /* Every waiter is landed with its own call. */
    ASSET_LANDED_ONE = 0,
    /* The landing function finds every target itself: waiters with the same
     * function and context are one call. */
    ASSET_LANDED_ALL,
};

struct AssetPolicy
{
    char const* name;
    enum AssetFailPolicy fail;
    int fail_attempts;        /* ASSET_FAIL_GIVE_UP */
    uint32_t backoff_first_ms; /* ASSET_FAIL_BACKOFF */
    uint32_t backoff_max_ms;
    enum AssetLatePolicy late;
    uint32_t late_limit_ms; /* ASSET_LATE_DROP_AFTER */
    enum AssetNewerPolicy newer;
    enum AssetLandedPolicy landed;
};

/** The kind's row of the one policy table. */
struct AssetPolicy const*
Assets_Policy(enum AssetKind kind);

/** The wait after `wait_ms` (0 = none yet) under a back-off policy: the first
 *  wait, then doubled, up to the ceiling. */
uint32_t
Assets_BackoffNext(
    struct AssetPolicy const* policy,
    uint32_t wait_ms);

/* ---- loaders and waiters ------------------------------------------------- */

struct Assets;

enum AssetStart
{
    /* A load is running; it will call Assets_Finished or Assets_Abandoned. */
    ASSET_START_LOADING = 1,
    /* Nothing to load: the asset is already here. */
    ASSET_START_HERE = 0,
    /* The load could not be made at all: counts as a failed attempt. */
    ASSET_START_FAILED = -1,
};

struct AssetLoader
{
    /* Start a load for `key`. May call Assets_Finished before returning (a
     * loader that is answered at once). Must not want or await. */
    enum AssetStart (*start)(
        void* data,
        struct Assets* assets,
        struct AssetKey key);
    /* Is `key` here now? NULL: only `start` can say. */
    bool (*resident)(
        void* data,
        struct AssetKey key);
    void* data;
};

/** Opaque to the table: whatever the waiter's owner needs to find its target
 *  again. Never a pointer to something that can be freed while it waits. */
struct AssetWaiterHandle
{
    int32_t v[4];
};

enum AssetOutcome
{
    ASSET_OUTCOME_LANDED = 0,
    /* The policy gave up on it, or remembered it failed. */
    ASSET_OUTCOME_FAILED,
    /* Too late under ASSET_LATE_DROP_AFTER, or replaced by a newer request. */
    ASSET_OUTCOME_DROPPED,
};

typedef void (*AssetLandFn)(
    void* ctx,
    struct AssetKey key,
    struct AssetWaiterHandle handle,
    enum AssetOutcome outcome,
    uint32_t late_ms);

struct AssetWaiter
{
    AssetLandFn land;
    void* ctx;
    struct AssetWaiterHandle handle;
};

enum AssetState
{
    /* No entry: not asked for, or here, or a forgotten failure. */
    ASSET_STATE_NONE = 0,
    /* Wanted, no loader running: waiting out a back-off, or queued behind an
     * older load of a REPLACE kind. */
    ASSET_STATE_WANTED,
    ASSET_STATE_LOADING,
    /* Remembered as failed (ASSET_FAIL_REMEMBER). */
    ASSET_STATE_FAILED,
    /* Not an entry state: what Assets_Want answers for an asset that is
     * resident. Nothing was registered. */
    ASSET_STATE_HERE,
};

/* ---- the table ----------------------------------------------------------- */

/** An empty table with no loaders. Entries and waiters grow from small. */
struct Assets*
Assets_New(void);

/** NULL is accepted. Waiters still registered are dropped uncalled. */
void
Assets_Free(struct Assets* assets);

/**
 * The client is shutting down: from here every end, abandonment and unjoin is
 * ignored. A task queue freed whole frees its tasks without ending their
 * joins (the parents are freed with them), so a loader freed with it must not
 * end the table's either. Close before freeing the queues, free after.
 */
void
Assets_Close(struct Assets* assets);

/** The kind's loader. Wanting a kind with none is an assert. */
void
Assets_SetLoader(
    struct Assets* assets,
    enum AssetKind kind,
    struct AssetLoader const* loader);

/** The clock the back-off and the late policies read, in milliseconds. */
void
Assets_SetClock(
    struct Assets* assets,
    uint64_t now_ms);

uint64_t
Assets_Clock(struct Assets const* assets);

/**
 * VISUAL: register `waiter` (NULL: no waiter) and start a load if none is in
 * flight. Answers what was found:
 *   HERE    resident; nothing registered, the caller lands it now;
 *   FAILED  remembered failure; nothing registered;
 *   LOADING / WANTED  registered (or collapsed under ASSET_LANDED_ALL).
 * A want for an entry waiting out a back-off restarts it if the wait is over.
 */
enum AssetState
Assets_Want(
    struct Assets* assets,
    struct AssetKey key,
    struct AssetWaiter const* waiter);

/**
 * REQUIRED: count the load on `*join` and start one if none is in flight
 * (an entry waiting out a back-off starts now: a task is waiting on it).
 * Returns 1 when counted, 0 when nothing to wait for (here, or a remembered
 * failure). `*join` drops when the attempt in flight ends, landed or not.
 */
int
Assets_Await(
    struct Assets* assets,
    struct AssetKey key,
    int* join);

/** A task counting on `join` is going away before its loads ended: stop
 *  counting them. `*join` is zeroed. */
void
Assets_Unjoin(
    struct Assets* assets,
    int* join);

/** The loader for `key` ended; `ok` says whether the asset is here. */
void
Assets_Finished(
    struct Assets* assets,
    struct AssetKey key,
    bool ok);

/** The loader for `key` was freed before it ended. Not a failed attempt: the
 *  entry goes back to wanted and its awaiters are released. */
void
Assets_Abandoned(
    struct Assets* assets,
    struct AssetKey key);

/** For a kind that remembers failures: `key` turned out not to be usable
 *  after its load landed (a texture whose pixels would not convert). It is
 *  failed from now on; a waiter still registered lands FAILED. Not for a key
 *  whose loader is running. */
void
Assets_Fail(
    struct Assets* assets,
    struct AssetKey key);

/** Restart every loader whose back-off is over and that still has a waiter. */
void
Assets_Pump(struct Assets* assets);

/** Retry `key` at once, keeping its back-off wait for the next failure. No-op
 *  when it has no entry. */
void
Assets_Lift(
    struct Assets* assets,
    struct AssetKey key);

/**
 * Land every arrival of the kinds in `kind_mask`, in the order the waiters
 * registered. Arrivals made by the landings themselves wait for the next call.
 * Returns how many landing calls were made.
 */
int
Assets_Land(
    struct Assets* assets,
    uint32_t kind_mask);

/** Forget every want waiter with `land` and `ctx` for which `match` answers
 *  true (pending or arrived), uncalled. Returns how many. */
int
Assets_DropWaiters(
    struct Assets* assets,
    AssetLandFn land,
    void* ctx,
    bool (*match)(
        struct AssetWaiterHandle handle,
        struct AssetKey key,
        void* arg),
    void* arg);

enum AssetState
Assets_State(
    struct Assets const* assets,
    struct AssetKey key);

/** A loader for `key` is running. */
bool
Assets_InFlight(
    struct Assets const* assets,
    struct AssetKey key);

/** Would a want start a load now? True with no entry, or for a wanted entry
 *  whose back-off is over and whose kind is free to load; false while it is
 *  loading, waiting out a back-off, or remembered failed. For an owner whose
 *  residency test is costly, to ask before it pays for it. */
bool
Assets_Due(
    struct Assets const* assets,
    struct AssetKey key);

/** Failed attempts since the entry was made, or since its last success. */
int
Assets_Attempts(
    struct Assets const* assets,
    struct AssetKey key);

/** Bumped by every load that ends: a reader that remembers a miss can ask
 *  "has anything arrived since?". */
uint32_t
Assets_Serial(struct Assets const* assets);

/** Entries of `kind` (wanted, loading or failed); ASSET_KIND_COUNT for all. */
int
Assets_Count(
    struct Assets const* assets,
    enum AssetKind kind);

/** Want waiters registered or arrived and not landed, of `kind`
 *  (ASSET_KIND_COUNT for all). */
int
Assets_WaiterCount(
    struct Assets const* assets,
    enum AssetKind kind);

/** Loads started for `kind` since the table was made (ASSET_KIND_COUNT for
 *  all): what a test counts to see a load was not duplicated. */
uint32_t
Assets_LoadsStarted(
    struct Assets const* assets,
    enum AssetKind kind);

/** ASSET_KIND_SOUND's deadline: a sound more than ten client cycles of 20 ms
 *  late is dropped (the reference's delays[i] >= -10). */
#define ASSETS_SOUND_LATE_LIMIT_MS 200

/** The ceilings: reaching either is an assert. */
#define ASSETS_ENTRIES_MAX 65536
#define ASSETS_WAITERS_MAX 65536

#endif /* SRC_ENGINE_ASSET_REQUESTS_H */
