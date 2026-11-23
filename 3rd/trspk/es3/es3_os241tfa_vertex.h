#ifndef ES3_OS241TFA_VERTEX_H
#define ES3_OS241TFA_VERTEX_H

#include "../gles2/gles2_os1_vertex.h"
#include "es3_os235sailing_vertex.h"

#include <stddef.h>
#include <stdint.h>

/*
 * The retained world vertex of the WebGL 2 (Post-TFA OSRS) renderer
 * (TRSPK_VERTEX_FORMAT_ES3_OS241TFA): the Sailing OSRS vertex
 * (es3_os235sailing_vertex.h, the GLES2 one plus the face's depth bias) and
 * the face's TEXTURE PROJECTOR, as half floats: P less this corner's position,
 * and the edges U = M - P, V = N - P, in world space
 * (trspk_toridraw_face_projector).
 *
 * The client finds a textured pixel's texel where the eye ray through it
 * meets the P/M/N plane [class171.method5476]; the baked texcoord drops the
 * corner straight onto the plane. The two agree when the face lies in that
 * plane, which every model did until The Fractured Archive (TFA): the
 * Ocellus void's starfield sits on a plane below its faces and slides as the
 * camera moves. es3_world_os241tfa_vertex_shader does the client's lookup from
 * these. All zero on a face with none (untextured, a tile).
 *
 * The first 32 bytes are the Sailing OSRS vertex byte for byte. Halves: P sits
 * within a model of the corner and U, V are model edges, so 11 bits of
 * mantissa keep the texel to a fraction of one, and the vertex is 52 bytes
 * rather than 68.
 */
struct TRSPK_VertexES3Os241Tfa
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
    /* IEEE half floats (trspk_float_to_half). */
    uint16_t projector_p[3];
    uint16_t projector_u[3];
    uint16_t projector_v[3];
    uint16_t pad2;
};

_Static_assert(sizeof(struct TRSPK_VertexES3Os241Tfa) == 52u, "the Post-TFA OSRS world vertex is 52 bytes");
_Static_assert(
    offsetof(struct TRSPK_VertexES3Os241Tfa, rgba) == offsetof(struct TRSPK_VertexGLES2Os1, rgba) &&
        offsetof(struct TRSPK_VertexES3Os241Tfa, texcoord) ==
            offsetof(struct TRSPK_VertexGLES2Os1, texcoord) &&
        offsetof(struct TRSPK_VertexES3Os241Tfa, tile_col) ==
            offsetof(struct TRSPK_VertexGLES2Os1, tile_col) &&
        offsetof(struct TRSPK_VertexES3Os241Tfa, anim_u) == offsetof(struct TRSPK_VertexGLES2Os1, anim_u) &&
        offsetof(struct TRSPK_VertexES3Os241Tfa, anim_v) == offsetof(struct TRSPK_VertexGLES2Os1, anim_v),
    "the Post-TFA OSRS world vertex starts with the GLES2 one");
_Static_assert(
    offsetof(struct TRSPK_VertexES3Os241Tfa, depth_bias) ==
        offsetof(struct TRSPK_VertexES3Os235Sailing, depth_bias),
    "the Post-TFA OSRS world vertex starts with the Sailing one");

/* A float as an IEEE 754 half, rounded to nearest even; out of range
 * saturates to the largest finite half, and a NaN reads as 0. */
static inline uint16_t
trspk_float_to_half(float value)
{
    union
    {
        float f;
        uint32_t u;
    } bits;
    uint32_t sign;
    uint32_t mantissa;
    int exponent;

    bits.f = value;
    sign = (bits.u >> 16) & 0x8000u;
    exponent = (int)((bits.u >> 23) & 0xFFu) - 127 + 15;
    mantissa = bits.u & 0x7FFFFFu;
    if( ((bits.u >> 23) & 0xFFu) == 0xFFu )
        return mantissa ? 0u : (uint16_t)(sign | 0x7BFFu);
    if( exponent >= 31 )
        return (uint16_t)(sign | 0x7BFFu);
    if( exponent <= 0 )
    {
        uint32_t shift;
        uint32_t half_mantissa;
        if( exponent < -10 )
            return (uint16_t)sign;
        mantissa |= 0x800000u;
        shift = (uint32_t)(14 - exponent);
        half_mantissa = mantissa >> shift;
        if( (mantissa >> (shift - 1u)) & 1u && ((mantissa & ((1u << (shift - 1u)) - 1u)) || (half_mantissa & 1u)) )
            half_mantissa++;
        return (uint16_t)(sign | half_mantissa);
    }
    {
        uint32_t half = sign | ((uint32_t)exponent << 10) | (mantissa >> 13);
        uint32_t const rest = mantissa & 0x1FFFu;
        if( rest > 0x1000u || (rest == 0x1000u && (half & 1u)) )
            half++;
        if( (half & 0x7FFFu) >= 0x7C00u )
            half = sign | 0x7BFFu;
        return (uint16_t)half;
    }
}

#endif
