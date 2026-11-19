#ifndef SRC_PLUGIN_PLUGIN_FRAME_BINDING_H
#define SRC_PLUGIN_PLUGIN_FRAME_BINDING_H

/*
 * What the plugin bridge remembers about the gameframe between passes, and
 * when a remembered answer may be reused.
 *
 * Every entry is a MEMO: an answer the bridge already worked out by walking
 * the tree or searching the role table, kept with the counters it was worked
 * out against. Five of them, each with its own key:
 *
 *   - the node each placeable surface slot resolves to, keyed on the tree's
 *     `generation`;
 *   - the frame roles' interned ids ([slot][0] the group, [slot][1+m] member
 *     m, 0 = no such role) and `frame_compass_click`'s, keyed on the role
 *     table's size -- a role a later profile line declares can only appear by
 *     the table growing;
 *   - the ids the widget facets are spelled with (four revconfig var ids, two
 *     orb roles, the sixteen `sidetab_N` roles), keyed on the revconfig ref
 *     table's size and the role table's size;
 *   - the node each tab stone resolves to, keyed on the tree's `generation`
 *     and `id_generation`, and dropped whenever the facet ids are re-resolved;
 *   - the frame binder's last pass: the tree it ran against, both of that
 *     tree's generations, and the nodes it stamped with a slot tag (node +
 *     incarnation, so a recycled index reads as a different node).
 *
 * A "no such role" or "no such node" is remembered the same way an answer
 * is, under the same key. Invalidate drops every memo that names the tree's
 * nodes, for a caller that replaced the tree's contents wholesale.
 *
 * Nothing here walks a tree, searches a role table, reads a revconfig or
 * stamps a node. The caller asks whether a memo is current for the counters
 * it has now, does the lookup itself when it is not, and stores the answers
 * here; it also decides whether a previously stamped node is still alive.
 */

#include "plugin/torirs_plugin_host_types.h"

#include <stdbool.h>
#include <stdint.h>

struct UITree;

/** Frame-role slots the interned table has room for. */
#define PLUGIN_FRAME_BINDING_SLOTS 32
/** Numbered members one frame role may have, and tab stones. */
#define PLUGIN_FRAME_BINDING_MEMBERS 16
/** Nodes one binder pass may stamp with a slot tag. */
#define PLUGIN_FRAME_STAMP_MAX 64

/** One node the binder stamped, by index and by the incarnation it had. */
struct PluginFrameStamp
{
    int32_t node;
    uint64_t incarnation;
};

/**
 * The names the widget facets are spelled with, resolved. A var id is -1 or
 * 0 for "this revision does not have it"; a role id is 0 for no such role.
 */
struct PluginFacetIds
{
    int varbit_sidebar_flash;
    int varbit_cutscene;
    int varp_run_mode;
    int varp_special_armed;
    uint16_t role_orb_run;
    uint16_t role_orb_spec;
    /** `sidetab_N` for each tab stone. */
    uint16_t role_sidetab[PLUGIN_FRAME_BINDING_MEMBERS];
};

struct PluginFrameBinding
{
    /* Placeable slot -> node, for the tree generation in slot_node_gen. */
    int32_t slot_node[TORIRS_HOST_SURFACE_PLACEABLE_COUNT];
    uint32_t slot_node_gen;

    /* The nodes the last binder pass stamped. */
    struct PluginFrameStamp stamp[PLUGIN_FRAME_STAMP_MAX];
    int stamp_count;

    /* Interned frame-role ids, for a role table of role_ids_for_count. */
    uint16_t role_id[PLUGIN_FRAME_BINDING_SLOTS][1 + PLUGIN_FRAME_BINDING_MEMBERS];
    uint16_t compass_click_role;
    int role_ids_for_count;

    /* The facet ids, for the two table sizes beside them. */
    struct PluginFacetIds facet;
    int facet_refs_count;
    int facet_roles_count;
    uint8_t facet_valid;

    /* Tab stone -> node, for the two tree counters beside them. */
    int32_t sidetab_node[PLUGIN_FRAME_BINDING_MEMBERS];
    uint32_t sidetab_generation;
    uint32_t sidetab_id_generation;
    uint8_t sidetab_valid;

    /* The tree and generations the last binder pass ran against. */
    struct UITree const* bound_tree;
    uint32_t bound_generation;
    uint32_t bound_id_generation;
    uint8_t bound_valid;
};

/**
 * Forget every memo that names the tree's nodes: the binder's last pass, the
 * tab stones and the slot nodes. The interned role and facet ids are about
 * the role and ref tables, not the tree, and are kept.
 */
void
PluginFrameBinding_Invalidate(struct PluginFrameBinding* binding);

/* ---- slot nodes ---- */

