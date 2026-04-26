#ifndef GPU_3D_CACHE2_H
#define GPU_3D_CACHE2_H

#include "graphics/dash.h"
#include "graphics/uv_pnm.h"
#include "platforms/common/gpu_3d.h"
#include "tori_rs_render.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

constexpr size_t MAX_3D_ASSETS = 32768;
constexpr size_t MAX_TEXTURES = 256;
constexpr size_t MAX_POSE_COUNT = 16;

enum class GPU3DCache2UVCalculationMode
{
    TexturedFaceArray,
    FirstFace
};

inline void
gpu3d_hsl16_to_float_rgba(
    uint16_t hsl16,
    float rgba_out[4],
    float alpha)
{
    const uint32_t rgb = (uint32_t)dash_hsl16_to_rgb((int)(hsl16 & 0xFFFFu));
    rgba_out[0] = (float)((rgb >> 16) & 0xFFu) / 255.0f;
    rgba_out[1] = (float)((rgb >> 8) & 0xFFu) / 255.0f;
    rgba_out[2] = (float)(rgb & 0xFFu) / 255.0f;
    rgba_out[3] = alpha;
}
// Assuming 4 bytes per pixel (RGBA8) for a 128x128 texture
constexpr size_t BYTES_PER_PIXEL = 4;
constexpr size_t TEXTURE_DIMENSION = 128;
constexpr size_t BYTES_PER_TEXTURE = TEXTURE_DIMENSION * TEXTURE_DIMENSION * BYTES_PER_PIXEL;

// Safely packs R, G, B, A into two 16-bit integers.
// Guarantees physical memory layout [R][G][B][A] on ALL CPU architectures.
inline void
PackColorTo16BitUniversal(
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a,
    uint16_t& out_low,
    uint16_t& out_high)
{
    // Define the exact byte order we want in RAM
    uint8_t low_bytes[2] = { r, g };
    uint8_t high_bytes[2] = { b, a };

    // Copy those exact bytes directly into the memory of the 16-bit integers
    std::memcpy(&out_low, low_bytes, 2);
    std::memcpy(&out_high, high_bytes, 2);
}

// Packs R, G, B, A (0-255) into two 16-bit integers for a little-endian CPU.
// Memory output will strictly be: [R] [G] [B] [A]
inline void
PackColorTo16Bit(
    uint16_t color_hsl16,
    uint16_t& out_low,
    uint16_t& out_high)
{
    uint32_t rgb = dash_hsl16_to_rgb(color_hsl16);
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    // HSL16 palette stores 0x00RRGGBB — the top byte is always 0. Use 255 (fully opaque);
    // transparency is handled by the fragment shader's texture-alpha discard.
    uint8_t a = 255;

    PackColorTo16BitUniversal(r, g, b, a, out_low, out_high);
}

struct AtlasUVRect
{
    float u_offset; // Starting X coordinate
    float v_offset; // Starting Y coordinate
    float u_scale;  // Width in UV space
    float v_scale;  // Height in UV space
};

// Opaque handle for GPU resources (Replaces id<MTLBuffer>, ID3D11Buffer*, GLuint, etc.)
// - OpenGL: Cast GLuint to GPUResourceHandle
// - DirectX: Cast ID3D11Buffer* or ID3D12Resource* to GPUResourceHandle
// - Metal: retained via `GPU3DCache2` batch lookup / standalone slots; batch pose handles may be
// __bridge.
typedef uintptr_t GPUResourceHandle;

/** No merged scene batch: use pose `vbo`/`ebo` (solo model or direct binding). */
inline constexpr uint32_t kSceneBatchSlotNone = 0xFFFFFFFFu;
/** Max scene2 world-rebuild batch slots (ids `0 .. kGPU3DCache2MaxSceneBatches-1`). */
inline constexpr uint32_t kGPU3DCache2MaxSceneBatches = 10u;

/** Scene2 rebuild batch id for lifetime / merged resource ownership (not GPUBatch draw grouping). */
using SceneBatchId = uint32_t;

/** Backend draw batch id: shared by poses that bind the same merged mesh buffers (see
 * `AllocGpuBatchId(scene_batch_id)`).
 * `0` means unset or invalid for routing. */
using GPUBatchId = uint32_t;

enum class GPU3DGeometryClass : uint8_t
{
    StaticWorld,
    PrebakedAnimated,
    DynamicComposed,
    StreamBatch,
};

enum class GPU3DResourceLifetime : uint8_t
{
    Retained,
    FrameLocal,
    CommandLocal
};

enum class GPU3DUpdatePattern : uint8_t
{
    Immutable,
    SelectPrebakedFrame,
    RewriteOften,
    AppendOncePerFrame
};

enum class GPU3DBufferingStrategy : uint8_t
{
    None,
    MultiFrameSlots,
    Ring
};

enum class GPU3DDrawAddressing : uint8_t
{
    IndexedAbsolute,
    IndexedLocalToVertexOffset,
    NonIndexed
};

enum class GPU3DAllocationDomain : uint8_t
{
    Immutable,
    RetainedAnimated,
    RewriteArena,
    FrameStream
};

struct GPU3DResourcePolicy
{
    GPU3DGeometryClass geometry_class = GPU3DGeometryClass::StaticWorld;
    GPU3DUpdatePattern update_pattern = GPU3DUpdatePattern::Immutable;
    GPU3DBufferingStrategy buffering = GPU3DBufferingStrategy::None;
    GPU3DDrawAddressing draw_addressing = GPU3DDrawAddressing::IndexedAbsolute;
    GPU3DAllocationDomain allocation_domain = GPU3DAllocationDomain::Immutable;
    uint8_t frame_slot_count = 1;
};

struct GPU3DCache2Resource
{
    GPUResourceHandle vbo = 0;
    GPUResourceHandle ebo = 0;
    uint32_t vbo_offset = 0;
    uint32_t ebo_offset = 0;
    GPU3DResourcePolicy policy{};
    GPU3DResourceLifetime lifetime = GPU3DResourceLifetime::Retained;
    bool valid = false;
};

