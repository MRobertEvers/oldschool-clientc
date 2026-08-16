#ifndef TORIDRAW_TEXTURE_UV_H
#define TORIDRAW_TEXTURE_UV_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Per-vertex texture coordinates for model render types 0-3.
 *
 * ## Why this is here and not in the cache library
 *
 * It was in rscache next to the decoder, which is where the *fields* come from.
 * It belongs here for two reasons: it is a rendering concern, and it needs the
 * inverse-trig tables in graphics/shared_tables.h, which exist to keep the
 * result reproducible across platforms (libm's atan2 is not). rscache is a
 * standalone library and does not depend on ToriDraw, so the tables cannot go
 * the other way.
 *
 * The input is described as plain arrays rather than as a cache struct, so this
 * file has no dependency on any decoder. A caller fills the descriptor from
 * whatever model representation it has.
 *
 * ## What the render types are
 *
 * **Type 0** carries a *projector*: three vertex indices whose positions define
 * the texture plane. A rasterizer can walk that plane directly, which is what
 * the `texshadeblend` kernels do — they take orthographic p/m/n, not uv.
 *
 * **Types 1 (cylinder), 2 (cube) and 3 (sphere)** carry a *mapping* instead:
 * a projection about the face group's own centre, with a rotation, per-axis
 * scales, a scroll direction and a speed. Types 1 and 3 are non-linear in the
 * vertex position, so they cannot be expressed as a plane at all. Explicit uv
 * per face vertex is the only representation that covers all four, which is why
 * this produces that and the `texuv` kernels consume it.
 */

/** Everything the generator reads. Arrays it does not need may be NULL. */
struct ToriDraw_TextureUvSource
{
    const int* vertices_x;
    const int* vertices_y;
    const int* vertices_z;
    int vertex_count;

    const int* face_indices_a;
    const int* face_indices_b;
    const int* face_indices_c;
    /** -1 marks an untextured face; it gets zero uv. */
    const int16_t* face_textures;
    /** -1 marks a face with no mapping entry; it falls back to its own indices. */
    const int16_t* face_texture_coords;
    int face_count;

    int textured_face_count;
    /** For type 0 these are vertex indices. For types 1-3 they are a raw axis
     *  triple — see the decoder's note. Both are read as signed 16-bit. */
    const uint16_t* textured_p;
    const uint16_t* textured_m;
    const uint16_t* textured_n;
    const uint8_t* render_types;
    const int32_t* scale_x;
    const int32_t* scale_y;
    const int32_t* scale_z;
    const int8_t* rotation;
    const int8_t* direction;
    const int8_t* speed;
    const int8_t* trans_u;
    const int8_t* trans_v;
};

/**
 * Write `face_count * 6` floats: u0, v0, u1, v1, u2, v2 per face, in face order.
 *
 * uv is in tile units — 1.0 is one full wrap of the texture, not one texel — and
 * is neither wrapped nor clamped, so the sampler's addressing mode decides what
 * a value outside [0,1) means. An untextured face writes six zeroes.
 *
 * Requires ToriDraw_InitAtanTable(). Returns false only on a NULL argument or a
 * source with no face textures.
 */
bool
ToriDraw_ComputeTextureUv(
    const struct ToriDraw_TextureUvSource* src,
    float* out_uv);

/**
 * The per-textured-face mapping basis, exposed for tests and diagnostics.
 *
 * `centre` is the midpoint of the bounding box of every vertex used by a face
 * naming this textured-face index — a property of the face *group*, not of any
 * one triangle. `matrix` takes a centred vertex into mapping space. Only filled
 * for render types above 0.
 */
struct ToriDraw_TextureUvBasis
{
    int centre_x;
    int centre_y;
    int centre_z;
    float matrix[9];
    bool valid;
};

/** `out_basis` must hold `textured_face_count` entries. */
bool
ToriDraw_ComputeTextureUvBases(
    const struct ToriDraw_TextureUvSource* src,
    struct ToriDraw_TextureUvBasis* out_basis);

#endif
