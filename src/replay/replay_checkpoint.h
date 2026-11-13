#ifndef SRC_REPLAY_REPLAY_CHECKPOINT_H
#define SRC_REPLAY_REPLAY_CHECKPOINT_H

/*
 * Session checkpoints (the replay scrubber design, "Structure 4").
 *
 * A checkpoint is a private, in-memory COPY of the client's session state at
 * a tick boundary: everything a session-class packet (VARP_*, UPDATE_INV_*,
 * UPDATE_STAT, IF_*, RUNCLIENTSCRIPT, MESSAGE_*, CAM_*, SET_PLAYER_OP, friends
 * and clan, MIDI_SONG) or a clientscript one of them runs can change. Putting
 * it back leaves the client as if it had just reached that tick, so a
 * backward seek re-executes from the nearest checkpoint instead of from the
 * login. Same process, same build: arrays are copied and strings duplicated,
 * nothing is serialised.
 *
 * The WORLD (scene, locs, ground items, players, npcs) is not in it: the
 * world ledger (replay/replay_ledger.h) restates it at any tick.
 *
 * Each module that owns session state copies its own (VarPManager_Checkpoint*,
 * UITree_Checkpoint*, RS_CS2Host_Checkpoint*, ...); this module is the table
 * of those domains, in restore order, and the report of what each costs.
 *
 * WHEN. Take and Restore both need a moment at which the session is
 * self-contained: no clientscript yielded, no packet or mount executing, no
 * task that will write session state when it lands. ReplayCheckpoint_CanTake
 * says whether this is one, and names what is in the way when it is not; both
 * Take and Restore assert it.
 */

#include <stddef.h>
#include <stdint.h>

struct App;
struct ReplayCheckpoint;

/* The domains, in the order a restore applies them (the UI tree before the
 * interface stores re-keyed against it). */
enum ReplayCheckpointDomain
{
    REPLAY_CKPT_VARPS = 0,
    REPLAY_CKPT_VARCS,
    REPLAY_CKPT_INVS,
    REPLAY_CKPT_STATS,
    REPLAY_CKPT_SOCIAL,
    REPLAY_CKPT_SLOTS,
    REPLAY_CKPT_IDK,
    REPLAY_CKPT_CHAT,
    REPLAY_CKPT_LOOT,
    REPLAY_CKPT_CS2HOST,
    REPLAY_CKPT_CS2QUEUE,
    REPLAY_CKPT_UITREE,
    REPLAY_CKPT_IFSTORES,
    REPLAY_CKPT_CAMERA,
    REPLAY_CKPT_SCALARS,
    REPLAY_CKPT_MUSIC,
    REPLAY_CKPT_DOMAIN_COUNT
};

/**
 * 1 when the session can be copied or put back now. Else 0, and `*why` (when
 * `why` is not NULL) names the first thing in the way: "a clientscript is
 * yielded on a host request", "a packet or interface mount is executing", ...
 */
int
ReplayCheckpoint_CanTake(
    struct App const* app,
    char const** why);

/** A copy of the session now. Asserts ReplayCheckpoint_CanTake. */
struct ReplayCheckpoint*
ReplayCheckpoint_Take(struct App* app);

/**
 * Put `cp` back. Asserts ReplayCheckpoint_CanTake. Afterwards nothing points
 * into the session state it replaced (the emit list, hover, focus, drag and
 * the minimenu are dropped), every cached UI lookup misses once, and a
 * relayout, a CS1 pass and a redraw are pending. `cp` is not consumed: it can
 * be restored again.
 */
void
ReplayCheckpoint_Restore(
    struct App* app,
    struct ReplayCheckpoint const* cp);

/** Accepts NULL. */
void
ReplayCheckpoint_Free(struct ReplayCheckpoint* cp);

/** What `cp` holds, every domain's copy included. */
size_t
ReplayCheckpoint_Bytes(struct ReplayCheckpoint const* cp);

/**
 * Deep compare. 1 when equal; else 0 and, when `why` is not NULL, the first
 * domain and field that differs ("uitree: node 812 (com 0x02310004 rs_text)
 * u.rs_text").
 */
int
ReplayCheckpoint_Equal(
    struct ReplayCheckpoint const* a,
    struct ReplayCheckpoint const* b,
    char* why,
    int why_cap);

/** ReplayCheckpoint_Equal for one domain; `why` holds the field only. */
int
ReplayCheckpoint_EqualDomain(
    struct ReplayCheckpoint const* a,
    struct ReplayCheckpoint const* b,
    int domain,
    char* why,
    int why_cap);

/** "varps", "uitree", ... */
char const*
ReplayCheckpoint_DomainName(int domain);

/** One domain's cost: its copy's bytes and the take time (ms). */
size_t
ReplayCheckpoint_DomainBytes(
    struct ReplayCheckpoint const* cp,
    int domain);

double
ReplayCheckpoint_DomainTakeMs(
    struct ReplayCheckpoint const* cp,
    int domain);

/** The last ReplayCheckpoint_Restore's time in `domain`, and in all (ms),
 *  the after-restore invalidation included in the total. */
double
ReplayCheckpoint_LastRestoreMs(int domain);

double
ReplayCheckpoint_LastRestoreTotalMs(void);

/** One `replay: checkpoint cost ...` line per domain and a total, on the
 *  report stream: bytes, take ms, and the last restore's ms. */
void
ReplayCheckpoint_PrintCost(
    struct ReplayCheckpoint const* cp,
    char const* label);

#endif
