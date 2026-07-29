#include "uitree.h"

#include "uitree_layout.h"
#include "uitree_scroll.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char const*
UITree_MenuSubmenuEntry(
    struct UITreeMenuOptions const* opts,
    int op_index,
    int entry_index)
{
    assert(opts);
    if( !opts->submenus )
        return "";
    if( op_index < 1 || op_index > UITREE_SUBMENU_OP_SLOTS )
        return "";
    if( entry_index < 1 || entry_index > UITREE_SUBMENU_ENTRY_SLOTS )
        return "";
    return opts->submenus->ops[op_index - 1][entry_index - 1];
}

bool
UITree_MenuSubmenuSetEntry(
    struct UITreeMenuOptions* opts,
    int op_index,
    int entry_index,
    char const* text)
{
    assert(opts);
    if( op_index < 1 || op_index > UITREE_SUBMENU_OP_SLOTS )
        return false;
    if( entry_index < 1 || entry_index > UITREE_SUBMENU_ENTRY_SLOTS )
        return false;
    if( !opts->submenus )
    {
        opts->submenus = calloc(1, sizeof(*opts->submenus));
        if( !opts->submenus )
            return false;
    }
    strncpy(
        opts->submenus->ops[op_index - 1][entry_index - 1],
        text ? text : "",
        UITREE_MENU_OPTION_LEN - 1);
    opts->submenus->ops[op_index - 1][entry_index - 1][UITREE_MENU_OPTION_LEN - 1] = '\0';
    return true;
}

void
UITree_MenuSubmenuClear(
    struct UITreeMenuOptions* opts,
    int op_index)
{
    assert(opts);
    if( !opts->submenus )
        return;
    if( op_index <= 0 )
    {
        /* Every op cleared = no submenus at all; drop the block. */
        UITree_MenuSubmenuFree(opts);
        return;
    }
    if( op_index > UITREE_SUBMENU_OP_SLOTS )
        return;
    for( int i = 0; i < UITREE_SUBMENU_ENTRY_SLOTS; i++ )
        opts->submenus->ops[op_index - 1][i][0] = '\0';
}

void
UITree_MenuSubmenuFree(struct UITreeMenuOptions* opts)
{
    assert(opts);
    free(opts->submenus);
    opts->submenus = NULL;
}

/* Copy menu options between components: everything is by value except the
 * submenu block, which is owned per component and must not be aliased. Whatever
 * dst held is released, and dst == src (or a shared block) is safe. */
static void
uitree_menu_options_copy(
    struct UITreeMenuOptions* dst,
    struct UITreeMenuOptions const* src)
{
    struct UITreeMenuSubmenuOptions* const dst_owned = dst->submenus;
    struct UITreeMenuSubmenuOptions* copy = NULL;

    if( src->submenus )
    {
        copy = calloc(1, sizeof(*copy));
        if( copy )
            memcpy(copy, src->submenus, sizeof(*copy));
    }
    *dst = *src;
    dst->submenus = copy;
    if( dst_owned != copy )
        free(dst_owned);
}

/* The key UITree_FindChildBySubid matches a child on: its dynamic slot for
 * cc_create'd children, the low half of its uid for cache-baked ones. Immutable
 * while the child is linked (both fields are set once, at push). */
static int32_t
uitree_child_key(struct UITreeComponent const* child)
{
    return child->dynamic ? child->dynamic_child_index : (child->component_id & 0xFFFF);
}

/* Fold a newly appended child into its parent's key ceiling. Leaves an unknown
 * ceiling unknown — the next lookup recomputes it once. */
