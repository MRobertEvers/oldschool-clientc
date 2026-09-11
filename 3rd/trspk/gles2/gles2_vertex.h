#ifndef GLES2_VERTEX_H
#define GLES2_VERTEX_H

#include <stdint.h>

/*
 * The retained world vertex of the native Android GLES2 renderer.
 *
 * 28 bytes. The two GL formats beside it are 48 (float colour, a float
 * texture id and a float uv mode per corner) and D3D9's is 32. Everything the
 * vertex shader reads is here and nothing else:
 *
 *   position   world space, already placed (rotation + translation baked)
 *   rgba       four normalised bytes in R,G,B,A memory order, which is the
 *              order GL reads a 4 x GL_UNSIGNED_BYTE attribute in
 *   texcoord   the face's LOCAL texture coordinate, 0..1 across its tile.
 *              Not resolved to the atlas here: the fragment shader wraps and
 *              clamps it per fragment (the GL3 formula) and then maps it
 *              into the tile, so one atlas serves every face, scrolling ones
 *              included, and the world pass never rebinds a texture.
 *   tile_col   which 128x128 atlas tile: column and row of the slot.
 *   tile_row   Slot 0 is the opaque white tile untextured faces sample.
 *   anim_u     the texture's scroll per clock tick along u / v, as a signed
 *   anim_v     speed biased by 128 (128 = still). The vertex shader turns it
 *              back into speed / 128 texels per tick; a face whose texture had
 *              not loaded when it was baked is patched in place when it does.
 *
 * On a 2013 phone the vertex fetch is a real cost and the retained arena is
 * a real fraction of the process; 28 rather than 48 is most of the way to
 * halving both.
 */
struct TRSPK_VertexGLES2
{
    float position[3];
    uint32_t rgba;
    float texcoord[2];
    uint8_t tile_col;
    uint8_t tile_row;
    uint8_t anim_u;
    uint8_t anim_v;
};

#define TRSPK_VERTEX_GLES2_ANIM_STILL 128u

#endif
