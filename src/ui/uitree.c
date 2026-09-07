#include "uitree.h"

#include "uitree_frame.h"
#include "uitree_input.h"
#include "uitree_minimenu.h"

#include "perf/torirs_perf.h"
#include "uitree_layout.h"
#include "uitree_scroll.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

struct UITreeWidgetGeometry
{
    struct UITreeWidgetGeometry* next;
    uint64_t owner, position_serial, size_serial, hidden_serial;
    uint64_t projection_serial;
    uint64_t outline_serial;
    int projection_height;
    int x, y, width, height;
    bool hidden;
    bool outline;
    /* Depth relation to another widget; anchor_serial 0 = never stated. */
    uint64_t anchor_serial;
    int anchor_relation;
    struct UITreeNodeRef anchor_target;
    /* Native re-skin: serial 0 = never stated. */
    uint64_t art_serial, mask_serial;
    int art_scene_id, mask_scene_id;
};
static uint64_t widget_geometry_serial;


#define UITREE_NATIVE_GEOMETRY_FIELDS(F) \
    F(position.kind) F(position.x) F(position.y) F(position.width) F(position.height) \
    F(position.relative_flags) F(position.anchor_x) F(position.anchor_y) \
    F(position.left) F(position.top) F(position.right) F(position.bottom) \
    F(position.x_mode) F(position.y_mode) F(position.width_mode) F(position.height_mode) \
    F(position.aspect_w) F(position.aspect_h) F(position.safe_area_source) \
    F(position.safe_area_flags) F(position.safe_area_margin) F(scroll_x) F(scroll_y)
#define AUDIT_NAME(field) #field,
static char const* const geometry_field_names[] = {
    UITREE_NATIVE_GEOMETRY_FIELDS(AUDIT_NAME) "scroll_width", "scroll_height"
};
#undef AUDIT_NAME
#define GEOMETRY_FIELD_COUNT (sizeof(geometry_field_names) / sizeof(geometry_field_names[0]))
struct UITreeGeometryRecord
{
    uint64_t incarnation;
    int32_t values[GEOMETRY_FIELD_COUNT];
};
struct UITreeGeometryAudit
{
    struct UITreeGeometryRecord* records;
    uint32_t capacity;
};

static void
uitree_geometry_values(struct UITreeComponent const* c, int32_t* values)
{
    int i = 0;
#define AUDIT_VALUE(field) values[i++] = (int32_t)c->field;
    UITREE_NATIVE_GEOMETRY_FIELDS(AUDIT_VALUE)
#undef AUDIT_VALUE
    values[i++] = c->type == UIELEM_RS_LAYER ? c->u.rs_layer.scroll_width : 0;
    values[i++] = c->type == UIELEM_RS_LAYER ? c->u.rs_layer.scroll_height : 0;
}

static void
uitree_geometry_audit_stamp(struct UITree* tree, int32_t idx)
{
    struct UITreeGeometryAudit* audit = tree->geometry_audit;
    if( !audit || idx < 0 || (uint32_t)idx >= tree->component_count ) return;
    if( audit->capacity < tree->component_capacity )
    {
        uint32_t old = audit->capacity;
        audit->capacity = tree->component_capacity;
        audit->records = realloc(audit->records, (size_t)audit->capacity * sizeof(*audit->records));
        if( !audit->records ) abort();
        memset(audit->records + old, 0, (size_t)(audit->capacity-old) * sizeof(*audit->records));
    }
    audit->records[idx].incarnation = tree->components[idx].incarnation;
    uitree_geometry_values(&tree->components[idx], audit->records[idx].values);
}

static bool
uitree_geometry_audit_node(struct UITree const* tree, int32_t idx, char const* where)
{
    struct UITreeGeometryAudit const* audit = tree->geometry_audit;
    if( !audit || idx < 0 || (uint32_t)idx >= audit->capacity ) return true;
    struct UITreeComponent const* c = &tree->components[idx];
    struct UITreeGeometryRecord const* record = &audit->records[idx];
    /* A new incarnation is unsealed only until its constructor returns. */
    if( c->freed || record->incarnation != c->incarnation ) return true;
    int32_t current[GEOMETRY_FIELD_COUNT];
    uitree_geometry_values(c, current);
    for( size_t i = 0; i < GEOMETRY_FIELD_COUNT; ++i )
        if( current[i] != record->values[i] )
        {
            TORIRS_REPORT("MUTATION_AUDIT unclassified geometry com=%d node=%d field=%s old=%d new=%d observed=%s\n",
                c->component_id, idx, geometry_field_names[i], record->values[i], current[i], where);
            return false;
        }
    return true;
}

void
UITree_GeometryAuditEnable(struct UITree* tree)
{
    assert(tree);
    if( tree->geometry_audit ) return;
    tree->geometry_audit = calloc(1, sizeof(*tree->geometry_audit));
    if( !tree->geometry_audit ) abort();
    for( uint32_t i = 0; i < tree->component_count; ++i )
        if( !tree->components[i].freed ) uitree_geometry_audit_stamp(tree, (int32_t)i);
}

bool
UITree_GeometryAuditCheck(struct UITree const* tree, char const* where)
{
    if( !tree || !tree->geometry_audit ) return true;
    for( uint32_t i = 0; i < tree->component_count; ++i )
        if( !uitree_geometry_audit_node(tree, (int32_t)i, where) ) return false;
    return true;
}

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
    if( child->plugin_owner ) return -1;
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
    if( key < 0 ) return;

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
    if( key < 0 ) return;

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
    /* Retained native caches may outlive the tree they originally read.
     * A process-wide nonce prevents their index/incarnation pairs from ever
     * matching a component allocated in another tree. */
    static _Atomic uint64_t next_incarnation = 1;
    component->incarnation = atomic_fetch_add(&next_incarnation, 1);
    if( !component->incarnation ) abort();
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