static void
uitree_child_key_added(
    struct UITree* tree,
    int32_t parent_index,
    int32_t child_index)
{
    if( parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return;

    struct UITreeComponent* parent = &tree->components[parent_index];
    int32_t const key = uitree_child_key(&tree->components[child_index]);
    if( parent->child_key_max == UITREE_CHILD_KEY_UNKNOWN )
        return;
    if( parent->child_key_max == UITREE_CHILD_KEY_NONE || key > parent->child_key_max )
        parent->child_key_max = key;
}

/* Removing a child can only lower the ceiling, and only if it *was* the ceiling —
 * so replace-in-slot rebuilds (which delete a row below the ceiling and re-create
 * it) keep a usable ceiling instead of invalidating it every row. */
static void
uitree_child_key_removed(
    struct UITree* tree,
    int32_t parent_index,
    int32_t child_index)
{
    if( parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return;

    struct UITreeComponent* parent = &tree->components[parent_index];
    if( parent->child_key_max == UITREE_CHILD_KEY_UNKNOWN )
        return;
    if( uitree_child_key(&tree->components[child_index]) >= parent->child_key_max )
        parent->child_key_max = UITREE_CHILD_KEY_UNKNOWN;
}

/* Ceiling for `parent_index`, walking the sibling list once if it is unknown.
 * The walk costs what a single by-sub-id scan costs, and every lookup until the
 * next invalidating mutation is then O(1). */
static int32_t
uitree_child_key_ceiling(
    struct UITree* tree,
    int32_t parent_index)
{
    struct UITreeComponent* parent = &tree->components[parent_index];
    if( parent->child_key_max != UITREE_CHILD_KEY_UNKNOWN )
        return parent->child_key_max;

    int32_t max = UITREE_CHILD_KEY_NONE;
    for( int32_t child = parent->first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        int32_t const key = uitree_child_key(&tree->components[child]);
        if( max == UITREE_CHILD_KEY_NONE || key > max )
            max = key;
    }
    parent->child_key_max = max;
    return max;
}

/* Append `child_index` to `parent_index`'s sibling list. The parent's
 * last_child_hint short-circuits the walk to the tail; it is only trusted when
 * it still looks like a live last child of this parent (and is not the node
 * being linked, which a recycled slot can make it), so a stale hint costs one
 * validation and falls back to the walk. */
static void
uitree_append_child(
    struct UITree* tree,
    int32_t parent_index,
    int32_t child_index)
{
    struct UITreeComponent* parent = &tree->components[parent_index];
    if( parent->first_child < 0 )
    {
        parent->first_child = child_index;
        parent->last_child_hint = child_index;
        return;
    }

    int32_t walk = parent->last_child_hint;
    if( walk < 0 || (uint32_t)walk >= tree->component_count || walk == child_index ||
        tree->components[walk].freed || tree->components[walk].parent != parent_index ||
        tree->components[walk].next_sibling >= 0 )
    {
        walk = parent->first_child;
        while( tree->components[walk].next_sibling >= 0 )
            walk = tree->components[walk].next_sibling;
    }
    if( walk == child_index )
        return; /* already the tail */
    tree->components[walk].next_sibling = child_index;
    parent->last_child_hint = child_index;
}

static int32_t
link_under_parent(
    struct UITree* tree,
    int32_t parent_index,
    int32_t new_index)
{
    struct UITreeComponent* new_c = &tree->components[new_index];
    new_c->parent = parent_index;
    /* Preserve first_child (bake may attach children before the parent is
     * linked into the root list). */
    new_c->next_sibling = -1;

    if( parent_index >= 0 && (uint32_t)parent_index >= tree->component_count )
    {
        fprintf(
            stderr,
            "uitree: invalid parent index %d for child %d (count=%u)\n",
            (int)parent_index,
            (int)new_index,
            tree->component_count);
        parent_index = -1;
        new_c->parent = -1;
    }

    if( parent_index < 0 )
    {
        new_c->parent = -1;
        if( tree->root_index < 0 )
        {
            tree->root_index = new_index;
            tree->last_root_index = new_index;
        }
        else
        {
            int32_t walk = tree->last_root_index;
            if( walk < 0 || (uint32_t)walk >= tree->component_count || walk == new_index ||
                tree->components[walk].parent >= 0 )
            {
                walk = tree->root_index;
                while( tree->components[walk].next_sibling >= 0 )
                    walk = tree->components[walk].next_sibling;
            }
            if( walk != new_index )
                tree->components[walk].next_sibling = new_index;
            tree->last_root_index = new_index;
        }
        return new_index;
    }

    uitree_append_child(tree, parent_index, new_index);
    return new_index;
}

void
UITree_LinkUnderParent(
    struct UITree* tree,
    int32_t parent_index,
    int32_t child_index)
{
    assert(tree);
    assert(child_index >= 0 && (uint32_t)child_index < tree->component_count);
    if( parent_index == UITREE_PARENT_UNLINKED )
        return;
    link_under_parent(tree, parent_index, child_index);
}

static int32_t
push_element_unlinked(struct UITree* tree)
{
    int32_t idx;

    /* Reuse a reclaimed slot before growing — keeps the array bounded under the
     * CC_DELETEALL + CC_CREATE rebuild pattern (TS parity: deleted widgets leave
     * the uid map and their storage is recycled). */
    if( tree->free_head >= 0 )
    {
        idx = tree->free_head;
        tree->free_head = tree->components[idx].free_next;
    }
    else
    {
        if( tree->component_count >= tree->component_capacity )
        {
            uint32_t new_capacity =
                tree->component_capacity == 0 ? 16 : tree->component_capacity * 2;
            struct UITreeComponent* new_components =
                realloc(tree->components, new_capacity * sizeof(struct UITreeComponent));
            if( !new_components )
                return -1;
            tree->components = new_components;
            tree->component_capacity = new_capacity;
        }
        idx = (int32_t)tree->component_count++;
    }

    struct UITreeComponent* component = &tree->components[idx];
    memset(component, 0, sizeof(struct UITreeComponent));
    component->parent = -1;
    component->first_child = -1;
    component->next_sibling = -1;
    component->last_child_hint = -1;
    component->child_key_max = UITREE_CHILD_KEY_NONE;
    component->free_next = -1;
    component->component_id = -1;
    component->behavior.over_layer_id = -1;
    component->drag_render_area_uid = -1;
    component->drag_render_area_child_index = -1;
    component->drag_visual_trans = -1;
    component->is_dirty = 1;
    tree->generation++;
    return idx;
}

static int32_t
push_element(
    struct UITree* tree,
    int32_t parent_index)
{
    int32_t idx = push_element_unlinked(tree);
    if( idx < 0 )
        return -1;
    if( parent_index == UITREE_PARENT_UNLINKED )
        return idx;
    link_under_parent(tree, parent_index, idx);
    return idx;
}

static int
UITree_AllocateDynamicComponentId(
    struct UITree* tree,
    int iface_id)
{
    assert(tree);

    uint16_t next = tree->next_dynamic_uid;
    if( next < 0x8000u )
        next = 0x8000u;

    for( int i = 0; i < 0x8000; i++ )
    {
        uint16_t const child_id = next;
        int const uid = (iface_id << 16) | (int)child_id;
        next = (uint16_t)((child_id + 1u) & 0xffffu);
        if( next < 0x8000u )
            next = 0x8000u;
        if( UITree_FindByComponentId(tree, uid) < 0 )
        {
            tree->next_dynamic_uid = next;
            return uid;
        }
    }

    for( uint16_t child_id = 0x8000u; child_id != 0u; child_id++ )
    {
        int const uid = (iface_id << 16) | (int)child_id;
        if( UITree_FindByComponentId(tree, uid) < 0 )
            return uid;
    }
    return (iface_id << 16) | 0xffff;
}

static int32_t
UITree_ResolveComponentTarget(
    struct UITree const* tree,
    int component_id,
    int active_component)
{
    if( component_id >= 0 )
        return UITree_FindByComponentId(tree, component_id);
    if( active_component >= 0 )
        return UITree_FindByComponentId(tree, active_component);
    return -1;
}

static void
UITree_UnlinkChild(
    struct UITree* tree,
    int32_t parent_index,
    int32_t child_index)
{
    if( parent_index < 0 || child_index < 0 || (uint32_t)parent_index >= tree->component_count ||
        (uint32_t)child_index >= tree->component_count )
        return;

    struct UITreeComponent* parent = &tree->components[parent_index];
    int32_t prev = -1;
    int32_t walk = parent->first_child;
    while( walk >= 0 )
    {
        if( walk == child_index )
        {
            int32_t const next = tree->components[walk].next_sibling;
            if( prev < 0 )
                parent->first_child = next;
            else
                tree->components[prev].next_sibling = next;
            uitree_child_key_removed(tree, parent_index, walk);
            tree->components[walk].parent = -1;
            tree->components[walk].next_sibling = -1;
            parent->is_dirty = 1;
            tree->generation++;
            return;
        }
        prev = walk;
        walk = tree->components[walk].next_sibling;
    }
}

static void
UITree_UnlinkFromRootList(
    struct UITree* tree,
    int32_t child_index)
{
    assert(tree);
    assert(child_index >= 0 && (uint32_t)child_index < tree->component_count);

    if( tree->root_index < 0 )
        return;

    if( tree->root_index == child_index )
    {
        tree->root_index = tree->components[child_index].next_sibling;
        if( tree->last_root_index == child_index )
            tree->last_root_index = tree->root_index;
        tree->components[child_index].next_sibling = -1;
        tree->components[child_index].parent = -1;
        tree->generation++;
        return;
    }

    int32_t prev = tree->root_index;
    int32_t walk = tree->components[prev].next_sibling;
    while( walk >= 0 )
    {
        if( walk == child_index )
        {
            tree->components[prev].next_sibling = tree->components[walk].next_sibling;
            if( tree->last_root_index == child_index )
                tree->last_root_index = prev;
            tree->components[walk].next_sibling = -1;
            tree->components[walk].parent = -1;
            tree->generation++;
            return;
        }
        prev = walk;
        walk = tree->components[walk].next_sibling;
    }
}

void
UITree_Reparent(
    struct UITree* tree,
    int32_t child_index,
    int32_t new_parent_index)
{
    assert(tree);
    assert(child_index >= 0 && (uint32_t)child_index < tree->component_count);
    if( new_parent_index >= 0 )
        assert((uint32_t)new_parent_index < tree->component_count);
    assert(child_index != new_parent_index);

    struct UITreeComponent* child = &tree->components[child_index];
    int32_t old_parent = child->parent;
    if( old_parent == new_parent_index )
        return;

    if( old_parent >= 0 )
        UITree_UnlinkChild(tree, old_parent, child_index);
    else
        UITree_UnlinkFromRootList(tree, child_index);

    /* Preserve first_child subtree; only splice into new sibling list. */
    child->parent = new_parent_index;
    child->next_sibling = -1;
    child->is_dirty = 1;
    child->position.layout_resolved = 0;
    tree->layout_stale = 1;

    if( new_parent_index < 0 )
    {
        if( tree->root_index < 0 )
        {
            tree->root_index = child_index;
            tree->last_root_index = child_index;
        }
        else
        {
            int32_t walk = tree->last_root_index;
            if( walk < 0 || (uint32_t)walk >= tree->component_count ||
                tree->components[walk].parent >= 0 )
            {
                walk = tree->root_index;
                while( tree->components[walk].next_sibling >= 0 )
                    walk = tree->components[walk].next_sibling;
            }
            tree->components[walk].next_sibling = child_index;
            tree->last_root_index = child_index;
        }
    }
    else
    {
        uitree_append_child(tree, new_parent_index, child_index);
        uitree_child_key_added(tree, new_parent_index, child_index);
        tree->components[new_parent_index].is_dirty = 1;
    }
    tree->generation++;
}

uint32_t
UITree_HotkeyEffectFromName(char const* name)
{
    if( !name || name[0] == '\0' )
        return 0;
    if( strcmp(name, "select_tab") == 0 )
        return UITREE_HOTKEY_EFFECT_SELECT_TAB;
    return 0;
}

char const*
UITree_ComponentTypeStr(enum UITreeComponentType type)
{
    switch( type )
    {
    case UIELEM_BUILTIN_COMPASS:
        return "compass";
    case UIELEM_BUILTIN_CROSS:
        return "cross";
    case UIELEM_BUILTIN_MINIMENU:
        return "minimenu";
    case UIELEM_BUILTIN_HOVERTEXT:
        return "hovertext";
    case UIELEM_BUILTIN_ENTITY_OVERLAY:
        return "entity_overlay";
    case UIELEM_BUILTIN_MINIMAP:
        return "minimap";
    case UIELEM_BUILTIN_WORLD:
        return "world";
    case UIELEM_BUILTIN_SIDEBAR:
        return "sidebar";
    case UIELEM_BUILTIN_CHAT:
        return "chat";
    case UIELEM_BUILTIN_CHAT_BUTTON:
        return "chat_button";
    case UIELEM_BUILTIN_SPRITE:
        return "sprite";
    case UIELEM_BUILTIN_REDSTONE_TAB:
        return "redstone_tab";
    case UIELEM_BUILTIN_TAB_ICONS:
        return "tab_icons";
    case UIELEM_BUILTIN_PLAYERMODEL:
        return "playermodel";
    case UIELEM_RS_TEXT:
        return "rs_text";
    case UIELEM_RS_GRAPHIC:
        return "rs_graphic";
    case UIELEM_RS_MODEL:
        return "rs_model";
    case UIELEM_RS_INV:
        return "rs_inv";
    case UIELEM_RS_LAYER:
        return "rs_layer";
    case UIELEM_RS_RECT:
        return "rs_rect";
    case UIELEM_RS_LINE:
        return "rs_line";
    case UIELEM_RS_INV_TEXT:
        return "rs_inv_text";
    case UIELEM_CC_OBJ:
        return "cc_obj";
    }
    return "unknown";
}

struct UITree*
UITree_New(uint32_t hint)
{
    (void)hint;
    struct UITree* tree = malloc(sizeof(struct UITree));
    assert(tree);

    memset(tree, 0, sizeof(struct UITree));
    tree->root_index = -1;
    tree->last_root_index = -1;
    tree->free_head = -1;
    return tree;
}

/* Free a component's heap-owned resources and NULL the pointers so the slot is
 * safe to reuse and UITree_Free cannot double-free. */
static void
uitree_component_free_owned(struct UITreeComponent* c)
{
    if( c->type == UIELEM_RS_TEXT && c->u.rs_text.text )
    {
        free((void*)c->u.rs_text.text);
        c->u.rs_text.text = NULL;
    }
    if( c->type == UIELEM_RS_TEXT && c->u.rs_text.text_active )
    {
        free((void*)c->u.rs_text.text_active);
        c->u.rs_text.text_active = NULL;
    }
    struct UITreeBehavior* b = &c->behavior;
    if( b->scripts )
    {
        for( int s = 0; s < b->scripts_count; s++ )
            free(b->scripts[s]);
        free(b->scripts);
        b->scripts = NULL;
    }
    free(b->scripts_lengths);
    b->scripts_lengths = NULL;
    free(b->script_comparator);
    b->script_comparator = NULL;
    free(b->script_operand);
    b->script_operand = NULL;
    b->scripts_count = 0;
    UITree_MenuSubmenuFree(&c->menu_options);
}

/* Reclaim an already-unlinked component and its entire subtree: free owned
 * resources, clear the slot (component_id=-1 removes it from id lookups and
 * frees its uid for reuse), and push it onto the tree free-list. */
static void
uitree_reclaim_subtree(
    struct UITree* tree,
    int32_t idx)
{
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;

    struct UITreeComponent* c = &tree->components[idx];
    if( c->freed )
        return;

    /* Callers unlink before reclaiming, but a reclaim of a still-linked node must
     * not leave its parent claiming a key that just went away. */
    uitree_child_key_removed(tree, c->parent, idx);

    int32_t child = c->first_child;
    while( child >= 0 )
    {
        int32_t const next = tree->components[child].next_sibling;
        uitree_reclaim_subtree(tree, child);
        child = next;
    }

    uitree_component_free_owned(c);
    memset(c, 0, sizeof(*c));
    c->parent = -1;
    c->first_child = -1;
    c->next_sibling = -1;
    c->last_child_hint = -1;
    c->child_key_max = UITREE_CHILD_KEY_NONE;
    c->component_id = -1;
    c->freed = 1;
    c->free_next = tree->free_head;
    tree->free_head = idx;
    tree->id_generation++;
}

void
UITree_Free(struct UITree* tree)
{
    assert(tree);

    for( uint32_t i = 0; i < tree->component_count; i++ )
        uitree_component_free_owned(&tree->components[i]);
    free(tree->id_index_keys);
    free(tree->id_index_vals);
    free(tree->layout_order);
    free(tree->layout_depth);
    free(tree->layout_abs_x);
    free(tree->layout_abs_y);
    free(tree->layout_abs_w);
    free(tree->layout_abs_h);
    free(tree->components);
    free(tree);
}

void
UITree_MarkAllDirty(struct UITree* tree)
{
    assert(tree);

    for( uint32_t i = 0; i < tree->component_count; i++ )
        tree->components[i].is_dirty = 1;
}

void
UITree_MarkNodeDirty(
    struct UITree* tree,
    int32_t idx)
{
    assert(tree);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;
    tree->components[idx].is_dirty = 1;
}

void
UITree_ClearNodeDirty(
    struct UITree* tree,
    int32_t idx)
{
    assert(tree);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;
    tree->components[idx].is_dirty = 0;
}

bool
UITree_NodeNeedsEmit(struct UITreeComponent const* component)
{
    assert(component);
    return component->is_dirty || component->always_dirty;
}

bool
UITree_TypeIsAlwaysDirtyFrame(enum UITreeComponentType type)
{
    return type == UIELEM_BUILTIN_WORLD || type == UIELEM_BUILTIN_MINIMAP ||
           type == UIELEM_BUILTIN_COMPASS || type == UIELEM_BUILTIN_CROSS ||
           type == UIELEM_BUILTIN_MINIMENU || type == UIELEM_BUILTIN_HOVERTEXT;
}

void
UITree_MarkFrameAlwaysDirtyTypes(struct UITree* tree)
{
    assert(tree);

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent* c = &tree->components[i];
        if( UITree_TypeIsAlwaysDirtyFrame(c->type) )
        {
            c->is_dirty = 1;
            if( !c->always_dirty )
                c->always_dirty = 1;
        }
    }
}

/* Original O(n) semantics, kept as the allocation-failure fallback and (when
 * UITREE_ID_INDEX_VERIFY is defined) as the correctness oracle for the index. */
static int32_t
UITree_FindByComponentId_Linear(
    struct UITree const* tree,
    int component_id)
{
    int32_t fallback = -1;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].component_id != component_id )
            continue;
        if( tree->components[i].dynamic )
            return (int32_t)i;
        if( fallback < 0 )
            fallback = (int32_t)i;
    }
    return fallback;
}