struct GPU3DMeshBinding
{
    GPUResourceHandle vbo = 0;
    GPUResourceHandle ebo = 0;
    uint32_t vertex_byte_offset = 0;
    uint32_t index_byte_offset = 0;
    uint32_t base_vertex = 0;
    uint32_t element_count = 0;
    GPU3DDrawAddressing addressing = GPU3DDrawAddressing::IndexedAbsolute;
    GPU3DResourceLifetime lifetime = GPU3DResourceLifetime::Retained;
    bool valid = false;
};

inline GPU3DResourcePolicy
GPU3DCache2PolicyForUsageHint(
    ToriRS_UsageHint usage_hint,
    uint8_t frame_slot_count = 1)
{
    GPU3DResourcePolicy p;
    p.frame_slot_count = frame_slot_count;
    switch( usage_hint )
    {
    case TORIRS_USAGE_SCENERY:
        p.geometry_class = GPU3DGeometryClass::StaticWorld;
        p.update_pattern = GPU3DUpdatePattern::Immutable;
        p.buffering = GPU3DBufferingStrategy::None;
        p.allocation_domain = GPU3DAllocationDomain::Immutable;
        p.draw_addressing = GPU3DDrawAddressing::IndexedAbsolute;
        break;
    case TORIRS_USAGE_NPC:
        p.geometry_class = GPU3DGeometryClass::PrebakedAnimated;
        p.update_pattern = GPU3DUpdatePattern::SelectPrebakedFrame;
        p.buffering = GPU3DBufferingStrategy::None;
        p.allocation_domain = GPU3DAllocationDomain::RetainedAnimated;
        p.draw_addressing = GPU3DDrawAddressing::IndexedLocalToVertexOffset;
        break;
    case TORIRS_USAGE_PLAYER:
        p.geometry_class = GPU3DGeometryClass::DynamicComposed;
        p.update_pattern = GPU3DUpdatePattern::RewriteOften;
        p.buffering = GPU3DBufferingStrategy::MultiFrameSlots;
        p.allocation_domain = GPU3DAllocationDomain::RewriteArena;
        p.draw_addressing = GPU3DDrawAddressing::IndexedAbsolute;
        break;
    case TORIRS_USAGE_PROJECTILE:
        p.geometry_class = GPU3DGeometryClass::StreamBatch;
        p.update_pattern = GPU3DUpdatePattern::AppendOncePerFrame;
        p.buffering = GPU3DBufferingStrategy::Ring;
        p.allocation_domain = GPU3DAllocationDomain::FrameStream;
        p.draw_addressing = GPU3DDrawAddressing::NonIndexed;
        break;
    default:
        break;
    }
    return p;
}

/** One scene batch slot: merged GPU resources + member scene model ids for unload/clear. */
struct GPU3DCache2SceneBatchEntry
{
    GPU3DCache2Resource resource{};
    /** Model ids (`DrawModel3D::model_id` / cache slot) belonging to this scene batch. */
    std::vector<uint16_t> scene_model_ids;
    /** GPUBatch ids allocated while this slot is valid (`AllocGpuBatchId(scene_batch_id)`). */
    std::vector<GPUBatchId> gpu_batch_ids;
    ToriRS_UsageHint usage_hint = TORIRS_USAGE_SCENERY;
    bool valid = false;

    /** CPU copy of merged batch VBO at submit time (`sizeof(VertexType)` per vertex). Used for
     *  cross-backend static scenery coalescing (CPU transform + dynamic draw) without GPU readback.
     */
    std::vector<uint8_t> merged_vbo_cpu_snapshot{};
    uint32_t merged_vbo_vertex_stride_bytes = 0u;
    uint32_t merged_vbo_vertex_count = 0u;
};

/** Platform-specific buffer handles (`MTLBuffer*` / GL name / D3D resource cast to
 *  `GPUResourceHandle`). Non-owning on Metal when filled from batch submit (`__bridge`); lifetime
 *  matches the renderer batch entry or standalone owner.
 *
 *  **Metal (`Pass3DBuilder2SubmitMetal`)**: when `scene_batch_id` is in
 * `0..kGPU3DCache2MaxSceneBatches-1`, VBO/EBO are resolved from `GPU3DCache2::SceneBatchGet`. When
 * `scene_batch_id == kSceneBatchSlotNone`, only pose `vbo`/`ebo` (and offsets) apply. */
struct GPUModelPosedData
{
    GPUResourceHandle vbo = 0;
    GPUResourceHandle ebo = 0;
    /** Draw batch id assigned when this pose is uploaded (`AllocGpuBatchId(scene_batch_id)`);
     * `0` if unset. Solo uploads use `kSceneBatchSlotNone` so ids are not tied to a scene slot. */
    GPUBatchId gpu_batch_id = 0u;
    /** `0..kGPU3DCache2MaxSceneBatches-1`: scene2 merged batch slot; `kSceneBatchSlotNone`:
     * solo/direct.
     */
    SceneBatchId scene_batch_id = kSceneBatchSlotNone;
    uint32_t vbo_offset = 0;
    uint32_t ebo_offset = 0;
    // If drawing without sorting.
    uint32_t element_count = 0;
    bool valid = false;
};

#define GPU_MODEL_ANIMATION_NONE_IDX 0
#define GPU_MODEL_ANIMATION_PRIMARY_IDX 1
#define GPU_MODEL_ANIMATION_SECONDARY_IDX 2

struct GPUModelData
{
    /** Flat list of baked poses; bind is always `poses[0]`; `animation_offsets[1/2]` index bases.
     */
    std::vector<GPUModelPosedData> poses;
    /** `[0]` is always 0 (bind). `[1]` / `[2]` are starting pose indices for primary / secondary.
     */
    uint32_t animation_offsets[3];
};

struct BatchedQueueModel
{
    bool is_va = false;
    uint16_t model_id;
    /** Absolute index into `GPUModelData::poses` for this merged batch slice. */
    uint32_t pose_index;
    uint32_t vertex_count;
    uint32_t vbo_start;
    uint32_t face_count;
    uint32_t ebo_start;

    BatchedQueueModel() = default;

