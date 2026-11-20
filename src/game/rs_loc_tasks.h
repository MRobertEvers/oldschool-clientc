#ifndef SRC_GAME_RS_LOC_TASKS_H
#define SRC_GAME_RS_LOC_TASKS_H

/*
 * THE LOC TASKS: a loc change and a loc merge, ONE copy of the rules, run on
 * both clients' parallel queues.
 *
 * These are the two loc kinds whose apply writes GAME STATE -- what a test, a
 * route or a later packet observes: the scenery, its collision and the
 * world's loc change records. So they are not the real client's alone: the
 * real client (app/app_tasks_locs.c) and the headless client
 * (scriptrun/scriptrun_tasks.c) each add them to their own parallel queue,
 * list them in their own pending index and answer their asset waits from
 * their own assets table. What differs is what the asset waits cost (the
 * real client reads the cache; the headless client's loaders answer at once,
 * so a change applies in the pump straight after its packet) and what
 * "place" draws (the real client builds a scene element and runs the loc's
 * add trigger; the headless client stamps its collision map and its loc
 * table).
 *
 * TASK_LOCCHANGE (LOC_ADD_CHANGE / LOC_DEL, a varp re-place, an editor edit,
 * the re-place after a rebuild, a merge's restore). ONE pending change per
 * view, tile, CACHE level and layer: a later change for the same key cancels
 * the pending one -- a change or a merge -- and takes its place, as the deob
 * keeps one pending spawn per key and overwrites it (Statics.method5669).
 * Its body, top to bottom:
 *   1. the loc's config chain, one record at a time (each names the next by
 *      the varps), then every model the resolved child names and its
 *      sequence, together (the reference's changeLocAvailable gate, deob
 *      locChangeDoQueue); a delete loads nothing;
 *   2. cancelled? (replaced, or its scope went);
 *   3. its view's scene mid-rebuild waits for the rebuild (the rebuild's
 *      finish carries it over or drops it by cancelling it); a scene that is
 *      not the one it was made in drops it; a loc whose own record could not
 *      be served is dropped, said;
 *   4. place.
 *
 * TASK_LOCMERGE (LOC_MERGE): a hide with a time to apply and a time to
 * restore, counted on the client cycle for the view it was sent to
 * (Client-TS locChangeCreate's t1+1 and t2+1). At the hide's time the loc in
 * its layer goes (a delete, collision with it; its record says it is a
 * merge's, so a rebuild does not re-place it); at the restore's time the loc
 * the key's FIRST change replaced comes back, as a loc change like any
 * other. A scene that went in between drops it (the deob unlinks every
 * pending spawn that is not permanent, client.method2278).
 *
 * WHAT THE CLIENT SUPPLIES (struct RS_LocHost): where the tasks live (its
 * queue, index, assets table and cycle clock), its config records and varps,
 * whether a view's scene is still the one a task was made in, and the place
 * itself. Every call is synchronous.
 *
 * App-free: nothing here names struct App, a scene element, a model or a
 * cache provider.
 */

#include "game/rs_parallel_task.h"
#include "game/rs_pending.h"

#include <stdbool.h>
#include <stdint.h>

struct Assets;
struct RS_Pending;
struct ToriRS_Clock;
struct ToriRS_Location;
struct ToriRS_TaskQueue;
struct ToriRS_Trigger;
struct VarPManager;
struct World;

/* The kinds, the first of the locs family's range (game/rs_pending.h). The
 * real client's own loc kinds follow from RS_LOC_TASKS_END. */
enum RS_LocTaskKind
{
    RS_LOC_TASK_CHANGE = RS_PENDING_KINDS_LOCS_FIRST,
    RS_LOC_TASK_MERGE,
    RS_LOC_TASKS_END
};

/* How deep a multiloc chain a change follows before it stops. */
enum
{
    RS_LOC_RESOLVE_DEPTH = 16
};

/* What a loc change places, copied from its packet: the scene tile of its
 * view and the CACHE level (a loc packet's bridge climb, zone_loc_tile_at),
 * the loc (-1: a delete), its shape and angle, and the placement's own menu
 * (LOC_ADD_CHANGE_V2: the 5-bit shown mask, a replacement label per slot,
 * "" = the loctype's). */
