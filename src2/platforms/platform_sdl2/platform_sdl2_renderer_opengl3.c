#include "platform_sdl2_renderer_opengl3.h"

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
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"
#include "toridraw/toridraw_math.h"
#include "toridraw/toridraw_types.h"

#include <SDL.h>
#include <assert.h>
#include <math.h>
#include <stddef.h>
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
#define TRSPK_GL3_ATLAS_TILE 128u
#define TRSPK_GL3_ATLAS_COLS 16u
#define TRSPK_GL3_DRAWRANGE_CAP 4096u
#define TRSPK_GL3_VBO_PAGE (1u << 28)
#define TRSPK_GL3_GPU_IBO_INIT 4096u
#define TRSPK_GL3_GPU_VBO_INIT 4096u
#define TRSPK_GL3_SPRITE_CAP 256
#define TRSPK_GL3_FONT_CAP 8
#define TRSPK_GL3_2D_ATLAS_DIM 2048u

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
    if( renderer->quad_vao )
        glDeleteVertexArrays(1, &renderer->quad_vao);
    if( renderer->quad_vbo )
        glDeleteBuffers(1, &renderer->quad_vbo);
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
}

static void
gl3_make_ortho2d(
    float* m,
    float left,
    float right,
    float bottom,
    float top)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / (right - left);
    m[5] = 2.0f / (top - bottom);
    m[10] = -1.0f;
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[15] = 1.0f;
}

