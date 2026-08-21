#include "uitree.h"

#include "perf/torirs_perf.h"
#include "uitree_layout.h"
#include "uitree_scroll.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- UITreeNodeSet ------------------------------------------------------- */

static void
UITreeNodeSet_Init(struct UITreeNodeSet* set)
{
    assert(set);
    set->slots = NULL;
    set->pos = NULL;
    set->count = 0;
    set->cap = 0;
    set->pos_cap = 0;
}

static void
UITreeNodeSet_Free(struct UITreeNodeSet* set)
{
    assert(set);
    free(set->slots);
    free(set->pos);
    UITreeNodeSet_Init(set);
}

static void
UITreeNodeSet_Clear(struct UITreeNodeSet* set)
{
    uint32_t i;
    assert(set);
    if( set->pos )
    {
        for( i = 0; i < set->pos_cap; i++ )
            set->pos[i] = -1;
    }
    set->count = 0;
}

static void
UITreeNodeSet_EnsurePosCap(
    struct UITreeNodeSet* set,
    uint32_t need_cap)
{
    uint32_t old_cap;
    uint32_t ncap;
    int32_t* p;
    uint32_t i;

    assert(set);
    if( need_cap <= set->pos_cap )
        return;
    old_cap = set->pos_cap;
    ncap = old_cap == 0 ? 16 : old_cap;
    while( ncap < need_cap )
        ncap *= 2;
    p = (int32_t*)realloc(set->pos, (size_t)ncap * sizeof(int32_t));
    assert(p);
    for( i = old_cap; i < ncap; i++ )
        p[i] = -1;
    set->pos = p;
    set->pos_cap = ncap;
}

static int
UITreeNodeSet_Contains(
    struct UITreeNodeSet const* set,
    int32_t slot)
{
    assert(set);
    if( slot < 0 || set->pos == NULL || (uint32_t)slot >= set->pos_cap )
        return 0;
    return set->pos[slot] >= 0;
}

static void
UITreeNodeSet_Add(
    struct UITreeNodeSet* set,
    int32_t slot)
{
    int32_t* slots;
    assert(set);
    assert(slot >= 0);
    UITreeNodeSet_EnsurePosCap(set, (uint32_t)slot + 1u);
    if( UITreeNodeSet_Contains(set, slot) )
        return;
    if( set->count >= set->cap )
    {
        int ncap = set->cap == 0 ? 16 : set->cap * 2;
        slots = (int32_t*)realloc(set->slots, (size_t)ncap * sizeof(int32_t));
        assert(slots);
        set->slots = slots;
        set->cap = ncap;
    }
    set->pos[slot] = set->count;
    set->slots[set->count++] = slot;
}

static void
UITreeNodeSet_Remove(
    struct UITreeNodeSet* set,
    int32_t slot)
{
    int32_t at;
    int32_t last_slot;
    assert(set);
    if( slot < 0 || set->pos == NULL || (uint32_t)slot >= set->pos_cap )
        return;
    at = set->pos[slot];
    if( at < 0 )
        return;
    assert(at < set->count);
    last_slot = set->slots[set->count - 1];
    set->slots[at] = last_slot;
    set->pos[last_slot] = at;
    set->pos[slot] = -1;
    set->count--;
}

/* ---- Per-tree live-set bookkeeping --------------------------------------- */

static void
uitree_all_sets_ensure_pos(
    struct UITree* tree,
    uint32_t need_cap)
{
    uint32_t i;
    assert(tree);
    UITreeNodeSet_EnsurePosCap(&tree->models, need_cap);
    UITreeNodeSet_EnsurePosCap(&tree->timer_hooks, need_cap);
    UITreeNodeSet_EnsurePosCap(&tree->key_hooks, need_cap);
    UITreeNodeSet_EnsurePosCap(&tree->wheel_hooks, need_cap);
    UITreeNodeSet_EnsurePosCap(&tree->opkeys, need_cap);
    UITreeNodeSet_EnsurePosCap(&tree->client_code, need_cap);
    UITreeNodeSet_EnsurePosCap(&tree->resize_hooks, need_cap);
    UITreeNodeSet_EnsurePosCap(&tree->sub_change_hooks, need_cap);
    UITreeNodeSet_EnsurePosCap(&tree->scroll_layers, need_cap);
    UITreeNodeSet_EnsurePosCap(&tree->debug_overlays, need_cap);
    if( tree->group_map )
    {
        for( i = 0; i < tree->group_map_cap; i++ )
        {
            if( tree->group_map[i].group_id >= 0 )
                UITreeNodeSet_EnsurePosCap(&tree->group_map[i].nodes, need_cap);
        }
    }
}

static void
uitree_all_sets_free(struct UITree* tree)
{
    uint32_t i;
    assert(tree);
    UITreeNodeSet_Free(&tree->models);
    UITreeNodeSet_Free(&tree->timer_hooks);
    UITreeNodeSet_Free(&tree->key_hooks);
    UITreeNodeSet_Free(&tree->wheel_hooks);
    UITreeNodeSet_Free(&tree->opkeys);
    UITreeNodeSet_Free(&tree->client_code);
    UITreeNodeSet_Free(&tree->resize_hooks);
    UITreeNodeSet_Free(&tree->sub_change_hooks);
    UITreeNodeSet_Free(&tree->scroll_layers);
    UITreeNodeSet_Free(&tree->debug_overlays);
    if( tree->group_map )
    {
        for( i = 0; i < tree->group_map_cap; i++ )
            UITreeNodeSet_Free(&tree->group_map[i].nodes);
        free(tree->group_map);
        tree->group_map = NULL;
        tree->group_map_cap = 0;
    }
}

static void
uitree_all_sets_clear(struct UITree* tree)
{
    uint32_t i;
    assert(tree);
    UITreeNodeSet_Clear(&tree->models);
    UITreeNodeSet_Clear(&tree->timer_hooks);
    UITreeNodeSet_Clear(&tree->key_hooks);
    UITreeNodeSet_Clear(&tree->wheel_hooks);
    UITreeNodeSet_Clear(&tree->opkeys);
    UITreeNodeSet_Clear(&tree->client_code);
    UITreeNodeSet_Clear(&tree->resize_hooks);
    UITreeNodeSet_Clear(&tree->sub_change_hooks);
    UITreeNodeSet_Clear(&tree->scroll_layers);
    UITreeNodeSet_Clear(&tree->debug_overlays);
    if( tree->group_map )
    {
        for( i = 0; i < tree->group_map_cap; i++ )
        {
            UITreeNodeSet_Free(&tree->group_map[i].nodes);
            tree->group_map[i].group_id = -1;
        }
    }
    tree->world_index = -1;
    tree->worldmap_index = -1;
}

static uint32_t
uitree_group_hash(int group_id)
{
    return (uint32_t)group_id * 2654435761u;
}

static struct UITreeGroupBucket*
uitree_group_bucket(
    struct UITree* tree,
    int group_id,
    int create)
{
    uint32_t i;
    uint32_t cap;
    uint32_t start;
    struct UITreeGroupBucket* map;

    assert(tree);
    assert(group_id >= 0);
    if( tree->group_map_cap == 0 )
    {
        if( !create )
            return NULL;
        cap = 16;
        map = (struct UITreeGroupBucket*)calloc(cap, sizeof(*map));
        assert(map);
        for( i = 0; i < cap; i++ )
            map[i].group_id = -1;
        tree->group_map = map;
        tree->group_map_cap = cap;
    }

    if( create )
    {
        uint32_t live = 0;
        for( i = 0; i < tree->group_map_cap; i++ )
            if( tree->group_map[i].group_id >= 0 )
                live++;
        if( live * 2u >= tree->group_map_cap )
        {
            uint32_t ncap = tree->group_map_cap * 2u;
            struct UITreeGroupBucket* nmap =
                (struct UITreeGroupBucket*)calloc(ncap, sizeof(*nmap));
            assert(nmap);
            for( i = 0; i < ncap; i++ )
                nmap[i].group_id = -1;
            for( i = 0; i < tree->group_map_cap; i++ )
            {
                if( tree->group_map[i].group_id < 0 )
                    continue;
                {
                    uint32_t j =
                        uitree_group_hash(tree->group_map[i].group_id) & (ncap - 1u);
                    while( nmap[j].group_id >= 0 )
                        j = (j + 1u) & (ncap - 1u);
                    nmap[j] = tree->group_map[i];
                }
            }
            free(tree->group_map);
            tree->group_map = nmap;
            tree->group_map_cap = ncap;
        }
    }

    cap = tree->group_map_cap;
    start = uitree_group_hash(group_id) & (cap - 1u);
    i = start;
    for( ;; )
    {
        struct UITreeGroupBucket* b = &tree->group_map[i];
        if( b->group_id == group_id )
            return b;
        if( b->group_id < 0 )
        {
            if( !create )
                return NULL;
            b->group_id = group_id;
            UITreeNodeSet_Init(&b->nodes);
            if( tree->component_capacity > 0 )
                UITreeNodeSet_EnsurePosCap(&b->nodes, tree->component_capacity);
            return b;
        }
        i = (i + 1u) & (cap - 1u);
        assert(i != start && "group map full");
    }
}