static inline uint32_t
uitree_id_hash(int component_id)
{
    /* Fibonacci hashing; the low bits are masked to the table size by the caller. */
    return (uint32_t)component_id * 2654435761u;
}

/* Insert (or resolve a tie for) one component into the open-addressed map.
 * Reproduces the linear scan's winner for an id: a dynamic node beats a
 * non-dynamic one, and within a class the lowest array index wins. The rule is
 * stated in terms of the two candidates rather than insertion order, because
 * incremental inserts (UITree_Push reusing a free-list slot) do not arrive in
 * ascending index order the way a full rebuild's sweep does. */
static void
uitree_id_index_put(struct UITree* tree, int component_id, int32_t idx)
{
    uint32_t const mask = tree->id_index_cap - 1;
    uint32_t h = uitree_id_hash(component_id) & mask;
    for( ;; )
    {
        int32_t const k = tree->id_index_keys[h];
        if( k < 0 )
        {
            tree->id_index_keys[h] = component_id;
            tree->id_index_vals[h] = idx;
            return;
        }
        if( k == component_id )
        {
            int32_t const cur = tree->id_index_vals[h];
            int const new_dyn = tree->components[idx].dynamic ? 1 : 0;
            int const cur_dyn = tree->components[cur].dynamic ? 1 : 0;
            if( new_dyn != cur_dyn ? new_dyn : idx < cur )
                tree->id_index_vals[h] = idx;
            return;
        }
        h = (h + 1) & mask;
    }
}

/* Rebuild the id->index map from the current component array. Returns false if
 * the backing storage could not be (re)allocated, in which case callers fall
 * back to the linear scan. */
static bool
UITree_RebuildIdIndex(struct UITree* tree)
{
    uint32_t cap = tree->id_index_cap ? tree->id_index_cap : 16;
    while( cap < tree->component_count * 2u )
        cap <<= 1;

    if( cap != tree->id_index_cap || !tree->id_index_keys )
    {
        int32_t* keys = realloc(tree->id_index_keys, cap * sizeof(int32_t));
        int32_t* vals = realloc(tree->id_index_vals, cap * sizeof(int32_t));
        if( keys )
            tree->id_index_keys = keys;
        if( vals )
            tree->id_index_vals = vals;
        if( !keys || !vals )
            return false;
        tree->id_index_cap = cap;
    }

    for( uint32_t i = 0; i < tree->id_index_cap; i++ )
        tree->id_index_keys[i] = -1;

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        int const id = tree->components[i].component_id;
        if( id >= 0 )
            uitree_id_index_put(tree, id, (int32_t)i);
    }

    tree->id_index_gen = tree->id_generation;
    tree->id_index_valid = 1;
    return true;
}

/* Record that `idx` just had its component_id assigned (UITree_Push). Bumps
 * id_generation and, when the map is currently in step with it and has room,
 * folds the new id in so the map stays usable — otherwise the bump alone leaves
 * it stale and the next lookup rebuilds. Must be called after component_id and
 * `dynamic` are both set, since the tie-break reads both. */
static void
uitree_id_index_note_added(struct UITree* tree, int32_t idx)
{
    bool const in_step = tree->id_index_valid && tree->id_index_gen == tree->id_generation;
    tree->id_generation++;
    if( !in_step )
        return;

    int const id = tree->components[idx].component_id;
    /* Keep load factor <= 0.5; growing means rehashing, so leave that to the
     * next lookup's rebuild (which also picks the new capacity). */
    if( id >= 0 )
    {
        if( tree->id_index_cap < tree->component_count * 2u )
            return;
        uitree_id_index_put(tree, id, idx);
    }
    tree->id_index_gen = tree->id_generation;
}

int32_t
UITree_FindByComponentId(
    struct UITree const* tree,
    int component_id)
{
    assert(tree);
    if( component_id < 0 || !tree->components )
        return -1;

    /* The map is a cache; refreshing it does not change the tree's logical state,
     * so mutate through the const handle. Keyed on id_generation (id assignments
     * and reclaims only) — topology churn does not invalidate id lookups. */
    struct UITree* t = (struct UITree*)tree;
    if( !t->id_index_valid || t->id_index_gen != t->id_generation )
    {
        if( !UITree_RebuildIdIndex(t) )
            return UITree_FindByComponentId_Linear(tree, component_id);
    }

    int32_t result = -1;
    uint32_t const mask = t->id_index_cap - 1;
    uint32_t h = uitree_id_hash(component_id) & mask;
    for( ;; )
    {
        int32_t const k = t->id_index_keys[h];
        if( k < 0 )
            break;
        if( k == component_id )
        {
            result = t->id_index_vals[h];
            break;
        }
        h = (h + 1) & mask;
    }

#ifdef UITREE_ID_INDEX_VERIFY
    assert(result == UITree_FindByComponentId_Linear(tree, component_id));
#endif
    return result;
}

void
UITree_WalkAdvance(
    struct UITree const* tree,
    int32_t* io_current,
    int32_t* stack,
    int* io_stack_top,
    int stack_max,
    bool current_visible)
{
    assert(tree);
    assert(io_current);
    if( *io_current < 0 )
        return;

    struct UITreeComponent const* c = &tree->components[*io_current];

    if( c->first_child >= 0 && current_visible && io_stack_top && stack &&
        *io_stack_top + 1 < stack_max )
    {
        stack[++(*io_stack_top)] = *io_current;
        *io_current = c->first_child;
        return;
    }

    if( c->next_sibling >= 0 )
    {
        *io_current = c->next_sibling;
        return;
    }

    if( !io_stack_top || !stack )
    {
        *io_current = -1;
        return;
    }

    while( *io_stack_top >= 0 )
    {
        int32_t parent_index = stack[(*io_stack_top)--];
        struct UITreeComponent const* parent = &tree->components[parent_index];
        if( parent->next_sibling >= 0 )
        {
            *io_current = parent->next_sibling;
            return;
        }
    }
    *io_current = -1;
}

void
UITree_SetBehavior(
    struct UITree* tree,
    int32_t idx,
    struct UITreeBehavior const* src)
{
    assert(tree);
    assert(src);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;

    struct UITreeComponent* c = &tree->components[idx];
    struct UITreeBehavior* dst = &c->behavior;

    if( dst->scripts )
    {
        for( int s = 0; s < dst->scripts_count; s++ )
            free(dst->scripts[s]);
        free(dst->scripts);
    }
    free(dst->scripts_lengths);
    free(dst->script_comparator);
    free(dst->script_operand);
    memset(dst, 0, sizeof(*dst));

    dst->hide = src->hide;
    dst->button_type = src->button_type;
    dst->client_code = src->client_code;
    dst->click_mask = src->click_mask;
    dst->over_layer_id = src->over_layer_id;
    dst->over_color = src->over_color;
    dst->active_color = src->active_color;
    dst->active_over_color = src->active_over_color;
    dst->scripts_count = src->scripts_count;
    dst->comparator_count = src->comparator_count;
    dst->script_kind = src->script_kind;

    if( src->scripts_count <= 0 || !src->scripts )
        return;

    dst->scripts = calloc((size_t)src->scripts_count, sizeof(int*));
    dst->scripts_lengths = calloc((size_t)src->scripts_count, sizeof(int));
    if( !dst->scripts || !dst->scripts_lengths )
        goto fail;

    for( int i = 0; i < src->scripts_count; i++ )
    {
        if( !src->scripts[i] )
            continue;
        int len =
            (src->scripts_lengths && src->scripts_lengths[i] > 0) ? src->scripts_lengths[i] : 0;
        if( len <= 0 )
            continue;
        dst->scripts[i] = malloc((size_t)len * sizeof(int));
        if( !dst->scripts[i] )
            goto fail;
        memcpy(dst->scripts[i], src->scripts[i], (size_t)len * sizeof(int));
        dst->scripts_lengths[i] = len;
    }

    /* Comparator arrays are sized by their own count, not scripts_count. */
    if( src->script_comparator && src->comparator_count > 0 )
    {
        dst->script_comparator = malloc((size_t)src->comparator_count * sizeof(int));
        if( !dst->script_comparator )
            goto fail;
        memcpy(
            dst->script_comparator,
            src->script_comparator,
            (size_t)src->comparator_count * sizeof(int));
    }

