#ifndef SRC_UI_UITREE_SNAPSHOT_H
#define SRC_UI_UITREE_SNAPSHOT_H

struct UITree;
struct UITreeEmitBuffer;

/**
 * Write a bounded, preview-only snapshot of the live runtime component tree.
 *
 * This is intentionally downstream of layout, CS2 settle and emit. It records
 * the component slots the production client is actually using (including
 * CC_CREATE children), their resolved boxes, visibility/cull state, runtime
 * hooks and the first emitted draw descriptor for each node.
 *
 * Returns 0 on success and -1 on invalid arguments, I/O failure, or a snapshot
 * exceeding its hard diagnostic bounds. A failed partial file is removed.
 */
int
UITreeSnapshot_WriteJson(
    struct UITree const* tree,
    struct UITreeEmitBuffer const* emit,
    char const* path,
    int interface_id,
    int viewport_w,
    int viewport_h);

#endif
