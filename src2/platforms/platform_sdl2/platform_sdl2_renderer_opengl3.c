#include "platform_sdl2_renderer_opengl3.h"

#include "../trspk_toridraw.h"
#include "graphics/uv_pnm.h"
#include "libtorirs.h"
#include "platformkit/opengl3/opengl3_sdlgl.h"
#include "platformkit/opengl3/trspk_opengl3.h"
#include "render/libtorirs_render.h"
#include "toridraw/toridraw.h"

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

#define TRSPK_EBO_SIZE 4096

struct LibToriPlatformSDL2_RendererGL3
{
    SDL_Window* window;
    SDL_GLContext gl_context;
    int width;
    int height;

    struct TRSPK_VBO* vbo_cpu;
    uint16_t ibo_16[TRSPK_EBO_SIZE];
    uint16_t ibo_16_size;

    GLuint atlas_texture;
    GLuint program3d_vao;
    GLuint program3d;
    GLint a_position;
    GLint a_color;
    GLint a_texcoord;
    GLint a_tex_id;
    GLint a_uv_mode;
    GLint s_atlas;

    GLuint program2d;

    // 3D Matrices etc.
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

struct LibToriPlatformSDL2_RendererGL3*
LibToriPlatformSDL2_RendererGL3_New(
    int width,
    int height)
{
    struct LibToriPlatformSDL2_RendererGL3* renderer =
        malloc(sizeof(struct LibToriPlatformSDL2_RendererGL3));
    assert(renderer != NULL);
    memset(renderer, 0, sizeof(struct LibToriPlatformSDL2_RendererGL3));

    renderer->width = width;
    renderer->height = height;

    renderer->vbo_cpu = trspk_vbo_create(0, TRSPK_VERTEX_FORMAT_OPENGL3);

    return renderer;
}

void
LibToriPlatformSDL2_RendererGL3_Free(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    (void)renderer;
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

    bool ok = trspk_sdlgl_init();
    if( !ok )
    {
        fprintf(stderr, "OpenGL3: trspk_sdlgl_init failed\n");
        assert(false);
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
    {
        fprintf(stderr, "OpenGL3: glCreateProgram failed\n");
        assert(false);
        goto fail_gl;
    }
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
    if( !gl3_check_error("uniform block binding") )
        goto fail_gl;

    glGenBuffers(1, &renderer->ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, renderer->ubo);
    glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)sizeof(TRSPK_UboWorld), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    if( !gl3_check_error("ubo") )
        goto fail_gl;

    renderer->a_position = glGetAttribLocation(renderer->program3d, "a_position");
    renderer->a_color = glGetAttribLocation(renderer->program3d, "a_color");
    renderer->a_texcoord = glGetAttribLocation(renderer->program3d, "a_texcoord");
    renderer->a_tex_id = glGetAttribLocation(renderer->program3d, "a_tex_id");
    renderer->a_uv_mode = glGetAttribLocation(renderer->program3d, "a_uv_mode");
    renderer->s_atlas = glGetUniformLocation(renderer->program3d, "s_atlas");

    glGenBuffers(1, &renderer->ebo);
    glGenBuffers(1, &renderer->vbo);
    if( !gl3_check_error("gen mesh buffers") )
        goto fail_gl;

    glGenVertexArrays(1, &renderer->program3d_vao);
    glBindVertexArray(renderer->program3d_vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
    glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(struct TRSPK_VertexOpenGl3), NULL, GL_DYNAMIC_DRAW);
    bind_vbo_attribs(renderer);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, TRSPK_EBO_SIZE * sizeof(uint16_t), NULL, GL_DYNAMIC_DRAW);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if( !gl3_check_error("vao setup") )
        goto fail_gl;

    renderer->window = window;

