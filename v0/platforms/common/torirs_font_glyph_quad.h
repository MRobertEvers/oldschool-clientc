#ifndef TORIRS_FONT_GLYPH_QUAD_H
#define TORIRS_FONT_GLYPH_QUAD_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One font glyph as two triangles (6 vertices × 8 floats): cx, cy, u, v, r, g, b, a.
 * Logical pixel corners (sx0,sy0)-(sx1,sy1) are converted with torirs_logical_pixel_to_ndc.
 */
void
torirs_font_glyph_quad_clipspace(
    float sx0,
    float sy0,
    float sx1,
    float sy1,
    float u0,
    float v0,
    float u1,
    float v1,
    float cr,
    float cg,
    float cb,
    float ca,
    float fbw,
    float fbh,
    float out_vq[48]);

#ifdef __cplusplus
}
#endif

#endif
