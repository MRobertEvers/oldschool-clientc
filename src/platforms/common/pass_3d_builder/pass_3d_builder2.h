#ifndef PASS_3D_BUILDER2_H
#define PASS_3D_BUILDER2_H

#include "platforms/common/gpu_3d.h"
#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

/** `uint16_t` indices; must match `mtl_pass3d_index_buf` size in Metal init.
 *  One frame can sort many models × faces × 3; 64Ki entries was too small for world draws. */
inline constexpr uint32_t kPass3DBuilder2DynamicIndexUInt16Capacity = 1u << 21; // 2_097_152

/** Max CPU→GPU bytes for one 3D pass static scenery coalesce vertex stream (portable backends). */
inline constexpr size_t kPass3DBuilder2CoalescedVtxBufBytes = 32u * 1024u * 1024u;

/** `AddModelDrawYawOnly(..., usage_hint)` when unknown: do not take static coalesce fast path. */
inline constexpr uint8_t kPass3DUsageHintUnspecified = 255u;

struct DrawModel3D
{
    uint16_t model_id;
    bool use_animation;
    uint8_t animation_index;
    uint8_t frame_index;

    // Rotation/position data
    uint32_t instance_offset;

    // Sorted faces
    uint32_t dynamic_index_offset;
    uint32_t dynamic_index_count;

    DrawModel3D(
        uint16_t model_id,
        bool use_animation,
        uint8_t animation_index,
        uint8_t frame_index,
        uint32_t instance_offset,
        uint32_t dynamic_index_offset,
        uint32_t dynamic_index_count)
        : model_id(model_id)
        , use_animation(use_animation)
        , animation_index(animation_index)
        , frame_index(frame_index)
        , instance_offset(instance_offset)
        , dynamic_index_offset(dynamic_index_offset)
        , dynamic_index_count(dynamic_index_count)
    {}

    static DrawModel3D
    Create(
        uint16_t model_id,
        bool use_animation,
        uint8_t animation_index,
        uint8_t frame_index,
        uint32_t instance_offset,
        uint32_t dynamic_index_offset,
        uint32_t dynamic_index_count)
    {
        return DrawModel3D(
            model_id,
            use_animation,
            animation_index,
            frame_index,
            instance_offset,
            dynamic_index_offset,
            dynamic_index_count);
    }
};

/** Draw commands for one GPUBatch (`gpu_batch_id` from `GPUModelPosedData`), filled at record time.
 */
struct Pass3DCommandGPUBatch
{
    GPUBatchId gpu_batch_id = 0u;
    std::vector<DrawModel3D> draws;
};

/** One visible static scenery draw recorded for coalescing before GPU submit. */
struct Pass3DStaticCoalesceOp
{
    GPUBatchId gpu_batch_id = 0u;
    SceneBatchId scene_batch_id = kSceneBatchSlotNone;
    uint32_t vbo_offset = 0u;
    /** Max local vertex index in `sorted_indices` is `< model_vertex_span` (expanded mesh). */
    uint32_t model_vertex_span = 0u;
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    int rotation_r2pi2048 = 0;
    std::vector<uint16_t> sorted_indices;
};

/** One `drawIndexed*` range after coalescing all ops with the same `gpu_batch_id`. */
struct Pass3DStaticCoalesceSpan
{
    GPUBatchId gpu_batch_id = 0u;
    uint32_t idx_word_first = 0u;
    uint32_t idx_word_count = 0u;
    uint32_t vtx_first = 0u;
    uint32_t vtx_count = 0u;
};

namespace pass3d_builder2_detail
{

template<typename InstanceUniformType, typename VertexType>
inline void
ApplyYawTranslationToVertex(VertexType& v, const InstanceUniformType& t)
{
    const float px = v.position[0];
    const float py = v.position[1];
    const float pz = v.position[2];
    const float xr = px * t.cos_yaw + pz * t.sin_yaw;
    const float zr = -px * t.sin_yaw + pz * t.cos_yaw;
    v.position[0] = xr + t.x;
    v.position[1] = py + t.y;
    v.position[2] = zr + t.z;
}

} // namespace pass3d_builder2_detail

template<typename InstanceUniformType>
class Pass3DBuilder2
{
private:
    std::vector<InstanceUniformType> instance_pool;

    // A contiguous pool holding ALL sorted/dynamic indices for the current frame.
    // The backend will upload this entire vector in one swift API call.
    std::vector<uint16_t> indices_pool;

