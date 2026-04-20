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
extern int g_deob_cnt_raster_inverted_args;
extern int g_deob_cnt_dispatch_calls;

/* pix3d_deob_texture_triangle branch histogram (Pix3D.ts y-order cases). Reset in dbg_tick.
 * Diagnosis: if UV looks wrong only when ymin is not vertex 1, compare tri_yA_min vs tri_yB_mid /
 * tri_yC_max — skew confirms which Pix3D outer branch runs; combined with face/P/M/N asserts,
 * narrows walk bugs vs input contract. */
extern int g_deob_cnt_tex_tri_yA_min;
extern int g_deob_cnt_tex_tri_yB_mid;
extern int g_deob_cnt_tex_tri_yC_max;
extern int g_deob_cnt_tex_tri_yA_yBlt_yC;
extern int g_deob_cnt_tex_tri_yA_yBge_yC;
extern int g_deob_cnt_tex_tri_yA_yBltC_inner_ac_ab;
extern int g_deob_cnt_tex_tri_yA_yBltC_inner_else;
extern int g_deob_cnt_tex_tri_yA_yBgeC_path1;
extern int g_deob_cnt_tex_tri_yA_yBgeC_path2;

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