    BatchedQueueModel(
        bool is_va,
        uint16_t model_id,
        uint32_t pose_index,
        uint32_t vertex_count,
        uint32_t vbo_start,
        uint32_t face_count,
        uint32_t ebo_start)
        : is_va(is_va)
        , model_id(model_id)
        , pose_index(pose_index)
        , vertex_count(vertex_count)
        , vbo_start(vbo_start)
        , face_count(face_count)
        , ebo_start(ebo_start)
    {}

    static BatchedQueueModel
    CreateModel(
        uint16_t model_id,
        uint32_t pose_index,
        uint32_t vertex_count,
        uint32_t vertices_start,
        uint32_t face_count,
        uint32_t ebo_start)
    {
        return BatchedQueueModel(
            false, model_id, pose_index, vertex_count, vertices_start, face_count, ebo_start);
    }
};

template<typename VertexType>
struct BatchQueue
{
    std::vector<BatchedQueueModel> batch;
    /** Interleaved `VertexType` (see `GPU3DMeshVertex` in platforms/gpu_3d.h); same layout as
     *  `MetalVertexPacked` in `Shaders.metal` / `vertexShader` (indexed, same transform as
     *  `vertexShaderStream`). */
    std::vector<VertexType> vbo;
    std::vector<uint16_t> ebo;
};

struct GPUTextureAtlas
{
    AtlasUVRect uv_region[MAX_TEXTURES];
    GPUResourceHandle handle = 0;

    // Buffer to hold raw RGBA pixel data until flushed to the GPU
    // Total size: 256 * 128 * 128 * 4 = 16.7 MB
    std::vector<uint8_t> pixel_buffer;

    // Tracks the highest index loaded to optimize the final upload size
    uint32_t max_loaded_index = 0;
    uint32_t atlas_width = 0;
    uint32_t atlas_height = 0;
};

/** `VertexType` must provide `static VertexType FromCommon(const CommonVertex&)`. */
template<typename VertexType>
class GPU3DCache2
{
private:
    std::array<GPUModelData, MAX_3D_ASSETS> models;
    GPUTextureAtlas atlas;
    std::vector<BatchQueue<VertexType>> batch_queues;

    std::array<GPU3DCache2SceneBatchEntry, kGPU3DCache2MaxSceneBatches> scene_batch_lookup_{};
    std::array<GPUResourceHandle, MAX_3D_ASSETS> standalone_retained_vbo_{};
    std::array<GPUResourceHandle, MAX_3D_ASSETS> standalone_retained_ebo_{};

    /** Monotonic id for merged mesh uploads; all poses in one `BatchSubmit*` share one id. */
    uint32_t gpu_batch_id_next_ = 0u;

    void
    invalidatePosesForSceneBatch(SceneBatchId scene_batch_id);

    uint32_t
    allocatePoseSlot(
        uint16_t model_id,
        uint8_t gpu_segment_slot,
        uint16_t frame_index);

public:
    GPU3DCache2() = default;
    ~GPU3DCache2() = default;

    /** Allocate a new GPUBatch id for the next merged mesh upload (one shared VBO/EBO group).
     * When `scene_batch_id` refers to a valid scene batch slot, the id is recorded there and
     * poses using that slot are cleared when the slot is cleared/replaced (`SceneBatchClear` /
     * `SceneBatchBegin` / `SceneBatchReset`). Use `kSceneBatchSlotNone` for solo uploads. */
    GPUBatchId
    AllocGpuBatchId(SceneBatchId scene_batch_id);

    void
    InitAtlas(
        GPUResourceHandle handle,
        uint32_t width,
        uint32_t height);

    void
    UnloadAtlas();

    void
    LoadTexture128(
        uint16_t texture_id,
        const void* pixel_data);

    const uint8_t*
    GetAtlasPixelData() const;

    uint32_t
    GetAtlasPixelDataSize() const;

    int
    BatchBegin();
    void
    BatchEnd();

    void
    BatchAddModeli16(
        uint16_t model_id,
        uint8_t gpu_segment_slot,
        uint16_t frame_index,
        uint32_t vertex_count,
        int16_t* vertices_x,
        int16_t* vertices_y,
        int16_t* vertices_z,
        uint32_t faces_count,
        uint16_t* faces_a,
        uint16_t* faces_b,
        uint16_t* faces_c,
        uint16_t* faces_a_color_hsl16,
        uint16_t* faces_b_color_hsl16,
        uint16_t* faces_c_color_hsl16,
        uint8_t* face_alphas,
        const int* face_infos_nullable = nullptr);

    void
    BatchAddModelTexturedi16(
        uint16_t model_id,
        uint8_t gpu_segment_slot,
        uint16_t frame_index,
        uint32_t vertex_count,
        int16_t* vertices_x,
        int16_t* vertices_y,
        int16_t* vertices_z,
        uint32_t faces_count,
        uint16_t* faces_a,
        uint16_t* faces_b,
        uint16_t* faces_c,
        uint16_t* faces_a_color_hsl16,
        uint16_t* faces_b_color_hsl16,
        uint16_t* faces_c_color_hsl16,
        uint8_t* face_alphas,
        int16_t* faces_textures,
        uint16_t* textured_faces,
        uint16_t* textured_faces_a,
        uint16_t* textured_faces_b,
        uint16_t* textured_faces_c,
        const int* face_infos_nullable,
        GPU3DCache2UVCalculationMode uv_calculation_mode =
            GPU3DCache2UVCalculationMode::TexturedFaceArray);

    /** Vertex count in `vbo` (each element is one `VertexType`). */
    uint32_t
    BatchGetVBOVertexCount();

    uint32_t
    BatchGetVBOSize();

    uint32_t
    BatchGetEBOSize();

    const VertexType*
    BatchGetVBO() const;

    uint16_t*
    BatchGetEBO();

    uint32_t
    GetAtlasWidth() const;

    uint32_t
    GetAtlasHeight() const;

    const AtlasUVRect&
    GetAtlasUVRect(uint16_t texture_id) const;

    bool
    HasBufferedAtlasData() const;

    const std::vector<BatchedQueueModel>&
    BatchGetTrackingData() const;

    void
    SetModelPose(
        uint16_t model_id,
        uint32_t pose_index,
        const GPUModelPosedData& data);

