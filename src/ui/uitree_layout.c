#include "uitree_layout.h"

#include "perf/torirs_perf.h"
#include "ui_if3_layout.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Fixed-mode client canvas — the historical default. */
int UITree_LayoutRootWidth = 765;
int UITree_LayoutRootHeight = 503;

void
UITree_LayoutSetRootSize(int width, int height)
{
    if( width > 0 )
        UITree_LayoutRootWidth = width;
    if( height > 0 )
        UITree_LayoutRootHeight = height;
}

void
UITree_LayoutInvalidate(struct UITree* tree)
{
    assert(tree);

    for( uint32_t i = 0; i < tree->component_count; i++ )
        tree->components[i].position.layout_resolved = 0;
    tree->layout_stale = 1;
    tree->layout_resolved_valid = 0;
}

void
UITree_LayoutInvalidateBoxes(struct UITree* tree)
{
    assert(tree);
    tree->layout_stale = 1;
    tree->layout_resolved_valid = 0;
}

void
UITree_EnsureLayout(struct UITree const* tree)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_ENSURE_LAYOUT, 1);
    /* Lazy JIT re-layout for CS2 getters (reference ensureLayout). The resolve
     * does not change logical state, so mutate through the const handle. */
    struct UITree* t = (struct UITree*)tree;
    if( !t || !t->layout_stale )
        return;
    UITree_LayoutResolve(t, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
}

static int
dim_from_parent_mode(
    int8_t mode,
    int orig,
    int parent_dim)
{
    return UITree_If3DimFromParentMode(mode, orig, parent_dim);
}

static int
axis_from_position_mode(
    int8_t mode,
    int base,
    int parent_origin,
    int parent_dim,
    int self_dim)
{
    return UITree_If3AxisFromPositionMode(mode, base, parent_origin, parent_dim, self_dim);
}

static void
resolve_relative(
    struct UITreeElemPosition* pos,
    int parent_x,
    int parent_y,
    int parent_w,
    int parent_h)
{
    int x = parent_x;
    int y = parent_y;
    int w = pos->width > 0 ? pos->width : parent_w;
    int h = pos->height > 0 ? pos->height : parent_h;

    if( pos->relative_flags & UITREE_RELATIVE_FLAG_LEFT )
        x = parent_x + pos->left;
    else if( pos->relative_flags & UITREE_RELATIVE_FLAG_RIGHT )
        x = parent_x + parent_w - pos->right - w;

    if( pos->relative_flags & UITREE_RELATIVE_FLAG_TOP )
        y = parent_y + pos->top;
    else if( pos->relative_flags & UITREE_RELATIVE_FLAG_BOTTOM )
        y = parent_y + parent_h - pos->bottom - h;

    if( !(pos->relative_flags & (UITREE_RELATIVE_FLAG_LEFT | UITREE_RELATIVE_FLAG_RIGHT)) )
        x = parent_x + (parent_w - w) / 2;
    if( !(pos->relative_flags & (UITREE_RELATIVE_FLAG_TOP | UITREE_RELATIVE_FLAG_BOTTOM)) )
        y = parent_y + (parent_h - h) / 2;

    pos->abs_x = x;
    pos->abs_y = y;
    pos->abs_w = w;
    pos->abs_h = h;
    pos->layout_resolved = 1;
}

/*
 * Parent box a child lays out against: the parent's resolved box, with an
 * RS_LAYER's scroll extent standing in for its visible size. Falls back to the
 * root box when there is no parent.
 */
static void
layout_parent_box(
    struct UITree const* tree,
    int32_t parent,
    int root_x,
    int root_y,
    int root_w,
    int root_h,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    struct UITreeComponent const* p;

    *out_x = root_x;
    *out_y = root_y;
    *out_w = root_w;
    *out_h = root_h;

    if( parent < 0 || (uint32_t)parent >= tree->component_count )
        return;

    p = &tree->components[parent];
    *out_x = p->position.abs_x;
    *out_y = p->position.abs_y;
    *out_w = p->position.abs_w;
    *out_h = p->position.abs_h;
    if( p->type == UIELEM_RS_LAYER )
    {
        if( p->u.rs_layer.scroll_width > 0 )
            *out_w = p->u.rs_layer.scroll_width;
        if( p->u.rs_layer.scroll_height > 0 )
            *out_h = p->u.rs_layer.scroll_height;
    }
}

