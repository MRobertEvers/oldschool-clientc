#include "uitree_obj_cell.h"

#include "uitree_input.h"
#include "uitree_inv_view.h"
#include "uitree_layout.h"
#include "uitree_scroll.h"

#include <assert.h>

#define UITREE_OBJ_CELL_HIT_MAX 16

static bool
node_in_bounds(
    struct UITreeComponent const* node,
    int px,
    int py)
{
    int bx = 0, by = 0, bw = 0, bh = 0;

    UITree_LayoutGetBounds(&node->position, &bx, &by, &bw, &bh);
    return px >= bx && py >= by && px < bx + bw && py < by + bh;
}

/** True when the component names at least one right-click verb of its own. */
static bool
menu_has_ops(struct UITreeMenuOptions const* opts)
{
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
    {
        if( opts->ops[i][0] != '\0' )
            return true;
    }
    return false;
}

/** The uid the wire names for a dynamic child: its static parent's. */
static int
dynamic_parent_component_id(
    struct UITree const* tree,
    struct UITreeComponent const* node)
{
    if( node->parent < 0 )
        return node->component_id;
    return tree->components[node->parent].component_id;
}

static bool
grid_cell(
    struct UITree const* tree,
    int32_t node_index,
    struct UITreeComponent const* node,
    int px,
    int py,
    struct UITreeObjCell* out)
{
    struct UITreeInvGridLayout layout;
    int bx = 0, by = 0, bw = 0, bh = 0;
    int offx = 0, offy = 0;
    int slot;

    layout.cols = node->u.rs_inv.cols;
    layout.rows = node->u.rs_inv.rows;
    layout.margin_x = node->u.rs_inv.margin_x;
    layout.margin_y = node->u.rs_inv.margin_y;
    layout.offset_x = node->u.rs_inv.inv_slot_offset_x;
    layout.offset_y = node->u.rs_inv.inv_slot_offset_y;

    UITree_LayoutGetBounds(&node->position, &bx, &by, &bw, &bh);
    UITree_AccumScrollOffset(tree, node_index, &offx, &offy);
    slot = UITree_InvViewGridHitTest(bx, by, &layout, px + offx, py + offy);
    if( slot < 0 )
        return false;

    out->kind = UITREE_OBJ_CELL_GRID;
    out->node_index = node_index;
    out->ops_node_index = node_index;
    out->component_id = node->component_id;
    out->slot = slot;
    out->inv_source_id = node->u.rs_inv.inv_source_id;
    out->obj_id = 0;
    out->obj_count = 0;
    out->can_drag = node->u.rs_inv.can_drag;
    out->obj_ops = node->u.rs_inv.obj_ops;
    out->obj_use = node->u.rs_inv.obj_use;
    return true;
}

bool
UITree_ObjCellForNode(
    struct UITree const* tree,
    int32_t node_index,
    int px,
    int py,
    struct UITreeObjCell* out)
{
    struct UITreeComponent const* node;

    assert(tree && out);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return false;
    node = &tree->components[node_index];
    if( node->freed )
        return false;

    if( node->type == UIELEM_RS_INV )
        return grid_cell(tree, node_index, node, px, py, out);

    /* A CS2 item child. `item_id` is set by CC_SETOBJECT and is the only thing
     * that distinguishes an item cell from the chrome graphics beside it — the
     * backpack's slots and the worn tab's silhouettes are all plain dynamic
     * RS_GRAPHICs otherwise. */
    if( !node->dynamic || node->item_id <= 0 )
        return false;

    out->kind = UITREE_OBJ_CELL_DYNAMIC;
    out->node_index = node_index;
    /*
     * Which component's verbs the cell offers.
     *
     * Usually the container's: the backpack and the worn tab set their ops once
     * on the static parent and let every cell inherit them. The bank does not —
     * `bankmain_drawitem` calls cc_setop on the *child* it just drew, because
     * the rows differ per item and per setting ("Withdraw-1", "Withdraw-All",
     * "Release" on a placeholder). Reading only the parent showed a bank item
     * the ObjType's Drop/Use rows and none of its own.
     *
     * The child wins when it has any, which is also what the reference does —
     * it reads the ops off whatever component the cursor is over.
     */
    out->ops_node_index = menu_has_ops(&node->menu_options)
                              ? node_index
                              : (node->parent >= 0 ? node->parent : node_index);
    out->component_id = dynamic_parent_component_id(tree, node);
    out->slot = node->dynamic_child_index;
    out->inv_source_id = -1;
    out->obj_id = node->item_id;
    out->obj_count = node->item_count > 0 ? node->item_count : 1;
    /*
     * No IfType record exists for a script-created child, so the cache flags a
     * grid would carry do not. What stands in for them is whether the
     * container named verbs of its own:
     *
     *   149:0  (backpack)  no ops   -> the ObjType's Wear/Eat/Drop, plus Use
     *   387:15 (worn head) "Remove" -> that, and nothing from the ObjType
     *
     * which is the same split the real client gets from the server's
     * IF_SETEVENTS op mask, arrived at without one.
     */
    out->can_drag = 1;
    out->obj_ops = !menu_has_ops(&tree->components[out->ops_node_index].menu_options);
    out->obj_use = out->obj_ops;
    return true;
}