    GPUModelPosedData
    GetModelPose(
        uint16_t model_id,
        uint32_t pose_index) const;

    /** Bind pose when `use_animation` is false; else
     * `poses[animation_offsets[scene_anim+1]+frame]`. */
    GPUModelPosedData
    GetModelPoseForDraw(
        uint16_t model_id,
        bool use_animation,
        int scene_animation_index,
        int frame_index) const;

    /** Same geometry as bind pose `poses[0]`. */
    GPUModelPosedData
    GetModel(uint16_t model_id) const
    {
        return GetModelPose(model_id, 0u);
    }

    /** Reset poses and segment bases for a model (e.g. batch unload). */
    void
    ClearModel(uint16_t model_id);

    bool
    SceneBatchBegin(
        SceneBatchId scene_batch_id,
        ToriRS_UsageHint usage_hint);

    GPU3DCache2SceneBatchEntry*
    SceneBatchGet(SceneBatchId scene_batch_id);

    const GPU3DCache2SceneBatchEntry*
    SceneBatchGet(SceneBatchId scene_batch_id) const;

    void
    SceneBatchAddModel(
        SceneBatchId scene_batch_id,
        uint16_t scene_model_id);

    void
    SceneBatchSetResource(
        SceneBatchId scene_batch_id,
        const GPU3DCache2Resource& resource);

    GPU3DCache2SceneBatchEntry
    SceneBatchClear(SceneBatchId scene_batch_id);

    void
    SceneBatchReset();

    void
    SetStandaloneRetainedBuffers(
        uint16_t model_id,
        GPUResourceHandle vbo,
        GPUResourceHandle ebo);

    GPU3DCache2Resource
    TakeStandaloneRetainedBuffers(uint16_t model_id);
};

template<typename VertexType>
inline void
GPU3DCache2<VertexType>::invalidatePosesForSceneBatch(SceneBatchId scene_batch_id)
{
    if( scene_batch_id >= kGPU3DCache2MaxSceneBatches )
        return;
    for( uint16_t model_id = 0; model_id < MAX_3D_ASSETS; model_id++ )
    {
        GPUModelData& md = models[model_id];
        for( GPUModelPosedData& pose : md.poses )
        {
            if( pose.scene_batch_id == scene_batch_id )
                pose = GPUModelPosedData{};
        }
    }
}

template<typename VertexType>
inline GPUBatchId
GPU3DCache2<VertexType>::AllocGpuBatchId(SceneBatchId scene_batch_id)
{
    if( ++gpu_batch_id_next_ == 0u )
        gpu_batch_id_next_ = 1u;
    const GPUBatchId id = gpu_batch_id_next_;
    if( scene_batch_id < kGPU3DCache2MaxSceneBatches )
    {
        GPU3DCache2SceneBatchEntry& e = scene_batch_lookup_[scene_batch_id];
        if( e.valid )
            e.gpu_batch_ids.push_back(id);
    }
    return id;
}

template<typename VertexType>
inline int
GPU3DCache2<VertexType>::BatchBegin()
{
    batch_queues.push_back(BatchQueue<VertexType>{});
    return 0;
}

template<typename VertexType>
inline void
GPU3DCache2<VertexType>::BatchEnd()
{
    batch_queues.pop_back();
}

template<typename VertexType>
inline uint32_t
GPU3DCache2<VertexType>::allocatePoseSlot(
    uint16_t model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index)
{
    if( model_id >= MAX_3D_ASSETS )
        return 0;
    GPUModelData& md = models[model_id];
    if( gpu_segment_slot == GPU_MODEL_ANIMATION_NONE_IDX )
    {
        md.animation_offsets[0] = 0;
        if( md.poses.size() < 1u )
            md.poses.resize(1u);
        return 0u;
    }
    if( gpu_segment_slot == GPU_MODEL_ANIMATION_PRIMARY_IDX )
    {
        if( md.animation_offsets[1] == 0u )
            md.animation_offsets[1] =
                (uint32_t)md.poses.size() < 1u ? 0u : (uint32_t)md.poses.size();
        const uint32_t idx = md.animation_offsets[1] + (uint32_t)frame_index;
        if( md.poses.size() <= idx )
            md.poses.resize((size_t)idx + 1u);
        return idx;
    }
    if( gpu_segment_slot == GPU_MODEL_ANIMATION_SECONDARY_IDX )
    {
        if( md.animation_offsets[2] == 0u )
            md.animation_offsets[2] = (uint32_t)md.poses.size();
        const uint32_t idx = md.animation_offsets[2] + (uint32_t)frame_index;
        if( md.poses.size() <= idx )
            md.poses.resize((size_t)idx + 1u);
        return idx;
    }
    return 0u;
}

