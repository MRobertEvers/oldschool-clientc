#include "platform_sdl2_renderer_webgl1.h"

#include "graphics/uv_pnm.h"
#include "libtorirs.h"
#include "platformkit/core/trspk_ibo.h"
#include "platformkit/core/trspk_modelarena.h"
#include "platformkit/core/trspk_pose.h"
#include "platformkit/core/trspk_vbochain16.h"
#include "platformkit/webgl1/trspk_webgl1.h"
#include "platforms/trspk_toridraw.h"
#include "render/libtorirs_render.h"
#include "toridraw/toridraw.h"
#include <GLES2/gl2.h>

#include <SDL.h>
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRSPK_WEBGL1_GPU_IBO_INIT 4096u
#define TRSPK_WEBGL1_ENCODE_PAGE_LOCAL(page, local) (((page) << 16u) | ((local)&0xFFFFu))
#define TRSPK_WEBGL1_BASE_PAGE(encoded) ((encoded) >> 16u)
#define TRSPK_WEBGL1_BASE_LOCAL(encoded) ((encoded)&0xFFFFu)

struct LibToriPlatformSDL2_RendererWebGL1
{
    SDL_Window* window;
    SDL_GLContext gl_context;
    int width;
    int height;

    struct TRSPK_VBOChain16* vbo_chain;
    struct TRSPK_ModelArena* model_arena;
    struct TRSPK_PoseTable poses;
    struct TRSPK_IBOChain* ibo_chain;

    GLuint* page_buffers;
    uint32_t page_buffer_count;

    GLuint atlas_texture;
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

    GLuint ebo;
    uint32_t gpu_ibo_capacity;

    float view[16];
    float proj[16];
    struct LibToriRS_RenderCommand_Begin3D cur_3d;
    bool has_3d;
    bool in3d;
    double frame_clock;
};

static void
webgl1_destroy_gl_resources(struct LibToriPlatformSDL2_RendererWebGL1* renderer)
{
    if( renderer->program3d )
        glDeleteProgram(renderer->program3d);
    if( renderer->page_buffers && renderer->page_buffer_count > 0u )
        glDeleteBuffers((GLsizei)renderer->page_buffer_count, renderer->page_buffers);
    if( renderer->ebo )
        glDeleteBuffers(1, &renderer->ebo);
    if( renderer->atlas_texture )
        glDeleteTextures(1, &renderer->atlas_texture);
    renderer->program3d = 0u;
    renderer->ebo = 0u;
    renderer->atlas_texture = 0u;
    free(renderer->page_buffers);
    renderer->page_buffers = NULL;
    renderer->page_buffer_count = 0u;
}

static struct ToriDraw_Model*
get_model(struct ToriDraw_ModelHandle model_handle)
{
    return model_handle.u.model.model;
}

static struct ToriDraw_Context*
get_context(struct LibToriRS_Instance* instance)
{
    return LibToriRS_GetCurrentToriDrawContext(instance);
}

static void
uv_pnm(
    struct UVFaceCoords* uv_pnm_out,
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
        uv_pnm_out,
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
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    uint32_t page_count)
{
    if( page_count <= renderer->page_buffer_count )
        return;

    GLuint* grown =
        (GLuint*)realloc(renderer->page_buffers, page_count * sizeof(GLuint));
    assert(grown != NULL);

    for( uint32_t i = renderer->page_buffer_count; i < page_count; ++i )
    {
        grown[i] = 0u;
        glGenBuffers(1, &grown[i]);
    }

    renderer->page_buffers = grown;
    renderer->page_buffer_count = page_count;
}