bool
UITree_ObjCellAt(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py,
    struct UITreeObjCell* out)
{
    int32_t hits[UITREE_OBJ_CELL_HIT_MAX];
    int hit_count;

    assert(tree && out);
    hit_count = UITree_CollectNodesAt(tree, host, px, py, hits, UITREE_OBJ_CELL_HIT_MAX);
    for( int i = 0; i < hit_count; i++ )
    {
        if( UITree_ObjCellForNode(tree, hits[i], px, py, out) )
            return true;
    }
    return false;
}

int
UITree_ObjCellDynamicSlotAt(
    struct UITree const* tree,
    int parent_component_id,
    int px,
    int py)
{
    int32_t parent_idx;

    assert(tree);
    parent_idx = UITree_FindByComponentId((struct UITree*)tree, parent_component_id);
    if( parent_idx < 0 )
        return -1;

    for( int32_t idx = tree->components[parent_idx].first_child; idx >= 0;
         idx = tree->components[idx].next_sibling )
    {
        struct UITreeComponent const* child = &tree->components[idx];
        if( child->freed || !child->dynamic )
            continue;
        if( node_in_bounds(child, px, py) )
            return child->dynamic_child_index;
    }
    return -1;
}

/** First dynamic child of `parent_idx` whose child index is `slot`. */
static int32_t
dynamic_child_at_slot(
    struct UITree const* tree,
    int32_t parent_idx,
    int slot)
{
    for( int32_t idx = tree->components[parent_idx].first_child; idx >= 0;
         idx = tree->components[idx].next_sibling )
    {
        struct UITreeComponent const* child = &tree->components[idx];
        if( !child->freed && child->dynamic && child->dynamic_child_index == slot )
            return idx;
    }
    return -1;
}

bool
UITree_ObjCellDynamicSwap(
    struct UITree* tree,
    int parent_component_id,
    int slot_a,
    int slot_b)
{
    int32_t parent_idx;
    int32_t idx_a, idx_b;
    struct UITreeComponent* a;
    struct UITreeComponent* b;
    int swap;

    assert(tree);
    parent_idx = UITree_FindByComponentId(tree, parent_component_id);
    if( parent_idx < 0 )
        return false;
    idx_a = dynamic_child_at_slot(tree, parent_idx, slot_a);
    idx_b = dynamic_child_at_slot(tree, parent_idx, slot_b);
    if( idx_a < 0 || idx_b < 0 )
        return false;

    a = &tree->components[idx_a];
    b = &tree->components[idx_b];

#define UITREE_OBJ_CELL_SWAP(field)                                                                \
    do                                                                                             \
    {                                                                                              \
        swap = a->field;                                                                           \
        a->field = b->field;                                                                       \
        b->field = swap;                                                                           \
    } while( 0 )

    UITREE_OBJ_CELL_SWAP(item_id);
    UITREE_OBJ_CELL_SWAP(item_count);
    UITREE_OBJ_CELL_SWAP(item_scene_id);
    UITREE_OBJ_CELL_SWAP(item_atlas_index);
#undef UITREE_OBJ_CELL_SWAP

    /* An empty cell is hidden by the paint script, so a swap has to move the
     * hide with the item or the moved item lands in an invisible slot. */
    {
        uint8_t hide_swap = a->behavior.hide;
        a->behavior.hide = b->behavior.hide;
        b->behavior.hide = hide_swap;
    }

    UITree_MarkNodeDirty(tree, idx_a);
    UITree_MarkNodeDirty(tree, idx_b);
    return true;
}
