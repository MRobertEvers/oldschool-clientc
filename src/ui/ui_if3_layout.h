#ifndef SRC_UI_IF3_LAYOUT_H
#define SRC_UI_IF3_LAYOUT_H

#include "uitree_layout.h"

#include <stdint.h>

static inline int
UITree_If3DimFromParentMode(
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
        return UITree_MulShift14(parent_dim, orig);
    default:
        return orig;
    }
}

static inline int
UITree_If3AxisFromPositionMode(
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
        /* Oversized centred children left/top-align instead of overhanging the
         * origin side. Stretch gameframe: canvas-sized viewport_tracker inside
         * canvas-42 gameframe would otherwise sit at abs_x=-21 and clip HUDs
         * (stat boosts, worldmap) at the screen edge; right bleed under the
         * popout strip is preserved. */
        if( self_dim > parent_dim )
            return parent_origin + base;
        return parent_origin + ((parent_dim - self_dim) >> 1) + base;
    case 2:
        return parent_origin + parent_dim - base - self_dim;
    case 3:
        return parent_origin + UITree_MulShift14(parent_dim, base);
    case 4:
        if( self_dim > parent_dim )
            return parent_origin + UITree_MulShift14(parent_dim, base);
        return parent_origin + ((parent_dim - self_dim) >> 1) +
               UITree_MulShift14(parent_dim, base);
    case 5:
        return parent_origin + parent_dim - UITree_MulShift14(parent_dim, base) - self_dim;
    default:
        return parent_origin + base;
    }
}

static inline void
UITree_If3ComputeSize(
    int8_t width_mode,
    int8_t height_mode,
    int base_width,
    int base_height,
    int parent_w,
    int parent_h,
    int aspect_w,
    int aspect_h,
    int* out_w,
    int* out_h)
{
    int w = UITree_If3DimFromParentMode(width_mode, base_width, parent_w);
    int h = UITree_If3DimFromParentMode(height_mode, base_height, parent_h);

    if( aspect_w <= 0 )
        aspect_w = 1;
    if( aspect_h <= 0 )
        aspect_h = 1;

    if( width_mode == 4 )
        w = aspect_w * h / aspect_h;
    if( height_mode == 4 )
        h = aspect_h * w / aspect_w;

    if( w < 0 )
        w = 0;
    if( h < 0 )
        h = 0;

    *out_w = w;
    *out_h = h;
}

static inline void
UITree_If3ComponentParentRelativeLayout(
    int if3,
    int8_t width_mode,
    int8_t height_mode,
    int8_t x_mode,
    int8_t y_mode,
    int base_x,
    int base_y,
    int base_width,
    int base_height,
    int aspect_w,
    int aspect_h,
    int parent_w,
    int parent_h,
    int* out_rel_x,
    int* out_rel_y,
    int* out_w,
    int* out_h)
{
    if( !if3 )
    {
        *out_rel_x = base_x;
        *out_rel_y = base_y;
        *out_w = base_width;
        *out_h = base_height;
        return;
    }

    int w = 0;
    int h = 0;
    UITree_If3ComputeSize(
        width_mode,
        height_mode,
        base_width,
        base_height,
        parent_w,
        parent_h,
        aspect_w,
        aspect_h,
        &w,
        &h);

    *out_w = w;
    *out_h = h;
    *out_rel_x = UITree_If3AxisFromPositionMode(x_mode, base_x, 0, parent_w, w);
    *out_rel_y = UITree_If3AxisFromPositionMode(y_mode, base_y, 0, parent_h, h);
}

#endif
