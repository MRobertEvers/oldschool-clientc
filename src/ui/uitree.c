#include "uitree.h"

#include "uitree_frame.h"

#include "perf/torirs_perf.h"
#include "uitree_layout.h"
#include "uitree_scroll.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"



static void
uitree_topo_bump(struct UITree* tree, int line)
{
    tree->dirty_gen++;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_EMIT_DIRTY_TOPO, 1);
    g_torirs_dirty_topo_seq++;
    g_torirs_dirty_topo_line = line;
}

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
    tree->entity_overlay_index = -1;
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
    if( tree->entity_overlay_index == idx )
        tree->entity_overlay_index = -1;
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
    if( c->type == UIELEM_BUILTIN_ENTITY_OVERLAY )
        tree->entity_overlay_index = idx;
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
        if( c->type == UIELEM_BUILTIN_ENTITY_OVERLAY )
            assert(tree->entity_overlay_index == (int32_t)i);
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

/* Not the zeroed block: an absent background is -1, not 0. */
struct UITreeInvSlots const uitree_inv_slots_none = {
    .bg_scene_id = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
};

struct UITreeInvSlots const*
UITree_InvSlots(struct UITreeComponent const* c)
{
    assert(c);
    return c->u.rs_inv.slots ? c->u.rs_inv.slots : &uitree_inv_slots_none;
}

struct UITreeInvSlots*
UITree_InvSlotsMut(struct UITreeComponent* c)
{
    assert(c);
    if( !c->u.rs_inv.slots )
    {
        c->u.rs_inv.slots = malloc(sizeof(*c->u.rs_inv.slots));
        assert(c->u.rs_inv.slots);
        *c->u.rs_inv.slots = uitree_inv_slots_none;
    }
    return c->u.rs_inv.slots;
}

struct UITreeChatConfig const uitree_chat_none = { 0 };
struct UITreeDebugOverlayConfig const uitree_debug_overlay_none = { 0 };
struct UITreeChatButtonConfig const uitree_chat_button_none = { 0 };
struct UITreeLoginInputConfig const uitree_login_input_none = { 0 };

struct UITreeChatConfig const*
UITree_Chat(struct UITreeComponent const* c)
{
    assert(c);
    return c->u.chat ? c->u.chat : &uitree_chat_none;
}

struct UITreeChatConfig*
UITree_ChatMut(struct UITreeComponent* c)
{
    assert(c);
    if( !c->u.chat )
    {
        c->u.chat = malloc(sizeof(*c->u.chat));
        assert(c->u.chat);
        *c->u.chat = uitree_chat_none;
    }
    return c->u.chat;
}

struct UITreeDebugOverlayConfig const*
UITree_DebugOverlay(struct UITreeComponent const* c)
{
    assert(c);
    return c->u.debug_overlay ? c->u.debug_overlay : &uitree_debug_overlay_none;
}

struct UITreeDebugOverlayConfig*
UITree_DebugOverlayMut(struct UITreeComponent* c)
{
    assert(c);
    if( !c->u.debug_overlay )
    {
        c->u.debug_overlay = malloc(sizeof(*c->u.debug_overlay));
        assert(c->u.debug_overlay);
        *c->u.debug_overlay = uitree_debug_overlay_none;
    }
    return c->u.debug_overlay;
}

struct UITreeChatButtonConfig const*
UITree_ChatButton(struct UITreeComponent const* c)
{
    assert(c);
    return c->u.chat_button ? c->u.chat_button : &uitree_chat_button_none;
}

struct UITreeChatButtonConfig*
UITree_ChatButtonMut(struct UITreeComponent* c)
{
    assert(c);
    if( !c->u.chat_button )
    {
        c->u.chat_button = malloc(sizeof(*c->u.chat_button));
        assert(c->u.chat_button);
        *c->u.chat_button = uitree_chat_button_none;
    }
    return c->u.chat_button;
}

struct UITreeLoginInputConfig const*
UITree_LoginInput(struct UITreeComponent const* c)
{
    assert(c);
    return c->u.login_input ? c->u.login_input : &uitree_login_input_none;
}

struct UITreeLoginInputConfig*
UITree_LoginInputMut(struct UITreeComponent* c)
{
    assert(c);
    if( !c->u.login_input )
    {
        c->u.login_input = malloc(sizeof(*c->u.login_input));
        assert(c->u.login_input);
        *c->u.login_input = uitree_login_input_none;
    }
    return c->u.login_input;
}

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
        TORIRS_ERR("uitree: invalid parent index %d for child %d (count=%u)\n",
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
            /*
             * Three-halves, not double. A component is 404 bytes and the tree
             * ratchets to the high-water of SIMULTANEOUS components -- it never
             * shrinks, because the free list recycles slots instead. Doubling
             * therefore rounds that high-water up to the next power of two and
             * keeps the gap for the life of the process, and it is the largest
             * single block in the UI. Three-halves lands nearer the true count
             * and halves the copy that a grow has to hold live.
             *
             * The step stays at least one so the sequence cannot stall on a
             * small capacity where integer division rounds the growth away.
             */
            uint32_t new_capacity =
                tree->component_capacity == 0
                    ? 16
                    : tree->component_capacity + (tree->component_capacity >> 1) + 1;
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
    if( ++tree->next_incarnation == 0 )
        tree->next_incarnation++;
    component->incarnation = tree->next_incarnation;
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
    uitree_topo_bump(tree, __LINE__);
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
            uitree_topo_bump(tree, __LINE__);
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
    uitree_topo_bump(tree, __LINE__);
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
        uitree_topo_bump(tree, __LINE__);
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
    case UIELEM_BUILTIN_INKWELL:
        return "inkwell";
    case UIELEM_BUILTIN_LOGIN_INPUT:
        return "login_input";
    case UIELEM_BUILTIN_LOGIN_BUTTON:
        return "login_button";
    case UIELEM_BUILTIN_LOGIN_TOGGLE:
        return "login_toggle";
    case UIELEM_BUILTIN_LOGIN_MESSAGE:
        return "login_message";
    case UIELEM_BUILTIN_TITLE_PROGRESS:
        return "title_progress";
    case UIELEM_BUILTIN_TITLE_PROGRESS_TEXT:
        return "title_progress_text";
    case UIELEM_BUILTIN_TITLE_FLAMES:
        return "title_flames";
    case UIELEM_BUILTIN_MINIMENU:
        return "minimenu";
    case UIELEM_BUILTIN_HOVERTEXT:
        return "hovertext";
    case UIELEM_BUILTIN_MULTIWAY:
        return "multiway";
    case UIELEM_BUILTIN_REBOOT_TIMER:
        return "reboot_timer";
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
    case UIELEM_RS_ARC:
        return "rs_arc";
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
    tree->entity_overlay_index = -1;
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
    if( c->type == UIELEM_RS_INV )
    {
        free(c->u.rs_inv.slots);
        c->u.rs_inv.slots = NULL;
    }
    if( c->type == UIELEM_BUILTIN_CHAT )
    {
        free(c->u.chat);
        c->u.chat = NULL;
    }
    if( c->type == UIELEM_BUILTIN_DEBUG_OVERLAY )
    {
        free(c->u.debug_overlay);
        c->u.debug_overlay = NULL;
    }
    if( c->type == UIELEM_BUILTIN_CHAT_BUTTON )
    {
        free(c->u.chat_button);
        c->u.chat_button = NULL;
    }
    if( c->type == UIELEM_BUILTIN_LOGIN_INPUT )
    {
        free(c->u.login_input);
        c->u.login_input = NULL;
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
    free(tree->layout_dirty);
    free(tree->emit_visited);
    uitree_all_sets_free(tree);
    UITree_FrameForget(tree);
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
    /* A plugin layout's hold names NODES, and every one of them has just
     * stopped existing. Dropping it rather than releasing it is deliberate:
     * there is nothing left to restore, and the frame's owner is asked for a
     * fresh declaration once the new tree is baked. */
    UITree_FrameForget(tree);
}


void
UITree_MarkAllDirty(struct UITree* tree)
{
    assert(tree);

    for( uint32_t i = 0; i < tree->component_count; i++ )
        tree->components[i].is_dirty = 1;
    /* Outside the loop deliberately: dirty_gen is a tree-level generation, so a
     * blanket mark is one change to it, not one per node. Braces above are not
     * optional here — without them this line reads as loop body. */
    uitree_topo_bump(tree, __LINE__);
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
    /* `is_dirty` is unconditional above: it is this node's repaint bit and the
     * caller is right about it either way. Only the emit-retention generation
     * is filtered, and only by reachability — a mark on a node the last walk
     * never entered cannot change the next walk's output, because the next walk
     * will not enter it either unless an ancestor moved, and that ancestor
     * bumps. See UITree::emit_visited for why this is a bitmap rather than a
     * hidden-ancestor query, and why a node past the bitmap counts as reached. */
    if( (uint32_t)idx >= tree->emit_visited_cap || tree->emit_visited[idx] == tree->emit_epoch )
    {
        tree->dirty_gen++;
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_EMIT_DIRTY_MARK, 1);
        /* Only the reached branch is sequenced. An unreached mark cannot change
         * the next emit, so it is not what holds emit_gen_quiet at zero, and
         * counting it here would attribute harmless writes to a script and send
         * target 7 after the wrong one. See g_torirs_dirty_mark_seq. */
        g_torirs_dirty_mark_seq++;
    }
    else
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_EMIT_DIRTY_UNREACHED, 1);
}

