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
static void
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

    if( pos->kind == UIPOS_RELATIVE )
    {
        resolve_relative(pos, px, py, pw, ph);
        return;
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
        int8_t xm = pos->x_mode >= 0 ? pos->x_mode : 0;
        int8_t ym = pos->y_mode >= 0 ? pos->y_mode : 0;
        rx = axis_from_position_mode(xm, pos->x, 0, pw, w);
        ry = axis_from_position_mode(ym, pos->y, 0, ph, h);
    }

    pos->abs_x = px + rx;
    pos->abs_y = py + ry;
    pos->abs_w = w;
    pos->abs_h = h;
    pos->layout_resolved = 1;
}

/* Deepest root->node chain UITree_EnsureLayoutFor will walk before giving up
 * and doing a full resolve. Real interface trees nest far shallower than this. */
#define UITREE_LAYOUT_MAX_CHAIN 64

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
        layout_compute_node(t, (uint32_t)node, px, py, pw, ph);
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
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_LAYOUT_NODES, (int64_t)tree->component_count);
    if( tree->component_count == 0 )
        return;

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
        int* new_abs_x = realloc(tree->layout_abs_x, (size_t)cap * sizeof(int));
        int* new_abs_y = realloc(tree->layout_abs_y, (size_t)cap * sizeof(int));
        int* new_abs_w = realloc(tree->layout_abs_w, (size_t)cap * sizeof(int));
        int* new_abs_h = realloc(tree->layout_abs_h, (size_t)cap * sizeof(int));
        if( new_order )
            tree->layout_order = new_order;
        if( new_depth )
            tree->layout_depth = new_depth;
        if( new_abs_x )
            tree->layout_abs_x = new_abs_x;
        if( new_abs_y )
            tree->layout_abs_y = new_abs_y;
        if( new_abs_w )
            tree->layout_abs_w = new_abs_w;
        if( new_abs_h )
            tree->layout_abs_h = new_abs_h;
        if( !new_order || !new_depth || !new_abs_x || !new_abs_y || !new_abs_w || !new_abs_h )
            return; /* out of memory: skip this frame rather than crash */
        tree->layout_cap = cap;
        tree->layout_order_valid = 0;
    }

    /* abs_* scratch is no longer read: nodes resolve against their parent's box
     * in the tree directly (see layout_parent_box). The buffers stay allocated
     * so UITree_Free is unchanged. */
    int* const depth = tree->layout_depth;
    int* const order = tree->layout_order;

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
                continue;
            }
            int d = 0;
            int32_t cur = (int32_t)i;
            while( cur >= 0 && (uint32_t)cur < n )
            {
                int32_t const parent = tree->components[cur].parent;
                if( parent < 0 )
                    break;
                if( (uint32_t)parent >= n )
                    break;
                cur = parent;
                d++;
                if( d > (int)n )
                    break;
            }
            depth[i] = d;
            if( d > max_depth )
                max_depth = d;
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

    /* Depth order guarantees a parent is resolved before its children, so each
     * node can read its parent's box straight out of the tree. */
    for( uint32_t k = 0; k < tree->layout_order_count; k++ )
    {
        int i = order[k];
        int px;
        int py;
        int pw;
        int ph;

        layout_parent_box(
            tree, tree->components[i].parent, root_x, root_y, root_w, root_h, &px, &py, &pw, &ph);
        layout_compute_node(tree, (uint32_t)i, px, py, pw, ph);
    }
    tree->layout_stale = 0;
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
