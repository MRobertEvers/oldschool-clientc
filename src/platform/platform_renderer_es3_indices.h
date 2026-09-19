#ifndef TORIRS_ES3_INDICES_H
#define TORIRS_ES3_INDICES_H

#include <stdbool.h>
#include <stdint.h>
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

/*
 * The painter's index packer: one model's sorted face order turned into
 * triangle indices.
 *
 * `address` is the model's first vertex IN ITS BINDING'S BUFFER. There is no
 * window to be relative to -- the index is 32-bit and reaches the whole
 * buffer -- which is the difference that lets the painter here index the
 * retained world where the GLES2 renderer has to copy it into a ring first.
 *
 * A face the order names but the bake cannot supply (the order names faces
 * by their index in the MODEL, which runs past what was baked) becomes a
 * degenerate triangle on the model's first vertex, so the run stays exactly
 * `count * 3` indices long and the caller's reservation is never wrong.
 *
 * This used to say "no SIMD arm: ... this lane is wasm", which stopped being
 * true when the ES 3.0 core became Android's --gles3. On armv7 it is the same
 * loop the ES2 packer needed NEON for -- "a measurable slice of a 2013
 * phone's frame" -- and it moves TWICE the bytes there, because a 32-bit
 * index is twelve bytes a face where the U16 window's is six. So it gets the
 * same treatment, four faces a step rather than eight (u32 lanes, not u16).
 *
 * The scalar loop below is the reference AND the control arm: both produce
 * byte-identical indices, so `use_neon` can be flipped to A/B it. Per lane:
 * in = face < limit (an all-ones mask), vertex = address + (face * 3 & in),
 * step = in >> 31, and vst3q_u32 interleaves {vertex, vertex + step,
 * vertex + 2 step} exactly as the scalar's triplet[0..2] lays them out.
 * A negative face reads as a huge unsigned one and fails the compare, as it
 * does in the scalar's unsigned compare, and its masked product is unused.
 */
static inline void
es3_painter_write_indices_ex(
    uint32_t* indices,
    uint32_t address,
    uint32_t source_face_limit,
    const int* faces,
    uint32_t count,
    bool use_neon)
{
    uint32_t index = 0u;
    (void)use_neon;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    if( use_neon )
    {
        const uint32x4_t limit4 = vdupq_n_u32(source_face_limit);
        const uint32x4_t address4 = vdupq_n_u32(address);
        const uint32x4_t three4 = vdupq_n_u32(3u);
        for( ; index + 4u <= count; index += 4u )
        {
            uint32x4_t face = vreinterpretq_u32_s32(vld1q_s32(faces + index));
            uint32x4_t in = vcltq_u32(face, limit4);
            uint32x4_t vertex =
                vaddq_u32(address4, vandq_u32(vmulq_u32(face, three4), in));
            uint32x4_t step = vshrq_n_u32(in, 31);
            uint32x4x3_t triplet;
            triplet.val[0] = vertex;
            triplet.val[1] = vaddq_u32(vertex, step);
            triplet.val[2] = vaddq_u32(vertex, vaddq_u32(step, step));
            vst3q_u32(indices + index * 3u, triplet);
        }
    }
#endif
    for( ; index < count; index++ )
    {
        uint32_t face = (uint32_t)faces[index];
        uint32_t inside = face < source_face_limit ? 1u : 0u;
        uint32_t vertex = address + (inside ? face * 3u : 0u);
        uint32_t* triplet = indices + (index * 3u);
        triplet[0] = vertex;
        triplet[1] = vertex + inside;
        triplet[2] = vertex + inside + inside;
    }
}

/** The scalar reference, kept as its own name for the test and the replay. */
static inline void
es3_painter_write_indices(
    uint32_t* indices,
    uint32_t address,
    uint32_t source_face_limit,
    const int* faces,
    uint32_t count)
{
    es3_painter_write_indices_ex(indices, address, source_face_limit, faces, count, false);
}

#endif