static void
gl3_upload_sprite_atlas(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    if( !trspk_atlas_is_initialized(&renderer->sprite_atlas) )
        return;
    if( !renderer->sprite_atlas_texture )
        glGenTextures(1, &renderer->sprite_atlas_texture);
    glBindTexture(GL_TEXTURE_2D, renderer->sprite_atlas_texture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        (GLsizei)renderer->sprite_atlas.width,
        (GLsizei)renderer->sprite_atlas.height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        renderer->sprite_atlas.pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void
gl3_draw_textured_quad(
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
gl3_draw_textured_quad_uv4(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    float const pos[4][2],
    float const uv[4][2],
    float const rgba[4])
{
    struct GL3Vertex2D verts[6] = {
        { { pos[0][0], pos[0][1] }, { uv[0][0], uv[0][1] }, { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { pos[1][0], pos[1][1] }, { uv[1][0], uv[1][1] }, { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { pos[2][0], pos[2][1] }, { uv[2][0], uv[2][1] }, { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { pos[0][0], pos[0][1] }, { uv[0][0], uv[0][1] }, { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { pos[2][0], pos[2][1] }, { uv[2][0], uv[2][1] }, { rgba[0], rgba[1], rgba[2], rgba[3] } },
        { { pos[3][0], pos[3][1] }, { uv[3][0], uv[3][1] }, { rgba[0], rgba[1], rgba[2], rgba[3] } },
    };
    glBindBuffer(GL_ARRAY_BUFFER, renderer->quad_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void
gl3_sprite_local_to_screen(
    int dst_x,
    int dst_y,
    int local_x,
    int local_y,
    float* out_x,
    float* out_y)
{
    *out_x = (float)(dst_x + local_x);
    *out_y = (float)(dst_y + local_y);
}

static void
gl3_sprite_local_to_uv(
    int local_x,
    int local_y,
    int dst_anchor_x,
    int dst_anchor_y,
    int src_anchor_x,
    int src_anchor_y,
    int src_w,
    int src_h,
    int rotation_r2pi2048,
    float u0,
    float v0,
    float u1,
    float v1,
    float* out_u,
    float* out_v)
{
    const int ang = ToriDraw_NormalizeAngle(rotation_r2pi2048);
    const int cos = ToriDraw_Cos(ang);
    const int sin = ToriDraw_Sin(ang);
    const int rel_x = local_x - dst_anchor_x;
    const int rel_y = local_y - dst_anchor_y;
    const int src_rel_x = ((rel_x * cos + rel_y * sin) >> 16);
    const int src_rel_y = ((-rel_x * sin + rel_y * cos) >> 16);
    const float src_x = (float)(src_anchor_x + src_rel_x);
    const float src_y = (float)(src_anchor_y + src_rel_y);
    const float du = u1 - u0;
    const float dv = v1 - v0;
    *out_u = u0 + (src_w > 0 ? (src_x / (float)src_w) * du : 0.0f);
    *out_v = v0 + (src_h > 0 ? (src_y / (float)src_h) * dv : 0.0f);
}

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
    const int dst_anchor_x = command->u.sprite.dst_anchor_x;
    const int dst_anchor_y = command->u.sprite.dst_anchor_y;
    const int src_anchor_x = command->u.sprite.src_anchor_x;
    const int src_anchor_y = command->u.sprite.src_anchor_y;
    const int src_w = sp->crop_width > 0 ? sp->crop_width : sp->width;
    const int src_h = sp->crop_height > 0 ? sp->crop_height : sp->height;
    const int rotation = command->u.sprite.rotation;

    const int local_corners[4][2] = {
        { 0, 0 },
        { dst_w, 0 },
        { dst_w, dst_h },
        { 0, dst_h },
    };
    float pos[4][2];
    float uv[4][2];
    for( int i = 0; i < 4; i++ )
    {
        gl3_sprite_local_to_screen(
            dst_x, dst_y, local_corners[i][0], local_corners[i][1], &pos[i][0], &pos[i][1]);
        gl3_sprite_local_to_uv(
            local_corners[i][0],
            local_corners[i][1],
            dst_anchor_x,
            dst_anchor_y,
            src_anchor_x,
            src_anchor_y,
            src_w,
            src_h,
            rotation,
            u0,
            v0,
            u1,
            v1,
            &uv[i][0],
            &uv[i][1]);
    }

    if( renderer->u2d_uv_clamp >= 0 )
        glUniform1i(renderer->u2d_uv_clamp, 1);
    if( renderer->u2d_uv_bounds >= 0 )
        glUniform4f(renderer->u2d_uv_bounds, u0, v0, u1, v1);
    gl3_draw_textured_quad_uv4(renderer, pos, uv, rgba);
    if( renderer->u2d_uv_clamp >= 0 )
        glUniform1i(renderer->u2d_uv_clamp, 0);
}

static void
gl3_bake_font_atlas(struct GL3FontSlot* slot)
{
    struct ToriDraw_Font* font = slot->font;
    if( !font || slot->baked )
        return;

    int max_w = 0;
    int total_h = 0;
    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        if( font->glyph_width[i] > max_w )
            max_w = font->glyph_width[i];
        total_h += font->glyph_height[i];
    }
    if( max_w <= 0 || total_h <= 0 )
        return;

    int atlas_w = max_w;
    int atlas_h = total_h;
    uint8_t* pixels = calloc((size_t)atlas_w * (size_t)atlas_h, 1);
    if( !pixels )
        return;

    int y = 0;
    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        int gw = font->glyph_width[i];
        int gh = font->glyph_height[i];
        uint8_t const* src = font->glyph_alpha[i];
        if( src && gw > 0 && gh > 0 )
        {
            for( int row = 0; row < gh; row++ )
            {
                for( int col = 0; col < gw; col++ )
                    pixels[(y + row) * atlas_w + col] = src[col + row * gw];
            }
        }
        float u0 = 0.0f;
        float v0 = (float)y / (float)atlas_h;
        float u1 = (float)gw / (float)atlas_w;
        float v1 = (float)(y + gh) / (float)atlas_h;
        slot->glyph_uv[i * 4 + 0] = u0;
        slot->glyph_uv[i * 4 + 1] = v0;
        slot->glyph_uv[i * 4 + 2] = u1;
        slot->glyph_uv[i * 4 + 3] = v1;
        y += gh;
    }

    if( slot->texture )
        glDeleteTextures(1, &slot->texture);
    glGenTextures(1, &slot->texture);
    glBindTexture(GL_TEXTURE_2D, slot->texture);
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
            pixels);
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_align);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    slot->atlas_w = atlas_w;
    slot->atlas_h = atlas_h;
    slot->baked = true;
    free(pixels);
}

static void
gl3_color_from_rgb(
    int color,
    float alpha,
    float rgba[4])
{
    rgba[0] = (float)((color >> 16) & 0xFF) / 255.0f;
    rgba[1] = (float)((color >> 8) & 0xFF) / 255.0f;
    rgba[2] = (float)(color & 0xFF) / 255.0f;
    rgba[3] = alpha;
}

static void
gl3_draw_font_glyphs(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct GL3FontSlot* slot,
    struct ToriDraw_Font* font,
    char const* text,
    int x,
    int y,
    float const rgba[4])
{
    if( !text || !slot->baked )
        return;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, slot->texture);
    glUniform1i(renderer->u2d_texture, 0);
    if( renderer->u2d_text_mode >= 0 )
        glUniform1i(renderer->u2d_text_mode, 1);

    int cx = x;
    for( char const* p = text; *p; ++p )
    {
        int gi = (unsigned char)font->charcodeset[(unsigned char)*p];
        int gw = font->glyph_width[gi];
        int gh = font->glyph_height[gi];
        float gx = (float)(cx + font->offset_x[gi]);
        float gy = (float)(y + font->offset_y[gi]);
        float u0 = slot->glyph_uv[gi * 4 + 0];
        float v0 = slot->glyph_uv[gi * 4 + 1];
        float u1 = slot->glyph_uv[gi * 4 + 2];
        float v1 = slot->glyph_uv[gi * 4 + 3];
        gl3_draw_textured_quad(renderer, gx, gy, gx + (float)gw, gy + (float)gh, u0, v0, u1, v1, rgba);
        cx += font->advance[gi];
    }

    glBindTexture(GL_TEXTURE_2D, renderer->sprite_atlas_texture);
    glUniform1i(renderer->u2d_texture, 0);
    if( renderer->u2d_text_mode >= 0 )
        glUniform1i(renderer->u2d_text_mode, 0);
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(renderer->program2d);
    glViewport(renderer->lb_x, renderer->lb_y, renderer->lb_w, renderer->lb_h);
    gl3_make_ortho2d(renderer->proj2d, 0.0f, (float)renderer->width, (float)renderer->height, 0.0f);
    glUniformMatrix4fv(renderer->u2d_projection, 1, GL_FALSE, renderer->proj2d);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer->sprite_atlas_texture);
    glUniform1i(renderer->u2d_texture, 0);
    if( renderer->u2d_text_mode >= 0 )
        glUniform1i(renderer->u2d_text_mode, 0);
    if( renderer->u2d_uv_clamp >= 0 )
        glUniform1i(renderer->u2d_uv_clamp, 0);
    glBindVertexArray(renderer->quad_vao);
}

static void
gl3_ev_end_2d(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    (void)command;
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
            continue;
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
        struct TRSPK_AtlasTile tile;
        if( !trspk_atlas_binpack_insert(
                &renderer->sprite_atlas,
                (const uint8_t*)rgba,
                (uint32_t)sp->width * 4u,
                (uint32_t)sp->width,
                (uint32_t)sp->height,
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
gl3_ev_sprite(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    if( !renderer->in2d )
        return;
    const int element_id = command->u.sprite.element_id;
    const int atlas_index = command->u.sprite.atlas_index;
    if( element_id < 0 || element_id >= TRSPK_GL3_SPRITE_CAP )
        return;
    struct GL3SpriteSlot* slot = &renderer->sprite_slots[element_id];
    if( !slot->sprites || atlas_index < 0 || atlas_index >= slot->count || !slot->uvs )
        return;
    struct ToriDraw_Sprite* sp = slot->sprites[atlas_index];
    if( !sp )
        return;

    if( command->u.sprite.scissor_w > 0 && command->u.sprite.scissor_h > 0 )
    {
        glEnable(GL_SCISSOR_TEST);
        int sy = renderer->height - command->u.sprite.scissor_y - command->u.sprite.scissor_h;
        glScissor(
            command->u.sprite.scissor_x, sy, command->u.sprite.scissor_w, command->u.sprite.scissor_h);
    }
    else
        glDisable(GL_SCISSOR_TEST);

    float u0 = slot->uvs[atlas_index * 4 + 0];
    float v0 = slot->uvs[atlas_index * 4 + 1];
    float u1 = slot->uvs[atlas_index * 4 + 2];
    float v1 = slot->uvs[atlas_index * 4 + 3];
    float alpha = command->u.sprite.alpha > 0 ? (float)command->u.sprite.alpha / 255.0f : 1.0f;
    int w = command->u.sprite.w > 0 ? command->u.sprite.w : sp->width;
    int h = command->u.sprite.h > 0 ? command->u.sprite.h : sp->height;
    float x0 = (float)command->u.sprite.x;
    float y0 = (float)command->u.sprite.y;
    float x1 = (float)(command->u.sprite.x + w);
    float y1 = (float)(command->u.sprite.y + h);
    float rgba[4] = { 1.0f, 1.0f, 1.0f, alpha };
    const bool rotated = command->u.sprite.rotated != 0;

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
            gl3_draw_textured_quad(renderer, x0, y0, x1, y1, mu0, mv0, mu1, mv1, mask_rgba);

            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glStencilMask(0x00);
            glStencilFunc(GL_EQUAL, 1, 0xFF);
        }
    }

    if( rotated )
        gl3_draw_sprite_rotated(renderer, command, sp, u0, v0, u1, v1, rgba);
    else
        gl3_draw_textured_quad(renderer, x0, y0, x1, y1, u0, v0, u1, v1, rgba);

    if( use_stencil )
        glDisable(GL_STENCIL_TEST);
}

static void
gl3_ev_font_load(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    const int font_id = command->u.font_load.font_id;
    if( font_id < 0 || font_id >= TRSPK_GL3_FONT_CAP )
        return;
    struct GL3FontSlot* slot = &renderer->font_slots[font_id];
    slot->font = command->u.font_load.font;
    slot->baked = false;
    gl3_bake_font_atlas(slot);
}

static void
gl3_ev_font(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    if( !renderer->in2d )
        return;
    const int font_id = command->u.font.font_id;
    if( font_id < 0 || font_id >= TRSPK_GL3_FONT_CAP )
        return;
    struct GL3FontSlot* slot = &renderer->font_slots[font_id];
    struct ToriDraw_Font* font = slot->font;
    if( !font || !command->u.font.text )
        return;
    if( !slot->baked )
        gl3_bake_font_atlas(slot);
    if( !slot->baked )
        return;

    int x = command->u.font.x;
    int y = command->u.font.y;
    if( command->u.font.center )
        x -= ToriDraw2D_MeasureString(font, command->u.font.text) / 2;

    float shadow_rgba[4];
    gl3_color_from_rgb(0, 1.0f, shadow_rgba);
    if( command->u.font.shadowed )
        gl3_draw_font_glyphs(renderer, slot, font, command->u.font.text, x + 1, y + 1, shadow_rgba);

    float text_rgba[4];
    gl3_color_from_rgb(command->u.font.color, 1.0f, text_rgba);
    gl3_draw_font_glyphs(renderer, slot, font, command->u.font.text, x, y, text_rgba);
}

static struct ToriDraw_Model*
get_model(struct ToriDraw_ModelHandle model_handle)
{
    return model_handle.u.model.model;
}

static struct ToriDraw_Scene*
get_context(struct LibToriRS_Instance* instance)
{
    return LibToriRS_GetCurrentToriDrawScene(instance);
}

static void
uv_pnm_face(
    struct UVFaceCoords* out,
    struct ToriDraw_Model* model,
    uint32_t face_index)
{
    uint32_t face_a = (uint32_t)model->face_indices_a[face_index];
    uint32_t face_b = (uint32_t)model->face_indices_b[face_index];
    uint32_t face_c = (uint32_t)model->face_indices_c[face_index];

    uint32_t p_vertex = face_a;
    uint32_t m_vertex = face_b;
    uint32_t n_vertex = face_c;

    if( model->face_texture_coords && model->face_texture_coords[face_index] != -1 )
    {
        assert(model->textured_p_coordinate != NULL);
        assert(model->textured_m_coordinate != NULL);
        assert(model->textured_n_coordinate != NULL);

        uint32_t texture_face = (uint32_t)model->face_texture_coords[face_index];
        p_vertex = (uint32_t)model->textured_p_coordinate[texture_face];
        m_vertex = (uint32_t)model->textured_m_coordinate[texture_face];
        n_vertex = (uint32_t)model->textured_n_coordinate[texture_face];
    }

    uv_pnm_compute(
        out,
        model->vertices_x[p_vertex],
        model->vertices_y[p_vertex],
        model->vertices_z[p_vertex],
        model->vertices_x[m_vertex],
        model->vertices_y[m_vertex],
        model->vertices_z[m_vertex],
        model->vertices_x[n_vertex],
        model->vertices_y[n_vertex],
        model->vertices_z[n_vertex],
        model->vertices_x[face_a],
        model->vertices_y[face_a],
        model->vertices_z[face_a],
        model->vertices_x[face_b],
        model->vertices_y[face_b],
        model->vertices_z[face_b],
        model->vertices_x[face_c],
        model->vertices_y[face_c],
        model->vertices_z[face_c]);
}

static void
hsl16_to_rgba(
    uint16_t hsl16,
    uint8_t alpha,
    float rgba[4])
{
    uint32_t rgb = ToriDraw_Hsl16ToRgb(hsl16);
    rgba[0] = (float)((rgb >> 16) & 0xFFu) / 255.0f;
    rgba[1] = (float)((rgb >> 8) & 0xFFu) / 255.0f;
    rgba[2] = (float)(rgb & 0xFFu) / 255.0f;
    rgba[3] = (float)alpha / 255.0f;
}

static void
compute_view_matrix(
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

static void
compute_projection_matrix(
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

static void
compute_pass_matrices(
    float view[16],
    float proj[16],
    const struct LibToriRS_RenderCommand_Begin3D* b3d,
    int fallback_w,
    int fallback_h)
{
    const struct ToriDraw_Position* cam_pos = &b3d->camera_position;
    const struct ToriDraw_Camera* cam = &b3d->camera;
    const struct ToriDraw_ViewPort* vp = &b3d->view_port;
    const int pass_w = vp->width > 0 ? vp->width : fallback_w;
    const int pass_h = vp->height > 0 ? vp->height : fallback_h;

    compute_view_matrix(
        view,
        -(float)cam_pos->x,
        -(float)cam_pos->y,
        -(float)cam_pos->z,
        ToriDraw_AngleToRadians(cam->pitch),
        ToriDraw_AngleToRadians(cam->yaw));
    compute_projection_matrix(proj, (90.0f * (float)M_PI) / 180.0f, (float)pass_w, (float)pass_h);
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
gl3_decode_texture_rgba(
    const struct ToriDraw_Texture* tex,
    uint8_t* out_rgba)
{
    const uint32_t src_w = (uint32_t)tex->width;
    const uint32_t src_h = (uint32_t)tex->height;

    memset(out_rgba, 0, TRSPK_GL3_ATLAS_TILE * TRSPK_GL3_ATLAS_TILE * 4u);
    if( src_w == 0u || src_h == 0u )
        return;

    for( uint32_t dst_row = 0; dst_row < TRSPK_GL3_ATLAS_TILE; dst_row++ )
    {
        uint32_t src_row = (dst_row * src_h) / TRSPK_GL3_ATLAS_TILE;
        for( uint32_t dst_col = 0; dst_col < TRSPK_GL3_ATLAS_TILE; dst_col++ )
        {
            uint32_t src_col = (dst_col * src_w) / TRSPK_GL3_ATLAS_TILE;
            int texel = tex->texels[src_row * src_w + src_col];
            uint8_t rv = (uint8_t)((texel >> 16) & 0xFF);
            uint8_t gv = (uint8_t)((texel >> 8) & 0xFF);
            uint8_t bv = (uint8_t)(texel & 0xFF);
            uint8_t av = (tex->opaque || texel != 0) ? 255u : 0u;
            uint32_t idx = (dst_row * TRSPK_GL3_ATLAS_TILE + dst_col) * 4u;
            out_rgba[idx + 0] = rv;
            out_rgba[idx + 1] = gv;
            out_rgba[idx + 2] = bv;
            out_rgba[idx + 3] = av;
        }
    }
}

static float
gl3_pack_uv_mode(
    int animation_direction,
    int animation_speed)
{
    if( animation_direction == 0 || animation_speed == 0 )
        return 0.0f;

    int enc;
    if( animation_direction == 2 || animation_direction == 4 )
        enc = animation_speed * 2 + 1;
    else
        enc = animation_speed * 2 + 257;

    return (float)(2 * enc);
}

static float
gl3_encode_tex_id(
    int tex_id,
    const struct ToriDraw_Texture* tex)
{
    if( tex_id < 0 )
        return TRSPK_VERTEX_OPENGL3_TEXID_INVALID;
    if( tex && !tex->opaque )
        return (float)(tex_id + 256);
    return (float)tex_id;
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
    renderer->groups[TRSPK_VBO_GROUP_STATIC].gpu_capacity = 0u;
    renderer->gpu_ibo_capacity = 0u;
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

    static uint8_t rgba_scratch[TRSPK_GL3_ATLAS_TILE * TRSPK_GL3_ATLAS_TILE * 4];
    gl3_decode_texture_rgba(tex, rgba_scratch);

    trspk_atlas_grid_insert_at(
        &renderer->atlas,
        (uint32_t)tex_id,
        rgba_scratch,
        TRSPK_GL3_ATLAS_TILE * 4u,
        TRSPK_GL3_ATLAS_TILE,
        TRSPK_GL3_ATLAS_TILE,
        NULL);
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
        const float sx = (float)renderer->lb_w / (float)renderer->width;
        const float sy = (float)renderer->lb_h / (float)renderer->height;
        const int wx = vp->x_center - vp_w / 2;
        const int wy = vp->y_center - vp_h / 2;
        const int gl_x = renderer->lb_x + (int)(wx * sx);
        const int gl_w = (int)(vp_w * sx);
        const int gl_h = (int)(vp_h * sy);
        const int gl_y = renderer->lb_y + (int)((renderer->height - (wy + vp_h)) * sy);
        glViewport(gl_x, gl_y, gl_w, gl_h);
    }

    compute_pass_matrices(
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
    assert(command->kind == TORIRSRC_MODEL_UNLOAD);

    const int element_id = command->u.model_load.element_id;
    trspk_modelarena_unload_element(
        renderer->groups[TRSPK_VBO_GROUP_STATIC].arena, element_id);
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
    if( !model || model->face_count <= 0 || !g->arena || !g->vbo_cpu )
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

        const uint32_t face_a = (uint32_t)model->face_indices_a[face_index];
        const uint32_t face_b = (uint32_t)model->face_indices_b[face_index];
        const uint32_t face_c = (uint32_t)model->face_indices_c[face_index];

        const uint16_t color_a_hsl16 = model->face_colors_a[face_index];
        const uint16_t color_b_hsl16 = model->face_colors_b[face_index];
        const uint16_t color_c_hsl16 = model->face_colors_c[face_index];
        uint8_t alpha;
        if( model->face_alphas )
            alpha = 0xFFu - model->face_alphas[face_index];
        else
            alpha = 0xFFu;

        float color_a[4], color_b[4], color_c[4];
        if( color_c_hsl16 == TORIDRAWHSL16_HIDDEN )
        {
            alpha = 0u;
            hsl16_to_rgba(color_a_hsl16, alpha, color_a);
            color_b[0] = color_a[0];
            color_b[1] = color_a[1];
            color_b[2] = color_a[2];
            color_b[3] = color_a[3];
            color_c[0] = color_a[0];
            color_c[1] = color_a[1];
            color_c[2] = color_a[2];
            color_c[3] = color_a[3];
        }
        else if( color_c_hsl16 == TORIDRAWHSL16_FLAT )
        {
            hsl16_to_rgba(color_a_hsl16, alpha, color_a);
            color_b[0] = color_a[0];
            color_b[1] = color_a[1];
            color_b[2] = color_a[2];
            color_b[3] = color_a[3];
            color_c[0] = color_a[0];
            color_c[1] = color_a[1];
            color_c[2] = color_a[2];
            color_c[3] = color_a[3];
        }
        else
        {
            hsl16_to_rgba(color_a_hsl16, alpha, color_a);
            hsl16_to_rgba(color_b_hsl16, alpha, color_b);
            hsl16_to_rgba(color_c_hsl16, alpha, color_c);
        }

        const int tex_id = model->face_textures ? (int)model->face_textures[face_index] : -1;
        struct ToriDraw_Texture* tex = NULL;
        float uv_mode = 0.0f;
        if( tex_id >= 0 && ctx )
        {
            tex = ToriDraw_TextureMapGet(&ToriDraw_SceneTexState(ctx)->texture_map, tex_id);
            if( tex )
                uv_mode = gl3_pack_uv_mode(tex->animation_direction, tex->animation_speed);
        }

        const float tex_id_encoded = gl3_encode_tex_id(tex_id, tex);

        trspk_triangles_set(&g->triangles, (base / 3u) + face_index, TRSPK_TRIANGLES_ATLAS);

        struct UVFaceCoords uv;
        uv_pnm_face(&uv, model, face_index);

        float wx_a, wy_a, wz_a;
        float wx_b, wy_b, wz_b;
        float wx_c, wy_c, wz_c;
        trspk_toridraw_world_vertex(
            world_position,
            model->vertices_x[face_a],
            model->vertices_y[face_a],
            model->vertices_z[face_a],
            &wx_a,
            &wy_a,
            &wz_a);
        trspk_toridraw_world_vertex(
            world_position,
            model->vertices_x[face_b],
            model->vertices_y[face_b],
            model->vertices_z[face_b],
            &wx_b,
            &wy_b,
            &wz_b);
        trspk_toridraw_world_vertex(
            world_position,
            model->vertices_x[face_c],
            model->vertices_y[face_c],
            model->vertices_z[face_c],
            &wx_c,
            &wy_c,
            &wz_c);

        gl3_write_vertex_opengl3(
            g->vbo_cpu,
            vi + 0u,
            wx_a,
            wy_a,
            wz_a,
            color_a,
            uv.u1,
            uv.v1,
            tex_id_encoded,
            uv_mode);
        gl3_write_vertex_opengl3(
            g->vbo_cpu,
            vi + 1u,
            wx_b,
            wy_b,
            wz_b,
            color_b,
            uv.u2,
            uv.v2,
            tex_id_encoded,
            uv_mode);
        gl3_write_vertex_opengl3(
            g->vbo_cpu,
            vi + 2u,
            wx_c,
            wy_c,
            wz_c,
            color_c,
            uv.u3,
            uv.v3,
            tex_id_encoded,
            uv_mode);
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

    if( !animation || !animation->base || !animation->frames || animation->frame_count <= 0 )
        return;
    if( base_handle->kind != TORIDRAWMK_MODEL || !base_handle->u.model.model )
        return;

    struct ToriDraw_Model* source = base_handle->u.model.model;

    for( int frame = 0; frame < animation->frame_count; frame++ )
    {
        struct ToriDraw_Model* baked = ToriDraw_ModelCopy(source);
        if( !baked )
            continue;

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

    case TORIRSRC_SPRITE_LOAD:
        gl3_ev_sprite_load(renderer, instance, command);
        break;

    case TORIRSRC_SPRITE:
        gl3_ev_sprite(renderer, instance, command);
        break;

    case TORIRSRC_FONT_LOAD:
        gl3_ev_font_load(renderer, instance, command);
        break;

    case TORIRSRC_FONT:
        gl3_ev_font(renderer, instance, command);
        break;

    case TORIRSRC_ANIM_LOAD:
        gl3_ev_anim_load(renderer, instance, command);
        break;

    case TORIRSRC_ANIM_UNLOAD:
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

    if( !    trspk_atlas_init_grid(
            &renderer->atlas,
            TRSPK_GL3_ATLAS_DIM,
            TRSPK_GL3_ATLAS_DIM,
            TRSPK_GL3_ATLAS_TILE,
            TRSPK_GL3_ATLAS_TILE,
            4u) )
        goto fail;

    if( !trspk_atlas_init_binpack(
            &renderer->sprite_atlas,
            TRSPK_GL3_2D_ATLAS_DIM,
            TRSPK_GL3_2D_ATLAS_DIM,
            4u) )
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
            renderer->u2d_projection = glGetUniformLocation(renderer->program2d, "u_projection");
            renderer->u2d_texture = glGetUniformLocation(renderer->program2d, "u_texture");
            renderer->u2d_text_mode = glGetUniformLocation(renderer->program2d, "u_text_mode");
            renderer->u2d_uv_clamp = glGetUniformLocation(renderer->program2d, "u_uv_clamp");
            renderer->u2d_uv_bounds = glGetUniformLocation(renderer->program2d, "u_uv_bounds");
            glDeleteShader(vs2d);
            glDeleteShader(fs2d);
        }
        glGenVertexArrays(1, &renderer->quad_vao);
        glGenBuffers(1, &renderer->quad_vbo);
        glBindVertexArray(renderer->quad_vao);
        glBindBuffer(GL_ARRAY_BUFFER, renderer->quad_vbo);
        glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(struct GL3Vertex2D), NULL, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct GL3Vertex2D), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 2, GL_FLOAT, GL_FALSE, sizeof(struct GL3Vertex2D), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(
            2, 4, GL_FLOAT, GL_FALSE, sizeof(struct GL3Vertex2D), (void*)(4 * sizeof(float)));
        glBindVertexArray(0);
    }

    renderer->window = window;
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
    if( !renderer || !instance )
        return;

    SDL_GL_MakeCurrent(renderer->window, renderer->gl_context);

    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_GL_GetDrawableSize(renderer->window, &drawable_w, &drawable_h);

    {
        const float src_aspect = (float)renderer->width / (float)renderer->height;
        const float win_aspect = (float)drawable_w / (float)drawable_h;

        if( src_aspect > win_aspect )
        {
            renderer->lb_w = drawable_w;
            renderer->lb_h = (int)((float)drawable_w / src_aspect);
            renderer->lb_x = 0;
            renderer->lb_y = (drawable_h - renderer->lb_h) / 2;
        }
        else
        {
            renderer->lb_h = drawable_h;
            renderer->lb_w = (int)((float)drawable_h * src_aspect);
            renderer->lb_y = 0;
            renderer->lb_x = (drawable_w - renderer->lb_w) / 2;
        }
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

    SDL_GL_SwapWindow(renderer->window);
}
