#include "platform_sdl2_renderer_webgl1.h"

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
#define TRSPK_WEBGL1_ATLAS_TILE 128u
#define TRSPK_WEBGL1_ENCODE_PAGE_LOCAL(page, local) (((page) << 16u) | ((local)&0xFFFFu))
#define TRSPK_WEBGL1_BASE_PAGE(encoded) ((encoded) >> 16u)
#define TRSPK_WEBGL1_BASE_LOCAL(encoded) ((encoded)&0xFFFFu)

struct WebGL1DrawRecord
{
    uint32_t page;
    uint32_t draw_offset;
    uint32_t count;
};

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

static struct ToriDraw_Scene*
get_context(struct LibToriRS_Instance* instance)
{
    return LibToriRS_GetCurrentToriDrawScene(instance);
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
    uint32_t rgb = ToriDraw_Hsl16ToRgb(hsl16);
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
        ToriDraw_AngleToRadians(cam->pitch),
        ToriDraw_AngleToRadians(cam->yaw));
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

static void
webgl1_decode_texture_rgba(
    const struct ToriDraw_Texture* tex,
    uint8_t* out_rgba)
{
    const uint32_t src_w = (uint32_t)tex->width;
    const uint32_t src_h = (uint32_t)tex->height;

    memset(out_rgba, 0, TRSPK_WEBGL1_ATLAS_TILE * TRSPK_WEBGL1_ATLAS_TILE * 4u);
    if( src_w == 0u || src_h == 0u )
        return;

    for( uint32_t dst_row = 0; dst_row < TRSPK_WEBGL1_ATLAS_TILE; dst_row++ )
    {
        uint32_t src_row = (dst_row * src_h) / TRSPK_WEBGL1_ATLAS_TILE;
        for( uint32_t dst_col = 0; dst_col < TRSPK_WEBGL1_ATLAS_TILE; dst_col++ )
        {
            uint32_t src_col = (dst_col * src_w) / TRSPK_WEBGL1_ATLAS_TILE;
            int texel = tex->texels[src_row * src_w + src_col];
            uint8_t rv = (uint8_t)((texel >> 16) & 0xFF);
            uint8_t gv = (uint8_t)((texel >> 8) & 0xFF);
            uint8_t bv = (uint8_t)(texel & 0xFF);
            uint8_t av = (tex->opaque || texel != 0) ? 255u : 0u;
            uint32_t idx = (dst_row * TRSPK_WEBGL1_ATLAS_TILE + dst_col) * 4u;
            out_rgba[idx + 0] = rv;
            out_rgba[idx + 1] = gv;
            out_rgba[idx + 2] = bv;
            out_rgba[idx + 3] = av;
        }
    }
}

static float
webgl1_pack_uv_mode(
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
webgl1_encode_tex_id(
    int tex_id,
    const struct ToriDraw_Texture* tex)
{
    if( tex_id < 0 )
        return TRSPK_VERTEX_WEBGL1_TEXID_INVALID;
    if( tex && !tex->opaque )
        return (float)(tex_id + 256);
    return (float)tex_id;
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

    static uint8_t rgba_scratch[TRSPK_WEBGL1_ATLAS_TILE * TRSPK_WEBGL1_ATLAS_TILE * 4];
    webgl1_decode_texture_rgba(tex, rgba_scratch);

    trspk_atlas_grid_insert_at(
        &renderer->atlas,
        (uint32_t)tex_id,
        rgba_scratch,
        TRSPK_WEBGL1_ATLAS_TILE * 4u,
        TRSPK_WEBGL1_ATLAS_TILE,
        TRSPK_WEBGL1_ATLAS_TILE,
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
    uint32_t page,
    uint32_t draw_offset,
    uint32_t count)
{
    if( !webgl1_ensure_draw_records(renderer, renderer->draw_record_count + 1u) )
        return false;

    struct WebGL1DrawRecord* rec = &renderer->draw_records[renderer->draw_record_count++];
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

static void
webgl1_bake_into_arena(
    struct LibToriPlatformSDL2_RendererWebGL1* renderer,
    struct LibToriRS_Instance* instance,
    int element_id,
    int pose_id,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position)
{
    struct ToriDraw_Model* model = get_model(model_handle);
    if( !model || model->face_count <= 0 || !renderer->model_arena || !renderer->vbo_chain )
        return;

    struct ToriDraw_Scene* ctx = get_context(instance);
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
                uv_mode = webgl1_pack_uv_mode(tex->animation_direction, tex->animation_speed);
        }

        const float tex_id_encoded = webgl1_encode_tex_id(tex_id, tex);
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

        webgl1_write_vertex(
            page_vbo,
            vi + 0u,
            wx_a,
            wy_a,
            wz_a,
            color_a,
            uv_coords.u1,
            uv_coords.v1,
            tex_id_encoded,
            uv_mode);
        webgl1_write_vertex(
            page_vbo,
            vi + 1u,
            wx_b,
            wy_b,
            wz_b,
            color_b,
            uv_coords.u2,
            uv_coords.v2,
            tex_id_encoded,
            uv_mode);
        webgl1_write_vertex(
            page_vbo,
            vi + 2u,
            wx_c,
            wy_c,
            wz_c,
            color_c,
            uv_coords.u3,
            uv_coords.v3,
            tex_id_encoded,
            uv_mode);
    }

    trspk_pose_table_set(
        &renderer->poses,
        element_id,
        0,
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
        if( page >= renderer->page_buffer_count || renderer->page_buffers[page] == 0u )
            continue;

        memcpy(
            renderer->ibo_staging + cursor,
            node->ibo.indices.as_u16,
            count * sizeof(uint16_t));

        if( !webgl1_push_draw_record(renderer, page, cursor, count) )
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
        bind_vbo_attribs(renderer, renderer->page_buffers[rec->page]);
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
        webgl1_bake_into_arena(renderer, instance, element_id, frame, baked_handle, world_position);
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
        compute_pass_matrices(
            renderer->view, renderer->proj, &renderer->cur_3d, renderer->width, renderer->height);
        upload_world_uniforms(renderer, renderer->view, renderer->proj);

        if( trspk_atlas_is_dirty(&renderer->atlas) )
            webgl1_upload_atlas_texture(renderer);
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
            instance,
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
            instance,
            command->u.batch.element_id,
            command->u.batch.pose_id,
            command->u.batch.model,
            &command->u.batch.world_position);
        break;
    case TORIRSRC_BATCH3D_ANIM_ADD:
        webgl1_bake_into_arena(
            renderer,
            instance,
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

        struct ToriDraw_Scene* ctx = get_context(instance);
        if( !ctx )
            break;

        const int pose_id = command->u.model.anim_frame;
        if( command->u.model.dynamic )
        {
            webgl1_bake_into_arena(
                renderer,
                instance,
                command->u.model.element_id,
                pose_id,
                command->u.model.model,
                &command->u.model.world_position);
        }

        uint32_t encoded_base = 0u;
        if( !trspk_pose_table_get(
                &renderer->poses, command->u.model.element_id, 0, pose_id, &encoded_base) )
            break;

        const uint32_t page = TRSPK_WEBGL1_BASE_PAGE(encoded_base);
        const uint32_t local_base = TRSPK_WEBGL1_BASE_LOCAL(encoded_base);

        const int face_count = ToriDraw_FaceOrderCount(ctx);
        if( face_count <= 0 )
            break;

        int* face_order = ToriDraw_FaceOrder(ctx);
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
            TRSPK_WEBGL1_ATLAS_TILE,
            TRSPK_WEBGL1_ATLAS_TILE,
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
