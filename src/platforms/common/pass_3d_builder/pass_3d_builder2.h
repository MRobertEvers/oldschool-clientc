#ifndef PASS_3D_BUILDER2_H
#define PASS_3D_BUILDER2_H

#include "platforms/gpu_3d.h"

#include <cstdint>
#include <cstring>
#include <vector>

/** `uint16_t` indices; must match `mtl_pass3d_index_buf` size in Metal init.
 *  One frame can sort many models × faces × 3; 64Ki entries was too small for world draws. */
inline constexpr uint32_t kPass3DBuilder2DynamicIndexUInt16Capacity = 1u << 21; // 2_097_152

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

template<typename InstanceUniformType>
class Pass3DBuilder2
{
private:
    std::vector<DrawModel3D> draw_commands;
    std::vector<InstanceUniformType> instance_pool;

    // A contiguous pool holding ALL sorted/dynamic indices for the current frame.
    // The backend will upload this entire vector in one swift API call.
    std::vector<uint16_t> indices_pool;

    bool is_building;

public:
    Pass3DBuilder2();
    ~Pass3DBuilder2();

    // Called on TORIRS_GFX_BEGIN_3D
    void
    Begin3D();

    // Called on TORIRS_GFX_MODEL_DRAW
    // - For static, opaque geometry: omit the last two parameters.
    // - For sorted, transparent geometry: pass the sorted index array.
    void
    AddModelDrawYawOnly(
        uint16_t model_id,
        int32_t x,
        int32_t y,
        int32_t z,
        int rotation_r2pi2048,
        bool use_animation,
        uint8_t animation_index,
        uint8_t frame_index,
        uint16_t* sorted_indices = nullptr,
        uint32_t index_count = 0);

    void
    End3D();

    /** Clears recorded commands and index pool after a successful GPU submit. */
    void
    ClearAfterSubmit();

    const std::vector<DrawModel3D>&
    GetCommands() const;

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
Pass3DBuilder2<InstanceUniformType>::Begin3D()
{
    is_building = true;
    draw_commands.clear();
    instance_pool.clear();
    indices_pool.clear();
}

template<typename InstanceUniformType>
inline Pass3DBuilder2<InstanceUniformType>::Pass3DBuilder2()
    : is_building(false)
{
    draw_commands.reserve(4096);
    instance_pool.reserve(4096);
    indices_pool.reserve(4096 * 16);
}

template<typename InstanceUniformType>
inline Pass3DBuilder2<InstanceUniformType>::~Pass3DBuilder2()
{}

template<typename InstanceUniformType>
inline void
Pass3DBuilder2<InstanceUniformType>::AddModelDrawYawOnly(
    uint16_t model_id,
    int32_t x,
    int32_t y,
    int32_t z,
    int rotation_r2pi2048,
    bool use_animation,
    uint8_t animation_index,
    uint8_t frame_index,
    uint16_t* sorted_indices,
    uint32_t index_count)
{
    if( !is_building )
        return;

    // 1. Instance transform: `InstanceUniformType` (e.g. `GPU3DTransformUniformMetal`) matches
    // `vertexShader` buffer(2) layout.
    uint32_t instance_offset = static_cast<uint32_t>(instance_pool.size());
    instance_pool.push_back(InstanceUniformType::FromYawOnly(x, y, z, rotation_r2pi2048));

    // 2. Handle the Sorted Faces (Index Data)
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

    // 3. Create the Command
    // We pass the model_id, the location of our rotation, and the location of our indices.
    draw_commands.push_back(
        DrawModel3D::Create(
            model_id,
            use_animation,
            animation_index,
            frame_index,
            instance_offset,
            index_pool_offset,
            index_count));
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
    draw_commands.clear();
    instance_pool.clear();
    indices_pool.clear();
}

template<typename InstanceUniformType>
inline const std::vector<DrawModel3D>&
Pass3DBuilder2<InstanceUniformType>::GetCommands() const
{
    return draw_commands;
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
    return !draw_commands.empty();
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

#endif // PASS_3D_BUILDER2_H