static void
uitree_group_add(
    struct UITree* tree,
    int group_id,
    int32_t idx)
{
    struct UITreeGroupBucket* b;
    if( group_id < 0 )
        return;
    b = uitree_group_bucket(tree, group_id, 1);
    assert(b);
    UITreeNodeSet_Add(&b->nodes, idx);
}

static void
uitree_group_remove(
    struct UITree* tree,
    int group_id,
    int32_t idx)
{
    struct UITreeGroupBucket* b;
    if( group_id < 0 || !tree->group_map )
        return;
    b = uitree_group_bucket(tree, group_id, 0);
    if( !b )
        return;
    UITreeNodeSet_Remove(&b->nodes, idx);
}

static void
uitree_sync_hook_sets(
    struct UITree* tree,
    int32_t idx)
{
    struct UITreeComponent const* c;
    struct UITreeRuntimeHooks const* h;
    assert(tree);
    assert(idx >= 0 && (uint32_t)idx < tree->component_count);
    c = &tree->components[idx];
    h = c->runtime_hooks;
    if( h && h->on_timer.script_id > 0 )
        UITreeNodeSet_Add(&tree->timer_hooks, idx);
    else
        UITreeNodeSet_Remove(&tree->timer_hooks, idx);
    /* One set for all three keyboard hooks: they are collected in the same
     * scan and a component that carries any of them is rare enough that
     * splitting the set would only add bookkeeping. */
    if( h && (h->on_key.script_id > 0 || h->on_key_down.script_id > 0 ||
              h->on_key_up.script_id > 0) )
        UITreeNodeSet_Add(&tree->key_hooks, idx);
    else
        UITreeNodeSet_Remove(&tree->key_hooks, idx);
    if( h && h->on_scroll_wheel.script_id > 0 )
        UITreeNodeSet_Add(&tree->wheel_hooks, idx);
    else
        UITreeNodeSet_Remove(&tree->wheel_hooks, idx);
    if( h && h->on_resize.script_id > 0 )
        UITreeNodeSet_Add(&tree->resize_hooks, idx);
    else
        UITreeNodeSet_Remove(&tree->resize_hooks, idx);
    if( h && h->on_sub_change.script_id > 0 )
        UITreeNodeSet_Add(&tree->sub_change_hooks, idx);
    else
        UITreeNodeSet_Remove(&tree->sub_change_hooks, idx);
}

static void
uitree_live_unregister(
    struct UITree* tree,
    int32_t idx)
{
    struct UITreeComponent const* c;
    assert(tree);
    assert(idx >= 0 && (uint32_t)idx < tree->component_count);
    c = &tree->components[idx];
    UITreeNodeSet_Remove(&tree->models, idx);
    UITreeNodeSet_Remove(&tree->timer_hooks, idx);
    UITreeNodeSet_Remove(&tree->key_hooks, idx);
    UITreeNodeSet_Remove(&tree->wheel_hooks, idx);
    UITreeNodeSet_Remove(&tree->opkeys, idx);
    UITreeNodeSet_Remove(&tree->client_code, idx);
    UITreeNodeSet_Remove(&tree->resize_hooks, idx);
    UITreeNodeSet_Remove(&tree->sub_change_hooks, idx);
    UITreeNodeSet_Remove(&tree->scroll_layers, idx);
    UITreeNodeSet_Remove(&tree->debug_overlays, idx);
    if( c->component_id >= 0 )
        uitree_group_remove(tree, (c->component_id >> 16) & 0xffff, idx);
    if( tree->world_index == idx )
        tree->world_index = -1;
    if( tree->worldmap_index == idx )
        tree->worldmap_index = -1;
}

static void
uitree_live_register(
    struct UITree* tree,
    int32_t idx)
{
    struct UITreeComponent const* c;
    assert(tree);
    assert(idx >= 0 && (uint32_t)idx < tree->component_count);
    c = &tree->components[idx];
    if( c->type == UIELEM_RS_MODEL )
        UITreeNodeSet_Add(&tree->models, idx);
    if( c->type == UIELEM_RS_LAYER )
        UITreeNodeSet_Add(&tree->scroll_layers, idx);
    if( c->type == UIELEM_BUILTIN_DEBUG_OVERLAY )
        UITreeNodeSet_Add(&tree->debug_overlays, idx);
    if( c->behavior.client_code > 0 )
        UITreeNodeSet_Add(&tree->client_code, idx);
    if( UITree_OpKeys(c)->has_bindings )
        UITreeNodeSet_Add(&tree->opkeys, idx);
    if( c->component_id >= 0 )
        uitree_group_add(tree, (c->component_id >> 16) & 0xffff, idx);
    if( c->type == UIELEM_BUILTIN_WORLD )
        tree->world_index = idx;
    if( c->type == UIELEM_BUILTIN_WORLDMAP )
        tree->worldmap_index = idx;
    uitree_sync_hook_sets(tree, idx);
}

void
UITree_SyncHookMembership(
    struct UITree* tree,
    int32_t idx)
{
    assert(tree);
    assert(idx >= 0 && (uint32_t)idx < tree->component_count);
    uitree_sync_hook_sets(tree, idx);
}

void
UITree_FreeHooksAt(
    struct UITree* tree,
    int32_t idx)
{
    assert(tree);
    assert(idx >= 0 && (uint32_t)idx < tree->component_count);
    UITreeNodeSet_Remove(&tree->timer_hooks, idx);
    UITreeNodeSet_Remove(&tree->key_hooks, idx);
    UITreeNodeSet_Remove(&tree->wheel_hooks, idx);
    UITreeNodeSet_Remove(&tree->resize_hooks, idx);
    UITreeNodeSet_Remove(&tree->sub_change_hooks, idx);
    UITree_HooksFree(&tree->components[idx]);
}

struct UITreeNodeSet const*
UITree_GroupNodes(
    struct UITree const* tree,
    int group_id)
{
    struct UITreeGroupBucket* b;
    assert(tree);
    if( group_id < 0 || !tree->group_map )
        return NULL;
    b = uitree_group_bucket((struct UITree*)tree, group_id, 0);
    if( !b || b->nodes.count <= 0 )
        return NULL;
    return &b->nodes;
}

int
UITree_GroupPresent(
    struct UITree const* tree,
    int group_id)
{
    return UITree_GroupNodes(tree, group_id) != NULL;
}

#ifdef UITREE_NODE_SET_VERIFY
static int
uitree_set_has_slot(
    struct UITreeNodeSet const* set,
    int32_t slot)
{
    int i;
    for( i = 0; i < set->count; i++ )
        if( set->slots[i] == slot )
            return 1;
    return 0;
}

