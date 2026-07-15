#include "platform_sdl2_renderer_opengl3.h"

#if defined(TORIRS_ENABLE_LVGL_HUD)
#include "platform_sdl2_lvgl_hud.h"
#endif

#include "../trspk_sprite.h"
#include "../trspk_toridraw.h"
#include "graphics/uv_pnm.h"
#include "libtorirs.h"
#include "platformkit/core/trspk_atlas.h"
#include "platformkit/core/trspk_drawrangeex.h"
#include "platformkit/core/trspk_drawrangelist.h"
#include "platformkit/core/trspk_ibo.h"
#include "platformkit/core/trspk_modelarena.h"
#include "platformkit/core/trspk_pose.h"
#include "platformkit/core/trspk_triangles.h"
#include "platformkit/core/trspk_vbo.h"
#include "platformkit/opengl3/opengl3_2d_shaders.h"
#include "platformkit/opengl3/opengl3_sdlgl.h"
#include "platformkit/opengl3/trspk_opengl3.h"
#include "render/libtorirs_render.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_math.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"
#include "toridraw/toridraw_types.h"

#include <SDL.h>
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TRSPK_UboWorld
{
    float modelViewMatrix[16];
    float projectionMatrix[16];
    float uClock;
    float _pad[3];
} TRSPK_UboWorld;

#define TRSPK_GL3_ATLAS_DIM 2048u
#define TRSPK_GL3_ATLAS_COLS 16u
#define TRSPK_GL3_DRAWRANGE_CAP 4096u
#define TRSPK_GL3_VBO_PAGE (1u << 28)
#define TRSPK_GL3_GPU_IBO_INIT 4096u
#define TRSPK_GL3_GPU_VBO_INIT 4096u
#define TRSPK_GL3_SPRITE_CAP 256
#define TRSPK_GL3_FONT_CAP 8
#define TRSPK_GL3_2D_ATLAS_DIM 2048u
#define GL3_2D_BATCH_MAX_VERTS 32768u

struct GL3SpriteSlot
{
    struct ToriDraw_Sprite** sprites;
    int count;
    float* uvs;
};

struct GL3FontSlot
{
    struct ToriDraw_Font* font;
    GLuint texture;
    int atlas_w;
    int atlas_h;
    float glyph_uv[TORIDRAW_FONT_GLYPH_COUNT * 4];
    bool baked;
};

struct GL3Vertex2D
{
    float position[2];
    float texcoord[2];
    float color[4];
};

struct GL3Batch2DState
{
    struct GL3Vertex2D* verts;
    uint32_t vert_count;
    GLuint texture;
    int text_mode;
    bool uv_clamp;
    float uv_bounds[4];
    int scissor_x;
    int scissor_y;
    int scissor_w;
    int scissor_h;
    bool scissor_set;
};

struct GL3ModelGroup
{
    struct TRSPK_VBO* vbo_cpu;
    GLuint vbo_gpu;
    uint32_t gpu_capacity;
    struct TRSPK_ModelArena* arena;
    struct TRSPK_Triangles triangles;
    GLuint vao;
    bool reset_each_frame;
};

struct LibToriPlatformSDL2_RendererGL3
{
    SDL_Window* window;
    SDL_GLContext gl_context;
    int width;
    int height;
    int lb_x;
    int lb_y;
    int lb_w;
    int lb_h;

    struct TRSPK_Atlas atlas;
    GLuint atlas_texture;

    struct GL3ModelGroup groups[TRSPK_VBO_GROUP_COUNT];
    uint32_t gpu_ibo_capacity;

    struct TRSPK_PoseTable poses;

    struct TRSPK_IBOChain* ibo_chain;
    struct TRSPK_IBO* ibo_staging;
    struct TRSPK_DrawRangeList* draw_ranges;

    GLuint program3d;
    GLint a_position;
    GLint a_color;
    GLint a_texcoord;
    GLint a_tex_id;
    GLint a_uv_mode;
    GLint s_atlas;

    GLuint program2d;

    GLuint ubo;
    GLuint ebo;

    float view[16];
    float proj[16];
    struct LibToriRS_RenderCommand_Begin3D cur_3d;
    bool has_3d;
    bool in3d;
    double frame_clock;

    struct TRSPK_Atlas sprite_atlas;
    GLuint sprite_atlas_texture;
    GLuint white_texture;
    struct GL3SpriteSlot sprite_slots[TRSPK_GL3_SPRITE_CAP];
    struct GL3FontSlot font_slots[TRSPK_GL3_FONT_CAP];
    GLuint quad_vao;
    GLuint quad_vbo;
    GLint u2d_projection;
    GLint u2d_texture;
    GLint u2d_text_mode;
    GLint u2d_uv_clamp;
    GLint u2d_uv_bounds;
    bool in2d;
    float proj2d[16];
    struct GL3Batch2DState batch2d;
    int draw_scissor_x;
    int draw_scissor_y;
    int draw_scissor_w;
    int draw_scissor_h;
    bool sprite_atlas_texture_allocated;
#if defined(TORIRS_ENABLE_LVGL_HUD)
    struct LibToriHud* hud;
    GLuint hud_texture;
#endif
};

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static void
bind_vbo_attribs(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    GLuint vbo_gpu)
{
    const GLsizei stride = (GLsizei)sizeof(struct TRSPK_VertexOpenGl3);
    const uintptr_t offset_position = offsetof(struct TRSPK_VertexOpenGl3, position);
    const uintptr_t offset_color = offsetof(struct TRSPK_VertexOpenGl3, color);
    const uintptr_t offset_texcoord = offsetof(struct TRSPK_VertexOpenGl3, texcoord);
    const uintptr_t offset_tex_id = offsetof(struct TRSPK_VertexOpenGl3, tex_id);
    const uintptr_t offset_uv_mode = offsetof(struct TRSPK_VertexOpenGl3, uv_mode);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_gpu);
    if( renderer->a_position >= 0 )
    {
        glEnableVertexAttribArray((GLuint)renderer->a_position);
        glVertexAttribPointer(
            (GLuint)renderer->a_position,
            4,
            GL_FLOAT,
            GL_FALSE,
            stride,
            (const void*)offset_position);
    }
    if( renderer->a_color >= 0 )
    {
        glEnableVertexAttribArray((GLuint)renderer->a_color);
        glVertexAttribPointer(
            (GLuint)renderer->a_color, 4, GL_FLOAT, GL_FALSE, stride, (const void*)offset_color);
    }
    if( renderer->a_texcoord >= 0 )
    {
        glEnableVertexAttribArray((GLuint)renderer->a_texcoord);
        glVertexAttribPointer(
            (GLuint)renderer->a_texcoord,
            2,
            GL_FLOAT,
            GL_FALSE,
            stride,
            (const void*)offset_texcoord);
    }
    if( renderer->a_tex_id >= 0 )
    {
        glEnableVertexAttribArray((GLuint)renderer->a_tex_id);
        glVertexAttribPointer(
            (GLuint)renderer->a_tex_id, 1, GL_FLOAT, GL_FALSE, stride, (const void*)offset_tex_id);
    }
    if( renderer->a_uv_mode >= 0 )
    {
        glEnableVertexAttribArray((GLuint)renderer->a_uv_mode);
        glVertexAttribPointer(
            (GLuint)renderer->a_uv_mode,
            1,
            GL_FLOAT,
            GL_FALSE,
            stride,
            (const void*)offset_uv_mode);
    }
}

static bool
gl3_check_error(const char* where)
{
    const GLenum err = glGetError();
    if( err == GL_NO_ERROR )
        return true;
    fprintf(stderr, "OpenGL3: %s: glGetError 0x%x\n", where, (unsigned)err);
    return false;
}

static GLuint
gl3_compile_shader(
    GLenum type,
    const char* src)
{
    GLuint shader = glCreateShader(type);
    if( shader == 0u )
    {
        fprintf(stderr, "OpenGL3: glCreateShader failed\n");
        return 0u;
    }
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char buf[512];
        glGetShaderInfoLog(shader, (GLsizei)sizeof(buf), NULL, buf);
        fprintf(stderr, "OpenGL3: shader compile failed: %s\n", buf);
        glDeleteShader(shader);
        return 0u;
    }
    if( !gl3_check_error("compile shader") )
    {
        glDeleteShader(shader);
        return 0u;
    }
    return shader;
}

static void
gl3_destroy_gl_resources(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    if( renderer->program3d )
        glDeleteProgram(renderer->program3d);
    if( renderer->ubo )
        glDeleteBuffers(1, &renderer->ubo);
    if( renderer->ebo )
        glDeleteBuffers(1, &renderer->ebo);
    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        struct GL3ModelGroup* g = &renderer->groups[gi];
        if( g->vao )
            glDeleteVertexArrays(1, &g->vao);
        if( g->vbo_gpu )
            glDeleteBuffers(1, &g->vbo_gpu);
        g->vao = 0u;
        g->vbo_gpu = 0u;
        g->gpu_capacity = 0u;
    }
    if( renderer->atlas_texture )
        glDeleteTextures(1, &renderer->atlas_texture);
    if( renderer->program2d )
        glDeleteProgram(renderer->program2d);
    if( renderer->sprite_atlas_texture )
        glDeleteTextures(1, &renderer->sprite_atlas_texture);
    if( renderer->white_texture )
        glDeleteTextures(1, &renderer->white_texture);
    if( renderer->quad_vao )
        glDeleteVertexArrays(1, &renderer->quad_vao);
    if( renderer->quad_vbo )
        glDeleteBuffers(1, &renderer->quad_vbo);
#if defined(TORIRS_ENABLE_LVGL_HUD)
    if( renderer->hud_texture )
        glDeleteTextures(1, &renderer->hud_texture);
    renderer->hud_texture = 0u;
#endif
    for( int i = 0; i < TRSPK_GL3_FONT_CAP; i++ )
    {
        if( renderer->font_slots[i].texture )
            glDeleteTextures(1, &renderer->font_slots[i].texture);
    }
    renderer->program3d = 0u;
    renderer->program2d = 0u;
    renderer->ubo = 0u;
    renderer->ebo = 0u;
    renderer->atlas_texture = 0u;
    renderer->sprite_atlas_texture = 0u;
    renderer->white_texture = 0u;
}

static void
gl3_upload_sprite_atlas(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    if( !trspk_atlas_is_initialized(&renderer->sprite_atlas) )
        return;
    if( !trspk_atlas_is_dirty(&renderer->sprite_atlas) )
        return;
    if( !renderer->sprite_atlas.pixels || renderer->sprite_atlas.width == 0u ||
        renderer->sprite_atlas.height == 0u )
        return;

    if( !renderer->sprite_atlas_texture )
        glGenTextures(1, &renderer->sprite_atlas_texture);
    if( !renderer->sprite_atlas_texture )
        return;

    glBindTexture(GL_TEXTURE_2D, renderer->sprite_atlas_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if( !renderer->sprite_atlas_texture_allocated )
    {
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            (GLsizei)renderer->sprite_atlas.width,
            (GLsizei)renderer->sprite_atlas.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            NULL);
        renderer->sprite_atlas_texture_allocated = true;
    }

    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        (GLsizei)renderer->sprite_atlas.width,
        (GLsizei)renderer->sprite_atlas.height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        renderer->sprite_atlas.pixels);
    trspk_atlas_clear_dirty(&renderer->sprite_atlas);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void
