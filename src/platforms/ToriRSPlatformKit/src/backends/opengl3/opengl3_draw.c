#include "../../../include/ToriRSPlatformKit/trspk_math.h"
#include "../webgl1/webgl1_vertex.h"
#include "opengl3_internal.h"
#include "trspk_opengl3.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void
trspk_opengl3_draw_begin_3d(
    TRSPK_OpenGL3Renderer* r,
    const TRSPK_Rect* viewport)
{
    if( !r )
        return;
    trspk_opengl3_dynamic_reset_pass(r);
    if( viewport )
    {
        r->pass_logical_rect = *viewport;
        if( r->pass_logical_rect.width <= 0 || r->pass_logical_rect.height <= 0 )
        {
            int32_t w = 1;
            int32_t h = 1;
            if( r->window_width > 0u && r->window_height > 0u )
            {
                w = (int32_t)r->window_width;
                h = (int32_t)r->window_height;
            }
            else if( r->width > 0u && r->height > 0u )
            {
                w = (int32_t)r->width;
                h = (int32_t)r->height;
            }
            r->pass_logical_rect.x = 0;
            r->pass_logical_rect.y = 0;
            r->pass_logical_rect.width = w;
            r->pass_logical_rect.height = h;
            if( r->pass_logical_rect.width <= 0 )
                r->pass_logical_rect.width = 1;
            if( r->pass_logical_rect.height <= 0 )
                r->pass_logical_rect.height = 1;
        }
    }
    else
    {
        r->pass_logical_rect.x = 0;
        r->pass_logical_rect.y = 0;
        r->pass_logical_rect.width = (int32_t)r->width;
        r->pass_logical_rect.height = (int32_t)r->height;
    }
    r->pass_state.index_count = 0u;
    r->pass_state.subdraw_count = 0u;
}

static bool
trspk_opengl3_reserve_subdraws(
    TRSPK_OpenGL3PassState* pass,
    uint32_t count)
{
    if( count <= pass->subdraw_capacity )
        return true;
    uint32_t cap = pass->subdraw_capacity ? pass->subdraw_capacity : 64u;
    while( cap < count )
        cap *= 2u;
    TRSPK_OpenGL3SubDraw* n =
        (TRSPK_OpenGL3SubDraw*)realloc(pass->subdraws, sizeof(TRSPK_OpenGL3SubDraw) * cap);
    if( !n )
        return false;
    pass->subdraws = n;
    pass->subdraw_capacity = cap;
    return true;
}

static TRSPK_GPUHandle
trspk_opengl3_resolve_pose_vbo(
    TRSPK_OpenGL3Renderer* r,
    const TRSPK_ModelPose* pose)
{
    if( !r || !pose )
        return 0u;
    if( !pose->dynamic )
        return pose->vbo;

    if( pose->usage_class == (uint8_t)TRSPK_USAGE_PROJECTILE )
        return (TRSPK_GPUHandle)(uintptr_t)r->dynamic_projectile_vbo;
    return (TRSPK_GPUHandle)(uintptr_t)r->dynamic_npc_vbo;
}

void
trspk_opengl3_draw_add_model(
    TRSPK_OpenGL3Renderer* r,
    const TRSPK_ModelPose* pose,
    const uint32_t* sorted_indices,
    uint32_t index_count)
{
    if( !r || !pose || !pose->valid || !sorted_indices || index_count == 0u )
        return;
    TRSPK_OpenGL3PassState* pass = &r->pass_state;
    TRSPK_GPUHandle vbo = trspk_opengl3_resolve_pose_vbo(r, pose);
    if( vbo == 0u )
        return;
    if( !pass->index_pool )
        return;
    if( pass->index_count + index_count > pass->index_capacity )
        return;
    if( !trspk_opengl3_reserve_subdraws(pass, pass->subdraw_count + 1u) )
        return;
    TRSPK_GPUHandle vbo_store;
    TRSPK_GPUHandle vao_store = 0u;
    if( pose->dynamic )
    {
        vbo_store = pose->usage_class == (uint8_t)TRSPK_USAGE_PROJECTILE
                        ? TRSPK_OPENGL3_SUBDRAW_VBO_DYNAMIC_PROJECTILE
                        : TRSPK_OPENGL3_SUBDRAW_VBO_DYNAMIC_NPC;
    }
    else
    {
        vbo_store = pose->vbo;
        vao_store = pose->world_vao;
    }
    const uint32_t start = pass->index_count;
    for( uint32_t i = 0; i < index_count; ++i )
        pass->index_pool[pass->index_count++] = pose->vbo_offset + sorted_indices[i];
    pass->subdraws[pass->subdraw_count].vbo = vbo_store;
    pass->subdraws[pass->subdraw_count].vao = vao_store;
    pass->subdraws[pass->subdraw_count].pool_start = start;
    pass->subdraws[pass->subdraw_count].index_count = index_count;
    pass->subdraw_count++;
}

