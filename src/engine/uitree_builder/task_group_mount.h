#ifndef TASK_GROUP_MOUNT_H
#define TASK_GROUP_MOUNT_H

#include "asyncio.h"

struct CacheProvider;
struct RS_CS2Host;

/*
 * What a bake reads of group `group`'s ComponentPack, which must be in the
 * cache: every sprite and font it names and the npc configs its npc-head
 * widgets name. Each asked of the provider's assets table, counted on `join`;
 * the caller waits (TASK_AWAIT_ASSETS). Every builder of a group into a tree
 * asks with this one, whatever bakes it.
 */
void
GroupMount_AskComponentPackParts(
    struct CacheProvider* provider,
    int group,
    int* join);

/*
 * The models group `group`'s ComponentPack names: VISUAL, so asked by a host
 * that binds them after the bake, or that has no later binder and waits for
 * them before it. Counted on `join`, as above.
 */
void
GroupMount_AskComponentPackModels(
    struct CacheProvider* provider,
    int group,
    int* join);

/*
 * Put interface group `group` into the host's UI tree: the ONE way a group
 * gets there, whoever asks.
 *
 *   1. its ComponentPack, through the assets table;
 *   2. what the bake reads of it, through the assets table: every sprite and
 *      font it names, and the npc configs its npc-head widgets name;
 *   3. the bake (UITree_BuildFromComponentPack), unless the group is in the
 *      tree already -- a group is baked once, and whoever reaches it next
 *      reuses that copy and every dynamic child made in it.
 *
 * A fresh bake is HIDDEN (mount_hidden) and a tree root, unless
 * `mount_parent_id` names a component of another group that is in the tree,
 * in which case it is reparented under that one (a script that reached into
 * a sub through its parent). Whoever opens the group clears the hide
 * (Task_InterfaceOpen's mount, IF_OPENTOP's root). Its models are VISUAL:
 * after a bake the host's pack_models_lazy, when it has one, starts the
 * parallel task that loads and binds them.
 *
 * Queued beside the task that needs the group and joined by it
 * (ToriRS_TaskQueue_AddParallelPoolSubTask on the provider's asset queue, then
 * PT_TASK_JOIN). When nobody waits for it any more by the time its loads end
 * -- the asker was cancelled or freed, which detaches the join -- it does not
 * bake. A ComponentPack the cache does not hold is not baked; the asker sees the group
 * absent (CacheProvider_ComponentPackHas) and answers that.
 *
 * Needs the host's tree and bridge, and its provider's assets table.
 */
struct ToriRS_Task*
CreateTask_GroupMount(
    struct RS_CS2Host* host,
    int group,
    int mount_parent_id);

#endif /* TASK_GROUP_MOUNT_H */
