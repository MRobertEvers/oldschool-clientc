#pragma once

#include "graphics/dash.h"
#include "platforms/gpu_3d_cache.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/** One expanded corner: full 32-bit vertex index into static VBO + instance id (max 4096/pass). */
struct DrawStreamEntry
{
    uint32_t vertex_index;
    uint32_t instance_id;
};

/** 32 bytes; must match `InstanceXform` in Shaders.metal.
 *  cos_yaw/sin_yaw are CPU-prebaked for Y-axis rotation (projection.u.c / D3D11 xz step); GPU
 *  only applies multiply-add. */
struct InstanceXform
{
    float cos_yaw;
    float sin_yaw;
    float x, y, z;
    uint32_t angle_encoding;
    int32_t _pad[2];
};

static_assert(sizeof(InstanceXform) == 32, "InstanceXform size must match Metal shader");

struct PassFlushSlice
{
    int chunk_index; ///< >=0 batched chunk; -1 standalone / non-chunk static VBO
    void* vbo_handle;
    uint32_t entry_offset; ///< index into DrawStreamEntry stream
    uint32_t entry_count;  ///< number of DrawStreamEntry (3 per visible triangle)
};

/**
 * Frame-scoped accumulator: projected face order + draw stream for bindless world draws.
 * Call begin_pass at BEGIN_3D (or after a mid-pass flush).
 */
class BufferedFaceOrder
{
public:
    static constexpr int kMaxInstancesPerPass = 4096;

    /** When true, append_model fills legacy_models() for CPU/legacy renderers (default off). */
    void
    set_track_legacy_models(bool on)
    {
        track_legacy_models_ = on;
    }

    void
    begin_pass()
    {
        stream_.clear();
        instances_.clear();
        slices_.clear();
        legacy_models_.clear();
        cur_vbo_ = nullptr;
        cur_chunk_ = -999999;
        finalized_ = false;
    }

    /**
     * @param batch_chunk_index batch chunk index, or -1 when drawing from a standalone VBO path
     * @param static_vbo        id<MTLBuffer> bridged as void* — static geometry for this slice
     * @param vertex_index_base first vertex index in static VBO for this model's geometry
     * @param gpu_face_count    model face count used for bounds checks on projected indices
     * @param cos_yaw / sin_yaw  CPU-prebaked trig for Y rotation (Metal: from
     *                           `metal_prebake_model_yaw_cos_sin` / Dash yaw → radians).
     * @param angle_encoding     must match Gpu3DCache / model entry (metadata for future paths).
     * @return false if instance table full (caller should flush + begin_pass + retry)
     */
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
        Gpu3DAngleEncoding angle_encoding)
    {
        if( !dash || !model || !position || !view_port || !camera || !static_vbo || gpu_face_count <= 0 )
            return true;

        if( (int)instances_.size() >= kMaxInstancesPerPass )
            return false;

        const int prep = dash3d_prepare_projected_face_order(dash, model, position, view_port, camera);
        if( prep <= 0 )
            return true;

        int face_count = 0;
        const int* faces = dash3d_projected_face_order(dash, &face_count);
        if( face_count <= 0 || !faces )
            return true;

        if( (int)face_order_scratch_.size() < face_count )
            face_order_scratch_.resize((size_t)face_count);
        memcpy(face_order_scratch_.data(), faces, sizeof(int) * (size_t)face_count);

        int emit_tris = 0;
        for( int i = 0; i < face_count; ++i )
        {
            const int f = face_order_scratch_[(size_t)i];
            if( f >= 0 && f < gpu_face_count )
                ++emit_tris;
        }
        if( emit_tris <= 0 )
            return true;

        InstanceXform xf = {
            cos_yaw,
            sin_yaw,
            (float)position->x,
            (float)position->y,
            (float)position->z,
            static_cast<uint32_t>(angle_encoding),
            { 0, 0 },
        };
        const int inst_id = (int)instances_.size();
        instances_.push_back(xf);
        const uint32_t inst_u = (uint32_t)inst_id;

        const uint32_t stream_start = (uint32_t)stream_.size();
        open_slice_if_needed(batch_chunk_index, static_vbo, stream_start);

        for( int i = 0; i < face_count; ++i )
        {
            const int f = face_order_scratch_[(size_t)i];
            if( f < 0 || f >= gpu_face_count )
                continue;
            for( int k = 0; k < 3; ++k )
            {
                const uint32_t vid = (uint32_t)(vertex_index_base + f * 3 + k);
                stream_.push_back(DrawStreamEntry{ vid, inst_u });
            }
        }

        if( track_legacy_models_ )
        {
            std::vector<int> face_copy((size_t)face_count);
            memcpy(face_copy.data(), face_order_scratch_.data(), sizeof(int) * (size_t)face_count);
            legacy_models_.push_back(AppendedModel{ inst_id,
                                                    batch_chunk_index,
                                                    static_vbo,
                                                    vertex_index_base,
                                                    gpu_face_count,
                                                    std::move(face_copy) });
        }
        return true;
    }

    int
    instance_count() const
    {
        return (int)instances_.size();
    }

    const std::vector<DrawStreamEntry>&
    stream() const
    {
        return stream_;
    }

    const std::vector<InstanceXform>&
    instances() const
    {
        return instances_;
    }

    /** Finalize tail slice entry_count; call once before reading slices(). */
    void
    finalize_slices()
    {
        if( finalized_ || slices_.empty() )
            return;
        const uint32_t n = (uint32_t)stream_.size();
        for( size_t i = 0; i < slices_.size(); ++i )
        {
            PassFlushSlice& s = slices_[i];
            const uint32_t next_off =
                (i + 1 < slices_.size()) ? slices_[i + 1].entry_offset : n;
            s.entry_count = next_off - s.entry_offset;
        }
        finalized_ = true;
    }

    const std::vector<PassFlushSlice>&
    slices() const
    {
        return slices_;
    }

    struct AppendedModel
    {
        int instance_id;
        int chunk_index;
        void* vbo;
        int vertex_index_base;
        int face_count;
        std::vector<int> sorted_faces;
    };

    const std::vector<AppendedModel>&
    models() const
    {
        return legacy_models_;
    }

private:
    void
    open_slice_if_needed(int batch_chunk_index, void* static_vbo, uint32_t stream_pos)
    {
        if( static_vbo == cur_vbo_ && batch_chunk_index == cur_chunk_ )
            return;
        if( !slices_.empty() )
        {
            PassFlushSlice& back = slices_.back();
            back.entry_count = stream_pos - back.entry_offset;
        }
        PassFlushSlice s;
        s.chunk_index = batch_chunk_index;
        s.vbo_handle = static_vbo;
        s.entry_offset = stream_pos;
        s.entry_count = 0;
        slices_.push_back(s);
        cur_vbo_ = static_vbo;
        cur_chunk_ = batch_chunk_index;
    }

    std::vector<DrawStreamEntry> stream_;
    std::vector<InstanceXform> instances_;
    std::vector<PassFlushSlice> slices_;
    std::vector<AppendedModel> legacy_models_;
    std::vector<int> face_order_scratch_;
    void* cur_vbo_ = nullptr;
    int cur_chunk_ = -999999;
    bool finalized_ = false;
    bool track_legacy_models_ = false;
};
