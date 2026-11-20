#ifndef SRC_UI_UITREE_KEPT_MOUNTS_H
#define SRC_UI_UITREE_KEPT_MOUNTS_H

/*
 * The mounts kept for a top that is not the live root.
 *
 * When IF_OPENTOP replaces the top, the deob keeps its InterfaceParent table:
 * the subs stay open, keyed by their old slots, and the IF_MOVESUBs that
 * follow relink each one into the new top. The tree here is rebuilt instead,
 * so this table is where those records wait. A record no move claims stays,
 * as the deob keeps it, and reopens when the top whose slot it names is the
 * root again.
 *
 * What this decides is the table's own bookkeeping: on a detach the live
 * tree's records win any slot both name and always fit, a kept one is dropped
 * only when the table is full, and a record taken out leaves its place to the
 * last one (so order is not kept across a take).
 *
 * Nothing here reads a tree or opens anything. The caller hands in the live
 * tree's records, reports what a full table forgot, and reopens what it takes.
 *
 * Not pending work, so not in the pending index (game/rs_pending.h): a kept
 * mount waits on no asset and no task; it is the record of what is mounted
 * under a top that is not the root, the deob's own table, read by the next
 * IF_MOVESUB / IF_RESYNC / IF_OPENTOP at packet time on the main queue.
 */

#include "ui/uitree.h"

#include <stdbool.h>

struct UITreeKeptMounts
{
    struct UITreeInterfaceParent items[UITREE_INTERFACE_PARENT_MAX];
    int count;
};

/** Forget every kept mount. */
void
UITreeKeptMounts_Clear(struct UITreeKeptMounts* kept);

int
UITreeKeptMounts_Count(struct UITreeKeptMounts const* kept);

/** The record at `index`, 0 <= index < count. */
struct UITreeInterfaceParent const*
UITreeKeptMounts_At(
    struct UITreeKeptMounts const* kept,
    int index);

/** Keep one more mount at the end. False, and nothing kept, when full. */
bool
UITreeKeptMounts_Add(
    struct UITreeKeptMounts* kept,
    int container_uid,
    int group_id,
    int type);

/**
 * Take the mount kept at `container_uid` out of the table: its record, or one
 * with group 0 when none is kept there. The last record takes its place.
 */
struct UITreeInterfaceParent
UITreeKeptMounts_Take(
    struct UITreeKeptMounts* kept,
    int container_uid);

/**
 * The live root is about to go: keep its `live` records first, then every
 * record already kept whose slot none of them names, in order, while there is
 * room. Writes the records a full table could not keep to `out_forgotten`
 * (room for UITREE_INTERFACE_PARENT_MAX) and returns how many there were.
 */
int
UITreeKeptMounts_Detach(
    struct UITreeKeptMounts* kept,
    struct UITreeInterfaceParent const* live,
    int live_count,
    struct UITreeInterfaceParent* out_forgotten);

#endif
