#ifndef SRC_GAME_RS_PENDING_H
#define SRC_GAME_RS_PENDING_H

/*
 * THE PENDING INDEX: how a live parallel task is FOUND by what it is for.
 *
 * A packet handler whose work must not hold the packet queue (it waits on an
 * asset that is only drawn or heard) makes ONE task, carrying its own data,
 * and adds it to the parallel queue (app->frame_runner) as an ordinary task.
 * That task embeds the common header below, and while it is alive the header
 * is listed here. The index holds NO data of its own: it is a list of those
 * headers, and it answers
 *
 *   - is a task of this kind pending for this key? (tests, later packets);
 *   - cancel the task for this key (a newer packet supersedes an older one:
 *     it cancels the older and adds its own);
 *   - cancel every task of a scope (a rebuild's scene, a torn-down view, a
 *     removed entity, a closed interface, a logout's everything).
 *
 * Cancelling sets the header's flag and wakes the task if it is parked
 * (task_trigger.h): it sees the flag at its next check, exits, and is freed
 * by its queue, which unlists it (in one place, the task's free). So a
 * cancelled task is still listed until it has exited, but no query below
 * counts it.
 *
 * A KEY IS NEVER A POINTER. Between the packet and the asset, an entity, a
 * scene element or an interface component can be freed and its slot reused.
 * A key is an id plus a generation (RS_PendingKey_*); the task re-resolves it
 * when it applies and treats "gone" as a normal result.
 *
 * App-free: it knows the task header and nothing else. The task shape that
 * uses it (await, cancelled, late, wait for an earlier task's turn) is
 * game/rs_parallel_task.h; the client's half is app/app_parallel_task.h.
 */

#include "engine/asset_requests.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- kinds: one range per family ----------------------------------------
 *
 * A kind names what a parallel task is for, so a query can ask for it. Kind 0
 * is never a kind. Each family picks its kinds inside its own range, in its
 * own file, so two families cannot pick the same id and neither edits the
 * other's lines. A range is 32 ids; a family that needs more takes the spare
 * range at the end and says so here.
 */
enum RS_PendingKindRange
{
    /* Reserved for the tests (game/test/rs_pending_test.c,
     * game/test/rs_parallel_task_test.c). */
    RS_PENDING_KINDS_TEST_FIRST = 1,
    RS_PENDING_KINDS_TEST_LAST = 31,
    /* app/app_tasks_ground.c: ground items and widget icons. */
    RS_PENDING_KINDS_GROUND_FIRST = 32,
    RS_PENDING_KINDS_GROUND_LAST = 63,
    /* app/app_tasks_effects.c: projectiles and spot animations. */
    RS_PENDING_KINDS_EFFECTS_FIRST = 64,
    RS_PENDING_KINDS_EFFECTS_LAST = 95,
    /* app/app_tasks_locs.c: loc changes, loc animations, merges, zone resets,
     * zone packets for unbuilt views. */
    RS_PENDING_KINDS_LOCS_FIRST = 96,
    RS_PENDING_KINDS_LOCS_LAST = 127,
    /* app/app_tasks_entities.c: entity bodies and animations. */
    RS_PENDING_KINDS_ENTITIES_FIRST = 128,
    RS_PENDING_KINDS_ENTITIES_LAST = 159,
    /* app/app_tasks_if_assets.c: interface models, chatheads, inventory
     * icons. */
    RS_PENDING_KINDS_IF_ASSETS_FIRST = 160,
    RS_PENDING_KINDS_IF_ASSETS_LAST = 191,
    /* app/app_tasks_mounts.c: the visual parts of interface mounts, player
     * models. */
    RS_PENDING_KINDS_MOUNTS_FIRST = 192,
    RS_PENDING_KINDS_MOUNTS_LAST = 223,
    /* app/app_tasks_scripts.c: visual script operations. */
    RS_PENDING_KINDS_SCRIPTS_FIRST = 224,
    RS_PENDING_KINDS_SCRIPTS_LAST = 255,
    /* The scene's own work, listed so a parallel task can wait on it by
     * name (rs_parallel_task.h, PT_PARALLEL_AWAIT_REBUILD). */
    RS_PENDING_KINDS_WORLD_FIRST = 256,
    RS_PENDING_KINDS_WORLD_LAST = 287,
    /* The App's own asset tasks that belong to no packet family
     * (app/app_textures.c: a texture published into the scene). */
    RS_PENDING_KINDS_ASSETS_FIRST = 288,
    RS_PENDING_KINDS_ASSETS_LAST = 319,

