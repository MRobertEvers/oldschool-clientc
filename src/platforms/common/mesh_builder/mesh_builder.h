#pragma once

#include <cstdint>
#include <vector>

/**
 * MeshVertex — shared vertex layout used by MeshBuilder.
 *
 * Memory layout is intentionally identical to MetalVertex (metal_internal.h)
 * and WgVertex (webgl1_vertex.h), so backend Submit functions can pass the
 * raw buffer directly to the GPU without conversion.  Each backend asserts
 * this at compile time with a static_assert on sizeof.
 */
struct MeshVertex
{
    float position[4]; ///< x, y, z, w  (w=1 for world geometry)
    float color[4];    ///< r, g, b, a
    float texcoord[2]; ///< u, v
    uint16_t tex_id;   ///< world atlas tile id (0 = none)
    uint16_t uv_mode;  ///< 0 = standard clamp/fract; 1 = VA fract tile
};

/**
 * MeshBuilder — CPU-side accumulator for indexed triangle geometry.
 *
 * Usage:
 *   MeshBuilder b;
 *   b.push_vertex({ ... });
 *   b.push_triangle(0, 1, 2);
 *   // then call the backend Submit function, e.g. MeshBuilderSubmitMetal(b, ctx)
 */
class MeshBuilder
{
public:
    void
    reset();
    uint32_t
    push_vertex(const MeshVertex& v);
    void
    push_triangle(
        uint32_t a,
        uint32_t b,
        uint32_t c);

    const std::vector<MeshVertex>&
    vertices() const;

    const std::vector<uint32_t>&
    indices() const;
    uint32_t
    vertex_count() const;
    uint32_t
    face_count() const;
    bool
    empty() const;

private:
    std::vector<MeshVertex> vertices_;
    std::vector<uint32_t> indices_;
};

// ---------------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------------

/** Discard all accumulated vertices and indices (keeps capacity). */
inline void
MeshBuilder::reset()
{
    vertices_.clear();
    indices_.clear();
}

/**
 * Append a vertex and return its index (suitable for push_triangle).
 * Returns UINT32_MAX on overflow (> 2^32-1 vertices; not expected in practice).
 */
inline uint32_t
MeshBuilder::push_vertex(const MeshVertex& v)
{
    const uint32_t idx = static_cast<uint32_t>(vertices_.size());
    vertices_.push_back(v);
    return idx;
}

/** Append a triangle using three previously-pushed vertex indices. */
inline void
MeshBuilder::push_triangle(
    uint32_t a,
    uint32_t b,
    uint32_t c)
{
    indices_.push_back(a);
    indices_.push_back(b);
    indices_.push_back(c);
}

/** All accumulated vertices (contiguous; safe to pass to glBufferData / MTLBuffer). */
inline const std::vector<MeshVertex>&
MeshBuilder::vertices() const
{
    return vertices_;
}

/** All accumulated indices (3 per triangle). */
inline const std::vector<uint32_t>&
MeshBuilder::indices() const
{
    return indices_;
}

inline uint32_t
MeshBuilder::vertex_count() const
{
    return static_cast<uint32_t>(vertices_.size());
}

/** Number of complete triangles (indices().size() / 3). */
inline uint32_t
MeshBuilder::face_count() const
{
    return static_cast<uint32_t>(indices_.size() / 3u);
}

inline bool
MeshBuilder::empty() const
{
    return vertices_.empty();
}