void
UITree_VerifyLiveSets(struct UITree const* tree)
{
    uint32_t i;
    assert(tree);
    for( i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        int expect;
        if( c->freed )
        {
            assert(!UITreeNodeSet_Contains(&tree->models, (int32_t)i));
            assert(!UITreeNodeSet_Contains(&tree->timer_hooks, (int32_t)i));
            assert(!UITreeNodeSet_Contains(&tree->client_code, (int32_t)i));
            continue;
        }
        expect = c->type == UIELEM_RS_MODEL;
        assert(!!UITreeNodeSet_Contains(&tree->models, (int32_t)i) == !!expect);
        expect = c->type == UIELEM_RS_LAYER;
        assert(!!UITreeNodeSet_Contains(&tree->scroll_layers, (int32_t)i) == !!expect);
        expect = c->type == UIELEM_BUILTIN_DEBUG_OVERLAY;
        assert(!!UITreeNodeSet_Contains(&tree->debug_overlays, (int32_t)i) == !!expect);
        expect = c->behavior.client_code > 0;
        assert(!!UITreeNodeSet_Contains(&tree->client_code, (int32_t)i) == !!expect);
        expect = c->op_keys.has_bindings != 0;
        assert(!!UITreeNodeSet_Contains(&tree->opkeys, (int32_t)i) == !!expect);
        expect = c->runtime_hooks && c->runtime_hooks->on_timer.script_id > 0;
        assert(!!UITreeNodeSet_Contains(&tree->timer_hooks, (int32_t)i) == !!expect);
        expect = c->runtime_hooks && c->runtime_hooks->on_key.script_id > 0;
        assert(!!UITreeNodeSet_Contains(&tree->key_hooks, (int32_t)i) == !!expect);
        expect = c->runtime_hooks && c->runtime_hooks->on_scroll_wheel.script_id > 0;
        assert(!!UITreeNodeSet_Contains(&tree->wheel_hooks, (int32_t)i) == !!expect);
        expect = c->runtime_hooks && c->runtime_hooks->on_resize.script_id > 0;
        assert(!!UITreeNodeSet_Contains(&tree->resize_hooks, (int32_t)i) == !!expect);
        expect = c->runtime_hooks && c->runtime_hooks->on_sub_change.script_id > 0;
        assert(!!UITreeNodeSet_Contains(&tree->sub_change_hooks, (int32_t)i) == !!expect);
        if( c->component_id >= 0 )
        {
            int group = (c->component_id >> 16) & 0xffff;
            struct UITreeNodeSet const* g = UITree_GroupNodes(tree, group);
            assert(g && uitree_set_has_slot(g, (int32_t)i));
        }
        if( c->type == UIELEM_BUILTIN_WORLD )
            assert(tree->world_index == (int32_t)i);
        if( c->type == UIELEM_BUILTIN_WORLDMAP )
            assert(tree->worldmap_index == (int32_t)i);
    }
}
#endif

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
        assert(opts->submenus);
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

struct UITreeRuntimeHooks const uitree_hooks_none;

struct UITreeRuntimeHooks*
UITree_HooksMut(struct UITreeComponent* c)
{
    assert(c);
    if( !c->runtime_hooks )
        c->runtime_hooks = UITree_HooksBlockNew();
    return c->runtime_hooks;
}

void
UITree_HooksFree(struct UITreeComponent* c)
{
    assert(c);
    UITree_HooksBlockFree(c->runtime_hooks);
    c->runtime_hooks = NULL;
}

/* The key UITree_FindChildBySubid matches a child on: its dynamic slot for
 * cc_create'd children, the low half of its uid for cache-baked ones. Immutable
 * while the child is linked (both fields are set once, at push). */
static int32_t
uitree_child_key(struct UITreeComponent const* child)
{
    return child->dynamic ? child->dynamic_child_index : (child->component_id & 0xFFFF);
}

/* Slot a key occupies in `child_key_index`: the dynamic half or the static one. */
static inline int32_t
uitree_child_key_slot(struct UITreeComponent const* child, int32_t key, int32_t cap)
{
    return child->dynamic ? key : cap + key;
}

static void
uitree_child_index_drop(struct UITreeComponent* parent)
{
    free(parent->child_key_index);
    parent->child_key_index = NULL;
    parent->child_key_index_cap = 0;
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

    if( parent->child_key_index )
    {
        int32_t const cap = parent->child_key_index_cap;
        if( key < 0 || key >= cap )
        {
            /* Outside the built range: the next long walk sizes a new map. */
            uitree_child_index_drop(parent);
        }
        else
        {
            int32_t const slot = uitree_child_key_slot(&tree->components[child_index], key, cap);
            if( parent->child_key_index[slot] >= 0 )
            {
                /* A second live child on this key: only the walk can say which
                 * one comes first in sibling order. */
                parent->child_key_index_bad = 1;
                uitree_child_index_drop(parent);
            }
            else
            {
                parent->child_key_index[slot] = child_index;
            }
        }
    }

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
    int32_t const key = uitree_child_key(&tree->components[child_index]);

    if( parent->child_key_index && key >= 0 && key < parent->child_key_index_cap )
    {
        int32_t const cap = parent->child_key_index_cap;
        int32_t const slot = uitree_child_key_slot(&tree->components[child_index], key, cap);
        /* Only clear the slot this child actually owns: a replace-in-slot
         * rebuild reclaims the old row before pushing the new one, so leaving
         * the map allocated (and merely emptied) is what keeps the rebuild
         * linear instead of dropping the map 500 times. */
        if( parent->child_key_index[slot] == child_index )
            parent->child_key_index[slot] = -1;
    }

    if( parent->child_key_max == UITREE_CHILD_KEY_UNKNOWN )
        return;
    if( key >= parent->child_key_max )
        parent->child_key_max = UITREE_CHILD_KEY_UNKNOWN;
}

/* Build the key->child map for `parent_index` from one sibling walk. Called
 * only after a lookup has already had to walk a long list, so short containers
 * (the overwhelming majority) never pay for it. A parent whose children carry
 * duplicate keys is marked bad and keeps walking: the map cannot express "first
 * in sibling order wins" between two children on one key. */
static void
uitree_child_index_build(struct UITree* tree, int32_t parent_index)
{
    struct UITreeComponent* parent = &tree->components[parent_index];
    if( parent->child_key_index_bad )
        return;

    int32_t max_key = -1;
    for( int32_t child = parent->first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        int32_t const key = uitree_child_key(&tree->components[child]);
        if( key > max_key )
            max_key = key;
    }
    /* Keys above the masked comparison's range are not addressable through the
     * map (UITree_FindChildBySubid only takes the fast path for sub_id <= 0xFFFF). */
    if( max_key < 0 || max_key > 0xFFFF )
        return;

    int32_t const cap = max_key + 1;
    int32_t* index = malloc((size_t)cap * 2u * sizeof(int32_t));
    assert(index);
    for( int32_t i = 0; i < cap * 2; i++ )
        index[i] = -1;

    for( int32_t child = parent->first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        int32_t const key = uitree_child_key(&tree->components[child]);
        if( key < 0 || key >= cap )
            continue;
        int32_t const slot = uitree_child_key_slot(&tree->components[child], key, cap);
        if( index[slot] >= 0 )
        {
            parent->child_key_index_bad = 1;
            free(index);
            return;
        }
        index[slot] = child;
    }

    uitree_child_index_drop(parent);
    parent->child_key_index = index;
    parent->child_key_index_cap = cap;
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
            uitree_all_sets_ensure_pos(tree, new_capacity);
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
    /* Widget.field4122 starts at 4 in the rev239 gamepack.  method5229 uses
     * that value as the boundary between ordinary component operations and
     * CC_OP_LOW_PRIORITY: leaving calloc's zero here incorrectly demoted ops
     * 2..4 on every script-created item cell (including the bank's default
     * Deposit operation), so a short left click found no normal default row. */
    component->target_priority = 4;
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
    UITree_LayoutInvalidateBoxes(tree);

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
    assert(name);
    if( name[0] == '\0' )
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
    case UIELEM_BUILTIN_DEBUG_OVERLAY:
        return "debug_overlay";
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
    case UIELEM_BUILTIN_WORLDMAP:
        return "worldmap";
    case UIELEM_BUILTIN_WORLDMAP_OVERVIEW:
        return "worldmap_overview";
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
    tree->world_index = -1;
    tree->worldmap_index = -1;
    return tree;
}

