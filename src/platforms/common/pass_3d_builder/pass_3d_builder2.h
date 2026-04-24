#ifndef PASS_3D_BUILDER2_H
#define PASS_3D_BUILDER2_H

#include <cstdint>
#include <cstring>
#include <vector>

// This is the data we send to the GPU for EVERY object
struct DrawModelInstanceData3D
{
    uint16_t rotation_r2pi2048;
    uint16_t padding[3]; // Align to 8 bytes for GPU performance
};

struct DrawModel3D
{
    uint16_t model_id;

    // Rotation/position data
    uint32_t instance_offset;

    // Sorted faces
    uint32_t dynamic_index_offset;
    uint32_t dynamic_index_count;

    DrawModel3D(
        uint16_t model_id,
        uint32_t instance_offset,
        uint32_t dynamic_index_offset,
        uint32_t dynamic_index_count)
        : model_id(model_id)
        , instance_offset(instance_offset)
        , dynamic_index_offset(dynamic_index_offset)
        , dynamic_index_count(dynamic_index_count)
    {}

    static DrawModel3D
    Create(
        uint16_t model_id,
        uint32_t instance_offset,
        uint32_t dynamic_index_offset,
        uint32_t dynamic_index_count)
    {
        return DrawModel3D(model_id, instance_offset, dynamic_index_offset, dynamic_index_count);
    }
};

class Pass3DBuilder2
{
private:
    std::vector<DrawModel3D> draw_commands;
    std::vector<DrawModelInstanceData3D> instance_pool;

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
        int rotation_r2pi2048,
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

    const std::vector<DrawModelInstanceData3D>&
    GetInstancePool() const;

    uint32_t
    GetInstancePoolSize() const;

    bool
    HasCommands() const;

    bool
    HasDynamicIndices() const;
};

inline void
Pass3DBuilder2::Begin3D()
{
    is_building = true;
    draw_commands.clear();
    instance_pool.clear();
    indices_pool.clear();
}

inline Pass3DBuilder2::Pass3DBuilder2()
    : is_building(false)
{
    draw_commands.reserve(4096);
    instance_pool.reserve(4096);
    indices_pool.reserve(4096 * 16);
}

inline Pass3DBuilder2::~Pass3DBuilder2()
{}

inline void
Pass3DBuilder2::AddModelDrawYawOnly(
    uint16_t model_id,
    int rotation_r2pi2048,
    uint16_t* sorted_indices,
    uint32_t index_count)
{
    if( !is_building )
        return;

    // 1. Handle the Rotation (Instance Data)
    // We store the rotation in a pool. The command will remember where it is.
    uint32_t instance_offset = static_cast<uint32_t>(instance_pool.size());
    instance_pool.push_back({ (uint16_t)rotation_r2pi2048 });

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
        DrawModel3D::Create(model_id, instance_offset, index_pool_offset, index_count));
}

inline void
Pass3DBuilder2::End3D()
{
    is_building = false;
}

inline void
Pass3DBuilder2::ClearAfterSubmit()
{
    draw_commands.clear();
    instance_pool.clear();
    indices_pool.clear();
}

inline const std::vector<DrawModel3D>&
Pass3DBuilder2::GetCommands() const
{
    return draw_commands;
}

inline const std::vector<uint16_t>&
Pass3DBuilder2::GetDynamicIndices() const
{
    return indices_pool;
}

inline bool
Pass3DBuilder2::HasCommands() const
{
    return !draw_commands.empty();
}

inline bool
Pass3DBuilder2::HasDynamicIndices() const
{
    return !indices_pool.empty();
}

#endif // PASS_3D_BUILDER2_H
