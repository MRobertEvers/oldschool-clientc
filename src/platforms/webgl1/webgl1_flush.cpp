#include "platforms/webgl1/webgl1_internal.h"

#include "platforms/common/torirs_gpu_clipspace.h"

#include <cmath>
#include <cstring>
#include <vector>

#include <emscripten.h>

static int s_wg1_gl_pipe = 0;
enum
{
    kPipeNone = 0,
    kPipeWorld = 1,
    kPipeUi = 2,
    kPipeFont = 3
};

static void
mat4_mul_colmajor(
    const float* a,
    const float* b,
    float* out)
{
    for( int c = 0; c < 4; ++c )
        for( int r = 0; r < 4; ++r )
        {
            float s = 0.0f;
            for( int k = 0; k < 4; ++k )
                s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

static void
mat4_vec4(
    const float* m,
    float x,
    float y,
    float z,
    float w,
    float* o)
{
    for( int r = 0; r < 4; ++r )
        o[r] = m[0 * 4 + r] * x + m[1 * 4 + r] * y + m[2 * 4 + r] * z + m[3 * 4 + r] * w;
}

void
wg1_ensure_world_pipe(WebGL1RenderCtx* ctx)
{
    if( s_wg1_gl_pipe == kPipeWorld || !ctx || !ctx->renderer )
        return;
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(r->program_world);
    s_wg1_gl_pipe = kPipeWorld;
}

void
wg1_ensure_ui_sprite_pipe(WebGL1RenderCtx* ctx)
{
    if( s_wg1_gl_pipe == kPipeUi || !ctx || !ctx->renderer )
        return;
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_SCISSOR_TEST);
    if( r->program_ui_sprite )
        glUseProgram(r->program_ui_sprite);
    s_wg1_gl_pipe = kPipeUi;
}

void
wg1_ensure_font_pipe(WebGL1RenderCtx* ctx)
{
    if( s_wg1_gl_pipe == kPipeFont || !ctx || !ctx->renderer )
        return;
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if( r->font_program )
        glUseProgram(r->font_program);
    s_wg1_gl_pipe = kPipeFont;
}

void
wg1_flush_3d(WebGL1RenderCtx* ctx, BufferedFaceOrder* bfo)
{
    if( !ctx || !bfo || !ctx->renderer || !ctx->game )
        return;
    bfo->finalize_slices();
    if( bfo->stream().empty() )
        return;

    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    wg1_ensure_world_pipe(ctx);

    const float* mv = ctx->world_uniforms;
    const float* pr = ctx->world_uniforms + 16;
    float mvp[16];
    mat4_mul_colmajor(pr, mv, mvp);

    const float clock_pad[4] = {
        (float)((uint64_t)emscripten_get_now() / 20),
        0.0f,
        0.0f,
        0.0f,
    };

    if( r->loc_world_uClockPad >= 0 )
        glUniform4fv(r->loc_world_uClockPad, 1, clock_pad);
    if( r->loc_world_uAtlas >= 0 )
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, r->world_atlas_tex);
        glUniform1i(r->loc_world_uAtlas, 0);
    }
    if( r->loc_world_uTileMeta >= 0 )
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, r->world_tile_meta_tex);
        glUniform1i(r->loc_world_uTileMeta, 1);
    }

    static std::vector<float> exp;
    const GLsizei stride = (GLsizei)(12 * sizeof(float));
    const GLint posLoc = 0;
    const GLint colLoc = 1;
    const GLint texLoc = 2;
    const GLint tidLoc = 3;
    const GLint uvmLoc = 4;

    if( !r->exp_stream_vbo )
        glGenBuffers(1, &r->exp_stream_vbo);

    for( const PassFlushSlice& slice : bfo->slices() )
    {
        if( slice.entry_count == 0 )
            continue;
        const GLuint geomVbo = (GLuint)(uintptr_t)slice.vbo_handle;
        if( !geomVbo )
            continue;
        auto it = r->geometry_mirror.find(geomVbo);
        if( it == r->geometry_mirror.end() || it->second.empty() )
            continue;
        const std::vector<WgVertex>& geom = it->second;
        const std::vector<DrawStreamEntry>& st = bfo->stream();
        const std::vector<GPU3DTransformUniform>& inst = bfo->instances();

        exp.clear();
        exp.reserve((size_t)slice.entry_count * 12u);
        for( uint32_t i = 0; i < slice.entry_count; ++i )
        {
            const DrawStreamEntry& e = st[slice.entry_offset + i];
            if( e.instance_id >= inst.size() || e.vertex_index >= geom.size() )
                continue;
            const WgVertex& v = geom[e.vertex_index];
            const GPU3DTransformUniform& xf = inst[e.instance_id];
            const float px = v.position[0];
            const float py = v.position[1];
            const float pz = v.position[2];
            const float xr = px * xf.cos_yaw + pz * xf.sin_yaw;
            const float zr = -px * xf.sin_yaw + pz * xf.cos_yaw;
            const float wx = xr + xf.x;
            const float wy = py + xf.y;
            const float wz = zr + xf.z;
            float clip[4];
            mat4_vec4(mvp, wx, wy, wz, 1.0f, clip);
            exp.push_back(clip[0]);
            exp.push_back(clip[1]);
            exp.push_back(clip[2]);
            exp.push_back(clip[3]);
            exp.push_back(v.color[0]);
            exp.push_back(v.color[1]);
            exp.push_back(v.color[2]);
            exp.push_back(v.color[3]);
            exp.push_back(v.texcoord[0]);
            exp.push_back(v.texcoord[1]);
            exp.push_back((float)v.tex_id);
            exp.push_back((float)v.uv_mode);
        }
        if( exp.empty() )
            continue;

        glBindBuffer(GL_ARRAY_BUFFER, r->exp_stream_vbo);
        glBufferData(
            GL_ARRAY_BUFFER,
            (GLsizeiptr)(exp.size() * sizeof(float)),
            exp.data(),
            GL_STREAM_DRAW);

        glVertexAttribPointer(posLoc, 4, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(colLoc, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));
        glEnableVertexAttribArray(colLoc);
        glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
        glEnableVertexAttribArray(texLoc);
        glVertexAttribPointer(tidLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(10 * sizeof(float)));
        glEnableVertexAttribArray(tidLoc);
        glVertexAttribPointer(uvmLoc, 1, GL_FLOAT, GL_FALSE, stride, (void*)(11 * sizeof(float)));
        glEnableVertexAttribArray(uvmLoc);

        const GLsizei nvert = (GLsizei)(exp.size() / 12u);
        glDrawArrays(GL_TRIANGLES, 0, nvert);
        r->debug_model_draws++;
        r->debug_triangles += (unsigned int)(nvert / 3);

        glDisableVertexAttribArray(posLoc);
        glDisableVertexAttribArray(colLoc);
        glDisableVertexAttribArray(texLoc);
        glDisableVertexAttribArray(tidLoc);
        glDisableVertexAttribArray(uvmLoc);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    bfo->begin_pass();
    s_wg1_gl_pipe = kPipeNone;
}

