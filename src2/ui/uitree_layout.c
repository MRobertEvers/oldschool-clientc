#include "uitree_layout.h"

#include "ui_if3_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
uitree_layout_invalidate(struct UITree* tree)
{
    if( !tree )
        return;
    for( uint32_t i = 0; i < tree->component_count; i++ )
        tree->components[i].position.layout_resolved = 0;
}

static int
dim_from_parent_mode(
    int8_t mode,
    int orig,
    int parent_dim)
{
    switch( mode )
    {
    case 0:
        return orig;
    case 1:
        return parent_dim - orig;
    case 2:
        return uitree_mul_shift14(parent_dim, orig);
    default:
        return orig;
    }
}

static int
axis_from_position_mode(
    int8_t mode,
    int base,
    int parent_origin,
    int parent_dim,
    int self_dim)
{
    switch( mode )
    {
    case 0:
        return parent_origin + base;
    case 1:
        return parent_origin + ((parent_dim - self_dim) >> 1) + base;
    case 2:
        return parent_origin + parent_dim - base - self_dim;
    case 3:
        return parent_origin + uitree_mul_shift14(parent_dim, base);
    case 4:
        return parent_origin + ((parent_dim - self_dim) >> 1) +
               uitree_mul_shift14(parent_dim, base);
    case 5:
        return parent_origin + parent_dim - uitree_mul_shift14(parent_dim, base) - self_dim;
    default:
        return parent_origin + base;
    }
}

static void
resolve_relative(
    struct StaticUIElemPosition* pos,
    int parent_x,
    int parent_y,
    int parent_w,
    int parent_h,
    int container_w,
    int container_h)
{
    int x = parent_x;
    int y = parent_y;
    int w = pos->width > 0 ? pos->width : parent_w;
    int h = pos->height > 0 ? pos->height : parent_h;

    if( pos->relative_flags & STATIC_UI_RELATIVE_FLAG_LEFT )
        x = parent_x + pos->left;
    else if( pos->relative_flags & STATIC_UI_RELATIVE_FLAG_RIGHT )
        x = parent_x + parent_w - pos->right - w;

    if( pos->relative_flags & STATIC_UI_RELATIVE_FLAG_TOP )
        y = parent_y + pos->top;
    else if( pos->relative_flags & STATIC_UI_RELATIVE_FLAG_BOTTOM )
        y = parent_y + parent_h - pos->bottom - h;

    if( !(pos->relative_flags & (STATIC_UI_RELATIVE_FLAG_LEFT | STATIC_UI_RELATIVE_FLAG_RIGHT)) )
        x = parent_x + (parent_w - w) / 2;
    if( !(pos->relative_flags & (STATIC_UI_RELATIVE_FLAG_TOP | STATIC_UI_RELATIVE_FLAG_BOTTOM)) )
        y = parent_y + (parent_h - h) / 2;

    (void)container_w;
    (void)container_h;
    pos->abs_x = x;
    pos->abs_y = y;
    pos->abs_w = w;
    pos->abs_h = h;
    pos->layout_resolved = 1;
}

void
uitree_layout_resolve(
    struct UITree* tree,
    int root_x,
    int root_y,
    int root_w,
    int root_h)
{
    if( !tree || tree->component_count == 0 )
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
        fprintf(stderr, "uitree_layout_resolve: allocation failed for %u components\n", n);
        assert(depth && order && abs_x && abs_y && abs_w && abs_h);
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
            {
                fprintf(
                    stderr,
                    "uitree_layout_resolve: invalid parent %d for component %u (n=%u)\n",
                    parent,
                    (unsigned)i,
                    n);
                break;
            }
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
        struct StaticUIComponent* c = &tree->components[i];
        struct StaticUIElemPosition* pos = &c->position;

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
            struct StaticUIComponent const* parent = &tree->components[c->parent];
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
            resolve_relative(pos, px, py, pw, ph, root_w, root_h);
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
                ui_if3_compute_size(
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
uitree_layout_get_bounds(
    struct StaticUIElemPosition const* position,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    if( !position || !out_x || !out_y || !out_w || !out_h )
        return;
    if( position->layout_resolved )
    {
        *out_x = position->abs_x;
        *out_y = position->abs_y;
        *out_w = position->abs_w;
        *out_h = position->abs_h;
        return;
    }
    *out_x = position->x;
    *out_y = position->y;
    *out_w = position->width;
    *out_h = position->height;
}
