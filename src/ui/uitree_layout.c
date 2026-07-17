#include "uitree_layout.h"

#include "ui_if3_layout.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
UITree_LayoutInvalidate(struct UITree* tree)
{
    assert(tree);

    for( uint32_t i = 0; i < tree->component_count; i++ )
        tree->components[i].position.layout_resolved = 0;
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
    int* depth = calloc((size_t)n, sizeof(int));
    int* order = calloc((size_t)n, sizeof(int));
    int* abs_x = calloc((size_t)n, sizeof(int));
    int* abs_y = calloc((size_t)n, sizeof(int));
    int* abs_w = calloc((size_t)n, sizeof(int));
    int* abs_h = calloc((size_t)n, sizeof(int));
    if( !depth || !order || !abs_x || !abs_y || !abs_w || !abs_h )
    {
        free(depth);
        free(order);
        free(abs_x);
        free(abs_y);
        free(abs_w);
        free(abs_h);
        return;
    }

    for( uint32_t i = 0; i < n; i++ )
    {
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
        order[i] = (int)i;
    }

    for( uint32_t a = 0; a < n; a++ )
    {
        for( uint32_t b = a + 1; b < n; b++ )
        {
            if( depth[order[b]] < depth[order[a]] )
            {
                int t = order[a];
                order[a] = order[b];
                order[b] = t;
            }
        }
    }

    for( uint32_t k = 0; k < n; k++ )
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

    free(depth);
    free(order);
    free(abs_x);
    free(abs_y);
    free(abs_w);
    free(abs_h);
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
