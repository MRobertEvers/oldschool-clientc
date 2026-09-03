#ifndef TORIRS_PLUGIN_UI_H
#define TORIRS_PLUGIN_UI_H

/*
 * Canonical, retained named-UI declarations.
 *
 * This module deliberately knows nothing about UITree, the renderer, input,
 * or the plugin host.  It answers the policy question between those layers:
 * for a semantic name, which provider supplies each independent facet now?
 *
 * Names are interned once and are never recycled.  A ToriRS_UiNodeRef is
 * therefore safe to cache across lane, frame-provider, and CS2 generations;
 * only the snapshot returned by Resolve is generation-specific.
 */

#include "plugin/torirs_plugin_v2.h"

#include <stdbool.h>
#include <stdint.h>

#define TORIRS_UI_REGISTRY_NODES_MAX 256
#define TORIRS_UI_REGISTRY_CONTRIBUTIONS_MAX 512
#define TORIRS_UI_REGISTRY_HASH_SIZE 512
#define TORIRS_UI_BASE_DECLARATIONS_MAX (TORIRS_UI_REGISTRY_NODES_MAX * 3)

struct ToriRS_UiContributionRef
{
    uint32_t value;
};

enum ToriRS_UiRegistryResult
{
    TORIRS_UI_REGISTRY_OK = 0,
    TORIRS_UI_REGISTRY_INVALID,
    TORIRS_UI_REGISTRY_DUPLICATE,
    TORIRS_UI_REGISTRY_FULL,
};

/* Pointer-free retained form of ToriRS_UiNode. Parent is interned and every
 * caller-owned string is copied, so declarations may come from scratch
 * memory and executors only ever consume stable retained data. */
struct ToriRS_UiStoredNode
{
    struct ToriRS_Rect bounds;
    struct ToriRS_UiNodeRef parent;
    int anchor;
    int paint_order;
    int clip;
    uint32_t flags;
    /* Compatibility mirror of state_images[IDLE]. */
    struct ToriRS_ImageRef image;
    struct ToriRS_ImageRef state_images[TORIRS_UI_VISUAL_STATE_COUNT];
    char label[TORIRS_UI_LABEL_MAX];
    int label_x;
    int label_y;
    int hit_rect_mode;
    struct ToriRS_Rect hit_rect;
    uint32_t action_count;
    char actions[TORIRS_UI_NAMED_ACTIONS_MAX][TORIRS_UI_ACTION_MAX];
    /* Compatibility mirror of actions[0]. */
    char action[TORIRS_UI_ACTION_MAX];
};

/* A current snapshot.  Empty provider strings mean that facet is absent.
 * conflict_facets can be non-zero even when Resolve returns false: two
 * providers may conflict over the only facet of an otherwise absent node. */
struct ToriRS_UiResolvedNode
{
    struct ToriRS_UiNodeRef ref;
    uint32_t available_facets;
    uint32_t conflict_facets;
    struct ToriRS_UiStoredNode value;
    char bounds_provider[TORIRS_PLUGIN_NAME_MAX];
    char appearance_provider[TORIRS_PLUGIN_NAME_MAX];
    char actions_provider[TORIRS_PLUGIN_NAME_MAX];
    uint32_t revision;
};

struct ToriRS_UiContributionStatus
{
    int state;
    uint32_t active_facets;
    uint32_t conflict_facets;
    /* Lexicographically first conflicting provider other than this one. */
    char conflict_plugin[TORIRS_PLUGIN_NAME_MAX];
};

/* One input row for an atomic lane/frame base replacement. */
struct ToriRS_UiBaseDeclaration
{
    char const* provider;
    char const* node;
    uint32_t facets;
    struct ToriRS_UiNode value;
};

/* One deduplicated retained-state command for an executor.  Resolve `node`
 * after taking the change; absence is the remove command. */
struct ToriRS_UiChange
{
    struct ToriRS_UiNodeRef node;
    uint32_t facets;
    uint32_t revision;
};