void
trspk_opengl3_bind_world_attribs(
    const TRSPK_OpenGL3WorldShaderLocs* locs,
    uint32_t mesh_vbo)
{
    if( !locs || mesh_vbo == 0u )
        return;
    const TRSPK_GLsizei stride = (TRSPK_GLsizei)sizeof(TRSPK_VertexWebGL1);
    trspk_glBindBuffer(GL_ARRAY_BUFFER, (TRSPK_GLuint)mesh_vbo);
    if( locs->a_position >= 0 )
    {
        trspk_glEnableVertexAttribArray((TRSPK_GLuint)locs->a_position);
        trspk_glVertexAttribPointer(
            (TRSPK_GLuint)locs->a_position,
            4,
            GL_FLOAT,
            (TRSPK_GLboolean)0,
            stride,
            (const TRSPK_GLvoid*)0);
    }
    if( locs->a_color >= 0 )
    {
        trspk_glEnableVertexAttribArray((TRSPK_GLuint)locs->a_color);
        trspk_glVertexAttribPointer(
            (TRSPK_GLuint)locs->a_color,
            4,
            GL_FLOAT,
            (TRSPK_GLboolean)0,
            stride,
            (const TRSPK_GLvoid*)(uintptr_t)16u);
    }
    if( locs->a_texcoord >= 0 )
    {
        trspk_glEnableVertexAttribArray((TRSPK_GLuint)locs->a_texcoord);
        trspk_glVertexAttribPointer(
            (TRSPK_GLuint)locs->a_texcoord,
            2,
            GL_FLOAT,
            (TRSPK_GLboolean)0,
            stride,
            (const TRSPK_GLvoid*)(uintptr_t)32u);
    }
    if( locs->a_tex_id >= 0 )
    {
        trspk_glEnableVertexAttribArray((TRSPK_GLuint)locs->a_tex_id);
        trspk_glVertexAttribPointer(
            (TRSPK_GLuint)locs->a_tex_id,
            1,
            GL_FLOAT,
            (TRSPK_GLboolean)0,
            stride,
            (const TRSPK_GLvoid*)(uintptr_t)40u);
    }
    if( locs->a_uv_mode >= 0 )
    {
        trspk_glEnableVertexAttribArray((TRSPK_GLuint)locs->a_uv_mode);
        trspk_glVertexAttribPointer(
            (TRSPK_GLuint)locs->a_uv_mode,
            1,
            GL_FLOAT,
            (TRSPK_GLboolean)0,
            stride,
            (const TRSPK_GLvoid*)(uintptr_t)44u);
    }
}

void
trspk_opengl3_world_vao_setup(
    uint32_t vao,
    const TRSPK_OpenGL3WorldShaderLocs* locs,
    uint32_t mesh_vbo,
    uint32_t ibo)
{
    if( !locs || vao == 0u || mesh_vbo == 0u || ibo == 0u )
        return;
    trspk_glBindVertexArray((TRSPK_GLuint)vao);
    trspk_opengl3_bind_world_attribs(locs, mesh_vbo);
    /* GL_ELEMENT_ARRAY_BUFFER is VAO state; must bind ibo while this VAO is active. */
    trspk_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (TRSPK_GLuint)ibo);
}

