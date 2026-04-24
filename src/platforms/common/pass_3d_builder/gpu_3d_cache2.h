#ifndef GPU_3D_CACHE2_H
#define GPU_3D_CACHE2_H

#include "graphics/dash.h"
#include "graphics/uv_pnm.h"

#include <array>
#include <cassert>
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
constexpr size_t VERTEX_ATTRIBUTE_COUNT = 7;
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
    uint8_t a = (rgb >> 24) & 0xFF;

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
// - Metal: Use CFBridgingRetain/Release to cast id to GPUResourceHandle safely
typedef uintptr_t GPUResourceHandle;

struct GPUModelPosedData
{
    GPUResourceHandle vbo = 0;
    GPUResourceHandle ebo = 0;
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
    GPUModelPosedData poses[MAX_POSE_COUNT];
    uint32_t animation_offsets[3];
};

struct BatchedQueueModel
{
    bool is_va = false;
    uint16_t model_id;
    uint16_t animation_index;
    uint16_t frame_index;
    uint32_t vertex_count;
    uint32_t vbo_start;
    uint32_t face_count;
    uint32_t ebo_start;

    BatchedQueueModel() = default;

    BatchedQueueModel(
        bool is_va,
        uint16_t model_id,
        uint16_t animation_index,
        uint16_t frame_index,
        uint32_t vertex_count,
        uint32_t vbo_start,
        uint32_t face_count,
        uint32_t ebo_start)
        : is_va(is_va)
        , model_id(model_id)
        , animation_index(animation_index)
        , frame_index(frame_index)
        , vertex_count(vertex_count)
        , vbo_start(vbo_start)
        , face_count(face_count)
        , ebo_start(ebo_start)
    {}

    static BatchedQueueModel
    CreateModel(
        uint16_t model_id,
        uint16_t animation_index,
        uint16_t frame_index,
        uint32_t vertex_count,
        uint32_t vertices_start,
        uint32_t face_count,
        uint32_t ebo_start)
    {
        return BatchedQueueModel(
            false,
            model_id,
            animation_index,
            frame_index,
            vertex_count,
            vertices_start,
            face_count,
            ebo_start);
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
        uint16_t animation_index,
        uint16_t frame_index,
        uint32_t vertex_count,
        uint16_t* vertices_x,
        uint16_t* vertices_y,
        uint16_t* vertices_z,
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
        uint16_t animation_index,
        uint16_t frame_index,
        uint32_t vertex_count,
        uint16_t* vertices_x,
        uint16_t* vertices_y,
        uint16_t* vertices_z,
        uint32_t faces_count,
        uint16_t* faces_a,
        uint16_t* faces_b,
        uint16_t* faces_c,
        uint16_t* faces_a_color_hsl16,
        uint16_t* faces_b_color_hsl16,
        uint16_t* faces_c_color_hsl16,
        uint8_t* faces_textures,
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

    bool
    HasBufferedAtlasData() const;

    const std::vector<BatchedQueueModel>&
    BatchGetTrackingData() const;

    void
    SetModelPose(
        uint16_t model_id,
        uint16_t pose_id,
        const GPUModelPosedData& data);

    GPUModelPosedData
    GetModelPose(
        uint16_t model_id,
        uint16_t pose_id) const;

    /** Same geometry as pose 0; prefer GetModelPose when using multiple poses. */
    GPUModelPosedData
    GetModel(uint16_t model_id) const
    {
        return GetModelPose(model_id, 0);
    }

    /** Reset all pose slots for a model (call when unloading a batch that contains this model). */
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

inline void
GPU3DCache2::BatchAddModeli16(
    uint16_t model_id,
    uint16_t animation_index,
    uint16_t frame_index,
    uint32_t vertex_count,
    uint16_t* vertices_x,
    uint16_t* vertices_y,
    uint16_t* vertices_z,
    uint32_t faces_count,
    uint16_t* faces_a,
    uint16_t* faces_b,
    uint16_t* faces_c,
    uint16_t* faces_a_color_hsl16,
    uint16_t* faces_b_color_hsl16,
    uint16_t* faces_c_color_hsl16)
{
    BatchQueue& batch_queue = batch_queues.back();

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
            model_id,
            animation_index,
            frame_index,
            vertex_count,
            vbo_element_start,
            faces_count,
            ebo_start));
}

