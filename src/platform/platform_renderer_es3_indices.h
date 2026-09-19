#ifndef TORIRS_ES3_INDICES_H
#define TORIRS_ES3_INDICES_H

#include <stdint.h>

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
 * No SIMD arm: the ES2 renderer has a NEON one because this loop is a
 * measurable slice of a 2013 phone's frame. This lane is wasm, where the
 * autovectorizer gets the same shape out of the scalar loop, and where the
 * work itself is far smaller -- the painter no longer re-emits the whole
 * visible world as vertices, so what is left here is six bytes a face.
 */
static inline void
es3_painter_write_indices(
    uint32_t* indices,
    uint32_t address,
    uint32_t source_face_limit,
    const int* faces,
    uint32_t count)
{
    uint32_t index;
    for( index = 0u; index < count; index++ )
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

#endif
