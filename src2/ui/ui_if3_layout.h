#ifndef UI_IF3_LAYOUT_H
#define UI_IF3_LAYOUT_H

#include "osrs/rscache/dat2a/dat2a_component.h"
#include "uitree_layout.h"

#include <stdint.h>

static inline int
ui_if3_dim_from_parent_mode(
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
        return (int)((int64_t)parent_dim * (int64_t)orig / UITREE_RS_LAYOUT_UNITS);
    default:
        return orig;
    }
}

static inline int
ui_if3_axis_from_position_mode(
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
        return parent_origin + (parent_dim - self_dim) / 2 + base;
    case 2:
        return parent_origin + parent_dim - base - self_dim;
    case 3:
        return parent_origin + (int)((int64_t)parent_dim * (int64_t)base / UITREE_RS_LAYOUT_UNITS);
    case 4:
        return parent_origin + (parent_dim - self_dim) / 2 +
               (int)((int64_t)parent_dim * (int64_t)base / UITREE_RS_LAYOUT_UNITS);
    case 5:
        return parent_origin + parent_dim -
               (int)((int64_t)parent_dim * (int64_t)base / UITREE_RS_LAYOUT_UNITS) - self_dim;
    default:
        return parent_origin + base;
    }
}

static inline void
ui_if3_compute_size(
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
    int w = ui_if3_dim_from_parent_mode(width_mode, base_width, parent_w);
    int h = ui_if3_dim_from_parent_mode(height_mode, base_height, parent_h);

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
ui_if3_component_parent_relative_layout(
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
    ui_if3_compute_size(
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
    *out_rel_x = ui_if3_axis_from_position_mode(x_mode, base_x, 0, parent_w, w);
    *out_rel_y = ui_if3_axis_from_position_mode(y_mode, base_y, 0, parent_h, h);
}

static inline void
ui_if3_dat2_component_parent_relative_layout(
    Component const* comp,
    int parent_w,
    int parent_h,
    int* out_rel_x,
    int* out_rel_y,
    int* out_w,
    int* out_h)
{
    ui_if3_component_parent_relative_layout(
        comp && comp->if3 ? 1 : 0,
        comp ? comp->widthMode : 0,
        comp ? comp->heightMode : 0,
        comp ? comp->xMode : 0,
        comp ? comp->yMode : 0,
        comp ? comp->baseX : 0,
        comp ? comp->baseY : 0,
        comp ? comp->baseWidth : 0,
        comp ? comp->baseHeight : 0,
        comp ? comp->aspect_ratio_w : 1,
        comp ? comp->aspect_ratio_h : 1,
        parent_w,
        parent_h,
        out_rel_x,
        out_rel_y,
        out_w,
        out_h);
}

#endif