gl3_sprite_uv_clamp_set(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    bool enable,
    float u0,
    float v0,
    float u1,
    float v1)
{
    if( renderer->u2d_uv_clamp >= 0 )
        glUniform1i(renderer->u2d_uv_clamp, enable ? 1 : 0);
    if( enable && renderer->u2d_uv_bounds >= 0 )
        glUniform4f(renderer->u2d_uv_bounds, u0, v0, u1, v1);
}

static void
gl3_apply_logical_scissor(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    int logical_x,
    int logical_y,
    int logical_w,
    int logical_h)
{
    int gl_x = 0;
    int gl_y = 0;
    int gl_w = 0;
    int gl_h = 0;
    trspk_logical_rect_to_framebuffer(
        logical_x,
        logical_y,
        logical_w,
        logical_h,
        renderer->height,
        renderer->lb_x,
        renderer->lb_y,
        renderer->lb_w,
        renderer->lb_h,
        renderer->width,
        renderer->height,
        &gl_x,
        &gl_y,
        &gl_w,
        &gl_h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(gl_x, gl_y, gl_w, gl_h);
}

static void
gl3_flush_2d_batch(struct LibToriPlatformSDL2_RendererGL3* renderer);

static void
gl3_batch2d_flush_if_needed(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    GLuint texture,
    int text_mode,
    bool uv_clamp,
    float const uv_bounds[4],
    int scissor_x,
    int scissor_y,
    int scissor_w,
    int scissor_h,
    uint32_t extra_verts);

static void
gl3_batch2d_append_verts(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    GLuint texture,
    int text_mode,
    bool uv_clamp,
    float const uv_bounds[4],
    int scissor_x,
    int scissor_y,
    int scissor_w,
    int scissor_h,
    struct GL3Vertex2D const* verts,
    uint32_t vert_count)
{
    gl3_batch2d_flush_if_needed(
        renderer,
        texture,
        text_mode,
        uv_clamp,
        uv_bounds,
        scissor_x,
        scissor_y,
        scissor_w,
        scissor_h,
        vert_count);

    struct GL3Batch2DState* b = &renderer->batch2d;
    b->texture = texture;
    b->text_mode = text_mode;
    b->uv_clamp = uv_clamp;
    if( uv_bounds )
    {
        b->uv_bounds[0] = uv_bounds[0];
        b->uv_bounds[1] = uv_bounds[1];
        b->uv_bounds[2] = uv_bounds[2];
        b->uv_bounds[3] = uv_bounds[3];
    }
    b->scissor_set = scissor_w > 0 && scissor_h > 0;
    if( b->scissor_set )
    {
        b->scissor_x = scissor_x;
        b->scissor_y = scissor_y;
        b->scissor_w = scissor_w;
        b->scissor_h = scissor_h;
    }

    memcpy(&b->verts[b->vert_count], verts, (size_t)vert_count * sizeof(struct GL3Vertex2D));
    b->vert_count += vert_count;
}

static void
gl3_set_draw_scissor(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    int logical_x,
    int logical_y,
    int logical_w,
    int logical_h)
{
    renderer->draw_scissor_x = logical_x;
    renderer->draw_scissor_y = logical_y;
    renderer->draw_scissor_w = logical_w;
    renderer->draw_scissor_h = logical_h;
}

static void
gl3_batch2d_reset(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    struct GL3Batch2DState* b = &renderer->batch2d;
    b->vert_count = 0u;
    b->texture = 0u;
    b->text_mode = 0;
    b->uv_clamp = false;
    b->scissor_set = false;
}

static void
gl3_batch2d_free(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    free(renderer->batch2d.verts);
    renderer->batch2d.verts = NULL;
    renderer->batch2d.vert_count = 0u;
}

static bool
gl3_batch2d_init(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    renderer->batch2d.verts =
        (struct GL3Vertex2D*)malloc((size_t)GL3_2D_BATCH_MAX_VERTS * sizeof(struct GL3Vertex2D));
    if( !renderer->batch2d.verts )
        return false;
    gl3_batch2d_reset(renderer);
    return true;
}

static bool
gl3_batch2d_scissor_matches(
    struct GL3Batch2DState const* b,
    int scissor_x,
    int scissor_y,
    int scissor_w,
    int scissor_h)
{
    const bool want_scissor = scissor_w > 0 && scissor_h > 0;
    if( want_scissor != b->scissor_set )
        return false;
    if( !want_scissor )
        return true;
    return b->scissor_x == scissor_x && b->scissor_y == scissor_y && b->scissor_w == scissor_w &&
           b->scissor_h == scissor_h;
}

static void
gl3_flush_2d_batch(struct LibToriPlatformSDL2_RendererGL3* renderer);

static void
gl3_batch2d_flush_if_needed(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    GLuint texture,
    int text_mode,
    bool uv_clamp,
    float const uv_bounds[4],
    int scissor_x,
    int scissor_y,
    int scissor_w,
    int scissor_h,
    uint32_t extra_verts)
{
    struct GL3Batch2DState* b = &renderer->batch2d;
    if( b->vert_count == 0u )
    {
        b->texture = texture;
        b->text_mode = text_mode;
        b->uv_clamp = uv_clamp;
        if( uv_bounds )
        {
            b->uv_bounds[0] = uv_bounds[0];
            b->uv_bounds[1] = uv_bounds[1];
            b->uv_bounds[2] = uv_bounds[2];
            b->uv_bounds[3] = uv_bounds[3];
        }
        b->scissor_set = scissor_w > 0 && scissor_h > 0;
        if( b->scissor_set )
        {
            b->scissor_x = scissor_x;
            b->scissor_y = scissor_y;
            b->scissor_w = scissor_w;
            b->scissor_h = scissor_h;
        }
        return;
    }

    bool const state_changed = b->texture != texture || b->text_mode != text_mode ||
                               b->uv_clamp != uv_clamp ||
                               !gl3_batch2d_scissor_matches(b, scissor_x, scissor_y, scissor_w, scissor_h);
    bool const uv_bounds_changed =
        uv_clamp &&
        (b->uv_bounds[0] != uv_bounds[0] || b->uv_bounds[1] != uv_bounds[1] ||
         b->uv_bounds[2] != uv_bounds[2] || b->uv_bounds[3] != uv_bounds[3]);

    if( state_changed || uv_bounds_changed || b->vert_count + extra_verts > GL3_2D_BATCH_MAX_VERTS )
        gl3_flush_2d_batch(renderer);
}

static void
gl3_batch2d_append_quad(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    GLuint texture,
    int text_mode,
    bool uv_clamp,
    float const uv_bounds[4],
    int scissor_x,
    int scissor_y,
    int scissor_w,
    int scissor_h,
    float x0,
    float y0,
    float x1,
    float y1,
    float u0,
    float v0,
    float u1,
    float v1,
    float const rgba[4])
{
    gl3_batch2d_flush_if_needed(
        renderer,
        texture,
        text_mode,
        uv_clamp,
        uv_bounds,
        scissor_x,
        scissor_y,
        scissor_w,
        scissor_h,
        6u);

    struct GL3Batch2DState* b = &renderer->batch2d;
    b->texture = texture;
    b->text_mode = text_mode;
    b->uv_clamp = uv_clamp;
    if( uv_bounds )
    {
        b->uv_bounds[0] = uv_bounds[0];
        b->uv_bounds[1] = uv_bounds[1];
        b->uv_bounds[2] = uv_bounds[2];
        b->uv_bounds[3] = uv_bounds[3];
    }
    b->scissor_set = scissor_w > 0 && scissor_h > 0;
    if( b->scissor_set )
    {
        b->scissor_x = scissor_x;
        b->scissor_y = scissor_y;
        b->scissor_w = scissor_w;
        b->scissor_h = scissor_h;
    }

    struct GL3Vertex2D* dst = &b->verts[b->vert_count];
    dst[0] = (struct GL3Vertex2D){ { x0, y0 }, { u0, v0 }, { rgba[0], rgba[1], rgba[2], rgba[3] } };
    dst[1] = (struct GL3Vertex2D){ { x1, y0 }, { u1, v0 }, { rgba[0], rgba[1], rgba[2], rgba[3] } };
    dst[2] = (struct GL3Vertex2D){ { x1, y1 }, { u1, v1 }, { rgba[0], rgba[1], rgba[2], rgba[3] } };
    dst[3] = (struct GL3Vertex2D){ { x0, y0 }, { u0, v0 }, { rgba[0], rgba[1], rgba[2], rgba[3] } };
    dst[4] = (struct GL3Vertex2D){ { x1, y1 }, { u1, v1 }, { rgba[0], rgba[1], rgba[2], rgba[3] } };
    dst[5] = (struct GL3Vertex2D){ { x0, y1 }, { u0, v1 }, { rgba[0], rgba[1], rgba[2], rgba[3] } };
    b->vert_count += 6u;
}

static void
gl3_flush_2d_batch(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    struct GL3Batch2DState* b = &renderer->batch2d;
    if( b->vert_count == 0u )
        return;

    if( b->scissor_set )
        gl3_apply_logical_scissor(renderer, b->scissor_x, b->scissor_y, b->scissor_w, b->scissor_h);
    else
        glDisable(GL_SCISSOR_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(renderer->program2d);
    glUniformMatrix4fv(renderer->u2d_projection, 1, GL_FALSE, renderer->proj2d);
    glBindVertexArray(renderer->quad_vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, b->texture);
    glUniform1i(renderer->u2d_texture, 0);
    if( renderer->u2d_text_mode >= 0 )
        glUniform1i(renderer->u2d_text_mode, b->text_mode);
    if( renderer->u2d_uv_clamp >= 0 )
        glUniform1i(renderer->u2d_uv_clamp, b->uv_clamp ? 1 : 0);
    if( b->uv_clamp && renderer->u2d_uv_bounds >= 0 )
        glUniform4f(
            renderer->u2d_uv_bounds, b->uv_bounds[0], b->uv_bounds[1], b->uv_bounds[2], b->uv_bounds[3]);

    glBindBuffer(GL_ARRAY_BUFFER, renderer->quad_vbo);
    glBufferSubData(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)(b->vert_count * sizeof(struct GL3Vertex2D)), b->verts);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)b->vert_count);

    b->vert_count = 0u;
}

