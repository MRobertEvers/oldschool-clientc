#ifndef TEX_TRI_ASM_H
#define TEX_TRI_ASM_H

/*
 * Hand-written perspective-textured triangle kernel.
 *
 * The C twin, raster_texshadeblend_persp_texopaque_branching_lerp8_v3 in
 * texshadeblend.persp.texopaque.branching.lerp8_v3.u.c, stays the reference:
 * it defines the pixels, and toridraw_textri_asm_test.c fails if this does not
 * reproduce it byte for byte over a replayed triangle set. "Close enough" is
 * not available here -- a textured triangle that lands one texel row off is
 * the streaking class of bug in docs/qbd_toridraw_streaks_debug.md, which does
 * not read as a wrong pixel so much as a wrong-looking rock.
 *
 * WHY BY HAND, WHEN THE SPAN IS ALREADY BY HAND
 *
 * Because the span has to be CALLED. tex_span_i686.S is a cdecl entry taking
 * fifteen arguments, and the C walk invokes it once per scanline. Nine of those
 * fifteen do not change between the first row of a triangle and the last: the
 * pixel buffer, the screen width, the three per-pixel plane steps, the
 * per-pixel shade step, the texel pointer, the texture width, and the width
 * dispatch that last one drives. Every row pays fifteen pushes, a call and a
 * return, then twelve stores rebuilding the ST_* strides from steps that did
 * not move, then a branch on a texture width that did not move either.
 *
 * The face census over the osrs239 lumbridge bench says this is the variant
 * worth absorbing: 417,302 faces and 57,509,312 pixels, which is 78.87% of
 * textured faces and 89.81% of textured area. It is also the ONLY production
 * caller of the span asm, so absorbing it does not leave the call site behind
 * for someone else.
 *
 * The fill is not duplicated. Both this kernel and the standalone span expand
 * SPANBODY from span/tex_span_body.inc -- see the header there for why.
 */

#ifdef TORIDRAW_TEXTRI_ASM

#ifdef TORIDRAW_PIXEL16
#error "tex_tri_i686.S assumes 32-bit pixels and 32-bit texels"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Bit-exact hand-written twin of
 *  raster_texshadeblend_persp_texopaque_branching_lerp8_v3, sort included. */
void toridraw_textri_opaque_lerp8_v3_asm(
    int* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit_a,
    int shade7bit_b,
    int shade7bit_c,
    int* texels,
    int texture_width);


/** Bit-exact hand-written twin of
 *  raster_texshadeblend_persp_textrans_branching_lerp8_v3. A texel whose RGB
 *  is zero leaves the destination alone; everything else is the opaque twin.
 *  The test is on the RAW texel, before the shade, because a dark texel under
 *  a low shade blends to zero too and skipping it would drop a pixel the
 *  reference draws. */
void toridraw_textri_trans_lerp8_v3_asm(
    int* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit_a,
    int shade7bit_b,
    int shade7bit_c,
    int* texels,
    int texture_width);

/*
 * The flat-shaded pair -- kernels 7 and 8.
 *
 * Same twenty-five arguments as the blend kernels, and only shade7bit_a is
 * read: a face with one shade has both gradients exactly zero, so the walk
 * these enter has no shade accumulator, no per-block broadcast and no
 * per-row step. Pass the face shade three times (which is what the reference
 * expansion below does) or once with the other two lanes left alone.
 *
 * Their reference is the blend kernel at equal shades, which is a stronger
 * statement than it sounds: it means the flat and blend arms of a textured
 * model are the same rasteriser and tile against each other exactly.
 */
void toridraw_textri_flat_opaque_lerp8_v3_asm(
    int* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit_a,
    int shade7bit_b,
    int shade7bit_c,
    int* texels,
    int texture_width);

void toridraw_textri_flat_trans_lerp8_v3_asm(
    int* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit_a,
    int shade7bit_b,
    int shade7bit_c,
    int* texels,
    int texture_width);