/*
 * Resolve one node against an already-resolved parent box.
 *
 * A node's box is a pure function of its own fields and its parent's box —
 * never its siblings or children. That is the property UITree_EnsureLayoutFor
 * relies on to resolve a single root->node chain instead of the whole tree.
 */
static bool
layout_compute_node(
    struct UITree* tree,
    uint32_t i,
    int px,
    int py,
    int pw,
    int ph)
{
    struct UITreeComponent* c = &tree->components[i];
    struct UITreeElemPosition* pos = &c->position;
    int const was_resolved = pos->layout_resolved;
    int const old_x = pos->abs_x;
    int const old_y = pos->abs_y;
    int const old_w = pos->abs_w;
    int const old_h = pos->abs_h;

    if( pos->kind == UIPOS_RELATIVE )
    {
        resolve_relative(pos, px, py, pw, ph);
        return !was_resolved || pos->abs_x != old_x || pos->abs_y != old_y ||
               pos->abs_w != old_w || pos->abs_h != old_h;
    }

    int w = pos->width;
    int h = pos->height;
    if( pos->width_mode >= 0 || pos->height_mode >= 0 )
    {
        int8_t wm = pos->width_mode >= 0 ? pos->width_mode : 0;
        int8_t hm = pos->height_mode >= 0 ? pos->height_mode : 0;
        if( wm == 4 || hm == 4 )
        {
            UITree_If3ComputeSize(
                wm,
                hm,
                pos->width,
                pos->height,
                pw,
                ph,
                pos->aspect_w > 0 ? pos->aspect_w : 1,
                pos->aspect_h > 0 ? pos->aspect_h : 1,
                &w,
                &h);
        }
        else
        {
            w = dim_from_parent_mode(wm, pos->width, pw);
            h = dim_from_parent_mode(hm, pos->height, ph);
        }
    }

    if( c->parent < 0 && w == 0 && h == 0 )
    {
        w = pw;
        h = ph;
    }

    int rx = pos->x;
    int ry = pos->y;
    if( pos->x_mode >= 0 || pos->y_mode >= 0 )
    {
        int8_t ym = pos->y_mode >= 0 ? pos->y_mode : 0;
        int8_t xm = pos->x_mode >= 0 ? pos->x_mode : 0;
        rx = axis_from_position_mode(xm, pos->x, 0, pw, w);
        ry = axis_from_position_mode(ym, pos->y, 0, ph, h);
    }

    pos->abs_x = px + rx;
    pos->abs_y = py + ry;
    pos->abs_w = w;
    pos->abs_h = h;
    pos->layout_resolved = 1;
    return !was_resolved || pos->abs_x != old_x || pos->abs_y != old_y || pos->abs_w != old_w ||
           pos->abs_h != old_h;
}

/* Deepest root->node chain UITree_EnsureLayoutFor will walk before giving up
 * and doing a full resolve. Real interface trees nest far shallower than this. */
#define UITREE_LAYOUT_MAX_CHAIN 64

/* Depth not computed yet, as distinct from -1 (a freed slot, excluded from the
 * resolve order entirely). */
#define UITREE_LAYOUT_DEPTH_UNKNOWN (-2)