    glGenTextures(1, &renderer->atlas_texture);
    glBindTexture(GL_TEXTURE_2D, renderer->atlas_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if( !gl3_check_error("atlas texture") )
        goto fail_gl;

    return true;

fail_gl:
    if( vertexShader )
        glDeleteShader(vertexShader);
    if( fragmentShader )
        glDeleteShader(fragmentShader);
    gl3_destroy_gl_resources(renderer);
    SDL_GL_DeleteContext(renderer->gl_context);
    renderer->gl_context = NULL;
    assert(false && "OpenGL3: initialization failed");
    return false;
}

static struct ToriDraw_Model*
get_model(struct ToriDraw_ModelHandle model_handle)
{
    return model_handle.u.model.model;
}

static void
uv_pnm(
    struct UVFaceCoords* uv_pnm,
    struct ToriDraw_Model* model,
    uint32_t face_index)
{
    uint32_t face_a = model->face_indices_a[face_index];
    uint32_t face_b = model->face_indices_b[face_index];
    uint32_t face_c = model->face_indices_c[face_index];

    uint32_t texture_face = face_index;
    uint32_t p_vertex = face_a;
    uint32_t m_vertex = face_b;
    uint32_t n_vertex = face_c;

    if( model->face_texture_coords && model->face_texture_coords[face_index] != -1 )
    {
        assert(model->textured_p_coordinate != NULL);
        assert(model->textured_m_coordinate != NULL);
        assert(model->textured_n_coordinate != NULL);

        texture_face = model->face_texture_coords[face_index];

        p_vertex = model->textured_p_coordinate[texture_face];
        m_vertex = model->textured_m_coordinate[texture_face];
        n_vertex = model->textured_n_coordinate[texture_face];
    }
    else
    {
        texture_face = face_index;
        p_vertex = model->face_indices_a[texture_face];
        m_vertex = model->face_indices_b[texture_face];
        n_vertex = model->face_indices_c[texture_face];
    }

    uv_pnm_compute(
        uv_pnm,
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
    uint32_t rgb = toridraw_hsl16_to_rgb(hsl16);
    float r = (float)((rgb >> 16) & 0xFFu) / 255.0f;
    float g = (float)((rgb >> 8) & 0xFFu) / 255.0f;
    float b = (float)(rgb & 0xFFu) / 255.0f;
    float a = (float)alpha / 255.0f;

    rgba[0] = r;
    rgba[1] = g;
    rgba[2] = b;
    rgba[3] = a;
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
        toridraw_angle_to_radians(cam->pitch),
        toridraw_angle_to_radians(cam->yaw));
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

struct ToriDraw_Context*
get_context(struct LibToriRS_Instance* instance)
{
    struct ToriDraw_Context* ctx = NULL;
    ctx = LibToriRS_GetCurrentToriDrawContext(instance);
    return ctx;
}

static void
handle_render_command(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)renderer;
    (void)instance;
    (void)command;

    struct ToriDraw_Model* model = NULL;

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
        compute_pass_matrices(
            renderer->view, renderer->proj, &renderer->cur_3d, renderer->width, renderer->height);
        upload_world_ubo(renderer, renderer->view, renderer->proj);
        break;
    case TORIRSRC_END_3D:
        renderer->has_3d = false;
        renderer->in3d = false;
        break;
    case TORIRSRC_BEGIN_2D:
        break;
    case TORIRSRC_END_2D:
        break;
    case TORIRSRC_CLEAR_RECT:
        break;
    case TORIRSRC_MODEL_LOAD:
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
        model = get_model(command->u.model_load.model);
        if( model )
        {
            uint32_t gpu_vertex_count = model->face_count * 3;
            trspk_vbo_ensure_capacity(renderer->vbo_cpu, gpu_vertex_count);
            uint32_t face_index = 0;
            for( uint32_t i = 0; i < gpu_vertex_count; i += 3, face_index++ )
            {
                uint32_t face_a = model->face_indices_a[face_index];
                uint32_t face_b = model->face_indices_b[face_index];
                uint32_t face_c = model->face_indices_c[face_index];
                uint16_t color_a_hsl16 = model->face_colors_a[face_index];
                uint16_t color_b_hsl16 = model->face_colors_b[face_index];
                uint16_t color_c_hsl16 = model->face_colors_c[face_index];
                uint8_t alpha = model->face_alphas ? model->face_alphas[face_index] : 0xFFu;
                // alpha = 0xFFu - alpha;
                float color_a[4];
                float color_b[4];
                float color_c[4];
                hsl16_to_rgba(color_a_hsl16, alpha, color_a);
                hsl16_to_rgba(color_b_hsl16, alpha, color_b);
                hsl16_to_rgba(color_c_hsl16, alpha, color_c);
                struct UVFaceCoords uv_coords;
                uv_pnm(&uv_coords, model, (uint32_t)face_index);
                float u_a = uv_coords.u1;
                float v_a = uv_coords.v1;
                float u_b = uv_coords.u2;
                float v_b = uv_coords.v2;
                float u_c = uv_coords.u3;
                float v_c = uv_coords.v3;

                trspk_vbo_write_vertex_opengl3(
                    renderer->vbo_cpu,
                    i + 0,
                    model->vertices_x[face_a],
                    model->vertices_y[face_a],
                    model->vertices_z[face_a],
                    color_a,
                    u_a,
                    v_a,
                    -1.0f);
                trspk_vbo_write_vertex_opengl3(
                    renderer->vbo_cpu,
                    i + 1,
                    model->vertices_x[face_b],
                    model->vertices_y[face_b],
                    model->vertices_z[face_b],
                    color_b,
                    u_b,
                    v_b,
                    -1.0f);
                trspk_vbo_write_vertex_opengl3(
                    renderer->vbo_cpu,
                    i + 2,
                    model->vertices_x[face_c],
                    model->vertices_y[face_c],
                    model->vertices_z[face_c],
                    color_c,
                    u_c,
                    v_c,
                    -1.0f);
            }

            glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
            glBufferData(
                GL_ARRAY_BUFFER,
                renderer->vbo_cpu->vertex_count * sizeof(struct TRSPK_VertexOpenGl3),
                renderer->vbo_cpu->vertices.as_opengl3,
                GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    }
    break;
    case TORIRSRC_MODEL_UNLOAD:
        break;

    case TORIRSRC_DRAW_MODEL:
    {
        (void)command->u.model.model;
        (void)command->u.model.position;
        (void)command->u.model.element_id;
        if( !renderer->has_3d || !renderer->vbo_cpu || renderer->vbo_cpu->vertex_count == 0u )
            break;

        struct ToriDraw_Context* ctx = get_context(instance);
        if( !ctx )
            break;

        uint32_t face_count = toridraw_face_order_count(ctx);
        if( face_count <= 0 )
            break;

        int* face_order = toridraw_face_order(ctx);
        for( uint32_t i = 0; i < face_count; i++ )
        {
            uint32_t face = face_order[i];

            // Calculate the base index in the VBO where this face's vertices start
            uint16_t vertex_base = (uint16_t)(face * 3);

            // Write the 3 vertex indices that form this specific triangle face
            renderer->ibo_16[i * 3 + 0] = vertex_base + 0;
            renderer->ibo_16[i * 3 + 1] = vertex_base + 1;
            renderer->ibo_16[i * 3 + 2] = vertex_base + 2;
        }

        // The size passed to glDrawElements must be the total number of indices, not faces
        renderer->ibo_16_size = face_count * 3;

        // 1. Bind the buffer directly to update its data
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);

        // 2. "Orphan" the buffer: Tells the driver it can discard the old memory
        // and give us a fresh block immediately without waiting for the GPU to finish drawing.
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER, TRSPK_EBO_SIZE * sizeof(uint16_t), NULL, GL_DYNAMIC_DRAW);

        // 3. Shovel the new index data into the fresh block
        glBufferSubData(
            GL_ELEMENT_ARRAY_BUFFER, 0, renderer->ibo_16_size * sizeof(uint16_t), renderer->ibo_16);

        glBindVertexArray(renderer->program3d_vao);
        glDrawElements(GL_TRIANGLES, renderer->ibo_16_size, GL_UNSIGNED_SHORT, 0);
        glBindVertexArray(0);

        break;
    }
    default:
        break;
    }
}

void
LibToriPlatformSDL2_RendererGL3_Render(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance)
{
    (void)instance;
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
