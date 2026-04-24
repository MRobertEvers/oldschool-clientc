#ifndef GPU_3D_CACHE2_H
#define GPU_3D_CACHE2_H

#include "uv_pnm.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

constexpr size_t MAX_3D_ASSETS = 32768;
constexpr size_t MAX_TEXTURES = 256;

// 1. x
// 2. y
// 3. z
// 4. u
// 5. v
constexpr size_t VERTEX_ATTRIBUTE_COUNT = 5;
// Assuming 4 bytes per pixel (RGBA8) for a 128x128 texture
constexpr size_t BYTES_PER_PIXEL = 4;
constexpr size_t TEXTURE_DIMENSION = 128;
constexpr size_t BYTES_PER_TEXTURE = TEXTURE_DIMENSION * TEXTURE_DIMENSION * BYTES_PER_PIXEL;

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

struct GPUModelData
{
    GPUResourceHandle vbo;
    GPUResourceHandle ebo;
    uint32_t vbo_offset;
    uint32_t ebo_offset;
    bool valid = false;
};

struct BatchedQueueModel
{
    bool is_va = false;
    uint16_t model_id = 0;
    uint16_t va_id = 0;
    uint16_t fa_id = 0;
    uint32_t vertex_count = 0;
    uint32_t vbo_start = 0;
    uint32_t face_count = 0;
    uint32_t ebo_start = 0;

    BatchedQueueModel() = default;

    BatchedQueueModel(
        bool is_va,
        uint16_t model_id,
        uint16_t va_id,
        uint16_t fa_id,
        uint32_t vertex_count,
        uint32_t vbo_start,
        uint32_t face_count,
        uint32_t ebo_start)
        : is_va(is_va)
        , model_id(model_id)
        , va_id(va_id)
        , vertex_count(vertex_count)
        , vbo_start(vbo_start)
        , face_count(face_count)
        , ebo_start(ebo_start)
    {}

    static BatchedQueueModel
    CreateModel(
        uint16_t model_id,
        uint32_t vertex_count,
        uint32_t vertices_start,
        uint32_t face_count,
        uint32_t ebo_start)
    {
        return BatchedQueueModel(
            false, model_id, 0, 0, vertex_count, vertices_start, face_count, ebo_start);
    }

    static BatchedQueueModel
    CreateVA(
        uint16_t model_id,
        uint16_t va_id,
        uint16_t fa_id,
        uint32_t vertex_count,
        uint32_t vertices_start,
        uint32_t face_count,
        uint32_t ebo_start)
    {
        return BatchedQueueModel(
            true, model_id, va_id, fa_id, vertex_count, vertices_start, face_count, ebo_start);
    }
};

struct BatchQueue
{
    std::vector<BatchedQueueModel> batch;
    std::vector<uint16_t> vbo;
    std::vector<uint16_t> ebo;
};

// --- ADDED: Texture Data Struct ---
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
    GPU3DCache2();
    ~GPU3DCache2();

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

    int
    BatchBegin();
    void
    BatchEnd();

    void
    BatchAddModeli16(
        uint16_t model_id,
        uint32_t vertex_count,
        uint16_t* vertices_x,
        uint16_t* vertices_y,
        uint16_t* vertices_z,
        uint32_t faces_count,
        uint16_t* faces_a,
        uint16_t* faces_b,
        uint16_t* faces_c);

    void
    BatchAddModelVAi16(
        uint16_t model_id,
        uint16_t va_id,
        uint16_t fa_id,
        uint32_t vertex_count,
        uint16_t* vertices_x,
        uint16_t* vertices_y,
        uint16_t* vertices_z,
        uint32_t faces_count,
        uint16_t* faces_a,
        uint16_t* faces_b,
        uint16_t* faces_c);

    void
    BatchAddModelTexturedi16(
        uint16_t model_id,
        uint32_t vertex_count,
        uint16_t* vertices_x,
        uint16_t* vertices_y,
        uint16_t* vertices_z,
        uint32_t faces_count,
        uint16_t* faces_a,
        uint16_t* faces_b,
        uint16_t* faces_c,
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

    const uint8_t*
    GetAtlasPixelData() const;

    uint32_t
    GetAtlasWidth() const;

    uint32_t
    GetAtlasHeight() const;

    bool
    HasBufferedAtlasData() const;

private:
    BatchedQueueModel*
    BatchGetModelElement(uint16_t model_id);
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
    uint32_t vertex_count,
    uint16_t* vertices_x,
    uint16_t* vertices_y,
    uint16_t* vertices_z,
    uint32_t faces_count,
    uint16_t* faces_a,
    uint16_t* faces_b,
    uint16_t* faces_c)
{
    BatchQueue& batch_queue = batch_queues.back();

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
            model_id, vertex_count, vbo_element_start, faces_count, ebo_start));
}