void
trspk_opengl3_refresh_dynamic_world_vao(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_UsageClass usage)
{
    if( !r || r->dynamic_ibo == 0u )
        return;
    if( usage == TRSPK_USAGE_PROJECTILE )
    {
        if( r->vao_dynamic_projectile != 0u && r->dynamic_projectile_vbo != 0u )
            trspk_opengl3_world_vao_setup(
                r->vao_dynamic_projectile,
                &r->world_locs,
                r->dynamic_projectile_vbo,
                r->dynamic_ibo);
    }
    else
    {
        if( r->vao_dynamic_npc != 0u && r->dynamic_npc_vbo != 0u )
            trspk_opengl3_world_vao_setup(
                r->vao_dynamic_npc, &r->world_locs, r->dynamic_npc_vbo, r->dynamic_ibo);
    }
}

void
trspk_opengl3_dynamic_world_vao_if_needed(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_UsageClass usage)
{
    if( !r || r->dynamic_ibo == 0u )
        return;
    const uint32_t stream_rev = usage == TRSPK_USAGE_PROJECTILE ? r->dynamic_projectile_stream_revision
                                                                : r->dynamic_npc_stream_revision;
    uint32_t* applied_rev = usage == TRSPK_USAGE_PROJECTILE ? &r->dynamic_vao_applied_revision_projectile
                                                          : &r->dynamic_vao_applied_revision_npc;
    if( *applied_rev == stream_rev )
        return;
    const uint32_t mesh_vbo =
        usage == TRSPK_USAGE_PROJECTILE ? r->dynamic_projectile_vbo : r->dynamic_npc_vbo;
    if( mesh_vbo == 0u )
        return;
    trspk_opengl3_refresh_dynamic_world_vao(r, usage);
    *applied_rev = stream_rev;
}