template<typename VertexType>
inline void
GPU3DCache2<VertexType>::BatchAddModeli16(
    uint16_t model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    uint32_t vertex_count,
    int16_t* vertices_x,
    int16_t* vertices_y,
    int16_t* vertices_z,
    uint32_t faces_count,
    uint16_t* faces_a,
    uint16_t* faces_b,
    uint16_t* faces_c,
    uint16_t* faces_a_color_hsl16,
    uint16_t* faces_b_color_hsl16,
    uint16_t* faces_c_color_hsl16,
    uint8_t* face_alphas,
    const int* face_infos_nullable)
{
    (void)vertex_count;
    assert(faces_count < 4096);
    BatchQueue<VertexType>& batch_queue = batch_queues.back();
    const uint32_t pose_index = allocatePoseSlot(model_id, gpu_segment_slot, frame_index);

    const uint32_t vbo_vertex_start = (uint32_t)batch_queue.vbo.size();
    const uint32_t ebo_start = (uint32_t)batch_queue.ebo.size();
    const uint32_t new_vertex_count = faces_count * 3u;

    for( uint32_t i = 0; i < faces_count; i++ )
    {
        const uint32_t vo = (uint32_t)batch_queue.vbo.size();
        batch_queue.ebo.push_back((uint16_t)vo);
        batch_queue.ebo.push_back((uint16_t)(vo + 1u));
        batch_queue.ebo.push_back((uint16_t)(vo + 2u));

        uint8_t alpha = 0xFF;
        if( face_alphas )
            alpha = 0xFF - face_alphas[i];

        float alpha_float = alpha / 255.0f;

        const bool skip_face = (face_infos_nullable && ((face_infos_nullable[(int)i] & 3) == 2)) ||
                               (faces_c_color_hsl16[i] == (uint16_t)DASHHSL16_HIDDEN);

        const int face_a = (int)faces_a[i];
        const int face_b = (int)faces_b[i];
        const int face_c = (int)faces_c[i];
        const bool bad_idx = face_a < 0 || face_b < 0 || face_c < 0 ||
                             face_a >= (int)vertex_count || face_b >= (int)vertex_count ||
                             face_c >= (int)vertex_count;

        if( skip_face || bad_idx )
        {
            for( int k = 0; k < 3; k++ )
            {
                CommonVertex v{};
                v.position[0] = 0.0f;
                v.position[1] = 0.0f;
                v.position[2] = 0.0f;
                v.position[3] = 1.0f;
                v.color[0] = v.color[1] = v.color[2] = 0.0f;
                v.color[3] = alpha_float;
                v.texcoord[0] = v.texcoord[1] = 0.0f;
                v.tex_id = 0xFFFFu;
                v.uv_mode = 0u;
                batch_queue.vbo.push_back(VertexType::FromCommon(v));
            }
            continue;
        }

        const uint16_t hsl_a = faces_a_color_hsl16[i];
        const uint16_t hsl_b = faces_b_color_hsl16[i];
        const uint16_t hsl_c = faces_c_color_hsl16[i];
        float color_a[4], color_b[4], color_c[4];
        if( hsl_c == (uint16_t)DASHHSL16_FLAT )
        {
            gpu3d_hsl16_to_float_rgba(hsl_a, color_a, alpha_float);
            color_b[0] = color_a[0];
            color_b[1] = color_a[1];
            color_b[2] = color_a[2];
            color_b[3] = color_a[3];
            color_c[0] = color_a[0];
            color_c[1] = color_a[1];
            color_c[2] = color_a[2];
            color_c[3] = color_a[3];
        }
        else
        {
            gpu3d_hsl16_to_float_rgba(hsl_a, color_a, alpha_float);
            gpu3d_hsl16_to_float_rgba(hsl_b, color_b, alpha_float);
            gpu3d_hsl16_to_float_rgba(hsl_c, color_c, alpha_float);
        }

        CommonVertex va{};
        va.position[0] = (float)vertices_x[face_a];
        va.position[1] = (float)vertices_y[face_a];
        va.position[2] = (float)vertices_z[face_a];
        va.position[3] = 1.0f;
        va.color[0] = color_a[0];
        va.color[1] = color_a[1];
        va.color[2] = color_a[2];
        va.color[3] = color_a[3];
        va.texcoord[0] = 0.0f;
        va.texcoord[1] = 0.0f;
        va.tex_id = 0xFFFFu;
        va.uv_mode = 0u;
        batch_queue.vbo.push_back(VertexType::FromCommon(va));

        CommonVertex vb{};
        vb.position[0] = (float)vertices_x[face_b];
        vb.position[1] = (float)vertices_y[face_b];
        vb.position[2] = (float)vertices_z[face_b];
        vb.position[3] = 1.0f;
        vb.color[0] = color_b[0];
        vb.color[1] = color_b[1];
        vb.color[2] = color_b[2];
        vb.color[3] = color_b[3];
        vb.texcoord[0] = 0.0f;
        vb.texcoord[1] = 0.0f;
        vb.tex_id = 0xFFFFu;
        vb.uv_mode = 0u;
        batch_queue.vbo.push_back(VertexType::FromCommon(vb));

        CommonVertex vc{};
        vc.position[0] = (float)vertices_x[face_c];
        vc.position[1] = (float)vertices_y[face_c];
        vc.position[2] = (float)vertices_z[face_c];
        vc.position[3] = 1.0f;
        vc.color[0] = color_c[0];
        vc.color[1] = color_c[1];
        vc.color[2] = color_c[2];
        vc.color[3] = color_c[3];
        vc.texcoord[0] = 0.0f;
        vc.texcoord[1] = 0.0f;
        vc.tex_id = 0xFFFFu;
        vc.uv_mode = 0u;

        batch_queue.vbo.push_back(VertexType::FromCommon(vc));
    }

    batch_queue.batch.push_back(
        BatchedQueueModel::CreateModel(
            model_id, pose_index, new_vertex_count, vbo_vertex_start, faces_count, ebo_start));
}