/*
 * Runtime component writes have more than one cache consequence.  Keeping the
 * mapping here means a typed setter cannot remember the repaint bit while
 * forgetting the layout cache (or vice versa), which is exactly the class of
 * bug the scripted entity-overlay position update exposed.
 *
 * These are effects, not events: callers describe the field they are setting
 * through UITree_Set*At, and those setters choose the effects.  External
 * dependencies such as camera or inventory epochs belong one level above this
 * API; after recomputing a value they finish by calling a typed setter.
 */
enum UITreeMutationImpact
{
    UITREE_IMPACT_EMIT_SELF = 1u << 0,
    /** This node's own layout inputs changed.  A changed resolved box is
     * propagated to descendants by UITree_LayoutResolve. */
    UITREE_IMPACT_LAYOUT_SELF = 1u << 1,
    /** A child coordinate-space input changed without changing the parent's
     * resolved box (currently a layer's scroll extent), so every cached box
     * must be made unresolved. */
    UITREE_IMPACT_LAYOUT_TREE = 1u << 2,
    /** Traversal reachability changed.  This bump cannot use the previous
     * emit-visited bitmap, because an unhide invalidates that very answer. */
    UITREE_IMPACT_REACHABILITY = 1u << 3,
};

static void
uitree_note_mutation(
    struct UITree* tree,
    int32_t idx,
    uint32_t impacts)
{
    assert(tree);
    assert(idx >= 0 && (uint32_t)idx < tree->component_count);
    assert(!tree->components[idx].freed);

    if( impacts & UITREE_IMPACT_LAYOUT_TREE )
        UITree_LayoutInvalidate(tree);
    else if( impacts & UITREE_IMPACT_LAYOUT_SELF )
    {
        tree->components[idx].position.layout_resolved = 0;
        UITree_LayoutInvalidateNode(tree, idx);
    }

    if( impacts & UITREE_IMPACT_EMIT_SELF )
        UITree_MarkNodeDirty(tree, idx);
    if( impacts & UITREE_IMPACT_REACHABILITY )
        uitree_topo_bump(tree, __LINE__);
}

static struct UITreeComponent*
uitree_component_at_mutable(
    struct UITree* tree,
    int32_t idx)
{
    assert(tree);
    if( idx < 0 || (uint32_t)idx >= tree->component_count || tree->components[idx].freed )
        return NULL;
    return &tree->components[idx];
}

void
UITree_MarkNodeVisibilityDirty(
    struct UITree* tree,
    int32_t idx)
{
    assert(tree);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;
    tree->components[idx].is_dirty = 1;
    /* Visibility is also reachability. In particular, an unhidden node has a
     * zero emit_visited bit precisely because it was hidden last walk, so the
     * ordinary filtered mark cannot describe this transition. */
    uitree_topo_bump(tree, __LINE__);
}

