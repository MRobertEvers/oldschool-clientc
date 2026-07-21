#include "uitree_layout.h"

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

void
UITree_LayoutResolve(
    struct UITree* tree,
    int root_x,
    int root_y,
    int root_w,
    int root_h)
{
    assert(tree);
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

    int* const depth = tree->layout_depth;
    int* const order = tree->layout_order;
    int* const abs_x = tree->layout_abs_x;
    int* const abs_y = tree->layout_abs_y;
    int* const abs_w = tree->layout_abs_w;
    int* const abs_h = tree->layout_abs_h;

    /* depth/order are a pure function of tree topology (parent links). Recompute
     * only when the topology changed; otherwise reuse the cached ordering. The
     * parent-before-child order is built with an O(n) counting sort by depth
     * (within a depth level order is irrelevant: a node only reads its parent's
     * abs box, and parents always have strictly smaller depth). Freed
     * (reclaimed) slots are excluded entirely. */
    if( !tree->layout_order_valid || tree->layout_order_gen != tree->generation )
    {
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

    for( uint32_t k = 0; k < tree->layout_order_count; k++ )
    {
        int i = order[k];
        struct UITreeComponent* c = &tree->components[i];
        struct UITreeElemPosition* pos = &c->position;

        int px = root_x;
        int py = root_y;
        int pw = root_w;
        int ph = root_h;
        if( c->parent >= 0 && (uint32_t)c->parent < n )
        {
            px = abs_x[c->parent];
            py = abs_y[c->parent];
            pw = abs_w[c->parent];
            ph = abs_h[c->parent];
            struct UITreeComponent const* parent = &tree->components[c->parent];
            if( parent->type == UIELEM_RS_LAYER )
            {
                if( parent->u.rs_layer.scroll_width > 0 )
                    pw = parent->u.rs_layer.scroll_width;
                if( parent->u.rs_layer.scroll_height > 0 )
                    ph = parent->u.rs_layer.scroll_height;
            }
        }

        if( pos->kind == UIPOS_RELATIVE )
        {
            resolve_relative(pos, px, py, pw, ph);
            abs_x[i] = pos->abs_x;
            abs_y[i] = pos->abs_y;
            abs_w[i] = pos->abs_w;
            abs_h[i] = pos->abs_h;
            continue;
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

        abs_x[i] = px + rx;
        abs_y[i] = py + ry;
        abs_w[i] = w;
        abs_h[i] = h;
        pos->abs_x = abs_x[i];
        pos->abs_y = abs_y[i];
        pos->abs_w = abs_w[i];
        pos->abs_h = abs_h[i];
        pos->layout_resolved = 1;
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