/* Public layout only so callers can own a registry without allocation.  The
 * underscore-prefixed records are private implementation state. */
struct ToriRS_UiRegistryNode
{
    char name[TORIRS_UI_NAME_MAX];
    uint32_t base_facets;
    struct ToriRS_UiStoredNode base;
    char base_provider[3][TORIRS_PLUGIN_NAME_MAX];
    struct ToriRS_UiResolvedNode resolved;
    int first_contribution;
    bool dirty;
    bool change_queued;
    uint32_t changed_facets;
};

struct ToriRS_UiRegistryContribution
{
    bool used;
    uint32_t serial;
    int node;
    int next_for_node;
    int mode;
    uint32_t facets;
    char plugin[TORIRS_PLUGIN_NAME_MAX];
    struct ToriRS_UiStoredNode value;
    uint32_t active_facets;
    uint32_t conflict_facets;
    bool target_absent;
};

struct ToriRS_UiRegistry
{
    struct ToriRS_UiRegistryNode _nodes[TORIRS_UI_REGISTRY_NODES_MAX];
    struct ToriRS_UiRegistryContribution _contributions[TORIRS_UI_REGISTRY_CONTRIBUTIONS_MAX];
    /* node index + 1; zero is an empty hash bucket. */
    uint16_t _hash[TORIRS_UI_REGISTRY_HASH_SIZE];
    int _node_count;
    int _contribution_count;
    uint32_t _next_contribution_serial;
    uint32_t _revision;
    uint32_t _base_generation;
    uint16_t _change_queue[TORIRS_UI_REGISTRY_NODES_MAX];
    int _change_head;
    int _change_count;
};

/** Initialize the registry and intern the complete core frame vocabulary. */
void
ToriRS_UiRegistry_Init(struct ToriRS_UiRegistry* registry);

/** Validate canonical dotted spelling (not a legacy alias). */
bool
ToriRS_UiRegistry_NameIsValid(char const* name);

/**
 * Intern a canonical frame.* or plugin.* name.  A documented legacy alias is
 * canonicalized first.  Unknown frame.* names are refused because that
 * namespace is deliberately owned by the core vocabulary.
 */
struct ToriRS_UiNodeRef
ToriRS_UiRegistry_Ref(
    struct ToriRS_UiRegistry* registry,
    char const* name);

/** Find an existing canonical name or alias without creating anything. */
struct ToriRS_UiNodeRef
ToriRS_UiRegistry_Find(
    struct ToriRS_UiRegistry const* registry,
    char const* name);

/**
 * Intern a plugin-private local name as plugin.<plugin-id>.<local-name>.
 * Known legacy aliases and frame.* names still address the shared vocabulary.
 * A fully qualified plugin.* spelling is accepted only for the same plugin.
 */
struct ToriRS_UiNodeRef
ToriRS_UiRegistry_PrivateRef(
    struct ToriRS_UiRegistry* registry,
    char const* plugin,
    char const* name);

/** Canonical spelling for a ref, or NULL for an invalid ref. */
char const*
ToriRS_UiRegistry_Name(
    struct ToriRS_UiRegistry const* registry,
    struct ToriRS_UiNodeRef ref);

/** Interned-name enumeration for diagnostics and occluder collection. */
int
ToriRS_UiRegistry_NodeCount(struct ToriRS_UiRegistry const* registry);

struct ToriRS_UiNodeRef
ToriRS_UiRegistry_NodeAt(
    struct ToriRS_UiRegistry const* registry,
    int index);

/** Current retained-state revision.  Zero is never published. */
uint32_t
ToriRS_UiRegistry_Revision(struct ToriRS_UiRegistry const* registry);

/** Generation of the base lane/frame declaration. */
uint32_t
ToriRS_UiRegistry_BaseGeneration(struct ToriRS_UiRegistry const* registry);