int
UITree_SetReplacementHidden(
    struct UITree* tree,
    int32_t node_index,
    uint32_t incarnation,
    int hidden)
{
    struct UITreeComponent* component;

    assert(tree);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return 0;
    component = &tree->components[node_index];
    if( component->freed || incarnation == 0 || component->incarnation != incarnation )
        return 0;
    hidden = hidden ? 1 : 0;
    if( component->replacement_hidden != hidden )
    {
        component->replacement_hidden = (uint8_t)hidden;
        /* Both hiding and revealing change reachability. Use the unconditional
         * visibility bump so a previously pruned target cannot be retained. */
        UITree_MarkNodeVisibilityDirty(tree, node_index);
    }
    return 1;
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
           type == UIELEM_BUILTIN_MINIMENU || type == UIELEM_BUILTIN_HOVERTEXT ||
           type == UIELEM_BUILTIN_MULTIWAY || type == UIELEM_BUILTIN_REBOOT_TIMER;
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
            /* No dirty_gen bump. This is the one place where "repaint this
             * node" and "this node's emit descriptor changed" genuinely come
             * apart, and conflating them is what defeated the Opt 11 retention
             * gate on every single frame: this runs per frame over the compass,
             * cross, minimenu and hovertext nodes, so it was contributing ~34
             * of the ~45 bumps/frame all on its own. Saying a node always
             * repaints says nothing about whether what it emits changed — and
             * when it does change, the setter that changed it marks it. */
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
    /*
     * Fibonacci multiply, then fold the high half down before the caller masks.
     *
     * The fold is the part that matters, and leaving it out made this hash
     * ignore the interface id outright. Callers index with `hash & (cap - 1)`,
     * and the low n bits of `x * K` are a function of the low n bits of `x`
     * alone — multiplication never carries information downwards. Fibonacci
     * hashing puts its quality in the *high* bits for exactly that reason and is
     * normally read as `>> (32 - n)`.
     *
     * So with cap 16384 the old hash keyed on bits 0..13 of the id and discarded
     * every bit above them. Ids are `(iface_id << 16) | child_id`
     * (UITree_ComponentUid), which puts the whole interface id in the discarded
     * half: every component sharing a child index, across every resident
     * interface, hashed to one slot. Measured cost was 3.59 probes per lookup
     * against the ~1.4 the 0.44 load factor should give.
     *
     * Keep the result full-width so the existing `& mask` callers stay correct.
     */
    uint32_t h = (uint32_t)component_id * 2654435761u;
    h ^= h >> 16;
    return h;
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
        struct UITreeDebugOverlayConfig* overlay = UITree_DebugOverlayMut(c);
        overlay->font_id_small = font_id_small;
        overlay->font_id_menu = font_id_menu;
        overlay->font_id_body = font_id_body;
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
    if( c->freed )
        return;
    struct UITreeBehavior* dst = &c->behavior;
    int old_client_code = dst->client_code;
    uint8_t const old_hide = dst->hide;

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
    {
        uitree_note_mutation(
            tree,
            idx,
            UITREE_IMPACT_EMIT_SELF |
                (old_hide != dst->hide ? UITREE_IMPACT_REACHABILITY : 0));
        return;
    }

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

    /* SetBehavior is mostly a construction API today, but it is public and can
     * replace fields consumed by emit (active/hover colours and CS1 scripts) or
     * traversal (`hide`). Keep it on the same mutation seam as the typed runtime
     * setters so a post-publication call cannot leave a retained list stale. */
    uitree_note_mutation(
        tree,
        idx,
        UITREE_IMPACT_EMIT_SELF |
            (old_hide != dst->hide ? UITREE_IMPACT_REACHABILITY : 0));
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
    component->role_id = spec->role_id;
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
    /* Only the new node is unresolved. A box is a pure function of the
     * node's own fields and its PARENT's box, so gaining a child moves
     * neither the parent nor the new node's siblings. */
    UITree_LayoutInvalidateNode(tree, idx);

    switch( spec->type )
    {
    case UIELEM_BUILTIN_SPRITE:
    case UIELEM_BUILTIN_COMPASS:
    case UIELEM_BUILTIN_CROSS:
    case UIELEM_BUILTIN_MULTIWAY:
        component->u.sprite.scene_id = spec->u.sprite.scene_id;
        component->u.sprite.atlas_index = spec->u.sprite.atlas_index;
        component->u.sprite.mask_scene_id = spec->u.sprite.mask_scene_id;
        component->u.sprite.mask_atlas_index = spec->u.sprite.mask_atlas_index;
        break;

    /* The inkwell carries a CHOICE, not a binding, and the three fields use
     * -1 for "the profile said nothing" so the host can substitute its own
     * defaults (yellow walks, red interacts). Falling through to the memset
     * push_element_unlinked left behind does not mean "unstated": it means
     * style=SPLASH, walk=YELLOW and interact=YELLOW, all stated. The first two
     * happen to equal the defaults, which is why only the third showed --
     * every interact marker came out yellow however red the cross beside it
     * was, and `style=` in a profile was silently ignored. */
    case UIELEM_BUILTIN_INKWELL:
        component->u.inkwell.style = spec->u.inkwell.style;
        component->u.inkwell.walk_color = spec->u.inkwell.walk_color;
        component->u.inkwell.interact_color = spec->u.inkwell.interact_color;
        break;

    case UIELEM_BUILTIN_MINIMENU:
        component->u.minimenu.font_id = spec->u.minimenu.font_id;
        break;

    case UIELEM_BUILTIN_HOVERTEXT:
        component->u.hovertext.font_id = spec->u.hovertext.font_id;
        break;

    case UIELEM_BUILTIN_REBOOT_TIMER:
        component->u.reboot_timer.font_id = spec->u.reboot_timer.font_id;
        component->u.reboot_timer.color = spec->u.reboot_timer.color;
        break;

    case UIELEM_BUILTIN_DEBUG_OVERLAY:
        /* The spec arm is the same struct now, so the field-by-field copy
         * this replaced is one assignment. */
        *UITree_DebugOverlayMut(component) = spec->u.debug_overlay;
        break;

    case UIELEM_BUILTIN_CHAT:
        *UITree_ChatMut(component) = spec->u.chat;
        break;

    case UIELEM_BUILTIN_CHAT_BUTTON:
        *UITree_ChatButtonMut(component) = spec->u.chat_button;
        component->is_dirty = 1;
        uitree_topo_bump(tree, __LINE__);
        break;

    case UIELEM_BUILTIN_LOGIN_INPUT:
        *UITree_LoginInputMut(component) = spec->u.login_input;
        break;

    case UIELEM_BUILTIN_LOGIN_BUTTON:
        component->u.login_button.scene_id = spec->u.login_button.scene_id;
        component->u.login_button.atlas_index = spec->u.login_button.atlas_index;
        component->u.login_button.action = spec->u.login_button.action;
        break;

    case UIELEM_BUILTIN_LOGIN_TOGGLE:
        component->u.login_toggle.scene_id = spec->u.login_toggle.scene_id;
        component->u.login_toggle.atlas_index = spec->u.login_toggle.atlas_index;
        component->u.login_toggle.scene_id_on = spec->u.login_toggle.scene_id_on;
        component->u.login_toggle.atlas_index_on = spec->u.login_toggle.atlas_index_on;
        component->u.login_toggle.action = spec->u.login_toggle.action;
        component->u.login_toggle.toggle = spec->u.login_toggle.toggle;
        break;

    case UIELEM_BUILTIN_LOGIN_MESSAGE:
        component->u.login_message.index = spec->u.login_message.index;
        component->u.login_message.font_id = spec->u.login_message.font_id;
        component->u.login_message.color = spec->u.login_message.color;
        component->u.login_message.center = spec->u.login_message.center;
        component->u.login_message.shadowed = spec->u.login_message.shadowed;
        break;

    case UIELEM_BUILTIN_TITLE_PROGRESS:
        component->u.title_progress.color = spec->u.title_progress.color;
        component->u.title_progress.px_per_percent = spec->u.title_progress.px_per_percent;
        break;

    case UIELEM_BUILTIN_TITLE_FLAMES:
        component->u.title_flames.side = spec->u.title_flames.side;
        component->u.title_flames.bias = spec->u.title_flames.bias;
        component->u.title_flames.sway = spec->u.title_flames.sway;
        component->u.title_flames.run = spec->u.title_flames.run;
        component->u.title_flames.row = spec->u.title_flames.row;
        component->u.title_flames.blur = spec->u.title_flames.blur;
        break;

    case UIELEM_BUILTIN_TITLE_PROGRESS_TEXT:
        component->u.title_progress_text.font_id = spec->u.title_progress_text.font_id;
        component->u.title_progress_text.color = spec->u.title_progress_text.color;
        component->u.title_progress_text.center = spec->u.title_progress_text.center;
        component->u.title_progress_text.shadowed = spec->u.title_progress_text.shadowed;
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
        /*
         * The GENERIC colour too, and for the same reason on the three types
         * below: `IF_GETCOLOUR` answers `node->colour`, which until now only
         * `UITree_ApplyColour` ever wrote — so a script asking a
         * cache-authored component for its colour got 0.
         *
         * `brew_tools_init` opens with `if_getcolour($component21)` and hands
         * the answer to all ten of its buttons, so every label was drawn black
         * instead of the authored grey.
         */
        component->colour = spec->u.rs_text.color;
        component->u.rs_text.center = spec->u.rs_text.center;
        component->u.rs_text.y_align = spec->u.rs_text.y_align;
        component->u.rs_text.baseline = spec->u.rs_text.baseline;
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
        component->colour = spec->u.rs_rect.color;
        component->u.rs_rect.filled = spec->u.rs_rect.filled;
        break;

    case UIELEM_RS_ARC:
        component->u.rs_arc.color = spec->u.rs_arc.color;
        component->colour = spec->u.rs_arc.color;
        component->u.rs_arc.filled = spec->u.rs_arc.filled;
        component->u.rs_arc.line_width =
            spec->u.rs_arc.line_width > 0 ? spec->u.rs_arc.line_width : 1;
        component->u.rs_arc.arc_start = spec->u.rs_arc.arc_start;
        component->u.rs_arc.arc_end = spec->u.rs_arc.arc_end;
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
        /* The block is only allocated when a spec actually carries per-slot
         * data. The no-override case is uitree_inv_slots_none, which already
         * reads as offset 0 and background -1 -- the same answer the explicit
         * -1 fill used to write into every inv component. */
        if( spec->u.rs_inv.inv_slot_offset_x && spec->u.rs_inv.inv_slot_offset_y )
        {
            struct UITreeInvSlots* slots = UITree_InvSlotsMut(component);
            memcpy(
                slots->offset_x,
                spec->u.rs_inv.inv_slot_offset_x,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
            memcpy(
                slots->offset_y,
                spec->u.rs_inv.inv_slot_offset_y,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        }
        if( spec->u.rs_inv.inv_slot_bg_scene_id && spec->u.rs_inv.inv_slot_bg_atlas_index )
        {
            struct UITreeInvSlots* slots = UITree_InvSlotsMut(component);
            memcpy(
                slots->bg_scene_id,
                spec->u.rs_inv.inv_slot_bg_scene_id,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
            memcpy(
                slots->bg_atlas_index,
                spec->u.rs_inv.inv_slot_bg_atlas_index,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
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
        uitree_topo_bump(tree, __LINE__);
        break;

    case UIELEM_RS_LINE:
        component->u.rs_line.color = spec->u.rs_line.color;
        component->colour = spec->u.rs_line.color;
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
     * on the invisible orphan — the "Weapon:%1" bug).
     *
     * PROFILE-AUTHORED children are kept, and that exception is the whole
     * reason this loop rebuilds the list instead of truncating it.
     *
     * What this function is for is emptying a SLOT before the server's
     * interface goes into it (task_slot_mount), and a slot's contents are the
     * server's to replace. A control the profile authored into that slot is
     * not: it was placed by the boot manifest's RevConfig, no server knows it
     * exists, and nothing will ever put it back. The symptom is exact and
     * confusing -- the control is there in an offline boot and gone the moment
     * a real server sends its login burst of IF_SETTABs, which looks like the
     * control failing to build rather than like something sweeping it away.
     *
     * Recognised by id band (TORIRS_REVCONFIG_GROUP), the same way the chrome's
     * own components are recognised everywhere else in this tree. */
    {
        int32_t child = c->first_child;
        int32_t kept_head = -1;
        int32_t kept_tail = -1;

        while( child >= 0 )
        {
            int32_t const next = tree->components[child].next_sibling;
            int const id = tree->components[child].component_id;
            if( id >= 0 && ((id >> 16) & 0xFFFF) == TORIRS_REVCONFIG_GROUP )
            {
                tree->components[child].next_sibling = -1;
                if( kept_tail >= 0 )
                    tree->components[kept_tail].next_sibling = child;
                else
                    kept_head = child;
                kept_tail = child;
            }
            else
            {
                uitree_reclaim_subtree(tree, child);
            }
            child = next;
        }
        c->first_child = kept_head;
        c->last_child_hint = kept_tail;
    }
    c->child_key_max = UITREE_CHILD_KEY_NONE; /* nothing left to match by key */
    uitree_child_index_drop(c);               /* ... and none left to index */
    c->is_dirty = 1;
    uitree_topo_bump(tree, __LINE__);
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
    /* Readable mid-run so target 7 can attribute creates to the running script;
     * see g_torirs_cc_create_seq. */
    g_torirs_cc_create_seq++;
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
    case 10: /* TORIRS_COMPONENT_ARC */
        /* An arc with no CC_SETARC yet is a zero-width sector, which draws
         * nothing -- the reference's own default, and the right one: 5480
         * creates all three children before it shapes any of them, so a
         * full-turn default would flash a disc for the width of a rebuild. */
        spec.type = UIELEM_RS_ARC;
        spec.u.rs_arc.color = 0;
        spec.u.rs_arc.filled = 0;
        spec.u.rs_arc.line_width = 1;
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
UITree_EntityOverlayCreateLayer(struct UITree* tree, int sub_id, int width, int height)
{
    assert(tree);

    int32_t const parent = tree->entity_overlay_index;
    if( parent < 0 )
        return -1;

    int const parent_component_id = tree->components[parent].component_id;
    int const iface_id = parent_component_id >= 0 ? (parent_component_id >> 16) : 0;

    /* Replace in slot, like CC_CREATE: a script that rebuilds its overlay every
     * tick must cost one node, not one per tick. */
    int32_t existing = UITree_FindChildBySubid(tree, parent, parent_component_id, sub_id);
    if( existing >= 0 && tree->components[existing].dynamic )
    {
        UITree_UnlinkChild(tree, parent, existing);
        uitree_reclaim_subtree(tree, existing);
    }

    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = UITree_AllocateDynamicComponentId(tree, iface_id);
    spec.dynamic = 1;
    spec.dynamic_child_index = sub_id;
    spec.always_dirty = 1;
    spec.has_position = 1;
    spec.position.kind = UIPOS_XY;
    spec.position.width = width;
    spec.position.height = height;

    int32_t const idx = UITree_Push(tree, parent, &spec);
    if( idx < 0 )
        return -1;
    /* The children the script creates inherit this, and every overlay this
     * cache builds is IF3 -- an if3=0 layer lays its graphics out at native
     * sprite size and ignores the cc_setsize the script just made. */
    tree->components[idx].if3 = 1;
    return idx;
}

bool
UITree_EntityOverlaySetLayerPosition(
    struct UITree* tree,
    int32_t idx,
    int x,
    int y)
{
    struct UITreeComponent* c;

    assert(tree);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return false;
    c = &tree->components[idx];
    if( c->freed || c->type != UIELEM_RS_LAYER || c->parent != tree->entity_overlay_index )
        return false;
    return UITree_SetPositionAt(tree, idx, x, y);
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
    uitree_topo_bump(tree, __LINE__);
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
    uitree_topo_bump(tree, __LINE__);
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
    int removed_any = 0;
    while( child >= 0 )
    {
        int32_t const next = tree->components[child].next_sibling;
        if( tree->components[child].dynamic )
        {
            removed_any = 1;
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
    /* A deleteall that found nothing dynamic to delete changed nothing: no slot
     * was recycled, `first_child` still points where it did, and the key ceiling
     * and key->child map are still correct for the static children. Invalidating
     * them anyway — and bumping dirty_gen — is not merely wasted work, it is the
     * write that made the whole tree look modified every frame.
     *
     * The reference client rebuilds a list by clearing it and re-adding rows, so
     * the common steady-state call is deleteall on a parent that is *already*
     * empty. Measured on the login-screen scene: one per-tick onTimer script did
     * exactly one topology bump per call, 2004 in 2000 frames, and it was this
     * line. That single bump held the emit retention gate (targets 11, 12 and
     * 14) at zero hits for the entire run. Making the no-op case a real no-op is
     * the general fix — it is a property of deleteall, not of that script. */
    if( !removed_any )
        return;

    /* The splicing above bypasses UITree_UnlinkChild, so the key ceiling and the
     * key->child map are dropped once for the whole batch rather than per row;
     * the next by-sub-id lookup recomputes them over the static children that
     * survived. Both must go: the reclaim's per-child hook ran with an already
     * cleared `parent` and so could not retire either one. */
    parent->last_child_hint = -1;
    parent->child_key_max = UITREE_CHILD_KEY_UNKNOWN;
    uitree_child_index_drop(parent);
    parent->is_dirty = 1;
    uitree_topo_bump(tree, __LINE__);
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
UITree_SetHideAt(
    struct UITree* tree,
    int32_t idx,
    int hide)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c )
        return false;
    hide = hide ? 1 : 0;
    /*
     * Writing the value that is already there and marking the node dirty anyway
     * is what makes a quiet frame repaint. Scripts restate their whole interface
     * on every transmit — an inventory's 28 cells come back with the same object,
     * the same colour and the same hide flag — so the mutation volume the emit
     * walk sees is far larger than the number of things that actually moved.
     * Every applier below therefore compares first and leaves the node clean
     * when nothing changed; the position/size ones already did.
     */
    if( c->behavior.hide == (uint8_t)hide )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    c->behavior.hide = (uint8_t)hide;
    uitree_note_mutation(
        tree,
        idx,
        UITREE_IMPACT_EMIT_SELF | UITREE_IMPACT_REACHABILITY);
    return true;
}

bool
UITree_SetCS1ActiveAt(
    struct UITree* tree,
    int32_t idx,
    int active)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c )
        return false;
    active = active ? 1 : 0;
    if( c->cs1_active == (uint8_t)active )
        return true;
    c->cs1_active = (uint8_t)active;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool
UITree_SetCS1ValueAt(
    struct UITree* tree,
    int32_t idx,
    int value_index,
    int value)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c || value_index < 0 || value_index >= UITREE_CS1_VALUE_MAX )
        return false;
    if( c->cs1_values[value_index] == value )
        return true;
    c->cs1_values[value_index] = value;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool
UITree_SetFrameHiddenAt(
    struct UITree* tree,
    int32_t idx,
    int hidden)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c )
        return false;
    hidden = hidden ? 1 : 0;
    if( c->frame_hidden == (uint8_t)hidden )
        return true;
    c->frame_hidden = (uint8_t)hidden;
    uitree_note_mutation(
        tree,
        idx,
        UITREE_IMPACT_EMIT_SELF | UITREE_IMPACT_REACHABILITY);
    return true;
}

bool
UITree_SetScreenHiddenAt(
    struct UITree* tree,
    int32_t idx,
    int hidden)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c )
        return false;
    hidden = hidden ? 1 : 0;
    if( c->screen_hidden == (uint8_t)hidden )
        return true;
    c->screen_hidden = (uint8_t)hidden;
    uitree_note_mutation(
        tree,
        idx,
        UITREE_IMPACT_EMIT_SELF | UITREE_IMPACT_REACHABILITY);
    return true;
}

bool
UITree_SetProjectionHiddenAt(
    struct UITree* tree,
    int32_t idx,
    int hidden)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c || c->type != UIELEM_RS_LAYER || c->parent != tree->entity_overlay_index )
        return false;
    hidden = hidden ? 1 : 0;
    if( c->projection_hidden == (uint8_t)hidden )
        return true;
    c->projection_hidden = (uint8_t)hidden;
    uitree_note_mutation(
        tree,
        idx,
        UITREE_IMPACT_EMIT_SELF | UITREE_IMPACT_REACHABILITY);
    return true;
}

