#ifndef ES3_OSRS_VERTEX_H
#define ES3_OSRS_VERTEX_H

#include "../gles2/gles2_vertex.h"

#include <stddef.h>
#include <stdint.h>

/*
 * The retained world vertex of the WebGL2 (OSRS) renderer.
 *
 * The GLES2 vertex (gles2_vertex.h) and one thing more: the face's depth bias,
 * RSCache_Model.face_z_offsets, the rev-241 client's class174.field2408. The
 * client keys each corner at its camera depth less twice that byte, which is
 * how a decal authored on a floor, or a sword's shadow on the ground, wins the
 * surface it lies on instead of fighting it. See docs/toridraw_rszbuffer.md.
 *
 * The first 28 bytes are the GLES2 vertex byte for byte, so everything that
 * reads or patches those fields (the scroll bytes es3_refresh_anim_range
 * rewrites, the attribute offsets) is the same code at a different stride.
 * The bias is stored raw, as the cache has it; the shader doubles it.
 *
 * Its own format rather than the GLES2 one with the bias squeezed into spare
 * bits: the painter and depth renderers keep their vertex and their shaders,
 * and only this renderer pays the four bytes.
 */
struct TRSPK_VertexES3Osrs
{
    float position[3];
    uint32_t rgba;
    float texcoord[2];
    uint8_t tile_col;
    uint8_t tile_row;
    uint8_t anim_u;
    uint8_t anim_v;
    int8_t depth_bias;
    uint8_t pad[3];
};

_Static_assert(sizeof(struct TRSPK_VertexES3Osrs) == 32u, "the OSRS world vertex is 32 bytes");
_Static_assert(
    offsetof(struct TRSPK_VertexES3Osrs, rgba) == offsetof(struct TRSPK_VertexGLES2, rgba) &&
        offsetof(struct TRSPK_VertexES3Osrs, texcoord) ==
            offsetof(struct TRSPK_VertexGLES2, texcoord) &&
        offsetof(struct TRSPK_VertexES3Osrs, tile_col) ==
            offsetof(struct TRSPK_VertexGLES2, tile_col) &&
        offsetof(struct TRSPK_VertexES3Osrs, anim_u) == offsetof(struct TRSPK_VertexGLES2, anim_u) &&
        offsetof(struct TRSPK_VertexES3Osrs, anim_v) == offsetof(struct TRSPK_VertexGLES2, anim_v),
    "the OSRS world vertex starts with the GLES2 one");

#endif
