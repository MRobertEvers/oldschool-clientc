#include "pass_3d_builder2_webgl1.h"

#ifdef __EMSCRIPTEN__

#    include "gpu_3d_cache2.h"
#    include "platforms/common/gpu_3d.h"
#    include "platforms/platform_impl2_sdl2_renderer_webgl1.h"

#    include <GLES2/gl2.h>

#    include <cstdio>
#    include <cstring>
#    include <vector>

static void
webgl1_upload_tile_uniforms(
    GLuint program,
    const WebGL1WorldShaderLocs& L,
    const std::vector<WebGL1AtlasTile>& tiles)
{
    if( L.u_tileA < 0 || L.u_tileB < 0 || tiles.size() < (size_t)256 )
        return;
    float A[256 * 4];
    float B[256 * 4];
    for( int i = 0; i < 256; ++i )
    {
        const WebGL1AtlasTile& t = tiles[(size_t)i];
        A[i * 4 + 0] = t.u0;
        A[i * 4 + 1] = t.v0;
        A[i * 4 + 2] = t.du;
        A[i * 4 + 3] = t.dv;
        B[i * 4 + 0] = t.anim_u;
        B[i * 4 + 1] = t.anim_v;
        B[i * 4 + 2] = t.opaque;
        B[i * 4 + 3] = t._pad;
    }
    glUniform4fv(L.u_tileA, 256, A);
    glUniform4fv(L.u_tileB, 256, B);
}

static void
bind_mesh_attribs_stride48(GLuint mesh_vbo, const WebGL1WorldShaderLocs& L)
{
    glBindBuffer(GL_ARRAY_BUFFER, mesh_vbo);
    if( L.a_position >= 0 )
    {
        glEnableVertexAttribArray((GLuint)L.a_position);
        glVertexAttribPointer((GLuint)L.a_position, 4, GL_FLOAT, GL_FALSE, 48, (void*)0);
    }
    if( L.a_color >= 0 )
    {
        glEnableVertexAttribArray((GLuint)L.a_color);
        glVertexAttribPointer((GLuint)L.a_color, 4, GL_FLOAT, GL_FALSE, 48, (void*)16);
    }
    if( L.a_texcoord >= 0 )
    {
        glEnableVertexAttribArray((GLuint)L.a_texcoord);
        glVertexAttribPointer((GLuint)L.a_texcoord, 2, GL_FLOAT, GL_FALSE, 48, (void*)32);
    }
    if( L.a_tex_id >= 0 )
    {
        glEnableVertexAttribArray((GLuint)L.a_tex_id);
        glVertexAttribPointer((GLuint)L.a_tex_id, 1, GL_FLOAT, GL_FALSE, 48, (void*)40);
    }
    if( L.a_uv_mode >= 0 )
    {
        glEnableVertexAttribArray((GLuint)L.a_uv_mode);
        glVertexAttribPointer((GLuint)L.a_uv_mode, 1, GL_FLOAT, GL_FALSE, 48, (void*)44);
    }
}

static void
disable_mesh_attribs(const WebGL1WorldShaderLocs& L)
{
    if( L.a_position >= 0 )
        glDisableVertexAttribArray((GLuint)L.a_position);
    if( L.a_color >= 0 )
        glDisableVertexAttribArray((GLuint)L.a_color);
    if( L.a_texcoord >= 0 )
        glDisableVertexAttribArray((GLuint)L.a_texcoord);
    if( L.a_tex_id >= 0 )
        glDisableVertexAttribArray((GLuint)L.a_tex_id);
    if( L.a_uv_mode >= 0 )
        glDisableVertexAttribArray((GLuint)L.a_uv_mode);
}

static void
webgl1_set_instance_uniforms(const WebGL1WorldShaderLocs& L, const GPU3DTransformUniformMetal& inst)
{
    if( L.u_inst0 >= 0 )
        glUniform4f(L.u_inst0, inst.cos_yaw, inst.sin_yaw, inst.x, inst.y);
    if( L.u_inst1 >= 0 )
        glUniform4f(L.u_inst1, inst.z, 0.0f, 0.0f, 0.0f);
}

