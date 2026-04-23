// System ObjC/Metal headers must come before any game headers.
#include "platforms/common/mesh_builder/mesh_builder.h"
#include "platforms/metal/metal_internal.h"

// Verify that MeshVertex has the same byte size as MetalVertex so that the
// raw vertex buffer can be passed to Metal without any conversion.
static_assert(
    sizeof(MeshVertex) == sizeof(MetalVertex),
    "MeshVertex layout must match MetalVertex");

/**
 * MeshBuilderSubmitMetal — upload MeshBuilder geometry to a new MTLBuffer.
 *
 * Creates one MTLBuffer for the vertex data and one for the index data.
 * Both buffers are bridge-retained; the caller is responsible for releasing
 * them (via CFRelease or __bridge_transfer) when they are no longer needed.
 *
 * @param builder  Populated MeshBuilder; may be empty (returns null handles).
 * @param ctx      Active MetalRenderCtx for the current frame.
 * @param out_vbo  Receives the bridge-retained vertex MTLBuffer, or nullptr.
 * @param out_ibo  Receives the bridge-retained index MTLBuffer, or nullptr.
 *                 nullptr when the builder has no indices.
 */
void
MeshBuilderSubmitMetal(
    MeshBuilder& builder,
    MetalRenderCtx* ctx,
    void** out_vbo,
    void** out_ibo)
{
    if( out_vbo )
        *out_vbo = nullptr;
    if( out_ibo )
        *out_ibo = nullptr;

    if( !ctx || !ctx->device || builder.empty() )
        return;

    // -----------------------------------------------------------------------
    // Vertex buffer
    // -----------------------------------------------------------------------
    const std::vector<MeshVertex>& verts = builder.vertices();
    const size_t vbo_bytes = verts.size() * sizeof(MeshVertex);

    id<MTLBuffer> vbo = [ctx->device newBufferWithBytes:verts.data()
                                                 length:(NSUInteger)vbo_bytes
                                                options:MTLResourceStorageModeShared];
    if( !vbo )
    {
        fprintf(
            stderr,
            "[mesh_builder_metal] failed to create vertex MTLBuffer (%zu bytes)\n",
            vbo_bytes);
        return;
    }

    if( out_vbo )
        *out_vbo = (__bridge_retained void*)vbo;

    // -----------------------------------------------------------------------
    // Index buffer (optional — only created when indices are present)
    // -----------------------------------------------------------------------
    const std::vector<uint32_t>& indices = builder.indices();
    if( !indices.empty() && out_ibo )
    {
        const size_t ibo_bytes = indices.size() * sizeof(uint32_t);
        id<MTLBuffer> ibo = [ctx->device newBufferWithBytes:indices.data()
                                                     length:(NSUInteger)ibo_bytes
                                                    options:MTLResourceStorageModeShared];
        if( ibo )
            *out_ibo = (__bridge_retained void*)ibo;
        else
            fprintf(
                stderr,
                "[mesh_builder_metal] failed to create index MTLBuffer (%zu bytes)\n",
                ibo_bytes);
    }
}
