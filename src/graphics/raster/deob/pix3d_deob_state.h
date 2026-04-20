#ifndef PIX3D_DEOB_STATE_H
#define PIX3D_DEOB_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* Mirrors Pix2D / Pix3D static fields used by the deob rasterizers. */
extern int* g_pix3d_deob_pixels;
extern int g_pix3d_deob_width;
extern int g_pix3d_deob_height;
extern int g_pix3d_deob_size_x;
extern int g_pix3d_deob_clip_max_y;
extern int g_pix3d_deob_origin_x;
extern int g_pix3d_deob_origin_y;
extern int* g_pix3d_deob_scanline;

extern int g_pix3d_deob_hclip;
extern int g_pix3d_deob_trans;
extern int g_pix3d_deob_low_detail;
extern int g_pix3d_deob_low_mem;
extern int g_pix3d_deob_opaque;

extern int g_deob_cnt_compat_calls;
extern int g_deob_cnt_skip_clip_max_y;
extern int g_deob_cnt_skip_apex_zero_width;
extern int g_deob_cnt_skip_hclip_empty;
extern int g_deob_cnt_raster_ok;
extern int g_deob_cnt_dispatch_calls;

void pix3d_deob_set_clipping(
    int* pixels,
    int width,
    int height,
    int size_x,
    int clip_max_y);

void pix3d_deob_set_alpha(int trans);
void pix3d_deob_set_hclip(int hclip);
void pix3d_deob_set_low_detail(int low_detail);
void pix3d_deob_set_low_mem(int low_mem);

void pix3d_deob_free(void);

#ifdef __cplusplus
}
#endif

#endif /* PIX3D_DEOB_STATE_H */