/* One fewer node carries CS1 scripts. The count exists so the per-tick CS1 pass
 * can skip its whole-tree scan on an if3/CS2 tree, where nothing has any — see
 * UITree_HasCS1Scripts. */
static void
uitree_cs1_script_nodes_drop(struct UITree* tree)
{
    assert(tree->cs1_script_nodes > 0);
    tree->cs1_script_nodes--;
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
    free(c->child_key_index);
    c->child_key_index = NULL;
    c->child_key_index_cap = 0;
    c->child_key_index_bad = 0;
    free(c->data_text);
    c->data_text = NULL;
    for( int i = 0; i < c->params_count; i++ )
        free(c->params[i].str);
    free(c->params);
    c->params = NULL;
    c->params_count = 0;
    c->params_capacity = 0;
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
    /* Both lazy blocks, and the submenus the options block owns — see
     * ui/uitree_component_options.h. */
    UITree_MenuOptionsFree(c);
    UITree_OpKeysFree(c);
    UITree_HooksFree(c);
}

static void uitree_id_index_note_removed(struct UITree* tree, int32_t idx);

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

    if( c->behavior.scripts_count > 0 )
        uitree_cs1_script_nodes_drop(tree);
    /* A CC_DELETEALL can reclaim the node a drag is still running on. */
    UITree_SetComponentDragActive(tree, idx, 0);
    /* Drop live-set membership before clearing type/id/hooks. */
    uitree_live_unregister(tree, idx);
    /* Must read the id, so before the memset clears it. */
    uitree_id_index_note_removed(tree, idx);
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
    /* The id_generation bump lives in uitree_id_index_note_removed above, which
     * needs the id this memset has now cleared. */
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
    free(tree->layout_changed);
    uitree_all_sets_free(tree);
    free(tree->components);
    free(tree);
}

void
UITree_Clear(struct UITree* tree)
{
    assert(tree);

    while( tree->root_index >= 0 )
    {
        int32_t root = tree->root_index;
        tree->root_index = tree->components[root].next_sibling;
        if( tree->last_root_index == root )
            tree->last_root_index = tree->root_index;
        tree->components[root].next_sibling = -1;
        tree->components[root].parent = -1;
        uitree_reclaim_subtree(tree, root);
    }
    tree->root_index = -1;
    tree->last_root_index = -1;
    tree->interface_parent_count = 0;
    tree->generation++;
    /* Reclaim already unregistered each node; clear empties any leftover buckets. */
    uitree_all_sets_clear(tree);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_FIND_ID_LINEAR, 1);
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
            if( cur < 0 )
            {
                /* Reclaimed slot: this id has no incumbent to beat. */
                tree->id_index_vals[h] = idx;
                if( tree->id_index_tombs )
                    tree->id_index_tombs--;
                return;
            }
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_ID_REBUILD, 1);
    uint32_t cap = tree->id_index_cap ? tree->id_index_cap : 16;
    while( cap < tree->component_count * 2u )
        cap <<= 1;

    if( cap != tree->id_index_cap || !tree->id_index_keys )
    {
        int32_t* keys = realloc(tree->id_index_keys, cap * sizeof(int32_t));
        int32_t* vals = realloc(tree->id_index_vals, cap * sizeof(int32_t));
        assert(keys);
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
    tree->id_index_tombs = 0;
    tree->id_index_valid = 1;
    return true;
}

/* Record that `idx` is about to lose its component_id (reclaim). Must be called
 * while the component still holds it. Tombstones only the slot this component
 * won, so a rebuild is not needed per reclaim — which is what a container
 * rebuild does once per row it replaces. */
static void
uitree_id_index_note_removed(struct UITree* tree, int32_t idx)
{
    bool const in_step = tree->id_index_valid && tree->id_index_gen == tree->id_generation;
    int const id = tree->components[idx].component_id;
    tree->id_generation++;
    if( !in_step || id < 0 )
        return;

    uint32_t const mask = tree->id_index_cap - 1;
    uint32_t h = uitree_id_hash(id) & mask;
    for( ;; )
    {
        int32_t const k = tree->id_index_keys[h];
        if( k < 0 )
            break; /* never inserted — nothing to undo */
        if( k == id )
        {
            /* Only the winner's departure changes an answer; a duplicate that
             * lost the tie-break leaves the map correct as it stands. */
            if( tree->id_index_vals[h] == idx )
            {
                tree->id_index_vals[h] = -1;
                tree->id_index_tombs++;
            }
            break;
        }
        h = (h + 1) & mask;
    }

    /* Markers hold their slot, so a table that filled with them could probe
     * forever. Hand it back for a rebuild well before that. */
    if( (tree->component_count + tree->id_index_tombs) * 2u > tree->id_index_cap )
    {
        tree->id_index_valid = 0;
        return;
    }
    tree->id_index_gen = tree->id_generation;
}

/* Record that `idx` just had its component_id assigned (UITree_Push). Bumps
 * id_generation and, when the map is currently in step with it and has room,
 * folds the new id in so the map stays usable — otherwise the bump alone leaves
 * it stale and the next lookup rebuilds. Must be called after component_id and
 * `dynamic` are both set, since the tie-break reads both. */