bool
UITree_Reparent(
    struct UITree* tree,
    int32_t child_index,
    int32_t new_parent_index)
{
    if( !tree || child_index < 0 || (uint32_t)child_index >= tree->component_count ||
        tree->components[child_index].freed || new_parent_index < -1 )
        return false;
    if( new_parent_index >= 0 && (uint32_t)new_parent_index < tree->component_count &&
        tree->components[new_parent_index].plugin_owner &&
        tree->components[new_parent_index].plugin_owner != tree->components[child_index].plugin_owner )
        return false;
    /* Validate the entire proposed ancestor chain before changing any links. */
    int32_t ancestor = new_parent_index;
    uint32_t visited = 0;
    while( ancestor >= 0 )
    {
        if( (uint32_t)ancestor >= tree->component_count || ancestor == child_index ||
            tree->components[ancestor].freed || visited++ >= tree->component_count )
            return false;
        ancestor = tree->components[ancestor].parent;
    }

    struct UITreeComponent* child = &tree->components[child_index];
    int32_t old_parent = child->parent;
    if( old_parent == new_parent_index )
        return true;

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
    return true;
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
    static _Atomic uint64_t next_tree_instance = 1;
    tree->instance_id = atomic_fetch_add(&next_tree_instance, 1);
    if( !tree->instance_id ) abort(); /* Never recycle identity, even on overflow. */
    if( getenv("TORIRS_UI_MUTATION_AUDIT") ) UITree_GeometryAuditEnable(tree);
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

struct UITreeModelRenderCache*
UITree_ModelRenderCacheMut(struct UITreeComponent* component)
{
    assert(component && component->type == UIELEM_RS_MODEL);
    if( !component->model_render_cache )
    {
        component->model_render_cache = calloc(1, sizeof(*component->model_render_cache));
        if( !component->model_render_cache ) abort();
    }
    return component->model_render_cache;
}

/* Free a component's heap-owned resources and NULL the pointers so the slot is
 * safe to reuse and UITree_Free cannot double-free. */
static void
uitree_component_free_owned(struct UITree* tree, struct UITreeComponent* c)
{
    assert(tree);
    assert(c);
    free(c->plugin_key);
    c->plugin_key = NULL;
    while( c->widget_geometry )
    {
        struct UITreeWidgetGeometry* next = c->widget_geometry->next;
        if( c->widget_geometry->anchor_serial )
            tree->widget_anchor_edits--;
        free(c->widget_geometry);
        c->widget_geometry = next;
    }
    if( c->model_render_cache )
    {
        if( c->model_render_cache->release )
            c->model_render_cache->release(c->model_render_cache->data);
        free(c->model_render_cache);
        c->model_render_cache = NULL;
    }
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
    uitree_component_free_owned(tree, c);
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
        uitree_component_free_owned(tree, &tree->components[i]);
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
    if( tree->geometry_audit )
    {
        free(tree->geometry_audit->records);
        free(tree->geometry_audit);
    }
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
     * there is nothing left to restore, and the frame's provider is asked
     * again once the new tree is baked. */
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
    UITREE_IMPACT_GEOMETRY_STATE = 1u << 4,
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
    if( impacts & (UITREE_IMPACT_LAYOUT_SELF | UITREE_IMPACT_LAYOUT_TREE | UITREE_IMPACT_GEOMETRY_STATE) )
        uitree_geometry_audit_stamp(tree, idx);
}

static struct UITreeComponent*
uitree_component_at_mutable_checked(
    struct UITree* tree,
    int32_t idx,
    char const* where)
{
    assert(tree);
    if( idx < 0 || (uint32_t)idx >= tree->component_count || tree->components[idx].freed )
        return NULL;
    if( !uitree_geometry_audit_node(tree, idx, where) ) abort();
    return &tree->components[idx];
}

#define uitree_component_at_mutable(tree, idx) uitree_component_at_mutable_checked(tree, idx, __func__)

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
    component->plugin_owner = spec->plugin_owner;
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
        component->u.rs_model.active_anim_seq_id = spec->u.rs_model.active_anim_seq_id;
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
        component->item_id = spec->u.cc_obj.obj_id;
        component->item_count = spec->u.cc_obj.obj_count;
        component->item_scene_id = spec->u.cc_obj.scene_id;
        component->item_atlas_index = spec->u.cc_obj.atlas_index;
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
    uitree_geometry_audit_stamp(tree, idx);
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
            if( tree->components[child].plugin_owner ||
                (id >= 0 && ((id >> 16) & 0xFFFF) == TORIRS_REVCONFIG_GROUP) )
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
        if( c->plugin_owner ) continue;
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
        spec.u.rs_model.active_anim_seq_id = -1;
        spec.u.rs_model.zoom = 100;
        break;
    case 9: /* TORIRS_COMPONENT_LINE */
        spec.type = UIELEM_RS_LINE;
        break;
    case 12: /* TORIRS_COMPONENT_INPUT — the IF3 text-entry field */
        /* An editable field is a TYPE_TEXT that also takes the caret: the
         * scripts that build one set its font, colour and alignment with the
         * ordinary cc_settextfont / cc_setcolour / cc_settextalign and read it
         * back with cc_gettext, so anything else here would be a second text
         * node to teach the whole renderer about. The `input` flag is stamped
         * after the push, below. See `rs_text.input` in uitree.h. */
        spec.type = UIELEM_RS_TEXT;
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
    if( widget_type == 12 )
        tree->components[idx].u.rs_text.input = 1;
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
    spec.always_dirty = src.always_dirty;

    int32_t const idx = UITree_Push(tree, parent_index, &spec);
    if( idx < 0 )
        return -1;

    struct UITreeComponent* dst = &tree->components[idx];

    /* Push owns default lazy blocks for builtin arms. Drop those before
     * installing an independently owned payload; identity/topology stay new. */
    uitree_component_free_owned(tree, dst);
    dst->u = src.u;
    if( src.type == UIELEM_RS_TEXT )
    {
        /* Push allocated nothing for text (spec carried none); own fresh copies. */
        dst->u.rs_text.text = src.u.rs_text.text ? strdup(src.u.rs_text.text) : NULL;
        dst->u.rs_text.text_active =
            src.u.rs_text.text_active ? strdup(src.u.rs_text.text_active) : NULL;
    }

    /* Every pointer-bearing union arm must own its allocation. Native CC_COPY
     * normally copies IF3 payloads, but this internal operation also accepts
     * dynamic engine-created nodes. Never alias their inventory/chrome data. */
#define COPY_OWNED_ARM(arm) do { \
    dst->u.arm = src.u.arm ? malloc(sizeof(*src.u.arm)) : NULL; \
    if( src.u.arm ) { \
        if( !dst->u.arm ) abort(); \
        *dst->u.arm = *src.u.arm; \
    } \
} while( 0 )
    switch( src.type )
    {
    case UIELEM_RS_INV: COPY_OWNED_ARM(rs_inv.slots); break;
    case UIELEM_BUILTIN_CHAT: COPY_OWNED_ARM(chat); break;
    case UIELEM_BUILTIN_CHAT_BUTTON: COPY_OWNED_ARM(chat_button); break;
    case UIELEM_BUILTIN_DEBUG_OVERLAY: COPY_OWNED_ARM(debug_overlay); break;
    case UIELEM_BUILTIN_LOGIN_INPUT: COPY_OWNED_ARM(login_input); break;
    default: break;
    }
#undef COPY_OWNED_ARM

    /* Native state copies independently of the source's placement, claims,
     * focus, mount bookkeeping, derived indexes and active gesture. Those
     * remain the freshly allocated destination's values. */
    UITree_SetBehavior(tree, idx, &src.behavior);
    dst->native_hide = src.native_hide;
    dst->colour = src.colour;
    dst->fill_colour = src.fill_colour;
    dst->data_text = src.data_text ? strdup(src.data_text) : NULL;
    if( src.data_text && !dst->data_text ) abort();
    dst->cs1_active = src.cs1_active;
    memcpy(dst->cs1_values, src.cs1_values, sizeof(dst->cs1_values));
    dst->scroll_x = src.scroll_x;
    dst->scroll_y = src.scroll_y;
    dst->trans_bot = src.trans_bot;
    dst->hotkey_effects = src.hotkey_effects;
    dst->drag_render_area_uid = src.drag_render_area_uid;
    dst->drag_render_area_child_index = src.drag_render_area_child_index;
    dst->item_num_mode = src.item_num_mode;
    for( int i = 0; i < src.params_count; ++i )
    {
        struct UITreeComponentParam const* param = &src.params[i];
        if( !UITree_ApplyComponentParam(tree, dst->component_id, param->id, param->value,
                                       param->str) )
            abort();
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

    dst->is_dirty = 1;
    uitree_topo_bump(tree, __LINE__);
    tree->generation++;
    uitree_geometry_audit_stamp(tree, idx);
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
UITree_SetMountHiddenAt(struct UITree* tree, int32_t idx, int hidden)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c ) return false;
    hidden = hidden ? 1 : 0;
    if( c->mount_hidden == hidden ) return true;
    c->mount_hidden = (uint8_t)hidden;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF | UITREE_IMPACT_REACHABILITY |
                                  UITREE_IMPACT_LAYOUT_TREE);
    return true;
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
    if( c->behavior.hide == (uint8_t)hide && c->native_hide == (uint8_t)hide )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    c->behavior.hide = (uint8_t)hide;
    c->native_hide = (uint8_t)hide;
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
UITree_SetFrameStretchedAt(
    struct UITree* tree,
    int32_t idx,
    int stretched)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c )
        return false;
    stretched = stretched ? 1 : 0;
    if( c->frame_stretched == (uint8_t)stretched )
        return true;
    c->frame_stretched = (uint8_t)stretched;
    /* The clip of every descendant changes, which the retained emit list
     * cannot patch in place: treat it as the reachability change it is. */
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

/* ------------------------------------------------------------------ */
/* IF3 text-entry fields (component type 12)                           */
/* ------------------------------------------------------------------ */

