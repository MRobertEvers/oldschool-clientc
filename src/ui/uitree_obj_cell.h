#ifndef SRC_UITREE_OBJ_CELL_H
#define SRC_UITREE_OBJ_CELL_H

/*
 * "Which inventory slot is under this point" — for both shapes the tree can
 * express an item cell in.
 *
 * Older revisions give an inventory one TYPE_INV node holding a cols x rows
 * grid: the node is the container, the slot is a hit-test inside it, and the
 * items live in the client's InvManager.
 *
 * Rev 230 has no such node. The gameframe's CS2 draws the backpack as 28
 * `cc_create`d children of a plain layer (149:0) and the worn tab as one
 * child per equipment slot layer (387:15..25); each child carries its own
 * obj id, and its index within the parent IS the slot. That is also what the
 * protocol means: rsprot's If3Button/IfButtonD carry `componentId` (the
 * static parent) and `sub` (the child index) — see OpenRune's
 * IfButton1Handler/IfButtonDHandler — so the uid a packet must name is the
 * PARENT's, never the dynamic child's own.
 *
 * Both the right-click builder and the drag machine need the same answer, and
 * getting it from two places is how they drifted before.
 */

#include "uitree.h"

#include <stdbool.h>

struct UITreeHost;

enum UITreeObjCellKind
{
    UITREE_OBJ_CELL_NONE = 0,
    /** TYPE_INV grid slot. The obj comes from InvManager via inv_source_id. */
    UITREE_OBJ_CELL_GRID,
    /** CS2 `cc_create` item child. The obj is on the node itself. */
    UITREE_OBJ_CELL_DYNAMIC,
};

struct UITreeObjCell
{
    enum UITreeObjCellKind kind;
    /** Tree index of the node carrying the item (the grid, or the child). */
    int32_t node_index;
    /** Tree index of the node whose menu_options are the cell's own verbs:
     *  itself for a grid, the static PARENT for a CS2 cell. rev 230 sets the
     *  worn tab's "Remove" on 387:15..25 rather than on the item children. */
    int32_t ops_node_index;
    /** Component uid the server is told about: the grid node for a GRID cell,
     *  the dynamic child's static PARENT for a DYNAMIC one. */
    int component_id;
    /** Grid slot, or the dynamic child index. */
    int slot;
    /** GRID only; -1 for DYNAMIC (the tree, not InvManager, holds the obj). */
    int inv_source_id;
    /** DYNAMIC only; 0 for GRID (ask InvManager instead). */
    int obj_id;
    int obj_count;
    /** Grid flags (IfType objSwap/objOps/objUse). A CS2 cell has no such cache
     *  record, so they are inferred: a container that names its own verbs owns
     *  the menu (the worn tab offers "Remove", not "Wear"/"Drop"), and one that
     *  names none falls back to the ObjType's inventory ops (the backpack). */
    int can_drag;
    int obj_ops;
    int obj_use;
};

/**
 * Classify one already-hit node. `px`/`py` are canvas coords, needed only by
 * the GRID branch (a TYPE_INV node's own box is cols x rows PIXELS, so the
 * slot has to be resolved by the grid layout, not by the node bounds).
 *
 * Returns false when the node is not an item cell — including a dynamic child
 * whose slot is empty, which is a cell but not one anything can be done to.
 */
bool
UITree_ObjCellForNode(
    struct UITree const* tree,
    int32_t node_index,
    int px,
    int py,
    struct UITreeObjCell* out);

/**
 * Topmost filled item cell under (px, py), through UITree_CollectNodesAt so
 * visibility, clipping and the selected sidebar tab all apply.
 */
bool
UITree_ObjCellAt(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py,
    struct UITreeObjCell* out);

/**
 * Slot under (px, py) among the dynamic children of `parent_component_id`,
 * empty ones included — the drag *target* query.
 *
 * It cannot go through UITree_ObjCellAt: the CS2 paint script hides the child
 * of an empty slot, so a hit-test would skip exactly the slots a player drags
 * items into. The bounds are still laid out, so the parent's child list is
 * walked directly. Returns -1 when the point is over no child.
 */
int
UITree_ObjCellDynamicSlotAt(
    struct UITree const* tree,
    int parent_component_id,
    int px,
    int py);

/**
 * Swap two dynamic children's item fields, so a drag lands before the server's
 * echo does. The CS2 inv-transmit repaint overwrites this a tick later; it
 * exists only to remove the 600 ms of the item sitting where it was dropped
 * from. Returns false if either slot has no child.
 */
bool
UITree_ObjCellDynamicSwap(
    struct UITree* tree,
    int parent_component_id,
    int slot_a,
    int slot_b);

#endif