    if( src->script_operand && src->comparator_count > 0 )
    {
        dst->script_operand = malloc((size_t)src->comparator_count * sizeof(int));
        if( !dst->script_operand )
            goto fail;
        memcpy(
            dst->script_operand, src->script_operand, (size_t)src->comparator_count * sizeof(int));
    }
    return;

fail:
    if( dst->scripts )
    {
        for( int s = 0; s < dst->scripts_count; s++ )
            free(dst->scripts[s]);
        free(dst->scripts);
    }
    free(dst->scripts_lengths);
    free(dst->script_comparator);
    free(dst->script_operand);
    memset(dst, 0, sizeof(*dst));
}

int32_t
UITree_Push(
    struct UITree* tree,
    int32_t parent_index,
    struct UITreeNodeSpec const* spec)
{
    assert(tree);
    assert(spec);

    char* text_owned = NULL;
    char* text_active_owned = NULL;
    if( spec->type == UIELEM_RS_TEXT && spec->u.rs_text.text )
    {
        text_owned = strdup(spec->u.rs_text.text);
        if( !text_owned )
            return -1;
    }
    if( spec->type == UIELEM_RS_TEXT && spec->u.rs_text.text_active )
    {
        text_active_owned = strdup(spec->u.rs_text.text_active);
        if( !text_active_owned )
        {
            free(text_owned);
            return -1;
        }
    }

    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
    {
        free(text_owned);
        free(text_active_owned);
        return -1;
    }

    struct UITreeComponent* component = &tree->components[idx];
    component->type = spec->type;
    component->component_id = spec->component_id;
    component->dynamic = spec->dynamic ? 1 : 0;
    uitree_id_index_note_added(tree, idx);
    component->dynamic_child_index = spec->dynamic ? spec->dynamic_child_index : -1;
    /* Both halves of the sub-id key are set now, so the parent's key ceiling can
     * absorb this child (push_element already linked it). */
    uitree_child_key_added(tree, component->parent, idx);
    /* Specs carry labels and ops by value but never a submenu block (those only
     * arrive later, via CC/IF_SETOPSUBMENU) — copy through so a spec that ever
     * grows one is duplicated instead of aliased into the node. */
    uitree_menu_options_copy(&component->menu_options, &spec->menu_options);
    component->slot_tag = spec->slot_tag;
    component->hotkey_effects = spec->hotkey_effects;

    if( spec->has_position )
    {
        component->position = spec->position;
        component->position.layout_resolved = 0;
        component->position.abs_x = 0;
        component->position.abs_y = 0;
        component->position.abs_w = 0;
        component->position.abs_h = 0;
    }
    else
    {
        component->position.kind = UIPOS_XY;
        component->position.x = spec->x;
        component->position.y = spec->y;
        component->position.width = spec->width;
        component->position.height = spec->height;
        component->position.anchor_x = spec->anchor_x;
        component->position.anchor_y = spec->anchor_y;
        component->position.x_mode = -1;
        component->position.y_mode = -1;
        component->position.width_mode = -1;
        component->position.height_mode = -1;
    }
    tree->layout_stale = 1;