    RS_PENDING_KIND_MAX = 320
};

/* A view's scene being rebuilt: the background world job (app_world_job.c),
 * a blocking REBUILD inside its packet task (task_gameproto_exec.c), and a
 * local or editor load (app_world_load.c). Keyed by the view
 * (RS_PendingKey_View with tile 0,0, level 0, layer 0 and the view's
 * generation), listed from the rebuild's start to its finish. */
enum
{
    RS_PENDING_KIND_REBUILD = RS_PENDING_KINDS_WORLD_FIRST,
};

/* ---- keys ---------------------------------------------------------------- */

enum RS_PendingKeyShape
{
    RS_PENDING_KEY_NONE = 0,
    /* An entity: `sub` = RS_PENDING_ENTITY_NPC / _PLAYER, `id` = its slot (the
     * server index), `gen` = its spawn serial. */
    RS_PENDING_KEY_ENTITY,
    /* A place in a view: `id` = the view, `x`/`z` = the tile, `level`,
     * `layer` (a loc's layer, or a ground stack's 0), `gen` = the view's
     * generation (what changes when the view's slot is reused). */
    RS_PENDING_KEY_VIEW,
    /* A scripted projectile slot: `id` = the slot, `gen` = its serial. */
    RS_PENDING_KEY_PROJECTILE,
    /* An interface component: `id` = the component id (group << 16 | child),
     * `gen` = its interface's close serial. */
    RS_PENDING_KEY_COMPONENT,
    /* One thing of its kind, by an id that is never reused for something
     * else (a song slot, a skybox): `id`, `gen` = whatever generation the
     * owner keeps (0 if none). */
    RS_PENDING_KEY_ID,
};

enum RS_PendingEntityType
{
    RS_PENDING_ENTITY_NPC = 0,
    RS_PENDING_ENTITY_PLAYER = 1,
};

/* Compared field by field: two keys are the same target when every field is
 * equal. Always made with a constructor, which zeroes what a shape does not
 * use. */
struct RS_PendingKey
{
    uint8_t shape; /* enum RS_PendingKeyShape */
    uint8_t sub;   /* RS_PENDING_KEY_ENTITY: enum RS_PendingEntityType */
    uint8_t level;
    uint8_t layer;
    int32_t id;
    int32_t x;
    int32_t z;
    uint32_t gen;
};

static inline struct RS_PendingKey
RS_PendingKey_Entity(
    enum RS_PendingEntityType type,
    int slot,
    uint32_t spawn_serial)
{
    struct RS_PendingKey k = { 0 };
    k.shape = RS_PENDING_KEY_ENTITY;
    k.sub = (uint8_t)type;
    k.id = slot;
    k.gen = spawn_serial;
    return k;
}

static inline struct RS_PendingKey
RS_PendingKey_View(
    int view,
    int tile_x,
    int tile_z,
    int level,
    int layer,
    uint32_t view_gen)
{
    struct RS_PendingKey k = { 0 };
    k.shape = RS_PENDING_KEY_VIEW;
    k.id = view;
    k.x = tile_x;
    k.z = tile_z;
    k.level = (uint8_t)level;
    k.layer = (uint8_t)layer;
    k.gen = view_gen;
    return k;
}

static inline struct RS_PendingKey
RS_PendingKey_Projectile(
    int slot,
    uint32_t serial)
{
    struct RS_PendingKey k = { 0 };
    k.shape = RS_PENDING_KEY_PROJECTILE;
    k.id = slot;
    k.gen = serial;
    return k;
}