    bool is_building;

    std::vector<Pass3DCommandGPUBatch> gpu_command_batches_;

    std::vector<Pass3DStaticCoalesceOp> static_coalesce_ops_;
    std::vector<Pass3DStaticCoalesceSpan> static_coalesce_spans_;
    std::vector<uint8_t> static_coalesce_built_vtx_;
    std::vector<uint16_t> static_coalesce_built_idx_;
    uint32_t static_coalesce_index_prefix_words_ = 0u;
    size_t static_coalesce_estimated_vtx_bytes_ = 0u;
    size_t static_coalesce_estimated_idx_words_ = 0u;

    void
    append_draw_to_gpu_batch(
        GPUBatchId gpu_batch_id,
        const DrawModel3D& cmd);

public:
    Pass3DBuilder2();
    ~Pass3DBuilder2();

    // Called on TORIRS_GFX_STATE_BEGIN_3D
    void
    Begin3D();

    /** Record a draw using `cache` to resolve `GPUModelPosedData` and route into the correct
     *  GPUBatch buffer (`pose.gpu_batch_id`). Skips if pose is invalid or `gpu_batch_id` is 0. */
    template<typename VertexType>
    void
    AddModelDrawYawOnly(
        const GPU3DCache2<VertexType>& cache,
        uint16_t model_id,
        int32_t x,
        int32_t y,
        int32_t z,
        int rotation_r2pi2048,
        bool use_animation,
        uint8_t animation_index,
        uint8_t frame_index,
        uint16_t* sorted_indices = nullptr,
        uint32_t index_count = 0,
        uint8_t usage_hint = kPass3DUsageHintUnspecified);

    void
    End3D();

    /** Clears recorded commands and index pool after a successful GPU submit. */
    void
    ClearAfterSubmit();

    /** Build `static_coalesce_*` from `static_coalesce_ops_` (call once per submit before upload). */
    template<typename VertexType>
    void
    FinalizeStaticCoalesceStreamsForSubmit(const GPU3DCache2<VertexType>& cache);

    bool
    HasStaticCoalesceDraws() const;

    const std::vector<Pass3DStaticCoalesceSpan>&
    GetStaticCoalesceSpans() const;

    const std::vector<uint8_t>&
    GetStaticCoalesceBuiltVertices() const;

    const std::vector<uint16_t>&
    GetStaticCoalesceBuiltIndices() const;

    uint32_t
    GetStaticCoalesceIndexPrefixWords() const;

    const std::vector<Pass3DCommandGPUBatch>&
    GetGpuBatches() const;

    const std::vector<uint16_t>&
    GetDynamicIndices() const;

    const uint32_t
    GetDynamicIndicesSize() const;

    const std::vector<InstanceUniformType>&
    GetInstancePool() const;

    uint32_t
    GetInstancePoolSize() const;

    bool
    HasCommands() const;

    bool
    HasDynamicIndices() const;
};

template<typename InstanceUniformType>
inline void
Pass3DBuilder2<InstanceUniformType>::append_draw_to_gpu_batch(
    GPUBatchId gpu_batch_id,
    const DrawModel3D& cmd)
{
    for( Pass3DCommandGPUBatch& b : gpu_command_batches_ )
    {
        if( b.gpu_batch_id == gpu_batch_id )
        {
            b.draws.push_back(cmd);
            return;
        }
    }
    Pass3DCommandGPUBatch slot;
    slot.gpu_batch_id = gpu_batch_id;
    slot.draws.push_back(cmd);
    gpu_command_batches_.push_back(std::move(slot));
}

template<typename InstanceUniformType>
inline void
Pass3DBuilder2<InstanceUniformType>::Begin3D()
{
    is_building = true;
    instance_pool.clear();
    indices_pool.clear();
    gpu_command_batches_.clear();
    static_coalesce_ops_.clear();
    static_coalesce_spans_.clear();
    static_coalesce_built_vtx_.clear();
    static_coalesce_built_idx_.clear();
    static_coalesce_index_prefix_words_ = 0u;
    static_coalesce_estimated_vtx_bytes_ = 0u;
    static_coalesce_estimated_idx_words_ = 0u;
}

template<typename InstanceUniformType>
inline Pass3DBuilder2<InstanceUniformType>::Pass3DBuilder2()
    : is_building(false)
{
    instance_pool.reserve(4096);
    indices_pool.reserve(4096 * 16);
    gpu_command_batches_.reserve(64u);
}