static void
webgl1_upload_pages_if_dirty(struct LibToriPlatformSDL2_RendererWebGL1* renderer)
{
    if( !renderer->vbo_chain )
        return;

    const uint32_t page_count = renderer->vbo_chain->page_count;
    if( page_count == 0u )
        return;

    webgl1_ensure_page_buffers(renderer, page_count);

    for( uint32_t i = 0u; i < page_count; ++i )
    {
        struct TRSPK_VBO* page_vbo = renderer->vbo_chain->pages[i];
        if( !page_vbo || !trspk_vbo_is_dirty(page_vbo) || page_vbo->vertex_count == 0u )
            continue;

        glBindBuffer(GL_ARRAY_BUFFER, renderer->page_buffers[i]);
        glBufferData(
            GL_ARRAY_BUFFER,
            page_vbo->vertex_count * sizeof(struct TRSPK_VertexWebGL1),
            page_vbo->vertices.as_webgl1,
            GL_STATIC_DRAW);
        trspk_vbo_clear_dirty(page_vbo);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
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

static void
webgl1_bake_into_arena(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    int element_id,
    int pose_id,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position)
{
    struct ToriDraw_Model* model = get_model(model_handle);
    if( !model || model->face_count <= 0 || !renderer->model_arena || !renderer->vbo_chain )
        return;

    const uint32_t vert_count = (uint32_t)model->face_count * 3u;
    const uint32_t tri_count = (uint32_t)model->face_count;

    const uint32_t existing_slot =
        trspk_modelarena_find(renderer->model_arena, element_id, pose_id);
    if( existing_slot != TRSPK_MODELSLOT_NULL_IDX )
        trspk_modelarena_unload(renderer->model_arena, existing_slot);

    const uint32_t slot_index =
        trspk_modelarena_load(renderer->model_arena, element_id, pose_id, vert_count);
    const struct TRSPK_ModelSlot* model_slot =
        trspk_modelarena_get(renderer->model_arena, slot_index);
    if( !model_slot )
        return;

    struct TRSPK_VBO* page_vbo = renderer->vbo_chain->pages[model_slot->page];
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
        const uint8_t alpha = model->face_alphas ? model->face_alphas[face_index] : 0xFFu;
        float color_a[4], color_b[4], color_c[4];
        hsl16_to_rgba(color_a_hsl16, alpha, color_a);
        hsl16_to_rgba(color_b_hsl16, alpha, color_b);
        hsl16_to_rgba(color_c_hsl16, alpha, color_c);
        struct UVFaceCoords uv_coords;
        uv_pnm(&uv_coords, model, face_index);

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

        trspk_vbo_write_vertex_webgl1(
            page_vbo,
            vi + 0u,
            wx_a,
            wy_a,
            wz_a,
            color_a,
            uv_coords.u1,
            uv_coords.v1,
            TRSPK_VERTEX_WEBGL1_TEXID_INVALID);
        trspk_vbo_write_vertex_webgl1(
            page_vbo,
            vi + 1u,
            wx_b,
            wy_b,
            wz_b,
            color_b,
            uv_coords.u2,
            uv_coords.v2,
            TRSPK_VERTEX_WEBGL1_TEXID_INVALID);
        trspk_vbo_write_vertex_webgl1(
            page_vbo,
            vi + 2u,
            wx_c,
            wy_c,
            wz_c,
            color_c,
            uv_coords.u3,
            uv_coords.v3,
            TRSPK_VERTEX_WEBGL1_TEXID_INVALID);
    }

    trspk_pose_table_set(
        &renderer->poses,
        element_id,
        pose_id,
        TRSPK_WEBGL1_ENCODE_PAGE_LOCAL(model_slot->page, model_slot->vertex_base));
}

static void
webgl1_ev_end_3d(struct LibToriPlatformSDL2_RendererWebGL1* renderer)
{
    if( !renderer->has_3d )
        goto done;

    webgl1_upload_pages_if_dirty(renderer);

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

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        (GLsizeiptr)(renderer->gpu_ibo_capacity * sizeof(uint16_t)),
        NULL,
        GL_DYNAMIC_DRAW);

    uint32_t cursor = 0u;
    for( struct TRSPK_IBOChainNode* node = renderer->ibo_chain->head; node != NULL;
         node = node->next )
    {
        const uint32_t count = node->ibo.index_count;
        if( count == 0u )
            continue;

        const uint32_t page = node->ibo.offset;
        if( page >= renderer->page_buffer_count || renderer->page_buffers[page] == 0u )
            continue;

        glBufferSubData(
            GL_ELEMENT_ARRAY_BUFFER,
            (GLintptr)(cursor * sizeof(uint16_t)),
            (GLsizeiptr)(count * sizeof(uint16_t)),
            node->ibo.indices.as_u16);

        bind_vbo_attribs(renderer, renderer->page_buffers[page]);
        glDrawElements(
            GL_TRIANGLES,
            (GLsizei)count,
            GL_UNSIGNED_SHORT,
            (const void*)(uintptr_t)(cursor * sizeof(uint16_t)));

        cursor += count;
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
        compute_pass_matrices(
            renderer->view, renderer->proj, &renderer->cur_3d, renderer->width, renderer->height);
        upload_world_uniforms(renderer, renderer->view, renderer->proj);
        break;
    case TORIRSRC_END_3D:
        webgl1_ev_end_3d(renderer);
        break;
    case TORIRSRC_MODEL_LOAD:
        webgl1_bake_into_arena(
            renderer,
            command->u.model_load.element_id,
            0,
            command->u.model_load.model,
            &command->u.model_load.world_position);
        break;
    case TORIRSRC_MODEL_UNLOAD:
        trspk_modelarena_unload_element(renderer->model_arena, command->u.model_load.element_id);
        trspk_pose_table_remove_element(&renderer->poses, command->u.model_load.element_id);
        break;
    case TORIRSRC_BATCH3D_BEGIN:
        break;
    case TORIRSRC_BATCH3D_MODEL_ADD:
        webgl1_bake_into_arena(
            renderer,
            command->u.batch.element_id,
            command->u.batch.pose_id,
            command->u.batch.model,
            &command->u.batch.world_position);
        break;
    case TORIRSRC_BATCH3D_ANIM_ADD:
        webgl1_bake_into_arena(
            renderer,
            command->u.batch.element_id,
            command->u.batch.pose_id,
            command->u.batch.model,
            &command->u.batch.world_position);
        break;
    case TORIRSRC_BATCH3D_END:
        break;
    case TORIRSRC_BATCH3D_CLEAR:
        trspk_pose_table_clear(&renderer->poses);
        if( renderer->model_arena )
            trspk_modelarena_clear(renderer->model_arena);
        break;
    case TORIRSRC_DRAW_MODEL:
    {
        if( !renderer->has_3d || !renderer->ibo_chain )
            break;

        struct ToriDraw_Context* ctx = get_context(instance);
        if( !ctx )
            break;

        const int pose_id = command->u.model.anim_frame;
        uint32_t encoded_base = 0u;
        if( !trspk_pose_table_get(
                &renderer->poses, command->u.model.element_id, pose_id, &encoded_base) )
            break;

        const uint32_t page = TRSPK_WEBGL1_BASE_PAGE(encoded_base);
        const uint32_t local_base = TRSPK_WEBGL1_BASE_LOCAL(encoded_base);

        const int face_count = toridraw_face_order_count(ctx);
        if( face_count <= 0 )
            break;

        int* face_order = toridraw_face_order(ctx);
        for( int i = 0; i < face_count; i++ )
        {
            const uint32_t face = (uint32_t)face_order[i];
            const uint16_t b = (uint16_t)(local_base + face * 3u);
            const uint16_t idx[3] = { b, (uint16_t)(b + 1u), (uint16_t)(b + 2u) };
            trspk_ibochain_push16(renderer->ibo_chain, page, idx, 3u);
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

    renderer->vbo_chain = trspk_vbochain16_create(TRSPK_VERTEX_FORMAT_WEBGL1);
    if( !renderer->vbo_chain )
    {
        free(renderer);
        return NULL;
    }

    renderer->model_arena =
        trspk_modelarena_create_chain16(renderer->vbo_chain, NULL, 64u);
    if( !renderer->model_arena )
    {
        trspk_vbochain16_free(renderer->vbo_chain);
        free(renderer);
        return NULL;
    }

    renderer->ibo_chain = trspk_ibochain_create(TRSPK_INDEX_FORMAT_U16);
    if( !renderer->ibo_chain )
    {
        trspk_modelarena_free(renderer->model_arena);
        trspk_vbochain16_free(renderer->vbo_chain);
        free(renderer);
        return NULL;
    }

    trspk_pose_table_init(&renderer->poses);

    return renderer;
}

void
LibToriPlatformSDL2_RendererWebGL1_Free(struct LibToriPlatformSDL2_RendererWebGL1* renderer)
{
    if( !renderer )
        return;
    if( renderer->window && renderer->gl_context )
        SDL_GL_MakeCurrent(renderer->window, renderer->gl_context);
    webgl1_destroy_gl_resources(renderer);
    if( renderer->model_arena )
        trspk_modelarena_free(renderer->model_arena);
    trspk_pose_table_free(&renderer->poses);
    if( renderer->ibo_chain )
        trspk_ibochain_free(renderer->ibo_chain);
    if( renderer->vbo_chain )
        trspk_vbochain16_free(renderer->vbo_chain);
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

    glGenTextures(1, &renderer->atlas_texture);
    glBindTexture(GL_TEXTURE_2D, renderer->atlas_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

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
    renderer->frame_clock += 1.0 / 50.0;

    LibToriRS_FrameBegin(instance);
    struct LibToriRS_RenderCommand command;
    while( LibToriRS_FrameNextCommand(instance, &command) )
        webgl1_handle_render_command(renderer, instance, &command);
    LibToriRS_FrameEnd(instance);

    SDL_GL_SwapWindow(renderer->window);
}
