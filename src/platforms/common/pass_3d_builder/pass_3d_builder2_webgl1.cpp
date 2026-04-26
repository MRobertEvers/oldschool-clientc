#include "pass_3d_builder2_webgl1.h"

#ifdef __EMSCRIPTEN__

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
bind_instance_attribs(
    GLuint inst_buf,
    size_t instance_byte_offset,
    const WebGL1WorldShaderLocs& L)
{
    glBindBuffer(GL_ARRAY_BUFFER, inst_buf);
    const void* base = (const void*)instance_byte_offset;
    if( L.a_inst0 >= 0 )
    {
        glEnableVertexAttribArray((GLuint)L.a_inst0);
        glVertexAttribPointer((GLuint)L.a_inst0, 4, GL_FLOAT, GL_FALSE, 32, base);
    }
    if( L.a_inst1 >= 0 )
    {
        glEnableVertexAttribArray((GLuint)L.a_inst1);
        glVertexAttribPointer(
            (GLuint)L.a_inst1,
            4,
            GL_FLOAT,
            GL_FALSE,
            32,
            (const void*)((const uint8_t*)base + 16));
    }
}

static void
disable_world_attribs(const WebGL1WorldShaderLocs& L)
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
    if( L.a_inst0 >= 0 )
        glDisableVertexAttribArray((GLuint)L.a_inst0);
    if( L.a_inst1 >= 0 )
        glDisableVertexAttribArray((GLuint)L.a_inst1);
}

void
Pass3DBuilder2SubmitWebGL1(
    Pass3DBuilder2<GPU3DTransformUniformMetal>& builder,
    const GPU3DCache2<GPU3DMeshVertexWebGL1>& cache,
    struct Platform2_SDL2_Renderer_WebGL1* webgl_renderer,
    GLuint program,
    const WebGL1WorldShaderLocs& locs,
    GLuint dynamic_instance_buffer,
    GLuint dynamic_index_buffer,
    size_t instance_base_bytes,
    size_t index_base_bytes,
    GLuint fragment_atlas_texture,
    const float modelViewMatrix[16],
    const float projectionMatrix[16],
    float u_clock)
{
    (void)cache;
    if( !builder.HasCommands() || program == 0u )
        return;

    const size_t inst_bytes = builder.GetInstancePool().size() * sizeof(GPU3DTransformUniformMetal);
    const size_t inst_cap = (size_t)16384u * sizeof(GPU3DTransformUniformMetal);
    if( instance_base_bytes + inst_bytes > inst_cap )
    {
        fprintf(
            stderr,
            "Pass3DBuilder2SubmitWebGL1: instance upload exceeds buffer (%zu + %zu > %zu)\n",
            instance_base_bytes,
            inst_bytes,
            inst_cap);
        return;
    }

    if( inst_bytes > 0u && dynamic_instance_buffer )
    {
        glBindBuffer(GL_ARRAY_BUFFER, dynamic_instance_buffer);
        glBufferSubData(
            GL_ARRAY_BUFFER,
            (GLintptr)instance_base_bytes,
            (GLsizeiptr)inst_bytes,
            builder.GetInstancePool().data());
    }

    const size_t dyn_index_count = builder.GetDynamicIndices().size();
    const size_t dyn_capacity = (size_t)kPass3DBuilder2DynamicIndexUInt16Capacity;
    size_t copy_n = 0;
    if( builder.HasDynamicIndices() )
    {
        if( dyn_index_count > dyn_capacity )
        {
            fprintf(
                stderr,
                "Pass3DBuilder2SubmitWebGL1: dynamic index pool %zu exceeds capacity %zu\n",
                dyn_index_count,
                dyn_capacity);
        }
        copy_n = dyn_index_count < dyn_capacity ? dyn_index_count : dyn_capacity;
        const size_t idx_bytes = copy_n * sizeof(uint16_t);
        const size_t idx_cap =
            (size_t)kPass3DBuilder2DynamicIndexUInt16Capacity * sizeof(uint16_t);
        if( index_base_bytes + idx_bytes > idx_cap )
        {
            fprintf(stderr, "Pass3DBuilder2SubmitWebGL1: index upload exceeds buffer\n");
            return;
        }
        if( idx_bytes > 0u && dynamic_index_buffer )
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dynamic_index_buffer);
            glBufferSubData(
                GL_ELEMENT_ARRAY_BUFFER,
                (GLintptr)index_base_bytes,
                (GLsizeiptr)idx_bytes,
                builder.GetDynamicIndices().data());
        }
    }

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

    for( const auto& cmd : builder.GetCommands() )
    {
        const GPUModelPosedData model = cache.GetModelPoseForDraw(
            cmd.model_id, cmd.use_animation, (int)cmd.animation_index, (int)cmd.frame_index);
        if( !model.valid )
            continue;

        GLuint vbo = 0u;
        GLuint ebo = 0u;
        if( model.gpu_batch_id != 0u && webgl_renderer )
        {
            const auto it = webgl_renderer->model_cache2_batch_map.find(model.gpu_batch_id);
            if( it != webgl_renderer->model_cache2_batch_map.end() && it->second.vbo && it->second.ebo )
            {
                vbo = it->second.vbo;
                ebo = it->second.ebo;
            }
        }
        if( vbo == 0u )
            vbo = (GLuint)(uintptr_t)model.vbo;
        if( ebo == 0u )
            ebo = (GLuint)(uintptr_t)model.ebo;
        if( vbo == 0u || (cmd.dynamic_index_count == 0 && ebo == 0u) )
            continue;

        if( vbo != last_mesh_vbo )
        {
            bind_mesh_attribs_stride48(vbo, locs);
            last_mesh_vbo = vbo;
        }

        const size_t rotation_offset =
            instance_base_bytes + (size_t)cmd.instance_offset * sizeof(GPU3DTransformUniformMetal);
        bind_instance_attribs(dynamic_instance_buffer, rotation_offset, locs);

        if( cmd.dynamic_index_count > 0 )
        {
            const uint64_t end =
                (uint64_t)cmd.dynamic_index_offset + (uint64_t)cmd.dynamic_index_count;
            if( end > dyn_capacity )
                continue;
            static std::vector<uint16_t> scratch;
            scratch.resize((size_t)cmd.dynamic_index_count);
            const uint16_t* src =
                builder.GetDynamicIndices().data() + (size_t)cmd.dynamic_index_offset;
            const uint32_t base = model.vbo_offset;
            for( uint32_t i = 0; i < cmd.dynamic_index_count; ++i )
                scratch[i] = (uint16_t)((uint32_t)src[i] + base);

            if( webgl_renderer && webgl_renderer->scratch_idx_buf )
            {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, webgl_renderer->scratch_idx_buf);
                glBufferSubData(
                    GL_ELEMENT_ARRAY_BUFFER,
                    0,
                    (GLsizeiptr)(scratch.size() * sizeof(uint16_t)),
                    scratch.data());
                glDrawElements(
                    GL_TRIANGLES,
                    (GLsizei)cmd.dynamic_index_count,
                    GL_UNSIGNED_SHORT,
                    (void*)0);
            }
        }
        else
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glDrawElements(
                GL_TRIANGLES,
                (GLsizei)model.element_count,
                GL_UNSIGNED_SHORT,
                (void*)((size_t)model.ebo_offset * sizeof(uint16_t)));
        }
    }

    disable_world_attribs(locs);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

#endif