static GLuint s_sprite_stream_vbo = 0;

static void
wg1_draw_one_sprite_group_gl(
    WebGL1RenderCtx* ctx,
    const SpriteDrawGroup& g,
    const std::vector<SpriteQuadVertex>& verts)
{
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    if( !r || verts.empty() || g.vertex_count == 0 )
        return;
    const GLuint tex = (GLuint)(uintptr_t)g.atlas_tex;
    if( !tex )
        return;

    const float fbw = (float)(ctx->win_width > 0 ? ctx->win_width : r->width);
    const float fbh = (float)(ctx->win_height > 0 ? ctx->win_height : r->height);
    float ortho[16] = { 0 };
    if( fbw > 0.0f && fbh > 0.0f )
    {
        ortho[0] = 2.0f / fbw;
        ortho[5] = -2.0f / fbh;
        ortho[10] = -1.0f;
        ortho[12] = -1.0f;
        ortho[13] = 1.0f;
        ortho[15] = 1.0f;
    }

    wg1_ensure_ui_sprite_pipe(ctx);
    if( g.inverse_rot_blit )
    {
        if( !r->program_ui_sprite_invrot )
            return;
        glUseProgram(r->program_ui_sprite_invrot);
        if( r->loc_inv_uProj >= 0 )
            glUniformMatrix4fv(r->loc_inv_uProj, 1, GL_FALSE, ortho);
        if( r->loc_inv_uTex >= 0 )
            glUniform1i(r->loc_inv_uTex, 0);
        if( r->loc_inv_uParams >= 0 )
            glUniform4fv(
                r->loc_inv_uParams,
                4,
                reinterpret_cast<const float*>(&g.inverse_rot_params));
    }
    else
    {
        if( !r->program_ui_sprite )
            return;
        glUseProgram(r->program_ui_sprite);
        if( r->loc_ui_uProj >= 0 )
            glUniformMatrix4fv(r->loc_ui_uProj, 1, GL_FALSE, ortho);
        if( r->loc_ui_uTex >= 0 )
            glUniform1i(r->loc_ui_uTex, 0);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    if( g.has_scissor )
    {
        glEnable(GL_SCISSOR_TEST);
        glScissor(
            (GLint)g.scissor.x,
            (GLint)g.scissor.y,
            (GLsizei)g.scissor.width,
            (GLsizei)g.scissor.height);
    }
    else
        glDisable(GL_SCISSOR_TEST);

    if( !s_sprite_stream_vbo )
        glGenBuffers(1, &s_sprite_stream_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_sprite_stream_vbo);
    const SpriteQuadVertex* base = verts.data() + g.first_vertex;
    glBufferData(
        GL_ARRAY_BUFFER,
        (GLsizeiptr)(g.vertex_count * sizeof(SpriteQuadVertex)),
        base,
        GL_STREAM_DRAW);

    const GLsizei sprStride = (GLsizei)sizeof(SpriteQuadVertex);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sprStride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sprStride, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g.vertex_count);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    s_wg1_gl_pipe = kPipeNone;
}