struct RS_LocChange
{
    int x;
    int z;
    int level;
    int loc_id;
    int shape;
    int angle;
    int op_flags;
    char ops[5][32];
};

/*
 * The sequence a PENDING change carries onto the loc it places -- the deob's
 * class69.field666 -- in place of the loctype's own `anim=`, written by a
 * LOC_ANIM for its tile and layer. Two slots, because the deob's one is
 * written under a condition that cannot be judged until the loc config has
 * loaded: a LOC_ANIM that found nothing standing always writes it, one that
 * found a loc only when the NEW loctype defers (`unfound_*` is the last of
 * the first kind, `any_*` the last of either). `take_*`: a LOC_ANIM read a
 * server tick or more after the change, which the rev-239 client had built by
 * then and animated outright.
 */
struct RS_LocCarry
{
    int unfound_seq;
    int unfound_set;
    int any_seq;
    int any_set;
    int take_seq;
    int take_set;
};

struct RS_LocHostOps;

/* The client's half, as a task carries it: its calls, the client they are
 * made on (handed back to every call), and where its tasks live -- its
 * pending index, assets table, parallel queue and cycle clock, which live as
 * long as the client. By value in every task, so a task needs nothing else to
 * run. */
struct RS_LocHost
{
    struct RS_LocHostOps const* ops;
    void* ctx;
    struct RS_Pending* pending;
    struct Assets* assets;
    struct ToriRS_TaskQueue* queue;
    struct ToriRS_Clock* clock;
};

/* A loc change: one per view, tile, level and layer, the newest replacing
 * the one before. Its run function is rs_loc_tasks.c's. */
struct Task_LocChange
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct RS_LocHost host;
    struct RS_LocChange change;
    /* Its tile in absolute tiles, for a rebuild's carry-over. */
    int abs_x;
    int abs_z;
    /* The client's server tick serial when its packet was read. */
    unsigned tick;
    /* A change a packet made (LOC_ADD_CHANGE, LOC_DEL, a zone reset's
     * restore, an editor edit), which the quest driver reads as a row before
     * the scene shows it -- not a varp's, a merge's or a rebuild's re-place
     * of what already stood. */
    int drive_row;
    /* Carried over a rebuild while it was pending. */
    int carried;
    /* The chain: the record being loaded, how deep, what the change's own
     * record says (its `anim=` and deferanimchange), and the sequence the
     * resolved child plays. */
    int resolved_id;
    int resolve_depth;
    int base_seq;
    int defers_anim;
    int seq_id;
    struct RS_LocCarry carry;
};

/* The client's half: every call takes the host's `ctx`. */
struct RS_LocHostOps
{
    /* ---- the client's clocks, and adding a task ---- */
    /* The client cycle now (what the clock is advanced to). */
    uint32_t (*cycle)(void* ctx);
    /* The server tick serial now (a task's `tick`). */
    unsigned (*tick)(void* ctx);
    /* Add `task` to the parallel queue, `par` listed as `kind` for `key` in
     * `view` (RS_ParallelTask_Add, with the view's scene serial and the
     * arrival cycle now). */
    void (*add)(
        void* ctx,
        struct ToriRS_Task* task,
        struct RS_ParallelTask* par,
        int kind,
        struct RS_PendingKey key,
        int view);
    /* The one call a task's free makes (RS_ParallelTask_Release). */
    void (*release)(
        void* ctx,
        struct RS_ParallelTask* par);
    /* Something pending changed (added, cancelled, ended): a reader that
     * caches the pending set may stale it. NULL: nobody caches. */
    void (*changed)(void* ctx);
    /* A view's generation in a key: what changes when its slot is reused. */
    uint32_t (*view_gen)(
        void* ctx,
        int view);
    /* The world a view's locs and records are kept in; NULL when it has
     * none. */
    struct World* (*view_world)(
        void* ctx,
        int view);
    /* The CACHE level a loc packet for scene tile (`x`, `z`) of `view` on
     * walked plane `zone_level` means: one above on a bridge column
     * (zone_loc_tile_at's climb, World_LocCacheLevel). */
    int (*cache_level)(
        void* ctx,
        int view,
        int x,
        int z,
        int zone_level);

    /* ---- what a change reads ---- */
    /* Loc config `loc_id`, or NULL when the cache could not serve it. */
    struct ToriRS_Location* (*location)(
        void* ctx,
        int loc_id);
    struct VarPManager const* (*varps)(void* ctx);