static void
gl3_draw_textured_quad_immediate(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    float x0,
    float y0,
    float x1,
    float y1,
    float u0,
    float v0,
    float u1,
    float v1,
    float const rgba[4])
{
    struct GL3Vertex2D verts[6] = {
        { { x0, y0 }, { u0, v0 }, { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { x1, y0 }, { u1, v0 }, { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { x1, y1 }, { u1, v1 }, { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { x0, y0 }, { u0, v0 }, { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { x1, y1 }, { u1, v1 }, { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { x0, y1 }, { u0, v1 }, { rgba[0], rgba[1], rgba[2], rgba[3] } },
    };
    glBindBuffer(GL_ARRAY_BUFFER, renderer->quad_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void
gl3_draw_textured_quad(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    GLuint texture,
    int text_mode,
    bool uv_clamp,
    float const uv_bounds[4],
    float x0,
    float y0,
    float x1,
    float y1,
    float u0,
    float v0,
    float u1,
    float v1,
    float const rgba[4])
{
    gl3_batch2d_append_quad(
        renderer,
        texture,
        text_mode,
        uv_clamp,
        uv_bounds,
        renderer->draw_scissor_x,
        renderer->draw_scissor_y,
        renderer->draw_scissor_w,
        renderer->draw_scissor_h,
        x0,
        y0,
        x1,
        y1,
        u0,
        v0,
        u1,
        v1,
        rgba);
}

static void
gl3_draw_textured_quad_uv4(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    float const pos[4][2],
    float const uv[4][2],
    float const rgba[4])
{
    struct GL3Vertex2D verts[6] = {
        { { pos[0][0], pos[0][1] },
         { uv[0][0], uv[0][1] },
         { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { pos[1][0], pos[1][1] },
         { uv[1][0], uv[1][1] },
         { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { pos[2][0], pos[2][1] },
         { uv[2][0], uv[2][1] },
         { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { pos[0][0], pos[0][1] },
         { uv[0][0], uv[0][1] },
         { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { pos[2][0], pos[2][1] },
         { uv[2][0], uv[2][1] },
         { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { pos[3][0], pos[3][1] },
         { uv[3][0], uv[3][1] },
         { rgba[0], rgba[1], rgba[2], rgba[3] } },
    };
    gl3_batch2d_append_verts(
        renderer,
        renderer->sprite_atlas_texture,
        0,
        false,
        NULL,
        renderer->draw_scissor_x,
        renderer->draw_scissor_y,
        renderer->draw_scissor_w,
        renderer->draw_scissor_h,
        verts,
        6u);
}

#if defined(TORIRS_ENABLE_LVGL_HUD)
static void
gl3_composite_hud(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance)
{
    if( !renderer->hud || !renderer->hud_texture )
        return;

    LibToriHud_Update(renderer->hud, instance);

    int hud_w = 0;
    int hud_h = 0;
    int hud_pitch = 0;
    uint8_t const* pixels = LibToriHud_PixelsBGRA(renderer->hud, &hud_w, &hud_h, &hud_pitch);
    if( !pixels || hud_w <= 0 || hud_h <= 0 )
        return;

    glBindTexture(GL_TEXTURE_2D, renderer->hud_texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, hud_pitch / 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, hud_w, hud_h, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(renderer->program2d);
    glViewport(renderer->lb_x, renderer->lb_y, renderer->lb_w, renderer->lb_h);
    trspk_mat4_ortho2d_top_left(renderer->proj2d, 0.0f, (float)renderer->width, (float)renderer->height, 0.0f);
    glUniformMatrix4fv(renderer->u2d_projection, 1, GL_FALSE, renderer->proj2d);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer->hud_texture);
    glUniform1i(renderer->u2d_texture, 0);
    if( renderer->u2d_text_mode >= 0 )
        glUniform1i(renderer->u2d_text_mode, 0);
    if( renderer->u2d_uv_clamp >= 0 )
        glUniform1i(renderer->u2d_uv_clamp, 0);
    glBindVertexArray(renderer->quad_vao);

    float const white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    gl3_flush_2d_batch(renderer);
    glDisable(GL_SCISSOR_TEST);
    gl3_draw_textured_quad_immediate(
        renderer,
        (float)LIBTORI_HUD_PANEL_X,
        (float)LIBTORI_HUD_PANEL_Y,
        (float)(LIBTORI_HUD_PANEL_X + hud_w),
        (float)(LIBTORI_HUD_PANEL_Y + hud_h),
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        white);
    glBindVertexArray(0);
}
#endif

static void
gl3_draw_sprite_rotated(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_RenderCommand* command,
    struct ToriDraw_Sprite* sp,
    float u0,
    float v0,
    float u1,
    float v1,
    float const rgba[4])
{
    const int dst_x = command->u.sprite.x;
    const int dst_y = command->u.sprite.y;
    const int dst_w = command->u.sprite.w > 0 ? command->u.sprite.w : sp->width;
    const int dst_h = command->u.sprite.h > 0 ? command->u.sprite.h : sp->height;
    if( dst_w <= 0 || dst_h <= 0 )
        return;

    float pos[4][2];
    float uv[4][2];
    trspk_sprite_rotated_corners(
        dst_x,
        dst_y,
        dst_w,
        dst_h,
        command->u.sprite.dst_anchor_x,
        command->u.sprite.dst_anchor_y,
        command->u.sprite.src_anchor_x,
        command->u.sprite.src_anchor_y,
        sp->crop_width > 0 ? sp->crop_width : sp->width,
        sp->crop_height > 0 ? sp->crop_height : sp->height,
        command->u.sprite.rotation,
        u0,
        v0,
        u1,
        v1,
        pos,
        uv);

    gl3_draw_textured_quad_uv4(renderer, pos, uv, rgba);
}

static bool
gl3_bake_font_atlas(
    struct GL3FontSlot* slot,
    int font_id)
{
    struct ToriDraw_Font* font = slot->font;
    if( !font || slot->baked )
        return slot->baked;

    int max_w = 0;
    int total_h = 0;
    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        if( font->glyph_width[i] > max_w )
            max_w = font->glyph_width[i];
        total_h += font->glyph_height[i];
    }
    if( max_w <= 0 || total_h <= 0 )
    {
        fprintf(stderr,
            "gl3_bake_font_atlas: invalid atlas dimensions font_id=%d max_w=%d total_h=%d "
            "line_height=%d\n",
            font_id,
            max_w,
            total_h,
            font->line_height);
        return false;
    }

    int const atlas_w = max_w;
    int const atlas_h = total_h;
    size_t const pixel_count = (size_t)atlas_w * (size_t)atlas_h;
    uint8_t* alpha = calloc(pixel_count, 1);
    if( !alpha )
    {
        fprintf(stderr, "gl3_bake_font_atlas: alpha alloc failed font_id=%d\n", font_id);
        return false;
    }

    int y = 0;
    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        int const gw = font->glyph_width[i];
        int const gh = font->glyph_height[i];
        uint8_t const* src = font->glyph_alpha[i];
        if( src && gw > 0 && gh > 0 )
        {
            for( int row = 0; row < gh; row++ )
            {
                for( int col = 0; col < gw; col++ )
                    alpha[(y + row) * atlas_w + col] = src[col + row * gw];
            }
        }
        float const u0 = 0.0f;
        float const v0 = (float)y / (float)atlas_h;
        float const u1 = (float)gw / (float)atlas_w;
        float const v1 = (float)(y + gh) / (float)atlas_h;
        slot->glyph_uv[i * 4 + 0] = u0;
        slot->glyph_uv[i * 4 + 1] = v0;
        slot->glyph_uv[i * 4 + 2] = u1;
        slot->glyph_uv[i * 4 + 3] = v1;
        y += gh;
    }

    if( slot->texture )
        glDeleteTextures(1, &slot->texture);
    slot->texture = 0u;

    GLuint tex = 0u;
    glGenTextures(1, &tex);
    if( tex == 0u )
    {
        free(alpha);
        fprintf(stderr, "gl3_bake_font_atlas: glGenTextures failed font_id=%d\n", font_id);
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, tex);
    {
        GLint unpack_align = 0;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_align);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_R8,
            atlas_w,
            atlas_h,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            alpha);
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_align);
    }
    free(alpha);

    if( !gl3_check_error("gl3_bake_font_atlas glTexImage2D") )
    {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &tex);
        fprintf(stderr,
            "gl3_bake_font_atlas: texture upload failed font_id=%d atlas=%dx%d\n",
            font_id,
            atlas_w,
            atlas_h);
        return false;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    slot->texture = tex;
    slot->atlas_w = atlas_w;
    slot->atlas_h = atlas_h;
    slot->baked = true;
    return true;
}

static struct ToriDraw_Scene*
get_context(struct LibToriRS_Instance* instance)
{
    return LibToriRS_GetCurrentToriDrawScene(instance);
}

static struct GL3FontSlot*
gl3_ensure_font_slot(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    int font_id)
{
    if( font_id < 0 || font_id >= TRSPK_GL3_FONT_CAP )
        return NULL;

    struct GL3FontSlot* slot = &renderer->font_slots[font_id];
    if( !slot->font )
    {
        struct ToriDraw_Scene* scene = get_context(instance);
        if( scene )
            slot->font = ToriDraw_SceneFontGet(scene, font_id);
    }
    if( slot->font && !slot->baked )
    {
        if( !gl3_bake_font_atlas(slot, font_id) )
        {
            struct ToriDraw_Font* font = slot->font;
            fprintf(stderr,
                "gl3_ensure_font_slot: bake failed font_id=%d line_height=%d\n",
                font_id,
                font ? font->line_height : -1);
        }
    }
    return slot;
}

static void
gl3_sync_scene_fonts(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance)
{
    struct ToriDraw_Scene* scene = get_context(instance);
    if( !scene )
        return;

    for( int font_id = 0; font_id < TRSPK_GL3_FONT_CAP; font_id++ )
    {
        if( ToriDraw_SceneFontHas(scene, font_id) )
            gl3_ensure_font_slot(renderer, instance, font_id);
    }
}

struct GL3FontGlyphCtx
{
    struct LibToriPlatformSDL2_RendererGL3* renderer;
    struct GL3FontSlot* slot;
    float alpha;
    bool shadow;
};

static void
gl3_font_glyph_callback(
    void* ctx,
    struct ToriDraw_Font* font,
    int gi,
    int x,
    int y,
    int color_rgb)
{
    (void)font;
    struct GL3FontGlyphCtx* gctx = (struct GL3FontGlyphCtx*)ctx;
    struct LibToriPlatformSDL2_RendererGL3* renderer = gctx->renderer;
    struct GL3FontSlot* slot = gctx->slot;

    int const gw = slot->font->glyph_width[gi];
    int const gh = slot->font->glyph_height[gi];
    if( gw <= 0 || gh <= 0 )
        return;

    float const gx = (float)x;
    float const gy = (float)y;
    float const u0 = slot->glyph_uv[gi * 4 + 0];
    float const v0 = slot->glyph_uv[gi * 4 + 1];
    float const u1 = slot->glyph_uv[gi * 4 + 2];
    float const v1 = slot->glyph_uv[gi * 4 + 3];

    int const rgb = gctx->shadow ? 0 : color_rgb;
    float rgba[4];
    trspk_color_rgb_to_rgba(rgb, gctx->alpha, rgba);
    gl3_draw_textured_quad(
        renderer,
        slot->texture,
        1,
        false,
        NULL,
        gx,
        gy,
        gx + (float)gw,
        gy + (float)gh,
        u0,
        v0,
        u1,
        v1,
        rgba);
}

static void
gl3_draw_font_glyphs(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct GL3FontSlot* slot,
    struct ToriDraw_Font* font,
    char const* text,
    int x,
    int y,
    int default_color_rgb,
    float alpha,
    bool shadow,
    bool center)
{
    if( !text || !slot->baked || !font || slot->texture == 0u )
        return;

    gl3_flush_2d_batch(renderer);

    struct GL3FontGlyphCtx ctx = {
        .renderer = renderer,
        .slot = slot,
        .alpha = alpha,
        .shadow = shadow,
    };
    ToriDraw_FontVisitGlyphsStyled(
        font, text, x, y, default_color_rgb, center, gl3_font_glyph_callback, &ctx);

    gl3_flush_2d_batch(renderer);
    gl3_batch2d_reset(renderer);
}

static void
gl3_ev_fill_rect(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    if( !renderer->in2d )
        return;

    int const x = command->u.fill_rect.x;
    int const y = command->u.fill_rect.y;
    int const w = command->u.fill_rect.w;
    int const h = command->u.fill_rect.h;
    if( w <= 0 || h <= 0 )
        return;

    gl3_set_draw_scissor(
        renderer,
        command->u.fill_rect.scissor_x,
        command->u.fill_rect.scissor_y,
        command->u.fill_rect.scissor_w,
        command->u.fill_rect.scissor_h);

    float rgba[4];
    trspk_color_argb_to_rgba(command->u.fill_rect.argb, rgba);

    gl3_draw_textured_quad(
        renderer,
        renderer->white_texture,
        0,
        false,
        NULL,
        (float)x,
        (float)y,
        (float)(x + w),
        (float)(y + h),
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        rgba);
}

static void
gl3_ev_begin_2d(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    (void)command;
    renderer->in2d = true;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(renderer->program2d);
    glViewport(renderer->lb_x, renderer->lb_y, renderer->lb_w, renderer->lb_h);
    trspk_mat4_ortho2d_top_left(renderer->proj2d, 0.0f, (float)renderer->width, (float)renderer->height, 0.0f);
    glUniformMatrix4fv(renderer->u2d_projection, 1, GL_FALSE, renderer->proj2d);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer->sprite_atlas_texture);
    glUniform1i(renderer->u2d_texture, 0);
    if( renderer->u2d_text_mode >= 0 )
        glUniform1i(renderer->u2d_text_mode, 0);
    if( renderer->u2d_uv_clamp >= 0 )
        glUniform1i(renderer->u2d_uv_clamp, 0);
    glBindVertexArray(renderer->quad_vao);
    gl3_batch2d_reset(renderer);
    renderer->draw_scissor_x = 0;
    renderer->draw_scissor_y = 0;
    renderer->draw_scissor_w = 0;
    renderer->draw_scissor_h = 0;
    gl3_sync_scene_fonts(renderer, instance);
}

static void
gl3_ev_end_2d(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    (void)command;
    gl3_flush_2d_batch(renderer);
    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    renderer->in2d = false;
}

static void
gl3_ev_sprite_load(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    const int element_id = command->u.sprite_load.element_id;
    if( element_id < 0 || element_id >= TRSPK_GL3_SPRITE_CAP )
        return;

    struct GL3SpriteSlot* slot = &renderer->sprite_slots[element_id];
    slot->sprites = command->u.sprite_load.sprites;
    slot->count = command->u.sprite_load.count;
    free(slot->uvs);
    slot->uvs = calloc((size_t)slot->count * 4u, sizeof(float));
    if( !slot->uvs )
        return;

    for( int i = 0; i < slot->count; i++ )
    {
        struct ToriDraw_Sprite* sp = slot->sprites[i];
        if( !sp || !sp->pixels_argb )
        {
            fprintf(stderr,
                "gl3_ev_sprite_load: missing sprite pixels element_id=%d atlas_index=%d\n",
                element_id,
                i);
            assert(sp && sp->pixels_argb);
            continue;
        }
        uint32_t* rgba = malloc((size_t)sp->width * (size_t)sp->height * sizeof(uint32_t));
        if( !rgba )
            continue;
        for( int p = 0; p < sp->width * sp->height; p++ )
        {
            uint32_t pix = sp->pixels_argb[p];
            uint8_t a_hi = (uint8_t)((pix >> 24) & 0xFFu);
            uint32_t rgb = pix & 0x00FFFFFFu;
            uint8_t a = (a_hi != 0) ? a_hi : (rgb != 0u ? 0xFFu : 0u);
            uint8_t r = (uint8_t)((pix >> 16) & 0xFFu);
            uint8_t g = (uint8_t)((pix >> 8) & 0xFFu);
            uint8_t b = (uint8_t)(pix & 0xFFu);
            rgba[p] = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
        }

        int upload_x = 0;
        int upload_y = 0;
        int upload_w = sp->width;
        int upload_h = sp->height;
        const bool content_embedded =
            sp->crop_width > 0 && (sp->crop_width < sp->width || sp->crop_height < sp->height);
        if( content_embedded )
        {
            upload_x = sp->crop_x;
            upload_y = sp->crop_y;
            upload_w = sp->crop_width;
            upload_h = sp->crop_height;
            if( upload_x < 0 )
                upload_x = 0;
            if( upload_y < 0 )
                upload_y = 0;
            if( upload_x + upload_w > sp->width )
                upload_w = sp->width - upload_x;
            if( upload_y + upload_h > sp->height )
                upload_h = sp->height - upload_y;
        }
        if( upload_w <= 0 || upload_h <= 0 )
        {
            free(rgba);
            continue;
        }

        const uint8_t* crop_pixels =
            (const uint8_t*)rgba + ((size_t)upload_y * (size_t)sp->width + (size_t)upload_x) * 4u;
        struct TRSPK_AtlasTile tile;
        if( !trspk_atlas_binpack_insert(
                &renderer->sprite_atlas,
                crop_pixels,
                (uint32_t)sp->width * 4u,
                (uint32_t)upload_w,
                (uint32_t)upload_h,
                &tile) )
        {
            free(rgba);
            continue;
        }
        free(rgba);
        slot->uvs[i * 4 + 0] = tile.u_start;
        slot->uvs[i * 4 + 1] = tile.v_start;
        slot->uvs[i * 4 + 2] = tile.u_end;
        slot->uvs[i * 4 + 3] = tile.v_end;
    }
    gl3_upload_sprite_atlas(renderer);
}

static void
gl3_draw_sprite_tiled(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct ToriDraw_Sprite* sp,
    struct LibToriRS_RenderCommand* command,
    float u0,
    float v0,
    float u1,
    float v1,
    float const rgba[4])
{
    int const tile_w = sp->width > 0 ? sp->width : 1;
    int const tile_h = sp->height > 0 ? sp->height : 1;
    int const dest_x = command->u.sprite.x;
    int const dest_y = command->u.sprite.y;
    int const dest_w = command->u.sprite.w > 0 ? command->u.sprite.w : tile_w;
    int const dest_h = command->u.sprite.h > 0 ? command->u.sprite.h : tile_h;
    int const origin_x = dest_x + sp->crop_x;
    int const origin_y = dest_y + sp->crop_y;

    int clip_x = 0;
    int clip_y = 0;
    int clip_w = 0;
    int clip_h = 0;
    trspk_rect_intersect(
        command->u.sprite.scissor_x,
        command->u.sprite.scissor_y,
        command->u.sprite.scissor_w,
        command->u.sprite.scissor_h,
        dest_x,
        dest_y,
        dest_w,
        dest_h,
        &clip_x,
        &clip_y,
        &clip_w,
        &clip_h);
    gl3_set_draw_scissor(renderer, clip_x, clip_y, clip_w, clip_h);

    int start_x = 0;
    int start_y = 0;
    trspk_sprite_tile_phase_origin(dest_x, dest_y, origin_x, origin_y, tile_w, tile_h, &start_x, &start_y);

    float const uv_bounds[4] = { u0, v0, u1, v1 };
    int const dest_right = dest_x + dest_w;
    int const dest_bottom = dest_y + dest_h;
    for( int ty = start_y; ty < dest_bottom; ty += tile_h )
    {
        for( int tx = start_x; tx < dest_right; tx += tile_w )
        {
            gl3_draw_textured_quad(
                renderer,
                renderer->sprite_atlas_texture,
                0,
                true,
                uv_bounds,
                (float)tx,
                (float)ty,
                (float)(tx + tile_w),
                (float)(ty + tile_h),
                u0,
                v0,
                u1,
                v1,
                rgba);
        }
    }
}

static void
gl3_ev_sprite(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    if( !renderer->in2d )
    {
        fprintf(stderr, "gl3_ev_sprite: called outside 2D pass\n");
        assert(renderer->in2d);
        return;
    }
    const int element_id = command->u.sprite.element_id;
    const int atlas_index = command->u.sprite.atlas_index;
    if( element_id < 0 || element_id >= TRSPK_GL3_SPRITE_CAP )
    {
        fprintf(stderr,
            "gl3_ev_sprite: invalid element_id=%d atlas_index=%d\n",
            element_id,
            atlas_index);
        assert(element_id >= 0 && element_id < TRSPK_GL3_SPRITE_CAP);
        return;
    }
    struct GL3SpriteSlot* slot = &renderer->sprite_slots[element_id];
    if( !slot->sprites || atlas_index < 0 || atlas_index >= slot->count || !slot->uvs )
    {
        fprintf(stderr,
            "gl3_ev_sprite: missing sprite slot element_id=%d atlas_index=%d count=%d\n",
            element_id,
            atlas_index,
            slot->count);
        assert(slot->sprites && atlas_index >= 0 && atlas_index < slot->count && slot->uvs);
        return;
    }
    struct ToriDraw_Sprite* sp = slot->sprites[atlas_index];
    if( !sp )
    {
        fprintf(stderr,
            "gl3_ev_sprite: null sprite element_id=%d atlas_index=%d\n",
            element_id,
            atlas_index);
        assert(sp);
        return;
    }

    gl3_set_draw_scissor(
        renderer,
        command->u.sprite.scissor_x,
        command->u.sprite.scissor_y,
        command->u.sprite.scissor_w,
        command->u.sprite.scissor_h);

    float u0 = slot->uvs[atlas_index * 4 + 0];
    float v0 = slot->uvs[atlas_index * 4 + 1];
    float u1 = slot->uvs[atlas_index * 4 + 2];
    float v1 = slot->uvs[atlas_index * 4 + 3];
    float alpha = command->u.sprite.alpha > 0 ? (float)command->u.sprite.alpha / 255.0f : 1.0f;
    const bool rotated = command->u.sprite.rotated != 0;
    const bool tiled = !rotated && command->u.sprite.tiled != 0;
    float x0;
    float y0;
    float x1;
    float y1;
    if( rotated )
    {
        x0 = (float)command->u.sprite.x - 0.5f;
        y0 = (float)command->u.sprite.y - 0.5f;
        x1 = x0 + (float)(command->u.sprite.w > 0 ? command->u.sprite.w : 1);
        y1 = y0 + (float)(command->u.sprite.h > 0 ? command->u.sprite.h : 1);
    }
    else
    {
        int const dst_w = command->u.sprite.w > 0 ? command->u.sprite.w : sp->width;
        int const dst_h = command->u.sprite.h > 0 ? command->u.sprite.h : sp->height;
        x0 = (float)(command->u.sprite.x + sp->crop_x);
        y0 = (float)(command->u.sprite.y + sp->crop_y);
        if( tiled )
        {
            x0 = (float)command->u.sprite.x;
            y0 = (float)command->u.sprite.y;
        }
        x1 = x0 + (float)dst_w;
        y1 = y0 + (float)dst_h;
    }
    float rgba[4] = { 1.0f, 1.0f, 1.0f, alpha };

    const int mask_element_id = command->u.sprite.mask_element_id;
    const int mask_atlas_index = command->u.sprite.mask_atlas_index;
    bool use_stencil = false;
    if( mask_element_id >= 0 && mask_element_id < TRSPK_GL3_SPRITE_CAP )
    {
        struct GL3SpriteSlot* mask_slot = &renderer->sprite_slots[mask_element_id];
        if( mask_slot->sprites && mask_atlas_index >= 0 && mask_atlas_index < mask_slot->count &&
            mask_slot->uvs && mask_slot->sprites[mask_atlas_index] )
        {
            use_stencil = true;
            glEnable(GL_STENCIL_TEST);
            glClear(GL_STENCIL_BUFFER_BIT);
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            glStencilMask(0xFF);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

            float mu0 = mask_slot->uvs[mask_atlas_index * 4 + 0];
            float mv0 = mask_slot->uvs[mask_atlas_index * 4 + 1];
            float mu1 = mask_slot->uvs[mask_atlas_index * 4 + 2];
            float mv1 = mask_slot->uvs[mask_atlas_index * 4 + 3];
            float mask_rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            float const mask_uv_bounds[4] = { mu0, mv0, mu1, mv1 };
            gl3_flush_2d_batch(renderer);
            if( renderer->draw_scissor_w > 0 && renderer->draw_scissor_h > 0 )
                gl3_apply_logical_scissor(
                    renderer,
                    renderer->draw_scissor_x,
                    renderer->draw_scissor_y,
                    renderer->draw_scissor_w,
                    renderer->draw_scissor_h);
            else
                glDisable(GL_SCISSOR_TEST);
            glUseProgram(renderer->program2d);
            glBindVertexArray(renderer->quad_vao);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, renderer->sprite_atlas_texture);
            glUniform1i(renderer->u2d_texture, 0);
            if( renderer->u2d_text_mode >= 0 )
                glUniform1i(renderer->u2d_text_mode, 0);
            gl3_sprite_uv_clamp_set(renderer, true, mu0, mv0, mu1, mv1);
            gl3_draw_textured_quad_immediate(renderer, x0, y0, x1, y1, mu0, mv0, mu1, mv1, mask_rgba);
            gl3_sprite_uv_clamp_set(renderer, false, 0.0f, 0.0f, 0.0f, 0.0f);

            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glStencilMask(0x00);
            glStencilFunc(GL_EQUAL, 1, 0xFF);
        }
    }

    if( rotated )
    {
        gl3_draw_sprite_rotated(renderer, command, sp, u0, v0, u1, v1, rgba);
    }
    else if( tiled )
    {
        gl3_draw_sprite_tiled(renderer, sp, command, u0, v0, u1, v1, rgba);
    }
    else
    {
        float const uv_bounds[4] = { u0, v0, u1, v1 };
        gl3_draw_textured_quad(
            renderer,
            renderer->sprite_atlas_texture,
            0,
            true,
            uv_bounds,
            x0,
            y0,
            x1,
            y1,
            u0,
            v0,
            u1,
            v1,
            rgba);
    }

    if( use_stencil )
        glDisable(GL_STENCIL_TEST);
}

static void
gl3_ev_font_load(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    const int font_id = command->u.font_load.font_id;
    if( font_id < 0 || font_id >= TRSPK_GL3_FONT_CAP )
        return;
    struct GL3FontSlot* slot = &renderer->font_slots[font_id];
    if( slot->font != command->u.font_load.font )
    {
        slot->font = command->u.font_load.font;
        slot->baked = false;
    }
    struct GL3FontSlot* baked_slot = gl3_ensure_font_slot(renderer, instance, font_id);
    if( baked_slot && baked_slot->font && !baked_slot->baked )
    {
        fprintf(stderr,
            "gl3_ev_font_load: bake failed font_id=%d line_height=%d\n",
            font_id,
            baked_slot->font->line_height);
    }
}

static void
gl3_ev_font(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    if( !renderer->in2d )
    {
        fprintf(stderr, "gl3_ev_font: called outside 2D pass\n");
        return;
    }
    const int font_id = command->u.font.font_id;
    if( font_id < 0 || !command->u.font.text )
    {
        fprintf(stderr,
            "gl3_ev_font: invalid font_id=%d text=%p\n",
            font_id,
            (void*)command->u.font.text);
        return;
    }
    if( command->u.font.text[0] == '\0' )
        return;
    struct GL3FontSlot* slot = gl3_ensure_font_slot(renderer, instance, font_id);
    struct ToriDraw_Font* font = slot ? slot->font : NULL;
    if( !font || !slot->baked || !ToriDraw_FontValidate(font) )
    {
        fprintf(stderr,
            "gl3_ev_font: font missing or not baked (font_id=%d font=%p baked=%d text=\"%.32s%s\" "
            "x=%d y=%d)\n",
            font_id,
            (void*)font,
            slot ? (int)slot->baked : 0,
            command->u.font.text,
            strlen(command->u.font.text) > 32 ? "..." : "",
            command->u.font.x,
            command->u.font.y);
        return;
    }

    gl3_set_draw_scissor(
        renderer,
        command->u.font.scissor_x,
        command->u.font.scissor_y,
        command->u.font.scissor_w,
        command->u.font.scissor_h);

    int x = command->u.font.x;
    int y = command->u.font.y;
    bool const center = command->u.font.center != 0;

    y -= font->line_height;

    if( command->u.font.shadowed )
        gl3_draw_font_glyphs(
            renderer,
            slot,
            font,
            command->u.font.text,
            x + 1,
            y + 1,
            command->u.font.color,
            1.0f,
            true,
            center);

    gl3_draw_font_glyphs(
        renderer,
        slot,
        font,
        command->u.font.text,
        x,
        y,
        command->u.font.color,
        1.0f,
        false,
        center);
}

static struct ToriDraw_Model*
get_model(struct ToriDraw_ModelHandle model_handle)
{
    return model_handle.u.model.model;
}

static void
upload_world_ubo(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    const float* view,
    const float* proj)
{
    TRSPK_UboWorld u = { 0 };
    memcpy(u.modelViewMatrix, view, sizeof(float) * 16u);
    memcpy(u.projectionMatrix, proj, sizeof(float) * 16u);
    u.uClock = (float)renderer->frame_clock;
    glBindBuffer(GL_UNIFORM_BUFFER, renderer->ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)sizeof(TRSPK_UboWorld), &u);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    glBindBufferRange(GL_UNIFORM_BUFFER, 0u, renderer->ubo, 0, (GLsizeiptr)sizeof(TRSPK_UboWorld));
}

static void
gl3_upload_atlas_texture(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    if( !trspk_atlas_is_initialized(&renderer->atlas) || !renderer->atlas_texture )
        return;
    if( !renderer->atlas.pixels || renderer->atlas.width == 0u || renderer->atlas.height == 0u )
        return;

    glBindTexture(GL_TEXTURE_2D, renderer->atlas_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* GL_RGBA8 (sized internal format) is required on Core Profile / Metal-backed GL. */
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        (GLsizei)renderer->atlas.width,
        (GLsizei)renderer->atlas.height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        renderer->atlas.pixels);
    trspk_atlas_clear_dirty(&renderer->atlas);
}

static void
gl3_bind_world_draw_state(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    glUseProgram(renderer->program3d);
    glBindBufferRange(GL_UNIFORM_BUFFER, 0u, renderer->ubo, 0, (GLsizeiptr)sizeof(TRSPK_UboWorld));
    if( renderer->s_atlas >= 0 )
    {
        glUniform1i(renderer->s_atlas, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, renderer->atlas_texture);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_FALSE);
}

static void
gl3_release_gpu_mesh_buffers(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        struct GL3ModelGroup* g = &renderer->groups[gi];
        if( g->vbo_gpu )
        {
            glDeleteBuffers(1, &g->vbo_gpu);
            glGenBuffers(1, &g->vbo_gpu);
            glBindVertexArray(g->vao);
            glBindBuffer(GL_ARRAY_BUFFER, g->vbo_gpu);
            glBufferData(
                GL_ARRAY_BUFFER,
                TRSPK_GL3_GPU_VBO_INIT * sizeof(struct TRSPK_VertexOpenGl3),
                NULL,
                GL_DYNAMIC_DRAW);
            bind_vbo_attribs(renderer, g->vbo_gpu);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
            glBindVertexArray(0);
            g->gpu_capacity = TRSPK_GL3_GPU_VBO_INIT;
        }
    }

    if( renderer->ebo )
    {
        glDeleteBuffers(1, &renderer->ebo);
        glGenBuffers(1, &renderer->ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            TRSPK_GL3_GPU_IBO_INIT * sizeof(uint32_t),
            NULL,
            GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        renderer->gpu_ibo_capacity = TRSPK_GL3_GPU_IBO_INIT;
    }
}

static void
gl3_reset_model_group(struct GL3ModelGroup* g)
{
    if( g->arena )
        trspk_modelarena_clear(g->arena);
}

static bool
gl3_upload_group(struct GL3ModelGroup* g)
{
    if( !g->vbo_cpu || g->vbo_gpu == 0u )
        return true;

    const uint32_t vert_count = g->vbo_cpu->vertex_count;
    if( vert_count == 0u )
    {
        trspk_vbo_clear_dirty(g->vbo_cpu);
        return true;
    }

    if( !g->reset_each_frame && !trspk_vbo_is_dirty(g->vbo_cpu) )
        return true;

    const GLsizeiptr byte_size = (GLsizeiptr)(vert_count * sizeof(struct TRSPK_VertexOpenGl3));
    if( vert_count > g->gpu_capacity )
    {
        uint32_t cap = g->gpu_capacity ? g->gpu_capacity : TRSPK_GL3_GPU_VBO_INIT;
        while( cap < vert_count )
            cap *= 2u;
        g->gpu_capacity = cap;

        glBindBuffer(GL_ARRAY_BUFFER, g->vbo_gpu);
        glBufferData(
            GL_ARRAY_BUFFER,
            (GLsizeiptr)(cap * sizeof(struct TRSPK_VertexOpenGl3)),
            NULL,
            GL_DYNAMIC_DRAW);
    }

    glBindBuffer(GL_ARRAY_BUFFER, g->vbo_gpu);
    glBufferSubData(GL_ARRAY_BUFFER, 0, byte_size, g->vbo_cpu->vertices.as_opengl3);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    trspk_vbo_clear_dirty(g->vbo_cpu);
    return true;
}

static bool
gl3_ensure_gpu_ibo(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    uint32_t index_count)
{
    if( index_count == 0u )
        return true;

    if( renderer->gpu_ibo_capacity >= index_count )
        return true;

    uint32_t cap = renderer->gpu_ibo_capacity ? renderer->gpu_ibo_capacity : TRSPK_GL3_GPU_IBO_INIT;
    while( cap < index_count )
        cap *= 2u;

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(cap * sizeof(uint32_t)), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    renderer->gpu_ibo_capacity = cap;
    return true;
}

static void
gl3_write_vertex_opengl3(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    float color[4],
    float u,
    float v,
    float tex_id,
    float uv_mode)
{
    trspk_vbo_write_vertex_opengl3(vbo, index, x, y, z, color, u, v, tex_id);
    vbo->vertices.as_opengl3[index].uv_mode = uv_mode;
}

/* -----------------------------------------------------------------------
 * Render-command dispatch
 * ----------------------------------------------------------------------- */

static void
gl3_ev_model_unload(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command);

static void
gl3_ev_tex_load(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    assert(command->kind == TORIRSRC_TEX_LOAD);

    int tex_id = command->u.tex_load.texture_id;
    struct ToriDraw_Texture* tex = command->u.tex_load.texture;
    if( tex_id < 0 || tex_id >= 256 || !tex || !tex->texels )
        return;

    static uint8_t rgba_scratch[TRSPK_ATLAS_TILE * TRSPK_ATLAS_TILE * 4u];
    trspk_atlas_decode_texture_rgba(tex, TRSPK_ATLAS_TILE, rgba_scratch);

    trspk_atlas_grid_insert_at(
        &renderer->atlas,
        (uint32_t)tex_id,
        rgba_scratch,
        TRSPK_ATLAS_TILE * 4u,
        TRSPK_ATLAS_TILE,
        TRSPK_ATLAS_TILE,
        NULL);
}

static void
gl3_ev_tex_unload(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    assert(command->kind == TORIRSRC_TEX_UNLOAD);

    const int tex_id = command->u.tex_load.texture_id;
    if( tex_id < 0 || tex_id >= 256 || !renderer->atlas.pixels )
        return;

    struct TRSPK_AtlasTile tile;
    if( !trspk_atlas_grid_tile_for_slot(&renderer->atlas, (uint32_t)tex_id, &tile) )
        return;

    const size_t row_bytes = (size_t)tile.w * renderer->atlas.channels;
    for( uint32_t row = 0; row < tile.h; row++ )
    {
        uint8_t* dst = renderer->atlas.pixels + (size_t)(tile.y + row) * renderer->atlas.stride +
                       (size_t)tile.x * renderer->atlas.channels;
        memset(dst, 0, row_bytes);
    }
    trspk_atlas_set_dirty(&renderer->atlas);
}

static void
gl3_ev_sprite_unload(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    const int element_id = command->u.sprite_load.element_id;
    if( element_id < 0 || element_id >= TRSPK_GL3_SPRITE_CAP )
        return;

    struct GL3SpriteSlot* slot = &renderer->sprite_slots[element_id];
    free(slot->uvs);
    slot->uvs = NULL;
    slot->sprites = NULL;
    slot->count = 0;
}

static void
gl3_ev_font_unload(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    const int font_id = command->u.font_load.font_id;
    if( font_id < 0 || font_id >= TRSPK_GL3_FONT_CAP )
        return;

    struct GL3FontSlot* slot = &renderer->font_slots[font_id];
    if( slot->texture )
    {
        glDeleteTextures(1, &slot->texture);
        slot->texture = 0;
    }
    slot->font = NULL;
    slot->baked = false;
    slot->atlas_w = 0;
    slot->atlas_h = 0;
    memset(slot->glyph_uv, 0, sizeof(slot->glyph_uv));
}

static void
gl3_ev_anim_unload(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    assert(command->kind == TORIRSRC_ANIM_UNLOAD);
    gl3_ev_model_unload(renderer, instance, command);
}

static void
gl3_ev_begin_3d(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    assert(command->kind == TORIRSRC_BEGIN_3D);

    renderer->cur_3d = command->u.begin_3d;
    renderer->has_3d = true;
    renderer->in3d = true;

    {
        const struct ToriDraw_ViewPort* vp = &renderer->cur_3d.view_port;
        const int vp_w = vp->width > 0 ? vp->width : renderer->width;
        const int vp_h = vp->height > 0 ? vp->height : renderer->height;
        const int wx = vp->x_center - vp_w / 2;
        const int wy = vp->y_center - vp_h / 2;
        int gl_x = 0;
        int gl_y = 0;
        int gl_w = 0;
        int gl_h = 0;
        trspk_logical_rect_to_framebuffer(
            wx,
            wy,
            vp_w,
            vp_h,
            renderer->height,
            renderer->lb_x,
            renderer->lb_y,
            renderer->lb_w,
            renderer->lb_h,
            renderer->width,
            renderer->height,
            &gl_x,
            &gl_y,
            &gl_w,
            &gl_h);
        glViewport(gl_x, gl_y, gl_w, gl_h);
    }

    trspk_compute_pass_matrices(
        renderer->view, renderer->proj, &renderer->cur_3d, renderer->width, renderer->height);
    upload_world_ubo(renderer, renderer->view, renderer->proj);

    if( trspk_atlas_is_dirty(&renderer->atlas) )
        gl3_upload_atlas_texture(renderer);

    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        if( renderer->groups[gi].reset_each_frame )
            gl3_reset_model_group(&renderer->groups[gi]);
    }
}

static void
gl3_ev_batch3d_clear(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    (void)command;

    if( !renderer->groups[TRSPK_VBO_GROUP_STATIC].arena )
        return;

    trspk_pose_table_clear(&renderer->poses);
    gl3_reset_model_group(&renderer->groups[TRSPK_VBO_GROUP_STATIC]);
    gl3_release_gpu_mesh_buffers(renderer);
}

static void
gl3_ev_model_unload(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    assert(command->kind == TORIRSRC_MODEL_UNLOAD || command->kind == TORIRSRC_ANIM_UNLOAD);

    const int element_id = command->u.model_load.element_id;
    trspk_modelarena_unload_element(renderer->groups[TRSPK_VBO_GROUP_STATIC].arena, element_id);
    trspk_pose_table_remove_element(&renderer->poses, element_id);
}

/* Stride used to encode (anim_index, frame) into the model arena's flat pose_id.
   Must exceed the maximum number of frames any animation can have. */
#define GL3_POSE_ARENA_TRACK_STRIDE 4096

static uint32_t
gl3_bake_into_arena(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct GL3ModelGroup* g,
    struct LibToriRS_Instance* instance,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position,
    bool update_pose_table)
{
    struct ToriDraw_Model* model = get_model(model_handle);
    assert(model && g->arena && g->vbo_cpu);
    ToriDraw_ModelAssertPnmTextureInvariant(model);
    if( model->face_count <= 0 )
        return UINT32_MAX;
    struct ToriDraw_Scene* ctx = get_context(instance);
    const uint32_t vert_count = (uint32_t)model->face_count * 3u;
    const uint32_t tri_count = (uint32_t)model->face_count;

    const int arena_pose_id = anim_index * GL3_POSE_ARENA_TRACK_STRIDE + pose_id;

    uint32_t slot_index = TRSPK_MODELSLOT_NULL_IDX;

    if( update_pose_table )
    {
        const uint32_t existing_slot = trspk_modelarena_find(g->arena, element_id, arena_pose_id);
        if( existing_slot != TRSPK_MODELSLOT_NULL_IDX )
            trspk_modelarena_unload(g->arena, existing_slot);
        slot_index = trspk_modelarena_load(g->arena, element_id, arena_pose_id, vert_count);
    }
    else
    {
        slot_index = trspk_modelarena_load(g->arena, element_id, arena_pose_id, vert_count);
    }

    const struct TRSPK_ModelSlot* model_slot = trspk_modelarena_get(g->arena, slot_index);
    if( !model_slot )
        return UINT32_MAX;

    const uint32_t base = model_slot->vertex_base;

    for( uint32_t face_index = 0; face_index < tri_count; face_index++ )
    {
        const uint32_t vi = base + face_index * 3u;
        struct TRSPK_ToriDrawBakeFaceVerts face;
        trspk_toridraw_bake_face(model, face_index, world_position, ctx, true, &face);

        trspk_triangles_set(&g->triangles, (base / 3u) + face_index, TRSPK_TRIANGLES_ATLAS);

        gl3_write_vertex_opengl3(
            g->vbo_cpu,
            vi + 0u,
            face.wx_a,
            face.wy_a,
            face.wz_a,
            face.color_a,
            face.uv.u1,
            face.uv.v1,
            face.tex_id_encoded,
            face.uv_mode);
        gl3_write_vertex_opengl3(
            g->vbo_cpu,
            vi + 1u,
            face.wx_b,
            face.wy_b,
            face.wz_b,
            face.color_b,
            face.uv.u2,
            face.uv.v2,
            face.tex_id_encoded,
            face.uv_mode);
        gl3_write_vertex_opengl3(
            g->vbo_cpu,
            vi + 2u,
            face.wx_c,
            face.wy_c,
            face.wz_c,
            face.color_c,
            face.uv.u3,
            face.uv.v3,
            face.tex_id_encoded,
            face.uv_mode);
    }

    if( update_pose_table )
        trspk_pose_table_set(&renderer->poses, element_id, anim_index, pose_id, base);

    return base;
}

static void
gl3_ev_anim_load(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_ANIM_LOAD);

    const int element_id = command->u.anim_load.element_id;
    struct ToriDraw_Animation* animation = command->u.anim_load.animation;
    const struct ToriDraw_ModelHandle* base_handle = &command->u.anim_load.model;
    const struct ToriDraw_Position* world_position = &command->u.anim_load.world_position;

    assert(base_handle->kind == TORIDRAWMK_MODEL && base_handle->u.model.model);

    struct ToriDraw_Model* source = base_handle->u.model.model;

    for( int frame = 0; frame < animation->frame_count; frame++ )
    {
        struct ToriDraw_Model* baked = ToriDraw_ModelCopy(source);
        assert(baked);

        ToriDraw_ModelCaptureOriginalVertices(baked);
        ToriDraw_ModelAnimateReset(baked);
        ToriDraw_ModelAnimateFrame(baked, animation->base, &animation->frames[frame]);

        struct ToriDraw_ModelHandle baked_handle = {
            .kind = TORIDRAWMK_MODEL,
            .u.model.model = baked,
        };
        gl3_bake_into_arena(
            renderer,
            &renderer->groups[TRSPK_VBO_GROUP_STATIC],
            instance,
            element_id,
            0,
            frame,
            baked_handle,
            world_position,
            true);

        ToriDraw_ModelFree(baked);
    }
}

static void
gl3_ev_model_load(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_MODEL_LOAD);
    gl3_bake_into_arena(
        renderer,
        &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        instance,
        command->u.model_load.element_id,
        0,
        0,
        command->u.model_load.model,
        &command->u.model_load.world_position,
        true);
}

static void
gl3_ev_batch3d_begin(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)renderer;
    (void)instance;
    (void)command;
}

