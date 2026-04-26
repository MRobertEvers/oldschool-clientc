#ifndef PASS_3D_BUILDER2_METAL_H
#define PASS_3D_BUILDER2_METAL_H

#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"

#include <cstdint>
#include <cstdio>
#include <vector>

/** Max dynamic indices per 3D pass (same count as legacy Pass3DBuilder2; bytes = count * 4). */
inline constexpr uint32_t kPass3DBuilder2MetalDynamicIndexCapacity = 1u << 21; // 2_097_152

/** Scenery-only: one merged mesh VBO per pass, pooled sorted absolute indices (`uint32_t`). */
class Pass3DBuilder2Metal
{
private:
    bool is_building_ = false;
    GPUResourceHandle mesh_vbo_ = 0;
    std::vector<uint32_t> indices_pool_;

public:
    Pass3DBuilder2Metal()
    {
        indices_pool_.reserve(4096u * 16u);
    }

    void
    Begin()
    {
        is_building_ = true;
        mesh_vbo_ = 0;
        indices_pool_.clear();
    }

    void
    End()
    {
        is_building_ = false;
    }

    bool
    IsBuilding() const
    {
        return is_building_;
    }

    bool
    HasCommands() const
    {
        return !indices_pool_.empty() && mesh_vbo_ != 0;
    }

    void
    ClearAfterSubmit()
    {
        mesh_vbo_ = 0;
        indices_pool_.clear();
    }

    GPUResourceHandle
    MeshVbo() const
    {
        return mesh_vbo_;
    }

    const std::vector<uint32_t>&
    Indices() const
    {
        return indices_pool_;
    }

    const std::vector<uint32_t>&
    GetDynamicIndices() const
    {
        return indices_pool_;
    }

    bool
    HasDynamicIndices() const
    {
        return !indices_pool_.empty();
    }

    /** Append face-relative indices for one scenery draw, biased by `pose_vbo_vertex_offset`. */
    void
    AppendSortedDraw(
        GPUResourceHandle vbo,
        uint32_t pose_vbo_vertex_offset,
        const uint32_t* sorted_indices,
        uint32_t index_count);
};

#if defined(__OBJC__)

#    import <Metal/Metal.h>

void
Pass3DBuilder2SubmitMetal(
    Pass3DBuilder2Metal& builder,
    id<MTLRenderCommandEncoder> render_command_encoder,
    id<MTLBuffer> dynamic_index_buffer,
    NSUInteger index_base_bytes,
    id<MTLTexture> fragment_atlas_texture,
    id<MTLBuffer> atlas_tiles_buffer,
    id<MTLBuffer> uniforms_buffer,
    NSUInteger uniforms_buffer_offset_bytes,
    id<MTLSamplerState> fragment_sampler,
    id<MTLDepthStencilState> depth_stencil_state);

#endif // __OBJC__

#endif // PASS_3D_BUILDER2_METAL_H