void
trspk_opengl3_draw_submit_3d(
    TRSPK_OpenGL3Renderer* r,
    const TRSPK_Mat4* view,
    const TRSPK_Mat4* proj)
{
    if( !r || !view || !proj )
        return;
    trspk_opengl3_pass_flush_pending_dynamic_gpu_uploads(r);
    if( r->pass_state.index_count == 0u || r->pass_state.subdraw_count == 0u )
        return;
    TRSPK_OpenGL3PassState* pass = &r->pass_state;
    if( !pass->index_pool || pass->uniform_pass_subslot >= TRSPK_OPENGL3_MAX_3D_PASSES_PER_FRAME )
        return;

    const TRSPK_GLuint ibo = (TRSPK_GLuint)r->dynamic_ibo;
    const TRSPK_GLuint prog = (TRSPK_GLuint)r->prog_world3d;
    if( ibo == 0u || prog == 0u )
        return;

    const size_t index_bytes = (size_t)pass->index_count * sizeof(uint32_t);
    const size_t index_offset = pass->index_upload_offset;
    if( index_offset + index_bytes >
        (size_t)TRSPK_OPENGL3_DYNAMIC_INDEX_CAPACITY * sizeof(uint32_t) )
        return;

    trspk_glBindVertexArray(0u);
    trspk_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    trspk_glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER,
        (TRSPK_GLintptr)index_offset,
        (TRSPK_GLsizeiptr)index_bytes,
        pass->index_pool);

    TRSPK_Rect glvp;
    trspk_logical_rect_to_gl_viewport_pixels(
        &glvp, r->width, r->height, r->window_width, r->window_height, &r->pass_logical_rect);

    TRSPK_GLint sx = (TRSPK_GLint)glvp.x;
    TRSPK_GLint sy = (TRSPK_GLint)glvp.y;
    TRSPK_GLint sw = (TRSPK_GLint)glvp.width;
    TRSPK_GLint sh = (TRSPK_GLint)glvp.height;

    if( sx < 0 )
    {
        sw += sx;
        sx = 0;
    }
    if( sy < 0 )
    {
        sh += sy;
        sy = 0;
    }
    if( sx + sw > (TRSPK_GLint)r->width )
        sw = (TRSPK_GLint)r->width - sx;
    if( sy + sh > (TRSPK_GLint)r->height )
        sh = (TRSPK_GLint)r->height - sy;
    if( sw <= 0 || sh <= 0 )
    {
        sx = 0;
        sy = 0;
        sw = (TRSPK_GLint)(r->width > 0u ? r->width : 1u);
        sh = (TRSPK_GLint)(r->height > 0u ? r->height : 1u);
    }

    trspk_glViewport(sx, sy, sw, sh);
    trspk_glEnable(GL_SCISSOR_TEST);
    trspk_glScissor(sx, sy, sw, sh);

    trspk_glUseProgram(prog);
    if( r->world_locs.u_modelViewMatrix >= 0 )
        trspk_glUniformMatrix4fv(
            (TRSPK_GLint)r->world_locs.u_modelViewMatrix, 1, (TRSPK_GLboolean)0, view->m);
    if( r->world_locs.u_projectionMatrix >= 0 )
        trspk_glUniformMatrix4fv(
            (TRSPK_GLint)r->world_locs.u_projectionMatrix, 1, (TRSPK_GLboolean)0, proj->m);
    if( r->world_locs.u_clock >= 0 )
        trspk_glUniform1f((TRSPK_GLint)r->world_locs.u_clock, (TRSPK_GLfloat)r->frame_clock);
    trspk_glActiveTexture(GL_TEXTURE0);
    trspk_glBindTexture(GL_TEXTURE_2D, (TRSPK_GLuint)r->atlas_texture);

    trspk_glDepthFunc(GL_ALWAYS);
    trspk_glDepthMask((TRSPK_GLboolean)0);
    trspk_glEnable(GL_BLEND);
    trspk_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    TRSPK_GLuint last_vao = 0u;
    uint32_t run_start = 0u;
    while( run_start < pass->subdraw_count )
    {
        const TRSPK_OpenGL3SubDraw* first = &pass->subdraws[run_start];
        if( first->vbo == 0u || first->index_count == 0u )
        {
            run_start++;
            continue;
        }
        uint32_t run_end = run_start + 1u;
        uint32_t run_count = first->index_count;
        while( run_end < pass->subdraw_count && pass->subdraws[run_end].vbo == first->vbo &&
               pass->subdraws[run_end].vao == first->vao &&
               pass->subdraws[run_end].pool_start == first->pool_start + run_count )
        {
            run_count += pass->subdraws[run_end].index_count;
            run_end++;
        }

        TRSPK_GLuint mesh_vao = 0u;
        TRSPK_GLuint mesh_vbo = 0u;
        if( first->vbo == TRSPK_OPENGL3_SUBDRAW_VBO_DYNAMIC_NPC )
        {
            if( r->dynamic_npc_vbo != 0u )
            {
                mesh_vbo = (TRSPK_GLuint)r->dynamic_npc_vbo;
                mesh_vao = (TRSPK_GLuint)r->vao_dynamic_npc;
            }
        }
        else if( first->vbo == TRSPK_OPENGL3_SUBDRAW_VBO_DYNAMIC_PROJECTILE )
        {
            if( r->dynamic_projectile_vbo != 0u )
            {
                mesh_vbo = (TRSPK_GLuint)r->dynamic_projectile_vbo;
                mesh_vao = (TRSPK_GLuint)r->vao_dynamic_projectile;
            }
        }
        else
        {
            mesh_vbo = (TRSPK_GLuint)(uintptr_t)first->vbo;
            mesh_vao = (TRSPK_GLuint)(uintptr_t)first->vao;
        }

        if( mesh_vao != 0u && mesh_vbo != 0u )
        {
            const uintptr_t byte_offset =
                index_offset + (size_t)first->pool_start * sizeof(uint32_t);
            if( mesh_vao != last_vao )
            {
                trspk_glBindVertexArray(mesh_vao);
                last_vao = mesh_vao;
            }
            trspk_opengl3_bind_world_attribs(&r->world_locs, mesh_vbo);
            trspk_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
            trspk_glDrawElements(
                GL_TRIANGLES,
                (TRSPK_GLsizei)run_count,
                GL_UNSIGNED_INT,
                (const TRSPK_GLvoid*)byte_offset);
            r->diag_frame_submitted_draws++;
        }
        run_start = run_end;
    }

    trspk_glDisable(GL_SCISSOR_TEST);

    pass->uniform_pass_subslot++;
    pass->index_upload_offset += index_bytes;
    pass->index_count = 0u;
    pass->subdraw_count = 0u;
}