    switch( spec->type )
    {
    case UIELEM_BUILTIN_SPRITE:
    case UIELEM_BUILTIN_COMPASS:
    case UIELEM_BUILTIN_CROSS:
        component->u.sprite.scene_id = spec->u.sprite.scene_id;
        component->u.sprite.atlas_index = spec->u.sprite.atlas_index;
        component->u.sprite.mask_scene_id = spec->u.sprite.mask_scene_id;
        component->u.sprite.mask_atlas_index = spec->u.sprite.mask_atlas_index;
        break;

    case UIELEM_BUILTIN_MINIMENU:
        component->u.minimenu.font_id = spec->u.minimenu.font_id;
        break;

    case UIELEM_BUILTIN_HOVERTEXT:
        component->u.hovertext.font_id = spec->u.hovertext.font_id;
        break;

    case UIELEM_BUILTIN_CHAT:
        component->u.chat.minimenu = spec->u.chat.minimenu;
        component->u.chat.font_id = spec->u.chat.font_id;
        break;

    case UIELEM_BUILTIN_CHAT_BUTTON:
        component->u.chat_button = spec->u.chat_button;
        component->is_dirty = 1;
        break;

    case UIELEM_BUILTIN_REDSTONE_TAB:
        component->u.redstone_tab.tabno = spec->u.redstone_tab.tabno;
        component->u.redstone_tab.scene_id = spec->u.redstone_tab.scene_id;
        component->u.redstone_tab.atlas_index = spec->u.redstone_tab.atlas_index;
        component->u.redstone_tab.scene_id_active = spec->u.redstone_tab.scene_id_active;
        component->u.redstone_tab.atlas_index_active = spec->u.redstone_tab.atlas_index_active;
        break;

    case UIELEM_BUILTIN_MINIMAP:
        component->u.minimap.scene_id = spec->u.minimap.scene_id;
        component->u.minimap.mask_scene_id = spec->u.minimap.mask_scene_id;
        component->u.minimap.mask_atlas_index = spec->u.minimap.mask_atlas_index;
        break;

    case UIELEM_BUILTIN_WORLD:
        component->u.world.level_mask = spec->u.world.level_mask;
        component->u.world.mmb_rotate = spec->u.world.mmb_rotate;
        component->u.world.wheel_zoom = spec->u.world.wheel_zoom;
        break;

    case UIELEM_BUILTIN_SIDEBAR:
        component->u.sidebar.tabno = spec->u.sidebar.tabno;
        component->u.sidebar.componentno = spec->u.sidebar.componentno;
        component->u.sidebar.inv_source_id = spec->u.sidebar.inv_source_id;
        component->u.sidebar.selected = spec->u.sidebar.selected;
        break;

    case UIELEM_RS_LAYER:
        component->u.rs_layer.scroll_height = spec->u.rs_layer.scroll_height;
        component->u.rs_layer.scroll_width = spec->u.rs_layer.scroll_width;
        break;

    case UIELEM_RS_TEXT:
    {
        int font_id = spec->u.rs_text.font_id;
        if( font_id < 0 )
            font_id = 1;
        component->u.rs_text.font_id = font_id;
        component->u.rs_text.color = spec->u.rs_text.color;
        component->u.rs_text.center = spec->u.rs_text.center;
        component->u.rs_text.y_align = spec->u.rs_text.y_align;
        component->u.rs_text.line_height = spec->u.rs_text.line_height;
        component->u.rs_text.shadowed = spec->u.rs_text.shadowed;
        component->u.rs_text.text = text_owned;
        component->u.rs_text.text_active = text_active_owned;
        text_owned = NULL;
        text_active_owned = NULL;
        break;
    }

    case UIELEM_RS_GRAPHIC:
        component->u.rs_graphic.scene_id = spec->u.rs_graphic.scene_id;
        component->u.rs_graphic.atlas_index = spec->u.rs_graphic.atlas_index;
        component->u.rs_graphic.scene_id_active = spec->u.rs_graphic.scene_id_active;
        component->u.rs_graphic.atlas_index_active = spec->u.rs_graphic.atlas_index_active;
        component->u.rs_graphic.graphic_hitbox_only = spec->u.rs_graphic.graphic_hitbox_only;
        component->u.rs_graphic.tiled = spec->u.rs_graphic.tiled;
        component->u.rs_graphic.outline = spec->u.rs_graphic.outline;
        component->u.rs_graphic.graphic_shadow = spec->u.rs_graphic.graphic_shadow;
        component->u.rs_graphic.flip_h = spec->u.rs_graphic.flip_h;
        component->u.rs_graphic.flip_v = spec->u.rs_graphic.flip_v;
        break;

    case UIELEM_RS_RECT:
        component->u.rs_rect.color = spec->u.rs_rect.color;
        component->u.rs_rect.filled = spec->u.rs_rect.filled;
        break;

    case UIELEM_RS_MODEL:
        component->u.rs_model.gamecache_model_id = spec->u.rs_model.gamecache_model_id;
        component->u.rs_model.zoom = spec->u.rs_model.zoom;
        component->u.rs_model.xan = spec->u.rs_model.xan;
        component->u.rs_model.yan = spec->u.rs_model.yan;
        component->u.rs_model.zan = spec->u.rs_model.zan;
        component->u.rs_model.x_offset = spec->u.rs_model.x_offset;
        component->u.rs_model.y_offset = spec->u.rs_model.y_offset;
        component->u.rs_model.orthog = spec->u.rs_model.orthog;
        component->u.rs_model.fixed_zoom = spec->u.rs_model.fixed_zoom;
        component->u.rs_model.anim_seq_id = spec->u.rs_model.anim_seq_id;
        component->u.rs_model.anim_frame = spec->u.rs_model.anim_frame;
        component->u.rs_model.anim_frame_cycle = 0;
        component->u.rs_model.anim_hold = spec->u.rs_model.anim_hold;
        break;

    case UIELEM_RS_INV:
        component->u.rs_inv.inv_source_id = spec->u.rs_inv.inv_source_id;
        component->u.rs_inv.cols = spec->u.rs_inv.cols;
        component->u.rs_inv.rows = spec->u.rs_inv.rows;
        component->u.rs_inv.margin_x = spec->u.rs_inv.margin_x;
        component->u.rs_inv.margin_y = spec->u.rs_inv.margin_y;
        component->u.rs_inv.can_drag = spec->u.rs_inv.can_drag;
        component->u.rs_inv.obj_ops = spec->u.rs_inv.obj_ops;
        component->u.rs_inv.obj_use = spec->u.rs_inv.obj_use;
        if( spec->u.rs_inv.inv_slot_offset_x && spec->u.rs_inv.inv_slot_offset_y )
        {
            memcpy(
                component->u.rs_inv.inv_slot_offset_x,
                spec->u.rs_inv.inv_slot_offset_x,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
            memcpy(
                component->u.rs_inv.inv_slot_offset_y,
                spec->u.rs_inv.inv_slot_offset_y,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        }
        if( spec->u.rs_inv.inv_slot_bg_scene_id && spec->u.rs_inv.inv_slot_bg_atlas_index )
        {
            memcpy(
                component->u.rs_inv.inv_slot_bg_scene_id,
                spec->u.rs_inv.inv_slot_bg_scene_id,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
            memcpy(
                component->u.rs_inv.inv_slot_bg_atlas_index,
                spec->u.rs_inv.inv_slot_bg_atlas_index,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        }
        else
        {
            for( int i = 0; i < UI_INV_SLOT_OFFSET_MAX; i++ )
            {
                component->u.rs_inv.inv_slot_bg_scene_id[i] = -1;
                component->u.rs_inv.inv_slot_bg_atlas_index[i] = 0;
            }
        }
        break;

    case UIELEM_CC_OBJ:
        component->u.cc_obj.obj_id = spec->u.cc_obj.obj_id;
        component->u.cc_obj.obj_count = spec->u.cc_obj.obj_count;
        component->u.cc_obj.scene_id = spec->u.cc_obj.scene_id;
        component->u.cc_obj.atlas_index = spec->u.cc_obj.atlas_index;
        break;

    case UIELEM_BUILTIN_TAB_ICONS:
        component->u.tab_icon.scene_id = spec->u.tab_icon.scene_id;
        component->u.tab_icon.atlas_index = spec->u.tab_icon.atlas_index;
        component->u.tab_icon.tabno = spec->u.tab_icon.tabno;
        component->is_dirty = 1;
        break;

    case UIELEM_RS_LINE:
        component->u.rs_line.color = spec->u.rs_line.color;
        component->u.rs_line.line_width =
            spec->u.rs_line.line_width > 0 ? spec->u.rs_line.line_width : 1;
        component->u.rs_line.horizontal = spec->u.rs_line.horizontal ? 1 : 0;
        break;

    case UIELEM_RS_INV_TEXT:
        component->u.rs_inv_text.inv_source_id = spec->u.rs_inv_text.inv_source_id;
        component->u.rs_inv_text.cols = spec->u.rs_inv_text.cols;
        component->u.rs_inv_text.rows = spec->u.rs_inv_text.rows;
        component->u.rs_inv_text.margin_x = spec->u.rs_inv_text.margin_x;
        component->u.rs_inv_text.margin_y = spec->u.rs_inv_text.margin_y;
        component->u.rs_inv_text.font_id = spec->u.rs_inv_text.font_id;
        component->u.rs_inv_text.color = spec->u.rs_inv_text.color;
        component->u.rs_inv_text.center = spec->u.rs_inv_text.center;
        component->u.rs_inv_text.shadowed = spec->u.rs_inv_text.shadowed;
        break;

    default:
        break;
    }

    if( spec->always_dirty )
        component->always_dirty = 1;

    if( spec->behavior )
        UITree_SetBehavior(tree, idx, spec->behavior);

    return idx;
}

void
UITree_ClearSidebarChildren(
    struct UITree* tree,
    int32_t sidebar_idx)
{
    assert(tree);
    if( sidebar_idx < 0 || (uint32_t)sidebar_idx >= tree->component_count )
        return;
    if( tree->components[sidebar_idx].type != UIELEM_BUILTIN_SIDEBAR )
        return;
    UITree_ClearChildren(tree, sidebar_idx);
}

void
UITree_ClearChildren(
    struct UITree* tree,
    int32_t owner_idx)
{
    assert(tree);
    if( owner_idx < 0 || (uint32_t)owner_idx >= tree->component_count )
        return;
    struct UITreeComponent* c = &tree->components[owner_idx];
    /* Reclaim the detached subtree, not just unlink it: an orphaned copy
     * keeps its component_id and shadows the remounted nodes in
     * FindByComponentId/ResolveComponentTarget (server IF_SETTEXT then lands
     * on the invisible orphan — the "Weapon:%1" bug). */
    {
        int32_t child = c->first_child;
        while( child >= 0 )
        {
            int32_t const next = tree->components[child].next_sibling;
            uitree_reclaim_subtree(tree, child);
            child = next;
        }
    }
    c->first_child = -1;
    c->last_child_hint = -1;
    c->child_key_max = UITREE_CHILD_KEY_NONE; /* no children left to match */
    c->is_dirty = 1;
    tree->generation++;
}

int32_t
UITree_FindChildBySubid(
    struct UITree const* tree,
    int32_t parent_index,
    int parent_component_id,
    int sub_id)
{
    (void)parent_component_id;
    assert(tree);
    if( parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return -1;

    /* cc_create asks this once per row it builds, and a rebuild is a run of
     * misses (the rows do not exist yet), so answering a miss without walking
     * the list is what keeps a rebuild linear. The ceiling is a cache on the
     * parent; refreshing it does not change the tree's logical state, so mutate
     * through the const handle (same as UITree_FindByComponentId).
     *
     * Only sub_ids that fit the masked comparison below take this path: for a
     * wider one, a non-dynamic child's masked key can match a key numerically
     * far below the ceiling. */
    if( sub_id >= 0 && sub_id <= 0xFFFF )
    {
        if( sub_id > uitree_child_key_ceiling((struct UITree*)tree, parent_index) )
            return -1;
    }

    /* Dynamic children win over cache-baked ones. The reference's cc_find only
     * ever sees the dynamic array (`component.children[sub]`, filled by
     * cc_create) — a static subcomponent is addressed as group<<16|index by the
     * if_* ops instead, so the two namespaces never collide there. They share a
     * child list here, so a static child whose low uid equals `sub_id` would
     * shadow the dynamic child the script actually built: the world map's
     * cc_find(595|2, 4/5) matched the static 595|4/595|5 — the map frame — and
     * the grip sizing that followed collapsed it to 6x6, leaving a grey screen.
     * The static match stays as a fallback for the trees that rely on it. */
    int32_t static_match = -1;
    for( int32_t child = tree->components[parent_index].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        struct UITreeComponent const* c = &tree->components[child];
        if( c->dynamic && c->dynamic_child_index == sub_id )
            return child;
        if( !c->dynamic && static_match < 0 && (c->component_id & 0xFFFF) == (sub_id & 0xFFFF) )
            static_match = child;
    }
    return static_match;
}

int32_t
UITree_CcCreate(
    struct UITree* tree,
    int32_t parent_index,
    int parent_component_id,
    int widget_type,
    int sub_id)
{
    assert(tree);
    if( parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return -1;

    int const iface_id = parent_component_id >= 0 ? (parent_component_id >> 16) : 0;

    /* Replace-in-slot semantics: reclaim any existing dynamic child with this
     * sub_id BEFORE allocating a uid, so the freed slot and uid are immediately
     * reusable and repeated rebuild scripts don't grow the array. */
    int32_t existing = UITree_FindChildBySubid(tree, parent_index, parent_component_id, sub_id);
    if( existing >= 0 && tree->components[existing].dynamic )
    {
        UITree_UnlinkChild(tree, parent_index, existing);
        uitree_reclaim_subtree(tree, existing);
    }

    int const child_component_id = UITree_AllocateDynamicComponentId(tree, iface_id);

    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.component_id = child_component_id;
    spec.dynamic = 1;
    spec.dynamic_child_index = sub_id;
    spec.always_dirty = 1;
    spec.width = 0;
    spec.height = 0;

    switch( widget_type )
    {
    case 5:
        spec.type = UIELEM_RS_GRAPHIC;
        break;
    case 3:
        spec.type = UIELEM_RS_RECT;
        spec.u.rs_rect.color = 0;
        spec.u.rs_rect.filled = 1;
        break;
    case 4:
        spec.type = UIELEM_RS_TEXT;
        break;
    default:
        spec.type = UIELEM_CC_OBJ;
        break;
    }

    int32_t idx = UITree_Push(tree, parent_index, &spec);
    if( idx < 0 )
        return -1;
    /* Soft3D stretches IF3 graphics to layout size; CC_CREATE children must
     * inherit the parent's if3 flag (interfacex forces if3=1 on create). */
    tree->components[idx].if3 = tree->components[parent_index].if3;
    return idx;
}

int32_t
UITree_CcCopy(
    struct UITree* tree,
    int32_t parent_index,
    int parent_component_id,
    int src_sub_id,
    int dst_sub_id)
{
    assert(tree);
    if( parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return -1;
    if( src_sub_id == dst_sub_id )
        return -1;

    int32_t const src_idx =
        UITree_FindChildBySubid(tree, parent_index, parent_component_id, src_sub_id);
    if( src_idx < 0 )
        return -1;

    /* Copy the payload out before any push/reclaim: both can move or free the
     * components array. Owned strings are re-duplicated below, never shared. */
    struct UITreeComponent const src = tree->components[src_idx];

    int const iface_id = parent_component_id >= 0 ? (parent_component_id >> 16) : 0;

    int32_t existing =
        UITree_FindChildBySubid(tree, parent_index, parent_component_id, dst_sub_id);
    if( existing >= 0 && tree->components[existing].dynamic )
    {
        UITree_UnlinkChild(tree, parent_index, existing);
        uitree_reclaim_subtree(tree, existing);
    }

    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = src.type;
    spec.component_id = UITree_AllocateDynamicComponentId(tree, iface_id);
    spec.dynamic = 1;
    spec.dynamic_child_index = dst_sub_id;
    spec.always_dirty = 1;

    int32_t const idx = UITree_Push(tree, parent_index, &spec);
    if( idx < 0 )
        return -1;

    struct UITreeComponent* dst = &tree->components[idx];

    dst->u = src.u;
    if( src.type == UIELEM_RS_TEXT )
    {
        /* Push allocated nothing for text (spec carried none); own fresh copies. */
        dst->u.rs_text.text = src.u.rs_text.text ? strdup(src.u.rs_text.text) : NULL;
        dst->u.rs_text.text_active =
            src.u.rs_text.text_active ? strdup(src.u.rs_text.text_active) : NULL;
    }

    dst->trans = src.trans;
    dst->if3 = src.if3;
    dst->no_click_through = src.no_click_through;
    dst->draggable = src.draggable;
    dst->drag_behavior = src.drag_behavior;
    dst->drag_dead_zone = src.drag_dead_zone;
    dst->drag_dead_time = src.drag_dead_time;
    dst->model_transparent = src.model_transparent;
    dst->item_id = src.item_id;
    dst->item_count = src.item_count;
    dst->item_scene_id = src.item_scene_id;
    dst->item_atlas_index = src.item_atlas_index;
    dst->target_priority = src.target_priority;
    dst->force_left_click = src.force_left_click;
    dst->position = src.position;
    /* Deep: the submenu block is owned per component, so the copy must not alias
     * the source's (both are reclaimed independently). */
    uitree_menu_options_copy(&dst->menu_options, &src.menu_options);
    dst->runtime_hooks = src.runtime_hooks;
    /* Plain data, but it must be listed explicitly: this function copies field
     * by field rather than by struct assignment, so a template row that binds
     * op keys would silently lose them on copy. */
    dst->op_keys = src.op_keys;

    /* cs1 behavior scripts stay uncopied: dynamic children are driven by cs2
     * hooks, and the script arrays are owned per node. */

    dst->is_dirty = 1;
    tree->generation++;
    return idx;
}

void
UITree_CcDeleteAll(
    struct UITree* tree,
    int32_t parent_index)
{
    assert(tree);
    if( parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return;

    struct UITreeComponent* parent = &tree->components[parent_index];
    int32_t child = parent->first_child;
    int32_t prev = -1;
    while( child >= 0 )
    {
        int32_t const next = tree->components[child].next_sibling;
        if( tree->components[child].dynamic )
        {
            if( prev < 0 )
                parent->first_child = next;
            else
                tree->components[prev].next_sibling = next;
            tree->components[child].parent = -1;
            tree->components[child].next_sibling = -1;
            /* Really delete (TS unregisterWidgetTree parity): recycle the slot
             * and free the uid instead of leaking an orphan that lookups,
             * layout, and uid allocation would keep paying for. */
            uitree_reclaim_subtree(tree, child);
        }
        else
        {
            prev = child;
        }
        child = next;
    }
    /* The splicing above bypasses UITree_UnlinkChild, so the key ceiling is
     * dropped once for the whole batch rather than per row; the next by-sub-id
     * lookup recomputes it over the static children that survived. */
    parent->last_child_hint = -1;
    parent->child_key_max = UITREE_CHILD_KEY_UNKNOWN;
    parent->is_dirty = 1;
    tree->generation++;
}

int
UITree_CollectDynamicChildIndices(
    struct UITree const* tree,
    int parent_component_id,
    int start_index,
    int* out_indices,
    int out_cap)
{
    assert(tree);
    assert(out_indices);
    if( parent_component_id < 0 || out_cap <= 0 )
        return 0;

    int32_t parent_idx = UITree_FindByComponentId(tree, parent_component_id);
    if( parent_idx < 0 )
        return 0;

    int count = 0;
    for( int32_t child = tree->components[parent_idx].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        struct UITreeComponent const* c = &tree->components[child];
        if( c->dynamic_child_index <= start_index )
            continue;
        if( count < out_cap )
            out_indices[count++] = c->dynamic_child_index;
    }

    for( int i = 1; i < count; i++ )
    {
        int key = out_indices[i];
        int j = i - 1;
        while( j >= 0 && out_indices[j] > key )
        {
            out_indices[j + 1] = out_indices[j];
            j--;
        }
        out_indices[j + 1] = key;
    }
    return count;
}

bool
UITree_ApplyHide(
    struct UITree* tree,
    int component_id,
    int hide)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].behavior.hide = hide ? 1 : 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyClickMask(
    struct UITree* tree,
    int component_id,
    int32_t click_mask)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].behavior.click_mask = click_mask;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyText(
    struct UITree* tree,
    int component_id,
    char const* text)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_TEXT )
        return false;
    char* copy = strdup(text ? text : "");
    if( !copy )
        return false;

    free((void*)tree->components[idx].u.rs_text.text);
    tree->components[idx].u.rs_text.text = copy;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyGraphic(
    struct UITree* tree,
    int component_id,
    int scene_id,
    int atlas_index)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    tree->components[idx].u.rs_graphic.scene_id = scene_id;
    tree->components[idx].u.rs_graphic.atlas_index = atlas_index;
    tree->components[idx].u.rs_graphic.graphic_hitbox_only = 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyColour(
    struct UITree* tree,
    int component_id,
    int colour)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    if( tree->components[idx].type == UIELEM_RS_TEXT )
        tree->components[idx].u.rs_text.color = colour;
    else if( tree->components[idx].type == UIELEM_RS_RECT )
        tree->components[idx].u.rs_rect.color = colour;
    else
        return false;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyPosition(
    struct UITree* tree,
    int component_id,
    int x,
    int y)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].position.x = x;
    tree->components[idx].position.y = y;
    tree->components[idx].position.layout_resolved = 0;
    tree->layout_stale = 1;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplySize(
    struct UITree* tree,
    int component_id,
    int width,
    int height)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].position.width = width;
    tree->components[idx].position.height = height;
    tree->components[idx].position.layout_resolved = 0;
    tree->layout_stale = 1;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyPositionModes(
    struct UITree* tree,
    int component_id,
    int x,
    int y,
    int x_mode,
    int y_mode)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].position.x = x;
    tree->components[idx].position.y = y;
    tree->components[idx].position.x_mode = (int8_t)x_mode;
    tree->components[idx].position.y_mode = (int8_t)y_mode;
    tree->components[idx].position.layout_resolved = 0;
    tree->layout_stale = 1;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplySizeModes(
    struct UITree* tree,
    int component_id,
    int width,
    int height,
    int width_mode,
    int height_mode)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].position.width = width;
    tree->components[idx].position.height = height;
    tree->components[idx].position.width_mode = (int8_t)width_mode;
    tree->components[idx].position.height_mode = (int8_t)height_mode;
    tree->components[idx].position.layout_resolved = 0;
    tree->layout_stale = 1;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyGraphicTiled(
    struct UITree* tree,
    int component_id,
    int tiled)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    tree->components[idx].u.rs_graphic.tiled = tiled ? 1 : 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyGraphicOutline(
    struct UITree* tree,
    int component_id,
    int outline)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    tree->components[idx].u.rs_graphic.outline = outline;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyGraphicShadow(
    struct UITree* tree,
    int component_id,
    int shadow_colour)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    tree->components[idx].u.rs_graphic.graphic_shadow = shadow_colour;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyScrollSize(
    struct UITree* tree,
    int component_id,
    int scroll_width,
    int scroll_height)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_LAYER )
        return false;
    tree->components[idx].u.rs_layer.scroll_width = scroll_width;
    tree->components[idx].u.rs_layer.scroll_height = scroll_height;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyScrollPos(
    struct UITree* tree,
    int component_id,
    int scroll_x,
    int scroll_y)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].scroll_x = scroll_x;
    tree->components[idx].scroll_y = scroll_y;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