template<typename VertexType>
inline void
GPU3DCache2<VertexType>::BatchAddModelTexturedi16(
    uint16_t model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    uint32_t vertex_count,
    int16_t* vertices_x,
    int16_t* vertices_y,
    int16_t* vertices_z,
    uint32_t faces_count,
    uint16_t* faces_a,
    uint16_t* faces_b,
    uint16_t* faces_c,
    uint16_t* faces_a_color_hsl16,
    uint16_t* faces_b_color_hsl16,
    uint16_t* faces_c_color_hsl16,
    uint8_t* face_alphas,
    int16_t* faces_textures,
    uint16_t* textured_faces,
    uint16_t* textured_faces_a,
    uint16_t* textured_faces_b,
    uint16_t* textured_faces_c,
    const int* face_infos_nullable,
    GPU3DCache2UVCalculationMode uv_calculation_mode)
{
    assert(faces_count < 4096);
    BatchQueue<VertexType>& batch_queue = batch_queues.back();
    const uint32_t pose_index = allocatePoseSlot(model_id, gpu_segment_slot, frame_index);

    const uint32_t vbo_vertex_start = (uint32_t)batch_queue.vbo.size();
    const uint32_t ebo_start = (uint32_t)batch_queue.ebo.size();
    const uint32_t new_vertex_count = faces_count * 3u;

    uint32_t tp_vertex = 0;
    uint32_t tm_vertex = 0;
    uint32_t tn_vertex = 0;
    struct UVFaceCoords uv;

    for( uint32_t i = 0; i < faces_count; i++ )
    {
        const uint32_t vbo_offset = (uint32_t)batch_queue.vbo.size();
        batch_queue.ebo.push_back((uint16_t)vbo_offset);
        batch_queue.ebo.push_back((uint16_t)(vbo_offset + 1u));
        batch_queue.ebo.push_back((uint16_t)(vbo_offset + 2u));

        uint8_t alpha = 0xFF;
        if( face_alphas )
            alpha = 0xFF - face_alphas[i];
        float alpha_float = alpha / 255.0f;

        const bool skip_face = (face_infos_nullable && ((face_infos_nullable[(int)i] & 3) == 2)) ||
                               (faces_c_color_hsl16[i] == (uint16_t)DASHHSL16_HIDDEN);

        const int face_a = (int)faces_a[i];
        const int face_b = (int)faces_b[i];
        const int face_c = (int)faces_c[i];
        const bool bad_idx = face_a < 0 || face_b < 0 || face_c < 0 ||
                             face_a >= (int)vertex_count || face_b >= (int)vertex_count ||
                             face_c >= (int)vertex_count;

        if( skip_face || bad_idx )
        {
            for( int k = 0; k < 3; k++ )
            {
                CommonVertex v{};
                v.position[0] = v.position[1] = v.position[2] = 0.0f;
                v.position[3] = 1.0f;
                v.color[0] = v.color[1] = v.color[2] = 0.0f;
                v.color[3] = alpha_float;
                v.texcoord[0] = v.texcoord[1] = 0.0f;
                v.tex_id = 0xFFFFu;
                v.uv_mode = 0u;
                batch_queue.vbo.push_back(VertexType::FromCommon(v));
            }
            continue;
        }

        /* P/N/M frame for `uv_pnm_compute`: match `wg1_fill_model_face_vertices_model_local`
         * (ground VA uses face 0 corners for all faces; else textured remap or face i). */
        if( uv_calculation_mode == GPU3DCache2UVCalculationMode::FirstFace )
        {
            tp_vertex = faces_a[0];
            tm_vertex = faces_b[0];
            tn_vertex = faces_c[0];
        }
        else if( textured_faces && textured_faces[i] != 0xFFFF )
        {
            tp_vertex = textured_faces_a[textured_faces[i]];
            tm_vertex = textured_faces_b[textured_faces[i]];
            tn_vertex = textured_faces_c[textured_faces[i]];
        }
        else
        {
            tp_vertex = faces_a[i];
            tm_vertex = faces_b[i];
            tn_vertex = faces_c[i];
        }

        uv_pnm_compute(
            &uv,
            (float)vertices_x[tp_vertex],
            (float)vertices_y[tp_vertex],
            (float)vertices_z[tp_vertex],
            (float)vertices_x[tm_vertex],
            (float)vertices_y[tm_vertex],
            (float)vertices_z[tm_vertex],
            (float)vertices_x[tn_vertex],
            (float)vertices_y[tn_vertex],
            (float)vertices_z[tn_vertex],
            (float)vertices_x[face_a],
            (float)vertices_y[face_a],
            (float)vertices_z[face_a],
            (float)vertices_x[face_b],
            (float)vertices_y[face_b],
            (float)vertices_z[face_b],
            (float)vertices_x[face_c],
            (float)vertices_y[face_c],
            (float)vertices_z[face_c]);

        uint16_t tex_id;
        if( faces_textures[i] != -1 )
            tex_id = (uint16_t)faces_textures[i];
        else
            tex_id = 0xFFFFu;

        const uint16_t hsl_a = faces_a_color_hsl16[i];
        const uint16_t hsl_b = faces_b_color_hsl16[i];
        const uint16_t hsl_c = faces_c_color_hsl16[i];
        float color_a[4], color_b[4], color_c[4];
        if( hsl_c == (uint16_t)DASHHSL16_FLAT )
        {
            gpu3d_hsl16_to_float_rgba(hsl_a, color_a, alpha_float);
            color_b[0] = color_a[0];
            color_b[1] = color_a[1];
            color_b[2] = color_a[2];
            color_b[3] = color_a[3];
            color_c[0] = color_a[0];
            color_c[1] = color_a[1];
            color_c[2] = color_a[2];
            color_c[3] = color_a[3];
        }
        else
        {
            gpu3d_hsl16_to_float_rgba(hsl_a, color_a, alpha_float);
            gpu3d_hsl16_to_float_rgba(hsl_b, color_b, alpha_float);
            gpu3d_hsl16_to_float_rgba(hsl_c, color_c, alpha_float);
        }

        CommonVertex va{};
        va.position[0] = (float)vertices_x[face_a];
        va.position[1] = (float)vertices_y[face_a];
        va.position[2] = (float)vertices_z[face_a];
        va.position[3] = 1.0f;
        va.color[0] = color_a[0];
        va.color[1] = color_a[1];
        va.color[2] = color_a[2];
        va.color[3] = color_a[3];
        /* Raw floats from `uv_pnm_compute` (no CPU fract/tile). */
        va.texcoord[0] = uv.u1;
        va.texcoord[1] = uv.v1;
        va.tex_id = tex_id;
        va.uv_mode = 0u;
        batch_queue.vbo.push_back(VertexType::FromCommon(va));

        CommonVertex vb{};
        vb.position[0] = (float)vertices_x[face_b];
        vb.position[1] = (float)vertices_y[face_b];
        vb.position[2] = (float)vertices_z[face_b];
        vb.position[3] = 1.0f;
        vb.color[0] = color_b[0];
        vb.color[1] = color_b[1];
        vb.color[2] = color_b[2];
        vb.color[3] = color_b[3];
        vb.texcoord[0] = uv.u2;
        vb.texcoord[1] = uv.v2;
        vb.tex_id = tex_id;
        vb.uv_mode = 0u;
        batch_queue.vbo.push_back(VertexType::FromCommon(vb));

        CommonVertex vc{};
        vc.position[0] = (float)vertices_x[face_c];
        vc.position[1] = (float)vertices_y[face_c];
        vc.position[2] = (float)vertices_z[face_c];
        vc.position[3] = 1.0f;
        vc.color[0] = color_c[0];
        vc.color[1] = color_c[1];
        vc.color[2] = color_c[2];
        vc.color[3] = color_c[3];
        vc.texcoord[0] = uv.u3;
        vc.texcoord[1] = uv.v3;
        vc.tex_id = tex_id;
        vc.uv_mode = 0u;
        batch_queue.vbo.push_back(VertexType::FromCommon(vc));
    }

    batch_queue.batch.push_back(
        BatchedQueueModel::CreateModel(
            model_id, pose_index, new_vertex_count, vbo_vertex_start, faces_count, ebo_start));
}