bool
UITree_ApplyHide(
    struct UITree* tree,
    int component_id,
    int hide)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_OTHER, 1);
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    return UITree_SetHideAt(tree, idx, hide);
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

    struct UITreeComponent* c = &tree->components[idx];
    char* owned;

    for( int i = 0; i < c->params_count; i++ )
    {
        if( c->params[i].id != param_id )
            continue;
        /* Restating a param that is already there was a strdup and a free to
         * arrive back where it started; the gameframe re-tags its rows on every
         * rebuild. Both-NULL is caught by the pointer compare. */
        if( c->params[i].value == value &&
            (c->params[i].str == str ||
             (c->params[i].str && str && strcmp(c->params[i].str, str) == 0)) )
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
            return true;
        }
        owned = NULL;
        if( str )
        {
            owned = strdup(str);
            assert(owned);
        }
        free(c->params[i].str);
        c->params[i].str = owned;
        c->params[i].value = value;
        return true;
    }

    owned = NULL;
    if( str )
    {
        owned = strdup(str);
        assert(owned);
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
    if( tree->components[idx].behavior.click_mask == click_mask )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    tree->components[idx].behavior.click_mask = click_mask;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_SetTextAt(
    struct UITree* tree,
    int32_t idx,
    char const* text)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c )
        return false;
    char const* text_now = c->type == UIELEM_RS_TEXT ? c->u.rs_text.text : c->data_text;
    char* copy;

    if( !text )
        text = "";
    /* Before the strdup, not after: the chatbox's 500 line components are
     * rewritten with their current contents on every chat transmit, and a text
     * that has not changed is a malloc, a free and a repaint for nothing. */
    if( text_now && strcmp(text_now, text) == 0 )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }

    copy = strdup(text);
    assert(copy);

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
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool
UITree_ApplyText(
    struct UITree* tree,
    int component_id,
    char const* text)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    return UITree_SetTextAt(tree, idx, text);
}