static void
wg1_draw_one_font_group_gl(
    WebGL1RenderCtx* ctx,
    const FontDrawGroup& g,
    const std::vector<float>& fv)
{
    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    if( !r || !r->font_program || fv.empty() || g.float_count == 0 )
        return;
    const GLuint tex = (GLuint)(uintptr_t)g.atlas_tex;
    if( !tex )
        return;

    wg1_ensure_font_pipe(ctx);

    const float fbw = (float)(ctx->win_width > 0 ? ctx->win_width : r->width);
    const float fbh = (float)(ctx->win_height > 0 ? ctx->win_height : r->height);
    float ortho[16] = { 0 };
    if( fbw > 0.0f && fbh > 0.0f )
    {
        ortho[0] = 2.0f / fbw;
        ortho[5] = -2.0f / fbh;
        ortho[10] = -1.0f;
        ortho[12] = -1.0f;
        ortho[13] = 1.0f;
        ortho[15] = 1.0f;
    }
    if( r->font_uniform_projection >= 0 )
        glUniformMatrix4fv(r->font_uniform_projection, 1, GL_FALSE, ortho);
    if( r->font_uniform_tex >= 0 )
        glUniform1i(r->font_uniform_tex, 0);

    if( !r->font_vbo )
        glGenBuffers(1, &r->font_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->font_vbo);
    const float* sub = fv.data() + g.first_float;
    glBufferData(
        GL_ARRAY_BUFFER,
        (GLsizeiptr)(g.float_count * sizeof(float)),
        sub,
        GL_STREAM_DRAW);

    const GLsizei fstride = (GLsizei)(8 * sizeof(float));
    const GLsizei vcount = (GLsizei)(g.float_count / 8u);
    if( r->font_attrib_pos >= 0 )
    {
        glEnableVertexAttribArray(r->font_attrib_pos);
        glVertexAttribPointer(r->font_attrib_pos, 2, GL_FLOAT, GL_FALSE, fstride, (void*)0);
    }
    if( r->font_attrib_uv >= 0 )
    {
        glEnableVertexAttribArray(r->font_attrib_uv);
        glVertexAttribPointer(
            r->font_attrib_uv, 2, GL_FLOAT, GL_FALSE, fstride, (void*)(2 * sizeof(float)));
    }
    if( r->font_attrib_color >= 0 )
    {
        glEnableVertexAttribArray(r->font_attrib_color);
        glVertexAttribPointer(
            r->font_attrib_color, 4, GL_FLOAT, GL_FALSE, fstride, (void*)(4 * sizeof(float)));
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glDrawArrays(GL_TRIANGLES, 0, vcount);

    if( r->font_attrib_pos >= 0 )
        glDisableVertexAttribArray(r->font_attrib_pos);
    if( r->font_attrib_uv >= 0 )
        glDisableVertexAttribArray(r->font_attrib_uv);
    if( r->font_attrib_color >= 0 )
        glDisableVertexAttribArray(r->font_attrib_color);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    s_wg1_gl_pipe = kPipeNone;
}

void
wg1_flush_2d(WebGL1RenderCtx* ctx)
{
    if( !ctx || !ctx->renderer )
        return;
    if( !ctx->bsp2d || !ctx->bft2d || !ctx->b2d_order )
        return;

    ctx->bft2d->close_open_segment();
    const std::vector<SpriteDrawGroup>& sgroups = ctx->bsp2d->groups();
    const std::vector<SpriteQuadVertex>& sverts = ctx->bsp2d->verts();
    const std::vector<FontDrawGroup>& fgroups = ctx->bft2d->groups();
    const std::vector<float>& fall = ctx->bft2d->verts();

    for( const Tori2DFlushOp& op : ctx->b2d_order->ops() )
    {
        if( op.kind == Tori2DFlushKind::Sprite )
        {
            if( op.group_index < sgroups.size() && !sverts.empty() )
                wg1_draw_one_sprite_group_gl(ctx, sgroups[op.group_index], sverts);
        }
        else
        {
            if( op.group_index < fgroups.size() && !fall.empty() )
                wg1_draw_one_font_group_gl(ctx, fgroups[op.group_index], fall);
        }
    }

    if( ctx->bsp2d )
        ctx->bsp2d->begin_pass();
    if( ctx->bft2d )
        ctx->bft2d->begin_pass();
    if( ctx->b2d_order )
        ctx->b2d_order->begin_pass();
    ctx->split_sprite_before_next_enqueue = false;
    ctx->split_font_before_next_set_font = false;
}

void
wg1_restore_gl_state_after_ui(struct Platform2_SDL2_Renderer_WebGL1* r)
{
    (void)r;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    s_wg1_gl_pipe = kPipeNone;
}