int
UITree_IsInputNode(struct UITreeComponent const* c)
{
    assert(c);
    return c->type == UIELEM_RS_TEXT && c->u.rs_text.input;
}

struct UITreeNodeRef
UITree_RefAt(struct UITree const* tree, int32_t index)
{
    if( !tree || index < 0 || (uint32_t)index >= tree->component_count ||
        tree->components[index].freed )
        return (struct UITreeNodeRef){0};
    return (struct UITreeNodeRef){tree->instance_id, tree->components[index].incarnation, index};
}

int32_t
UITree_ResolveRef(struct UITree const* tree, struct UITreeNodeRef ref)
{
    if( !tree || !ref.tree_instance || ref.tree_instance != tree->instance_id ||
        !ref.incarnation || ref.index < 0 || (uint32_t)ref.index >= tree->component_count )
        return -1;
    struct UITreeComponent const* c = &tree->components[ref.index];
    return !c->freed && c->incarnation == ref.incarnation ? ref.index : -1;
}

static uint64_t
action_hash_int(uint64_t hash, uint64_t value)
{
    for( int i = 0; i < 8; ++i )
    {
        hash = (hash ^ (uint8_t)value) * UINT64_C(1099511628211);
        value >>= 8;
    }
    return hash;
}

static uint64_t
action_hash_text(uint64_t hash, char const* text)
{
    if( text )
        for( unsigned char const* p = (unsigned char const*)text; *p; ++p )
            hash = (hash ^ *p) * UINT64_C(1099511628211);
    return hash * UINT64_C(1099511628211); /* delimiter */
}

static uint64_t
action_hash_hook(uint64_t hash, struct UITreeRuntimeScriptHook const* hook)
{
    hash = action_hash_int(hash, hook->script_id > 0 ? hook->script_id : 0);
    if( hook->script_id <= 0 ) return hash;
    hash = action_hash_int(hash, hook->argc);
    hash = action_hash_int(hash, hook->str_argc);
    hash = action_hash_int(hash, hook->str_mask);
    for( int i = 0; i < hook->argc; ++i )
        if( !(hook->str_mask & (UINT64_C(1) << i)) )
            hash = action_hash_int(hash, UITree_HookArg(hook, i));
    for( int i = 0; i < hook->str_argc; ++i )
        hash = action_hash_text(hash, UITree_HookStr(hook, i));
    return hash;
}

uint64_t
UITree_ActionSignatureAt(struct UITree const* tree, int32_t idx)
{
    struct UITreeNodeRef ref = UITree_RefAt(tree, idx);
    if( !ref.incarnation ) return 0;
    struct UITreeComponent const* c = &tree->components[idx];
    struct UITreeMenuOptions const* options = UITree_MenuOptions(c);
    struct UITreeRuntimeHooks const* hooks = UITree_Hooks(c);
    uint64_t hash = UINT64_C(14695981039346656037);
#define ACTION_FIELD(field) hash = action_hash_int(hash, (uint64_t)c->field)
    ACTION_FIELD(incarnation); ACTION_FIELD(type); ACTION_FIELD(if3);
    ACTION_FIELD(behavior.button_type); ACTION_FIELD(behavior.client_code);
    ACTION_FIELD(behavior.click_mask); ACTION_FIELD(behavior.target_mask);
    ACTION_FIELD(target_priority); ACTION_FIELD(force_left_click);
    ACTION_FIELD(item_id); ACTION_FIELD(item_count); ACTION_FIELD(plugin_op_serial);
#undef ACTION_FIELD
    hash = action_hash_text(hash, c->data_text);
    /* Native social rows and server-armed continue prompts derive their
     * action label/target from the text, even without an explicit opbase. */
    if( c->type == UIELEM_RS_TEXT )
        hash = action_hash_text(hash, c->u.rs_text.text);
    hash = action_hash_text(hash, options->option);
    hash = action_hash_text(hash, options->target_verb);
    hash = action_hash_text(hash, options->target_base);
    hash = action_hash_int(hash, options->option_action);
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; ++i )
    {
        hash = action_hash_text(hash, options->ops[i]);
        hash = action_hash_int(hash, options->op_actions[i]);
    }
    if( options->submenus )
        for( int op = 1; op <= UITREE_SUBMENU_OP_SLOTS; ++op )
            for( int entry = 1; entry <= UITREE_SUBMENU_ENTRY_SLOTS; ++entry )
            {
                char const* text = UITree_MenuSubmenuEntry(options, op, entry);
                if( !text[0] ) continue;
                hash = action_hash_int(hash, (uint64_t)(op * 256 + entry));
                hash = action_hash_text(hash, text);
            }
    hash = action_hash_int(hash, 0);
    hash = action_hash_hook(hash, &hooks->on_op);
    hash = action_hash_hook(hash, &hooks->on_click);
    hash = action_hash_hook(hash, &hooks->on_target_enter);
    hash = action_hash_hook(hash, &hooks->on_target_leave);
    hash = action_hash_int(hash, c->params_count);
    for( int i = 0; i < c->params_count; ++i )
    {
        hash = action_hash_int(hash, c->params[i].id);
        hash = action_hash_int(hash, c->params[i].str != NULL);
        if( c->params[i].str ) hash = action_hash_text(hash, c->params[i].str);
        else hash = action_hash_int(hash, c->params[i].value);
    }
    if( c->type == UIELEM_RS_INV )
    {
        hash = action_hash_int(hash, c->u.rs_inv.inv_source_id);
        hash = action_hash_int(hash, c->u.rs_inv.obj_ops);
        hash = action_hash_int(hash, c->u.rs_inv.obj_use);
    }
    else if( c->type == UIELEM_RS_INV_TEXT ) hash = action_hash_int(hash, c->u.rs_inv_text.inv_source_id);
    else if( c->type == UIELEM_BUILTIN_SIDEBAR ) hash = action_hash_int(hash, c->u.sidebar.inv_source_id);
    return hash ? hash : 1;
}

void
UITree_StampMenuPick(struct UITree const* tree, int32_t idx, struct UIMinimenuPick* pick)
{
    struct UITreeNodeRef ref = UITree_RefAt(tree, idx);
    if( !ref.incarnation ) return;
    pick->has_node_identity = 1;
    pick->node_index = idx;
    pick->node_incarnation = ref.incarnation;
    pick->action_signature = UITree_ActionSignatureAt(tree, idx);
}

bool
UITree_MenuPickCurrent(struct UITree const* tree, struct UIMinimenuPick const* pick)
{
    if( !pick->has_node_identity ) return true;
    return tree && pick->node_index >= 0 && (uint32_t)pick->node_index < tree->component_count &&
        !tree->components[pick->node_index].freed &&
        tree->components[pick->node_index].incarnation == pick->node_incarnation &&
        (!pick->action_signature || pick->action_signature == UITree_ActionSignatureAt(tree, pick->node_index));
}

static struct UITreeWidgetGeometry*
uitree_widget_geometry(struct UITreeComponent* c, uint64_t owner)
{
    int count = 0;
    for( struct UITreeWidgetGeometry* edit = c->widget_geometry; edit; edit = edit->next )
    {
        if( edit->owner == owner ) return edit;
        ++count;
    }
    if( count >= 32 ) return NULL;
    struct UITreeWidgetGeometry* edit = calloc(1, sizeof(*edit));
    if( !edit ) return NULL;
    edit->owner = owner;
    edit->next = c->widget_geometry;
    c->widget_geometry = edit;
    return edit;
}