bool
UITree_SetGraphicAt(
    struct UITree* tree,
    int32_t idx,
    int scene_id,
    int atlas_index)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c || c->type != UIELEM_RS_GRAPHIC )
        return false;
    if( c->u.rs_graphic.scene_id == scene_id &&
        c->u.rs_graphic.atlas_index == atlas_index &&
        c->u.rs_graphic.graphic_hitbox_only == 0 )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    c->u.rs_graphic.scene_id = scene_id;
    c->u.rs_graphic.atlas_index = atlas_index;
    c->u.rs_graphic.graphic_hitbox_only = 0;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
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
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    return UITree_SetGraphicAt(tree, idx, scene_id, atlas_index);
}

bool
UITree_SetColourAt(
    struct UITree* tree,
    int32_t idx,
    int colour)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c )
        return false;
    /* Both the generic field and the type's own copy — they can be written from
     * different places, so "the generic one already matches" is not enough. */
    int const colour_now_matches =
        c->colour == colour &&
        (c->type != UIELEM_RS_TEXT || c->u.rs_text.color == colour) &&
        (c->type != UIELEM_RS_RECT || c->u.rs_rect.color == colour) &&
        (c->type != UIELEM_RS_ARC || c->u.rs_arc.color == colour) &&
        (c->type != UIELEM_RS_LINE || c->u.rs_line.color == colour);
    if( colour_now_matches )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    c->colour = colour;
    if( c->type == UIELEM_RS_TEXT )
        c->u.rs_text.color = colour;
    else if( c->type == UIELEM_RS_RECT )
        c->u.rs_rect.color = colour;
    else if( c->type == UIELEM_RS_ARC )
        c->u.rs_arc.color = colour;
    /* A LINE keeps its own copy too, and the emit arm reads THAT
     * (`out->color = component->u.rs_line.color`). Missing it here meant
     * `cc_setcolour` on a line was accepted, stored in the generic field and
     * then never drawn: every script-built divider rule came out black. */
    else if( c->type == UIELEM_RS_LINE )
        c->u.rs_line.color = colour;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool
UITree_ApplyColour(
    struct UITree* tree,
    int component_id,
    int colour)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    return UITree_SetColourAt(tree, idx, colour);
}

bool
UITree_SetFillColourAt(
    struct UITree* tree,
    int32_t idx,
    int colour)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c )
        return false;
    if( c->fill_colour == colour )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    c->fill_colour = colour;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool
UITree_ApplyFillColour(
    struct UITree* tree,
    int component_id,
    int colour)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    return UITree_SetFillColourAt(tree, idx, colour);
}

bool
UITree_SetTransparencyAt(
    struct UITree* tree,
    int32_t idx,
    int transparency)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c )
        return false;
    if( c->trans == transparency )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    c->trans = transparency;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool
UITree_SetMinimenuFontAt(
    struct UITree* tree,
    int32_t idx,
    int font_id)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c || c->type != UIELEM_BUILTIN_MINIMENU )
        return false;
    if( c->u.minimenu.font_id == font_id )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    c->u.minimenu.font_id = font_id;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool
UITree_SetArcAnglesAt(
    struct UITree* tree,
    int32_t idx,
    int arc_start,
    int arc_end)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c || c->type != UIELEM_RS_ARC )
        return false;
    if( c->u.rs_arc.arc_start == arc_start && c->u.rs_arc.arc_end == arc_end )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    c->u.rs_arc.arc_start = arc_start;
    c->u.rs_arc.arc_end = arc_end;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool
UITree_SetModelAt(
    struct UITree* tree,
    int32_t idx,
    int model_id)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c || c->type != UIELEM_RS_MODEL )
        return false;
    if( c->u.rs_model.gamecache_model_id == model_id )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    c->u.rs_model.gamecache_model_id = model_id;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool
UITree_SetModelPoseAt(
    struct UITree* tree,
    int32_t idx,
    int x_offset,
    int y_offset,
    int x_angle,
    int y_angle,
    int z_angle,
    int zoom)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c || c->type != UIELEM_RS_MODEL )
        return false;
    if( c->u.rs_model.x_offset == x_offset && c->u.rs_model.y_offset == y_offset &&
        c->u.rs_model.xan == x_angle && c->u.rs_model.yan == y_angle &&
        c->u.rs_model.zan == z_angle && (zoom <= 0 || c->u.rs_model.zoom == zoom) )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    c->u.rs_model.x_offset = x_offset;
    c->u.rs_model.y_offset = y_offset;
    c->u.rs_model.xan = x_angle;
    c->u.rs_model.yan = y_angle;
    c->u.rs_model.zan = z_angle;
    if( zoom > 0 )
        c->u.rs_model.zoom = zoom;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool
UITree_SetPositionAt(
    struct UITree* tree,
    int32_t idx,
    int x,
    int y)
{
    struct UITreeComponent* const com = uitree_component_at_mutable(tree, idx);
    if( !com )
        return false;
    int const frame_owned = UITree_FramePositionOwned(tree, idx);
    if( com->position.x == x && com->position.y == y &&
        (com->position.layout_resolved || frame_owned) )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    com->position.x = x;
    com->position.y = y;
    /* The plugin owns only the effective box. Keep accepting the cache's
     * native state underneath it, but do not invalidate or dirty a frame whose
     * visible result did not change. Release will expose this value and marks
     * the node once at that transition. */
    if( frame_owned )
        return true;
    uitree_note_mutation(
        tree,
        idx,
        UITREE_IMPACT_LAYOUT_SELF | UITREE_IMPACT_EMIT_SELF);
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
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    return UITree_SetPositionAt(tree, idx, x, y);
}

