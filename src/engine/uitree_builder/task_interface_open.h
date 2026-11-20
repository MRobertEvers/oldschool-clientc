#ifndef TASK_INTERFACE_OPEN_H
#define TASK_INTERFACE_OPEN_H

#include "asyncio.h"
#include "inv/inv_manager.h"

struct CacheProvider;
struct UITree;
struct RS_CS2Host;
struct InvManager;
struct UITreeSceneBridge;
struct UIIfTextStore;
struct UIIfIntStore;

/* Worst case is every worn + backpack slot holding a distinct obj. Sized from
 * the container defaults so the two move together; the collector clamps rather
 * than aborting if a container is ever registered with more slots than these. */
#define INTERFACE_OPEN_SEED_OBJ_MAX \
    (INV_MANAGER_DEFAULT_WORN_SLOTS + INV_MANAGER_DEFAULT_BACKPACK_SLOTS)

struct InterfaceOpenStats
{
    int interface_id;
    int pack_component_count;
    int onload_count;
    int tree_component_count;
};

/**
 * Open one cache interface pack as a root (TS setRootInterface):
 * load → bake → layout → onLoad → layout → inv/var transmit → layout.
 * Spillover sibling groups that are not InterfaceParent-mounted are hidden.
 */
struct ToriRS_Task*
CreateTask_InterfaceOpen(
    struct CacheProvider* provider,
    struct UITree* tree,
    struct RS_CS2Host* host,
    struct InvManager* invs,
    struct UITreeSceneBridge* bridge,
    int interface_id,
    struct InterfaceOpenStats* stats);

/**
 * Open a pack as a sub-interface mounted under target_uid (TS openSubInterface):
 * load → bake → reparent under target → layout(host) → onLoad → layout →
 * onSubChange → inv/var transmit → layout. Resize listeners are queued by
 * size-changing layout passes (when enabled) or explicitly by IF_CALLONRESIZE;
 * opening a sub-interface does not dispatch them generically.
 * type: 0 modal, 1 overlay, 3 tab/sidemodal.
 */
struct ToriRS_Task*
CreateTask_InterfaceOpenSub(
    struct CacheProvider* provider,
    struct UITree* tree,
    struct RS_CS2Host* host,
    struct InvManager* invs,
    struct UITreeSceneBridge* bridge,
    int target_uid,
    int interface_id,
    int type,
    struct InterfaceOpenStats* stats);

/**
 * Give an interface-open task (made by CreateTask_InterfaceOpen*, not yet
 * run) the server's stored IF_SETTEXT, IF_SETHIDE and IF_SETCOLOUR values:
 * the ones for the opened group are applied to its nodes after the bake and
 * before its load scripts run.
 */
void
InterfaceOpen_SetStores(
    struct ToriRS_Task* task,
    struct UIIfTextStore const* texts,
    struct UIIfIntStore const* hides,
    struct UIIfIntStore const* colours);

/** The distinct objs the worn and backpack containers hold (up to `cap`):
 *  the inventory models a mount prefetches. Returns how many. */
int
InterfaceOpen_CollectSeedObjs(
    struct InvManager const* invs,
    int* out_ids,
    int cap);

/** Rasterise the icon of every worn and backpack obj whose model is here. */
void
InterfaceOpen_BindSeedIcons(
    struct InvManager* invs,
    struct UITreeSceneBridge* bridge);

/**
 * Bind the models now here onto group `group`'s model widgets (and the
 * default avatar onto a player-preview widget with none) and mark each bound
 * node dirty. What a mount binds of its models when they are here at the
 * mount, for when they arrive later. Returns how many nodes it bound.
 */
int
InterfaceOpen_BindGroupModels(
    struct UITree* tree,
    struct UITreeSceneBridge* bridge,
    int group);

/** What a model widget still waits for before it can be drawn. */
enum InterfaceOpenModelWantKind
{
    /* Its model (UITreeComponent.u.rs_model.gamecache_model_id). */
    INTERFACE_OPEN_WANT_MODEL = 0,
    /* Its active model (u.rs_model.active_model_id). */
    INTERFACE_OPEN_WANT_ACTIVE_MODEL,
    /* The default avatar, on a player-preview widget (client code 327/328)
     * that names no model: the default appearance's kit models. */
    INTERFACE_OPEN_WANT_AVATAR,
};

struct InterfaceOpenModelWant
{
    enum InterfaceOpenModelWantKind kind;
    /* The node, by index and generation. */
    int32_t index;
    /* Its model's cache id; -1 for the avatar. */
    int model_id;
};

/* A node has at most a model and an active model, or the avatar. */
#define INTERFACE_OPEN_NODE_WANTS_MAX 2

/**
 * What model node `idx` of `tree` waits for: its model or active model when
 * it is a cache model `provider` does not hold (a scene composite an
 * IF_SETNPCHEAD or IF_SETOBJECT bound is its own task's), the avatar when the
 * bridge has not built it.
 * Fills up to INTERFACE_OPEN_NODE_WANTS_MAX of `out` and returns how many; 0
 * for a node that is not a live model widget or has all it needs.
 */
int
InterfaceOpen_NodeModelWants(
    struct UITree const* tree,
    struct CacheProvider* provider,
    struct UITreeSceneBridge const* bridge,
    int32_t idx,
    struct InterfaceOpenModelWant* out);

/**
 * Bind onto model node `idx` what is here of what it shows (the per-node
 * body of InterfaceOpen_BindGroupModels): its model, its active model, or the
 * avatar posed at the player's idle. Marks it dirty and returns 1 when it
 * bound something.
 */
int
InterfaceOpen_BindModelNode(
    struct UITree* tree,
    struct UITreeSceneBridge* bridge,
    int32_t idx);

/**
 * The model nodes under `owner` (not `owner` itself), depth first, up to
 * `cap` of them into `out`; returns how many there are (which may exceed
 * `cap`). A subtree walk: what an old-format slot mount baked under its slot,
 * whose nodes carry no interface group to find them by (a dat1 component id
 * is flat, so its high half -- the group a dat2 id carries -- is 0).
 */
int
InterfaceOpen_SubtreeModelNodes(
    struct UITree const* tree,
    int32_t owner,
    int32_t* out,
    int cap);

/**
 * Record group `group` mounted at component `target_uid` (mount `type`) and
 * move every node of it that is not under another of its own nodes under
 * that component, clearing what the mount bookkeeping hid. The open's mount
 * step, and the whole of an IF_MOVESUB's relink.
 */
void
InterfaceOpen_MountGroupUnder(
    struct UITree* tree,
    int target_uid,
    int group,
    int type);

/**
 * Apply the stored values for group `group`'s components onto its nodes
 * (InterfaceOpen_SetStores says when a mount does this). An unhide of a
 * hidden node raises `host`'s widgets_loaded_dirty, as a script's unhide
 * does.
 */
void
InterfaceOpen_ApplyStoredValues(
    struct UITree* tree,
    struct RS_CS2Host* host,
    int group,
    struct UIIfTextStore const* texts,
    struct UIIfIntStore const* hides,
    struct UIIfIntStore const* colours);

#endif