/* Does `child` sit in an equipment slot — a container the script builds three
 * cc_create children for (d0 border, d1 item overlay, d2 empty silhouette) —
 * rather than in an item grid?
 *
 * A grid (the backpack under 149|0, a bank's rows) puts one cell per sub_id
 * under a single parent, so its d1 and d2 are both real slots. Keying only on
 * "this is d1" therefore made every grid's *third* cell the silhouette of its
 * second: setting an object in slot 1 hid slot 2. The inventory's third square
 * went blank that way, and only came back while the pointer was over it,
 * because the emit walk lets a hovered component through its own hide gate.
 *
 * The shape is the discriminator: a slot container's cells stop at d2, a grid's
 * do not. */
static bool
uitree_parent_is_equipment_slot(
    struct UITree const* tree,
    struct UITreeComponent const* child)
{
    assert(tree);
    assert(child);
    if( child->parent < 0 || (uint32_t)child->parent >= tree->component_count )
        return false;

    for( int32_t sib = tree->components[child->parent].first_child; sib >= 0;
         sib = tree->components[sib].next_sibling )
    {
        struct UITreeComponent const* s = &tree->components[sib];
        if( s->dynamic && s->dynamic_child_index > 2 )
            return false;
    }
    return true;
}

bool
UITree_ApplyObject(
    struct UITree* tree,
    int component_id,
    int obj_id,
    int obj_count,
    int scene_id,
    int atlas_index)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;

    struct UITreeComponent* c = &tree->components[idx];
    if( obj_id > 0 && !c->dynamic && c->first_child >= 0 )
    {
        int32_t overlay_idx = UITree_FindChildBySubid(tree, idx, c->component_id, 1);
        if( overlay_idx >= 0 )
        {
            idx = overlay_idx;
            c = &tree->components[idx];
        }
    }

    /* Equipment slots: d1 = item overlay, d2 = empty silhouette graphic.
     * Only toggle silhouette when applying to the overlay, d2 is chrome (not
     * another CC_OBJ), and the parent is an equipment slot at all — an item
     * *grid* fills one parent with a cell per sub_id, so its d2 is a real slot
     * (see uitree_parent_is_equipment_slot). */
    int const is_equipment_overlay =
        c->dynamic && c->dynamic_child_index == 1 && uitree_parent_is_equipment_slot(tree, c);

    if( obj_id <= 0 )
    {
        c->item_id = 0;
        c->item_count = 0;
        c->item_scene_id = -1;
        c->item_atlas_index = 0;
        if( c->type == UIELEM_CC_OBJ )
        {
            c->u.cc_obj.obj_id = 0;
            c->u.cc_obj.obj_count = 0;
            c->u.cc_obj.scene_id = -1;
            c->u.cc_obj.atlas_index = 0;
        }
        /* RS_GRAPHIC: clear item overlay only — leave rs_graphic.scene_id
         * (SETGRAPHIC chrome / silhouette) intact. */
        /* Scripts often leave d2 hidden after InvTransmit — show it when the
         * overlay is cleared. */
        if( is_equipment_overlay && c->parent >= 0 )
        {
            int32_t parent_idx = c->parent;
            int32_t sil_idx = UITree_FindChildBySubid(
                tree, parent_idx, tree->components[parent_idx].component_id, 2);
            if( sil_idx >= 0 && tree->components[sil_idx].type == UIELEM_RS_GRAPHIC )
                (void)UITree_ApplyHide(tree, tree->components[sil_idx].component_id, 0);
        }
        UITree_MarkNodeDirty(tree, idx);
        return true;
    }

    c->item_id = obj_id;
    c->item_count = obj_count > 0 ? obj_count : 1;
    c->item_scene_id = scene_id;
    c->item_atlas_index = atlas_index;

    if( c->type == UIELEM_CC_OBJ )
    {
        c->u.cc_obj.obj_id = obj_id;
        c->u.cc_obj.obj_count = c->item_count;
        c->u.cc_obj.scene_id = scene_id;
        c->u.cc_obj.atlas_index = atlas_index;
    }
    /* RS_GRAPHIC: item lives in item_id/item_scene_id; do not overwrite
     * rs_graphic.scene_id (SETGRAPHIC chrome). Emit prefers item when set. */

    if( c->behavior.hide )
        c->behavior.hide = 0;
    /* Hide silhouette sibling while an item occupies the equipment slot. */
    if( is_equipment_overlay && c->parent >= 0 )
    {
        int32_t parent_idx = c->parent;
        int32_t sil_idx = UITree_FindChildBySubid(
            tree, parent_idx, tree->components[parent_idx].component_id, 2);
        if( sil_idx >= 0 && tree->components[sil_idx].type == UIELEM_RS_GRAPHIC )
            (void)UITree_ApplyHide(tree, tree->components[sil_idx].component_id, 1);
    }
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyModel(
    struct UITree* tree,
    int component_id,
    int model_id)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_MODEL )
        return false;
    tree->components[idx].u.rs_model.gamecache_model_id = model_id;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyModelTransparent(
    struct UITree* tree,
    int component_id,
    int transparent)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].model_transparent = transparent ? 1 : 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyModelAngle(
    struct UITree* tree,
    int component_id,
    int xan,
    int yan,
    int zoom)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_MODEL )
        return false;
    tree->components[idx].u.rs_model.xan = xan;
    tree->components[idx].u.rs_model.yan = yan;
    if( zoom > 0 )
        tree->components[idx].u.rs_model.zoom = zoom;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyModelAnim(
    struct UITree* tree,
    int component_id,
    int anim_seq_id)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_MODEL )
        return false;
    tree->components[idx].u.rs_model.anim_seq_id = anim_seq_id;
    tree->components[idx].u.rs_model.anim_frame = 0;
    tree->components[idx].u.rs_model.anim_frame_cycle = 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyTextFont(
    struct UITree* tree,
    int component_id,
    int font_id)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_TEXT )
        return false;
    tree->components[idx].u.rs_text.font_id = font_id;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyTextAlign(
    struct UITree* tree,
    int component_id,
    int h_align,
    int v_align,
    int line_height)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_TEXT )
        return false;
    tree->components[idx].u.rs_text.center = h_align;
    tree->components[idx].u.rs_text.y_align = v_align;
    tree->components[idx].u.rs_text.line_height = line_height;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyTextShadow(
    struct UITree* tree,
    int component_id,
    int shadowed)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_TEXT )
        return false;
    tree->components[idx].u.rs_text.shadowed = shadowed ? 1 : 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyTargetPriority(
    struct UITree* tree,
    int component_id,
    int priority)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    int stored;
    if( idx < 0 )
        return false;
    /* Reference: -1 resets to 4; 1..32 stores value-1. Other values are ignored. */
    if( priority == -1 )
        stored = 4;
    else if( priority >= 1 && priority <= 32 )
        stored = priority - 1;
    else
        return false;
    tree->components[idx].target_priority = stored;
    return true;
}

