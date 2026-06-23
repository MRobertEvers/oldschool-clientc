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
#include "platformkit/opengl3/opengl3_sdlgl.h"
#include "platformkit/opengl3/trspk_opengl3.h"
#include "render/libtorirs_render.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"
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

struct LibToriPlatformSDL2_RendererGL3
{
    SDL_Window* window;
    SDL_GLContext gl_context;
    int width;
    int height;

    struct TRSPK_Atlas atlas;
    GLuint atlas_texture;

    struct TRSPK_VBO* vbo_static_cpu;
    uint32_t gpu_vbo_capacity;
    uint32_t gpu_ibo_capacity;

    struct TRSPK_ModelArena* model_arena;
    struct TRSPK_Triangles triangles;
    struct TRSPK_PoseTable poses;

    struct TRSPK_IBOChain* ibo_chain;
    struct TRSPK_IBO* ibo_staging;
    struct TRSPK_DrawRangeList* draw_ranges;

    GLuint program3d_vao;
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
    GLuint vbo;

    float view[16];
    float proj[16];
    struct LibToriRS_RenderCommand_Begin3D cur_3d;
    bool has_3d;
    bool in3d;
    double frame_clock;
};

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static void
bind_vbo_attribs(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    const GLsizei stride = (GLsizei)sizeof(struct TRSPK_VertexOpenGl3);
    const uintptr_t offset_position = offsetof(struct TRSPK_VertexOpenGl3, position);
    const uintptr_t offset_color = offsetof(struct TRSPK_VertexOpenGl3, color);
    const uintptr_t offset_texcoord = offsetof(struct TRSPK_VertexOpenGl3, texcoord);
    const uintptr_t offset_tex_id = offsetof(struct TRSPK_VertexOpenGl3, tex_id);
    const uintptr_t offset_uv_mode = offsetof(struct TRSPK_VertexOpenGl3, uv_mode);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
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
    if( renderer->vbo )
        glDeleteBuffers(1, &renderer->vbo);
    if( renderer->program3d_vao )
        glDeleteVertexArrays(1, &renderer->program3d_vao);
    if( renderer->atlas_texture )
        glDeleteTextures(1, &renderer->atlas_texture);
    renderer->program3d = 0u;
    renderer->ubo = 0u;
    renderer->ebo = 0u;
    renderer->vbo = 0u;
    renderer->program3d_vao = 0u;
    renderer->atlas_texture = 0u;
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
    renderer->gpu_vbo_capacity = 0u;
    renderer->gpu_ibo_capacity = 0u;
}

static bool
gl3_upload_vbo_if_dirty(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    if( !trspk_vbo_is_dirty(renderer->vbo_static_cpu) || !renderer->vbo_static_cpu )
        return true;

    const uint32_t vert_count = renderer->vbo_static_cpu->vertex_count;
    if( vert_count == 0u )
    {
        trspk_vbo_clear_dirty(renderer->vbo_static_cpu);
        return true;
    }

    const GLsizeiptr byte_size = (GLsizeiptr)(vert_count * sizeof(struct TRSPK_VertexOpenGl3));
    if( vert_count > renderer->gpu_vbo_capacity )
    {
        uint32_t cap =
            renderer->gpu_vbo_capacity ? renderer->gpu_vbo_capacity : TRSPK_GL3_GPU_VBO_INIT;
        while( cap < vert_count )
            cap *= 2u;
        renderer->gpu_vbo_capacity = cap;

        glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
        glBufferData(
            GL_ARRAY_BUFFER,
            (GLsizeiptr)(cap * sizeof(struct TRSPK_VertexOpenGl3)),
            NULL,
            GL_STATIC_DRAW);
    }

    glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, byte_size, renderer->vbo_static_cpu->vertices.as_opengl3);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    trspk_vbo_clear_dirty(renderer->vbo_static_cpu);
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

    if( trspk_atlas_is_dirty(&renderer->atlas) )
        gl3_upload_atlas_texture(renderer);
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
        glViewport(0, 0, vp_w, vp_h);
    }

    compute_pass_matrices(
        renderer->view, renderer->proj, &renderer->cur_3d, renderer->width, renderer->height);
    upload_world_ubo(renderer, renderer->view, renderer->proj);

    if( trspk_atlas_is_dirty(&renderer->atlas) )
        gl3_upload_atlas_texture(renderer);
}

