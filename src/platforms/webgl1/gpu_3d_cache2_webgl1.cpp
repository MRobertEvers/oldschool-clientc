#include "platforms/webgl1/gpu_3d_cache2_webgl1.h"

#ifdef __EMSCRIPTEN__

#    include <GLES2/gl2.h>

#    include <cstdio>
#    include <cstring>

static_assert(sizeof(GPU3DMeshVertexWebGL1) == 48u, "GPU3DMeshVertexWebGL1 stride");

void
GPU3DCache2BatchSubmitWebGL1(
    GPU3DCache2<GPU3DMeshVertexWebGL1>& cache,
    const WebGL1BatchBuffer& batch,
    BatchBuffersWebGL1& out_batch_buffers,
    SceneBatchId scene_batch_id,
    ToriRS_UsageHint usage_hint)
{
    const uint32_t vcount = (uint32_t)batch.vbo.size();
    const uint32_t vbo_bytes = vcount * (uint32_t)sizeof(GPU3DMeshVertexWebGL1);
    const uint32_t ebo_index_count = (uint32_t)batch.ebo.size();
    const uint32_t ebo_size = ebo_index_count * sizeof(uint16_t);

    if( vbo_bytes == 0 || ebo_size == 0 )
    {
        return;
    }

    const uint16_t* ebo_cpu = batch.ebo.data();
    uint32_t max_index = 0;
    for( uint32_t i = 0; i < ebo_index_count; ++i )
    {
        max_index = ebo_cpu[i] > max_index ? ebo_cpu[i] : max_index;
    }
    if( vcount == 0 || max_index >= vcount )
    {
        fprintf(
            stderr,
            "GPU3DCache2BatchSubmitWebGL1: EBO index out of range (max_index=%u vertex_count=%u)\n",
            max_index,
            vcount);
        return;
    }

    GLuint vbo = 0;
    GLuint ebo = 0;
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    if( vbo == 0 || ebo == 0 )
    {
        fprintf(stderr, "GPU3DCache2BatchSubmitWebGL1: glGenBuffers failed\n");
        if( vbo )
            glDeleteBuffers(1, &vbo);
        if( ebo )
            glDeleteBuffers(1, &ebo);
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vbo_bytes, batch.vbo.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ebo_size, ebo_cpu, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    const GPUResourceHandle vbo_handle = (GPUResourceHandle)(uintptr_t)vbo;
    const GPUResourceHandle ebo_handle = (GPUResourceHandle)(uintptr_t)ebo;

    const GPUBatchId merge_gpu_batch_id = cache.AllocGpuBatchId(scene_batch_id);

    const GPU3DResourcePolicy pose_policy = GPU3DCache2PolicyForUsageHint(usage_hint);

    GPUModelPosedData pose_data;
    for( const BatchedQueueModel& batched_model : batch.tracking )
    {
        pose_data.vbo = vbo_handle;
        pose_data.ebo = ebo_handle;
        pose_data.gpu_batch_id = merge_gpu_batch_id;
        pose_data.scene_batch_id =
            (scene_batch_id < kGPU3DCache2MaxSceneBatches) ? scene_batch_id : kSceneBatchSlotNone;
        pose_data.vbo_offset = batched_model.vbo_start;
        pose_data.ebo_offset = batched_model.ebo_start;
        pose_data.element_count = batched_model.face_count * 3;
        pose_data.policy = pose_policy;
        pose_data.valid = true;
        cache.SetModelPose(batched_model.model_id, batched_model.pose_index, pose_data);
    }

    out_batch_buffers.vbo = vbo;
    out_batch_buffers.ebo = ebo;
}

GLuint
GPU3DCache2SubmitAtlasWebGL1(GPU3DCache2<GPU3DMeshVertexWebGL1>& cache)
{
    if( !cache.HasBufferedAtlasData() )
        return 0u;

    GLuint tex = 0u;
    glGenTextures(1, &tex);
    if( tex == 0u )
        return 0u;

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const int w = (int)cache.GetAtlasWidth();
    const int h = (int)cache.GetAtlasHeight();
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        w,
        h,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        cache.GetAtlasPixelData());
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

#endif // __EMSCRIPTEN__
