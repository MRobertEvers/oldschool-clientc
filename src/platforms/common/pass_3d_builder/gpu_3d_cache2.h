#ifndef GPU_3D_CACHE2_H
#define GPU_3D_CACHE2_H

#include "graphics/dash.h"
#include "graphics/uv_pnm.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

constexpr size_t MAX_3D_ASSETS = 32768;
constexpr size_t MAX_TEXTURES = 256;
constexpr size_t MAX_POSE_COUNT = 16;

// 1. x
// 2. y
// 3. z
// 4. Color (low)
// 5. Color (high)
// 6. u
// 7. v
// 8. tex_id (0xFFFF = untextured; 0..255 for atlas tiles, matches Shaders.metal ModelVertexV2)
constexpr size_t VERTEX_ATTRIBUTE_COUNT = 8;

/** Pack face-local UV into 0..65535 for Metal fragmentShader tile-local `in.texcoord`. */
inline uint16_t
gpu3d_pack_uv_tile01(float u)
{
    // float fu = u - std::floor(u);
    // if( fu < 0.f )
    //     fu = 0.f;
    // else if( fu > 1.f )
    //     fu = 1.f;
    // int q = (int)std::lrint(static_cast<double>(fu) * 65535.0);
    // if( q < 0 )
    //     q = 0;
    // if( q > 65535 )
    //     q = 65535;
    return static_cast<uint16_t>(u);
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
// - Metal: retain on `MetalCache2BatchEntry` / other owners; batch pose handles may be __bridge.
typedef uintptr_t GPUResourceHandle;

/** Platform-specific buffer handles (`MTLBuffer*` / GL name / D3D resource cast to
 *  `GPUResourceHandle`). Non-owning on Metal when filled from batch submit (`__bridge`); lifetime
 *  matches the renderer batch entry or standalone owner.
 *
 *  **Metal (`Pass3DBuilder2SubmitMetal`)**: when `gpu_batch_id != 0`, VBO/EBO are resolved
 * **first** from `model_cache2_batch_map`; if the map has no entry, the same non-owning `vbo`/`ebo`
 * stored here (same pointers as the batch) are used as fallback. When `gpu_batch_id == 0`, only
 * `vbo`/`ebo` (and offsets) apply. */
struct GPUModelPosedData
{
    GPUResourceHandle vbo = 0;
    GPUResourceHandle ebo = 0;
    /** Non-zero: merged world batch id (Metal: key into `model_cache2_batch_map`; map preferred,
     *  pose `vbo`/`ebo` fallback). */
    uint32_t gpu_batch_id = 0;
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

struct BatchQueue
{
    std::vector<BatchedQueueModel> batch;
    std::vector<uint16_t> vbo;
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

class GPU3DCache2
{
private:
    std::array<GPUModelData, MAX_3D_ASSETS> models;
    GPUTextureAtlas atlas;
    std::vector<BatchQueue> batch_queues;

    uint32_t
    allocatePoseSlot(
        uint16_t model_id,
        uint8_t gpu_segment_slot,
        uint16_t frame_index);

public:
    GPU3DCache2() = default;
    ~GPU3DCache2() = default;

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
        uint16_t* faces_c_color_hsl16);

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
        int16_t* faces_textures,
        uint16_t* textured_faces,
        uint16_t* textured_faces_a,
        uint16_t* textured_faces_b,
        uint16_t* textured_faces_c);

    uint32_t
    BatchGetVBOSize();

    uint32_t
    BatchGetEBOSize();

    uint16_t*
    BatchGetVBO();

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
    ClearModel(uint16_t model_id)
    {
        if( model_id < MAX_3D_ASSETS )
            models[model_id] = GPUModelData{};
    }
};

inline int
GPU3DCache2::BatchBegin()
{
    batch_queues.push_back(BatchQueue{});
    return 0;
}

inline void
GPU3DCache2::BatchEnd()
{
    batch_queues.pop_back();
}

inline uint32_t
GPU3DCache2::allocatePoseSlot(
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

inline void
GPU3DCache2::BatchAddModeli16(
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
    uint16_t* faces_c_color_hsl16)
{
    if( model_id == 6277 )
        printf(
            "BatchAddModeli16: model_id: %d, vertex_count: %d, faces_count: %d\n",
            model_id,
            vertex_count,
            faces_count);
    assert(vertex_count < 4096);
    assert(faces_count < 4096);
    BatchQueue& batch_queue = batch_queues.back();
    const uint32_t pose_index = allocatePoseSlot(model_id, gpu_segment_slot, frame_index);

    uint16_t color_low, color_high;

    // Track the actual starting VERTEX index, not the element count
    uint32_t vertex_offset = batch_queue.vbo.size() / VERTEX_ATTRIBUTE_COUNT;

    // Track element starts for your tracking struct if needed
    uint32_t vbo_element_start = batch_queue.vbo.size();
    uint32_t ebo_start = batch_queue.ebo.size();

    batch_queue.vbo.reserve(batch_queue.vbo.size() + vertex_count * VERTEX_ATTRIBUTE_COUNT);
    for( int i = 0; i < vertex_count; i++ )
    {
        batch_queue.vbo.push_back(vertices_x[i]);
        batch_queue.vbo.push_back(vertices_y[i]);
        batch_queue.vbo.push_back(vertices_z[i]);
        PackColorTo16Bit(faces_a_color_hsl16[i], color_low, color_high);
        batch_queue.vbo.push_back(color_low);
        batch_queue.vbo.push_back(color_high);
        batch_queue.vbo.push_back(0);
        batch_queue.vbo.push_back(0);
        batch_queue.vbo.push_back(0xFFFFu);
    }

    batch_queue.ebo.reserve(batch_queue.ebo.size() + faces_count * 3);
    for( int i = 0; i < faces_count; i++ )
    {
        // Add the vertex_offset, not the float offset
        batch_queue.ebo.push_back(faces_a[i] + vertex_offset);
        batch_queue.ebo.push_back(faces_b[i] + vertex_offset);
        batch_queue.ebo.push_back(faces_c[i] + vertex_offset);
    }

    batch_queue.batch.push_back(
        BatchedQueueModel::CreateModel(
            model_id, pose_index, vertex_count, vbo_element_start, faces_count, ebo_start));
}

inline void
GPU3DCache2::BatchAddModelTexturedi16(
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
    int16_t* faces_textures, // Array mapping each face to a texture ID (-1 = no texture)
    uint16_t* textured_faces,
    uint16_t* textured_faces_a,
    uint16_t* textured_faces_b,
    uint16_t* textured_faces_c)
{
    printf(
        "BatchAddModelTexturedi16: model_id: %d, vertex_count: %d, faces_count: %d\n",
        model_id,
        vertex_count,
        faces_count);
    assert(vertex_count < 4096);
    assert(faces_count < 4096);
    BatchQueue& batch_queue = batch_queues.back();
    const uint32_t pose_index = allocatePoseSlot(model_id, gpu_segment_slot, frame_index);

    uint16_t color_low, color_high;

    // The starting vertex index for this newly un-indexed mesh
    uint32_t vertex_offset = batch_queue.vbo.size() / VERTEX_ATTRIBUTE_COUNT;

    uint32_t vbo_element_start = batch_queue.vbo.size();
    uint32_t ebo_start = batch_queue.ebo.size();

    // Reserve based on the faces we are expanding, not the original vertex count
    uint32_t new_vertex_count = faces_count * 3;
    batch_queue.vbo.reserve(batch_queue.vbo.size() + new_vertex_count * VERTEX_ATTRIBUTE_COUNT);
    batch_queue.ebo.reserve(batch_queue.ebo.size() + faces_count * 3);

    uint32_t tp_vertex = 0;
    uint32_t tm_vertex = 0;
    uint32_t tn_vertex = 0;
    struct UVFaceCoords uv;
    // Use signed int16 aliases so negative vertex coords convert to float correctly.
    const int16_t* sx = reinterpret_cast<const int16_t*>(vertices_x);
    const int16_t* sy = reinterpret_cast<const int16_t*>(vertices_y);
    const int16_t* sz = reinterpret_cast<const int16_t*>(vertices_z);

    for( int i = 0; i < faces_count; i++ )
    {
        // The indices are strictly sequential.
        batch_queue.ebo.push_back(vertex_offset++);
        batch_queue.ebo.push_back(vertex_offset++);
        batch_queue.ebo.push_back(vertex_offset++);

        // Note: uint16_t is unsigned, so -1 becomes 0xFFFF.
        if( textured_faces && textured_faces[i] != 0xFFFF )
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
            (float)sx[tp_vertex],
            (float)sy[tp_vertex],
            (float)sz[tp_vertex],
            (float)sx[tm_vertex],
            (float)sy[tm_vertex],
            (float)sz[tm_vertex],
            (float)sx[tn_vertex],
            (float)sy[tn_vertex],
            (float)sz[tn_vertex],
            (float)sx[faces_a[i]],
            (float)sy[faces_a[i]],
            (float)sz[faces_a[i]],
            (float)sx[faces_b[i]],
            (float)sy[faces_b[i]],
            (float)sz[faces_b[i]],
            (float)sx[faces_c[i]],
            (float)sy[faces_c[i]],
            (float)sz[faces_c[i]]);

        // -1 (0xFFFF as int16) = face has no texture; fallback to solid color (tex_id 0xFFFF).
        uint16_t tex_id;
        if( faces_textures[i] != -1 )
            tex_id = (uint16_t)faces_textures[i];
        else
            tex_id = 0xFFFFu;

        // Push Vert A
        batch_queue.vbo.push_back(vertices_x[faces_a[i]]);
        batch_queue.vbo.push_back(vertices_y[faces_a[i]]);
        batch_queue.vbo.push_back(vertices_z[faces_a[i]]);
        PackColorTo16Bit(faces_a_color_hsl16[i], color_low, color_high);
        batch_queue.vbo.push_back(color_low);
        batch_queue.vbo.push_back(color_high);
        batch_queue.vbo.push_back(gpu3d_pack_uv_tile01(uv.u1));
        batch_queue.vbo.push_back(gpu3d_pack_uv_tile01(uv.v1));
        batch_queue.vbo.push_back(tex_id);

        // Push Vert B
        batch_queue.vbo.push_back(vertices_x[faces_b[i]]);
        batch_queue.vbo.push_back(vertices_y[faces_b[i]]);
        batch_queue.vbo.push_back(vertices_z[faces_b[i]]);
        PackColorTo16Bit(faces_b_color_hsl16[i], color_low, color_high);
        batch_queue.vbo.push_back(color_low);
        batch_queue.vbo.push_back(color_high);
        batch_queue.vbo.push_back(gpu3d_pack_uv_tile01(uv.u2));
        batch_queue.vbo.push_back(gpu3d_pack_uv_tile01(uv.v2));
        batch_queue.vbo.push_back(tex_id);

        // Push Vert C
        batch_queue.vbo.push_back(vertices_x[faces_c[i]]);
        batch_queue.vbo.push_back(vertices_y[faces_c[i]]);
        batch_queue.vbo.push_back(vertices_z[faces_c[i]]);
        PackColorTo16Bit(faces_c_color_hsl16[i], color_low, color_high);
        batch_queue.vbo.push_back(color_low);
        batch_queue.vbo.push_back(color_high);
        batch_queue.vbo.push_back(gpu3d_pack_uv_tile01(uv.u3));
        batch_queue.vbo.push_back(gpu3d_pack_uv_tile01(uv.v3));
        batch_queue.vbo.push_back(tex_id);
    }

    batch_queue.batch.push_back(
        BatchedQueueModel::CreateModel(
            model_id, pose_index, new_vertex_count, vbo_element_start, faces_count, ebo_start));
}

inline uint16_t*
GPU3DCache2::BatchGetVBO()
{
    return batch_queues.back().vbo.data();
}

inline uint16_t*
GPU3DCache2::BatchGetEBO()
{
    return batch_queues.back().ebo.data();
}

inline uint32_t
GPU3DCache2::BatchGetVBOSize()
{
    return batch_queues.back().vbo.size();
}

inline uint32_t
GPU3DCache2::BatchGetEBOSize()
{
    return batch_queues.back().ebo.size();
}

inline void
GPU3DCache2::InitAtlas(
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

inline const uint8_t*
GPU3DCache2::GetAtlasPixelData() const
{
    return atlas.pixel_buffer.data();
}

inline uint32_t
GPU3DCache2::GetAtlasPixelDataSize() const
{
    return atlas.pixel_buffer.size();
}

inline void
GPU3DCache2::UnloadAtlas()
{
    atlas = GPUTextureAtlas{};
}

inline void
GPU3DCache2::LoadTexture128(
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

inline uint32_t
GPU3DCache2::GetAtlasWidth() const
{
    return atlas.atlas_width;
}

inline uint32_t
GPU3DCache2::GetAtlasHeight() const
{
    return atlas.atlas_height;
}

inline const AtlasUVRect&
GPU3DCache2::GetAtlasUVRect(uint16_t texture_id) const
{
    static const AtlasUVRect kEmpty{};
    if( texture_id >= MAX_TEXTURES )
        return kEmpty;
    return atlas.uv_region[texture_id];
}

inline bool
GPU3DCache2::HasBufferedAtlasData() const
{
    return !atlas.pixel_buffer.empty();
}

inline const std::vector<BatchedQueueModel>&
GPU3DCache2::BatchGetTrackingData() const
{
    assert(!batch_queues.empty());
    return batch_queues.back().batch;
}

inline void
GPU3DCache2::SetModelPose(
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

inline GPUModelPosedData
GPU3DCache2::GetModelPose(
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

inline GPUModelPosedData
GPU3DCache2::GetModelPoseForDraw(
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

#endif