    /* ---- still wanted? ---- */
    /* The view's built trigger (primed while a rebuild of it is under way,
     * or it awaits its first scene), NULL for a view that is gone. The op
     * NULL, with `scene_building`: a client that builds a view's scene at
     * its packet, so nothing waits for one. */
    struct ToriRS_Trigger* (*built_trigger)(
        void* ctx,
        int view);
    /* Is the scene the task was made in being rebuilt right now? */
    bool (*scene_building)(
        void* ctx,
        struct RS_ParallelTask const* par);
    /* The world of the task's view when it is the scene the task was made in
     * and it is built, NULL otherwise (the drop is said, naming `owner`). */
    struct World* (*world)(
        void* ctx,
        struct RS_ParallelTask const* par,
        char const* owner);

    /* ---- apply ---- */
    /* Place `c` (or remove the loc in its layer, for a delete) in `world`
     * now: the record (World_LocChangePushOps; `end_time` >= 0 marks a
     * merge's), the scenery and its collision, and whatever the client draws.
     * `carried`: a LOC_ANIM's sequence the loc starts on. */
    void (*place)(
        void* ctx,
        int view,
        struct World* world,
        struct RS_LocChange const* c,
        int end_time,
        int carried,
        int carried_seq);
    /* A change was placed (a trace). NULL: none. */
    void (*applied)(
        void* ctx,
        struct Task_LocChange const* task,
        int carried,
        int carried_seq);
};

/* ---- keys ----------------------------------------------------------------- */

/** The key a loc task for `view`'s scene tile, CACHE level and shape's layer
 *  is listed under. */
struct RS_PendingKey
RS_LocTasks_Key(
    struct RS_LocHost const* host,
    int view,
    int scene_x,
    int scene_z,
    int level,
    int shape);

/* ---- adding --------------------------------------------------------------- */

/** Fill `c` (the menu: `op_flags`, `ops`). */
void
RS_LocChange_Fill(
    struct RS_LocChange* c,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int op_flags,
    const char ops[5][32]);

/**
 * Add a change for live view `view` as `c` says, carrying `carry` (NULL:
 * none), read on server tick `tick`, a driver row or not. Replaces whatever
 * is pending for its key (a change or a merge). Its absolute tile is taken
 * from the view's world now (host->view_world).
 */
struct Task_LocChange*
RS_LocChange_Add(
    struct RS_LocHost const* host,
    int view,
    struct RS_LocChange const* c,
    struct RS_LocCarry const* carry,
    unsigned tick,
    int drive_row);

/**
 * Add a merge for `view`: hide the loc in `shape`'s layer at its scene tile
 * and CACHE level `start_cycle` + 1 client cycles from now, and put the
 * key's first replaced loc back `end_cycle` + 1 from now. Replaces whatever
 * is pending for its key.
 */
void
RS_LocMerge_Add(
    struct RS_LocHost const* host,
    int view,
    int scene_x,
    int scene_z,
    int level,
    int shape,
    int start_cycle,
    int end_cycle);

/* ---- finding and cancelling ------------------------------------------------ */

/** The change pending for the key, or NULL. */
struct Task_LocChange*
RS_LocChange_Pending(
    struct RS_LocHost const* host,
    int view,
    int scene_x,
    int scene_z,
    int level,
    int layer);

/** Is a merge pending for the key? */
bool
RS_LocMerge_Pending(
    struct RS_LocHost const* host,
    int view,
    int scene_x,
    int scene_z,
    int level,
    int layer);

/** Every pending change of `view` (cancelled ones skipped), in arrival order:
 *  the parallel queue holds them in the order they were added. Writes up to
 *  `cap`; returns how many there are. */
int
RS_LocChange_OfView(
    struct RS_LocHost const* host,
    int view,
    struct Task_LocChange** out,
    int cap);

/** Cancel what is pending for the key (a change or a merge). Returns how
 *  many. */
int
RS_LocTasks_CancelKey(
    struct RS_LocHost const* host,
    struct RS_PendingKey key);

/**
 * UPDATE_ZONE_FULL_FOLLOWS: cancel every change and merge pending for the
 * zone at scene tile (`base_x`, `base_z`) of `view` on walked plane
 * `zone_level` (each tile's key at the level a loc packet means there,
 * host->cache_level). Returns how many.
 */