void
UITree_DebugOverlaySetFontIds(
    struct UITree* tree,
    int font_id_small,
    int font_id_menu,
    int font_id_body)
{
    assert(tree);
    /* Every overlay component, not the first: a tree may carry more than one,
     * and a scale change that reached only one of them would put two chromes
     * at two sizes on one screen. The live set holds exactly those nodes, so
     * this asks the question without a scan. */
    for( int32_t s = 0; s < tree->debug_overlays.count; s++ )
    {
        struct UITreeComponent* c = &tree->components[tree->debug_overlays.slots[s]];
        assert(c->type == UIELEM_BUILTIN_DEBUG_OVERLAY);
        c->u.debug_overlay.font_id_small = font_id_small;
        c->u.debug_overlay.font_id_menu = font_id_menu;
        c->u.debug_overlay.font_id_body = font_id_body;
    }
}

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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_FIND_ID, 1);
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
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_FIND_ID_PROBES, 1);
        int32_t const k = t->id_index_keys[h];
        if( k < 0 )
            break;
        if( k == component_id )
        {
            result = t->id_index_vals[h];
            if( result == -1 )
            {
                /* The winner was reclaimed and the replacement — a duplicate id
                 * that lost the original tie-break — can only be found by a
                 * scan. Do it once and cache the answer (including "none", as
                 * -2) so repeated lookups for a dead id stay O(1). */
                result = UITree_FindByComponentId_Linear(tree, component_id);
                t->id_index_vals[h] = result >= 0 ? result : -2;
                if( result >= 0 && t->id_index_tombs )
                    t->id_index_tombs--;
            }
            else if( result == -2 )
            {
                result = -1;
            }
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
    int old_client_code = dst->client_code;

    if( dst->scripts )
    {
        for( int s = 0; s < dst->scripts_count; s++ )
            free(dst->scripts[s]);
        free(dst->scripts);
    }
    free(dst->scripts_lengths);
    free(dst->script_comparator);
    free(dst->script_operand);
    if( dst->scripts_count > 0 )
        uitree_cs1_script_nodes_drop(tree);
    memset(dst, 0, sizeof(*dst));

    dst->hide = src->hide;
    dst->button_type = src->button_type;
    dst->client_code = src->client_code;
    dst->click_mask = src->click_mask;
    dst->target_mask = src->target_mask;
    dst->over_layer_id = src->over_layer_id;
    dst->over_color = src->over_color;
    dst->active_color = src->active_color;
    dst->active_over_color = src->active_over_color;
    dst->scripts_count = src->scripts_count;
    dst->comparator_count = src->comparator_count;
    dst->script_kind = src->script_kind;
    if( dst->scripts_count > 0 )
        tree->cs1_script_nodes++;

    if( (old_client_code > 0) != (dst->client_code > 0) )
    {
        if( dst->client_code > 0 )
            UITreeNodeSet_Add(&tree->client_code, idx);
        else
            UITreeNodeSet_Remove(&tree->client_code, idx);
    }

    if( src->scripts_count <= 0 || !src->scripts )
        return;

    dst->scripts = calloc((size_t)src->scripts_count, sizeof(int*));
    dst->scripts_lengths = calloc((size_t)src->scripts_count, sizeof(int));
    assert(dst->scripts);
    assert(dst->scripts_lengths);

    for( int i = 0; i < src->scripts_count; i++ )
    {
        if( !src->scripts[i] )
            continue;
        int len =
            (src->scripts_lengths && src->scripts_lengths[i] > 0) ? src->scripts_lengths[i] : 0;
        if( len <= 0 )
            continue;
        dst->scripts[i] = malloc((size_t)len * sizeof(int));
        assert(dst->scripts[i]);
        memcpy(dst->scripts[i], src->scripts[i], (size_t)len * sizeof(int));
        dst->scripts_lengths[i] = len;
    }

    /* Comparator arrays are sized by their own count, not scripts_count. */
    if( src->script_comparator && src->comparator_count > 0 )
    {
        dst->script_comparator = malloc((size_t)src->comparator_count * sizeof(int));
        assert(dst->script_comparator);
        memcpy(
            dst->script_comparator,
            src->script_comparator,
            (size_t)src->comparator_count * sizeof(int));
    }

    if( src->script_operand && src->comparator_count > 0 )
    {
        dst->script_operand = malloc((size_t)src->comparator_count * sizeof(int));
        assert(dst->script_operand);
        memcpy(
            dst->script_operand, src->script_operand, (size_t)src->comparator_count * sizeof(int));
    }
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
        assert(text_owned);
    }
    if( spec->type == UIELEM_RS_TEXT && spec->u.rs_text.text_active )
    {
        text_active_owned = strdup(spec->u.rs_text.text_active);
        assert(text_active_owned);
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
    UITree_MenuOptionsSet(component, &spec->menu_options);
    component->slot_tag = spec->slot_tag;
    component->no_click_through = spec->no_click_through;
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
        /* Slots come off the free list with whatever the previous occupant left
         * behind; the resolve treats a set flag as "box is already correct". */
        component->position.layout_resolved = 0;
    }
    UITree_LayoutInvalidateBoxes(tree);

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

    case UIELEM_BUILTIN_DEBUG_OVERLAY:
        component->u.debug_overlay.font_id_small = spec->u.debug_overlay.font_id_small;
        component->u.debug_overlay.font_id_body = spec->u.debug_overlay.font_id_body;
        component->u.debug_overlay.skin_scene_id = spec->u.debug_overlay.skin_scene_id;
        for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT; i++ )
            component->u.debug_overlay.skin_atlas[i] = spec->u.debug_overlay.skin_atlas[i];
        component->u.debug_overlay.font_id_menu = spec->u.debug_overlay.font_id_menu;
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
        component->u.rs_graphic.sprite_angle_r2pi65536 =
            spec->u.rs_graphic.sprite_angle_r2pi65536;
        break;

    case UIELEM_RS_RECT:
        component->u.rs_rect.color = spec->u.rs_rect.color;
        component->u.rs_rect.filled = spec->u.rs_rect.filled;
        break;

    case UIELEM_RS_MODEL:
        component->u.rs_model.gamecache_model_id = spec->u.rs_model.gamecache_model_id;
        component->u.rs_model.active_model_id = spec->u.rs_model.active_model_id;
        component->u.rs_model.zoom = spec->u.rs_model.zoom;
        component->u.rs_model.xan = spec->u.rs_model.xan;
        component->u.rs_model.yan = spec->u.rs_model.yan;
        component->u.rs_model.zan = spec->u.rs_model.zan;
        component->u.rs_model.rotate_x_speed = spec->u.rs_model.rotate_x_speed;
        component->u.rs_model.rotate_y_speed = spec->u.rs_model.rotate_y_speed;
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

    uitree_live_register(tree, idx);
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
    uitree_child_index_drop(c);               /* ... and none left to index */
    c->is_dirty = 1;
    tree->generation++;
}

/* Sibling steps beyond which a lookup pays for the parent's key->child map.
 * Below it the walk is already cheaper than building and holding the index. */
#define UITREE_CHILD_INDEX_MIN_STEPS 32

int32_t
UITree_FindChildBySubid(
    struct UITree const* tree,
    int32_t parent_index,
    int parent_component_id,
    int sub_id)
{
    (void)parent_component_id;
    assert(tree);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_FIND_CHILD, 1);
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
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_FIND_CHILD_CEIL_MISS, 1);
            return -1;
        }
    }

    /* A hit is what the ceiling cannot make cheap, and a container rebuild is a
     * run of hits. Answer those from the parent's key->child map when one has
     * been built; it reproduces the walk below exactly (dynamic first, static
     * as the fallback), and is only ever built for a duplicate-free child list. */
    {
        struct UITreeComponent* parent = &((struct UITree*)tree)->components[parent_index];
        if( parent->child_key_index && sub_id >= 0 && sub_id < parent->child_key_index_cap )
        {
            int32_t const cap = parent->child_key_index_cap;
            int32_t hit = parent->child_key_index[sub_id];
            if( hit < 0 )
                hit = parent->child_key_index[cap + sub_id];
            if( hit >= 0 )
                TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_FIND_CHILD_HIT, 1);
            return hit;
        }
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
    int32_t found = -1;
    int32_t steps = 0;
    for( int32_t child = tree->components[parent_index].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        steps++;
        struct UITreeComponent const* c = &tree->components[child];
        if( c->dynamic && c->dynamic_child_index == sub_id )
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_FIND_CHILD_HIT, 1);
            found = child;
            break;
        }
        if( !c->dynamic && static_match < 0 && (c->component_id & 0xFFFF) == (sub_id & 0xFFFF) )
            static_match = child;
    }
    if( found < 0 )
    {
        found = static_match;
        if( found >= 0 )
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_FIND_CHILD_HIT, 1);
    }

    /* This list is long enough that walking it per lookup is the cost; index it
     * so the rest of the rebuild is O(1) per row. */
    if( steps > UITREE_CHILD_INDEX_MIN_STEPS )
        uitree_child_index_build((struct UITree*)tree, parent_index);
    return found;
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_CC_CREATE, 1);
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
    case 5: /* TORIRS_COMPONENT_GRAPHIC */
        spec.type = UIELEM_RS_GRAPHIC;
        break;
    case 3: /* TORIRS_COMPONENT_RECT */
        /* Default is outline, not fill. Scripts that want a filled tint call
         * cc_setfill(true) (and usually cc_settrans); those that leave the
         * default get a 1px border. Filled=1 here painted opaque black over
         * the XP tracker's trans=128 row tint (script5364). */
        spec.type = UIELEM_RS_RECT;
        spec.u.rs_rect.color = 0;
        spec.u.rs_rect.filled = 0;
        break;
    case 4: /* TORIRS_COMPONENT_TEXT */
        spec.type = UIELEM_RS_TEXT;
        break;
    case 6: /* TORIRS_COMPONENT_MODEL */
        /* World map key/overview toggles (and ~96 other cc_create sites) use
         * iftype_model. Mapping to CC_OBJ left ApplyModel a no-op and emit
         * skipped the node (obj_id stays 0). Ids -1 so the gap between create
         * and setmodel does not draw scene model 0; zoom 100 is the reference
         * default when no setmodelangle has run yet. */
        spec.type = UIELEM_RS_MODEL;
        spec.u.rs_model.gamecache_model_id = -1;
        spec.u.rs_model.active_model_id = -1;
        spec.u.rs_model.anim_seq_id = -1;
        spec.u.rs_model.zoom = 100;
        break;
    case 9: /* TORIRS_COMPONENT_LINE */
        spec.type = UIELEM_RS_LINE;
        break;
    default:
        /* Type 2 (INV) and any unknown: item box until SETOBJECT fills it. */
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
    /* The copy hangs off a different parent than the template row, so the box
     * that came with `position` is not its box — the resolve treats a set flag
     * as "already correct" and would keep it. */
    dst->position.layout_resolved = 0;
    /* Deep: the submenu block is owned per component, so the copy must not alias
     * the source's (both are reclaimed independently). A template row with no
     * menu options copies as none — dst was just pushed, so it has none yet. */
    if( src.menu_options )
        UITree_MenuOptionsSet(dst, src.menu_options);
    /* Deep for the same reason as the submenu block above: the hook block is
     * owned per component. A template row with no hooks copies as none. */
    if( src.runtime_hooks )
    {
        struct UITreeRuntimeHooks* hooks = UITree_HooksMut(dst);
        if( hooks )
            UITree_HooksBlockCopy(hooks, src.runtime_hooks);
    }
    /* Plain data, but it must be listed explicitly: this function copies field
     * by field rather than by struct assignment, so a template row that binds
     * op keys would silently lose them on copy. */
    UITree_OpKeysSet(dst, src.op_keys);
    uitree_sync_hook_sets(tree, idx);
    if( UITree_OpKeys(dst)->has_bindings )
        UITreeNodeSet_Add(&tree->opkeys, idx);
    else
        UITreeNodeSet_Remove(&tree->opkeys, idx);

    /* cs1 behavior scripts stay uncopied: dynamic children are driven by cs2
     * hooks, and the script arrays are owned per node. */

    dst->is_dirty = 1;
    tree->generation++;
    return idx;
}