/**
 * Commit an intentionally empty base tree.  Interned refs and plugin
 * contributions remain.  Rebuilders must use ReplaceBase rather than call
 * ClearBase followed by AddBase: ReplaceBase keeps the prior tree live until
 * the complete candidate has passed validation.
 */
void
ToriRS_UiRegistry_ClearBase(struct ToriRS_UiRegistry* registry);

/**
 * Validate and atomically replace the complete lane/frame base declaration.
 * A malformed, duplicate, over-budget, or cyclic candidate changes nothing.
 * A successful commit advances one revision and queues one deduplicated
 * changed-node command per affected semantic name, including removals.
 */
enum ToriRS_UiRegistryResult
ToriRS_UiRegistry_ReplaceBase(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiBaseDeclaration const* declarations,
    int declaration_count);

/**
 * Add non-overlapping base facets for bootstrap/tests.  Duplicate base facets
 * are refused atomically; independent providers may supply independent
 * facets.  `value` is copied.  Whole-tree rebuilds use ReplaceBase.
 */
enum ToriRS_UiRegistryResult
ToriRS_UiRegistry_AddBase(
    struct ToriRS_UiRegistry* registry,
    char const* provider,
    char const* name,
    uint32_t facets,
    struct ToriRS_UiNode const* value);

/** Remove every base facet owned by provider. */
int
ToriRS_UiRegistry_RemoveBaseProvider(
    struct ToriRS_UiRegistry* registry,
    char const* provider);

/**
 * Add one retained contribution.  A plugin may declare a node only once;
 * combine its desired facets in that declaration.  Node and parent local
 * names are automatically plugin-namespaced.  The operation is atomic.
 */
enum ToriRS_UiRegistryResult
ToriRS_UiRegistry_AddContribution(
    struct ToriRS_UiRegistry* registry,
    char const* plugin,
    struct ToriRS_UiContribution const* contribution,
    struct ToriRS_UiContributionRef* out_ref);

/** Atomically restate a subset of an existing contribution's declared
 * facets. Strings are copied again. An identical restatement advances neither
 * the revision nor the change journal. */
enum ToriRS_UiRegistryResult
ToriRS_UiRegistry_UpdateContribution(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiContributionRef contribution,
    uint32_t facets,
    struct ToriRS_UiNode const* value);

/** Remove all base facets and contributions belonging to a plugin. */
int
ToriRS_UiRegistry_RemovePlugin(
    struct ToriRS_UiRegistry* registry,
    char const* plugin);

/**
 * Resolve a current snapshot from the full declaration set.  Returns false
 * when no facet is active.  `out` is still filled so diagnostics can see an
 * all-conflicted absent node.
 */
bool
ToriRS_UiRegistry_Resolve(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiNodeRef ref,
    struct ToriRS_UiResolvedNode* out);

/** Current status of a retained contribution; false after plugin teardown. */
bool
ToriRS_UiRegistry_ContributionStatus(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiContributionRef contribution,
    struct ToriRS_UiContributionStatus* out);

/**
 * Number/list of contenders when `facet` is conflicted.  Names are exposed in
 * lexical order, independent of registration or callback order.
 */
int
ToriRS_UiRegistry_ConflictCount(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiNodeRef node,
    uint32_t facet);

bool
ToriRS_UiRegistry_ConflictPluginAt(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiNodeRef node,
    uint32_t facet,
    int index,
    char* out_plugin,
    int out_size);

/** Number of deduplicated node changes waiting for an executor. */
int
ToriRS_UiRegistry_ChangeCount(struct ToriRS_UiRegistry const* registry);

/**
 * Pop one mutation-recorded command.  No registry scan or old/new tree diff is
 * needed.  Changes to the same node coalesce until that node is popped.
 */
bool
ToriRS_UiRegistry_ChangeNext(
    struct ToriRS_UiRegistry* registry,
    struct ToriRS_UiChange* out);

#endif /* TORIRS_PLUGIN_UI_H */