bool
UITree_ApplyForceLeftClick(
    struct UITree* tree,
    int component_id,
    int enabled)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].force_left_click = enabled ? 1 : 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ClearOpSubmenu(
    struct UITree* tree,
    int component_id,
    int op_index)
{
    int32_t idx;
    if( op_index < 1 || op_index > UITREE_SUBMENU_OP_SLOTS )
        return false;
    idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    UITree_MenuSubmenuClear(&tree->components[idx].menu_options, op_index);
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyRuntimeHook(
    struct UITree* tree,
    int component_id,
    struct UITreeRuntimeScriptHook* slot,
    int script_id,
    int const* argv,
    int argc,
    uint32_t str_mask,
    char const* const* strs,
    int str_argc)
{
    (void)tree;
    (void)component_id;
    assert(slot);

    memset(slot, 0, sizeof(*slot));
    slot->script_id = script_id;
    if( argc > UITREE_HOOK_ARG_MAX )
        argc = UITREE_HOOK_ARG_MAX;
    if( argc < 0 )
        argc = 0;
    slot->argc = argc;
    if( argv && argc > 0 )
        memcpy(slot->argv, argv, (size_t)argc * sizeof(int));
    slot->str_mask = str_mask;
    if( str_argc > UITREE_HOOK_STR_ARG_MAX )
        str_argc = UITREE_HOOK_STR_ARG_MAX;
    if( str_argc < 0 || !strs )
        str_argc = 0;
    slot->str_argc = str_argc;
    for( int i = 0; i < str_argc; i++ )
    {
        strncpy(slot->strv[i], strs[i] ? strs[i] : "", UITREE_HOOK_STR_ARG_LEN - 1);
        slot->strv[i][UITREE_HOOK_STR_ARG_LEN - 1] = '\0';
    }
    return true;
}

bool
UITree_ApplyOpBase(
    struct UITree* tree,
    int component_id,
    char const* text)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    strncpy(
        tree->components[idx].menu_options.option, text ? text : "", UITREE_MENU_OPTION_LEN - 1);
    tree->components[idx].menu_options.option[UITREE_MENU_OPTION_LEN - 1] = '\0';
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

/* Op-key bindings have no visual effect, so unlike the op/text mutators these
 * deliberately do not mark the node dirty. */
static struct UITreeOpKeyBinding*
uitree_opkey_slot(
    struct UITree* tree,
    int component_id,
    int op_index,
    struct UITreeComponent** out_node)
{
    int32_t idx;

    if( op_index < 1 || op_index > UITREE_OPKEY_SLOTS )
        return NULL;
    idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return NULL;
    if( out_node )
        *out_node = &tree->components[idx];
    return &tree->components[idx].op_keys.slots[op_index - 1];
}

static void
uitree_opkey_refresh_has_bindings(struct UITreeComponent* node)
{
    node->op_keys.has_bindings = 0;
    for( int i = 0; i < UITREE_OPKEY_SLOTS; i++ )
        if( node->op_keys.slots[i].bound )
        {
            node->op_keys.has_bindings = 1;
            return;
        }
}

bool
UITree_ApplyOpKey(
    struct UITree* tree,
    int component_id,
    int op_index,
    int const* key_chars,
    int const* key_codes,
    int pair_count)
{
    struct UITreeComponent* node = NULL;
    struct UITreeOpKeyBinding* slot = uitree_opkey_slot(tree, component_id, op_index, &node);

    if( !slot )
        return false;

    if( pair_count > UITREE_OPKEY_PAIR_MAX )
        pair_count = UITREE_OPKEY_PAIR_MAX;

    /* Reference: a negative first keychar clears the slot rather than binding. */
    if( pair_count <= 0 || !key_chars || !key_codes || key_chars[0] < 0 )
    {
        memset(slot, 0, sizeof(*slot));
        uitree_opkey_refresh_has_bindings(node);
        return true;
    }

    memset(slot, 0, sizeof(*slot));
    slot->bound = 1;
    slot->pair_count = (uint8_t)pair_count;
    for( int i = 0; i < pair_count; i++ )
    {
        slot->key_chars[i] = key_chars[i];
        slot->key_codes[i] = key_codes[i];
    }
    node->op_keys.has_bindings = 1;
    return true;
}

bool
UITree_ApplyOpKeyRate(
    struct UITree* tree,
    int component_id,
    int op_index,
    int rate,
    int enabled)
{
    struct UITreeOpKeyBinding* slot = uitree_opkey_slot(tree, component_id, op_index, NULL);
    if( !slot )
        return false;
    slot->rate = rate;
    slot->rate_enabled = enabled ? 1 : 0;
    return true;
}

bool
UITree_ApplyOpKeyIgnoreHeld(
    struct UITree* tree,
    int component_id,
    int op_index)
{
    struct UITreeOpKeyBinding* slot = uitree_opkey_slot(tree, component_id, op_index, NULL);
    if( !slot )
        return false;
    slot->ignore_held = 1;
    return true;
}

int
UITree_GetLayoutWidth(
    struct UITree const* tree,
    int component_id)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return 0;
    UITree_EnsureLayoutFor(tree, idx);
    struct UITreeElemPosition const* pos = &tree->components[idx].position;
    if( pos->layout_resolved && pos->abs_w > 0 )
        return pos->abs_w;
    return pos->width > 0 ? pos->width : 0;
}

int
UITree_GetLayoutHeight(
    struct UITree const* tree,
    int component_id)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return 0;
    UITree_EnsureLayoutFor(tree, idx);
    struct UITreeElemPosition const* pos = &tree->components[idx].position;
    if( pos->layout_resolved && pos->abs_h > 0 )
        return pos->abs_h;
    return pos->height > 0 ? pos->height : 0;
}

int
UITree_GetRelativeX(
    struct UITree const* tree,
    int component_id)
{
    int32_t idx;
    int32_t parent_idx;
    struct UITreeComponent const* node;

    assert(tree);
    idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return 0;
    UITree_EnsureLayoutFor(tree, idx);
    node = &tree->components[idx];
    if( !node->position.layout_resolved )
        return node->position.x;
    parent_idx = node->parent;
    if( parent_idx < 0 || (uint32_t)parent_idx >= tree->component_count )
        return node->position.abs_x;
    return node->position.abs_x - tree->components[parent_idx].position.abs_x;
}

int
UITree_GetRelativeY(
    struct UITree const* tree,
    int component_id)
{
    int32_t idx;
    int32_t parent_idx;
    struct UITreeComponent const* node;

    assert(tree);
    idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return 0;
    UITree_EnsureLayoutFor(tree, idx);
    node = &tree->components[idx];
    if( !node->position.layout_resolved )
        return node->position.y;
    parent_idx = node->parent;
    if( parent_idx < 0 || (uint32_t)parent_idx >= tree->component_count )
        return node->position.abs_y;
    return node->position.abs_y - tree->components[parent_idx].position.abs_y;
}

bool
UITree_ComponentVisibleById(
    struct UITreeComponent const* component,
    int hovered_component_id)
{
    assert(component);
    if( !component->behavior.hide )
        return true;
    if( component->component_id < 0 )
        return true;
    return hovered_component_id == component->component_id;
}

bool
UITree_ComponentHoveredByIds(
    int component_id,
    struct UITreeHoverIds const* hover_ids)
{
    assert(hover_ids);
    return hover_ids->main_com_id == component_id || hover_ids->side_com_id == component_id ||
           hover_ids->chat_com_id == component_id;
}

