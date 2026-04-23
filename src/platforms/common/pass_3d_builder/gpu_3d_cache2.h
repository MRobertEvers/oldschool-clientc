#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

constexpr size_t MAX_3D_ASSETS = 32768;

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

struct BatchedQueueFaceArray
{
    uint32_t fa_id;
    uint32_t face_count;
    uint32_t faces_start;

    BatchedQueueFaceArray(
        uint32_t fa_id,
        uint32_t face_count,
        uint32_t faces_start)
        : fa_id(fa_id)
        , face_count(face_count)
        , faces_start(faces_start)
    {}

    static BatchedQueueFaceArray
    CreateFA(
        uint32_t fa_id,
        uint32_t face_count,
        uint32_t faces_start)
    {
        return BatchedQueueFaceArray(fa_id, face_count, faces_start);
    }
};

struct BatchedQueueVertexArray
{
    uint32_t va_id;
    uint32_t vertex_count;
    uint32_t vertices_start;

    BatchedQueueVertexArray(
        uint32_t va_id,
        uint32_t vertex_count,
        uint32_t vertices_start)
        : va_id(va_id)
        , vertex_count(vertex_count)
        , vertices_start(vertices_start)
    {}

    static BatchedQueueVertexArray
    CreateVA(
        uint32_t va_id,
        uint32_t vertex_count,
        uint32_t vertices_start)
    {
        return BatchedQueueVertexArray(va_id, vertex_count, vertices_start);
    }
};

struct BatchedQueueModel
{
    bool is_va;
    uint16_t model_id;
    uint16_t va_id;
    uint16_t fa_id;
    uint32_t vertex_count;
    uint32_t vertices_start;
    uint32_t face_count;
    uint32_t ebo_start;

    BatchedQueueModel(
        bool is_va,
        uint16_t model_id,
        uint16_t va_id,
        uint16_t fa_id,
        uint32_t vertex_count,
        uint32_t vertices_start,
        uint32_t face_count,
        uint32_t ebo_start)
        : is_va(is_va)
        , model_id(model_id)
        , va_id(va_id)
        , vertex_count(vertex_count)
        , vertices_start(vertices_start)
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

enum class BatchedQueueElementType
{
    Model,
    VA,
    FA,
};

struct BatchedQueueElement
{
    BatchedQueueElementType type;
    union
    {
        BatchedQueueModel model;
        BatchedQueueVertexArray va;
        BatchedQueueFaceArray fa;
    } u;
};

struct BatchQueue
{
    std::vector<BatchedQueueElement> batch;
    std::vector<uint16_t> vbo;
    std::vector<uint16_t> ebo;
};

class GPU3DCache2
{
private:
    std::array<GPUModelData, MAX_3D_ASSETS> models;

    std::vector<BatchQueue> batch_queues;

public:
    GPU3DCache2();
    ~GPU3DCache2();

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
    BatchAddFAi16(
        uint16_t fa_id,
        uint32_t face_count,
        uint16_t* faces_a,
        uint16_t* faces_b,
        uint16_t* faces_c);

    void
    BatchAddVAi16(
        uint16_t va_id,
        uint32_t vertex_count,
        uint16_t* vertices_x,
        uint16_t* vertices_y,
        uint16_t* vertices_z);

private:
    BatchedQueueElement*
    BatchGetVAElement(uint16_t va_id);

    BatchedQueueElement*
    BatchGetFAElement(uint16_t fa_id);
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

    uint32_t vbo_start = batch_queue.vbo.size();
    batch_queue.vbo.reserve(batch_queue.vbo.size() + vertex_count * 3);
    for( int i = 0; i < vertex_count; i++ )
    {
        batch_queue.vbo.push_back(vertices_x[i]);
        batch_queue.vbo.push_back(vertices_y[i]);
        batch_queue.vbo.push_back(vertices_z[i]);
    }

    uint32_t ebo_start = batch_queue.ebo.size();
    batch_queue.ebo.reserve(batch_queue.ebo.size() + faces_count * 3);
    for( int i = 0; i < faces_count; i++ )
    {
        batch_queue.ebo.push_back(faces_a[i]);
        batch_queue.ebo.push_back(faces_b[i]);
        batch_queue.ebo.push_back(faces_c[i]);
    }

    batch_queue.batch.emplace_back(
        BatchedQueueModel::CreateModel(model_id, vertex_count, vbo_start, faces_count, ebo_start));
}

inline void
GPU3DCache2::BatchAddModelVAi16(
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
    uint16_t* faces_c)
{
    BatchQueue& batch_queue = batch_queues.back();

    BatchedQueueElement* va_element = BatchGetVAElement(va_id);
    if( va_element == nullptr )
    {
        assert(false && "VA element not found");
        return;
    }

    BatchedQueueElement* fa_element = BatchGetFAElement(fa_id);
    if( fa_element == nullptr )
    {
        assert(false && "FA element not found");
        return;
    }

    batch_queue.batch.emplace_back(
        BatchedQueueModel::CreateVA(
            model_id,
            va_id,
            fa_id,
            vertex_count,
            va_element->u.va.vertices_start,
            faces_count,
            fa_element->u.fa.faces_start));
}

inline void
GPU3DCache2::BatchAddFAi16(
    uint16_t fa_id,
    uint32_t face_count,
    uint16_t* faces_a,
    uint16_t* faces_b,
    uint16_t* faces_c)
{
    BatchQueue& batch_queue = batch_queues.back();

    uint32_t ebo_start = batch_queue.ebo.size();
    batch_queue.ebo.reserve(batch_queue.ebo.size() + face_count * 3);
    for( int i = 0; i < face_count; i++ )
    {
        batch_queue.ebo.push_back(faces_a[i]);
        batch_queue.ebo.push_back(faces_b[i]);
        batch_queue.ebo.push_back(faces_c[i]);
    }

    batch_queue.batch.emplace_back(BatchedQueueFaceArray::CreateFA(fa_id, face_count, ebo_start));
}

inline BatchedQueueElement*
GPU3DCache2::BatchGetVAElement(uint16_t va_id)
{
    for( auto& batch_queue : batch_queues )
    {
        for( auto& element : batch_queue.batch )
        {
            if( element.type == BatchedQueueElementType::VA && element.u.va.va_id == va_id )
                return &element;
        }
    }
}