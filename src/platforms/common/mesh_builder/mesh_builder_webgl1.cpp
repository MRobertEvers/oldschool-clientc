#include "platforms/common/mesh_builder/mesh_builder.h"
#include "platforms/webgl1/webgl1_internal.h"

// Verify that MeshVertex has the same byte size as WgVertex so that the
// raw vertex buffer can be passed to WebGL1 without any conversion.
static_assert(
    sizeof(MeshVertex) == sizeof(WgVertex),
    "MeshVertex layout must match WgVertex");

/**
 * MeshBuilderSubmitWebGL1 — upload MeshBuilder geometry to a new GL VBO.
 *
 * Creates a GL_ARRAY_BUFFER VBO from the vertex data using GL_STATIC_DRAW,
 * then inserts a copy of the vertices into renderer->geometry_mirror so that
 * wg1_flush_3d can expand the draw stream on the CPU side.
 *
 * The caller is responsible for deleting the VBO (glDeleteBuffers) when it
 * is no longer needed and erasing the corresponding entry from
 * renderer->geometry_mirror.
 *
 * @param builder  Populated MeshBuilder.
 * @param ctx      Active WebGL1RenderCtx for the current frame.
 * @return         The new VBO name, or 0 on error.
 */
GLuint
MeshBuilderSubmitWebGL1(
    MeshBuilder& builder,
    WebGL1RenderCtx* ctx)
{
    if( !ctx || !ctx->renderer || builder.empty() )
        return 0;

    const std::vector<MeshVertex>& verts = builder.vertices();
    const size_t vbo_bytes = verts.size() * sizeof(MeshVertex);

    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    if( !vbo )
    {
        fprintf(stderr, "[mesh_builder_webgl1] glGenBuffers failed\n");
        return 0;
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vbo_bytes, verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Mirror into geometry_mirror so wg1_flush_3d can read vertex data back
    // on the CPU side (WebGL1 has no GPU texture buffer fetch).
    // MeshVertex and WgVertex share the same memory layout; cast is safe.
    const WgVertex* wg_verts = reinterpret_cast<const WgVertex*>(verts.data());
    ctx->renderer->geometry_mirror[vbo] = std::vector<WgVertex>(wg_verts, wg_verts + verts.size());

    return vbo;
}