static void
gl3_ev_batch3d_model_add(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_BATCH3D_MODEL_ADD);
    gl3_bake_into_arena(
        renderer,
        &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        instance,
        command->u.batch.element_id,
        0,
        command->u.batch.pose_id,
        command->u.batch.model,
        &command->u.batch.world_position,
        true);
}

static void
gl3_ev_batch3d_anim_add(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_BATCH3D_ANIM_ADD);
    gl3_bake_into_arena(
        renderer,
        &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        instance,
        command->u.batch.element_id,
        command->u.batch.anim_index,
        command->u.batch.pose_id,
        command->u.batch.model,
        &command->u.batch.world_position,
        true);
}

static void
gl3_ev_batch3d_end(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    (void)command;

    if( renderer->groups[TRSPK_VBO_GROUP_STATIC].vbo_cpu )
        trspk_vbo_set_dirty(renderer->groups[TRSPK_VBO_GROUP_STATIC].vbo_cpu);
}

static void
gl3_ev_model_draw(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_DRAW_MODEL);
    if( !renderer->has_3d || !renderer->ibo_chain )
        return;

    struct ToriDraw_Scene* ctx = get_context(instance);
    if( !ctx )
        return;

    const int anim_index = command->u.model.anim_index;
    const int pose_id = command->u.model.anim_frame;
    uint32_t group = TRSPK_VBO_GROUP_STATIC;
    uint32_t vertex_base = 0u;

    if( command->u.model.dynamic )
    {
        vertex_base = gl3_bake_into_arena(
            renderer,
            &renderer->groups[TRSPK_VBO_GROUP_DYNAMIC],
            instance,
            command->u.model.element_id,
            anim_index,
            pose_id,
            command->u.model.model,
            &command->u.model.world_position,
            false);
        if( vertex_base == UINT32_MAX )
            return;
        group = TRSPK_VBO_GROUP_DYNAMIC;
    }
    else
    {
        if( !trspk_pose_table_get(
                &renderer->poses, command->u.model.element_id, anim_index, pose_id, &vertex_base) )
            return;
    }

    const int face_count = ToriDraw_FaceOrderCount(ctx);
    if( face_count <= 0 )
        return;

    int* face_order = ToriDraw_FaceOrder(ctx);
    for( int i = 0; i < face_count; i++ )
    {
        const uint32_t face = (uint32_t)face_order[i];
        const uint32_t b = vertex_base + face * 3u;
        const uint32_t idx[3] = { b, b + 1u, b + 2u };
        trspk_ibochain_push32(renderer->ibo_chain, group, 0u, idx, 3u);
    }
}