static bool
uitree_widget_set_geometry(struct UITree* tree, struct UITreeNodeRef ref,
                          uint64_t owner, int a, int b, bool size)
{
    int32_t idx = UITree_ResolveRef(tree, ref);
    if( idx < 0 || !owner || (size && (a < 0 || b < 0)) || widget_geometry_serial == UINT64_MAX )
        return false;
    struct UITreeComponent* c = &tree->components[idx];
    if( c->plugin_owner )
    {
        if( c->plugin_owner != owner ) return false;
        return size ? UITree_SetSizeAt(tree,idx,a,b) : UITree_SetPositionAt(tree,idx,a,b);
    }
    struct UITreeWidgetGeometry* edit = uitree_widget_geometry(c, owner);
    if( !edit ) return false;
    if( size )
    {
        edit->width = a; edit->height = b;
        edit->size_serial = ++widget_geometry_serial;
    }
    else
    {
        edit->x = a; edit->y = b;
        edit->position_serial = ++widget_geometry_serial;
    }
    uitree_note_mutation(tree, idx, UITREE_IMPACT_LAYOUT_SELF | UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool UITree_WidgetSetPosition(struct UITree* t, struct UITreeNodeRef r, uint64_t owner, int x, int y)
{ return uitree_widget_set_geometry(t, r, owner, x, y, false); }
bool UITree_WidgetSetSize(struct UITree* t, struct UITreeNodeRef r, uint64_t owner, int w, int h)
{ return uitree_widget_set_geometry(t, r, owner, w, h, true); }

static void uitree_widget_refresh_hidden(struct UITree* tree,int32_t idx)
{
    struct UITreeComponent* c=&tree->components[idx];
    struct UITreeWidgetGeometry const* last=NULL;
    for( struct UITreeWidgetGeometry const* e=c->widget_geometry;e;e=e->next )
        if( e->hidden_serial && (!last || e->hidden_serial>last->hidden_serial) ) last=e;
    bool hidden=last && last->hidden;
    last=NULL;
    for( struct UITreeWidgetGeometry const* e=c->widget_geometry;e;e=e->next )
        if( e->outline_serial && (!last || e->outline_serial>last->outline_serial) ) last=e;
    bool outline=last && last->outline;
    if( c->widget_hidden==hidden && c->widget_text_outline==outline ) return;
    bool visibility_changed=c->widget_hidden!=hidden;
    c->widget_hidden=hidden;
    c->widget_text_outline=outline;
    uitree_note_mutation(tree,idx,UITREE_IMPACT_EMIT_SELF|(visibility_changed ? UITREE_IMPACT_REACHABILITY : 0));
}

bool UITree_WidgetSetHidden(struct UITree* tree,struct UITreeNodeRef ref,uint64_t owner,bool hidden)
{
    int32_t idx=UITree_ResolveRef(tree,ref);
    if( idx<0 || !owner || widget_geometry_serial==UINT64_MAX ) return false;
    struct UITreeComponent* c=&tree->components[idx];
    if( c->plugin_owner && c->plugin_owner!=owner ) return false;
    struct UITreeWidgetGeometry* edit=uitree_widget_geometry(c,owner);
    if( !edit ) return false;
    edit->hidden=hidden;
    edit->hidden_serial=++widget_geometry_serial;
    uitree_widget_refresh_hidden(tree,idx);
    return true;
}

enum UITreeWidgetRelation UITree_WidgetAnchorAt(struct UITree const* tree, int32_t idx, int32_t* out_target)
{
    assert(tree);
    assert(out_target);
    *out_target = -1;
    if( idx < 0 || (uint32_t)idx >= tree->component_count || tree->components[idx].freed )
        return UITREE_WIDGET_RELATION_NATIVE;
    struct UITreeWidgetGeometry const* last = NULL;
    for( struct UITreeWidgetGeometry const* e = tree->components[idx].widget_geometry; e; e = e->next )
        if( e->anchor_serial && (!last || e->anchor_serial > last->anchor_serial) ) last = e;
    if( !last || last->anchor_relation == UITREE_WIDGET_RELATION_NATIVE )
        return UITREE_WIDGET_RELATION_NATIVE;
    /* A target that died leaves the edit standing and binding nothing; a fresh
     * incarnation at the same index is a different widget and never inherits it. */
    int32_t target = UITree_ResolveRef(tree, last->anchor_target);
    if( target < 0 )
        return UITREE_WIDGET_RELATION_NATIVE;
    *out_target = target;
    return (enum UITreeWidgetRelation)last->anchor_relation;
}

int UITree_WidgetAnchorCount(struct UITree const* tree)
{
    assert(tree);
    return tree->widget_anchor_edits;
}

enum UITreeWidgetAnchorResult
UITree_WidgetSetAnchor(struct UITree* tree, struct UITreeNodeRef ref, uint64_t owner,
                       struct UITreeNodeRef target, enum UITreeWidgetRelation relation)
{
    assert(tree);
    assert(owner);
    int32_t idx = UITree_ResolveRef(tree, ref);
    if( idx < 0 || widget_geometry_serial == UINT64_MAX )
        return UITREE_WIDGET_ANCHOR_STALE;
    struct UITreeComponent* c = &tree->components[idx];
    if( c->plugin_owner && c->plugin_owner != owner )
        return UITREE_WIDGET_ANCHOR_BLOCKED;
    if( relation < UITREE_WIDGET_RELATION_NATIVE || relation > UITREE_WIDGET_RELATION_REPLACE )
        return UITREE_WIDGET_ANCHOR_INVALID;
    int32_t t = -1;
    if( relation != UITREE_WIDGET_RELATION_NATIVE )
    {
        t = UITree_ResolveRef(tree, target);
        if( t < 0 )
            return UITREE_WIDGET_ANCHOR_STALE;
        if( t == idx )
            return UITREE_WIDGET_ANCHOR_INVALID;
        /* An ordering unit is a subtree, and units must be disjoint: neither
         * side may contain the other. */
        for( int32_t p = tree->components[t].parent; p >= 0; p = tree->components[p].parent )
            if( p == idx ) return UITREE_WIDGET_ANCHOR_INVALID;
        for( int32_t p = c->parent; p >= 0; p = tree->components[p].parent )
            if( p == t ) return UITREE_WIDGET_ANCHOR_INVALID;
        /* No cycle through the effective anchors the target already has. */
        int32_t at = t;
        for( uint32_t guard = 0; guard < tree->component_count; ++guard )
        {
            int32_t next;
            if( UITree_WidgetAnchorAt(tree, at, &next) == UITREE_WIDGET_RELATION_NATIVE ) break;
            if( next == idx ) return UITREE_WIDGET_ANCHOR_INVALID;
            at = next;
        }
    }
    struct UITreeWidgetGeometry* edit = uitree_widget_geometry(c, owner);
    if( !edit )
        return UITREE_WIDGET_ANCHOR_BUDGET;
    if( !edit->anchor_serial )
        tree->widget_anchor_edits++;
    edit->anchor_serial = ++widget_geometry_serial;
    edit->anchor_relation = relation;
    edit->anchor_target = t >= 0 ? UITree_RefAt(tree, t) : (struct UITreeNodeRef){0};
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF | UITREE_IMPACT_REACHABILITY);
    return UITREE_WIDGET_ANCHOR_OK;
}

static bool uitree_widget_art_type(enum UITreeComponentType type)
{ return type == UIELEM_BUILTIN_SPRITE || type == UIELEM_RS_GRAPHIC || type == UIELEM_BUILTIN_COMPASS; }
static bool uitree_widget_mask_type(enum UITreeComponentType type)
{ return type == UIELEM_BUILTIN_MINIMAP || type == UIELEM_BUILTIN_COMPASS || type == UIELEM_BUILTIN_SPRITE; }

static bool uitree_widget_set_skin(struct UITree* tree, struct UITreeNodeRef ref, uint64_t owner, int scene_id, bool mask)
{
    assert(tree);
    assert(owner);
    int32_t idx = UITree_ResolveRef(tree, ref);
    if( idx < 0 || widget_geometry_serial == UINT64_MAX ) return false;
    struct UITreeComponent* c = &tree->components[idx];
    /* Owned controls carry their picture directly (UITree_WidgetSetGraphic). */
    if( c->plugin_owner ) return false;
    if( mask ? (!uitree_widget_mask_type(c->type) || scene_id < 0)
             : (!uitree_widget_art_type(c->type) || scene_id <= 0) ) return false;
    struct UITreeWidgetGeometry* edit = uitree_widget_geometry(c, owner);
    if( !edit ) return false;
    if( mask ) { edit->mask_scene_id = scene_id; edit->mask_serial = ++widget_geometry_serial; }
    else { edit->art_scene_id = scene_id; edit->art_serial = ++widget_geometry_serial; }
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool UITree_WidgetSetArt(struct UITree* tree, struct UITreeNodeRef ref, uint64_t owner, int scene_id)
{ return uitree_widget_set_skin(tree, ref, owner, scene_id, false); }
bool UITree_WidgetSetMask(struct UITree* tree, struct UITreeNodeRef ref, uint64_t owner, int scene_id)
{ return uitree_widget_set_skin(tree, ref, owner, scene_id, true); }

int UITree_WidgetSkinAt(struct UITree const* tree, int32_t idx, int* out_art_scene_id, int* out_mask_scene_id)
{
    assert(tree);
    assert(out_art_scene_id);
    assert(out_mask_scene_id);
    if( idx < 0 || (uint32_t)idx >= tree->component_count || tree->components[idx].freed ) return 0;
    struct UITreeWidgetGeometry const* art = NULL;
    struct UITreeWidgetGeometry const* mask = NULL;
    for( struct UITreeWidgetGeometry const* e = tree->components[idx].widget_geometry; e; e = e->next )
    {
        if( e->art_serial && (!art || e->art_serial > art->art_serial) ) art = e;
        if( e->mask_serial && (!mask || e->mask_serial > mask->mask_serial) ) mask = e;
    }
    if( art ) *out_art_scene_id = art->art_scene_id;
    if( mask ) *out_mask_scene_id = mask->mask_scene_id;
    return (art ? 1 : 0) | (mask ? 2 : 0);
}

int UITree_WidgetClearSkin(struct UITree* tree, int scene_id)
{
    assert(tree);
    int cleared = 0;
    if( scene_id <= 0 ) return 0;
    for( uint32_t i = 0; i < tree->component_count; ++i )
    {
        struct UITreeComponent* c = &tree->components[i];
        if( c->freed ) continue;
        for( struct UITreeWidgetGeometry* e = c->widget_geometry; e; e = e->next )
        {
            if( e->art_serial && e->art_scene_id == scene_id ) { e->art_serial = 0; e->art_scene_id = 0; ++cleared; }
            if( e->mask_serial && e->mask_scene_id == scene_id ) { e->mask_serial = 0; e->mask_scene_id = 0; ++cleared; }
        }
        if( cleared ) uitree_note_mutation(tree, (int32_t)i, UITREE_IMPACT_EMIT_SELF);
    }
    return cleared;
}

bool UITree_WidgetReset(struct UITree* tree, struct UITreeNodeRef ref, uint64_t owner)
{
    int32_t idx = UITree_ResolveRef(tree, ref);
    if( idx < 0 || !owner ) return false;
    struct UITreeWidgetGeometry** link = &tree->components[idx].widget_geometry;
    while( *link )
    {
        struct UITreeWidgetGeometry* edit = *link;
        if( edit->owner == owner )
        {
            bool const anchored = edit->anchor_serial != 0;
            *link = edit->next;
            if( anchored )
                tree->widget_anchor_edits--;
            free(edit);
            uitree_widget_refresh_hidden(tree,idx);
            uitree_note_mutation(tree, idx, UITREE_IMPACT_LAYOUT_SELF | UITREE_IMPACT_EMIT_SELF |
                                            (anchored ? UITREE_IMPACT_REACHABILITY : 0));
            break;
        }
        link = &edit->next;
    }
    return true;
}

bool UITree_WidgetSetTextOutline(struct UITree* tree,struct UITreeNodeRef ref,uint64_t owner,bool outline)
{
    int32_t idx=UITree_ResolveRef(tree,ref);
    if( idx<0 || !owner || widget_geometry_serial==UINT64_MAX ) return false;
    struct UITreeComponent* c=&tree->components[idx];
    if( c->type!=UIELEM_RS_TEXT || (c->plugin_owner && c->plugin_owner!=owner) ) return false;
    struct UITreeWidgetGeometry* edit=uitree_widget_geometry(c,owner);
    if( !edit ) return false;
    edit->outline=outline;edit->outline_serial=++widget_geometry_serial;
    uitree_widget_refresh_hidden(tree,idx);
    return true;
}

bool UITree_WidgetSetProjectionHeight(struct UITree* tree,struct UITreeNodeRef ref,uint64_t owner,int height)
{
    int32_t idx=UITree_ResolveRef(tree,ref);
    if( idx<0 || !owner || height<0 || height>32767 || widget_geometry_serial==UINT64_MAX ) return false;
    struct UITreeComponent* c=&tree->components[idx];
    if( tree->entity_overlay_index<0 || c->type!=UIELEM_RS_LAYER || c->parent!=tree->entity_overlay_index ||
        (c->plugin_owner && c->plugin_owner!=owner) ) return false;
    struct UITreeWidgetGeometry* edit=uitree_widget_geometry(c,owner);
    if( !edit ) return false;
    edit->projection_height=height;edit->projection_serial=++widget_geometry_serial;
    uitree_note_mutation(tree,idx,UITREE_IMPACT_EMIT_SELF|UITREE_IMPACT_REACHABILITY);
    return true;
}

int UITree_WidgetProjectionHeight(struct UITree const* tree,int32_t idx)
{
    if( !tree || idx<0 || (uint32_t)idx>=tree->component_count ) return 0;
    struct UITreeWidgetGeometry const* last=NULL;
    for( struct UITreeWidgetGeometry const* e=tree->components[idx].widget_geometry;e;e=e->next )
        if( e->projection_serial && (!last || e->projection_serial>last->projection_serial) ) last=e;
    return last ? last->projection_height : 0;
}

int32_t UITree_WidgetCreateText(struct UITree* tree, struct UITreeNodeRef parent, uint64_t owner,
                               char const* key, int font_id)
{
    int32_t p = UITree_ResolveRef(tree,parent);
    if( p < 0 || !owner || !key || !*key || strlen(key) > 63 ||
        (tree->components[p].plugin_owner && tree->components[p].plugin_owner != owner) ) return -1;
    for( int32_t child=tree->components[p].first_child; child>=0; child=tree->components[child].next_sibling )
        if( tree->components[child].plugin_owner == owner && tree->components[child].plugin_key &&
            strcmp(tree->components[child].plugin_key,key)==0 ) return child;
    int count=0;
    for( uint32_t i=0; i<tree->component_count; ++i )
        if( !tree->components[i].freed && tree->components[i].plugin_owner==owner ) ++count;
    if( count>=128 ) return -1;
    char* saved_key=strdup(key);
    if( !saved_key ) return -1;
    struct UITreeNodeSpec spec={0};
    spec.type=UIELEM_RS_TEXT; spec.component_id=-1; spec.plugin_owner=owner;
    spec.width=180; spec.height=16;
    spec.u.rs_text.font_id=font_id;
    spec.u.rs_text.color=0xffffff;
    spec.u.rs_text.shadowed=1;
    int32_t index=UITree_Push(tree,p,&spec);
    if( index<0 ) { free(saved_key); return -1; }
    tree->components[index].plugin_key=saved_key;
    return index;
}

/* The owned-child creation shared by text and graphic controls: owner/key
 * scoped, idempotent per parent, capped at 128 owned nodes per owner. */
static int32_t uitree_widget_create_owned(struct UITree* tree, struct UITreeNodeRef parent, uint64_t owner,
                                          char const* key, struct UITreeNodeSpec* spec)
{
    int32_t p = UITree_ResolveRef(tree,parent);
    if( p < 0 || !owner || !key || !*key || strlen(key) > 63 ||
        (tree->components[p].plugin_owner && tree->components[p].plugin_owner != owner) ) return -1;
    for( int32_t child=tree->components[p].first_child; child>=0; child=tree->components[child].next_sibling )
        if( tree->components[child].plugin_owner == owner && tree->components[child].plugin_key &&
            strcmp(tree->components[child].plugin_key,key)==0 ) return child;
    int count=0;
    for( uint32_t i=0; i<tree->component_count; ++i )
        if( !tree->components[i].freed && tree->components[i].plugin_owner==owner ) ++count;
    if( count>=128 ) return -1;
    char* saved_key=strdup(key);
    assert(saved_key);
    spec->component_id=-1; spec->plugin_owner=owner;
    int32_t index=UITree_Push(tree,p,spec);
    if( index<0 ) { free(saved_key); return -1; }
    tree->components[index].plugin_key=saved_key;
    return index;
}

int32_t UITree_WidgetCreateGraphic(struct UITree* tree, struct UITreeNodeRef parent, uint64_t owner, char const* key)
{
    struct UITreeNodeSpec spec={0};
    spec.type=UIELEM_RS_GRAPHIC;
    return uitree_widget_create_owned(tree,parent,owner,key,&spec);
}

bool UITree_WidgetSetGraphic(struct UITree* tree,struct UITreeNodeRef ref,uint64_t owner,
                             int scene_id,int width,int height)
{
    int32_t idx=UITree_ResolveRef(tree,ref);
    if( idx<0 || !owner || scene_id<0 || width<0 || height<0 || width>4096 || height>4096 ) return false;
    struct UITreeComponent* c=&tree->components[idx];
    if( c->plugin_owner!=owner || c->type!=UIELEM_RS_GRAPHIC ) return false;
    UITree_SetGraphicAt(tree,idx,scene_id,0);
    UITree_SetSizeModesAt(tree,idx,width,height,0,0);
    return true;
}

int UITree_WidgetClearGraphic(struct UITree* tree, int scene_id)
{
    assert(tree);
    int cleared=0;
    if( scene_id<=0 ) return 0;
    for( uint32_t i=0; i<tree->component_count; ++i )
    {
        struct UITreeComponent* c=&tree->components[i];
        if( c->freed || !c->plugin_owner || c->type!=UIELEM_RS_GRAPHIC || c->u.rs_graphic.scene_id!=scene_id ) continue;
        UITree_SetGraphicAt(tree,(int32_t)i,0,0);
        ++cleared;
    }
    return cleared;
}

bool UITree_WidgetSetTransparency(struct UITree* tree,struct UITreeNodeRef ref,uint64_t owner,int transparency)
{
    int32_t idx=UITree_ResolveRef(tree,ref);
    if( idx<0 || !owner || transparency<0 || transparency>255 ) return false;
    struct UITreeComponent* c=&tree->components[idx];
    if( c->plugin_owner!=owner ) return false;
    return UITree_SetTransparencyAt(tree,idx,transparency);
}

bool UITree_WidgetSetOperation(struct UITree* tree,struct UITreeNodeRef ref,uint64_t owner,
                                uint64_t serial,char const* label)
{
    int32_t idx=UITree_ResolveRef(tree,ref);
    if( idx<0 || !owner || tree->components[idx].plugin_owner!=owner ||
        !label || strlen(label)>=UITREE_MENU_OPTION_LEN || (serial && !*label) ) return false;
    struct UITreeComponent* c=&tree->components[idx];
    struct UITreeMenuOptions* options=UITree_MenuOptionsMut(c);
    if( !options ) return false;
    snprintf(options->option,sizeof(options->option),"%s",serial ? label : "");
    c->plugin_op_serial=serial;
    uitree_note_mutation(tree,idx,UITREE_IMPACT_EMIT_SELF|UITREE_IMPACT_REACHABILITY);
    return true;
}

bool UITree_WidgetRemove(struct UITree* tree, struct UITreeNodeRef ref, uint64_t owner)
{
    int32_t idx=UITree_ResolveRef(tree,ref);
    if( idx<0 || !owner || tree->components[idx].plugin_owner!=owner ) return false;
    if( tree->components[idx].parent>=0 ) UITree_UnlinkChild(tree,tree->components[idx].parent,idx);
    else UITree_UnlinkFromRootList(tree,idx);
    uitree_reclaim_subtree(tree,idx);
    tree->generation++;
    return true;
}

void UITree_WidgetResetOwner(struct UITree* tree, uint64_t owner)
{
    if( !tree || !owner ) return;
    for( uint32_t i = 0; i < tree->component_count; ++i )
    {
        if( tree->components[i].freed ) continue;
        if( tree->components[i].plugin_owner==owner )
            UITree_WidgetRemove(tree,UITree_RefAt(tree,(int32_t)i),owner);
        else if( tree->components[i].widget_geometry )
            UITree_WidgetReset(tree,UITree_RefAt(tree,(int32_t)i),owner);
    }
}

/* Bit 1: forced position, bit 2: forced dimensions, including an exact zero.
 * Modify a local layout specification; never overwrite current native inputs. */
int UITree_WidgetPositionOverride(struct UITree const* tree, int32_t idx, struct UITreeElemPosition* out)
{
    if( !tree || idx < 0 || (uint32_t)idx >= tree->component_count || !out ) return 0;
    struct UITreeWidgetGeometry const* position = NULL;
    struct UITreeWidgetGeometry const* size = NULL;
    for( struct UITreeWidgetGeometry const* edit = tree->components[idx].widget_geometry;
         edit; edit = edit->next )
    {
        if( edit->position_serial && (!position || edit->position_serial > position->position_serial) ) position = edit;
        if( edit->size_serial && (!size || edit->size_serial > size->size_serial) ) size = edit;
    }
    if( position )
    {
        out->x = position->x; out->y = position->y;
        out->x_mode = out->y_mode = 0;
        if( out->kind == UIPOS_RELATIVE )
        {
            out->relative_flags = UITREE_RELATIVE_FLAG_LEFT | UITREE_RELATIVE_FLAG_TOP;
            out->left = position->x; out->top = position->y;
        }
    }
    if( size )
    {
        out->width = size->width; out->height = size->height;
        out->width_mode = out->height_mode = 0;
    }
    return (position ? 1 : 0) | (size ? 2 : 0);
}

bool
UITree_StageDragPickup(struct UITree* tree, int32_t index, int x, int y)
{
    struct UITreeNodeRef ref = UITree_RefAt(tree, index);
    if( !ref.incarnation ) return false;
    tree->pending_drag_pickup_ref = ref;
    tree->pending_drag_pickup_x = x;
    tree->pending_drag_pickup_y = y;
    tree->pending_drag_pickup = 1;
    return true;
}

int
UITree_InputFocusId(struct UITree const* tree)
{
    assert(tree);
    int32_t idx = UITree_ResolveRef(tree, tree->input_focus);
    if( idx < 0 || !UITree_IsInputNode(&tree->components[idx]) )
        return -1;
    return tree->components[idx].component_id;
}

int
UITree_InputSetFocusId(
    struct UITree* tree,
    int com_id)
{
    int const lost = UITree_InputFocusId(tree);

    assert(tree);
    if( lost == com_id )
        return -1;
    if( lost >= 0 )
    {
        int32_t const lost_idx = UITree_FindByComponentId(tree, lost);
        if( lost_idx >= 0 )
            UITree_MarkNodeDirty(tree, lost_idx);
    }
    /* Also where a stale id is finally dropped -- UITree_InputFocusId only
     * refuses to believe one. */
    tree->input_focus = (struct UITreeNodeRef){0};
    if( com_id >= 0 )
    {
        int32_t const idx = UITree_FindByComponentId(tree, com_id);
        struct UITreeComponent* c;
        if( idx < 0 || tree->components[idx].freed ||
            !UITree_IsInputNode(&tree->components[idx]) )
            return lost;
        c = &tree->components[idx];
        tree->input_focus = UITree_RefAt(tree, idx);
        /* The caret lands at the END of what is already there. Clicking a box
         * that holds a half-typed name has to let you finish it, and the
         * reference puts the cursor at the tail for the same reason. */
        c->u.rs_text.caret = c->u.rs_text.text ? (int)strlen(c->u.rs_text.text) : 0;
        UITree_MarkNodeDirty(tree, idx);
    }
    return lost;
}

int
UITree_InputHitTest(
    struct UITree* tree,
    struct UITreeHost const* host,
    int px,
    int py)
{
    int32_t hits[UITREE_INPUT_HIT_STACK_MAX];
    int count;

    assert(tree);
    count = UITree_CollectNodesAt(tree, host, px, py, hits, UITREE_INPUT_HIT_STACK_MAX);
    /*
     * The field takes the click only when it is the TOP-MOST target, not merely
     * one of them. CollectNodesAt reports targets top-most first and has
     * already dropped everything decorative, so the test is the first entry.
     *
     * Not "the first input node in the stack", which was the obvious spelling
     * and is wrong: `settings_create_drop_down` builds a type-12 field for a
     * custom value and puts the row's own `Input` op on a DIFFERENT component
     * (`cc_setonop` after a `cc_find($component6, $int31)`). Taking the click
     * for any field found anywhere in the stack would give the caret to that
     * field and never run the op the row is built around.
     */
    if( count > 0 && UITree_IsInputNode(&tree->components[hits[0]]) )
        return tree->components[hits[0]].component_id;
    return -1;
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
    if( com->position.x == x && com->position.y == y &&
        com->position.layout_resolved )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    com->position.x = x;
    com->position.y = y;
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
    if( com->position.width == width && com->position.height == height &&
        com->position.layout_resolved )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    com->position.width = width;
    com->position.height = height;
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
    if( com->position.x == x && com->position.y == y && com->position.x_mode == (int8_t)x_mode &&
        com->position.y_mode == (int8_t)y_mode &&
        com->position.layout_resolved )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    com->position.x = x;
    com->position.y = y;
    com->position.x_mode = (int8_t)x_mode;
    com->position.y_mode = (int8_t)y_mode;
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
    if( com->position.width == width && com->position.height == height &&
        com->position.width_mode == (int8_t)width_mode &&
        com->position.height_mode == (int8_t)height_mode &&
        com->position.layout_resolved )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE, 1);
        return true;
    }
    com->position.width = width;
    com->position.height = height;
    com->position.width_mode = (int8_t)width_mode;
    com->position.height_mode = (int8_t)height_mode;
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
            UITREE_IMPACT_LAYOUT_TREE | UITREE_IMPACT_EMIT_SELF | UITREE_IMPACT_GEOMETRY_STATE);
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
        uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF | UITREE_IMPACT_GEOMETRY_STATE);
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
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF | UITREE_IMPACT_GEOMETRY_STATE);
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

