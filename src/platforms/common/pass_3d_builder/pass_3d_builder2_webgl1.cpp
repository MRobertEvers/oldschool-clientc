#include "pass_3d_builder2_webgl1.h"

#ifdef __EMSCRIPTEN__

#    include "platforms/platform_impl2_sdl2_renderer_webgl1.h"

#    include <GLES2/gl2.h>

#    include <cassert>
#    include <cstddef>
#    include <cstdio>
#    include <cstring>
#    include <vector>

void
Pass3DBuilder2WebGL1::Begin()
{
    is_building_ = true;
    indices_pool_.clear();
    slices_.clear();
}

void
Pass3DBuilder2WebGL1::ClearAfterSubmit()
{
    indices_pool_.clear();
    slices_.clear();
}

void
Pass3DBuilder2WebGL1::AppendSortedDraw(
    GPUResourceHandle vbo,
    uint32_t pose_vbo_vertex_offset,
    const uint16_t* sorted_indices,
    uint32_t index_count)
{
    if( !is_building_ || sorted_indices == nullptr || index_count == 0u )
        return;
    if( vbo == 0u )
        return;

    const uint32_t voff = pose_vbo_vertex_offset;
    for( uint32_t ii = 0; ii < index_count; ++ii )
    {
        const uint32_t abs_idx = voff + (uint32_t)sorted_indices[ii];
        if( abs_idx > 0xFFFFu )
        {
            static bool s_warned_oob;
            if( !s_warned_oob )
            {
                fprintf(
                    stderr,
                    "[Pass3DBuilder2WebGL1] biased index > 65535; skipping scenery draws that "
                    "overflow uint16 (use Metal or split batches)\n");
                s_warned_oob = true;
            }
            return;
        }
    }

    const bool same_vbo_as_last =
        !slices_.empty() && slices_.back().vbo == vbo && slices_.back().index_count > 0u;
    if( same_vbo_as_last )
    {
        for( uint32_t ii = 0; ii < index_count; ++ii )
            indices_pool_.push_back((uint16_t)(voff + (uint32_t)sorted_indices[ii]));
        slices_.back().index_count += index_count;
        return;
    }

    const uint32_t slice_off = (uint32_t)indices_pool_.size();
    for( uint32_t ii = 0; ii < index_count; ++ii )
        indices_pool_.push_back((uint16_t)(voff + (uint32_t)sorted_indices[ii]));

    Pass3DWebGL1Slice sl{};
    sl.vbo = vbo;
    sl.index_offset = slice_off;
    sl.index_count = index_count;
    slices_.push_back(sl);
}

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
bind_mesh_attribs_stride48_base(GLuint mesh_vbo, const WebGL1WorldShaderLocs& L, intptr_t base_bytes)
{
    glBindBuffer(GL_ARRAY_BUFFER, mesh_vbo);
    if( L.a_position >= 0 )
    {
        glEnableVertexAttribArray((GLuint)L.a_position);
        glVertexAttribPointer(
            (GLuint)L.a_position, 4, GL_FLOAT, GL_FALSE, 48, (void*)(base_bytes + 0));
    }
    if( L.a_color >= 0 )
    {
        glEnableVertexAttribArray((GLuint)L.a_color);
        glVertexAttribPointer(
            (GLuint)L.a_color, 4, GL_FLOAT, GL_FALSE, 48, (void*)(base_bytes + 16));
    }
    if( L.a_texcoord >= 0 )
    {
        glEnableVertexAttribArray((GLuint)L.a_texcoord);
        glVertexAttribPointer(
            (GLuint)L.a_texcoord, 2, GL_FLOAT, GL_FALSE, 48, (void*)(base_bytes + 32));
    }
    if( L.a_tex_id >= 0 )
    {
        glEnableVertexAttribArray((GLuint)L.a_tex_id);
        glVertexAttribPointer(
            (GLuint)L.a_tex_id, 1, GL_FLOAT, GL_FALSE, 48, (void*)(base_bytes + 40));
    }
    if( L.a_uv_mode >= 0 )
    {
        glEnableVertexAttribArray((GLuint)L.a_uv_mode);
        glVertexAttribPointer(
            (GLuint)L.a_uv_mode, 1, GL_FLOAT, GL_FALSE, 48, (void*)(base_bytes + 44));
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

void
Pass3DBuilder2SubmitWebGL1(
    Pass3DBuilder2WebGL1& builder,
    struct WebGL1RendererCore* webgl_renderer,
    GLuint program,
    const WebGL1WorldShaderLocs& locs,
    GLuint fragment_atlas_texture,
    const float modelViewMatrix[16],
    const float projectionMatrix[16],
    float u_clock)
{
    if( !builder.HasCommands() || program == 0u )
        return;

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

    if( webgl_renderer && webgl_renderer->pass3d_dynamic_ibo != 0u && builder.HasDynamicIndices() )
    {
        const std::vector<uint16_t>& dix = builder.Indices();
        const GLsizeiptr idx_bytes = (GLsizeiptr)(dix.size() * sizeof(uint16_t));
        if( idx_bytes > 0 )
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, webgl_renderer->pass3d_dynamic_ibo);
            glBufferData(
                GL_ELEMENT_ARRAY_BUFFER, idx_bytes, dix.data(), GL_STREAM_DRAW);
        }
    }

    GLuint last_mesh_vbo = 0u;

    for( const Pass3DWebGL1Slice& slice : builder.Slices() )
    {
        const GLuint vbo = (GLuint)(uintptr_t)slice.vbo;
        if( vbo == 0u || !webgl_renderer || webgl_renderer->pass3d_dynamic_ibo == 0u )
            continue;
        if( slice.index_count == 0u )
            continue;

        if( vbo != last_mesh_vbo )
        {
            bind_mesh_attribs_stride48_base(vbo, locs, 0);
            last_mesh_vbo = vbo;
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, webgl_renderer->pass3d_dynamic_ibo);
        webgl_renderer->diag_frame_submitted_model_draws++;
        webgl_renderer->diag_frame_dynamic_index_draws++;
        glDrawElements(
            GL_TRIANGLES,
            (GLsizei)slice.index_count,
            GL_UNSIGNED_SHORT,
            (void*)((size_t)slice.index_offset * sizeof(uint16_t)));
    }

    disable_mesh_attribs(locs);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

#endif
