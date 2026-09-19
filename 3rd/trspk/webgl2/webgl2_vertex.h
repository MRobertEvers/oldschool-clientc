#ifndef WEBGL2_VERTEX_H
#define WEBGL2_VERTEX_H

#include "gles2/gles2_vertex.h"

/*
 * The WebGL2 renderer's retained world vertex is the GLES2 one.
 *
 * It is spelled here rather than duplicated because the two renderers share
 * their whole bake pipeline -- trspk_vbo's writer, the ToriDraw encoders in
 * src/render/trspk_toridraw.c, Batch16, the model arena and the pose tables
 * all speak TRSPK_VERTEX_FORMAT_GLES2 -- and a second 28-byte struct with
 * the same fields would mean every bake fix landing twice and, sooner or
 * later, one of the two drifting.
 *
 * What differs is how the GL layer READS those 28 bytes:
 *
 *   position   3 x GL_FLOAT                        (both)
 *   rgba       4 x GL_UNSIGNED_BYTE, normalised    (both)
 *   texcoord   2 x GL_FLOAT                        (both)
 *   tile/anim  4 x GL_UNSIGNED_BYTE
 *              ES2:    glVertexAttribPointer, NOT normalised -- the shader
 *                      receives floats 0..255 and scales them itself,
 *                      because ES 1.00 has no integer attribute.
 *              WebGL2: glVertexAttribIPointer -- the shader receives a
 *                      uvec4 and the multiply is gone.
 *
 * A wider or differently packed vertex is the obvious next step for this
 * lane (ES3 allows GL_HALF_FLOAT and GL_INT_2_10_10_10_REV attributes, which
 * would take the vertex to 20 bytes), and it is deliberately NOT taken here:
 * it would fork the bake, which is the one thing this renderer's separation
 * from the WebGL1 one is meant not to do.
 */
typedef struct TRSPK_VertexGLES2 TRSPK_VertexWebGL2;

#endif