static void
gl3_ev_end_3d(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)command;
    (void)instance;

    if( !renderer->has_3d )
        goto done;

    if( trspk_atlas_is_dirty(&renderer->atlas) )
        gl3_upload_atlas_texture(renderer);

    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        if( !gl3_upload_group(&renderer->groups[gi]) )
            goto done;
    }

    if( !renderer->ibo_chain || !renderer->ibo_chain->head )
        goto done;

    gl3_bind_world_draw_state(renderer);

    uint32_t total_indices = 0u;
    for( struct TRSPK_IBOChainNode* node = renderer->ibo_chain->head; node != NULL;
         node = node->next )
        total_indices += node->ibo.index_count;

    if( total_indices == 0u )
        goto done;

    if( !gl3_ensure_gpu_ibo(renderer, total_indices) )
        goto done;

    if( !trspk_ibo_reserve(renderer->ibo_staging, total_indices) )
        goto done;

    uint32_t* staging = renderer->ibo_staging->indices.as_u32;

    const struct TRSPK_Triangles* triangles_by_group[TRSPK_VBO_GROUP_COUNT] = {
        &renderer->groups[TRSPK_VBO_GROUP_STATIC].triangles,
        &renderer->groups[TRSPK_VBO_GROUP_DYNAMIC].triangles,
    };

    const uint32_t built = trspk_drawrangeex_build32(
        renderer->draw_ranges, triangles_by_group, renderer->ibo_chain, staging);

    assert(built == total_indices);
    (void)built;

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
    glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)(total_indices * sizeof(uint32_t)), staging);

    GLuint bound_vao = 0u;
    const struct TRSPK_DrawRange* range = trspk_drawrangelist_head(renderer->draw_ranges);
    while( range )
    {
        const uint32_t index_count = range->end - range->start;
        const uint32_t prim_count = index_count / 3u;

        if( prim_count > 0u )
        {
            const GLuint group_vao = renderer->groups[range->group].vao;
            if( group_vao != bound_vao )
            {
                glBindVertexArray(group_vao);
                bound_vao = group_vao;
            }

            glDrawElements(
                GL_TRIANGLES,
                (GLsizei)index_count,
                GL_UNSIGNED_INT,
                (const void*)(uintptr_t)(range->start * sizeof(uint32_t)));
        }

        range = trspk_drawrangelist_next(renderer->draw_ranges, range);
    }

    glBindVertexArray(0);