void
trspk_opengl3_draw_clear_rect(
    TRSPK_OpenGL3Renderer* r,
    const TRSPK_Rect* rect)
{
    if( !r || !rect )
        return;
    TRSPK_Rect gr;
    trspk_logical_rect_to_gl_viewport_pixels(
        &gr, r->width, r->height, r->window_width, r->window_height, rect);
    TRSPK_GLint sx = (TRSPK_GLint)gr.x;
    TRSPK_GLint sy = (TRSPK_GLint)gr.y;
    TRSPK_GLint sw = (TRSPK_GLint)gr.width;
    TRSPK_GLint sh = (TRSPK_GLint)gr.height;
    if( sx < 0 )
    {
        sw += sx;
        sx = 0;
    }
    if( sy < 0 )
    {
        sh += sy;
        sy = 0;
    }
    if( sx + sw > (TRSPK_GLint)r->width )
        sw = (TRSPK_GLint)r->width - sx;
    if( sy + sh > (TRSPK_GLint)r->height )
        sh = (TRSPK_GLint)r->height - sy;
    if( sw <= 0 || sh <= 0 )
        return;
    trspk_glEnable(GL_SCISSOR_TEST);
    trspk_glScissor(sx, sy, sw, sh);
    trspk_glDepthMask((TRSPK_GLboolean)1);
    trspk_glClearDepthf(1.0f);
    trspk_glClear(GL_DEPTH_BUFFER_BIT);
}

float
trspk_opengl3_dash_yaw_to_radians(int dash_yaw)
{
    return ((float)dash_yaw * 2.0f * (float)M_PI) / 2048.0f;
}

void
trspk_opengl3_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw)
{
    float cosPitch = cosf(-pitch);
    float sinPitch = sinf(-pitch);
    float cosYaw = cosf(-yaw);
    float sinYaw = sinf(-yaw);

    out_matrix[0] = cosYaw;
    out_matrix[1] = sinYaw * sinPitch;
    out_matrix[2] = sinYaw * cosPitch;
    out_matrix[3] = 0.0f;
    out_matrix[4] = 0.0f;
    out_matrix[5] = cosPitch;
    out_matrix[6] = -sinPitch;
    out_matrix[7] = 0.0f;
    out_matrix[8] = -sinYaw;
    out_matrix[9] = cosYaw * sinPitch;
    out_matrix[10] = cosYaw * cosPitch;
    out_matrix[11] = 0.0f;
    out_matrix[12] = -camera_x * cosYaw + camera_z * sinYaw;
    out_matrix[13] =
        -camera_x * sinYaw * sinPitch - camera_y * cosPitch - camera_z * cosYaw * sinPitch;
    out_matrix[14] =
        -camera_x * sinYaw * cosPitch + camera_y * sinPitch - camera_z * cosYaw * cosPitch;
    out_matrix[15] = 1.0f;
}

void
trspk_opengl3_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height)
{
    float y = 1.0f / tanf(fov * 0.5f);
    float x = y;
    out_matrix[0] = x * 512.0f / (screen_width / 2.0f);
    out_matrix[1] = 0.0f;
    out_matrix[2] = 0.0f;
    out_matrix[3] = 0.0f;
    out_matrix[4] = 0.0f;
    out_matrix[5] = -y * 512.0f / (screen_height / 2.0f);
    out_matrix[6] = 0.0f;
    out_matrix[7] = 0.0f;
    out_matrix[8] = 0.0f;
    out_matrix[9] = 0.0f;
    out_matrix[10] = 0.0f;
    out_matrix[11] = 1.0f;
    out_matrix[12] = 0.0f;
    out_matrix[13] = 0.0f;
    out_matrix[14] = -1.0f;
    out_matrix[15] = 0.0f;
}