static inline struct RS_PendingKey
RS_PendingKey_Component(
    int component_id,
    uint32_t close_serial)
{
    struct RS_PendingKey k = { 0 };
    k.shape = RS_PENDING_KEY_COMPONENT;
    k.id = component_id;
    k.gen = close_serial;
    return k;
}

static inline struct RS_PendingKey
RS_PendingKey_Id(
    int id,
    uint32_t gen)
{
    struct RS_PendingKey k = { 0 };
    k.shape = RS_PENDING_KEY_ID;
    k.id = id;
    k.gen = gen;
    return k;
}

static inline bool
RS_PendingKey_Equal(
    struct RS_PendingKey a,
    struct RS_PendingKey b)
{
    return a.shape == b.shape && a.sub == b.sub && a.level == b.level && a.layer == b.layer &&
           a.id == b.id && a.x == b.x && a.z == b.z && a.gen == b.gen;
}

/* ---- the common header every parallel task embeds ------------------------ */

struct ToriRS_Task;

struct RS_ParallelTask
{
    /* What it is for: a kind from its family's range and the key of its
     * target. */
    uint16_t kind;
    /* Set by a cancel; the task exits at its next check. */
    uint8_t cancelled;
    /* Listed in an index (between add and free). */
    uint8_t listed;
    /* Parked on its view's rebuild in the current wait for it
     * (PT_APP_AWAIT_VIEW_REBUILT): what the wake's trace line keys on. */
    uint8_t rebuild_parked;
    struct RS_PendingKey key;
    /* The view it belongs to (-1: none, an interface's or a song's) and that
     * view's scene serial when it was made (World.load_seq; 0 with no view,
     * or a view with no world): "is my scene still live" compares both. */
    int32_t view;
    uint32_t scene_serial;
    /* The client cycle its packet arrived on: delays and lifetimes count from
     * arrival, not from when the asset came. */
    uint32_t arrived_cycle;
    /* Listing order: unique and increasing for the index's life (compared as
     * a signed difference, so it holds across the wrap). Two tasks for one
     * key apply in this order (RS_Pending_EarlierPending). */
    uint32_t order;
    /* The asset wait: the join count Assets_AwaitOutcome counts on, and how
     * the last awaited attempt ended. */
    int join;
    enum AssetOutcome outcome;
    /* The task this header is embedded in (RS_ParallelTask_Add sets it):
     * what a cancel wakes, and whose end a later task for the key waits on.
     * NULL for a header listed by hand (a rebuild's). */
    struct ToriRS_Task* task;

    /* The index's links: every listed header, and its key's bucket. */
    struct RS_ParallelTask* prev;
    struct RS_ParallelTask* next;
    struct RS_ParallelTask* bucket_prev;
    struct RS_ParallelTask* bucket_next;
};

/* ---- the index ----------------------------------------------------------- */

struct RS_Pending;

/** An empty index. */
struct RS_Pending*
RS_Pending_New(void);

/** NULL is accepted. Nothing may still be listed: every task unlists itself
 *  when it is freed, so the queues that hold them are freed first. */
void
RS_Pending_Free(struct RS_Pending* index);

/**
 * List `task` (a header not listed anywhere) as a pending `kind` (> 0, below
 * RS_PENDING_KIND_MAX) for `key`. Fills every field of the header: not
 * cancelled, join 0, the given view, scene serial and arrival cycle, and the
 * next order.
 */
void
RS_Pending_List(
    struct RS_Pending* index,
    struct RS_ParallelTask* task,
    int kind,
    struct RS_PendingKey key,
    int view,
    uint32_t scene_serial,
    uint32_t arrived_cycle);

/** Take `task` (listed in `index`) out. The one call a task's free makes. */
void
RS_Pending_Unlist(
    struct RS_Pending* index,
    struct RS_ParallelTask* task);

/** The newest task of `kind` for `key` that is not cancelled; NULL when none
 *  is pending. Valid until that task is freed. */
struct RS_ParallelTask*
RS_Pending_Find(
    struct RS_Pending const* index,
    int kind,
    struct RS_PendingKey key);