done:
    renderer->has_3d = false;
    renderer->in3d = false;
    if( renderer->ibo_chain )
        trspk_ibochain_reset(renderer->ibo_chain);
}

static void
handle_render_command(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    switch( command->kind )
    {
    case TORIRSRC_TEX_LOAD:
        gl3_ev_tex_load(renderer, instance, command);
        break;

    case TORIRSRC_TEX_UNLOAD:
        gl3_ev_tex_unload(renderer, instance, command);
        break;

    case TORIRSRC_BEGIN_3D:
        gl3_ev_begin_3d(renderer, instance, command);
        break;

    case TORIRSRC_END_3D:
        gl3_ev_end_3d(renderer, instance, command);
        break;

    case TORIRSRC_BEGIN_2D:
        gl3_ev_begin_2d(renderer, instance, command);
        break;

    case TORIRSRC_END_2D:
        gl3_ev_end_2d(renderer, instance, command);
        break;

    case TORIRSRC_CLEAR_RECT:
        break;

    case TORIRSRC_FILL_RECT:
        gl3_ev_fill_rect(renderer, instance, command);
        break;

    case TORIRSRC_SPRITE_LOAD:
        gl3_ev_sprite_load(renderer, instance, command);
        break;

    case TORIRSRC_SPRITE_UNLOAD:
        gl3_ev_sprite_unload(renderer, instance, command);
        break;

    case TORIRSRC_SPRITE:
        gl3_ev_sprite(renderer, instance, command);
        break;

    case TORIRSRC_FONT_LOAD:
        gl3_ev_font_load(renderer, instance, command);
        break;

    case TORIRSRC_FONT_UNLOAD:
        gl3_ev_font_unload(renderer, instance, command);
        break;

    case TORIRSRC_FONT:
        gl3_ev_font(renderer, instance, command);
        break;

    case TORIRSRC_ANIM_LOAD:
        gl3_ev_anim_load(renderer, instance, command);
        break;

    case TORIRSRC_ANIM_UNLOAD:
        gl3_ev_anim_unload(renderer, instance, command);
        break;

    case TORIRSRC_MODEL_LOAD:
        gl3_ev_model_load(renderer, instance, command);
        break;

    case TORIRSRC_MODEL_UNLOAD:
        gl3_ev_model_unload(renderer, instance, command);
        break;

    case TORIRSRC_BATCH3D_BEGIN:
        gl3_ev_batch3d_begin(renderer, instance, command);
        break;

    case TORIRSRC_BATCH3D_MODEL_ADD:
        gl3_ev_batch3d_model_add(renderer, instance, command);
        break;

    case TORIRSRC_BATCH3D_ANIM_ADD:
        gl3_ev_batch3d_anim_add(renderer, instance, command);
        break;

    case TORIRSRC_BATCH3D_END:
        gl3_ev_batch3d_end(renderer, instance, command);
        break;

    case TORIRSRC_BATCH3D_CLEAR:
        gl3_ev_batch3d_clear(renderer, instance, command);
        break;

    case TORIRSRC_DRAW_MODEL:
        gl3_ev_model_draw(renderer, instance, command);
        break;

    default:
        break;
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

struct LibToriPlatformSDL2_RendererGL3*
LibToriPlatformSDL2_RendererGL3_New(
    int width,
    int height)
{
    struct LibToriPlatformSDL2_RendererGL3* renderer =
        (struct LibToriPlatformSDL2_RendererGL3*)calloc(
            1, sizeof(struct LibToriPlatformSDL2_RendererGL3));
    if( !renderer )
        return NULL;

    renderer->width = width;
    renderer->height = height;

    renderer->ibo_chain = trspk_ibochain_create(TRSPK_INDEX_FORMAT_U32);
    renderer->ibo_staging = trspk_ibo_create(TRSPK_GL3_GPU_IBO_INIT, TRSPK_INDEX_FORMAT_U32);
    renderer->draw_ranges = trspk_drawrangelist_create(TRSPK_GL3_DRAWRANGE_CAP);

    if( !renderer->ibo_chain || !renderer->ibo_staging || !renderer->draw_ranges )
        goto fail;

    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        struct GL3ModelGroup* g = &renderer->groups[gi];
        g->vbo_cpu = trspk_vbo_create(0, TRSPK_VERTEX_FORMAT_OPENGL3);
        if( !g->vbo_cpu )
            goto fail;

        g->arena = trspk_modelarena_create(g->vbo_cpu, &g->triangles, TRSPK_GL3_VBO_PAGE, 64u);
        if( !g->arena )
            goto fail;

        g->reset_each_frame = (gi == TRSPK_VBO_GROUP_DYNAMIC);
        trspk_vbo_set_dirty(g->vbo_cpu);
    }

    trspk_pose_table_init(&renderer->poses);

    if( !trspk_atlas_init_grid(
            &renderer->atlas,
            TRSPK_GL3_ATLAS_DIM,
            TRSPK_GL3_ATLAS_DIM,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE,
            4u) )
        goto fail;

    if( !trspk_atlas_init_binpack(
            &renderer->sprite_atlas, TRSPK_GL3_2D_ATLAS_DIM, TRSPK_GL3_2D_ATLAS_DIM, 4u) )
        goto fail;

    trspk_vbo_set_dirty(renderer->groups[TRSPK_VBO_GROUP_STATIC].vbo_cpu);
    return renderer;

fail:
    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        struct GL3ModelGroup* g = &renderer->groups[gi];
        if( g->arena )
            trspk_modelarena_free(g->arena);
        if( g->vbo_cpu )
            trspk_vbo_free(g->vbo_cpu);
        trspk_triangles_free(&g->triangles);
    }
    if( renderer->ibo_chain )
        trspk_ibochain_free(renderer->ibo_chain);
    if( renderer->ibo_staging )
        trspk_ibo_free(renderer->ibo_staging);
    if( renderer->draw_ranges )
        trspk_drawrangelist_free(renderer->draw_ranges);
    trspk_pose_table_free(&renderer->poses);
    if( trspk_atlas_is_initialized(&renderer->atlas) )
        trspk_atlas_free(&renderer->atlas);
    free(renderer);
    return NULL;
}

