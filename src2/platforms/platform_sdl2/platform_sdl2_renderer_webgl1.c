#include "platform_sdl2_renderer_webgl1.h"

#if defined(TORIRS_ENABLE_LVGL_HUD)
#include "platform_sdl2_lvgl_hud.h"
#include "platformkit/webgl1/webgl1_hud_shaders.h"
#endif

#include "graphics/uv_pnm.h"
#include "libtorirs.h"
#include "platformkit/core/trspk_atlas.h"
#include "platformkit/core/trspk_ibo.h"
#include "platformkit/core/trspk_modelarena.h"
#include "platformkit/core/trspk_pose.h"
#include "platformkit/core/trspk_vbochain16.h"
#include "platformkit/webgl1/trspk_webgl1.h"
#include "platforms/trspk_toridraw.h"
#include "render/libtorirs_render.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"
#include <GLES2/gl2.h>

#include <SDL.h>
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRSPK_WEBGL1_GPU_IBO_INIT 4096u
#define TRSPK_WEBGL1_ATLAS_DIM 2048u
#define TRSPK_WEBGL1_ENCODE_PAGE_LOCAL(page, local) (((page) << 16u) | ((local)&0xFFFFu))
#define TRSPK_WEBGL1_BASE_PAGE(encoded) ((encoded) >> 16u)
#define TRSPK_WEBGL1_BASE_LOCAL(encoded) ((encoded)&0xFFFFu)

struct WebGL1DrawRecord
{
    uint32_t group;
    uint32_t page;
    uint32_t draw_offset;
    uint32_t count;
};

struct WebGL1ModelGroup
{
    struct TRSPK_VBOChain16* chain;
    struct TRSPK_ModelArena* arena;
    GLuint* page_buffers;
    uint32_t* page_buffer_caps;
    uint32_t page_buffer_count;
    bool reset_each_frame;
};

struct LibToriPlatformSDL2_RendererWebGL1
{
    SDL_Window* window;
    SDL_GLContext gl_context;
    int width;
    int height;

    struct WebGL1ModelGroup groups[TRSPK_VBO_GROUP_COUNT];
    struct TRSPK_PoseTable poses;
    struct TRSPK_IBOChain* ibo_chain;

    GLuint atlas_texture;
    struct TRSPK_Atlas atlas;
    GLuint program3d;
    GLint a_position;
    GLint a_color;
    GLint a_texcoord;
    GLint a_tex_id;
    GLint a_uv_mode;
    GLint u_modelViewMatrix;
    GLint u_projectionMatrix;
    GLint u_clock;
    GLint s_atlas;

    GLuint program2d;

#if defined(TORIRS_ENABLE_LVGL_HUD)
    struct LibToriHud* hud;
    GLuint hud_texture;
    GLuint hud_vbo;
    GLint u_hud_projection;
    GLint u_hud_texture;
    float hud_proj[16];
    uint8_t* hud_upload_buf;
    size_t hud_upload_buf_size;
#endif

    GLuint ebo;
    uint32_t gpu_ibo_capacity;

    uint16_t* ibo_staging;
    uint32_t ibo_staging_capacity;

    struct WebGL1DrawRecord* draw_records;
    uint32_t draw_record_capacity;
    uint32_t draw_record_count;

    float view[16];
    float proj[16];
    struct LibToriRS_RenderCommand_Begin3D cur_3d;
    bool has_3d;
    bool in3d;
    double frame_clock;
};

static void
webgl1_free_group_gl_resources(struct WebGL1ModelGroup* g)
{
    if( g->page_buffers && g->page_buffer_count > 0u )
        glDeleteBuffers((GLsizei)g->page_buffer_count, g->page_buffers);
    free(g->page_buffers);
    free(g->page_buffer_caps);
    g->page_buffers = NULL;
    g->page_buffer_caps = NULL;
    g->page_buffer_count = 0u;
}

static void
webgl1_destroy_gl_resources(struct LibToriPlatformSDL2_RendererWebGL1* renderer)
{
    if( renderer->program3d )
        glDeleteProgram(renderer->program3d);
#if defined(TORIRS_ENABLE_LVGL_HUD)
    if( renderer->program2d )
        glDeleteProgram(renderer->program2d);
    if( renderer->hud_texture )
        glDeleteTextures(1, &renderer->hud_texture);
    if( renderer->hud_vbo )
        glDeleteBuffers(1, &renderer->hud_vbo);
    if( renderer->hud )
        LibToriHud_Free(renderer->hud);
    free(renderer->hud_upload_buf);
    renderer->hud_upload_buf = NULL;
    renderer->hud_upload_buf_size = 0;
#endif
    for( uint32_t i = 0u; i < TRSPK_VBO_GROUP_COUNT; ++i )
        webgl1_free_group_gl_resources(&renderer->groups[i]);
    if( renderer->ebo )
        glDeleteBuffers(1, &renderer->ebo);
    if( renderer->atlas_texture )
        glDeleteTextures(1, &renderer->atlas_texture);
    renderer->program3d = 0u;
    renderer->program2d = 0u;
    renderer->ebo = 0u;
    renderer->atlas_texture = 0u;
}