/*
 * CC_DELETE — remove one dynamic child, not a parent's whole list.
 *
 * The splice is `UITree_CcDeleteAll`'s per-child body, applied to a node the
 * caller already resolved. Static children are refused outright: `cc_delete`
 * addresses whatever `cc_find` selected, and a script that has selected a
 * component it did not create is a script bug — deleting a cache-built widget
 * would leave a hole nothing rebuilds.
 *
 * The surviving siblings keep their sub-ids. That is what a list deleting one
 * row expects, and it is why this cannot be "delete all and re-add": the child
 * key ceiling is dropped so the next by-sub-id lookup recomputes it, exactly as
 * the batch form does.
 */
void
UITree_CcDelete(
    struct UITree* tree,
    int32_t index)
{
    assert(tree);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_CC_DELETE, 1);
    if( index < 0 || (uint32_t)index >= tree->component_count )
        return;

    struct UITreeComponent* node = &tree->components[index];
    int32_t parent_index = node->parent;
    struct UITreeComponent* parent;
    int32_t child;
    int32_t prev = -1;

    if( !node->dynamic )
        return;
    if( parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return;

    parent = &tree->components[parent_index];
    for( child = parent->first_child; child >= 0; child = tree->components[child].next_sibling )
    {
        if( child == index )
            break;
        prev = child;
    }
    if( child < 0 )
        return;

    if( prev < 0 )
        parent->first_child = node->next_sibling;
    else
        tree->components[prev].next_sibling = node->next_sibling;
    node->parent = -1;
    node->next_sibling = -1;
    uitree_reclaim_subtree(tree, index);

    parent->last_child_hint = -1;
    parent->child_key_max = UITREE_CHILD_KEY_UNKNOWN;
    /* Clearing `parent` above means the reclaim's per-child hook could not find
     * this parent, so the key->child map still points at the freed slot. It goes
     * with the ceiling, and the next long lookup rebuilds both. */
    uitree_child_index_drop(parent);
    parent->is_dirty = 1;
    tree->generation++;
}

void
UITree_CcDeleteAll(
    struct UITree* tree,
    int32_t parent_index)
{
    assert(tree);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_CC_DELETEALL, 1);
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
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_CC_DELETEALL_ROWS, 1);
            uitree_reclaim_subtree(tree, child);
        }
        else
        {
            prev = child;
        }
        child = next;
    }
    /* The splicing above bypasses UITree_UnlinkChild, so the key ceiling and the
     * key->child map are dropped once for the whole batch rather than per row;
     * the next by-sub-id lookup recomputes them over the static children that
     * survived. Both must go: the reclaim's per-child hook ran with an already
     * cleared `parent` and so could not retire either one. */
    parent->last_child_hint = -1;
    parent->child_key_max = UITREE_CHILD_KEY_UNKNOWN;
    uitree_child_index_drop(parent);
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
        /* Keep static / unset slots out: they store dynamic_child_index = -1. */
        if( !c->dynamic )
            continue;
        /*
         * Inclusive lower bound: scripts pass start=1 to walk children whose
         * sub-ids are 1..N (skill-guide Overview tab chrome in 9179). xrsps
         * documents `i > start` for FINDNEXTID-era ops, but that drops the
         * first real child when the allocator's first slot is 1 and leaves
         * 9179 one short of the cc_setonop arm — Overview could not switch
         * back from Quest XP. `i >= start` matches the call sites.
         */
        if( c->dynamic_child_index < start_index )
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_OTHER, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].behavior.hide = hide ? 1 : 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyComponentParam(
    struct UITree* tree,
    int component_id,
    int param_id,
    int value,
    char const* str)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_OTHER, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;

    char* owned = NULL;
    if( str )
    {
        owned = strdup(str);
        assert(owned);
    }

    struct UITreeComponent* c = &tree->components[idx];
    for( int i = 0; i < c->params_count; i++ )
    {
        if( c->params[i].id == param_id )
        {
            free(c->params[i].str);
            c->params[i].str = owned;
            c->params[i].value = value;
            return true;
        }
    }

    if( c->params_count == c->params_capacity )
    {
        /* Four covers the tagging the gameframe scripts actually do (the widest
         * site writes four params onto one row), so the usual component never
         * reallocs. */
        int capacity = c->params_capacity ? c->params_capacity * 2 : 4;
        struct UITreeComponentParam* grown =
            realloc(c->params, (size_t)capacity * sizeof(*grown));
        if( !grown )
        {
            free(owned);
            return false;
        }
        c->params = grown;
        c->params_capacity = capacity;
    }
    c->params[c->params_count].id = param_id;
    c->params[c->params_count].value = value;
    c->params[c->params_count].str = owned;
    c->params_count++;
    return true;
}

char const*
UITree_ComponentParamGetStr(
    struct UITree const* tree,
    int component_id,
    int param_id)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return NULL;

    struct UITreeComponent const* c = &tree->components[idx];
    for( int i = 0; i < c->params_count; i++ )
    {
        if( c->params[i].id == param_id )
            return c->params[i].str;
    }
    return NULL;
}

bool
UITree_ComponentParamGet(
    struct UITree const* tree,
    int component_id,
    int param_id,
    int* out_value)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;

    struct UITreeComponent const* c = &tree->components[idx];
    for( int i = 0; i < c->params_count; i++ )
    {
        if( c->params[i].id == param_id )
        {
            /* A string entry is not an int answer: the int getter has to treat it
             * as absent so the caller falls through to the ParamType default. */
            if( c->params[i].str )
                return false;
            if( out_value )
                *out_value = c->params[i].value;
            return true;
        }
    }
    return false;
}

