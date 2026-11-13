#ifndef SRC_UI_UITREE_CHECKPOINT_H
#define SRC_UI_UITREE_CHECKPOINT_H

/*
 * The UI tree's half of a session checkpoint (replay/replay_checkpoint.h).
 *
 * A checkpoint is a `struct UITree` that is not a live tree: every node slot
 * (freed ones too, so the free list and the slot a later create takes are the
 * same), each node's heap duplicated -- hooks and their arguments, params,
 * menu options, op keys, cs1 scripts, texts, the plugin edit lists -- and the
 * tree-level tables (live sets, the group map with its close serials, the
 * mount records, hotkeys). Lookup caches (the id index, the layout order, the
 * canvas candidates, each node's visible-children list and model render
 * cache) are not copied: a restore invalidates them and they rebuild.
 *
 * A restore puts the copy back into the SAME live tree it was taken from (its
 * identity, `instance_id`, is the tree's), keeps the tree's scratch, and moves
 * every generation and serial a cache keys on PAST anything the live tree had
 * reached, so nothing looked up before the restore is trusted after it. Nodes
 * keep their incarnations: a UITreeNodeRef taken at the checkpoint names the
 * same node again, and a ref taken after it names nothing (incarnations are a
 * process-wide serial, never reissued).
 */

#include <stddef.h>
#include <stdint.h>

struct UITree;

struct UITree*
UITree_CheckpointTake(struct UITree const* tree);

/** `tree` must be the tree `cp` was taken from. */
void
UITree_CheckpointRestore(
    struct UITree* tree,
    struct UITree const* cp);

/** Accepts NULL. */
void
UITree_CheckpointFree(struct UITree* cp);

size_t
UITree_CheckpointBytes(struct UITree const* cp);

/**
 * 1 when the two copies hold the same interface; else 0 and the first
 * difference in `why` ("node 812 (com 0x02310004 rs_text) u.rs_text.text ...").
 * Compares what the session made true and leaves out what the client derives
 * for drawing (layout boxes, animation phase, icon scene handles, dirty
 * marks, frame-binder stamps) and node incarnations.
 */
int
UITree_CheckpointEqual(
    struct UITree const* a,
    struct UITree const* b,
    char* why,
    int why_cap);

#endif