void
UITree_EnsureLayoutFor(
    struct UITree const* tree,
    int32_t idx)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_ENSURE_LAYOUT, 1);
    /* Lazy JIT re-layout for a single CS2 getter. `layout_stale` is one
     * tree-wide flag set by every CC_SETPOSITION/CC_SETSIZE, so servicing a
     * getter with the full resolve made an interleaved set/get script cost
     * O(components) per get. Only ancestors can affect this node's box, so
     * resolving the root->node chain (O(depth), typically well under 10) is
     * both equivalent and vastly cheaper.
     *
     * Deliberately does NOT clear layout_stale: the rest of the tree is still
     * unresolved, and the once-per-frame full resolve still has to run. */
    struct UITree* t = (struct UITree*)tree;
    int32_t chain[UITREE_LAYOUT_MAX_CHAIN];
    int chain_len = 0;
    int32_t cur;

    if( !t || !t->layout_stale )
        return;
    if( idx < 0 || (uint32_t)idx >= t->component_count )
        return;

    for( cur = idx; cur >= 0 && (uint32_t)cur < t->component_count;
         cur = t->components[cur].parent )
    {
        /* Over-deep (or a cycle in the parent links): fall back to the full
         * resolve rather than produce a half-laid-out node. */
        if( chain_len >= UITREE_LAYOUT_MAX_CHAIN )
        {
            UITree_LayoutResolve(t, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            return;
        }
        chain[chain_len++] = cur;
    }

    while( chain_len-- > 0 )
    {
        int32_t node = chain[chain_len];
        int px;
        int py;
        int pw;
        int ph;
        layout_parent_box(
            t,
            t->components[node].parent,
            0,
            0,
            UITREE_LAYOUT_ROOT_W,
            UITREE_LAYOUT_ROOT_H,
            &px,
            &py,
            &pw,
            &ph);
        if( !layout_compute_node(t, (uint32_t)node, px, py, pw, ph) )
            continue;
        /* This node's box moved, and it now reads as resolved — so the full
         * resolve would skip it and its still-stale descendants. Hand the
         * change over to the resolve's propagation, or make it walk everything
         * if there is nowhere to record it yet. */
        if( t->layout_changed && (uint32_t)node < t->layout_cap )
            t->layout_changed[node] = 1;
        else
            t->layout_force_full = 1;
    }
}

static void
uitree_perf_snapshot(struct UITree const* tree)
{
    int free_len = 0;
    for( int32_t i = tree->free_head; i >= 0; i = tree->components[i].free_next )
        free_len++;
    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_UITREE_COMPONENTS, (int64_t)tree->component_count);
    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_UITREE_CAPACITY, (int64_t)tree->component_capacity);
    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_UITREE_FREE_LIST, free_len);
    TORIRS_PERF_COUNT_SET(
        TORIRS_PERF_CTR_UITREE_NODE_BYTES,
        (int64_t)sizeof(struct UITreeComponent) * tree->component_capacity);
}

