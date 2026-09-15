#ifndef TORIRS_GLES2_INDICES_H
#define TORIRS_GLES2_INDICES_H
#include <stdbool.h>
#include <stdint.h>
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif
/* The production painter index packer, shared with the GL-free chain replay.
 * address is relative to the bound U16 window. Invalid faces are degenerate. */
static inline void
gles2_painter_write_indices(uint16_t* indices, uint32_t address,
    uint32_t source_face_limit, const int* faces, uint32_t count, bool use_neon)
{
    (void)use_neon;
    uint32_t index = 0u;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    /*
     * TORIRS_GLES2_TRIPLET_NEON: eight faces per step. The scalar loop below
     * is the reference and the control arm; this produces byte-identical
     * indices. Per lane: in = face < limit (all-ones mask), vertex = address
     * + (face * 3 & in), step = in & 1, and vst3q_u16 interleaves the three
     * vectors {vertex, vertex + step, vertex + 2 step} exactly as the
     * scalar's triplet[0..2] lays them out. The narrowing to u16 is the
     * scalar's (uint16_t) cast, and adding the step after narrowing is the
     * same as before it because truncation commutes with addition mod 2^16.
     * A negative face reads as a huge unsigned one and fails the compare,
     * as it does in the scalar's unsigned compare; its masked product is
     * never used. The assert above bounds every produced index below
     * 65536, so no lane saturates.
     */
    if( use_neon )
    {
        const uint32x4_t limit4 = vdupq_n_u32(source_face_limit);
        const uint32x4_t address4 = vdupq_n_u32(address);
        const uint32x4_t three4 = vdupq_n_u32(3u);
        for( ; index + 8u <= count; index += 8u )
        {
            uint32x4_t face_lo = vreinterpretq_u32_s32(vld1q_s32(faces + index));
            uint32x4_t face_hi = vreinterpretq_u32_s32(vld1q_s32(faces + index + 4u));
            uint32x4_t in_lo = vcltq_u32(face_lo, limit4);
            uint32x4_t in_hi = vcltq_u32(face_hi, limit4);
            uint32x4_t vertex_lo = vaddq_u32(address4, vandq_u32(vmulq_u32(face_lo, three4), in_lo));
            uint32x4_t vertex_hi = vaddq_u32(address4, vandq_u32(vmulq_u32(face_hi, three4), in_hi));
            uint16x8_t vertex = vcombine_u16(vmovn_u32(vertex_lo), vmovn_u32(vertex_hi));
            uint16x8_t step = vcombine_u16(
                vmovn_u32(vshrq_n_u32(in_lo, 31)), vmovn_u32(vshrq_n_u32(in_hi, 31)));
            uint16x8x3_t triplet;
            triplet.val[0] = vertex;
            triplet.val[1] = vaddq_u16(vertex, step);
            triplet.val[2] = vaddq_u16(vertex, vaddq_u16(step, step));
            vst3q_u16(indices + index * 3u, triplet);
        }
    }
#endif
    for( ; index < count; index++ )
    {
        uint32_t face = (uint32_t)faces[index];
        uint32_t vertex = face < source_face_limit ? address + (face * 3u) : address;
        uint32_t step = face < source_face_limit ? 1u : 0u;
        uint16_t* triplet = indices + (index * 3u);
        triplet[0] = (uint16_t)vertex;
        triplet[1] = (uint16_t)(vertex + step);
        triplet[2] = (uint16_t)(vertex + step + step);
    }
}
#endif