bool
UITree_SetXYBoxAt(
    struct UITree* tree,
    int32_t idx,
    int x,
    int y,
    int width,
    int height)
{
    struct UITreeComponent* const com = uitree_component_at_mutable(tree, idx);
    if( !com )
        return false;
    if( com->position.kind == UIPOS_XY && com->position.x == x && com->position.y == y &&
        com->position.width == width && com->position.height == height )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    com->position.kind = UIPOS_XY;
    com->position.x = x;
    com->position.y = y;
    com->position.width = width;
    com->position.height = height;
    uitree_note_mutation(
        tree,
        idx,
        UITREE_IMPACT_LAYOUT_SELF | UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool
UITree_SetSizeAt(
    struct UITree* tree,
    int32_t idx,
    int width,
    int height)
{
    struct UITreeComponent* const com = uitree_component_at_mutable(tree, idx);
    if( !com )
        return false;
    int const frame_owned = UITree_FramePositionOwned(tree, idx);
    if( com->position.width == width && com->position.height == height &&
        (com->position.layout_resolved || frame_owned) )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    com->position.width = width;
    com->position.height = height;
    if( frame_owned )
        return true;
    uitree_note_mutation(
        tree,
        idx,
        UITREE_IMPACT_LAYOUT_SELF | UITREE_IMPACT_EMIT_SELF);
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
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    return UITree_SetSizeAt(tree, idx, width, height);
}

bool
UITree_SetPositionModesAt(
    struct UITree* tree,
    int32_t idx,
    int x,
    int y,
    int x_mode,
    int y_mode)
{
    struct UITreeComponent* const com = uitree_component_at_mutable(tree, idx);
    if( !com )
        return false;
    int const frame_owned = UITree_FramePositionOwned(tree, idx);
    if( com->position.x == x && com->position.y == y && com->position.x_mode == (int8_t)x_mode &&
        com->position.y_mode == (int8_t)y_mode &&
        (com->position.layout_resolved || frame_owned) )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    com->position.x = x;
    com->position.y = y;
    com->position.x_mode = (int8_t)x_mode;
    com->position.y_mode = (int8_t)y_mode;
    if( frame_owned )
        return true;
    uitree_note_mutation(
        tree,
        idx,
        UITREE_IMPACT_LAYOUT_SELF | UITREE_IMPACT_EMIT_SELF);
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
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    return UITree_SetPositionModesAt(tree, idx, x, y, x_mode, y_mode);
}

bool
UITree_SetSizeModesAt(
    struct UITree* tree,
    int32_t idx,
    int width,
    int height,
    int width_mode,
    int height_mode)
{
    struct UITreeComponent* const com = uitree_component_at_mutable(tree, idx);
    if( !com )
        return false;
    int const frame_owned = UITree_FramePositionOwned(tree, idx);
    if( com->position.width == width && com->position.height == height &&
        com->position.width_mode == (int8_t)width_mode &&
        com->position.height_mode == (int8_t)height_mode &&
        (com->position.layout_resolved || frame_owned) )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    com->position.width = width;
    com->position.height = height;
    com->position.width_mode = (int8_t)width_mode;
    com->position.height_mode = (int8_t)height_mode;
    if( frame_owned )
        return true;
    uitree_note_mutation(
        tree,
        idx,
        UITREE_IMPACT_LAYOUT_SELF | UITREE_IMPACT_EMIT_SELF);
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
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    return UITree_SetSizeModesAt(tree, idx, width, height, width_mode, height_mode);
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
    tiled = tiled ? 1 : 0;
    if( tree->components[idx].u.rs_graphic.tiled == tiled )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    tree->components[idx].u.rs_graphic.tiled = tiled;
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
    if( tree->components[idx].u.rs_graphic.outline == outline )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
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
    if( tree->components[idx].u.rs_graphic.graphic_shadow == shadow_colour )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
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
            TORIRS_LOG("set2dangle: com=0x%08x angle=%d\n", component_id, angle_r2pi65536);
    }
    if( tree->components[idx].u.rs_graphic.sprite_angle_r2pi65536 == angle_r2pi65536 )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    tree->components[idx].u.rs_graphic.sprite_angle_r2pi65536 = angle_r2pi65536;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_SetScrollSizeAt(
    struct UITree* tree,
    int32_t idx,
    int scroll_width,
    int scroll_height)
{
    struct UITreeComponent* const com = uitree_component_at_mutable(tree, idx);
    int changed;
    int box_width;
    int box_height;
    int max_x;
    int max_y;
    int clamped_x;
    int clamped_y;
    int position_changed;

    if( !com || com->type != UIELEM_RS_LAYER )
        return false;
    UITree_EnsureLayoutFor(tree, idx);
    UITree_LayoutGetBounds(&com->position, NULL, NULL, &box_width, &box_height);
    changed = com->u.rs_layer.scroll_width != scroll_width ||
              com->u.rs_layer.scroll_height != scroll_height;
    if( changed )
    {
        com->u.rs_layer.scroll_width = scroll_width;
        com->u.rs_layer.scroll_height = scroll_height;
        /* The scroll extent is what this layer's children lay out against
         * (layout_parent_box), so it is a layout input like a position field. */
        uitree_note_mutation(
            tree,
            idx,
            UITREE_IMPACT_LAYOUT_TREE | UITREE_IMPACT_EMIT_SELF);
    }

    /* Extent changes can make the old canonical offset invalid. The layer box
     * was resolved before invalidation, so canonicalize without resolving the
     * whole tree or turning this lazy layout mutation into an eager one. */
    max_x = scroll_width - box_width;
    max_y = scroll_height - box_height;
    if( max_x < 0 )
        max_x = 0;
    if( max_y < 0 )
        max_y = 0;
    clamped_x = com->scroll_x;
    clamped_y = com->scroll_y;
    if( clamped_x < 0 )
        clamped_x = 0;
    if( clamped_x > max_x )
        clamped_x = max_x;
    if( clamped_y < 0 )
        clamped_y = 0;
    if( clamped_y > max_y )
        clamped_y = max_y;
    position_changed = com->scroll_x != clamped_x || com->scroll_y != clamped_y;
    if( position_changed )
    {
        com->scroll_x = clamped_x;
        com->scroll_y = clamped_y;
        uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    }
    if( !changed && !position_changed )
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
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
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    return UITree_SetScrollSizeAt(tree, idx, scroll_width, scroll_height);
}

bool
UITree_SetScrollPosAt(
    struct UITree* tree,
    int32_t idx,
    int scroll_x,
    int scroll_y)
{
    struct UITreeComponent* const com = uitree_component_at_mutable(tree, idx);
    if( !com )
        return false;
    if( com->type == UIELEM_RS_LAYER )
    {
        int max_x;
        int max_y;

        UITree_EnsureLayoutFor(tree, idx);
        max_x = UITree_ScrollMaxX(com);
        max_y = UITree_ScrollMaxY(com);
        if( scroll_x < 0 )
            scroll_x = 0;
        if( scroll_x > max_x )
            scroll_x = max_x;
        if( scroll_y < 0 )
            scroll_y = 0;
        if( scroll_y > max_y )
            scroll_y = max_y;
    }
    if( com->scroll_x == scroll_x && com->scroll_y == scroll_y )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    com->scroll_x = scroll_x;
    com->scroll_y = scroll_y;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
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
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    return UITree_SetScrollPosAt(tree, idx, scroll_x, scroll_y);
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

    /*
     * Tracked rather than short-circuited, because the sibling silhouette below
     * is state this write owns as much as the item fields are: a cell whose
     * object is unchanged can still have had its silhouette toggled from
     * elsewhere, and returning early would leave the two disagreeing. The
     * silhouette call is itself an ApplyHide, so it costs nothing when it agrees.
     */
    int changed;

    if( obj_id <= 0 )
    {
        changed = c->item_id != 0 || c->item_count != 0 || c->item_scene_id != -1 ||
                  c->item_atlas_index != 0;
        if( c->type == UIELEM_CC_OBJ )
            changed = changed || c->u.cc_obj.obj_id != 0 || c->u.cc_obj.obj_count != 0 ||
                      c->u.cc_obj.scene_id != -1 || c->u.cc_obj.atlas_index != 0;
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
        if( !changed )
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
            return true;
        }
        UITree_MarkNodeDirty(tree, idx);
        return true;
    }

    changed = c->item_id != obj_id || c->item_count != obj_count ||
              c->item_scene_id != scene_id || c->item_atlas_index != atlas_index ||
              c->item_num_mode != (uint8_t)num_mode || c->behavior.hide != 0;
    if( c->type == UIELEM_CC_OBJ )
        changed = changed || c->u.cc_obj.obj_id != obj_id || c->u.cc_obj.obj_count != obj_count ||
                  c->u.cc_obj.scene_id != scene_id || c->u.cc_obj.atlas_index != atlas_index;

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
        (void)UITree_SetHideAt(tree, idx, 0);
    /* Hide silhouette sibling while an item occupies the equipment slot. */
    if( is_equipment_overlay && c->parent >= 0 )
    {
        int32_t parent_idx = c->parent;
        int32_t sil_idx = UITree_FindChildBySubid(
            tree, parent_idx, tree->components[parent_idx].component_id, 2);
        if( sil_idx >= 0 && tree->components[sil_idx].type == UIELEM_RS_GRAPHIC )
            (void)UITree_ApplyHide(tree, tree->components[sil_idx].component_id, 1);
    }
    if( !changed )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
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
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    return UITree_SetModelAt(tree, idx, model_id);
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
    transparent = transparent ? 1 : 0;
    if( tree->components[idx].model_transparent == (uint8_t)transparent )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    tree->components[idx].model_transparent = (uint8_t)transparent;
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
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    struct UITreeComponent* const c = uitree_component_at_mutable(tree, idx);
    if( !c || c->type != UIELEM_RS_MODEL )
        return false;
    return UITree_SetModelPoseAt(
        tree,
        idx,
        x_offset,
        y_offset,
        c->u.rs_model.xan,
        c->u.rs_model.yan,
        c->u.rs_model.zan,
        0);
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
    int32_t const idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    struct UITreeComponent* const c = uitree_component_at_mutable(tree, idx);
    if( !c || c->type != UIELEM_RS_MODEL )
        return false;
    return UITree_SetModelPoseAt(
        tree,
        idx,
        c->u.rs_model.x_offset,
        c->u.rs_model.y_offset,
        xan,
        yan,
        c->u.rs_model.zan,
        zoom);
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
    if( tree->components[idx].u.rs_model.rotate_x_speed == x_speed &&
        tree->components[idx].u.rs_model.rotate_y_speed == y_speed )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
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
    /*
     * The one applier that stays unconditional. `UITreeAnim_Advance` moves
     * `anim_frame` without marking the node — it only reports "something was
     * posed" to the frame loop — so on a widget whose sequence is running, this
     * re-apply is what keeps the node emit-eligible while its pose changes
     * underneath. Comparing here would freeze animated chatheads on whichever
     * frame they were last dirtied at, which reads as a broken sequence rather
     * than as a missing dirty bit.
     */
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
    if( tree->components[idx].u.rs_text.font_id == font_id )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
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
    if( tree->components[idx].u.rs_text.center == h_align &&
        tree->components[idx].u.rs_text.y_align == v_align &&
        tree->components[idx].u.rs_text.line_height == line_height )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
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
    shadowed = shadowed ? 1 : 0;
    if( tree->components[idx].u.rs_text.shadowed == shadowed )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    tree->components[idx].u.rs_text.shadowed = shadowed;
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
    enabled = enabled ? 1 : 0;
    if( tree->components[idx].force_left_click == (uint8_t)enabled )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    tree->components[idx].force_left_click = (uint8_t)enabled;
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

    /*
     * A re-registration that names the binding already here has nothing to do.
     * Scripts re-arm `if_seton*` wholesale every time they rebuild an interface
     * — the inventory's per-slot hooks come back identical on every
     * inv_transmit — and setting the same thing again means freeing both tails,
     * mallocing them back at the same sizes, strdup'ing every string, then
     * resolving the component id and rewriting five set memberships to their
     * current values. `UITree_HookEquals` answers "would that change anything"
     * against post-clamp values, so a true here means byte-identical, and the
     * sets are a pure function of the slot, so they cannot have drifted while it
     * held still.
     */
    if( UITree_HookEquals(slot, script_id, argv, argc, str_mask, strs, str_argc) )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_HOOK_SKIP, 1);
        return true;
    }

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
    if( !text )
        text = "";
    /* strncmp to the same bound the copy uses: two texts that differ only past
     * the truncation point land on the same stored option, so they are equal
     * here too. */
    if( strncmp(
            UITree_MenuOptions(&tree->components[idx])->option,
            text,
            UITREE_MENU_OPTION_LEN - 1) == 0 )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    strncpy(
        UITree_MenuOptionsMut(&tree->components[idx])->option, text, UITREE_MENU_OPTION_LEN - 1);
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
    if( !text )
        text = "";
    if( strncmp(
            UITree_MenuOptions(&tree->components[idx])->target_verb,
            text,
            UITREE_MENU_OPTION_LEN - 1) == 0 )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    strncpy(
        UITree_MenuOptionsMut(&tree->components[idx])->target_verb,
        text,
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
        tree->drag_active_nodes++;
    else
    {
        assert(tree->drag_active_nodes > 0);
        tree->drag_active_nodes--;
    }

    /* Starting or ending a drag changes both the source subtree's position in
     * the command list and, for deferred drags, its z-order/pass membership.
     * Active frames are deliberately non-retainable because drag_visual_x/y
     * move independently below; this mark is still required for the release
     * frame, after drag_active_nodes has returned to zero. */
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
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

static int
uitree_node_or_ancestor_hidden(
    struct UITree const* tree,
    int32_t idx,
    int include_plugin_hidden,
    int32_t ignore_own_replacement,
    int ignore_frame_hidden)
{
    int group;
    int mount_hops = 0;
    int32_t group_root = -1;
    assert(tree);
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
            group_root = idx;
            /* On the node whose own replacement the caller stands for, the
             * gameframe's suppression is excused along with the replacement
             * itself: the replacement IS what the arranger provides in place
             * of the decoration it hid. @see emit_walk_node, which paints the
             * tombstone of such a node for the same reason.
             *
             * `ignore_frame_hidden` excuses it on the whole walk instead, for
             * a caller that names a COMPONENT rather than a place -- a
             * synthesised press. Two separate excuses because they answer two
             * different questions, and only the first is about this node.
             * @see UITree_NodeOrAncestorDisplayHiddenEx. */
            if( tree->components[idx].behavior.hide ||
                (include_plugin_hidden &&
                 ((tree->components[idx].frame_hidden && !ignore_frame_hidden &&
                   idx != ignore_own_replacement) ||
                  tree->components[idx].screen_hidden ||
                  tree->components[idx].projection_hidden ||
                  (tree->components[idx].replacement_hidden &&
                   idx != ignore_own_replacement))) )
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
        if( idx < 0 )
            return group_root >= 0 && !UITree_RootIsDisplayable(tree, group_root);
        if( ++mount_hops > UITREE_INTERFACE_PARENT_MAX )
            break;
    }
    return 0;
}