void
UITree_LayoutResolve(
    struct UITree* tree,
    int root_x,
    int root_y,
    int root_w,
    int root_h)
{
    assert(tree);
    uitree_perf_snapshot(tree);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_LAYOUT_RESOLVE, 1);
    if( tree->component_count == 0 )
        return;

    /* Nothing invalidated, same topology, same root box: every box already
     * holds the value this walk would recompute. The per-frame callers run
     * whenever CS2 ran, which on an idle frame is every frame, and an idle
     * frame changes no layout input at all. */
    if( tree->layout_resolved_valid && !tree->layout_stale && !tree->layout_force_full &&
        tree->layout_resolved_gen == tree->generation &&
        tree->layout_resolved_root_x == root_x && tree->layout_resolved_root_y == root_y &&
        tree->layout_resolved_root_w == root_w && tree->layout_resolved_root_h == root_h )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_LAYOUT_SKIP, 1);
        return;
    }

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_LAYOUT_NODES, (int64_t)tree->component_count);

    uint32_t const n = tree->component_count;

    /* Grow the cached scratch buffers if needed. component_count only ever grows,
     * and any growth bumps `generation`, so a larger n always forces the order
     * recompute below. */
    if( tree->layout_cap < n )
    {
        uint32_t cap = tree->layout_cap ? tree->layout_cap : 16;
        while( cap < n )
            cap <<= 1;
        int* new_order = realloc(tree->layout_order, (size_t)cap * sizeof(int));
        int* new_depth = realloc(tree->layout_depth, (size_t)cap * sizeof(int));
        uint8_t* new_changed = realloc(tree->layout_changed, (size_t)cap);
        if( new_order )
            tree->layout_order = new_order;
        if( new_depth )
            tree->layout_depth = new_depth;
        if( new_changed )
            tree->layout_changed = new_changed;
        if( !new_order || !new_depth || !new_changed )
            return; /* out of memory: skip this frame rather than crash */
        tree->layout_cap = cap;
        tree->layout_order_valid = 0;
    }

    int* const depth = tree->layout_depth;
    int* const order = tree->layout_order;
    uint8_t* const changed = tree->layout_changed;

    /* depth/order are a pure function of tree topology (parent links). Recompute
     * only when the topology changed; otherwise reuse the cached ordering. The
     * parent-before-child order is built with an O(n) counting sort by depth
     * (within a depth level order is irrelevant: a node only reads its parent's
     * abs box, and parents always have strictly smaller depth). Freed
     * (reclaimed) slots are excluded entirely. */
    if( !tree->layout_order_valid || tree->layout_order_gen != tree->generation )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_LAYOUT_DEPTH_RECOMPUTE, 1);
        int max_depth = 0;
        for( uint32_t i = 0; i < n; i++ )
        {
            if( tree->components[i].freed )
            {
                depth[i] = -1;
                /* Freed slots are excluded from `order`, so nothing writes their
                 * `changed` in the walk below — but a live child of a freed
                 * parent still reads it. Freeing a slot bumps `generation`, so
                 * this branch runs whenever the set of freed slots changes. */
                changed[i] = 0;
                continue;
            }
            depth[i] = UITREE_LAYOUT_DEPTH_UNKNOWN;
        }

        /* Each node's depth is its parent's plus one, so walking the parent chain
         * per node re-walks shared ancestors over and over — O(n * depth) random
         * reads into 1.7 KB structs, which measured as the most expensive thing
         * in the resolve. Instead take each chain only as far as the first
         * ancestor whose depth is already known and fill the run in from there,
         * so every node's parent link is followed once. `order` doubles as the
         * scratch stack: it is about to be overwritten anyway. */
        for( uint32_t i = 0; i < n; i++ )
        {
            if( depth[i] != UITREE_LAYOUT_DEPTH_UNKNOWN )
                continue;

            uint32_t run = 0;
            int32_t cur = (int32_t)i;
            while( cur >= 0 && (uint32_t)cur < n && depth[cur] == UITREE_LAYOUT_DEPTH_UNKNOWN )
            {
                /* Only reachable if the parent links contain a cycle. */
                if( run >= n )
                    break;
                order[run++] = cur;
                cur = tree->components[cur].parent;
            }

            /* A parent that is out of range, absent or freed makes its child a
             * root as far as ordering is concerned. */
            int d = (cur >= 0 && (uint32_t)cur < n && depth[cur] >= 0) ? depth[cur] : -1;
            while( run-- > 0 )
            {
                depth[order[run]] = ++d;
                if( d > max_depth )
                    max_depth = d;
            }
        }

        int* counts = calloc((size_t)max_depth + 1, sizeof(int));
        if( !counts )
            return; /* out of memory: skip this frame */
        for( uint32_t i = 0; i < n; i++ )
            if( depth[i] >= 0 )
                counts[depth[i]]++;
        int total = 0;
        for( int d = 0; d <= max_depth; d++ )
        {
            int const c = counts[d];
            counts[d] = total;
            total += c;
        }
        for( uint32_t i = 0; i < n; i++ )
            if( depth[i] >= 0 )
                order[counts[depth[i]]++] = (int)i;
        free(counts);

        tree->layout_order_count = (uint32_t)total;
        tree->layout_order_gen = tree->generation;
        tree->layout_order_valid = 1;
    }

    /* A node's box is a pure function of its own fields and its parent's box, so
     * it only needs recomputing when one of those two changed: the mutators
     * clear `layout_resolved` on the node they touch, and `changed` propagates a
     * parent's new box down. One CC_CREATE bumps `generation` and so reaches
     * here, but it must not drag the other few thousand nodes with it.
     *
     * Depth order guarantees a parent is visited (and its `changed` written)
     * before any of its children read it. */
    bool const force = tree->layout_force_full != 0;
    bool const root_changed = !tree->layout_resolved_root_valid ||
                              tree->layout_resolved_root_x != root_x ||
                              tree->layout_resolved_root_y != root_y ||
                              tree->layout_resolved_root_w != root_w ||
                              tree->layout_resolved_root_h != root_h;
    int64_t skipped = 0;
    for( uint32_t k = 0; k < tree->layout_order_count; k++ )
    {
        int const i = order[k];
        int32_t const parent = tree->components[i].parent;
        bool const parent_changed = (parent >= 0 && (uint32_t)parent < n) ? changed[parent] != 0
                                                                         : root_changed;
        int px;
        int py;
        int pw;
        int ph;

        if( !force && !parent_changed && tree->components[i].position.layout_resolved )
        {
            /* changed[i] is left alone: a JIT chain resolve may have set it, and
             * this node's children have not read it yet. */
            skipped++;
            continue;
        }

        layout_parent_box(tree, parent, root_x, root_y, root_w, root_h, &px, &py, &pw, &ph);
        if( layout_compute_node(tree, (uint32_t)i, px, py, pw, ph) )
            changed[i] = 1;
    }
    /* Accumulated rather than counted per node: at a few thousand skips a frame
     * the counter call was costing more than the work it was measuring. */
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_LAYOUT_NODE_SKIP, skipped);
    /* The marks only describe this pass; children have consumed them by now. */
    memset(changed, 0, n);
    tree->layout_force_full = 0;
    tree->layout_stale = 0;
    tree->layout_resolved_valid = 1;
    tree->layout_resolved_gen = tree->generation;
    tree->layout_resolved_root_valid = 1;
    tree->layout_resolved_root_x = root_x;
    tree->layout_resolved_root_y = root_y;
    tree->layout_resolved_root_w = root_w;
    tree->layout_resolved_root_h = root_h;
    /* Scratch buffers are owned by the tree and reused; freed in UITree_Free. */
}

