#pragma once

#include "graphics/dash.h"

#include <cstdint>

/**
 * Wraps dash3d projected face order for indexed draws (Metal, GL, D3D, etc.).
 * Valid only until the next dash3d_prepare / projection that touches the same DashGraphics.
 */
class SortedFaceOrder
{
public:
    void
    prepare(
        struct DashGraphics* dash,
        struct DashModel* model,
        struct DashPosition* position,
        struct DashViewPort* view_port,
        struct DashCamera* camera)
    {
        face_count_ = 0;
        faces_ = nullptr;
        if( !dash || !model || !position || !view_port || !camera )
            return;
        face_count_ =
            dash3d_prepare_projected_face_order(dash, model, position, view_port, camera);
        faces_ = dash3d_projected_face_order(dash, &face_count_);
    }

    int
    count() const
    {
        return face_count_;
    }

    const int*
    faces() const
    {
        return faces_;
    }

    /** Writes triangle list indices (3 per visible face). Returns number of uint32 written. */
    size_t
    emit_indices(
        uint32_t* dst,
        int vertex_index_base,
        int gpu_face_count) const
    {
        if( !dst || face_count_ <= 0 || !faces_ )
            return 0;
        size_t w = 0;
        for( int i = 0; i < face_count_; ++i )
        {
            const int f = faces_[i];
            if( f < 0 || f >= gpu_face_count )
                continue;
            dst[w++] = (uint32_t)(vertex_index_base + f * 3 + 0);
            dst[w++] = (uint32_t)(vertex_index_base + f * 3 + 1);
            dst[w++] = (uint32_t)(vertex_index_base + f * 3 + 2);
        }
        return w;
    }

private:
    int face_count_ = 0;
    const int* faces_ = nullptr;
};
