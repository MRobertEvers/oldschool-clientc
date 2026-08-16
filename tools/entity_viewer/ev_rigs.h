#ifndef EV_RIGS_H
#define EV_RIGS_H

/*
 * Which animations apply to which npc, computed from the cache itself.
 *
 * ## Why this exists
 *
 * The rig matching used to come only from `ev_catalog`'s CSVs, loaded once at
 * startup from `--catalog`. That made the animation lists a property of ONE
 * cache: switching to another dropped the catalog (its npc ids mean something
 * else there) and nothing replaced it, so every cache that was not the one the
 * server booted with listed no animations at all. On rs634 and rs727 — which
 * have no catalog anywhere — that was every session.
 *
 * The catalog is not what made this slow. Its five minutes are the *model*
 * decode it does for every one of 16,292 npcs, to answer `animaya_skinned` and
 * `strict_covers`. The rig walk itself — sweep every sequence to its framemap,
 * unify the rigs that are byte-identical, then read each npc's seed sequences —
 * is 1.8 s on rs634 and 8.8 s on rs727, which is short enough to redo on every
 * cache switch and far too long to block the accept loop. So it runs on a
 * worker thread with its own cache handle, and the page polls for it.
 *
 * What is deliberately NOT here: `animaya_skinned` per npc, because it needs
 * that model decode. It is answered for the one npc being looked at, where it
 * costs a few milliseconds instead of five minutes (see ev_server.c).
 *
 * ## Two publishes, because the two halves are worth very different waits
 *
 * The sequence sweep is about a second of the walk; the npc sweep is the rest.
 * But looking at ONE npc only needs the sequence half — its own rigs come from
 * its own record, which is a single decode. So the walk publishes as soon as
 * the sequences are indexed, and again when the npc pass finishes. Between the
 * two, `npcs_complete` is 0: the animation panel works and the npc list's match
 * counts are not there yet.
 *
 * ## What a match means
 *
 * A frame addresses bones by index into a framemap (the rig). A sequence built
 * against one rig, applied to a model skinned for another, moves the wrong
 * vertices — so sharing a rig is the hard precondition for an animation
 * applying at all. This is a *possibility* set, not a fact about what the game
 * plays: framemap 0 is the shared human rig and thousands of sequences use it.
 * For a boss with its own rig it returns a handful, and those are very nearly
 * its complete animation set.
 */

#include "asset_access.h"

/*
 * Rigs per npc.
 *
 * An npc reaches a rig through its seed sequences (idle, walk, the turn/run/
 * crawl variants, and an RS2 BasType's own set). A handful is the norm; the
 * cap is a ceiling on pathological records rather than a real limit.
 */
#define EV_RIG_MAX_FRAMEMAPS 16

struct EV_RigSeq
{
    int seq_id;
    int framemap_id; /* canonicalised; -1 when the sequence names no rig */
    int frame_count;
    /** Reached through the sequence's Animaya curve set rather than a frame
     *  list. Same rig id space either way; playing one needs a model with an
     *  Animaya skin, which this index does not know — ev_server answers that
     *  for the selected npc. */
    int skeletal;
};

struct EV_RigNpc
{
    int npc_id;
    int framemaps[EV_RIG_MAX_FRAMEMAPS];
    int framemap_count;
    /** How many sequences sit on those rigs, and how many of them are skeletal.
     *  Precomputed because the npc list shows it for every row. */
    int seq_count;
    int skeletal_count;
};

struct EV_RigIndex
{
    struct EV_RigSeq* seqs;
    int seq_count;

    /** Ascending by npc id, so a lookup is a binary search. Empty until the npc
     *  pass finishes; ev_rigs_npc_lookup answers from the cache meanwhile. */
    struct EV_RigNpc* npcs;
    int npc_count;
    int npcs_complete;

    /* Lookup tables, kept rather than freed with the walk: the on-demand path
     * asks the same questions as the npc pass and must answer them the same
     * way. seq id -> rig, and per rig how many sequences sit on it. */
    int* seq_framemap;
    int max_seq_id;
    int* fm_seq_count;
    int* fm_skeletal_count;
    int max_framemap;

    int distinct_rigs;
    int alias_ids; /* framemap ids folded into another identical rig */
    int build_ms;  /* the sequence half; the npc pass adds to it */
};

/* ---- building ------------------------------------------------------------ */