bool
UITree_ApplyClickMask(
    struct UITree* tree,
    int component_id,
    int32_t click_mask)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_OTHER, 1);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    char* copy = strdup(text ? text : "");
    assert(copy);

    struct UITreeComponent* c = &tree->components[idx];
    if( c->type == UIELEM_RS_TEXT )
    {
        free((void*)c->u.rs_text.text);
        c->u.rs_text.text = copy;
    }
    else
    {
        /* Layers (and other non-TEXT types) still accept if_settext — loot
         * script 4298 stores the source name on info-slot layers this way. */
        free(c->data_text);
        c->data_text = copy;
    }
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    struct UITreeComponent* c = &tree->components[idx];
    c->colour = colour;
    if( c->type == UIELEM_RS_TEXT )
        c->u.rs_text.color = colour;
    else if( c->type == UIELEM_RS_RECT )
        c->u.rs_rect.color = colour;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyFillColour(
    struct UITree* tree,
    int component_id,
    int colour)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].fill_colour = colour;
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_GEO, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    struct UITreeComponent* const com = &tree->components[idx];
    if( com->position.x == x && com->position.y == y && com->position.layout_resolved )
        return true;
    com->position.x = x;
    com->position.y = y;
    com->position.layout_resolved = 0;
    UITree_LayoutInvalidateBoxes(tree);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_GEO, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    struct UITreeComponent* const com = &tree->components[idx];
    if( com->position.width == width && com->position.height == height &&
        com->position.layout_resolved )
        return true;
    com->position.width = width;
    com->position.height = height;
    com->position.layout_resolved = 0;
    UITree_LayoutInvalidateBoxes(tree);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_GEO, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    struct UITreeComponent* const com = &tree->components[idx];
    if( com->position.x == x && com->position.y == y && com->position.x_mode == (int8_t)x_mode &&
        com->position.y_mode == (int8_t)y_mode && com->position.layout_resolved )
        return true;
    com->position.x = x;
    com->position.y = y;
    com->position.x_mode = (int8_t)x_mode;
    com->position.y_mode = (int8_t)y_mode;
    com->position.layout_resolved = 0;
    UITree_LayoutInvalidateBoxes(tree);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_GEO, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    struct UITreeComponent* const com = &tree->components[idx];
    if( com->position.width == width && com->position.height == height &&
        com->position.width_mode == (int8_t)width_mode &&
        com->position.height_mode == (int8_t)height_mode && com->position.layout_resolved )
        return true;
    com->position.width = width;
    com->position.height = height;
    com->position.width_mode = (int8_t)width_mode;
    com->position.height_mode = (int8_t)height_mode;
    com->position.layout_resolved = 0;
    UITree_LayoutInvalidateBoxes(tree);
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyGraphicTiled(
    struct UITree* tree,
    int component_id,
    int tiled)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    tree->components[idx].u.rs_graphic.graphic_shadow = shadow_colour;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyGraphic2DAngle(
    struct UITree* tree,
    int component_id,
    int angle_r2pi65536)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    /* Wrapped, not clamped: the world map's marker timer walks the angle up
     * past a full turn, and the rotate helper's tables want 0..65535. */
    angle_r2pi65536 &= 0xFFFF;
    {
        /* A rotation that never arrives and a rotation that arrives and is then
         * discarded downstream look the same on screen, so the setter is worth
         * a trace of its own. */
        static int debug = -1;
        if( debug < 0 )
            debug = getenv("TORIRS_ANGLE_DEBUG") != NULL;
        if( debug )
            fprintf(
                stderr, "set2dangle: com=0x%08x angle=%d\n", component_id, angle_r2pi65536);
    }
    if( tree->components[idx].u.rs_graphic.sprite_angle_r2pi65536 == angle_r2pi65536 )
        return true;
    tree->components[idx].u.rs_graphic.sprite_angle_r2pi65536 = angle_r2pi65536;
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_GEO, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_LAYER )
        return false;
    struct UITreeComponent* const com = &tree->components[idx];
    if( com->u.rs_layer.scroll_width == scroll_width &&
        com->u.rs_layer.scroll_height == scroll_height )
        return true;
    com->u.rs_layer.scroll_width = scroll_width;
    com->u.rs_layer.scroll_height = scroll_height;
    /* The scroll extent is what this layer's children lay out against
     * (layout_parent_box), so it is a layout input like a position field. */
    UITree_LayoutInvalidate(tree);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_GEO, 1);
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
    int atlas_index,
    int num_mode)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
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
    /* Keep the raw count: -1 is the scripts' "icon only, never a number"
     * sentinel (the spell tooltip's rune cells are cc_setobject($rune, -1)).
     * Clamping it to 1 here grew a yellow "1" on every stackable icon-only
     * cell. Readers that need a drawable count clamp at their own use site. */
    c->item_count = obj_count;
    c->item_scene_id = scene_id;
    c->item_atlas_index = atlas_index;
    c->item_num_mode = (uint8_t)num_mode;

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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].model_transparent = transparent ? 1 : 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyModelOffset(
    struct UITree* tree,
    int component_id,
    int x_offset,
    int y_offset)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_MODEL )
        return false;
    tree->components[idx].u.rs_model.x_offset = x_offset;
    tree->components[idx].u.rs_model.y_offset = y_offset;
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
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
UITree_ApplyModelRotateSpeed(
    struct UITree* tree,
    int component_id,
    int x_speed,
    int y_speed)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_MODEL )
        return false;
    tree->components[idx].u.rs_model.rotate_x_speed = x_speed;
    tree->components[idx].u.rs_model.rotate_y_speed = y_speed;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyModelAnim(
    struct UITree* tree,
    int component_id,
    int anim_seq_id)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_MODEL )
        return false;
    /*
     * Setting the sequence that is already running is a no-op, frame counters
     * included.
     *
     * The reference's IF_SETANIM writes `IfType.modelAnim` and nothing else —
     * `animFrame`/`animCycle` live on the component and are only ever moved by
     * the animator (Client.ts animateInterface). Here the counters do get reset
     * on a *change*, which is the sane reading of "play this instead"; what is
     * not sane is resetting them on a re-apply, because this function has a
     * caller that re-applies constantly: `app_if_head_poll` rebinds a chathead
     * (model *and* anim) whenever the tree generation moves, and a rev-230
     * gameframe bumps the generation on nearly every tick. A dialogue chathead
     * therefore sat on frame 0 forever — the animator advanced it, the poll put
     * it back, and nothing anywhere reported a problem.
     */
    if( tree->components[idx].u.rs_model.anim_seq_id != anim_seq_id )
    {
        tree->components[idx].u.rs_model.anim_seq_id = anim_seq_id;
        tree->components[idx].u.rs_model.anim_frame = 0;
        tree->components[idx].u.rs_model.anim_frame_cycle = 0;
    }
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyTextFont(
    struct UITree* tree,
    int component_id,
    int font_id)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_OTHER, 1);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_OTHER, 1);
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
    UITree_MenuSubmenuClear(UITree_MenuOptionsMut(&tree->components[idx]), op_index);
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
    uint64_t str_mask,
    char const* const* strs,
    int str_argc)
{
    (void)component_id;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_HOOK, 1);
    assert(slot);
    assert(tree);

    /* Clamping and the tail allocations both belong to the slot type — see
     * ui/uitree_hook.h for why they are no longer inline arrays. */
    UITree_HookSet(slot, script_id, argv, argc, str_mask, strs, str_argc);
    {
        int32_t idx = UITree_FindByComponentId(tree, component_id);
        if( idx >= 0 )
            uitree_sync_hook_sets(tree, idx);
    }
    return true;
}

bool
UITree_ApplyOpBase(
    struct UITree* tree,
    int component_id,
    char const* text)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_OTHER, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    strncpy(
        UITree_MenuOptionsMut(&tree->components[idx])->option,
        text ? text : "",
        UITREE_MENU_OPTION_LEN - 1);
    UITree_MenuOptionsMut(&tree->components[idx])->option[UITREE_MENU_OPTION_LEN - 1] = '\0';
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyTargetVerb(
    struct UITree* tree,
    int component_id,
    char const* text)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_OTHER, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    strncpy(
        UITree_MenuOptionsMut(&tree->components[idx])->target_verb,
        text ? text : "",
        UITREE_MENU_OPTION_LEN - 1);
    UITree_MenuOptionsMut(&tree->components[idx])->target_verb[UITREE_MENU_OPTION_LEN - 1] =
        '\0';
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
    {
        struct UITreeOpKeys* keys = UITree_OpKeysMut(&tree->components[idx]);
        return keys ? &keys->slots[op_index - 1] : NULL;
    }
}

