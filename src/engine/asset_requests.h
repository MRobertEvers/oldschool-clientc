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
 * Two ways to ask, and no third: there is no callback.
 *
 *  - Assets_Await(key, &join): the calling task counts the load on one of
 *    its join fields and waits with PT_TASK_JOIN. The count drops when the
 *    attempt in flight ENDS, landed or not -- a task waiting on a load must
 *    always be released. Assets_AwaitOutcome also writes, when the attempt
 *    ends, whether the asset is here (LANDED), the attempt failed (FAILED:
 *    Assets_State then says whether the table goes back for it), or the
 *    attempt was abandoned or replaced (DROPPED). Which queue the task is on
 *    is what makes the wait REQUIRED (the packet queue, a client script) or
 *    VISUAL (a parallel task on the parallel queue, app/app_parallel_task.h);
 *    the call is the same.
 *  - Assets_Want(key): start loading; nobody waits (a prefetch, or an owner
 *    that asks residency itself when it next looks).
 *
 * What happens when an asset arrives is written in the body of the task that
 * awaited it, never in a function the table calls: the ground-stack model,
 * the npc body, the deferred sequence bind, the texture publish
 * (app/app_textures.c) and the music install (app/app_tick.c) are each a
 * parallel task that awaits. (The callback waiter this replaced -- a landing
 * function per want, called from a landing pass, with late and landed
 * policies only it read -- is gone, with its last user, the music install.)
 *
 * Either way a loader is started only if none is in flight for the entry. The
 * client supplies one loader per kind (Assets_SetLoader): `start` puts a load
 * on the parallel queue and later reports its end with Assets_Finished (or
 * Assets_Abandoned when the task is freed before it ends). The table does not
 * know about tasks, caches or the App: a test drives it with fake loaders.
 *
 * WHAT DIFFERS PER KIND IS POLICY, stated once in the table in
 * asset_requests.c (Assets_Policy):
 *   - fail:   give up / retry with back-off forever / remember the failure;
 *   - newer:  keep both / a newer request of the kind replaces the older (one
 *             loader of the kind in flight; the older's awaiters are released
 *             DROPPED -- at once, or, while its loader runs, when it ends).
 * How late an arrival may still apply is the awaiting task's own `if` (it
 * knows when its packet came: App_ParallelTaskLate), not the table's.
 *
 * Capacities grow explicitly up to a fixed ceiling; reaching it is an assert,
 * never a silent stop of dedupe.
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
    /* No entries and no loader: its row is the back-off an icon whose model
     * chain did not come waits out before asking again (an inventory slot's
     * Task_InvIcon, a widget icon's Task_WidgetIcon). */
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
    /* A config record an appearance names: id = the record, variant = which
     * config (enum AssetConfigType). */
    ASSET_KIND_CONFIG,
    /* One world map surface region's tiles (table 18 by way of table 19's
     * compositemap): id = CacheProvider_WorldMapGeographyKey, which carries
     * the area. Released once baked, so "here" is the provider's answer. */
    ASSET_KIND_WORLDMAP_GEOGRAPHY,
    /* One map square's terrain archive: id = CacheProvider_MapId(x, z). */
    ASSET_KIND_MAP_TERRAIN,
    /* One map square's scenery (locs) archive: id = CacheProvider_MapId(x, z). */
    ASSET_KIND_MAP_SCENERY,

    ASSET_KIND_COUNT
};

/* ASSET_KIND_CONFIG's variant: the config table the id is in. A kit and an
 * obj share no id space, so the variant is part of the key. */
enum AssetConfigType
{
    ASSET_CONFIG_IDK = 0,
    ASSET_CONFIG_OBJ = 1,
    /* What a world map bake colours its tiles with (game/rs_worldmap_render.c):
     * a floor underlay and a floor overlay (flotype). */
    ASSET_CONFIG_UNDERLAY = 2,
    ASSET_CONFIG_FLOTYPE = 3,
    /* A spot-animation's type: its model and sequence ids. */
    ASSET_CONFIG_SPOTANIM = 4,

    /* What a client script reads the value of and waits for
     * (game/task_cs2_run.c, the yield table; the loaders are
     * app/app_tasks_scripts.c's). A range of their own, from 16. */
    ASSET_CONFIG_SCRIPTS_FIRST = 16,
    ASSET_CONFIG_SCRIPT = ASSET_CONFIG_SCRIPTS_FIRST, /* a client script */
    ASSET_CONFIG_ENUM,
    ASSET_CONFIG_STRUCT,
    ASSET_CONFIG_PARAM,
    ASSET_CONFIG_INVTYPE,
    ASSET_CONFIG_NPC,
    ASSET_CONFIG_LOC, /* also a loc change's multiloc chain (app/app_tasks_locs.c) */
    ASSET_CONFIG_DBROW,
    ASSET_CONFIG_DBTABLE,
    ASSET_CONFIG_DBINDEX, /* a table's index (cache table 21) */
    ASSET_CONFIG_FONT,
    ASSET_CONFIG_MAPELEMENT,
    ASSET_CONFIG_WORLDMAP, /* the whole world map: id 0 */
    ASSET_CONFIG_OBJ_ALL,  /* every objtype, for a name search: id 0 */
    ASSET_CONFIG_SCRIPTS_END,
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
    /* An attempt that comes back without the asset releases its awaiters
     * FAILED and the entry is forgotten. An owner that goes back for it
     * awaits again, counting its own attempts up to `fail_attempts`. */
    ASSET_FAIL_GIVE_UP = 0,
    /* Retry after a wait that doubles from `backoff_first_ms` up to
     * `backoff_max_ms`, for as long as anyone wants it. Never gives up. A
     * want honours the wait; an await does not (a task is waiting). */
    ASSET_FAIL_BACKOFF,
    /* The first failure is final for the session: its awaiters are released
     * FAILED and every later want or await answers FAILED at once. */
    ASSET_FAIL_REMEMBER,
};