/*
 * The batched doors. `rows` is `count` records of twenty-four ints, 16-byte
 * aligned: three screen x, three screen y, the nine orthographic coordinates
 * of the texture frame, three shades, the texel pointer, the texture width,
 * the colour-key gate, and three lanes of padding.
 *
 * The x, y and shade triples arrive ALREADY in y order -- the depth sort put
 * them there with the values it was holding for the winding -- so these skip
 * the six-way compare ladder entirely. The nine frame coordinates are NOT
 * permuted with them: they are (uv origin, u end, v end) taken from three
 * vertex ROLES, not from the triangle's own corners.
 *
 * What this deletes per triangle is the twenty-five argument cdecl marshal,
 * the call, four register saves and restores, and the five screen constants
 * re-read out of the incoming frame.
 */
void toridraw_textri_opaque_lerp8_v3_batch_asm(
    int* pixel_buffer, int stride, int screen_width, int screen_height,
    int camera_cot16, const int* rows, int count);

void toridraw_textri_trans_lerp8_v3_batch_asm(
    int* pixel_buffer, int stride, int screen_width, int screen_height,
    int camera_cot16, const int* rows, int count);

void toridraw_textri_flat_opaque_lerp8_v3_batch_asm(
    int* pixel_buffer, int stride, int screen_width, int screen_height,
    int camera_cot16, const int* rows, int count);

void toridraw_textri_flat_trans_lerp8_v3_batch_asm(
    int* pixel_buffer, int stride, int screen_width, int screen_height,
    int camera_cot16, const int* rows, int count);

/* The batched pipeline can take a textured face only when these exist. */
#define TORIDRAW_TEXTRI_BATCH 1

/*
 * The one piece of the triangle that stays in C, and the reason it does.
 *
 * ToriDraw_TexturePlanePrepare32 runs once per triangle, both of its
 * normalisation loops are normally zero iterations, and it maintains
 * g_toridraw_tex_plane_max_shift and g_toridraw_tex_plane_rejected, which the
 * raster debug line reports per model. Hand-writing it would buy a few
 * predicted-not-taken branches per triangle and would move those counters out
 * of the C, where every other caller still reads them.
 *
 * Declared here rather than in projection.h because the asm is its only caller:
 * this is the symbol tex_tri_i686.S calls, and nothing else should.
 */
struct ToriDraw_TexturePlane32;

/** 1 if the plane normalised into 32-bit range, 0 if it was rejected.
 *  int and not bool: cdecl returns bool in al, and the asm tests eax. */
int toridraw_texplane_prepare32_asm(
    struct ToriDraw_TexturePlane32* plane,
    int screen_width,
    int screen_height,
    int camera_cot16);

#ifdef __cplusplus
}
#endif

/*
 * The dispatch macro, so the call site names one thing and the build decides
 * which thing it is. Same shape as TORIDRAW_GOURAUD_TRI_OPAQUE_S4 next door.
 */
#define TORIDRAW_TEX_TRI_PERSP_OPAQUE      toridraw_textri_opaque_lerp8_v3_asm
#define TORIDRAW_TEX_TRI_PERSP_TRANS       toridraw_textri_trans_lerp8_v3_asm
#define TORIDRAW_TEX_TRI_PERSP_FLAT_OPAQUE toridraw_textri_flat_opaque_lerp8_v3_asm
#define TORIDRAW_TEX_TRI_PERSP_FLAT_TRANS  toridraw_textri_flat_trans_lerp8_v3_asm

#else

/* The reference expansion. The flat pair maps onto the blend kernel because
 * that IS their definition -- the caller has already put its one shade in all
 * three argument slots, so there is nothing here to collapse. */
#define TORIDRAW_TEX_TRI_PERSP_OPAQUE      raster_texshadeblend_persp_texopaque_branching_lerp8_v3
#define TORIDRAW_TEX_TRI_PERSP_TRANS       raster_texshadeblend_persp_textrans_branching_lerp8_v3
#define TORIDRAW_TEX_TRI_PERSP_FLAT_OPAQUE raster_texshadeblend_persp_texopaque_branching_lerp8_v3
#define TORIDRAW_TEX_TRI_PERSP_FLAT_TRANS  raster_texshadeblend_persp_textrans_branching_lerp8_v3

#endif

#endif