void
LibToriPlatformSDL2_RendererGL3_Free(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    if( !renderer )
        return;

    if( renderer->window && renderer->gl_context )
        SDL_GL_MakeCurrent(renderer->window, renderer->gl_context);

    gl3_batch2d_free(renderer);
    gl3_destroy_gl_resources(renderer);
    gl3_release_gpu_mesh_buffers(renderer);

    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        struct GL3ModelGroup* g = &renderer->groups[gi];
        trspk_triangles_free(&g->triangles);
        if( g->arena )
        {
            trspk_modelarena_free(g->arena);
            g->arena = NULL;
        }
        if( g->vbo_cpu )
        {
            trspk_vbo_free(g->vbo_cpu);
            g->vbo_cpu = NULL;
        }
    }

    trspk_pose_table_free(&renderer->poses);

    if( renderer->ibo_chain )
    {
        trspk_ibochain_free(renderer->ibo_chain);
        renderer->ibo_chain = NULL;
    }

    if( renderer->ibo_staging )
    {
        trspk_ibo_free(renderer->ibo_staging);
        renderer->ibo_staging = NULL;
    }

    if( renderer->draw_ranges )
    {
        trspk_drawrangelist_free(renderer->draw_ranges);
        renderer->draw_ranges = NULL;
    }

    if( trspk_atlas_is_initialized(&renderer->atlas) )
        trspk_atlas_free(&renderer->atlas);

#if defined(TORIRS_ENABLE_LVGL_HUD)
    if( renderer->hud )
    {
        LibToriHud_Free(renderer->hud);
        renderer->hud = NULL;
    }
#endif

    if( renderer->gl_context )
    {
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = NULL;
    }

    free(renderer);
}