template<typename VertexType>
inline const VertexType*
GPU3DCache2<VertexType>::BatchGetVBO() const
{
    return batch_queues.back().vbo.data();
}

template<typename VertexType>
inline uint16_t*
GPU3DCache2<VertexType>::BatchGetEBO()
{
    return batch_queues.back().ebo.data();
}

template<typename VertexType>
inline uint32_t
GPU3DCache2<VertexType>::BatchGetVBOVertexCount()
{
    return (uint32_t)batch_queues.back().vbo.size();
}

template<typename VertexType>
inline uint32_t
GPU3DCache2<VertexType>::BatchGetVBOSize()
{
    return BatchGetVBOVertexCount();
}

template<typename VertexType>
inline uint32_t
GPU3DCache2<VertexType>::BatchGetEBOSize()
{
    return batch_queues.back().ebo.size();
}

template<typename VertexType>
inline void
GPU3DCache2<VertexType>::InitAtlas(
    GPUResourceHandle handle,
    uint32_t width,
    uint32_t height)
{
    atlas.handle = handle;
    atlas.atlas_width = width;
    atlas.atlas_height = height;
    atlas.max_loaded_index = 0;

    // Pre-allocate the memory needed for the full atlas to avoid mid-load reallocations
    size_t total_atlas_bytes = width * height * BYTES_PER_PIXEL;
    atlas.pixel_buffer.assign(total_atlas_bytes, 0); // Initialize with 0s (transparent black)

    uint32_t columns = width / TEXTURE_DIMENSION;
    uint32_t rows = height / TEXTURE_DIMENSION;
    uint32_t max_capacity = columns * rows;

    float u_scale = static_cast<float>(TEXTURE_DIMENSION) / width;
    float v_scale = static_cast<float>(TEXTURE_DIMENSION) / height;

    for( int i = 0; i < MAX_TEXTURES; i++ )
    {
        if( i < max_capacity )
        {
            uint32_t col = i % columns;
            uint32_t row = i / columns;
            float u_offset = col * u_scale;
            float v_offset = row * v_scale;
            atlas.uv_region[i] = { u_offset, v_offset, u_scale, v_scale };
        }
        else
        {
            atlas.uv_region[i] = { 0.0f, 0.0f, 0.0f, 0.0f };
        }
    }
}

template<typename VertexType>
inline const uint8_t*
GPU3DCache2<VertexType>::GetAtlasPixelData() const
{
    return atlas.pixel_buffer.data();
}

template<typename VertexType>
inline uint32_t
GPU3DCache2<VertexType>::GetAtlasPixelDataSize() const
{
    return atlas.pixel_buffer.size();
}

template<typename VertexType>
inline void
GPU3DCache2<VertexType>::UnloadAtlas()
{
    atlas = GPUTextureAtlas{};
}

template<typename VertexType>
inline void
GPU3DCache2<VertexType>::LoadTexture128(
    uint16_t texture_id,
    const void* pixel_data)
{
    if( texture_id >= MAX_TEXTURES || !pixel_data )
        return;

    // Update tracking
    if( texture_id > atlas.max_loaded_index )
    {
        atlas.max_loaded_index = texture_id;
    }

    uint32_t columns = atlas.atlas_width / TEXTURE_DIMENSION;
    uint32_t col = texture_id % columns;
    uint32_t row = texture_id / columns;

    // We can't just memcpy the whole 128x128 block linearly because the atlas is wider than 128
    // pixels. We must copy it row by row into the correct position within the larger atlas buffer.

    uint32_t start_x = col * TEXTURE_DIMENSION;
    uint32_t start_y = row * TEXTURE_DIMENSION;
    uint32_t atlas_stride = atlas.atlas_width * BYTES_PER_PIXEL;
    uint32_t texture_stride = TEXTURE_DIMENSION * BYTES_PER_PIXEL;

    const uint8_t* src_pixels = static_cast<const uint8_t*>(pixel_data);

    for( uint32_t y = 0; y < TEXTURE_DIMENSION; ++y )
    {
        uint32_t dest_index = ((start_y + y) * atlas_stride) + (start_x * BYTES_PER_PIXEL);
        uint32_t src_index = y * texture_stride;

        std::memcpy(&atlas.pixel_buffer[dest_index], &src_pixels[src_index], texture_stride);
    }
}

template<typename VertexType>
inline uint32_t
GPU3DCache2<VertexType>::GetAtlasWidth() const
{
    return atlas.atlas_width;
}

template<typename VertexType>
inline uint32_t
GPU3DCache2<VertexType>::GetAtlasHeight() const
{
    return atlas.atlas_height;
}

template<typename VertexType>
inline const AtlasUVRect&
GPU3DCache2<VertexType>::GetAtlasUVRect(uint16_t texture_id) const
{
    static const AtlasUVRect kEmpty{};
    if( texture_id >= MAX_TEXTURES )
        return kEmpty;
    return atlas.uv_region[texture_id];
}

template<typename VertexType>
inline bool
GPU3DCache2<VertexType>::HasBufferedAtlasData() const
{
    return !atlas.pixel_buffer.empty();
}

template<typename VertexType>
inline const std::vector<BatchedQueueModel>&
GPU3DCache2<VertexType>::BatchGetTrackingData() const
{
    assert(!batch_queues.empty());
    return batch_queues.back().batch;
}