template<typename InstanceUniformType>
inline Pass3DBuilder2<InstanceUniformType>::~Pass3DBuilder2()
{}

template<typename InstanceUniformType>
template<typename VertexType>
inline void
Pass3DBuilder2<InstanceUniformType>::AddModelDrawYawOnly(
    const GPU3DCache2<VertexType>& cache,
    uint16_t model_id,
    int32_t x,
    int32_t y,
    int32_t z,
    int rotation_r2pi2048,
    bool use_animation,
    uint8_t animation_index,
    uint8_t frame_index,
    uint16_t* sorted_indices,
    uint32_t index_count,
    uint8_t usage_hint)
{
    if( !is_building )
        return;

    const GPUModelPosedData pose =
        cache.GetModelPoseForDraw(model_id, use_animation, (int)animation_index, (int)frame_index);
    if( !pose.valid || pose.gpu_batch_id == 0u )
        return;

    const bool try_coalesce =
        usage_hint == (uint8_t)TORIRS_USAGE_SCENERY && !use_animation && sorted_indices != nullptr &&
        index_count > 0u && (index_count % 3u) == 0u && pose.scene_batch_id < kGPU3DCache2MaxSceneBatches;

    if( try_coalesce )
    {
        uint32_t max_local = 0u;
        for( uint32_t ii = 0; ii < index_count; ++ii )
        {
            const uint16_t li = sorted_indices[ii];
            max_local = li > max_local ? li : max_local;
        }
        const uint32_t model_vertex_span = pose.element_count;
        if( max_local >= model_vertex_span )
            goto legacy_draw;

        const GPU3DCache2SceneBatchEntry* be = cache.SceneBatchGet(pose.scene_batch_id);
        const uint32_t vtx_stride = static_cast<uint32_t>(sizeof(VertexType));
        if( !be || be->merged_vbo_cpu_snapshot.empty() || be->merged_vbo_vertex_stride_bytes != vtx_stride ||
            be->merged_vbo_vertex_count == 0u )
            goto legacy_draw;

        const uint64_t abs_last =
            (uint64_t)pose.vbo_offset + (uint64_t)max_local + 1uLL; // exclusive end index
        if( abs_last > (uint64_t)be->merged_vbo_vertex_count )
            goto legacy_draw;

        const size_t worst_vtx = (size_t)index_count * (size_t)vtx_stride;
        const size_t worst_idx = (size_t)index_count;
        if( static_coalesce_estimated_vtx_bytes_ + worst_vtx > kPass3DBuilder2CoalescedVtxBufBytes ||
            static_coalesce_estimated_idx_words_ + worst_idx >
                (size_t)kPass3DBuilder2DynamicIndexUInt16Capacity )
            goto legacy_draw;

        Pass3DStaticCoalesceOp op;
        op.gpu_batch_id = pose.gpu_batch_id;
        op.scene_batch_id = pose.scene_batch_id;
        op.vbo_offset = pose.vbo_offset;
        op.model_vertex_span = model_vertex_span;
        op.x = x;
        op.y = y;
        op.z = z;
        op.rotation_r2pi2048 = rotation_r2pi2048;
        op.sorted_indices.assign(sorted_indices, sorted_indices + index_count);
        static_coalesce_ops_.push_back(std::move(op));
        static_coalesce_estimated_vtx_bytes_ += worst_vtx;
        static_coalesce_estimated_idx_words_ += worst_idx;
        return;
    }

legacy_draw:
    uint32_t instance_offset = static_cast<uint32_t>(instance_pool.size());
    instance_pool.push_back(InstanceUniformType::FromYawOnly(x, y, z, rotation_r2pi2048));

    uint32_t index_pool_offset = 0;
    if( sorted_indices != nullptr && index_count > 0 )
    {
        index_pool_offset = static_cast<uint32_t>(indices_pool.size());
        indices_pool.resize(index_pool_offset + index_count);
        std::memcpy(
            indices_pool.data() + index_pool_offset,
            sorted_indices,
            index_count * sizeof(uint16_t));
    }

    const DrawModel3D draw = DrawModel3D::Create(
        model_id,
        use_animation,
        animation_index,
        frame_index,
        instance_offset,
        index_pool_offset,
        index_count);

    append_draw_to_gpu_batch(pose.gpu_batch_id, draw);
}

template<typename InstanceUniformType>
inline void
Pass3DBuilder2<InstanceUniformType>::End3D()
{
    is_building = false;
}

