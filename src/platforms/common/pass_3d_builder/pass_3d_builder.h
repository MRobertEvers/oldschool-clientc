#pragma once

#include "platforms/common/buffered_face_order.h"

struct DashGraphics;
struct DashModel;
struct DashPosition;
struct DashViewPort;
struct DashCamera;

/**
 * Pass3DBuilder — frame-scoped accumulator for projected 3D model draws.
 *
 * Owns a BufferedFaceOrder internally.  The Submit functions wire it into
 * the backend render context and invoke the existing 3D flush routines.
 *
 * Usage:
 *   Pass3DBuilder b;
 *   b.begin_pass();
 *   b.set_track_legacy_models(false);
 *   bool ok = b.append_model(dash, model, pos, vp, cam,
 *                            chunk_index, vbo, vtx_base, face_count,
 *                            cos_yaw, sin_yaw, encoding);
 *   Pass3DBuilderSubmitMetal(b, ctx);   // or WebGL1 variant
 */
class Pass3DBuilder
{
public:
    void
    begin_pass();
    void
    set_track_legacy_models(bool on);
    bool
    append_model(
        struct DashGraphics* dash,
        struct DashModel* model,
        struct DashPosition* position,
        struct DashViewPort* view_port,
        struct DashCamera* camera,
        int batch_chunk_index,
        void* static_vbo,
        int vertex_index_base,
        int gpu_face_count,
        float cos_yaw,
        float sin_yaw,
        Gpu3DAngleEncoding angle_encoding);

    BufferedFaceOrder&
    face_order();
    bool
    empty() const;

private:
    BufferedFaceOrder bfo3d_;
};

// ---------------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------------

/** Reset internal face order; call at the start of each 3D pass. */
inline void
Pass3DBuilder::begin_pass()
{
    bfo3d_.begin_pass();
}

/**
 * When true, append_model also populates the legacy model list inside
 * BufferedFaceOrder (for CPU/software renderers).  Default: off.
 */
inline void
Pass3DBuilder::set_track_legacy_models(bool on)
{
    bfo3d_.set_track_legacy_models(on);
}

/**
 * Project and enqueue one model instance.
 *
 * Parameters mirror BufferedFaceOrder::append_model exactly.
 * Returns false if the instance table is full — caller should flush
 * (Pass3DBuilderSubmit*) then call begin_pass() before retrying.
 */
inline bool
Pass3DBuilder::append_model(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera,
    int batch_chunk_index,
    void* static_vbo,
    int vertex_index_base,
    int gpu_face_count,
    float cos_yaw,
    float sin_yaw,
    Gpu3DAngleEncoding angle_encoding)
{
    return bfo3d_.append_model(
        dash,
        model,
        position,
        view_port,
        camera,
        batch_chunk_index,
        static_vbo,
        vertex_index_base,
        gpu_face_count,
        cos_yaw,
        sin_yaw,
        angle_encoding);
}

inline BufferedFaceOrder&
Pass3DBuilder::face_order()
{
    return bfo3d_;
}

inline bool
Pass3DBuilder::empty() const
{
    return bfo3d_.stream().empty();
}