enum EV_RigStage
{
    EV_RIG_STAGE_SEQS = 0, /* sweeping every sequence to its framemap */
    EV_RIG_STAGE_RIGS,     /* unifying framemaps that decode to the same rig */
    EV_RIG_STAGE_NPCS,     /* reading each npc's seed sequences */
    EV_RIG_STAGE_DONE,
};

/**
 * Progress and abandonment, in one callback.
 *
 * Returns 0 to abandon the build — which is what a second cache switch does to
 * the first one's walk. `done`/`total` are 0 outside the npc stage: the two
 * sweeps before it are single library calls with no seam to report from, and
 * claiming a percentage for them would be an invention.
 */
typedef int (*EV_RigProgressFn)(void* userdata, enum EV_RigStage stage, int done, int total);

/**
 * The sequence half: every sequence in `cache` mapped to its canonical rig.
 *
 * Returns NULL when the cache has no sequence table to sweep. The result is
 * usable on its own — ev_rigs_npc_lookup answers any single npc from it.
 */
struct EV_RigIndex*
ev_rigs_build_seqs(struct Tool_Dat2Cache* cache);

/**
 * The npc half: fill `index->npcs` by reading every npc's seed sequences.
 *
 * Returns 0 if `progress` abandoned it, leaving `npcs_complete` at 0.
 */
int
ev_rigs_build_npcs(
    struct Tool_Dat2Cache* cache,
    struct EV_RigIndex* index,
    EV_RigProgressFn progress,
    void* userdata);

/** Both halves, for callers that can block (probes, the command line). */
struct EV_RigIndex*
ev_rigs_build(
    struct Tool_Dat2Cache* cache,
    EV_RigProgressFn progress,
    void* userdata);

/** A deep copy, so a partial result can be handed out while the original keeps
 *  being filled in. */
struct EV_RigIndex*
ev_rigs_clone(const struct EV_RigIndex* index);

void
ev_rigs_free(struct EV_RigIndex* index);

/* ---- queries ------------------------------------------------------------- */

/**
 * One npc's rigs and match counts, from the npc pass if it has run and straight
 * from `cache` if it has not.
 *
 * `cache` may be NULL, in which case only the precomputed rows can answer. The
 * two paths share their arithmetic on purpose: a badge that disagrees with the
 * list it labels is worse than either being absent.
 *
 * Returns 0 when the cache has no such npc.
 */
int
ev_rigs_npc_lookup(
    const struct EV_RigIndex* index,
    struct Tool_Dat2Cache* cache,
    int npc_id,
    struct EV_RigNpc* out);

/** The precomputed row, or NULL if the npc pass has not reached it. */
const struct EV_RigNpc*
ev_rigs_npc(const struct EV_RigIndex* index, int npc_id);

/** The rig a sequence is built on, or -1. */
int
ev_rigs_seq_framemap(const struct EV_RigIndex* index, int seq_id);

/* ---- the background build ------------------------------------------------ */

enum EV_RigState
{
    EV_RIG_IDLE = 0,
    EV_RIG_BUILDING,
    EV_RIG_READY,
    EV_RIG_FAILED,
};

struct EV_RigStatus
{
    enum EV_RigState state;
    enum EV_RigStage stage;
    int done;
    int total;
    int ms; /* how long the finished walk took */
};

/**
 * Start a rig walk for `cache_dir` read as `rev`, on a worker thread.
 *
 * The worker opens its OWN cache handle and never touches the server's: two
 * read-only handles on one directory are independent, where sharing one would
 * mean every request racing the walk over the same decode memos.
 *
 * A walk already in flight is abandoned — it finds out at its next progress
 * callback and frees its own work. Its result can never be published, because
 * a build is only accepted when it is still the current generation.
 */
void
ev_rigs_start(const char* cache_dir, const char* rev);

/**
 * Take delivery of a finished walk: pending becomes current, and the index it
 * replaces is freed.
 *
 * Main thread, and between requests rather than inside one — that is the whole
 * point of separating it from ev_rigs_current. A getter that also freed would
 * be able to pull an index out from under a handler that was still reading it.
 */
void
ev_rigs_collect(void);

/**
 * The index for the cache the last ev_rigs_start named, or NULL while its
 * sequence sweep is still running.
 *
 * A pure read: the pointer stays valid until the next ev_rigs_collect or
 * ev_rigs_start, so for the length of one request it cannot move.
 */
const struct EV_RigIndex*
ev_rigs_current(void);

void
ev_rigs_status(struct EV_RigStatus* out);

#endif