static void
gl3_ev_batch3d_clear(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    (void)command;

    if( !renderer->model_arena )
        return;

    trspk_pose_table_clear(&renderer->poses);
    trspk_modelarena_clear(renderer->model_arena);
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
    trspk_modelarena_unload_element(renderer->model_arena, element_id);
    trspk_pose_table_remove_element(&renderer->poses, element_id);
}

/* Stride used to encode (anim_index, frame) into the model arena's flat pose_id.
   Must exceed the maximum number of frames any animation can have. */
#define GL3_POSE_ARENA_TRACK_STRIDE 4096

static void
gl3_bake_into_arena(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position)
{
    struct ToriDraw_Model* model = get_model(model_handle);
    if( !model || model->face_count <= 0 )
        return;

    struct ToriDraw_Scene* ctx = get_context(instance);
    const uint32_t vert_count = (uint32_t)model->face_count * 3u;
    const uint32_t tri_count = (uint32_t)model->face_count;

    /* Encode (anim_index, pose_id/frame) into a flat key for the model arena. */
    const int arena_pose_id = anim_index * GL3_POSE_ARENA_TRACK_STRIDE + pose_id;

    const uint32_t existing_slot =
        trspk_modelarena_find(renderer->model_arena, element_id, arena_pose_id);
    if( existing_slot != TRSPK_MODELSLOT_NULL_IDX )
        trspk_modelarena_unload(renderer->model_arena, existing_slot);

    const uint32_t slot_index =
        trspk_modelarena_load(renderer->model_arena, element_id, arena_pose_id, vert_count);
    const struct TRSPK_ModelSlot* model_slot =
        trspk_modelarena_get(renderer->model_arena, slot_index);
    assert(model_slot != NULL);

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

        trspk_triangles_set(&renderer->triangles, (base / 3u) + face_index, TRSPK_TRIANGLES_ATLAS);

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
            renderer->vbo_static_cpu,
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
            renderer->vbo_static_cpu,
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
            renderer->vbo_static_cpu,
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

    trspk_pose_table_set(&renderer->poses, element_id, anim_index, pose_id, base);
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
            renderer, instance, element_id, 0, frame, baked_handle, world_position);

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
        instance,
        command->u.model_load.element_id,
        0,
        0,
        command->u.model_load.model,
        &command->u.model_load.world_position);
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
        instance,
        command->u.batch.element_id,
        0,
        command->u.batch.pose_id,
        command->u.batch.model,
        &command->u.batch.world_position);
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
        instance,
        command->u.batch.element_id,
        command->u.batch.anim_index,
        command->u.batch.pose_id,
        command->u.batch.model,
        &command->u.batch.world_position);
}