static void
uitree_opkey_refresh_has_bindings(struct UITreeComponent* node)
{
    UITree_OpKeysMut(node)->has_bindings = 0;
    for( int i = 0; i < UITREE_OPKEY_SLOTS; i++ )
        if( UITree_OpKeys(node)->slots[i].bound )
        {
            UITree_OpKeysMut(node)->has_bindings = 1;
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_OTHER, 1);
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
        if( UITree_OpKeys(node)->has_bindings )
            UITreeNodeSet_Add(&tree->opkeys, (int32_t)(node - tree->components));
        else
            UITreeNodeSet_Remove(&tree->opkeys, (int32_t)(node - tree->components));
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
    UITree_OpKeysMut(node)->has_bindings = 1;
    UITreeNodeSet_Add(&tree->opkeys, (int32_t)(node - tree->components));
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_OTHER, 1);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_OTHER, 1);
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
        if( UITree_MenuOptions(component)->ops[i][0] != '\0' )
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

void
UITree_ReclaimInterfaceGroup(
    struct UITree* tree,
    int group_id)
{
    struct UITreeNodeSet const* gset;
    enum
    {
        UITREE_RECLAIM_ROOTS_STACK = 256
    };
    int32_t stack_roots[UITREE_RECLAIM_ROOTS_STACK];
    int32_t* roots;
    int32_t* heap_roots = NULL;
    int root_n = 0;
    int gi;
    int i;

    assert(tree);
    if( group_id < 0 )
        return;

    gset = UITree_GroupNodes(tree, group_id);
    if( !gset || gset->count <= 0 )
        return;

    if( gset->count > UITREE_RECLAIM_ROOTS_STACK )
    {
        heap_roots = (int32_t*)malloc((size_t)gset->count * sizeof(int32_t));
        assert(heap_roots);
        roots = heap_roots;
    }
    else
    {
        roots = stack_roots;
    }

    /* Snapshot roots first: reclaim mutates the group node set. Same selection
     * as the hide-on-close loop — pack-internal nodes ride with their root. */
    for( gi = 0; gi < gset->count; gi++ )
    {
        int32_t idx = gset->slots[gi];
        struct UITreeComponent* c;
        assert(idx >= 0 && (uint32_t)idx < tree->component_count);
        c = &tree->components[idx];
        if( c->freed || c->component_id < 0 )
            continue;
        if( ((c->component_id >> 16) & 0xffff) != group_id )
            continue;
        if( c->parent >= 0 &&
            ((tree->components[c->parent].component_id >> 16) & 0xffff) == group_id )
            continue;
        roots[root_n++] = idx;
    }

    for( i = 0; i < root_n; i++ )
    {
        int32_t idx = roots[i];
        struct UITreeComponent* c = &tree->components[idx];
        if( c->freed )
            continue;
        if( c->parent >= 0 )
            UITree_UnlinkChild(tree, c->parent, idx);
        else
            UITree_UnlinkFromRootList(tree, idx);
        uitree_reclaim_subtree(tree, idx);
    }

    free(heap_roots);
    tree->generation++;
}

void
UITree_SetComponentDragActive(
    struct UITree* tree,
    int32_t idx,
    int active)
{
    assert(tree);
    assert(idx >= 0 && (uint32_t)idx < tree->component_count);

    struct UITreeComponent* com = &tree->components[idx];
    uint8_t const want = active ? 1 : 0;
    if( com->drag_active == want )
        return;
    com->drag_active = want;
    if( want )
    {
        tree->drag_active_nodes++;
        return;
    }
    assert(tree->drag_active_nodes > 0);
    tree->drag_active_nodes--;
}

int
UITree_ContainerHasMounts(
    struct UITree const* tree,
    int container_uid)
{
    int i;

    assert(tree);
    if( container_uid < 0 )
        return 0;
    for( i = 0; i < tree->interface_parent_count; i++ )
    {
        if( tree->interface_parents[i].container_uid == container_uid )
            return 1;
    }
    return 0;
}

int
UITree_ChildMountType(
    struct UITree const* tree,
    int container_uid,
    struct UITreeComponent const* child)
{
    int group;
    int i;

    assert(tree);
    if( container_uid < 0 )
        return -1;
    assert(child);
    group = (child->component_id >> 16) & 0xffff;
    for( i = 0; i < tree->interface_parent_count; i++ )
    {
        if( tree->interface_parents[i].container_uid == container_uid &&
            tree->interface_parents[i].group_id == group )
            return tree->interface_parents[i].type;
    }
    return -1;
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
    /* cc_setdraggable(parentUid, childIndex): host normally resolves to the
     * child's uid at set time (child_index left -1). Lazy form still used by
     * tests / older stores: render area is parent.children[childIndex]
     * (scrollbar track = child 0), falling back to the parent if missing. */
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
    if( UITree_Hooks(c)->on_drag.script_id > 0 )
        return 1;
    return 0;
}

int
UITree_ComponentIsDropTarget(struct UITreeComponent const* c)
{
    struct UITreeRuntimeHooks const* hooks;
    assert(c);
    if( (c->behavior.click_mask & UITREE_FLAG_DRAG_ON) != 0 )
        return 1;
    hooks = UITree_Hooks(c);
    if( hooks->on_drag.script_id > 0 )
        return 1;
    if( hooks->on_drag_complete.script_id > 0 )
        return 1;
    if( hooks->on_op.script_id > 0 )
        return 1;
    if( hooks->on_click.script_id > 0 )
        return 1;
    return 0;
}

int
UITree_ComponentOrAncestorHidden(
    struct UITree const* tree,
    int component_id)
{
    int32_t idx;
    int group;
    int mount_hops = 0;
    assert(tree);
    idx = UITree_FindByComponentId(tree, component_id);
    while( idx >= 0 && (uint32_t)idx < tree->component_count )
    {
        group = (tree->components[idx].component_id >> 16) & 0xffff;

        /* Walk the component's pack-local parents first. InterfaceParent
         * mounts are not represented by UITreeComponent::parent: the mounted
         * group's roots stay in the root list and the mount table supplies the
         * cross-interface edge at draw time. A visibility query has to follow
         * that same edge or a hook in an inactive side tab looks visible and
         * reacts to every transmit. The account-summary tab is particularly
         * expensive: its var listener rebuilds roughly a thousand dynamic
         * widgets when combat level changes on an XP update. */
        do
        {
            if( tree->components[idx].behavior.hide )
                return 1;
            idx = tree->components[idx].parent;
        } while( idx >= 0 && (uint32_t)idx < tree->component_count );

        /* Continue at the container this interface group is mounted into.
         * Nested mounts (gameframe -> side tab -> account summary) are common,
         * hence the loop rather than a single lookup. Guard malformed mount
         * cycles even though InterfaceParent construction should forbid them. */
        idx = -1;
        for( int i = 0; i < tree->interface_parent_count; i++ )
        {
            if( tree->interface_parents[i].group_id == group )
            {
                idx = UITree_FindByComponentId(tree, tree->interface_parents[i].container_uid);
                break;
            }
        }
        if( idx < 0 || ++mount_hops > UITREE_INTERFACE_PARENT_MAX )
            break;
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_WALK_DROP, 1);
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
        /* Collapsed clipping layer: nothing under it is drawn, so nothing under
         * it can be hit either (same rule as emit_walk_node). */
        if( UITree_LayerCullsChildren(c, w, h) )
            return 0;
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

    /* Mounted roots are physical children in UITree, but reference input walks
     * them as a separate final group. Ordinary children inherit this host's
     * local scroll; InterfaceParent roots use the raw host origin. Keep the
     * existing drop-candidate semantics across a type-0 boundary: Java does
     * not clear its current dragged-on widget there. */
    int const has_mounts = UITree_ContainerHasMounts(tree, c->component_id);
    for( int mount_sweep = 0; mount_sweep <= has_mounts; mount_sweep++ )
    {
        for( child = c->first_child; child >= 0;
             child = tree->components[child].next_sibling )
        {
            int const is_mount =
                has_mounts &&
                UITree_ChildMountType(tree, c->component_id, &tree->components[child]) >= 0;
            if( is_mount != mount_sweep )
                continue;
            drop_target_pick_in_subtree(
                tree,
                child,
                px,
                py,
                exclude_component_id,
                is_mount ? scroll_off_x : child_scroll_x,
                is_mount ? scroll_off_y : child_scroll_y,
                &child_clip,
                &child_surface,
                best_id,
                best_depth,
                depth + 1);
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