namespace
{

struct WebGLResolvedMesh
{
    GPUModelPosedData model{};
    GLuint vbo = 0u;
    GLuint ebo = 0u;
    bool drawable = false;
};

WebGLResolvedMesh
webgl1_resolve_static_mesh_for_cmd(
    const GPU3DCache2<GPU3DMeshVertexWebGL1>& cache,
    const DrawModel3D& cmd)
{
    WebGLResolvedMesh out;
    if( cmd.dynamic_index_count > 0u )
        return out;

    out.model = cache.GetModelPoseForDraw(
        cmd.model_id, cmd.use_animation, (int)cmd.animation_index, (int)cmd.frame_index);
    if( !out.model.valid )
        return out;

    GLuint vbo = 0u;
    GLuint ebo = 0u;
    if( out.model.scene_batch_id < kGPU3DCache2MaxSceneBatches )
    {
        const GPU3DCache2SceneBatchEntry* be = cache.SceneBatchGet(out.model.scene_batch_id);
        if( be && be->resource.valid )
        {
            vbo = (GLuint)(uintptr_t)be->resource.vbo;
            ebo = (GLuint)(uintptr_t)be->resource.ebo;
        }
    }
    if( vbo == 0u )
        vbo = (GLuint)(uintptr_t)out.model.vbo;
    if( ebo == 0u )
        ebo = (GLuint)(uintptr_t)out.model.ebo;
    if( vbo == 0u || ebo == 0u )
        return out;

    out.vbo = vbo;
    out.ebo = ebo;
    out.drawable = true;
    return out;
}

} // namespace

void
Pass3DBuilder2SubmitWebGL1(
    Pass3DBuilder2<GPU3DTransformUniformMetal>& builder,
    const GPU3DCache2<GPU3DMeshVertexWebGL1>& cache,
    struct Platform2_SDL2_Renderer_WebGL1* webgl_renderer,
    GLuint program,
    const WebGL1WorldShaderLocs& locs,
    GLuint fragment_atlas_texture,
    const float modelViewMatrix[16],
    const float projectionMatrix[16],
    float u_clock)
{
    if( !builder.HasCommands() || program == 0u )
        return;

    const std::vector<GPU3DTransformUniformMetal>& inst_pool = builder.GetInstancePool();

    glUseProgram(program);
    if( locs.u_modelViewMatrix >= 0 )
        glUniformMatrix4fv(locs.u_modelViewMatrix, 1, GL_FALSE, modelViewMatrix);
    if( locs.u_projectionMatrix >= 0 )
        glUniformMatrix4fv(locs.u_projectionMatrix, 1, GL_FALSE, projectionMatrix);
    if( locs.u_clock >= 0 )
        glUniform1f(locs.u_clock, u_clock);

    if( webgl_renderer && webgl_renderer->tiles_cpu.size() >= 256u && webgl_renderer->tiles_dirty )
    {
        webgl1_upload_tile_uniforms(program, locs, webgl_renderer->tiles_cpu);
        webgl_renderer->tiles_dirty = false;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fragment_atlas_texture);
    if( locs.s_atlas >= 0 )
        glUniform1i(locs.s_atlas, 0);

    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_FALSE);

    GLuint last_mesh_vbo = 0u;

    for( const Pass3DCommandGPUBatch& batch : builder.GetGpuBatches() )
    {
        if( batch.draws.empty() )
            continue;

        const WebGLResolvedMesh head = webgl1_resolve_static_mesh_for_cmd(cache, batch.draws[0]);
        if( !head.drawable )
            continue;

        bind_mesh_attribs_stride48(head.vbo, locs);
        last_mesh_vbo = head.vbo;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, head.ebo);

        for( const DrawModel3D& cmd : batch.draws )
        {
            const WebGLResolvedMesh r = webgl1_resolve_static_mesh_for_cmd(cache, cmd);
            if( !r.drawable )
                continue;

            if( cmd.instance_offset >= inst_pool.size() )
                continue;

            if( webgl_renderer )
                webgl_renderer->diag_frame_submitted_model_draws++;

            if( r.vbo != last_mesh_vbo )
            {
                bind_mesh_attribs_stride48(r.vbo, locs);
                last_mesh_vbo = r.vbo;
            }
            if( r.ebo != head.ebo )
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r.ebo);

            webgl1_set_instance_uniforms(locs, inst_pool[cmd.instance_offset]);

            glDrawElements(
                GL_TRIANGLES,
                (GLsizei)r.model.element_count,
                GL_UNSIGNED_SHORT,
                (void*)((size_t)r.model.ebo_offset * sizeof(uint16_t)));
        }
    }

    disable_mesh_attribs(locs);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

#endif