bool
LibToriPlatformSDL2_RendererGL3_Init(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    SDL_Window* window)
{
    renderer->gl_context = SDL_GL_CreateContext(window);
    if( !renderer->gl_context )
    {
        fprintf(stderr, "OpenGL3: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }

    if( SDL_GL_MakeCurrent(window, renderer->gl_context) != 0 )
    {
        fprintf(stderr, "OpenGL3: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = NULL;
        return false;
    }

    if( !trspk_sdlgl_init() )
    {
        fprintf(stderr, "OpenGL3: trspk_sdlgl_init failed\n");
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = NULL;
        return false;
    }

    SDL_GL_SetSwapInterval(0);

    const char* vs_src = trspk_opengl3_vertex_shader;
    const char* fs_src = trspk_opengl3_fragment_shader;
    GLuint vertexShader = gl3_compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fragmentShader = gl3_compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if( vertexShader == 0u || fragmentShader == 0u )
        goto fail_gl;

    renderer->program3d = glCreateProgram();
    if( renderer->program3d == 0u )
        goto fail_gl;

    glAttachShader(renderer->program3d, vertexShader);
    glAttachShader(renderer->program3d, fragmentShader);
    glBindAttribLocation(renderer->program3d, 0u, "a_position");
    glBindAttribLocation(renderer->program3d, 1u, "a_color");
    glBindAttribLocation(renderer->program3d, 2u, "a_texcoord");
    glBindAttribLocation(renderer->program3d, 3u, "a_tex_id");
    glBindAttribLocation(renderer->program3d, 4u, "a_uv_mode");
    glLinkProgram(renderer->program3d);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    vertexShader = 0u;
    fragmentShader = 0u;

    GLint link_ok = 0;
    glGetProgramiv(renderer->program3d, GL_LINK_STATUS, &link_ok);
    if( !link_ok )
    {
        char buf[512];
        glGetProgramInfoLog(renderer->program3d, (GLsizei)sizeof(buf), NULL, buf);
        fprintf(stderr, "OpenGL3: program link failed: %s\n", buf);
        goto fail_gl;
    }
    if( !gl3_check_error("link program") )
        goto fail_gl;

    const GLuint block_ix = glGetUniformBlockIndex(renderer->program3d, "TRSPK_UboWorld");
    if( block_ix != GL_INVALID_INDEX )
        glUniformBlockBinding(renderer->program3d, block_ix, 0u);

    glGenBuffers(1, &renderer->ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, renderer->ubo);
    glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)sizeof(TRSPK_UboWorld), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    renderer->a_position = glGetAttribLocation(renderer->program3d, "a_position");
    renderer->a_color = glGetAttribLocation(renderer->program3d, "a_color");
    renderer->a_texcoord = glGetAttribLocation(renderer->program3d, "a_texcoord");
    renderer->a_tex_id = glGetAttribLocation(renderer->program3d, "a_tex_id");
    renderer->a_uv_mode = glGetAttribLocation(renderer->program3d, "a_uv_mode");
    renderer->s_atlas = glGetUniformLocation(renderer->program3d, "s_atlas");

    glGenBuffers(1, &renderer->ebo);

    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        struct GL3ModelGroup* g = &renderer->groups[gi];
        glGenBuffers(1, &g->vbo_gpu);
        glGenVertexArrays(1, &g->vao);
        glBindVertexArray(g->vao);
        glBindBuffer(GL_ARRAY_BUFFER, g->vbo_gpu);
        glBufferData(
            GL_ARRAY_BUFFER,
            TRSPK_GL3_GPU_VBO_INIT * sizeof(struct TRSPK_VertexOpenGl3),
            NULL,
            GL_DYNAMIC_DRAW);
        bind_vbo_attribs(renderer, g->vbo_gpu);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
        glBindVertexArray(0);
        g->gpu_capacity = TRSPK_GL3_GPU_VBO_INIT;
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER, TRSPK_GL3_GPU_IBO_INIT * sizeof(uint32_t), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    renderer->gpu_ibo_capacity = TRSPK_GL3_GPU_IBO_INIT;

    glGenTextures(1, &renderer->atlas_texture);
    glBindTexture(GL_TEXTURE_2D, renderer->atlas_texture);
    /* GL_RGBA8 (sized internal format) is required on Core Profile / Metal-backed GL. */
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        (GLsizei)TRSPK_GL3_ATLAS_DIM,
        (GLsizei)TRSPK_GL3_ATLAS_DIM,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        renderer->atlas.pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if( !gl3_batch2d_init(renderer) )
    {
        fprintf(stderr, "OpenGL3: failed to allocate 2D batch buffer\n");
        goto fail_gl;
    }

    {
        GLuint vs2d = gl3_compile_shader(GL_VERTEX_SHADER, trspk_opengl3_2d_vertex_shader);
        GLuint fs2d = gl3_compile_shader(GL_FRAGMENT_SHADER, trspk_opengl3_2d_fragment_shader);
        if( vs2d && fs2d )
        {
            renderer->program2d = glCreateProgram();
            glAttachShader(renderer->program2d, vs2d);
            glAttachShader(renderer->program2d, fs2d);
            glBindAttribLocation(renderer->program2d, 0, "a_position");
            glBindAttribLocation(renderer->program2d, 1, "a_texcoord");
            glBindAttribLocation(renderer->program2d, 2, "a_color");
            glLinkProgram(renderer->program2d);
            GLint link_ok = 0;
            glGetProgramiv(renderer->program2d, GL_LINK_STATUS, &link_ok);
            if( !link_ok )
            {
                char buf[512];
                glGetProgramInfoLog(renderer->program2d, (GLsizei)sizeof(buf), NULL, buf);
                fprintf(stderr, "OpenGL3: program2d link failed: %s\n", buf);
                glDeleteProgram(renderer->program2d);
                renderer->program2d = 0u;
            }
            else
            {
                renderer->u2d_projection =
                    glGetUniformLocation(renderer->program2d, "u_projection");
                renderer->u2d_texture = glGetUniformLocation(renderer->program2d, "u_texture");
                renderer->u2d_text_mode = glGetUniformLocation(renderer->program2d, "u_text_mode");
                renderer->u2d_uv_clamp = glGetUniformLocation(renderer->program2d, "u_uv_clamp");
                renderer->u2d_uv_bounds = glGetUniformLocation(renderer->program2d, "u_uv_bounds");
                if( renderer->u2d_projection < 0 || renderer->u2d_texture < 0 ||
                    renderer->u2d_text_mode < 0 )
                {
                    fprintf(stderr,
                        "OpenGL3: program2d missing required uniforms "
                        "(projection=%d texture=%d text_mode=%d)\n",
                        renderer->u2d_projection,
                        renderer->u2d_texture,
                        renderer->u2d_text_mode);
                    glDeleteProgram(renderer->program2d);
                    renderer->program2d = 0u;
                }
            }
            glDeleteShader(vs2d);
            glDeleteShader(fs2d);
        }
        if( !renderer->program2d )
        {
            fprintf(stderr, "OpenGL3: failed to create 2D program\n");
            goto fail_gl;
        }
        glGenVertexArrays(1, &renderer->quad_vao);
        glGenBuffers(1, &renderer->quad_vbo);
        glBindVertexArray(renderer->quad_vao);
        glBindBuffer(GL_ARRAY_BUFFER, renderer->quad_vbo);
        glBufferData(
            GL_ARRAY_BUFFER,
            (GLsizeiptr)(GL3_2D_BATCH_MAX_VERTS * sizeof(struct GL3Vertex2D)),
            NULL,
            GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct GL3Vertex2D), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 2, GL_FLOAT, GL_FALSE, sizeof(struct GL3Vertex2D), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(
            2, 4, GL_FLOAT, GL_FALSE, sizeof(struct GL3Vertex2D), (void*)(4 * sizeof(float)));
        glBindVertexArray(0);

        {
            uint32_t const white_pixel = 0xFFFFFFFFu;
            glGenTextures(1, &renderer->white_texture);
            glBindTexture(GL_TEXTURE_2D, renderer->white_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white_pixel);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    renderer->window = window;

#if defined(TORIRS_ENABLE_LVGL_HUD)
    renderer->hud = LibToriHud_New();
    if( renderer->hud )
    {
        glGenTextures(1, &renderer->hud_texture);
        glBindTexture(GL_TEXTURE_2D, renderer->hud_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            LIBTORI_HUD_PANEL_W,
            LIBTORI_HUD_PANEL_H,
            0,
            GL_BGRA,
            GL_UNSIGNED_BYTE,
            NULL);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
#endif

    return true;

fail_gl:
    if( vertexShader )
        glDeleteShader(vertexShader);
    if( fragmentShader )
        glDeleteShader(fragmentShader);
    gl3_destroy_gl_resources(renderer);
    SDL_GL_DeleteContext(renderer->gl_context);
    renderer->gl_context = NULL;
    return false;
}

void
LibToriPlatformSDL2_RendererGL3_Render(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance)
{
    assert(renderer);
    assert(instance);

    SDL_GL_MakeCurrent(renderer->window, renderer->gl_context);

    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_GL_GetDrawableSize(renderer->window, &drawable_w, &drawable_h);

    {
        struct TRSPK_Letterbox lb;
        trspk_compute_letterbox(
            renderer->width, renderer->height, drawable_w, drawable_h, &lb);
        renderer->lb_x = lb.x;
        renderer->lb_y = lb.y;
        renderer->lb_w = lb.w;
        renderer->lb_h = lb.h;
    }

    glViewport(0, 0, drawable_w, drawable_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderer->in3d = false;
    renderer->has_3d = false;

    LibToriRS_FrameBegin(instance);
    renderer->frame_clock = (double)LibToriRS_GetAnimationClock(instance);
    struct LibToriRS_RenderCommand command;
    while( LibToriRS_FrameNextCommand(instance, &command) )
        handle_render_command(renderer, instance, &command);
    LibToriRS_FrameEnd(instance);

#if defined(TORIRS_ENABLE_LVGL_HUD)
    gl3_composite_hud(renderer, instance);
#endif

    SDL_GL_SwapWindow(renderer->window);
}

struct GL3MemStatsCountCtx
{
    uint32_t alive_model_slots;
};

static void
gl3_memstats_count_alive_slot(
    const struct TRSPK_ModelSlot* slot,
    void* user_data)
{
    (void)slot;
    struct GL3MemStatsCountCtx* ctx = (struct GL3MemStatsCountCtx*)user_data;
    ctx->alive_model_slots++;
}

void
LibToriPlatformSDL2_RendererGL3_MemStats(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriPlatformSDL2_RendererGL3_MemStats* out)
{
    assert(renderer);
    assert(out);

    memset(out, 0, sizeof(*out));

    struct GL3MemStatsCountCtx count_ctx = { 0u };
    if( renderer->groups[TRSPK_VBO_GROUP_STATIC].arena )
    {
        trspk_modelarena_visit_alive(
            renderer->groups[TRSPK_VBO_GROUP_STATIC].arena,
            gl3_memstats_count_alive_slot,
            &count_ctx);
    }
    if( renderer->groups[TRSPK_VBO_GROUP_DYNAMIC].arena )
    {
        trspk_modelarena_visit_alive(
            renderer->groups[TRSPK_VBO_GROUP_DYNAMIC].arena,
            gl3_memstats_count_alive_slot,
            &count_ctx);
    }
    out->alive_model_slots = count_ctx.alive_model_slots;
    out->pose_element_count = renderer->poses.element_count;

    const size_t vert_bytes = sizeof(struct TRSPK_VertexOpenGl3);
    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        struct GL3ModelGroup* g = &renderer->groups[gi];
        size_t cpu_bytes = 0u;
        if( g->arena && g->arena->vbo )
            cpu_bytes = (size_t)g->arena->write_cursor * vert_bytes;
        else if( g->vbo_cpu )
            cpu_bytes = (size_t)g->vbo_cpu->capacity * vert_bytes;

        const size_t gpu_bytes = (size_t)g->gpu_capacity * vert_bytes;
        if( gi == TRSPK_VBO_GROUP_STATIC )
        {
            out->cpu_vbo_static_bytes = cpu_bytes;
            out->gpu_vbo_static_bytes = gpu_bytes;
        }
        else
        {
            out->cpu_vbo_dynamic_bytes = cpu_bytes;
            out->gpu_vbo_dynamic_bytes = gpu_bytes;
        }
    }

    out->gpu_ibo_bytes = (size_t)renderer->gpu_ibo_capacity * sizeof(uint32_t);

    for( int i = 0; i < TRSPK_GL3_FONT_CAP; ++i )
    {
        if( renderer->font_slots[i].font || renderer->font_slots[i].texture )
            out->font_slots_live++;
    }

    for( int i = 0; i < TRSPK_GL3_SPRITE_CAP; ++i )
    {
        if( renderer->sprite_slots[i].sprites || renderer->sprite_slots[i].uvs )
            out->sprite_slots_live++;
    }
}