bool
UITree_SetObjectAt(struct UITree* tree, int32_t idx, int obj_id, int obj_count,
                   int scene_id, int atlas_index, int num_mode)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c ) return false;
    if( obj_id <= 0 )
    {
        obj_id = obj_count = atlas_index = 0;
        scene_id = -1;
        num_mode = c->item_num_mode;
    }
    if( c->item_id == obj_id && c->item_count == obj_count && c->item_scene_id == scene_id &&
        c->item_atlas_index == atlas_index && c->item_num_mode == (uint8_t)num_mode )
        return true;
    c->item_id = obj_id;
    c->item_count = obj_count; /* -1 is the native icon-only count sentinel. */
    c->item_scene_id = scene_id;
    c->item_atlas_index = atlas_index;
    c->item_num_mode = (uint8_t)num_mode;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool
UITree_ApplyObject(struct UITree* tree, int component_id, int obj_id, int obj_count,
                   int scene_id, int atlas_index, int num_mode)
{
    return UITree_SetObjectAt(tree, UITree_ResolveComponentTarget(tree, component_id, -1),
                              obj_id, obj_count, scene_id, atlas_index, num_mode);
}

bool
UITree_SwapObjectStateAt(struct UITree* tree, int32_t a, int32_t b)
{
    struct UITreeComponent* first = uitree_component_at_mutable(tree, a);
    struct UITreeComponent* second = uitree_component_at_mutable(tree, b);
    if( !first || !second ) return false;
    int id = first->item_id, count = first->item_count;
    int scene = first->item_scene_id, atlas = first->item_atlas_index;
    (void)UITree_SetObjectAt(tree, a, second->item_id, second->item_count,
                            second->item_scene_id, second->item_atlas_index, first->item_num_mode);
    (void)UITree_SetObjectAt(tree, b, id, count, scene, atlas, second->item_num_mode);
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
UITree_SetModelAnimationAt(struct UITree* tree, int32_t idx, int sequence,
                           int frame, int cycle, int hold)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c || c->type != UIELEM_RS_MODEL ) return false;
    hold = hold != 0;
    if( c->u.rs_model.anim_seq_id == sequence && c->u.rs_model.anim_frame == frame &&
        c->u.rs_model.anim_frame_cycle == cycle && c->u.rs_model.anim_hold == hold )
        return true;
    bool visual = c->u.rs_model.anim_seq_id != sequence || c->u.rs_model.anim_frame != frame;
    c->u.rs_model.anim_seq_id = sequence;
    c->u.rs_model.anim_frame = frame;
    c->u.rs_model.anim_frame_cycle = cycle;
    c->u.rs_model.anim_hold = (uint8_t)hold;
    uitree_note_mutation(tree, idx, visual ? UITREE_IMPACT_EMIT_SELF : 0);
    return true;
}