inline void
GPU3DCache2::BatchAddModelTexturedi16(
    uint16_t model_id,
    uint16_t animation_index,
    uint16_t frame_index,
    uint32_t vertex_count,
    uint16_t* vertices_x,
    uint16_t* vertices_y,
    uint16_t* vertices_z,
    uint32_t faces_count,
    uint16_t* faces_a,
    uint16_t* faces_b,
    uint16_t* faces_c,
    uint16_t* faces_a_color_hsl16,
    uint16_t* faces_b_color_hsl16,
    uint16_t* faces_c_color_hsl16,
    uint8_t* faces_textures, // Array mapping each face to a texture ID
    uint16_t* textured_faces,
    uint16_t* textured_faces_a,
    uint16_t* textured_faces_b,
    uint16_t* textured_faces_c)
{
    BatchQueue& batch_queue = batch_queues.back();

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

    for( int i = 0; i < faces_count; i++ )
    {
        // The indices are strictly sequential.
        batch_queue.ebo.push_back(vertex_offset++);
        batch_queue.ebo.push_back(vertex_offset++);
        batch_queue.ebo.push_back(vertex_offset++);

        // Note: uint16_t is unsigned, so -1 becomes 0xFFFF.
        if( textured_faces && textured_faces[i] != 0xFFFF )
        {
            tp_vertex = textured_faces_a[i];
            tm_vertex = textured_faces_b[i];
            tn_vertex = textured_faces_c[i];
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
            (float)vertices_x[faces_a[i]],
            (float)vertices_y[faces_a[i]],
            (float)vertices_z[faces_a[i]],
            (float)vertices_x[faces_b[i]],
            (float)vertices_y[faces_b[i]],
            (float)vertices_z[faces_b[i]],
            (float)vertices_x[faces_c[i]],
            (float)vertices_y[faces_c[i]],
            (float)vertices_z[faces_c[i]]);

        // Fallback to texture 0 if the array is null
        uint8_t tex_id;
        if( faces_textures[i] != 0xFF )
            tex_id = faces_textures[i];
        else
            tex_id = 0;

        const AtlasUVRect& atlas_rect = atlas.uv_region[tex_id];

        // Transform the local UVs to the global atlas space
        float final_u1 = (uv.u1 * atlas_rect.u_scale) + atlas_rect.u_offset;
        float final_v1 = (uv.v1 * atlas_rect.v_scale) + atlas_rect.v_offset;

        float final_u2 = (uv.u2 * atlas_rect.u_scale) + atlas_rect.u_offset;
        float final_v2 = (uv.v2 * atlas_rect.v_scale) + atlas_rect.v_offset;

        float final_u3 = (uv.u3 * atlas_rect.u_scale) + atlas_rect.u_offset;
        float final_v3 = (uv.v3 * atlas_rect.v_scale) + atlas_rect.v_offset;

        // Push Vert A
        batch_queue.vbo.push_back(vertices_x[faces_a[i]]);
        batch_queue.vbo.push_back(vertices_y[faces_a[i]]);
        batch_queue.vbo.push_back(vertices_z[faces_a[i]]);
        PackColorTo16Bit(faces_a_color_hsl16[i], color_low, color_high);
        batch_queue.vbo.push_back(color_low);
        batch_queue.vbo.push_back(color_high);
        batch_queue.vbo.push_back(static_cast<uint16_t>(final_u1));
        batch_queue.vbo.push_back(static_cast<uint16_t>(final_v1));

        // Push Vert B
        batch_queue.vbo.push_back(vertices_x[faces_b[i]]);
        batch_queue.vbo.push_back(vertices_y[faces_b[i]]);
        batch_queue.vbo.push_back(vertices_z[faces_b[i]]);
        PackColorTo16Bit(faces_b_color_hsl16[i], color_low, color_high);
        batch_queue.vbo.push_back(color_low);
        batch_queue.vbo.push_back(color_high);
        batch_queue.vbo.push_back(static_cast<uint16_t>(final_u2));
        batch_queue.vbo.push_back(static_cast<uint16_t>(final_v2));

        // Push Vert C
        batch_queue.vbo.push_back(vertices_x[faces_c[i]]);
        batch_queue.vbo.push_back(vertices_y[faces_c[i]]);
        batch_queue.vbo.push_back(vertices_z[faces_c[i]]);
        PackColorTo16Bit(faces_c_color_hsl16[i], color_low, color_high);
        batch_queue.vbo.push_back(color_low);
        batch_queue.vbo.push_back(color_high);
        batch_queue.vbo.push_back(static_cast<uint16_t>(final_u3));
        batch_queue.vbo.push_back(static_cast<uint16_t>(final_v3));
    }

    batch_queue.batch.push_back(
        BatchedQueueModel::CreateModel(
            model_id,
            animation_index,
            frame_index,
            new_vertex_count,
            vbo_element_start,
            faces_count,
            ebo_start));
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
    uint16_t pose_id,
    const GPUModelPosedData& data)
{
    if( model_id >= MAX_3D_ASSETS || pose_id >= MAX_POSE_COUNT )
        return;
    models[model_id].poses[pose_id] = data;
}

inline GPUModelPosedData
GPU3DCache2::GetModelPose(
    uint16_t model_id,
    uint16_t pose_id) const
{
    if( model_id >= MAX_3D_ASSETS || pose_id >= MAX_POSE_COUNT )
        return {};
    return models[model_id].poses[pose_id];
}

#endif