template<typename InstanceUniformType>
inline void
Pass3DBuilder2<InstanceUniformType>::ClearAfterSubmit()
{
    instance_pool.clear();
    indices_pool.clear();
    gpu_command_batches_.clear();
    static_coalesce_ops_.clear();
    static_coalesce_spans_.clear();
    static_coalesce_built_vtx_.clear();
    static_coalesce_built_idx_.clear();
    static_coalesce_index_prefix_words_ = 0u;
    static_coalesce_estimated_vtx_bytes_ = 0u;
    static_coalesce_estimated_idx_words_ = 0u;
}

template<typename InstanceUniformType>
inline const std::vector<Pass3DCommandGPUBatch>&
Pass3DBuilder2<InstanceUniformType>::GetGpuBatches() const
{
    return gpu_command_batches_;
}

template<typename InstanceUniformType>
inline const std::vector<uint16_t>&
Pass3DBuilder2<InstanceUniformType>::GetDynamicIndices() const
{
    return indices_pool;
}

template<typename InstanceUniformType>
inline const uint32_t
Pass3DBuilder2<InstanceUniformType>::GetDynamicIndicesSize() const
{
    return static_cast<uint32_t>(indices_pool.size());
}

template<typename InstanceUniformType>
inline bool
Pass3DBuilder2<InstanceUniformType>::HasCommands() const
{
    if( !static_coalesce_ops_.empty() )
        return true;
    for( const Pass3DCommandGPUBatch& b : gpu_command_batches_ )
    {
        if( !b.draws.empty() )
            return true;
    }
    return false;
}

template<typename InstanceUniformType>
inline bool
Pass3DBuilder2<InstanceUniformType>::HasDynamicIndices() const
{
    return !indices_pool.empty();
}

template<typename InstanceUniformType>
inline const std::vector<InstanceUniformType>&
Pass3DBuilder2<InstanceUniformType>::GetInstancePool() const
{
    return instance_pool;
}

template<typename InstanceUniformType>
inline uint32_t
Pass3DBuilder2<InstanceUniformType>::GetInstancePoolSize() const
{
    return static_cast<uint32_t>(instance_pool.size());
}

template<typename InstanceUniformType>
inline bool
Pass3DBuilder2<InstanceUniformType>::HasStaticCoalesceDraws() const
{
    return !static_coalesce_spans_.empty();
}

template<typename InstanceUniformType>
inline const std::vector<Pass3DStaticCoalesceSpan>&
Pass3DBuilder2<InstanceUniformType>::GetStaticCoalesceSpans() const
{
    return static_coalesce_spans_;
}

template<typename InstanceUniformType>
inline const std::vector<uint8_t>&
Pass3DBuilder2<InstanceUniformType>::GetStaticCoalesceBuiltVertices() const
{
    return static_coalesce_built_vtx_;
}

template<typename InstanceUniformType>
inline const std::vector<uint16_t>&
Pass3DBuilder2<InstanceUniformType>::GetStaticCoalesceBuiltIndices() const
{
    return static_coalesce_built_idx_;
}

template<typename InstanceUniformType>
inline uint32_t
Pass3DBuilder2<InstanceUniformType>::GetStaticCoalesceIndexPrefixWords() const
{
    return static_coalesce_index_prefix_words_;
}

