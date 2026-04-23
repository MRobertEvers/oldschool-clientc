#ifndef PASS_3D_BUILDER2_H
#define PASS_3D_BUILDER2_H

#include <cstdint>
#include <cstring>
#include <vector>

struct DrawCommand3D
{
    uint16_t model_id;
    int rotation_r2pi2048;

    uint32_t dynamic_index_offset;
    uint32_t dynamic_index_count;
};

class Pass3DBuilder2
{
private:
    std::vector<DrawCommand3D> commands;

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
        uint16_t modelId,
        int rotation_r2pi2048,
        uint16_t* sortedIndices = nullptr,
        uint32_t indexCount = 0);

    void
    End3D();

    /** Clears recorded commands and index pool after a successful GPU submit. */
    void
    ClearAfterSubmit();

    const std::vector<DrawCommand3D>&
    GetCommands() const;

    const std::vector<uint16_t>&
    GetDynamicIndices() const;

    bool
    HasCommands() const;

    bool
    HasDynamicIndices() const;
};

inline void
Pass3DBuilder2::Begin3D()
{
    is_building = true;
    commands.clear();
    indices_pool.clear();
}

inline Pass3DBuilder2::Pass3DBuilder2()
    : is_building(false)
{
    commands.reserve(4096);
    indices_pool.reserve(4096 * 16);
}

inline Pass3DBuilder2::~Pass3DBuilder2()
{}

inline void
Pass3DBuilder2::AddModelDrawYawOnly(
    uint16_t modelId,
    int rotation_r2pi2048,
    uint16_t* sorted_indices,
    uint32_t index_count)
{
    if( !is_building )
        return;

    uint32_t offset = 0;
    if( sorted_indices != nullptr && index_count > 0 )
    {
        offset = static_cast<uint32_t>(indices_pool.size());
        indices_pool.resize(offset + index_count);
        std::memcpy(indices_pool.data() + offset, sorted_indices, index_count * sizeof(uint16_t));
    }

    commands.push_back(DrawCommand3D{ modelId, rotation_r2pi2048, offset, index_count });
}

inline void
Pass3DBuilder2::End3D()
{
    is_building = false;
}

inline void
Pass3DBuilder2::ClearAfterSubmit()
{
    commands.clear();
    indices_pool.clear();
}

inline const std::vector<DrawCommand3D>&
Pass3DBuilder2::GetCommands() const
{
    return commands;
}

inline const std::vector<uint16_t>&
Pass3DBuilder2::GetDynamicIndices() const
{
    return indices_pool;
}

inline bool
Pass3DBuilder2::HasCommands() const
{
    return !commands.empty();
}

inline bool
Pass3DBuilder2::HasDynamicIndices() const
{
    return !indices_pool.empty();
}

#endif // PASS_3D_BUILDER2_H
