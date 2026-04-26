#include "gpu_3d_cache2_webgl1.h"

#ifdef __EMSCRIPTEN__

#    include <GLES2/gl2.h>

#    include <cstdio>
#    include <cstring>

static_assert(sizeof(GPU3DMeshVertexWebGL1) == 48u, "GPU3DMeshVertexWebGL1 stride");

void
GPU3DCache2BatchSubmitWebGL1(
    GPU3DCache2<GPU3DMeshVertexWebGL1>& cache,
    BatchBuffersWebGL1& out_batch_buffers,
    uint32_t batch_id)
{
    (void)batch_id;
    const uint32_t vcount = cache.BatchGetVBOVertexCount();
    const uint32_t vbo_bytes = vcount * (uint32_t)sizeof(GPU3DMeshVertexWebGL1);
    const uint32_t ebo_index_count = cache.BatchGetEBOSize();
    const uint32_t ebo_size = ebo_index_count * sizeof(uint16_t);

    if( vbo_bytes == 0 || ebo_size == 0 )
    {
        cache.BatchEnd();
        return;
    }

    const uint16_t* ebo_cpu = cache.BatchGetEBO();
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
        cache.BatchEnd();
        return;
    }

    GLuint vbo = 0;
    GLuint ebo = 0;
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    if( vbo == 0 || ebo == 0 )
    {
        fprintf(stderr, "GPU3DCache2BatchSubmitWebGL1: glGenBuffers failed\n");
        cache.BatchEnd();
        if( vbo )
            glDeleteBuffers(1, &vbo);
        if( ebo )
            glDeleteBuffers(1, &ebo);
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vbo_bytes, cache.BatchGetVBO(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ebo_size, ebo_cpu, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    const GPUResourceHandle vbo_handle = (GPUResourceHandle)(uintptr_t)vbo;
    const GPUResourceHandle ebo_handle = (GPUResourceHandle)(uintptr_t)ebo;

    GPUModelPosedData pose_data;
    for( const BatchedQueueModel& batched_model : cache.BatchGetTrackingData() )
    {
        pose_data.vbo = vbo_handle;
        pose_data.ebo = ebo_handle;
        pose_data.gpu_batch_id = batch_id;
        pose_data.vbo_offset = batched_model.vbo_start;
        pose_data.ebo_offset = batched_model.ebo_start;
        pose_data.element_count = batched_model.face_count * 3;
        pose_data.valid = true;
        cache.SetModelPose(batched_model.model_id, batched_model.pose_index, pose_data);
    }

    cache.BatchEnd();

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