int
UITree_ComponentOrAncestorHidden(
    struct UITree const* tree,
    int component_id)
{
    /* Cache/script activity remains live beneath a plugin frame so native CS2
     * state is current the instant the effective frame layer is released. */
    return uitree_node_or_ancestor_hidden(
        tree, UITree_FindByComponentId(tree, component_id), 0, -1, 0);
}

int
UITree_ComponentOrAncestorDisplayHidden(
    struct UITree const* tree,
    int component_id)
{
    return uitree_node_or_ancestor_hidden(
        tree, UITree_FindByComponentId(tree, component_id), 1, -1, 0);
}

int
UITree_NodeOrAncestorDisplayHidden(
    struct UITree const* tree,
    int32_t node_index)
{
    assert(tree);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return 1;
    return uitree_node_or_ancestor_hidden(tree, node_index, 1, -1, 0);
}

int
UITree_NodeOrAncestorDisplayHiddenExceptReplacement(
    struct UITree const* tree,
    int32_t node_index)
{
    assert(tree);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return 1;
    return uitree_node_or_ancestor_hidden(
        tree, node_index, 1, node_index, 0);
}

int
UITree_NodeOrAncestorDisplayHiddenEx(
    struct UITree const* tree,
    int32_t node_index,
    int ignore_own_replacement,
    int ignore_frame_hidden)
{
    assert(tree);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return 1;
    return uitree_node_or_ancestor_hidden(
        tree,
        node_index,
        1,
        ignore_own_replacement ? node_index : -1,
        ignore_frame_hidden);
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
    int32_t* best_node,
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
    if( c->behavior.hide || c->frame_hidden || c->screen_hidden || c->replacement_hidden ||
        c->projection_hidden )
        return 0;
    if( c->component_id == exclude_component_id )
        return 0;

    if( clip && clip->clip_w > 0 && clip->clip_h > 0 && !UITree_PointInClip(px, py, clip) )
        return 0;

    UITree_LayoutGetBounds(&c->position, &x, &y, &w, &h);
    /* Drop targets are picked at DRAWN positions using the same effective,
     * clamped ancestor scroll as emit and hit-testing. */
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
        int effective_scroll_x;
        int effective_scroll_y;
        UITree_ScrollGetClamped(c, &effective_scroll_x, &effective_scroll_y);
        if( UITree_ScrollLayerNeedsHorizontal(c) )
            child_scroll_x += effective_scroll_x;
        if( UITree_ScrollLayerNeedsVertical(c) )
            child_scroll_y += effective_scroll_y;
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
                best_node,
                best_depth,
                depth + 1);
        }
    }

    if( hit && UITree_ComponentIsDropTarget(c) && depth >= *best_depth )
    {
        *best_depth = depth;
        *best_id = c->component_id;
        *best_node = idx;
    }
    return *best_id >= 0;
}

int32_t
UITree_FindDropTargetNode(
    struct UITree const* tree,
    int px,
    int py,
    int exclude_component_id,
    int* out_component_id)
{
    int32_t root;
    int32_t best_node = -1;
    int best_id = -1;
    int best_depth = -1;
    assert(tree);
    for( root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        if( tree->components[root].behavior.hide || tree->components[root].frame_hidden ||
            tree->components[root].screen_hidden ||
            tree->components[root].replacement_hidden ||
            tree->components[root].projection_hidden )
            continue;
        drop_target_pick_in_subtree(
            tree,
            root,
            px,
            py,
            exclude_component_id,
            0,
            0,
            NULL,
            NULL,
            &best_id,
            &best_node,
            &best_depth,
            0);
    }
    if( out_component_id )
        *out_component_id = best_id;
    return best_node;
}

int
UITree_FindDropTarget(
    struct UITree const* tree,
    int px,
    int py,
    int exclude_component_id)
{
    int component_id = -1;
    (void)UITree_FindDropTargetNode(
        tree, px, py, exclude_component_id, &component_id);
    return component_id;
}