bool
UITree_ComponentVisibleByHoverIds(
    struct UITreeComponent const* component,
    struct UITreeHoverIds const* hover_ids)
{
    assert(component);
    assert(hover_ids);
    if( !component->behavior.hide )
        return true;
    if( component->component_id < 0 )
        return true;
    return UITree_ComponentHoveredByIds(component->component_id, hover_ids);
}

bool
UITree_ComponentIsClickable(struct UITreeComponent const* component)
{
    assert(component);
    return component->behavior.button_type != 0 || component->behavior.client_code > 0;
}

bool
UITree_ComponentHasMenuOptions(struct UITreeComponent const* component)
{
    assert(component);
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
    {
        if( component->menu_options.ops[i][0] != '\0' )
            return true;
    }
    return false;
}

int
UITree_InterfaceParentFind(
    struct UITree const* tree,
    int container_uid)
{
    int i;
    assert(tree);
    for( i = 0; i < tree->interface_parent_count; i++ )
    {
        if( tree->interface_parents[i].container_uid == container_uid )
            return i;
    }
    return -1;
}

int
UITree_InterfaceParentSet(
    struct UITree* tree,
    int container_uid,
    int group_id,
    int type)
{
    int idx;
    assert(tree);
    assert(container_uid >= 0);
    assert(group_id > 0);

    idx = UITree_InterfaceParentFind(tree, container_uid);
    if( idx < 0 )
    {
        assert(tree->interface_parent_count < UITREE_INTERFACE_PARENT_MAX);
        idx = tree->interface_parent_count++;
    }
    tree->interface_parents[idx].container_uid = container_uid;
    tree->interface_parents[idx].group_id = group_id;
    tree->interface_parents[idx].type = type;
    return idx;
}

void
UITree_InterfaceParentClear(
    struct UITree* tree,
    int container_uid)
{
    int idx;
    int last;
    assert(tree);
    idx = UITree_InterfaceParentFind(tree, container_uid);
    if( idx < 0 )
        return;
    last = tree->interface_parent_count - 1;
    if( idx != last )
        tree->interface_parents[idx] = tree->interface_parents[last];
    tree->interface_parent_count--;
}

int
UITree_InterfaceParentIsMountedGroup(
    struct UITree const* tree,
    int group_id)
{
    int i;
    assert(tree);
    for( i = 0; i < tree->interface_parent_count; i++ )
    {
        if( tree->interface_parents[i].group_id == group_id )
            return 1;
    }
    return 0;
}

int
UITree_RootIsDisplayable(
    struct UITree const* tree,
    int32_t root)
{
    int toplevel_group;
    int group;

    assert(tree);
    if( root < 0 || tree->root_index < 0 )
        return 0;

    /* A CS2 script that sets a property on an interface that isn't open yet makes
     * the host auto-mount that group as a top-level root so the property can apply
     * (loaded-for-access, NOT displayed). Such orphan roots must not render, hover,
     * or take clicks — otherwise a full-canvas panel (e.g. interface 728) covers
     * the gameframe. Displayable = the active gameframe (which is baked as several
     * roots sharing the toplevel group id), the app-overlay chrome (group 0x7FFE),
     * or a group actually placed into a slot (InterfaceParent-mounted). */
    toplevel_group = (tree->components[tree->root_index].component_id >> 16) & 0xffff;
    group = (tree->components[root].component_id >> 16) & 0xffff;
    if( group <= 0 || group == toplevel_group || group == 0x7FFE )
        return 1;
    return UITree_InterfaceParentIsMountedGroup(tree, group);
}

int32_t
UITree_ResolveDragRenderArea(
    struct UITree const* tree,
    struct UITreeComponent const* src)
{
    int32_t parent_idx;

    assert(tree);
    assert(src);
    if( src->drag_render_area_uid < 0 )
        return -1;
    parent_idx = UITree_FindByComponentId(tree, src->drag_render_area_uid);
    if( parent_idx < 0 )
        return -1;
    /* cc_setdraggable(parentUid, childIndex): the render area is the child at
     * childIndex within the parent (reference WidgetOps CC_SETDRAGGABLE — e.g.
     * the scrollbar dragger clamps to child 0, the track, not the whole bar).
     * Fall back to the parent when the child cannot be resolved. */
    if( src->drag_render_area_child_index >= 0 )
    {
        int32_t child = UITree_FindChildBySubid(
            tree,
            parent_idx,
            src->drag_render_area_uid,
            src->drag_render_area_child_index);
        if( child >= 0 )
            return child;
    }
    return parent_idx;
}

int
UITree_ComponentIsDraggable(struct UITreeComponent const* c)
{
    assert(c);
    if( UITree_ClickMaskDragDepth(c->behavior.click_mask) != 0 )
        return 1;
    if( c->drag_render_area_uid >= 0 )
        return 1;
    if( c->draggable )
        return 1;
    if( c->runtime_hooks.on_drag.script_id > 0 )
        return 1;
    return 0;
}

int
UITree_ComponentIsDropTarget(struct UITreeComponent const* c)
{
    assert(c);
    if( (c->behavior.click_mask & UITREE_FLAG_DRAG_ON) != 0 )
        return 1;
    if( c->runtime_hooks.on_drag.script_id > 0 )
        return 1;
    if( c->runtime_hooks.on_drag_complete.script_id > 0 )
        return 1;
    if( c->runtime_hooks.on_op.script_id > 0 )
        return 1;
    if( c->runtime_hooks.on_click.script_id > 0 )
        return 1;
    return 0;
}

int
UITree_ComponentOrAncestorHidden(
    struct UITree const* tree,
    int component_id)
{
    int32_t idx;
    assert(tree);
    idx = UITree_FindByComponentId(tree, component_id);
    while( idx >= 0 && (uint32_t)idx < tree->component_count )
    {
        if( tree->components[idx].behavior.hide )
            return 1;
        idx = tree->components[idx].parent;
    }
    return 0;
}

static int
drop_target_pick_in_subtree(
    struct UITree const* tree,
    int32_t idx,
    int px,
    int py,
    int exclude_component_id,
    int scroll_off_x,
    int scroll_off_y,
    struct UITreeScrollClip const* clip,
    struct UITreeScrollClip const* surface,
    int* best_id,
    int* best_depth,
    int depth)
{
    struct UITreeComponent const* c;
    int32_t child;
    int x, y, w, h;
    int hit;
    int child_scroll_x;
    int child_scroll_y;
    struct UITreeScrollClip child_clip;
    struct UITreeScrollClip child_surface;

    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return 0;
    c = &tree->components[idx];
    if( c->behavior.hide )
        return 0;
    if( c->component_id == exclude_component_id )
        return 0;

    if( clip && clip->clip_w > 0 && clip->clip_h > 0 && !UITree_PointInClip(px, py, clip) )
        return 0;

    UITree_LayoutGetBounds(&c->position, &x, &y, &w, &h);
    /* Drop targets are picked at DRAWN positions: offset by ancestor scroll
     * (canonical component->scroll_x/y, same as emit and hit-testing). */
    hit = UITree_PointInScrolledBounds(px, py, x, y, w, h, scroll_off_x, scroll_off_y);

    child_scroll_x = scroll_off_x;
    child_scroll_y = scroll_off_y;
    child_clip = clip ? *clip : (struct UITreeScrollClip){ 0 };
    child_surface = surface ? *surface : (struct UITreeScrollClip){ 0 };
    /* Same shared clip rule as the emit walk (UITree_LayerChildClip), so drop
     * targets match drawn pixels: own bounds ∩ enclosing surface, never
     * compounded with ancestor layers. */
    {
        struct UITreeScrollClip cc, cs;
        if( UITree_LayerChildClip(
                c, surface, x - scroll_off_x, y - scroll_off_y, w, h, &cc, &cs) )
        {
            child_clip = cc;
            child_surface = cs;
        }
    }
    if( c->type == UIELEM_RS_LAYER )
    {
        if( UITree_ScrollLayerNeedsHorizontal(c) )
            child_scroll_x += c->scroll_x;
        if( UITree_ScrollLayerNeedsVertical(c) )
            child_scroll_y += c->scroll_y;
    }

    for( child = c->first_child; child >= 0; child = tree->components[child].next_sibling )
    {
        drop_target_pick_in_subtree(
            tree, child, px, py, exclude_component_id,
            child_scroll_x, child_scroll_y, &child_clip, &child_surface, best_id, best_depth,
            depth + 1);
    }

    /* InterfaceParent mounts drawn/hit last under this container. */
    {
        int mi;
        for( mi = 0; mi < tree->interface_parent_count; mi++ )
        {
            struct UITreeInterfaceParent const* ip = &tree->interface_parents[mi];
            int32_t root;
            if( ip->container_uid != c->component_id )
                continue;
            for( root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
            {
                int cid = tree->components[root].component_id;
                int group = (cid >> 16) & 0xffff;
                if( group != ip->group_id )
                    continue;
                /* Mounted roots may be reparented; still walk from pack roots. */
                if( tree->components[root].parent >= 0 &&
                    tree->components[tree->components[root].parent].component_id ==
                        ip->container_uid )
                {
                    /* Mounted sub-interfaces live in the container's viewport
                     * space: reset scroll accumulation (matches emit). */
                    drop_target_pick_in_subtree(
                        tree,
                        root,
                        px,
                        py,
                        exclude_component_id,
                        scroll_off_x,
                        scroll_off_y,
                        &child_clip,
                        &child_surface,
                        best_id,
                        best_depth,
                        depth + 1);
                }
            }
        }
    }

    if( hit && UITree_ComponentIsDropTarget(c) && depth >= *best_depth )
    {
        *best_depth = depth;
        *best_id = c->component_id;
    }
    return *best_id >= 0;
}

int
UITree_FindDropTarget(
    struct UITree const* tree,
    int px,
    int py,
    int exclude_component_id)
{
    int32_t root;
    int best_id = -1;
    int best_depth = -1;
    assert(tree);
    for( root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        if( tree->components[root].behavior.hide )
            continue;
        drop_target_pick_in_subtree(
            tree, root, px, py, exclude_component_id, 0, 0, NULL, NULL, &best_id, &best_depth, 0);
    }
    return best_id;
}