template<typename InstanceUniformType>
template<typename VertexType>
inline void
Pass3DBuilder2<InstanceUniformType>::FinalizeStaticCoalesceStreamsForSubmit(
    const GPU3DCache2<VertexType>& cache)
{
    static_coalesce_spans_.clear();
    static_coalesce_built_vtx_.clear();
    static_coalesce_built_idx_.clear();
    static_coalesce_index_prefix_words_ = 0u;

    if( static_coalesce_ops_.empty() )
        return;

    std::stable_sort(
        static_coalesce_ops_.begin(),
        static_coalesce_ops_.end(),
        [](const Pass3DStaticCoalesceOp& a, const Pass3DStaticCoalesceOp& b) {
            return a.gpu_batch_id < b.gpu_batch_id;
        });

    const uint32_t vtx_stride = static_cast<uint32_t>(sizeof(VertexType));
    std::vector<uint32_t> local_remap;

    GPUBatchId cur_batch = 0u;
    bool have_span = false;
    size_t span_idx_word_start = 0;
    size_t span_vtx_byte_start = 0;

    auto flush_span = [&](GPUBatchId batch_id, size_t idx_end, size_t vtx_end_bytes) {
        if( !have_span )
            return;
        const size_t idx_n = idx_end - span_idx_word_start;
        const size_t vtx_n_bytes = vtx_end_bytes - span_vtx_byte_start;
        if( idx_n == 0u || vtx_n_bytes < (size_t)vtx_stride )
            return;
        Pass3DStaticCoalesceSpan sp{};
        sp.gpu_batch_id = batch_id;
        sp.idx_word_first = static_cast<uint32_t>(span_idx_word_start);
        sp.idx_word_count = static_cast<uint32_t>(idx_n);
        sp.vtx_first = static_cast<uint32_t>(span_vtx_byte_start / (size_t)vtx_stride);
        sp.vtx_count = static_cast<uint32_t>(vtx_n_bytes / (size_t)vtx_stride);
        static_coalesce_spans_.push_back(sp);
    };

    for( size_t oi = 0; oi < static_coalesce_ops_.size(); ++oi )
    {
        Pass3DStaticCoalesceOp& op = static_coalesce_ops_[oi];
        const GPUBatchId batch_id = op.gpu_batch_id;

        if( !have_span || batch_id != cur_batch )
        {
            flush_span(cur_batch, static_coalesce_built_idx_.size(), static_coalesce_built_vtx_.size());
            cur_batch = batch_id;
            have_span = true;
            span_idx_word_start = static_coalesce_built_idx_.size();
            span_vtx_byte_start = static_coalesce_built_vtx_.size();
        }

        const GPU3DCache2SceneBatchEntry* be = cache.SceneBatchGet(op.scene_batch_id);
        if( !be || be->merged_vbo_cpu_snapshot.empty() ||
            be->merged_vbo_vertex_stride_bytes != vtx_stride || be->merged_vbo_vertex_count == 0u )
            continue;

        const uint8_t* snap = be->merged_vbo_cpu_snapshot.data();
        const size_t snap_bytes = be->merged_vbo_cpu_snapshot.size();

        local_remap.assign((size_t)op.model_vertex_span, 0xFFFFFFFFu);

        const InstanceUniformType inst =
            InstanceUniformType::FromYawOnly(op.x, op.y, op.z, op.rotation_r2pi2048);

        bool op_ok = true;
        for( uint32_t ti = 0; op_ok && ti + 2u < (uint32_t)op.sorted_indices.size(); ti += 3u )
        {
            for( int k = 0; k < 3; ++k )
            {
                const uint16_t local = op.sorted_indices[ti + (uint32_t)k];
                if( (uint32_t)local >= op.model_vertex_span )
                {
                    op_ok = false;
                    break;
                }

                if( static_coalesce_built_vtx_.size() + (size_t)vtx_stride >
                        kPass3DBuilder2CoalescedVtxBufBytes ||
                    static_coalesce_built_idx_.size() + 1u >
                        (size_t)kPass3DBuilder2DynamicIndexUInt16Capacity )
                {
                    op_ok = false;
                    break;
                }

                uint32_t out_idx = local_remap[(size_t)local];
                if( out_idx == 0xFFFFFFFFu )
                {
                    const uint64_t abs_v = (uint64_t)op.vbo_offset + (uint64_t)local;
                    const uint64_t byte_off = abs_v * (uint64_t)vtx_stride;
                    if( byte_off + (uint64_t)vtx_stride > snap_bytes )
                    {
                        op_ok = false;
                        break;
                    }

                    VertexType v{};
                    std::memcpy(&v, snap + byte_off, sizeof(VertexType));
                    pass3d_builder2_detail::ApplyYawTranslationToVertex(v, inst);
                    out_idx = static_cast<uint32_t>(static_coalesce_built_vtx_.size() / (size_t)vtx_stride);
                    local_remap[(size_t)local] = out_idx;
                    const size_t at = static_coalesce_built_vtx_.size();
                    static_coalesce_built_vtx_.resize(at + (size_t)vtx_stride);
                    std::memcpy(static_coalesce_built_vtx_.data() + at, &v, sizeof(VertexType));
                }

                static_coalesce_built_idx_.push_back((uint16_t)out_idx);
            }
        }
        (void)op_ok;
    }

    flush_span(cur_batch, static_coalesce_built_idx_.size(), static_coalesce_built_vtx_.size());
    static_coalesce_index_prefix_words_ = static_cast<uint32_t>(static_coalesce_built_idx_.size());
}

#endif // PASS_3D_BUILDER2_H