bool
UITree_SetModelAnimationCursorAt(struct UITree* tree, int32_t idx, int frame, int cycle)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c || c->type != UIELEM_RS_MODEL ) return false;
    return UITree_SetModelAnimationAt(tree, idx, c->u.rs_model.anim_seq_id, frame, cycle,
                                      c->u.rs_model.anim_hold);
}

bool
UITree_ApplyModelAnim(struct UITree* tree, int component_id, int anim_seq_id)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_MODEL ) return false;
    struct UITreeComponent* c = &tree->components[idx];
    if( c->u.rs_model.anim_seq_id == anim_seq_id ) return true;
    return UITree_SetModelAnimationAt(tree, idx, anim_seq_id, 0, 0, c->u.rs_model.anim_hold);
}

bool
UITree_SetButtonTypeAt(struct UITree* tree, int32_t idx, int button_type)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c ) return false;
    if( c->behavior.button_type == button_type ) return true;
    c->behavior.button_type = button_type;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF | UITREE_IMPACT_REACHABILITY);
    return true;
}

bool
UITree_SetNativeIntAt(struct UITree* tree, int32_t idx, enum UITreeNativeIntField field, int value)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c ) return false;
    uint32_t impacts = UITREE_IMPACT_EMIT_SELF;
#define SET_NATIVE(member, val) do { \
    if( c->member == (val) ) return true; \
    c->member = (val); \
} while( 0 )
    switch( field )
    {
    case UITREE_NATIVE_IF3: SET_NATIVE(if3, (uint8_t)(value != 0)); impacts |= UITREE_IMPACT_REACHABILITY; break;
    case UITREE_NATIVE_HFLIP:
        if( c->type != UIELEM_RS_GRAPHIC ) return false;
        SET_NATIVE(u.rs_graphic.flip_h, (uint8_t)(value != 0)); break;
    case UITREE_NATIVE_VFLIP:
        if( c->type != UIELEM_RS_GRAPHIC ) return false;
        SET_NATIVE(u.rs_graphic.flip_v, (uint8_t)(value != 0)); break;
    case UITREE_NATIVE_LINE_WIDTH:
        if( c->type == UIELEM_RS_LINE ) { SET_NATIVE(u.rs_line.line_width, value); }
        else if( c->type == UIELEM_RS_ARC ) { SET_NATIVE(u.rs_arc.line_width, value > 0 ? value : 1); }
        else return false;
        break;
    case UITREE_NATIVE_LINE_DIRECTION:
        if( c->type != UIELEM_RS_LINE ) return false;
        SET_NATIVE(u.rs_line.horizontal, value != 0); break;
    case UITREE_NATIVE_NO_CLICK_THROUGH:
        SET_NATIVE(no_click_through, (uint8_t)(value != 0)); impacts |= UITREE_IMPACT_REACHABILITY; break;
    case UITREE_NATIVE_DRAG_DEAD_ZONE: SET_NATIVE(drag_dead_zone, (uint8_t)value); break;
    case UITREE_NATIVE_DRAG_DEAD_TIME: SET_NATIVE(drag_dead_time, (uint8_t)value); break;
    case UITREE_NATIVE_DRAG_BEHAVIOR: SET_NATIVE(drag_behavior, value); break;
    case UITREE_NATIVE_MODEL_ORTHOG:
        if( c->type != UIELEM_RS_MODEL ) return false;
        SET_NATIVE(u.rs_model.orthog, (uint8_t)(value != 0)); break;
    case UITREE_NATIVE_TRANS_BOTTOM: SET_NATIVE(trans_bot, value); break;
    case UITREE_NATIVE_INPUT_WRAP_WIDTH:
        if( c->type != UIELEM_RS_TEXT ) return false;
        SET_NATIVE(u.rs_text.input_wrap_width, value); break;
    case UITREE_NATIVE_FILL:
        if( c->type == UIELEM_RS_RECT ) { SET_NATIVE(u.rs_rect.filled, value != 0); }
        else if( c->type == UIELEM_RS_ARC ) { SET_NATIVE(u.rs_arc.filled, value != 0); }
        else return false;
        break;
    case UITREE_NATIVE_GRAPHIC_ACTIVE:
        if( c->type != UIELEM_RS_GRAPHIC ) return false;
        SET_NATIVE(u.rs_graphic.scene_id_active, value); break;
    default: return false;
    }