inline void
GPU3DCache2::BatchAddModelTexturedi16(
    uint16_t model_id,
    uint32_t vertex_count,
    uint16_t* vertices_x,
    uint16_t* vertices_y,
    uint16_t* vertices_z,
    uint32_t faces_count,
    uint16_t* faces_a,
    uint16_t* faces_b,
    uint16_t* faces_c,
    uint16_t* textured_faces,
    uint16_t* textured_faces_a,
    uint16_t* textured_faces_b,
    uint16_t* textured_faces_c)
{
    BatchQueue& batch_queue = batch_queues.back();

    // The starting vertex index for this newly un-indexed mesh
    uint32_t vertex_offset = batch_queue.vbo.size() / VERTEX_ATTRIBUTE_COUNT;

    uint32_t vbo_element_start = batch_queue.vbo.size();
    uint32_t ebo_start = batch_queue.ebo.size();

    // FIX: Reserve based on the faces we are expanding, not the original vertex count
    uint32_t new_vertex_count = faces_count * 3;
    batch_queue.vbo.reserve(batch_queue.vbo.size() + new_vertex_count * VERTEX_ATTRIBUTE_COUNT);
    batch_queue.ebo.reserve(batch_queue.ebo.size() + faces_count * 3);

    uint32_t tp_vertex = 0;
    uint32_t tm_vertex = 0;
    uint32_t tn_vertex = 0;
    struct UVFaceCoords uv;

    for( int i = 0; i < faces_count; i++ )
    {
        // FIX: Because we push 3 new vertices per face below,
        // the indices are strictly sequential.
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

        // Push Vert A
        batch_queue.vbo.push_back(vertices_x[faces_a[i]]);
        batch_queue.vbo.push_back(vertices_y[faces_a[i]]);
        batch_queue.vbo.push_back(vertices_z[faces_a[i]]);
        batch_queue.vbo.push_back(static_cast<uint16_t>(uv.u1));
        batch_queue.vbo.push_back(static_cast<uint16_t>(uv.v1));

        // Push Vert B
        batch_queue.vbo.push_back(vertices_x[faces_b[i]]);
        batch_queue.vbo.push_back(vertices_y[faces_b[i]]);
        batch_queue.vbo.push_back(vertices_z[faces_b[i]]);
        batch_queue.vbo.push_back(static_cast<uint16_t>(uv.u2));
        batch_queue.vbo.push_back(static_cast<uint16_t>(uv.v2));

        // Push Vert C
        batch_queue.vbo.push_back(vertices_x[faces_c[i]]);
        batch_queue.vbo.push_back(vertices_y[faces_c[i]]);
        batch_queue.vbo.push_back(vertices_z[faces_c[i]]);
        batch_queue.vbo.push_back(static_cast<uint16_t>(uv.u3));
        batch_queue.vbo.push_back(static_cast<uint16_t>(uv.v3));
    }

    batch_queue.batch.push_back(
        BatchedQueueModel::CreateModel(
            model_id, new_vertex_count, vbo_element_start, faces_count, ebo_start));
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

inline const uint8_t*
GPU3DCache2::GetAtlasPixelData() const
{
    return atlas.pixel_buffer.data();
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

#endif