static void
gl3_ev_batch3d_end(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    (void)command;

    if( renderer->vbo_static_cpu )
        trspk_vbo_set_dirty(renderer->vbo_static_cpu);
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
    uint32_t vertex_base = 0u;
    if( !trspk_pose_table_get(
            &renderer->poses, command->u.model.element_id, anim_index, pose_id, &vertex_base) )
        return;

    const int face_count = ToriDraw_FaceOrderCount(ctx);
    if( face_count <= 0 )
        return;

    int* face_order = ToriDraw_FaceOrder(ctx);
    for( int i = 0; i < face_count; i++ )
    {
        const uint32_t face = (uint32_t)face_order[i];
        const uint32_t b = vertex_base + face * 3u;
        const uint32_t idx[3] = { b, b + 1u, b + 2u };
        trspk_ibochain_push32(renderer->ibo_chain, 0u, idx, 3u);
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

    if( !gl3_upload_vbo_if_dirty(renderer) )
        goto done;

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

    const uint32_t built = trspk_drawrangeex_build32(
        renderer->draw_ranges, &renderer->triangles, renderer->ibo_chain, staging);

    assert(built == total_indices);
    (void)built;

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
    glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)(total_indices * sizeof(uint32_t)), staging);

    glBindVertexArray(renderer->program3d_vao);

    const struct TRSPK_DrawRange* range = trspk_drawrangelist_head(renderer->draw_ranges);
    while( range )
    {
        const uint32_t index_count = range->end - range->start;
        const uint32_t prim_count = index_count / 3u;

        if( prim_count > 0u )
        {
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
        break;

    case TORIRSRC_END_2D:
        break;

    case TORIRSRC_CLEAR_RECT:
        break;

    case TORIRSRC_SPRITE_LOAD:
        break;

    case TORIRSRC_SPRITE:
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

    renderer->vbo_static_cpu = trspk_vbo_create(0, TRSPK_VERTEX_FORMAT_OPENGL3);
    renderer->ibo_chain = trspk_ibochain_create(TRSPK_INDEX_FORMAT_U32);
    renderer->ibo_staging = trspk_ibo_create(TRSPK_GL3_GPU_IBO_INIT, TRSPK_INDEX_FORMAT_U32);
    renderer->draw_ranges = trspk_drawrangelist_create(TRSPK_GL3_DRAWRANGE_CAP);

    if( !renderer->vbo_static_cpu || !renderer->ibo_chain || !renderer->ibo_staging ||
        !renderer->draw_ranges )
        goto fail;

    renderer->model_arena = trspk_modelarena_create(
        renderer->vbo_static_cpu, &renderer->triangles, TRSPK_GL3_VBO_PAGE, 64u);
    if( !renderer->model_arena )
        goto fail;

    trspk_pose_table_init(&renderer->poses);

    if( !trspk_atlas_init_grid(
            &renderer->atlas,
            TRSPK_GL3_ATLAS_DIM,
            TRSPK_GL3_ATLAS_DIM,
            TRSPK_GL3_ATLAS_TILE,
            TRSPK_GL3_ATLAS_TILE,
            4u) )
        goto fail;

    trspk_vbo_set_dirty(renderer->vbo_static_cpu);
    return renderer;

fail:
    if( renderer->vbo_static_cpu )
        trspk_vbo_free(renderer->vbo_static_cpu);
    if( renderer->ibo_chain )
        trspk_ibochain_free(renderer->ibo_chain);
    if( renderer->ibo_staging )
        trspk_ibo_free(renderer->ibo_staging);
    if( renderer->draw_ranges )
        trspk_drawrangelist_free(renderer->draw_ranges);
    if( renderer->model_arena )
        trspk_modelarena_free(renderer->model_arena);
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

    trspk_triangles_free(&renderer->triangles);
    trspk_pose_table_free(&renderer->poses);

    if( renderer->model_arena )
    {
        trspk_modelarena_free(renderer->model_arena);
        renderer->model_arena = NULL;
    }

    if( renderer->vbo_static_cpu )
    {
        trspk_vbo_free(renderer->vbo_static_cpu);
        renderer->vbo_static_cpu = NULL;
    }

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
    glGenBuffers(1, &renderer->vbo);

    glGenVertexArrays(1, &renderer->program3d_vao);
    glBindVertexArray(renderer->program3d_vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        TRSPK_GL3_GPU_VBO_INIT * sizeof(struct TRSPK_VertexOpenGl3),
        NULL,
        GL_STATIC_DRAW);
    bind_vbo_attribs(renderer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER, TRSPK_GL3_GPU_IBO_INIT * sizeof(uint32_t), NULL, GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    renderer->gpu_vbo_capacity = TRSPK_GL3_GPU_VBO_INIT;
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
    renderer->width = drawable_w;
    renderer->height = drawable_h;

    glViewport(0, 0, drawable_w, drawable_h);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    renderer->in3d = false;
    renderer->has_3d = false;
    renderer->frame_clock += 1.0 / 50.0;

    LibToriRS_FrameBegin(instance);
    struct LibToriRS_RenderCommand command;
    while( LibToriRS_FrameNextCommand(instance, &command) )
        handle_render_command(renderer, instance, &command);
    LibToriRS_FrameEnd(instance);

    SDL_GL_SwapWindow(renderer->window);
}