#undef SET_NATIVE
    uitree_note_mutation(tree, idx, impacts);
    return true;
}

bool
UITree_SetDragAreaAt(struct UITree* tree, int32_t idx, int enabled, int uid, int child)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c ) return false;
    enabled = enabled != 0;
    if( c->draggable == enabled && c->drag_render_area_uid == uid &&
        c->drag_render_area_child_index == child ) return true;
    c->draggable = (uint8_t)enabled;
    c->drag_render_area_uid = uid;
    c->drag_render_area_child_index = child;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF | UITREE_IMPACT_REACHABILITY);
    return true;
}

bool
UITree_SetInputCaretAt(struct UITree* tree, int32_t idx, int caret)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c || !UITree_IsInputNode(c) ) return false;
    int length = c->u.rs_text.text ? (int)strlen(c->u.rs_text.text) : 0;
    if( caret < 0 ) caret = 0;
    if( caret > length ) caret = length;
    if( c->u.rs_text.caret == caret ) return true;
    c->u.rs_text.caret = caret;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF);
    return true;
}

bool
UITree_SetInventorySourceAt(struct UITree* tree, int32_t idx, int source_id)
{
    struct UITreeComponent* c = uitree_component_at_mutable(tree, idx);
    if( !c ) return false;
    int* value = NULL;
    if( c->type == UIELEM_BUILTIN_SIDEBAR ) value = &c->u.sidebar.inv_source_id;
    else if( c->type == UIELEM_RS_INV ) value = &c->u.rs_inv.inv_source_id;
    else if( c->type == UIELEM_RS_INV_TEXT ) value = &c->u.rs_inv_text.inv_source_id;
    if( !value ) return false;
    if( *value == source_id ) return true;
    *value = source_id;
    uitree_note_mutation(tree, idx, UITREE_IMPACT_EMIT_SELF | UITREE_IMPACT_REACHABILITY);
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
UITree_SetTextAlignAt(
    struct UITree* tree,
    int32_t idx,
    int h_align,
    int v_align,
    int line_height)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_CONTENT, 1);
    if( !tree || idx < 0 || (uint32_t)idx >= tree->component_count || tree->components[idx].freed || tree->components[idx].type != UIELEM_RS_TEXT )
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

