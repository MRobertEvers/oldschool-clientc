#ifndef TORIDRAW_2D_H
#define TORIDRAW_2D_H

#include "toridraw_types.h"

#include <stdint.h>

void
ToriDraw2D_BlendArgbPixel(
    struct ToriDraw_ViewPort* view_port,
    int x,
    int y,
    int argb,
    int* pixel_buffer);

void
ToriDraw2D_FillRect(
    struct ToriDraw_ViewPort* view_port,
    int x0,
    int y0,
    int x1,
    int y1,
    int argb,
    int* pixel_buffer);

void
ToriDraw2D_FillRectGradientVertical(
    struct ToriDraw_ViewPort* view_port,
    int x0,
    int y0,
    int x1,
    int y1,
    int color_top,
    int color_bot,
    int alpha,
    int* pixel_buffer);

void
ToriDraw2D_FillRectGradientAlpha(
    struct ToriDraw_ViewPort* view_port,
    int x0,
    int y0,
    int x1,
    int y1,
    int color_top,
    int color_bot,
    int alpha_top,
    int alpha_bot,
    int* pixel_buffer);

void
ToriDraw2D_DrawRectOutline(
    struct ToriDraw_ViewPort* view_port,
    int x0,
    int y0,
    int x1,
    int y1,
    int argb,
    int* pixel_buffer);

void
ToriDraw2D_DrawLine(
    struct ToriDraw_ViewPort* view_port,
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    int argb,
    int* pixel_buffer);

void
ToriDraw2D_BlitArgb(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    uint32_t const* src,
    int src_w,
    int src_h,
    int* pixel_buffer);

/* Source-over ARGB blit with an additional 0..255 opacity multiplier. */
void
ToriDraw2D_BlitArgbAlpha(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    uint32_t const* src,
    int src_w,
    int src_h,
    int alpha,
    int* pixel_buffer);

void
ToriDraw2D_BlitArgbScaled(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    uint32_t const* src,
    int src_w,
    int src_h,
    int* pixel_buffer);

void
ToriDraw2D_BlitArgbScaledAlpha(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    uint32_t const* src,
    int src_w,
    int src_h,
    int alpha,
    int* pixel_buffer);

void
ToriDraw2D_BlitArgbTiled(
    struct ToriDraw_ViewPort* view_port,
    int rect_x,
    int rect_y,
    int rect_w,
    int rect_h,
    uint32_t const* src,
    int src_w,
    int src_h,
    int origin_x,
    int origin_y,
    int* pixel_buffer);

void
ToriDraw2D_BlitArgbTiledAlpha(
    struct ToriDraw_ViewPort* view_port,
    int rect_x,
    int rect_y,
    int rect_w,
    int rect_h,
    uint32_t const* src,
    int src_w,
    int src_h,
    int origin_x,
    int origin_y,
    int alpha,
    int* pixel_buffer);

void
ToriDraw2D_BlitArgbMasked(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    uint32_t const* content,
    int content_w,
    int content_h,
    uint32_t const* mask,
    int mask_w,
    int mask_h,
    int* pixel_buffer);

void
ToriDraw2D_BlitArgbMaskedInverted(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    uint32_t const* content,
    int content_w,
    int content_h,
    uint32_t const* mask,
    int mask_w,
    int mask_h,
    int* pixel_buffer);

void
ToriDraw2D_BlitArgbRotatedMaskedInverted(
    struct ToriDraw_ViewPort* view_port,
    int mask_x,
    int mask_y,
    int mask_w,
    int mask_h,
    uint32_t const* content,
    int content_w,
    int content_h,
    uint32_t const* mask,
    int mask_sw,
    int mask_sh,
    int angle,
    int angle_scale,
    int alpha,
    int* pixel_buffer);

#endif