enum AssetNewerPolicy
{
    /* Requests of the kind are independent. */
    ASSET_NEWER_KEEP_BOTH = 0,
    /* A want or an await for another asset of the kind replaces every older
     * one: their awaiters are released DROPPED, and the newer load starts
     * when the older loader (which cannot be stopped mid-read) has ended.
     * Asking again for one that was replaced while its loader still runs
     * makes it the newest once more. */
    ASSET_NEWER_REPLACE,
};

struct AssetPolicy
{
    char const* name;
    enum AssetFailPolicy fail;
    int fail_attempts;        /* ASSET_FAIL_GIVE_UP: the owner's count */
    uint32_t backoff_first_ms; /* ASSET_FAIL_BACKOFF */
    uint32_t backoff_max_ms;
    enum AssetNewerPolicy newer;
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

/* ---- loaders ------------------------------------------------------------- */

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

/* How an attempt ended, for Assets_AwaitOutcome. */
enum AssetOutcome
{
    /* Here. */
    ASSET_OUTCOME_LANDED = 0,
    /* This attempt failed (Assets_State: FAILED remembered, NONE given up,
     * WANTED to be retried after its back-off). */
    ASSET_OUTCOME_FAILED,
    /* The attempt was abandoned (its loader freed) or replaced by a newer
     * request of a REPLACE kind; the asset is not here. */
    ASSET_OUTCOME_DROPPED,
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

/** An empty table with no loaders. Entries and joins grow from small. */
struct Assets*
Assets_New(void);

/** NULL is accepted. Joins still counted are dropped untouched (the tasks
 *  that held them were freed with their queues first). */
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

/** The clock the back-off reads, in milliseconds. */
void
Assets_SetClock(
    struct Assets* assets,
    uint64_t now_ms);

uint64_t
Assets_Clock(struct Assets const* assets);

/**
 * Start a load if none is in flight; nobody waits for it. Answers what was
 * found:
 *   HERE    resident; nothing to load;
 *   FAILED  remembered failure;
 *   LOADING / WANTED  an entry is loading, or waiting out a back-off or an
 *           older loader of a REPLACE kind;
 *   NONE    a load that ended at once (its start answered).
 * A want for an entry waiting out a back-off restarts it if the wait is over.
 */
enum AssetState
Assets_Want(
    struct Assets* assets,
    struct AssetKey key);

/**
 * Count the load on `*join` and start one if none is in flight (an entry
 * waiting out a back-off starts now: a task is waiting on it). Returns 1 when
 * counted, 0 when nothing to wait for (here, or a remembered failure).
 * `*join` drops when the attempt in flight ends, landed or not.
 */
int
Assets_Await(
    struct Assets* assets,
    struct AssetKey key,
    int* join);

/**
 * As Assets_Await, and `*outcome` says how it ended once `*join` has dropped
 * for it: LANDED here, FAILED this attempt failed, DROPPED abandoned or
 * replaced. Written at once when nothing is counted (LANDED here, FAILED a
 * remembered failure); DROPPED while the attempt runs. `outcome` lives where
 * `join` does (the awaiting task) and is forgotten with it by Assets_Unjoin.
 * One outcome per await: a task awaiting several assets on one join gives
 * each its own outcome, or asks the cache.
 */
int
Assets_AwaitOutcome(
    struct Assets* assets,
    struct AssetKey key,
    int* join,
    enum AssetOutcome* outcome);

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
 *  failed from now on; an awaiter still counted is released FAILED. Not for a
 *  key whose loader is running. */
void
Assets_Fail(
    struct Assets* assets,
    struct AssetKey key);

/** Retry `key` at once, keeping its back-off wait for the next failure. No-op
 *  when it has no entry. */
void
Assets_Lift(
    struct Assets* assets,
    struct AssetKey key);

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

/** Awaiting tasks' counts (Assets_Await, Assets_AwaitOutcome) still linked to
 *  a load of `kind` (ASSET_KIND_COUNT for all): what a test counts to see a
 *  task that went away was forgotten (Assets_Unjoin), so the load's end
 *  writes nothing into its freed memory. */
int
Assets_JoinCount(
    struct Assets const* assets,
    enum AssetKind kind);

/** Loads started for `kind` since the table was made (ASSET_KIND_COUNT for
 *  all): what a test counts to see a load was not duplicated. */
uint32_t
Assets_LoadsStarted(
    struct Assets const* assets,
    enum AssetKind kind);

/** The ceilings: reaching either is an assert. */
#define ASSETS_ENTRIES_MAX 65536
#define ASSETS_JOINS_MAX 65536

#endif /* SRC_ENGINE_ASSET_REQUESTS_H */