bool UITree_ApplyTextAlign(struct UITree* tree, int id, int h, int v, int height)
{ return UITree_SetTextAlignAt(tree,UITree_ResolveComponentTarget(tree,id,-1),h,v,height); }

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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_APPLY_HOOK, 1);
    assert(tree);
    int32_t idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 || !slot ) return false;
    struct UITreeRuntimeHooks* hooks = tree->components[idx].runtime_hooks;
    if( !hooks ) return false;
    uintptr_t offset = (uintptr_t)slot - (uintptr_t)hooks;
    if( offset >= sizeof(*hooks) || offset % sizeof(*slot) != 0 ||
        UITree_HooksSlotAt(hooks, (int)(offset / sizeof(*slot))) != slot )
        return false;

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
    uitree_sync_hook_sets(tree, idx);
    uitree_note_mutation(tree, idx, UITREE_IMPACT_REACHABILITY);
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
    if( pos->layout_resolved )
        return pos->abs_w > 0 ? pos->abs_w : 0;
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
    if( pos->layout_resolved )
        return pos->abs_h > 0 ? pos->abs_h : 0;
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
    if( component->native_hide || component->mount_hidden ) return false;
    if( !(component->behavior.hide || component->mount_hidden) )
        return true;
    /* IF1 uses hide for hover-gated tooltip layers. IF3/CS2 explicit hiding
     * is not a tooltip mechanism and must not be undone by last frame's hover. */
    if( component->if3 ) return false;
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
    assert(component && hover_ids);
    return UITree_ComponentVisibleById(component, hover_ids->main_com_id) ||
           UITree_ComponentVisibleById(component, hover_ids->side_com_id) ||
           UITree_ComponentVisibleById(component, hover_ids->chat_com_id);
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
            /* The Ex form can excuse the gameframe plugin's own hiding on the
             * whole walk for a caller that names a COMPONENT rather than a
             * place -- a synthesised press.
             * @see UITree_NodeOrAncestorDisplayHiddenEx. */
            if( tree->components[idx].behavior.hide || tree->components[idx].mount_hidden ||
                (include_plugin_hidden &&
                 ((tree->components[idx].frame_hidden && !ignore_frame_hidden) ||
                  tree->components[idx].screen_hidden ||
                  tree->components[idx].projection_hidden || tree->components[idx].widget_hidden)) )
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
        tree, UITree_FindByComponentId(tree, component_id), 0, 0);
}

int
UITree_ComponentOrAncestorDisplayHidden(
    struct UITree const* tree,
    int component_id)
{
    return uitree_node_or_ancestor_hidden(
        tree, UITree_FindByComponentId(tree, component_id), 1, 0);
}

int
UITree_NodeOrAncestorDisplayHidden(
    struct UITree const* tree,
    int32_t node_index)
{
    assert(tree);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return 1;
    return uitree_node_or_ancestor_hidden(tree, node_index, 1, 0);
}

int
UITree_NodeOrAncestorDisplayHiddenEx(
    struct UITree const* tree,
    int32_t node_index,
    int ignore_frame_hidden)
{
    assert(tree);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return 1;
    return uitree_node_or_ancestor_hidden(tree, node_index, 1, ignore_frame_hidden);
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
    if( c->behavior.hide || c->mount_hidden || c->frame_hidden || c->screen_hidden ||
        (c->projection_hidden || c->widget_hidden) )
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

    if( hit && UITree_ComponentIsDropTarget(c) &&
        depth >= *best_depth )
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
        if( tree->components[root].behavior.hide || tree->components[root].mount_hidden || tree->components[root].frame_hidden ||
            tree->components[root].screen_hidden ||
            (tree->components[root].projection_hidden || tree->components[root].widget_hidden) )
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