template<typename VertexType>
inline void
GPU3DCache2<VertexType>::SetModelPose(
    uint16_t model_id,
    uint32_t pose_index,
    const GPUModelPosedData& data)
{
    if( model_id >= MAX_3D_ASSETS )
        return;
    GPUModelData& md = models[model_id];
    if( pose_index >= md.poses.size() )
        md.poses.resize((size_t)pose_index + 1u);
    md.poses[pose_index] = data;
}

template<typename VertexType>
inline GPUModelPosedData
GPU3DCache2<VertexType>::GetModelPose(
    uint16_t model_id,
    uint32_t pose_index) const
{
    if( model_id >= MAX_3D_ASSETS )
        return {};
    const GPUModelData& md = models[model_id];
    if( pose_index >= md.poses.size() )
        return {};
    return md.poses[pose_index];
}

template<typename VertexType>
inline GPUModelPosedData
GPU3DCache2<VertexType>::GetModelPoseForDraw(
    uint16_t model_id,
    bool use_animation,
    int scene_animation_index,
    int frame_index) const
{
    if( model_id >= MAX_3D_ASSETS )
        return {};
    if( !use_animation )
        return GetModelPose(model_id, 0u);
    if( scene_animation_index < 0 || scene_animation_index > 1 || frame_index < 0 )
        return {};
    const GPUModelData& md = models[model_id];
    const uint32_t base = md.animation_offsets[(size_t)scene_animation_index + 1u];
    const uint32_t idx = base + (uint32_t)frame_index;
    return GetModelPose(model_id, idx);
}

template<typename VertexType>
inline void
GPU3DCache2<VertexType>::ClearModel(uint16_t model_id)
{
    if( model_id < MAX_3D_ASSETS )
    {
        standalone_retained_vbo_[model_id] = 0;
        standalone_retained_ebo_[model_id] = 0;
        models[model_id] = GPUModelData{};
    }
}

template<typename VertexType>
inline bool
GPU3DCache2<VertexType>::SceneBatchBegin(
    SceneBatchId scene_batch_id,
    ToriRS_UsageHint usage_hint)
{
    if( scene_batch_id >= kGPU3DCache2MaxSceneBatches )
        return false;
    if( scene_batch_lookup_[scene_batch_id].valid )
        invalidatePosesForSceneBatch(scene_batch_id);
    scene_batch_lookup_[scene_batch_id] = GPU3DCache2SceneBatchEntry{};
    scene_batch_lookup_[scene_batch_id].valid = true;
    scene_batch_lookup_[scene_batch_id].usage_hint = usage_hint;
    return true;
}

template<typename VertexType>
inline GPU3DCache2SceneBatchEntry*
GPU3DCache2<VertexType>::SceneBatchGet(SceneBatchId scene_batch_id)
{
    if( scene_batch_id >= kGPU3DCache2MaxSceneBatches )
        return nullptr;
    if( !scene_batch_lookup_[scene_batch_id].valid )
        return nullptr;
    return &scene_batch_lookup_[scene_batch_id];
}

template<typename VertexType>
inline const GPU3DCache2SceneBatchEntry*
GPU3DCache2<VertexType>::SceneBatchGet(SceneBatchId scene_batch_id) const
{
    if( scene_batch_id >= kGPU3DCache2MaxSceneBatches )
        return nullptr;
    if( !scene_batch_lookup_[scene_batch_id].valid )
        return nullptr;
    return &scene_batch_lookup_[scene_batch_id];
}

template<typename VertexType>
inline void
GPU3DCache2<VertexType>::SceneBatchAddModel(
    SceneBatchId scene_batch_id,
    uint16_t scene_model_id)
{
    GPU3DCache2SceneBatchEntry* e = SceneBatchGet(scene_batch_id);
    if( e )
        e->scene_model_ids.push_back(scene_model_id);
}

template<typename VertexType>
inline void
GPU3DCache2<VertexType>::SceneBatchSetResource(
    SceneBatchId scene_batch_id,
    const GPU3DCache2Resource& resource)
{
    GPU3DCache2SceneBatchEntry* e = SceneBatchGet(scene_batch_id);
    if( !e )
        return;
    e->resource = resource;
    e->resource.valid = (resource.vbo != 0u && resource.ebo != 0u);
}

template<typename VertexType>
inline GPU3DCache2SceneBatchEntry
GPU3DCache2<VertexType>::SceneBatchClear(SceneBatchId scene_batch_id)
{
    GPU3DCache2SceneBatchEntry out{};
    if( scene_batch_id < kGPU3DCache2MaxSceneBatches )
    {
        invalidatePosesForSceneBatch(scene_batch_id);
        out = scene_batch_lookup_[scene_batch_id];
        scene_batch_lookup_[scene_batch_id] = GPU3DCache2SceneBatchEntry{};
    }
    return out;
}

template<typename VertexType>
inline void
GPU3DCache2<VertexType>::SceneBatchReset()
{
    for( uint32_t i = 0; i < kGPU3DCache2MaxSceneBatches; i++ )
    {
        if( scene_batch_lookup_[i].valid )
            invalidatePosesForSceneBatch(i);
        scene_batch_lookup_[i] = GPU3DCache2SceneBatchEntry{};
    }
}

template<typename VertexType>
inline void
GPU3DCache2<VertexType>::SetStandaloneRetainedBuffers(
    uint16_t model_id,
    GPUResourceHandle vbo,
    GPUResourceHandle ebo)
{
    if( model_id >= MAX_3D_ASSETS )
        return;
    standalone_retained_vbo_[model_id] = vbo;
    standalone_retained_ebo_[model_id] = ebo;
}

template<typename VertexType>
inline GPU3DCache2Resource
GPU3DCache2<VertexType>::TakeStandaloneRetainedBuffers(uint16_t model_id)
{
    GPU3DCache2Resource r{};
    if( model_id >= MAX_3D_ASSETS )
        return r;
    r.vbo = standalone_retained_vbo_[model_id];
    r.ebo = standalone_retained_ebo_[model_id];
    r.valid = (r.vbo != 0u && r.ebo != 0u);
    standalone_retained_vbo_[model_id] = 0;
    standalone_retained_ebo_[model_id] = 0;
    return r;
}

/** World mesh cache: interleaved vertices match `MetalVertexPacked` / `GPU3DMeshVertex`. */

#endif