/** Cancel every task of `kind` for `key` (and wake each that is parked).
 *  Returns how many it cancelled (not counting those already cancelled). */
int
RS_Pending_CancelKey(
    struct RS_Pending* index,
    int kind,
    struct RS_PendingKey key);

/**
 * Is a task of `task`'s kind for its key, listed before it and not cancelled,
 * still pending? A task whose work must apply after an earlier one's for the
 * same key waits while this is true (PT_PARALLEL_AWAIT_TURN).
 */
bool
RS_Pending_EarlierPending(
    struct RS_Pending const* index,
    struct RS_ParallelTask const* task);

/** The latest of those earlier tasks (the one listed last before `task`), or
 *  NULL when none is pending: the one whose end a turn waits for. */
struct RS_ParallelTask*
RS_Pending_EarlierTask(
    struct RS_Pending const* index,
    struct RS_ParallelTask const* task);

/* ---- scopes -------------------------------------------------------------- */

enum RS_PendingScopeKind
{
    /* Everything (a logout). */
    RS_PENDING_SCOPE_ALL = 0,
    /* Tasks made in view `view`'s scene serial `serial` (that scene was
     * rebuilt). */
    RS_PENDING_SCOPE_SCENE,
    /* Tasks of view `view`, any scene serial (the view was torn down). */
    RS_PENDING_SCOPE_VIEW,
    /* Tasks keyed on entity (`sub`, `id`), any spawn serial (it was
     * removed). */
    RS_PENDING_SCOPE_ENTITY,
    /* Tasks keyed on a component of interface group `id` (it was closed). */
    RS_PENDING_SCOPE_GROUP,
    /* Every task of kind `id`. */
    RS_PENDING_SCOPE_KIND,
};

struct RS_PendingScope
{
    enum RS_PendingScopeKind kind;
    int32_t id;
    uint8_t sub;
    int32_t view;
    uint32_t serial;
};

static inline struct RS_PendingScope
RS_PendingScope_All(void)
{
    struct RS_PendingScope s = { RS_PENDING_SCOPE_ALL, 0, 0, -1, 0 };
    return s;
}

static inline struct RS_PendingScope
RS_PendingScope_Scene(
    int view,
    uint32_t scene_serial)
{
    struct RS_PendingScope s = { RS_PENDING_SCOPE_SCENE, 0, 0, view, scene_serial };
    return s;
}

static inline struct RS_PendingScope
RS_PendingScope_View(int view)
{
    struct RS_PendingScope s = { RS_PENDING_SCOPE_VIEW, 0, 0, view, 0 };
    return s;
}

static inline struct RS_PendingScope
RS_PendingScope_Entity(
    enum RS_PendingEntityType type,
    int slot)
{
    struct RS_PendingScope s = { RS_PENDING_SCOPE_ENTITY, slot, (uint8_t)type, -1, 0 };
    return s;
}

static inline struct RS_PendingScope
RS_PendingScope_Group(int group)
{
    struct RS_PendingScope s = { RS_PENDING_SCOPE_GROUP, group, 0, -1, 0 };
    return s;
}

static inline struct RS_PendingScope
RS_PendingScope_Kind(int kind)
{
    struct RS_PendingScope s = { RS_PENDING_SCOPE_KIND, kind, 0, -1, 0 };
    return s;
}

/** Cancel every task in `scope` (and wake each that is parked). Returns how
 *  many it cancelled (not counting those already cancelled). */
int
RS_Pending_Cancel(
    struct RS_Pending* index,
    struct RS_PendingScope scope);

/* ---- counts -------------------------------------------------------------- */

/** Pending (listed, not cancelled) tasks of `kind`; 0 for every kind. */
int
RS_Pending_Count(
    struct RS_Pending const* index,
    int kind);

/** Listed tasks, cancelled ones included (they are listed until they exit). */
int
RS_Pending_Listed(struct RS_Pending const* index);

#endif /* SRC_GAME_RS_PENDING_H */