/** True when the slot nodes were resolved against this tree generation. */
bool
PluginFrameBinding_SlotNodesCurrent(
    struct PluginFrameBinding const* binding,
    uint32_t generation);

/** Remember one slot's node. */
void
PluginFrameBinding_SetSlotNode(
    struct PluginFrameBinding* binding,
    int slot,
    int32_t node);

/** Every slot node has been stored for this generation. */
void
PluginFrameBinding_SlotNodesResolved(
    struct PluginFrameBinding* binding,
    uint32_t generation);

/** The remembered node for a slot. */
int32_t
PluginFrameBinding_SlotNode(struct PluginFrameBinding const* binding, int slot);

/* ---- the binder's pass ---- */

/**
 * True when the last pass ran against this tree, at these generations, with
 * role ids interned for a table of this size: its stamps are still right.
 */
bool
PluginFrameBinding_BoundCurrent(
    struct PluginFrameBinding const* binding,
    struct UITree const* tree,
    uint32_t generation,
    uint32_t id_generation,
    int roles_count);

/** The last pass no longer counts, whatever its keys say. */
void
PluginFrameBinding_DropBound(struct PluginFrameBinding* binding);

/** A pass has run against this tree at these generations. */
void
PluginFrameBinding_MarkBound(
    struct PluginFrameBinding* binding,
    struct UITree const* tree,
    uint32_t generation,
    uint32_t id_generation);

/* ---- interned frame-role ids ---- */

/** True when the role ids were interned for a role table of this size. */
bool
PluginFrameBinding_RoleIdsCurrent(
    struct PluginFrameBinding const* binding,
    int roles_count);

/** Set every id of one slot to 0, "no such role". */
void
PluginFrameBinding_ClearRoleIds(struct PluginFrameBinding* binding, int slot);

/** Remember one interned id: index 0 the group, 1 + m member m. */
void
PluginFrameBinding_SetRoleId(
    struct PluginFrameBinding* binding,
    int slot,
    int index,
    uint16_t role_id);

/** Remember `frame_compass_click`'s interned id. */
void
PluginFrameBinding_SetCompassClickRole(
    struct PluginFrameBinding* binding,
    uint16_t role_id);

/** Every role id has been interned for a role table of this size. */
void
PluginFrameBinding_RoleIdsInterned(
    struct PluginFrameBinding* binding,
    int roles_count);

/** One slot's ids, 1 + PLUGIN_FRAME_BINDING_MEMBERS of them. */
uint16_t const*
PluginFrameBinding_RoleIds(struct PluginFrameBinding const* binding, int slot);

uint16_t
PluginFrameBinding_CompassClickRole(struct PluginFrameBinding const* binding);

/* ---- stamps ---- */

int
PluginFrameBinding_StampCount(struct PluginFrameBinding const* binding);

/** One stamp of the last pass. */
struct PluginFrameStamp
PluginFrameBinding_Stamp(struct PluginFrameBinding const* binding, int i);

/** Start a new stamp list; the old one is gone. */
void
PluginFrameBinding_ClearStamps(struct PluginFrameBinding* binding);

/** Add one stamped node to the list. */
void
PluginFrameBinding_PushStamp(
    struct PluginFrameBinding* binding,
    int32_t node,
    uint64_t incarnation);

/* ---- facet ids ---- */

/** True when the facet ids were resolved for tables of these sizes. */
bool
PluginFrameBinding_FacetIdsCurrent(
    struct PluginFrameBinding const* binding,
    int refs_count,
    int roles_count);

/**
 * Remember the facet ids resolved for tables of these sizes. The tab stones'
 * nodes were found through the old role ids, so they are dropped.
 */
void
PluginFrameBinding_StoreFacetIds(
    struct PluginFrameBinding* binding,
    int refs_count,
    int roles_count,
    struct PluginFacetIds const* ids);

struct PluginFacetIds const*
PluginFrameBinding_FacetIds(struct PluginFrameBinding const* binding);

/* ---- tab stones ---- */

/** True when the stone nodes were resolved at these tree counters. */
bool
PluginFrameBinding_SidetabNodesCurrent(
    struct PluginFrameBinding const* binding,
    uint32_t generation,
    uint32_t id_generation);

/**
 * The stone nodes are being resolved at these tree counters. Marked before
 * the nodes are stored, so the caller's lookups see the memo as current.
 */
void
PluginFrameBinding_BeginSidetabNodes(
    struct PluginFrameBinding* binding,
    uint32_t generation,
    uint32_t id_generation);

/** Remember one stone's node, -1 when the tab has no stone. */
void
PluginFrameBinding_SetSidetabNode(
    struct PluginFrameBinding* binding,
    int tab,
    int32_t node);

/** Which tab's stone this node is, or -1. */
int
PluginFrameBinding_SidetabOf(struct PluginFrameBinding const* binding, int32_t node);

#endif