void
UITree_LayoutGetBounds(
    struct UITreeElemPosition const* position,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    if( !position )
        return;

    int x = position->x;
    int y = position->y;
    int w = position->width;
    int h = position->height;
    if( position->layout_resolved )
    {
        x = position->abs_x;
        y = position->abs_y;
        w = position->abs_w;
        h = position->abs_h;
    }
    if( out_x )
        *out_x = x;
    if( out_y )
        *out_y = y;
    if( out_w )
        *out_w = w;
    if( out_h )
        *out_h = h;
}

int
UITree_SnapshotResizeHooks(
    struct UITree const* tree,
    struct UITreeResizeHookSnapshot* out_entries,
    int max_entries)
{
    int count = 0;

    assert(tree);
    if( !out_entries || max_entries <= 0 )
        return 0;

    for( int i = 0; i < tree->resize_hooks.count && count < max_entries; i++ )
    {
        int32_t const idx = tree->resize_hooks.slots[i];
        struct UITreeComponent const* c;

        assert(idx >= 0 && (uint32_t)idx < tree->component_count);
        c = &tree->components[idx];
        if( c->freed || c->component_id < 0 )
            continue;
        if( UITree_Hooks(c)->on_resize.script_id <= 0 )
            continue;

        out_entries[count].node_index = idx;
        out_entries[count].component_id = c->component_id;
        out_entries[count].width = c->position.abs_w;
        out_entries[count].height = c->position.abs_h;
        count++;
    }
    return count;
}

int
UITree_CollectResizedHookIds(
    struct UITree const* tree,
    struct UITreeResizeHookSnapshot const* entries,
    int entry_count,
    int trigger_resize,
    int* out_component_ids,
    int max_ids)
{
    int count = 0;

    assert(tree);
    if( !trigger_resize || !entries || entry_count <= 0 || !out_component_ids ||
        max_ids <= 0 )
        return 0;

    for( int i = 0; i < entry_count && count < max_ids; i++ )
    {
        struct UITreeResizeHookSnapshot const* before = &entries[i];
        int32_t idx = before->node_index;
        struct UITreeComponent const* c;

        /* Component ids are the durable identity. The slot is only a fast
         * hint: topology work can move/reuse storage between the two phases. */
        if( idx < 0 || (uint32_t)idx >= tree->component_count ||
            tree->components[idx].freed ||
            tree->components[idx].component_id != before->component_id )
            idx = UITree_FindByComponentId(tree, before->component_id);
        if( idx < 0 )
            continue;
        c = &tree->components[idx];
        if( UITree_Hooks(c)->on_resize.script_id <= 0 )
            continue;
        if( c->position.abs_w == before->width && c->position.abs_h == before->height )
            continue;

        out_component_ids[count++] = before->component_id;
    }
    return count;
}