#if defined(TORIRS_ENABLE_LVGL_HUD)

struct WebGL1HudVertex
{
    float position[2];
    float texcoord[2];
    float color[4];
};

static void
webgl1_make_ortho2d(
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

static GLuint
webgl1_compile_shader(
    GLenum type,
    char const* source)
{
    GLuint shader = glCreateShader(type);
    if( shader == 0u )
        return 0u;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char buf[512];
        glGetShaderInfoLog(shader, (GLsizei)sizeof(buf), NULL, buf);
        fprintf(stderr, "WebGL1 HUD: shader compile failed: %s\n", buf);
        glDeleteShader(shader);
        return 0u;
    }
    return shader;
}

static bool
webgl1_init_hud_gl(struct LibToriPlatformSDL2_RendererWebGL1* renderer)
{
    renderer->hud = LibToriHud_New();
    if( !renderer->hud )
        return false;

    GLuint vs = webgl1_compile_shader(GL_VERTEX_SHADER, trspk_webgl1_hud_vertex_shader);
    GLuint fs = webgl1_compile_shader(GL_FRAGMENT_SHADER, trspk_webgl1_hud_fragment_shader);
    if( !vs || !fs )
    {
        if( vs )
            glDeleteShader(vs);
        if( fs )
            glDeleteShader(fs);
        return false;
    }

    renderer->program2d = glCreateProgram();
    if( renderer->program2d == 0u )
    {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return false;
    }

    glAttachShader(renderer->program2d, vs);
    glAttachShader(renderer->program2d, fs);
    glBindAttribLocation(renderer->program2d, 0, "a_position");
    glBindAttribLocation(renderer->program2d, 1, "a_texcoord");
    glBindAttribLocation(renderer->program2d, 2, "a_color");
    glLinkProgram(renderer->program2d);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint link_ok = 0;
    glGetProgramiv(renderer->program2d, GL_LINK_STATUS, &link_ok);
    if( !link_ok )
    {
        char buf[512];
        glGetProgramInfoLog(renderer->program2d, (GLsizei)sizeof(buf), NULL, buf);
        fprintf(stderr, "WebGL1 HUD: program link failed: %s\n", buf);
        return false;
    }

    renderer->u_hud_projection = glGetUniformLocation(renderer->program2d, "u_projection");
    renderer->u_hud_texture = glGetUniformLocation(renderer->program2d, "u_texture");

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
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenBuffers(1, &renderer->hud_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->hud_vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        6 * (GLsizeiptr)sizeof(struct WebGL1HudVertex),
        NULL,
        GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    renderer->hud_upload_buf_size =
        (size_t)LIBTORI_HUD_PANEL_W * (size_t)LIBTORI_HUD_PANEL_H * 4u;
    renderer->hud_upload_buf = (uint8_t*)malloc(renderer->hud_upload_buf_size);
    return renderer->hud_upload_buf != NULL;
}

static void
webgl1_upload_hud_texture_bgra(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    uint8_t const* src,
    int hud_w,
    int hud_h,
    int hud_pitch)
{
    if( !renderer->hud_upload_buf || hud_w <= 0 || hud_h <= 0 || !src )
        return;

    size_t const tight_row = (size_t)hud_w * 4u;
    size_t const needed = tight_row * (size_t)hud_h;
    if( needed > renderer->hud_upload_buf_size )
        return;

    uint8_t* dst = renderer->hud_upload_buf;
    for( int y = 0; y < hud_h; y++ )
    {
        uint8_t const* src_row = src + (size_t)y * (size_t)hud_pitch;
        uint8_t* dst_row = dst + (size_t)y * tight_row;
        for( int x = 0; x < hud_w; x++ )
        {
            uint8_t const b = src_row[x * 4 + 0];
            uint8_t const g = src_row[x * 4 + 1];
            uint8_t const r = src_row[x * 4 + 2];
            uint8_t const a = src_row[x * 4 + 3];
            dst_row[x * 4 + 0] = r;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = b;
            dst_row[x * 4 + 3] = a;
        }
    }

    glBindTexture(GL_TEXTURE_2D, renderer->hud_texture);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        hud_w,
        hud_h,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        dst);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void
webgl1_draw_hud_quad(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    float x0,
    float y0,
    float x1,
    float y1)
{
    float const white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    struct WebGL1HudVertex verts[6] = {
        { { x0, y0 }, { 0.0f, 0.0f }, { white[0], white[1], white[2], white[3] } },
        { { x1, y0 }, { 1.0f, 0.0f }, { white[0], white[1], white[2], white[3] } },
        { { x1, y1 }, { 1.0f, 1.0f }, { white[0], white[1], white[2], white[3] } },
        { { x0, y0 }, { 0.0f, 0.0f }, { white[0], white[1], white[2], white[3] } },
        { { x1, y1 }, { 1.0f, 1.0f }, { white[0], white[1], white[2], white[3] } },
        { { x0, y1 }, { 0.0f, 1.0f }, { white[0], white[1], white[2], white[3] } },
    };

    glBindBuffer(GL_ARRAY_BUFFER, renderer->hud_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(struct WebGL1HudVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        (GLsizei)sizeof(struct WebGL1HudVertex),
        (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2,
        4,
        GL_FLOAT,
        GL_FALSE,
        (GLsizei)sizeof(struct WebGL1HudVertex),
        (void*)(4 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void
webgl1_composite_hud(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    struct LibToriRS_Instance* instance)
{
    if( !renderer->hud || !renderer->hud_texture || !renderer->program2d )
        return;

    LibToriHud_Update(renderer->hud, instance);

    int hud_w = 0;
    int hud_h = 0;
    int hud_pitch = 0;
    uint8_t const* pixels =
        LibToriHud_PixelsBGRA(renderer->hud, &hud_w, &hud_h, &hud_pitch);
    if( !pixels || hud_w <= 0 || hud_h <= 0 )
        return;

    webgl1_upload_hud_texture_bgra(renderer, pixels, hud_w, hud_h, hud_pitch);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(renderer->program2d);
    glViewport(0, 0, renderer->width, renderer->height);
    webgl1_make_ortho2d(
        renderer->hud_proj,
        0.0f,
        (float)renderer->width,
        (float)renderer->height,
        0.0f);
    glUniformMatrix4fv(renderer->u_hud_projection, 1, GL_FALSE, renderer->hud_proj);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer->hud_texture);
    glUniform1i(renderer->u_hud_texture, 0);
    webgl1_draw_hud_quad(
        renderer,
        (float)LIBTORI_HUD_PANEL_X,
        (float)LIBTORI_HUD_PANEL_Y,
        (float)(LIBTORI_HUD_PANEL_X + hud_w),
        (float)(LIBTORI_HUD_PANEL_Y + hud_h));
}

#endif

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
upload_world_uniforms(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    const float* view,
    const float* proj)
{
    if( renderer->u_modelViewMatrix >= 0 )
        glUniformMatrix4fv(renderer->u_modelViewMatrix, 1, GL_FALSE, view);
    if( renderer->u_projectionMatrix >= 0 )
        glUniformMatrix4fv(renderer->u_projectionMatrix, 1, GL_FALSE, proj);
    if( renderer->u_clock >= 0 )
        glUniform1f(renderer->u_clock, (float)renderer->frame_clock);
}

static void
bind_vbo_attribs(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    GLuint page_buffer)
{
    const GLsizei stride = (GLsizei)sizeof(struct TRSPK_VertexWebGL1);
    const uintptr_t offset_position = offsetof(struct TRSPK_VertexWebGL1, position);
    const uintptr_t offset_color = offsetof(struct TRSPK_VertexWebGL1, color);
    const uintptr_t offset_texcoord = offsetof(struct TRSPK_VertexWebGL1, texcoord);
    const uintptr_t offset_tex_id = offsetof(struct TRSPK_VertexWebGL1, tex_id);
    const uintptr_t offset_uv_mode = offsetof(struct TRSPK_VertexWebGL1, uv_mode);
    glBindBuffer(GL_ARRAY_BUFFER, page_buffer);
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

static void
webgl1_ensure_page_buffers(
    struct WebGL1ModelGroup* g,
    uint32_t page_count)
{
    if( page_count <= g->page_buffer_count )
        return;

    GLuint* grown_bufs = (GLuint*)realloc(g->page_buffers, page_count * sizeof(GLuint));
    uint32_t* grown_caps = (uint32_t*)realloc(g->page_buffer_caps, page_count * sizeof(uint32_t));
    assert(grown_bufs != NULL && grown_caps != NULL);

    for( uint32_t i = g->page_buffer_count; i < page_count; ++i )
    {
        grown_bufs[i] = 0u;
        grown_caps[i] = 0u;
        glGenBuffers(1, &grown_bufs[i]);
    }

    g->page_buffers = grown_bufs;
    g->page_buffer_caps = grown_caps;
    g->page_buffer_count = page_count;
}

static void
webgl1_upload_group_pages(struct WebGL1ModelGroup* g)
{
    if( !g->chain )
        return;

    const uint32_t page_count = g->chain->page_count;
    if( page_count == 0u )
        return;

    webgl1_ensure_page_buffers(g, page_count);

    for( uint32_t i = 0u; i < page_count; ++i )
    {
        struct TRSPK_VBO* page_vbo = g->chain->pages[i];
        if( !page_vbo || page_vbo->vertex_count == 0u )
            continue;
        if( !g->reset_each_frame && !trspk_vbo_is_dirty(page_vbo) )
            continue;

        const uint32_t vert_count = page_vbo->vertex_count;
        const GLsizeiptr byte_size =
            (GLsizeiptr)(vert_count * sizeof(struct TRSPK_VertexWebGL1));

        glBindBuffer(GL_ARRAY_BUFFER, g->page_buffers[i]);
        if( vert_count > g->page_buffer_caps[i] )
        {
            glBufferData(GL_ARRAY_BUFFER, byte_size, NULL, GL_DYNAMIC_DRAW);
            g->page_buffer_caps[i] = vert_count;
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0, byte_size, page_vbo->vertices.as_webgl1);
        trspk_vbo_clear_dirty(page_vbo);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void
webgl1_reset_model_group(struct WebGL1ModelGroup* g)
{
    if( g->arena )
        trspk_modelarena_clear(g->arena);
    if( g->chain )
        trspk_vbochain16_reset(g->chain);
}

static void
webgl1_upload_atlas_texture(struct LibToriPlatformSDL2_RendererWebGL1* renderer)
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
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        (GLsizei)renderer->atlas.width,
        (GLsizei)renderer->atlas.height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        renderer->atlas.pixels);
    trspk_atlas_clear_dirty(&renderer->atlas);
}

static void
webgl1_write_vertex(
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
    trspk_vbo_write_vertex_webgl1(vbo, index, x, y, z, color, u, v, tex_id);
    vbo->vertices.as_webgl1[index].uv_mode = uv_mode;
}

static void
webgl1_ev_tex_load(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
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

static bool
webgl1_ensure_ibo_staging(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    uint32_t index_count)
{
    if( index_count <= renderer->ibo_staging_capacity )
        return true;

    uint32_t cap = renderer->ibo_staging_capacity ? renderer->ibo_staging_capacity
                                                  : TRSPK_WEBGL1_GPU_IBO_INIT;
    while( cap < index_count )
        cap *= 2u;

    uint16_t* grown = (uint16_t*)realloc(renderer->ibo_staging, cap * sizeof(uint16_t));
    if( !grown )
        return false;

    renderer->ibo_staging = grown;
    renderer->ibo_staging_capacity = cap;
    return true;
}

static bool
webgl1_ensure_draw_records(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    uint32_t record_count)
{
    if( record_count <= renderer->draw_record_capacity )
        return true;

    uint32_t cap = renderer->draw_record_capacity ? renderer->draw_record_capacity : 64u;
    while( cap < record_count )
        cap *= 2u;

    struct WebGL1DrawRecord* grown = (struct WebGL1DrawRecord*)realloc(
        renderer->draw_records, cap * sizeof(struct WebGL1DrawRecord));
    if( !grown )
        return false;

    renderer->draw_records = grown;
    renderer->draw_record_capacity = cap;
    return true;
}

static bool
webgl1_push_draw_record(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    uint32_t group,
    uint32_t page,
    uint32_t draw_offset,
    uint32_t count)
{
    if( !webgl1_ensure_draw_records(renderer, renderer->draw_record_count + 1u) )
        return false;

    struct WebGL1DrawRecord* rec = &renderer->draw_records[renderer->draw_record_count++];
    rec->group = group;
    rec->page = page;
    rec->draw_offset = draw_offset;
    rec->count = count;
    return true;
}

static bool
webgl1_ensure_gpu_ibo(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    uint32_t index_count)
{
    if( index_count == 0u )
        return true;

    if( renderer->gpu_ibo_capacity >= index_count )
        return true;

    uint32_t cap =
        renderer->gpu_ibo_capacity ? renderer->gpu_ibo_capacity : TRSPK_WEBGL1_GPU_IBO_INIT;
    while( cap < index_count )
        cap *= 2u;

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(cap * sizeof(uint16_t)), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    renderer->gpu_ibo_capacity = cap;
    return true;
}

static const struct TRSPK_ModelSlot*
webgl1_bake_into_arena(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    struct WebGL1ModelGroup* g,
    struct LibToriRS_Instance* instance,
    int element_id,
    int pose_id,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position,
    bool update_pose_table)
{
    struct ToriDraw_Model* model = get_model(model_handle);
    if( !model || model->face_count <= 0 || !g->arena || !g->chain )
        return NULL;

    struct ToriDraw_Scene* ctx = get_context(instance);
    const uint32_t vert_count = (uint32_t)model->face_count * 3u;
    const uint32_t tri_count = (uint32_t)model->face_count;

    uint32_t slot_index = TRSPK_MODELSLOT_NULL_IDX;

    if( update_pose_table )
    {
        const uint32_t existing_slot = trspk_modelarena_find(g->arena, element_id, pose_id);
        if( existing_slot != TRSPK_MODELSLOT_NULL_IDX )
            trspk_modelarena_unload(g->arena, existing_slot);
        slot_index = trspk_modelarena_load(g->arena, element_id, pose_id, vert_count);
    }
    else
    {
        slot_index = trspk_modelarena_load(g->arena, element_id, pose_id, vert_count);
    }

    const struct TRSPK_ModelSlot* model_slot = trspk_modelarena_get(g->arena, slot_index);
    if( !model_slot )
        return NULL;

    struct TRSPK_VBO* page_vbo = g->chain->pages[model_slot->page];
    const uint32_t base = model_slot->vertex_base;

    for( uint32_t face_index = 0; face_index < tri_count; face_index++ )
    {
        const uint32_t vi = base + face_index * 3u;
        struct TRSPK_ToriDrawBakeFaceVerts face;
        trspk_toridraw_bake_face(model, face_index, world_position, ctx, true, &face);

        webgl1_write_vertex(
            page_vbo,
            vi + 0u,
            face.wx_a,
            face.wy_a,
            face.wz_a,
            face.color_a,
            face.uv.u1,
            face.uv.v1,
            face.tex_id_encoded,
            face.uv_mode);
        webgl1_write_vertex(
            page_vbo,
            vi + 1u,
            face.wx_b,
            face.wy_b,
            face.wz_b,
            face.color_b,
            face.uv.u2,
            face.uv.v2,
            face.tex_id_encoded,
            face.uv_mode);
        webgl1_write_vertex(
            page_vbo,
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
    {
        trspk_pose_table_set(
            &renderer->poses,
            element_id,
            0,
            pose_id,
            TRSPK_WEBGL1_ENCODE_PAGE_LOCAL(model_slot->page, model_slot->vertex_base));
    }

    return model_slot;
}

static void
webgl1_ev_end_3d(struct LibToriPlatformSDL2_RendererWebGL1* renderer)
{
    if( !renderer->has_3d )
        goto done;

    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
        webgl1_upload_group_pages(&renderer->groups[gi]);

    if( !renderer->ibo_chain || !renderer->ibo_chain->head )
        goto done;

    uint32_t total_indices = 0u;
    for( struct TRSPK_IBOChainNode* node = renderer->ibo_chain->head; node != NULL;
         node = node->next )
        total_indices += node->ibo.index_count;

    if( total_indices == 0u )
        goto done;

    if( !webgl1_ensure_gpu_ibo(renderer, total_indices) )
        goto done;

    if( !webgl1_ensure_ibo_staging(renderer, total_indices) )
        goto done;

    renderer->draw_record_count = 0u;

    uint32_t cursor = 0u;
    for( struct TRSPK_IBOChainNode* node = renderer->ibo_chain->head; node != NULL;
         node = node->next )
    {
        const uint32_t count = node->ibo.index_count;
        if( count == 0u )
            continue;

        const uint32_t page = node->ibo.offset;
        const uint32_t group = node->group;
        if( group >= TRSPK_VBO_GROUP_COUNT )
            continue;

        struct WebGL1ModelGroup* mg = &renderer->groups[group];
        if( page >= mg->page_buffer_count || mg->page_buffers[page] == 0u )
            continue;

        memcpy(
            renderer->ibo_staging + cursor,
            node->ibo.indices.as_u16,
            count * sizeof(uint16_t));

        if( !webgl1_push_draw_record(renderer, group, page, cursor, count) )
            goto done;

        cursor += count;
    }

    const uint32_t used_indices = cursor;
    if( used_indices == 0u )
        goto done;

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        (GLsizeiptr)(renderer->gpu_ibo_capacity * sizeof(uint16_t)),
        NULL,
        GL_DYNAMIC_DRAW);
    glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER,
        0,
        (GLsizeiptr)(used_indices * sizeof(uint16_t)),
        renderer->ibo_staging);

    for( uint32_t i = 0u; i < renderer->draw_record_count; ++i )
    {
        const struct WebGL1DrawRecord* rec = &renderer->draw_records[i];
        struct WebGL1ModelGroup* mg = &renderer->groups[rec->group];
        if( rec->page >= mg->page_buffer_count )
            continue;
        bind_vbo_attribs(renderer, mg->page_buffers[rec->page]);
        glDrawElements(
            GL_TRIANGLES,
            (GLsizei)rec->count,
            GL_UNSIGNED_SHORT,
            (const void*)(uintptr_t)(rec->draw_offset * sizeof(uint16_t)));
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

done:
    renderer->has_3d = false;
    renderer->in3d = false;
    if( renderer->ibo_chain )
        trspk_ibochain_reset(renderer->ibo_chain);
}

static void
webgl1_ev_anim_load(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
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
        webgl1_bake_into_arena(
            renderer,
            &renderer->groups[TRSPK_VBO_GROUP_STATIC],
            instance,
            element_id,
            frame,
            baked_handle,
            world_position,
            true);
        ToriDraw_ModelFree(baked);
    }
}

static void
webgl1_handle_render_command(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    switch( command->kind )
    {
    case TORIRSRC_BEGIN_3D:
        if( !renderer->in3d )
        {
            glUseProgram(renderer->program3d);
            glUniform1i(renderer->s_atlas, 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, renderer->atlas_texture);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthFunc(GL_ALWAYS);
            glDepthMask(GL_FALSE);
        }

        renderer->cur_3d = command->u.begin_3d;
        renderer->has_3d = true;
        renderer->in3d = true;
        trspk_compute_pass_matrices(
            renderer->view, renderer->proj, &renderer->cur_3d, renderer->width, renderer->height);
        upload_world_uniforms(renderer, renderer->view, renderer->proj);

        if( trspk_atlas_is_dirty(&renderer->atlas) )
            webgl1_upload_atlas_texture(renderer);

        for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
        {
            if( renderer->groups[gi].reset_each_frame )
                webgl1_reset_model_group(&renderer->groups[gi]);
        }
        break;
    case TORIRSRC_TEX_LOAD:
        webgl1_ev_tex_load(renderer, instance, command);
        break;
    case TORIRSRC_ANIM_LOAD:
        webgl1_ev_anim_load(renderer, instance, command);
        break;
    case TORIRSRC_ANIM_UNLOAD:
        break;
    case TORIRSRC_END_3D:
        webgl1_ev_end_3d(renderer);
        break;
    case TORIRSRC_MODEL_LOAD:
        webgl1_bake_into_arena(
            renderer,
            &renderer->groups[TRSPK_VBO_GROUP_STATIC],
            instance,
            command->u.model_load.element_id,
            0,
            command->u.model_load.model,
            &command->u.model_load.world_position,
            true);
        break;
    case TORIRSRC_MODEL_UNLOAD:
        trspk_modelarena_unload_element(
            renderer->groups[TRSPK_VBO_GROUP_STATIC].arena, command->u.model_load.element_id);
        trspk_pose_table_remove_element(&renderer->poses, command->u.model_load.element_id);
        break;
    case TORIRSRC_BATCH3D_BEGIN:
        break;
    case TORIRSRC_BATCH3D_MODEL_ADD:
        webgl1_bake_into_arena(
            renderer,
            &renderer->groups[TRSPK_VBO_GROUP_STATIC],
            instance,
            command->u.batch.element_id,
            command->u.batch.pose_id,
            command->u.batch.model,
            &command->u.batch.world_position,
            true);
        break;
    case TORIRSRC_BATCH3D_ANIM_ADD:
        webgl1_bake_into_arena(
            renderer,
            &renderer->groups[TRSPK_VBO_GROUP_STATIC],
            instance,
            command->u.batch.element_id,
            command->u.batch.pose_id,
            command->u.batch.model,
            &command->u.batch.world_position,
            true);
        break;
    case TORIRSRC_BATCH3D_END:
        break;
    case TORIRSRC_BATCH3D_CLEAR:
        trspk_pose_table_clear(&renderer->poses);
        webgl1_reset_model_group(&renderer->groups[TRSPK_VBO_GROUP_STATIC]);
        break;
    case TORIRSRC_DRAW_MODEL:
    {
        if( !renderer->has_3d || !renderer->ibo_chain )
            break;

        struct ToriDraw_Scene* ctx = get_context(instance);
        if( !ctx )
            break;

        const int pose_id = command->u.model.anim_frame;
        uint32_t group = TRSPK_VBO_GROUP_STATIC;
        uint32_t page = 0u;
        uint32_t local_base = 0u;

        if( command->u.model.dynamic )
        {
            const struct TRSPK_ModelSlot* slot = webgl1_bake_into_arena(
                renderer,
                &renderer->groups[TRSPK_VBO_GROUP_DYNAMIC],
                instance,
                command->u.model.element_id,
                pose_id,
                command->u.model.model,
                &command->u.model.world_position,
                false);
            if( !slot )
                break;

            group = TRSPK_VBO_GROUP_DYNAMIC;
            page = slot->page;
            local_base = slot->vertex_base;
        }
        else
        {
            uint32_t encoded_base = 0u;
            if( !trspk_pose_table_get(
                    &renderer->poses, command->u.model.element_id, 0, pose_id, &encoded_base) )
                break;

            page = TRSPK_WEBGL1_BASE_PAGE(encoded_base);
            local_base = TRSPK_WEBGL1_BASE_LOCAL(encoded_base);
        }

        const int face_count = ToriDraw_FaceOrderCount(ctx);
        if( face_count <= 0 )
            break;

        int* face_order = ToriDraw_FaceOrder(ctx);
        for( int i = 0; i < face_count; i++ )
        {
            const uint32_t face = (uint32_t)face_order[i];
            const uint16_t b = (uint16_t)(local_base + face * 3u);
            const uint16_t idx[3] = { b, (uint16_t)(b + 1u), (uint16_t)(b + 2u) };
            trspk_ibochain_push16(renderer->ibo_chain, group, page, idx, 3u);
        }
        break;
    }
    default:
        break;
    }
}

struct LibToriPlatformSDL2_RendererWebGL1*
LibToriPlatformSDL2_RendererWebGL1_New(
    int width,
    int height)
{
    struct LibToriPlatformSDL2_RendererWebGL1* renderer =
        malloc(sizeof(struct LibToriPlatformSDL2_RendererWebGL1));
    memset(renderer, 0, sizeof(struct LibToriPlatformSDL2_RendererWebGL1));

    renderer->width = width;
    renderer->height = height;

    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        struct WebGL1ModelGroup* g = &renderer->groups[gi];
        g->chain = trspk_vbochain16_create(TRSPK_VERTEX_FORMAT_WEBGL1);
        if( !g->chain )
            goto fail;

        g->arena = trspk_modelarena_create_chain16(g->chain, NULL, 64u);
        if( !g->arena )
            goto fail;

        g->reset_each_frame = (gi == TRSPK_VBO_GROUP_DYNAMIC);
    }

    renderer->ibo_chain = trspk_ibochain_create(TRSPK_INDEX_FORMAT_U16);
    if( !renderer->ibo_chain )
        goto fail;

    trspk_pose_table_init(&renderer->poses);

    return renderer;

fail:
    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        if( renderer->groups[gi].arena )
            trspk_modelarena_free(renderer->groups[gi].arena);
        if( renderer->groups[gi].chain )
            trspk_vbochain16_free(renderer->groups[gi].chain);
    }
    if( renderer->ibo_chain )
        trspk_ibochain_free(renderer->ibo_chain);
    free(renderer);
    return NULL;
}

void
LibToriPlatformSDL2_RendererWebGL1_Free(struct LibToriPlatformSDL2_RendererWebGL1* renderer)
{
    if( !renderer )
        return;
    if( renderer->window && renderer->gl_context )
        SDL_GL_MakeCurrent(renderer->window, renderer->gl_context);
    webgl1_destroy_gl_resources(renderer);
    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        if( renderer->groups[gi].arena )
            trspk_modelarena_free(renderer->groups[gi].arena);
        if( renderer->groups[gi].chain )
            trspk_vbochain16_free(renderer->groups[gi].chain);
    }
    trspk_pose_table_free(&renderer->poses);
    if( renderer->ibo_chain )
        trspk_ibochain_free(renderer->ibo_chain);
    trspk_atlas_free(&renderer->atlas);
    free(renderer->ibo_staging);
    free(renderer->draw_records);
    if( renderer->gl_context )
    {
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = NULL;
    }
    free(renderer);
}

bool
LibToriPlatformSDL2_RendererWebGL1_Init(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    SDL_Window* window)
{
    renderer->gl_context = SDL_GL_CreateContext(window);
    if( !renderer->gl_context )
    {
        fprintf(stderr, "WebGL1: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }

    if( SDL_GL_MakeCurrent(window, renderer->gl_context) != 0 )
    {
        fprintf(stderr, "WebGL1: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = NULL;
        return false;
    }

    SDL_GL_SetSwapInterval(0);
    SDL_GL_GetDrawableSize(window, &renderer->width, &renderer->height);

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    if( vertex_shader == 0u || fragment_shader == 0u )
    {
        fprintf(stderr, "WebGL1: glCreateShader failed\n");
        goto fail_gl;
    }
    glShaderSource(vertex_shader, 1, &trspk_webgl1_vertex_shader, NULL);
    glShaderSource(fragment_shader, 1, &trspk_webgl1_fragment_shader, NULL);
    glCompileShader(vertex_shader);
    glCompileShader(fragment_shader);
    GLint ok = 0;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char buf[512];
        glGetShaderInfoLog(vertex_shader, (GLsizei)sizeof(buf), NULL, buf);
        fprintf(stderr, "WebGL1: vertex shader compile failed: %s\n", buf);
        goto fail_gl;
    }

    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char buf[512];
        glGetShaderInfoLog(fragment_shader, (GLsizei)sizeof(buf), NULL, buf);
        fprintf(stderr, "WebGL1: fragment shader compile failed: %s\n", buf);
        goto fail_gl;
    }

    renderer->program3d = glCreateProgram();
    if( renderer->program3d == 0u )
    {
        fprintf(stderr, "WebGL1: glCreateProgram failed\n");
        goto fail_gl;
    }
    glAttachShader(renderer->program3d, vertex_shader);
    glAttachShader(renderer->program3d, fragment_shader);
    glBindAttribLocation(renderer->program3d, 0u, "a_position");
    glBindAttribLocation(renderer->program3d, 1u, "a_color");
    glBindAttribLocation(renderer->program3d, 2u, "a_texcoord");
    glBindAttribLocation(renderer->program3d, 3u, "a_tex_id");
    glBindAttribLocation(renderer->program3d, 4u, "a_uv_mode");
    glLinkProgram(renderer->program3d);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    vertex_shader = 0u;
    fragment_shader = 0u;

    GLint link_ok = 0;
    glGetProgramiv(renderer->program3d, GL_LINK_STATUS, &link_ok);
    if( !link_ok )
    {
        char buf[512];
        glGetProgramInfoLog(renderer->program3d, (GLsizei)sizeof(buf), NULL, buf);
        fprintf(stderr, "WebGL1: glLinkProgram failed: %s\n", buf);
        goto fail_gl;
    }

    renderer->a_position = glGetAttribLocation(renderer->program3d, "a_position");
    renderer->a_color = glGetAttribLocation(renderer->program3d, "a_color");
    renderer->a_texcoord = glGetAttribLocation(renderer->program3d, "a_texcoord");
    renderer->a_tex_id = glGetAttribLocation(renderer->program3d, "a_tex_id");
    renderer->a_uv_mode = glGetAttribLocation(renderer->program3d, "a_uv_mode");
    renderer->u_modelViewMatrix = glGetUniformLocation(renderer->program3d, "u_modelViewMatrix");
    renderer->u_projectionMatrix = glGetUniformLocation(renderer->program3d, "u_projectionMatrix");
    renderer->u_clock = glGetUniformLocation(renderer->program3d, "u_clock");
    renderer->s_atlas = glGetUniformLocation(renderer->program3d, "s_atlas");

    glGenBuffers(1, &renderer->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        TRSPK_WEBGL1_GPU_IBO_INIT * sizeof(uint16_t),
        NULL,
        GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    renderer->gpu_ibo_capacity = TRSPK_WEBGL1_GPU_IBO_INIT;

    renderer->window = window;

    if( !trspk_atlas_init_grid(
            &renderer->atlas,
            TRSPK_WEBGL1_ATLAS_DIM,
            TRSPK_WEBGL1_ATLAS_DIM,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE,
            4u) )
    {
        fprintf(stderr, "WebGL1: trspk_atlas_init_grid failed\n");
        goto fail_gl;
    }

    glGenTextures(1, &renderer->atlas_texture);
    glBindTexture(GL_TEXTURE_2D, renderer->atlas_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

#if defined(TORIRS_ENABLE_LVGL_HUD)
    if( !webgl1_init_hud_gl(renderer) )
        fprintf(stderr, "WebGL1: HUD init failed (continuing without HUD)\n");
#endif

    return true;

fail_gl:
    if( vertex_shader )
        glDeleteShader(vertex_shader);
    if( fragment_shader )
        glDeleteShader(fragment_shader);
    webgl1_destroy_gl_resources(renderer);
    if( renderer->gl_context )
    {
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = NULL;
    }
    return false;
}

void
LibToriPlatformSDL2_RendererWebGL1_Render(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    struct LibToriRS_Instance* instance)
{
    if( SDL_GL_MakeCurrent(renderer->window, renderer->gl_context) != 0 )
        return;

    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_GL_GetDrawableSize(renderer->window, &drawable_w, &drawable_h);
    renderer->width = drawable_w;
    renderer->height = drawable_h;

    glViewport(0, 0, drawable_w, drawable_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    renderer->in3d = false;
    renderer->has_3d = false;

    LibToriRS_FrameBegin(instance);
    renderer->frame_clock = (double)LibToriRS_GetAnimationClock(instance);
    struct LibToriRS_RenderCommand command;
    while( LibToriRS_FrameNextCommand(instance, &command) )
        webgl1_handle_render_command(renderer, instance, &command);
    LibToriRS_FrameEnd(instance);

#if defined(TORIRS_ENABLE_LVGL_HUD)
    webgl1_composite_hud(renderer, instance);
#endif

    SDL_GL_SwapWindow(renderer->window);
}