int
RS_LocTasks_CancelZone(
    struct RS_LocHost const* host,
    int view,
    int base_x,
    int base_z,
    int zone_level);

/** A logout, a replay's reset: cancel every change and merge. */
void
RS_LocTasks_CancelAll(struct RS_LocHost const* host);

/* ---- a zone's reset (UPDATE_ZONE_FULL_FOLLOWS) ------------------------------ */

/*
 * UPDATE_ZONE_FULL_FOLLOWS resets the client's memory of the zone's LOCS: every
 * loc this client changed inside the 8x8, on the header's plane, goes back to
 * what the map put there, and the state the server writes behind the header
 * re-applies whatever still differs. The state describes only what differs
 * from the map, so a loc that changed AND CHANGED BACK while the client was not
 * told about the zone is described by nothing, and without the reset the client
 * keeps whatever it last drew (the Miscellania castle gate, 2510,3860).
 *
 * The rule, ONE copy for both clients: a key's record holds the map's loc as
 * its old one (World_LocChangePushOps keeps one record per key, its FIRST
 * change's old loc). Each record in the zone, at the cache level a loc packet
 * means on its tile, is rewritten to say the map's loc stands (old = new, no
 * timing), and where the record's change left something else the map's loc is
 * put back by a loc change like any other: it waits for the map loc's models on
 * the real client, a LOC_* behind the header for the same key replaces it, and
 * its apply writes the record it already has. A record with no old loc
 * (old_shape -1: nothing stood) keeps its change's shape, which every record
 * of a key shares, for the delete's layer. A world still loading is being
 * rebuilt from the map, so its zone already IS the map's: its records in the
 * zone are dropped and nothing is put back.
 */

/* At most one record per key: 8 x 8 tiles, 4 layers. */
#define RS_LOC_ZONE_KEYS (8 * 8 * 4)

/**
 * The rule's records half, for a client with no loc tasks to put the map's
 * loc back with (the headless client's shadow): rewrite `world`'s records in
 * the zone at its scene tile (`base_x`, `base_z`), `levels[dx][dz]` the cache
 * level of tile (base_x + dx, base_z + dz) on the header's plane, as above --
 * dropped when the view's scene is not `built` (still loading) -- and write
 * the changes that put the map's locs back to `out`. Returns how many.
 */
int
RS_LocZone_ResetRecords(
    struct World* world,
    int base_x,
    int base_z,
    int const levels[8][8],
    bool built,
    struct RS_LocChange out[RS_LOC_ZONE_KEYS]);

/**
 * The whole rule for live view `view`'s zone at scene tile (`base_x`,
 * `base_z`) on walked plane `zone_level`, read on server tick `tick`. The
 * view's scene is built when its world says so, or always for a client that
 * builds a scene at its packet (no `built_trigger` op). A built view's
 * pending changes and merges in the zone are cancelled
 * (RS_LocTasks_CancelZone), its records are reset (RS_LocZone_ResetRecords),
 * and each put-back is added as a loc change, a driver row. Returns how many
 * put-backs were added.
 */
int
RS_LocTasks_ZoneReset(
    struct RS_LocHost const* host,
    int view,
    int base_x,
    int base_z,
    int zone_level,
    unsigned tick);

/* ---- the carried sequence --------------------------------------------------- */

/* A LOC_ANIM read while `pending` waits: hand it the sequence the way the deob
 * stores it on a pending spawn. `stood`: a loc stood on the layer and took the
 * sequence itself. */
void
RS_LocChange_Carry(
    struct Task_LocChange* pending,
    int seq_id,
    int stood);

/* A LOC_ANIM aimed at the loc `landed` places, which the deob has already
 * built (read in an earlier server tick): it starts on the sequence whatever
 * its loctype says. */
void
RS_LocChange_Take(
    struct Task_LocChange* landed,
    int seq_id);

/* The sequence a change places its loc on instead of the loctype's `anim=`: a
 * taken one, else the carry the loctype's deferanimchange selects; 0 when it
 * has none. */
int
RS_LocChange_Carried(
    struct Task_LocChange const* task,
    int* out_seq_id);

#endif /* SRC_GAME_RS_LOC_TASKS_H */
